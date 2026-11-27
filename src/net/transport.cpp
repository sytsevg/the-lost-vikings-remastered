#include "net/transport.hpp"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <random>
#include <source_location>
#include <string_view>
#include <thread>
#include <utility>

#include "log/log.hpp"

namespace lvr::net {

namespace {

// What a frame carries (its first byte after the length on TCP, after the token over UDP).
constexpr std::uint8_t kMessage = 0;  // a message sent reliably
constexpr std::uint8_t kFast = 1;     // a message sent fast: by UDP and TCP both, numbered (only a newer one taken)
constexpr std::uint8_t kToken = 2;    // host to guest, over TCP: the token his UDP introduces itself with
constexpr std::uint8_t kPing = 3;
constexpr std::uint8_t kPong = 4;
constexpr std::uint8_t kHello = 5;    // guest to host, over UDP: here is my UDP (until welcomed)
constexpr std::uint8_t kWelcome = 6;  // host to guest, over UDP: it got through

constexpr std::uint64_t kPingEvery = 500;        // milliseconds
constexpr std::uint64_t kHelloEvery = 200;       // the guest's UDP introducing itself, until it is welcomed
constexpr std::uint64_t kQuietUdp = 2000;        // nothing over UDP for this long: the fast messages go over TCP
constexpr std::uint64_t kMeasuredEvery = 10000;  // how a peer's connection goes, in the log this often
// Nothing at all from a peer for this long (its pings come twice a second): gone, though its socket wasn't closed
// (a cable pulled, the machine asleep, the program hung).
constexpr std::uint64_t kSilent = 10000;                    // (SdlTransport::set_patience: shorter, for a test)
constexpr std::size_t kMostDatagram = 1200;                 // a fast message longer than this goes over TCP
constexpr std::size_t kMostFrame = std::size_t{16} << 20U;  // a frame longer than this: the peer is broken
constexpr std::size_t kHeader = 5;                          // a frame's length (or a datagram's token) and kind

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        out.push_back(static_cast<std::uint8_t>(value >> (8U * static_cast<unsigned>(byte))));
    }
}

std::uint32_t get32(std::span<const std::uint8_t> from) {
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < 4 && byte < from.size(); ++byte) {
        value |= std::uint32_t{from[byte]} << (8U * byte);
    }
    return value;
}

// NET_Init is counted: the transports alive (one may be made on a thread of its own, joining).
std::mutex net_users_lock;
int net_users = 0;

bool start_net() {
    const std::scoped_lock held{net_users_lock};
    if (net_users == 0 && !NET_Init()) {
        return false;
    }
    ++net_users;
    return true;
}

void stop_net() {
    const std::scoped_lock held{net_users_lock};
    if (--net_users == 0) {
        NET_Quit();
    }
}

// Waits up to `wait_ms` for what `wait` waits on (it waits as long as it is given), a slice at a time, given up as
// soon as `cancelled` says so: whether it got there.
template <typename Wait>
bool waited(int wait_ms, const std::atomic<bool>* cancelled, const Wait& wait) {
    constexpr int kSlice = 50;  // milliseconds
    for (int spent = 0; spent < wait_ms; spent += kSlice) {
        if (cancelled != nullptr && cancelled->load()) {
            return false;
        }
        if (const NET_Status status = wait(kSlice); status != NET_WAITING) {
            return status == NET_SUCCESS;
        }
    }
    return false;
}

}  // namespace

struct SdlTransport::Inner {
    struct Peer {
        int number = 0;
        NET_StreamSocket* stream = nullptr;
        std::vector<std::uint8_t> read;  // what came over TCP that isn't a whole frame yet
        std::uint32_t token = 0;
        NET_Address* udp_address = nullptr;
        std::uint16_t udp_port = 0;
        bool udp_ok = false;
        std::uint64_t udp_last = 0;    // when UDP from it last came
        std::uint64_t heard_last = 0;  // when anything from it last came, by either way
        std::uint32_t pong_last = 0;   // the ping whose answer was taken last (its other copy isn't)
        std::uint32_t fast_sent = 0;   // the number of the last fast message sent to it
        std::uint32_t fast_taken = 0;  // and of the last taken from it: one not newer is a copy, or stale
        std::uint64_t ping_last = 0;
        std::uint64_t hello_last = 0;
        std::optional<int> round_trip;
        bool gone = false;
        log::Throttle udp_said{10000};  // (UDP that comes and goes is said once in a while, with how often)
        // Since `measured_since`, for the log: the round trips, which way what is sent both ways came first, and
        // the bytes each way (frames and datagrams as written and read).
        std::uint64_t measured_since = 0;
        std::vector<int> round_trips;
        int first_by_udp = 0;
        int first_by_tcp = 0;
        std::uint64_t bytes_out = 0;
        std::uint64_t bytes_in = 0;
    };

    // How a peer's connection went since it was last said: the round trips' p50, p90 and p99 (of a ping every half
    // second), which way the fast messages and the pings' answers came first, and the bytes a second; then anew.
    static void say_measured(Peer& peer, std::uint64_t now) {
        const double seconds = static_cast<double>(now - peer.measured_since) / 1000.0;
        std::string trips = "no round trip measured";
        if (std::vector<int>& measured = peer.round_trips; !measured.empty()) {
            std::ranges::sort(measured);
            const auto at = [&measured](double share) {  // (the nearest rank)
                const auto rank = static_cast<std::size_t>(std::ceil(share * static_cast<double>(measured.size())));
                return measured.at(std::max<std::size_t>(rank, 1) - 1);
            };
            trips = std::format("round trip p50 {} ms, p90 {} ms, p99 {} ms ({} measured)", at(0.5), at(0.9), at(0.99),
                                measured.size());
        }
        const int first = peer.first_by_udp + peer.first_by_tcp;
        const int by_udp = first == 0 ? 0 : peer.first_by_udp * 100 / first;
        log::info("peer {}: {}; {}; {:.1f} KB/s out, {:.1f} KB/s in", peer.number, trips,
                  first == 0 ? std::string{"nothing sent both ways"}
                             : std::format("first by UDP {}%, by TCP {}%", by_udp, 100 - by_udp),
                  static_cast<double>(peer.bytes_out) / 1024.0 / seconds,
                  static_cast<double>(peer.bytes_in) / 1024.0 / seconds);
        peer.measured_since = now;
        peer.round_trips.clear();
        peer.first_by_udp = 0;
        peer.first_by_tcp = 0;
        peer.bytes_out = 0;
        peer.bytes_in = 0;
    }

    // A peer's connection ends (once): why, in the log.
    static void lose(Peer& peer, std::string_view why, log::Level level = log::Level::kInfo,
                     const std::source_location& where = std::source_location::current()) {
        if (!peer.gone) {
            log::say(level, std::format("peer {}: {}", peer.number, why), where);
            peer.gone = true;
        }
    }

    // Whether the fast messages go by UDP to a peer: said when that changes.
    static void udp_through(Peer& peer, bool ok, std::uint64_t now) {
        if (peer.udp_ok == ok) {
            return;
        }
        peer.udp_ok = ok;
        if (const auto held_back = peer.udp_said.pass(static_cast<std::uint32_t>(now))) {
            const std::string more = *held_back == 0 ? "" : std::format(" ({} changes held back)", *held_back);
            if (ok) {
                log::info("peer {}: UDP gets through{}", peer.number, more);
            } else {
                log::warn("peer {}: nothing over UDP for {} s: the fast messages go over TCP{}", peer.number,
                          kQuietUdp / 1000, more);
            }
        }
    }

    // The network's own thread (pump): it reads every millisecond, answers pings at once, and keeps what came for
    // poll. Every socket is used under `lock`, by it or by the game's thread (send, poll).
    std::mutex lock;
    std::vector<Event> gathered;
    std::atomic<bool> stopping{false};
    std::thread pumping;

    bool hosting = false;
    std::uint16_t port = 0;
    NET_Server* server = nullptr;
    NET_DatagramSocket* datagrams = nullptr;
    std::vector<Peer> peers;  // the host's guests; a guest's one, the host
    int next_number = 1;
    std::mt19937 tokens{std::random_device{}()};
    int loss = 0;                      // tests: the datagrams lost, in percent
    std::uint64_t patience = kSilent;  // how long a silent peer is waited for

    Inner() = default;
    Inner(const Inner&) = delete;
    Inner& operator=(const Inner&) = delete;
    Inner(Inner&&) = delete;
    Inner& operator=(Inner&&) = delete;
    ~Inner() {
        stopping = true;
        if (pumping.joinable()) {
            pumping.join();
        }
        for (Peer& peer : peers) {
            close(peer);
        }
        if (server != nullptr) {
            NET_DestroyServer(server);
        }
        if (datagrams != nullptr) {
            NET_DestroyDatagramSocket(datagrams);
        }
        stop_net();
    }

    static void close(Peer& peer) {
        if (peer.stream != nullptr) {
            NET_DestroyStreamSocket(peer.stream);
            peer.stream = nullptr;
        }
        if (peer.udp_address != nullptr) {
            NET_UnrefAddress(peer.udp_address);
            peer.udp_address = nullptr;
        }
    }

    Peer* find(int number) {
        const auto found = std::ranges::find(peers, number, &Peer::number);
        return found != peers.end() && !found->gone ? &*found : nullptr;
    }

    void write_tcp(Peer& peer, std::uint8_t kind, std::span<const std::uint8_t> payload) {
        std::vector<std::uint8_t> frame;
        frame.reserve(kHeader + payload.size());
        put32(frame, static_cast<std::uint32_t>(payload.size() + 1));
        frame.push_back(kind);
        frame.insert(frame.end(), payload.begin(), payload.end());
        if (peer.stream == nullptr ||
            !NET_WriteToStreamSocket(peer.stream, frame.data(), static_cast<int>(frame.size()))) {
            lose(peer, std::format("the connection can't be written to: {}", SDL_GetError()));
        } else {
            peer.bytes_out += frame.size();
        }
    }

    void write_udp(Peer& peer, std::uint8_t kind, std::span<const std::uint8_t> payload) {
        if (peer.udp_address == nullptr || datagrams == nullptr) {
            return;
        }
        std::vector<std::uint8_t> datagram;
        datagram.reserve(kHeader + payload.size());
        put32(datagram, peer.token);
        datagram.push_back(kind);
        datagram.insert(datagram.end(), payload.begin(), payload.end());
        if (NET_SendDatagram(datagrams, peer.udp_address, peer.udp_port, datagram.data(),
                             static_cast<int>(datagram.size()))) {
            peer.bytes_out += datagram.size();
        }
    }

    // A ping or a fast message, by UDP while it gets through, else over TCP.
    // A fast message both ways, the same bytes (the first to come wins; UDP drops, reorders and doubles,
    // and TCP is there behind it): what takes it must take it twice as once.
    void write_fast(Peer& peer, std::uint8_t kind, std::span<const std::uint8_t> payload) {
        if (peer.udp_ok && payload.size() <= kMostDatagram) {
            write_udp(peer, kind, payload);
        }
        write_tcp(peer, kind, payload);
    }

    // A frame's kind and payload, from either connection (by UDP or by TCP).
    void take(Peer& peer, std::uint8_t kind, std::span<const std::uint8_t> payload, std::vector<Event>& events,
              bool by_udp) {
        int& first_by = by_udp ? peer.first_by_udp : peer.first_by_tcp;  // (what came both ways: its first copy)
        peer.heard_last = SDL_GetTicks();
        switch (kind) {
            case kMessage:
                events.push_back({.kind = Event::Kind::kMessage,
                                  .peer = peer.number,
                                  .bytes = std::vector<std::uint8_t>(payload.begin(), payload.end())});
                return;
            case kFast:  // (each holds the newest of what it says: one overtaken by a newer one has nothing to add)
                if (payload.size() >= 4 && get32(payload) > peer.fast_taken) {
                    peer.fast_taken = get32(payload);
                    ++first_by;
                    events.push_back({.kind = Event::Kind::kMessage,
                                      .peer = peer.number,
                                      .bytes = std::vector<std::uint8_t>(payload.begin() + 4, payload.end())});
                }
                return;
            case kToken:
                if (!hosting) {
                    peer.token = get32(payload);
                }
                return;
            case kPing:
                write_fast(peer, kPong, payload);
                return;
            case kPong:  // (the first of its copies: the later one isn't the round trip)
                if (const std::uint32_t sent = get32(payload); sent != peer.pong_last) {
                    peer.pong_last = sent;
                    peer.round_trip = static_cast<int>(static_cast<std::uint32_t>(SDL_GetTicks()) - sent);
                    peer.round_trips.push_back(*peer.round_trip);
                    ++first_by;
                }
                return;
            default:
                return;
        }
    }

    void read_stream(Peer& peer, std::vector<Event>& events) {
        std::array<std::uint8_t, 4096> buffer{};
        while (!peer.gone) {
            const int got = NET_ReadFromStreamSocket(peer.stream, buffer.data(), static_cast<int>(buffer.size()));
            if (got < 0) {
                lose(peer, "the connection closed");
                break;
            }
            if (got == 0) {
                break;
            }
            peer.bytes_in += static_cast<std::uint64_t>(got);
            peer.read.insert(peer.read.end(), buffer.begin(), buffer.begin() + got);
        }
        std::size_t at = 0;
        while (peer.read.size() - at >= kHeader) {
            const std::uint32_t length = get32(std::span{peer.read}.subspan(at));
            if (length == 0 || length > kMostFrame) {
                lose(peer, std::format("a frame of {} bytes can't be one: the connection dropped", length),
                     log::Level::kWarn);
                break;
            }
            if (peer.read.size() - at < 4 + std::size_t{length}) {
                break;
            }
            const std::uint8_t kind = peer.read[at + 4];
            take(peer, kind, std::span{peer.read}.subspan(at + kHeader, length - 1), events, false);
            at += 4 + std::size_t{length};
        }
        peer.read.erase(peer.read.begin(), peer.read.begin() + static_cast<std::ptrdiff_t>(at));
    }

    // A pass of the network's thread: who connected, what came, and the pings and introductions due.
    void pump_once() {
        std::vector<Event>& events = gathered;
        const std::uint64_t now = SDL_GetTicks();
        if (hosting) {
            NET_StreamSocket* stream = nullptr;
            while (NET_AcceptClient(server, &stream) && stream != nullptr) {
                Peer peer;
                peer.number = next_number++;
                peer.stream = stream;
                peer.token = static_cast<std::uint32_t>(tokens());
                peer.heard_last = now;
                std::vector<std::uint8_t> token;
                put32(token, peer.token);
                write_tcp(peer, kToken, token);
                events.push_back({.kind = Event::Kind::kJoined, .peer = peer.number, .bytes = {}});
                log::info("peer {}: a machine connected", peer.number);
                peers.push_back(peer);
                stream = nullptr;
            }
        }
        for (Peer& peer : peers) {
            if (!peer.gone) {
                read_stream(peer, events);
            }
        }
        read_datagrams(events, now);
        for (Peer& peer : peers) {
            if (peer.gone) {
                continue;
            }
            if (now > peer.heard_last + patience) {  // (heard_last can be later than `now`: what came this pass)
                lose(peer, std::format("nothing heard for {} s: the connection dropped", patience / 1000),
                     log::Level::kWarn);  // (left below, as one whose connection broke)
                continue;
            }
            if (peer.udp_ok && now - peer.udp_last > kQuietUdp) {
                udp_through(peer, false, now);  // the fast messages over TCP until UDP comes through again
            }
            if (peer.measured_since == 0) {
                peer.measured_since = now;
            } else if (now - peer.measured_since >= kMeasuredEvery) {
                say_measured(peer, now);
            }
            if (!hosting && !peer.udp_ok && peer.token != 0 && now - peer.hello_last >= kHelloEvery) {
                write_udp(peer, kHello, {});
                peer.hello_last = now;
            }
            if (now - peer.ping_last >= kPingEvery) {
                std::vector<std::uint8_t> sent;
                put32(sent, static_cast<std::uint32_t>(now));
                write_fast(peer, kPing, sent);  // (both ways: the TCP connection too is known to be there)
                peer.ping_last = now;
            }
        }
        for (Peer& peer : peers) {
            if (peer.gone && peer.stream != nullptr) {
                events.push_back({.kind = Event::Kind::kLeft, .peer = peer.number, .bytes = {}});
                close(peer);
            }
        }
    }

    void start_pumping() {
        pumping = std::thread{[this] {
            while (!stopping) {
                {
                    const std::scoped_lock held{lock};
                    pump_once();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            SDL_CleanupTLS();  // (what SDL kept for this thread, as for one of its own: the errors' text)
        }};
    }

    void read_datagrams(std::vector<Event>& events, std::uint64_t now) {
        NET_Datagram* datagram = nullptr;
        while (datagrams != nullptr && NET_ReceiveDatagram(datagrams, &datagram) && datagram != nullptr) {
            const std::span<const std::uint8_t> bytes{datagram->buf, static_cast<std::size_t>(datagram->buflen)};
            if (bytes.size() >= kHeader) {
                const std::uint32_t token = get32(bytes);
                const std::uint8_t kind = bytes[4];
                const auto peer = std::ranges::find_if(peers, [token](const Peer& one) { return one.token == token; });
                if (peer != peers.end() && !peer->gone) {
                    peer->bytes_in += bytes.size();
                    if (hosting && kind == kHello) {  // his UDP: where to send to
                        if (peer->udp_address != nullptr) {
                            NET_UnrefAddress(peer->udp_address);
                        }
                        peer->udp_address = NET_RefAddress(datagram->addr);
                        peer->udp_port = datagram->port;
                        udp_through(*peer, true, now);
                        write_udp(*peer, kWelcome, {});
                    } else if (!hosting && kind == kWelcome) {
                        udp_through(*peer, true, now);
                    } else {
                        take(*peer, kind, bytes.subspan(kHeader), events, true);
                    }
                    peer->udp_last = now;
                }
            }
            NET_DestroyDatagram(datagram);
            datagram = nullptr;
        }
    }
};

SdlTransport::SdlTransport(std::unique_ptr<Inner> inner) : inner_{std::move(inner)} {
    inner_->start_pumping();
}

SdlTransport::~SdlTransport() = default;

std::expected<std::unique_ptr<SdlTransport>, std::string> SdlTransport::host(std::uint16_t port) {
    if (!start_net()) {
        return std::unexpected{std::string{"the network: "} + SDL_GetError()};
    }
    auto inner = std::make_unique<Inner>();
    inner->hosting = true;
    inner->port = port;
    inner->server = NET_CreateServer(nullptr, port, 0);
    if (inner->server == nullptr) {
        return std::unexpected{std::format("hosting on port {}: {}", port, SDL_GetError())};
    }
    inner->datagrams = NET_CreateDatagramSocket(nullptr, port, 0);
    if (inner->datagrams == nullptr) {
        return std::unexpected{std::format("hosting on port {} (UDP): {}", port, SDL_GetError())};
    }
    return std::unique_ptr<SdlTransport>{new SdlTransport{std::move(inner)}};
}

std::expected<std::unique_ptr<SdlTransport>, std::string> SdlTransport::join(const std::string& address,
                                                                             std::uint16_t port, int wait_ms,
                                                                             const std::atomic<bool>* cancelled) {
    if (!start_net()) {
        return std::unexpected{std::string{"the network: "} + SDL_GetError()};
    }
    auto inner = std::make_unique<Inner>();
    inner->port = port;
    NET_Address* host = NET_ResolveHostname(address.c_str());
    if (host == nullptr ||
        !waited(wait_ms, cancelled, [host](int slice) { return NET_WaitUntilResolved(host, slice); })) {
        const std::string why = SDL_GetError();
        if (host != nullptr) {
            NET_UnrefAddress(host);
        }
        return std::unexpected{std::format("{}: not found ({})", address, why)};
    }
    Inner::Peer peer;
    peer.number = 0;
    peer.stream = NET_CreateClient(host, port, 0);
    peer.heard_last = SDL_GetTicks();
    peer.udp_address = host;  // (the reference resolving gave)
    peer.udp_port = port;
    if (peer.stream == nullptr ||
        !waited(wait_ms, cancelled, [&peer](int slice) { return NET_WaitUntilConnected(peer.stream, slice); })) {
        const std::string why = SDL_GetError();
        Inner::close(peer);
        return std::unexpected{std::format("{} port {}: no game there ({})", address, port, why)};
    }
    inner->datagrams = NET_CreateDatagramSocket(nullptr, 0, 0);
    inner->peers.push_back(peer);
    if (inner->datagrams == nullptr) {
        return std::unexpected{std::format("UDP: {}", SDL_GetError())};
    }
    return std::unique_ptr<SdlTransport>{new SdlTransport{std::move(inner)}};
}

std::vector<Event> SdlTransport::poll() {
    Inner& in = *inner_;
    const std::scoped_lock held{in.lock};
    std::erase_if(in.peers, [](const Inner::Peer& peer) { return peer.gone && peer.stream == nullptr; });
    return std::exchange(in.gathered, {});
}

void SdlTransport::send(int peer, std::span<const std::uint8_t> bytes, Channel channel) {
    const std::scoped_lock held{inner_->lock};
    if (Inner::Peer* to = inner_->find(peer)) {
        if (channel == Channel::kReliable) {
            inner_->write_tcp(*to, kMessage, bytes);
        } else {
            std::vector<std::uint8_t> numbered;  // (the same number on both copies)
            put32(numbered, ++to->fast_sent);
            numbered.insert(numbered.end(), bytes.begin(), bytes.end());
            inner_->write_fast(*to, kFast, numbered);
        }
    }
}

void SdlTransport::drain(int wait_ms) {
    const std::scoped_lock held{inner_->lock};
    for (Inner::Peer& peer : inner_->peers) {
        if (peer.stream != nullptr) {
            (void)NET_WaitUntilStreamSocketDrained(peer.stream, wait_ms);
        }
    }
}

void SdlTransport::drop(int peer) {
    const std::scoped_lock held{inner_->lock};
    if (Inner::Peer* gone = inner_->find(peer)) {
        Inner::lose(*gone, "let go");
    }
}

std::optional<int> SdlTransport::round_trip_ms(int peer) const {
    const std::scoped_lock held{inner_->lock};
    const auto found = std::ranges::find(inner_->peers, peer, &Inner::Peer::number);
    return found != inner_->peers.end() ? found->round_trip : std::nullopt;
}

bool SdlTransport::fast_link(int peer) const {
    const std::scoped_lock held{inner_->lock};
    const auto found = std::ranges::find(inner_->peers, peer, &Inner::Peer::number);
    return found != inner_->peers.end() && found->udp_ok;
}

void SdlTransport::simulate_cut() {
    const std::scoped_lock held{inner_->lock};
    inner_->loss = 100;
    if (inner_->datagrams != nullptr) {
        NET_SimulateDatagramPacketLoss(inner_->datagrams, 100);
    }
    for (Inner::Peer& peer : inner_->peers) {
        if (peer.stream != nullptr) {
            NET_SimulateStreamPacketLoss(peer.stream, 100);
        }
    }
}

void SdlTransport::set_patience(std::uint64_t ms) {
    const std::scoped_lock held{inner_->lock};
    inner_->patience = ms;
}

void SdlTransport::simulate_loss(int percent) {
    const std::scoped_lock held{inner_->lock};
    inner_->loss = percent;
    if (inner_->datagrams != nullptr) {
        NET_SimulateDatagramPacketLoss(inner_->datagrams, percent);
    }
}

bool SdlTransport::hosting() const noexcept {
    return inner_->hosting;
}

std::uint32_t SdlTransport::now_ms() const {
    return static_cast<std::uint32_t>(SDL_GetTicks());
}

std::uint16_t SdlTransport::port() const noexcept {
    return inner_->port;
}

}  // namespace lvr::net
