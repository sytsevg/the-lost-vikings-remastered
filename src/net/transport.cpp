#include "net/transport.hpp"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <random>
#include <thread>
#include <utility>

namespace lvr::net {

namespace {

// What a frame carries (its first byte after the length on TCP, after the token over UDP).
constexpr std::uint8_t kMessage = 0;  // a message sent reliably
constexpr std::uint8_t kFast = 1;     // a message sent fast (over TCP: the fallback)
constexpr std::uint8_t kToken = 2;    // host to client, over TCP: the token his UDP introduces itself with
constexpr std::uint8_t kPing = 3;
constexpr std::uint8_t kPong = 4;
constexpr std::uint8_t kHello = 5;    // client to host, over UDP: here is my UDP (until welcomed)
constexpr std::uint8_t kWelcome = 6;  // host to client, over UDP: it got through

constexpr std::uint64_t kPingEvery = 500;    // milliseconds
constexpr std::uint64_t kHelloEvery = 200;   // the client's UDP introducing itself, until it is welcomed
constexpr std::uint64_t kQuietUdp = 2000;    // nothing over UDP for this long: the fast messages go over TCP
constexpr std::size_t kMostDatagram = 1200;  // a fast message longer than this goes over TCP
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

int net_users = 0;  // NET_Init is counted: the transports alive

bool start_net() {
    if (net_users == 0 && !NET_Init()) {
        return false;
    }
    ++net_users;
    return true;
}

void stop_net() {
    if (--net_users == 0) {
        NET_Quit();
    }
}

}  // namespace

struct Transport::Inner {
    struct Peer {
        int number = 0;
        NET_StreamSocket* stream = nullptr;
        std::vector<std::uint8_t> read;  // what came over TCP that isn't a whole frame yet
        std::uint32_t token = 0;
        NET_Address* udp_address = nullptr;
        std::uint16_t udp_port = 0;
        bool udp_ok = false;
        std::uint64_t udp_last = 0;  // when UDP from it last came
        std::uint64_t ping_last = 0;
        std::uint64_t hello_last = 0;
        std::optional<int> round_trip;
        bool gone = false;
    };

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
    std::vector<Peer> peers;  // the host's clients; a client's one, the host
    int next_number = 1;
    std::mt19937 tokens{std::random_device{}()};
    int loss = 0;  // tests: the datagrams lost, in percent

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
            peer.gone = true;
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
        (void)NET_SendDatagram(datagrams, peer.udp_address, peer.udp_port, datagram.data(),
                               static_cast<int>(datagram.size()));
    }

    // A ping or a fast message, by UDP while it gets through, else over TCP.
    void write_fast(Peer& peer, std::uint8_t kind, std::span<const std::uint8_t> payload) {
        if (peer.udp_ok && payload.size() <= kMostDatagram) {
            write_udp(peer, kind, payload);
        } else {
            write_tcp(peer, kind, payload);
        }
    }

    // A frame's kind and payload, from either connection.
    void take(Peer& peer, std::uint8_t kind, std::span<const std::uint8_t> payload, std::vector<Event>& events) {
        switch (kind) {
            case kMessage:
            case kFast:
                events.push_back({.kind = Event::Kind::kMessage,
                                  .peer = peer.number,
                                  .bytes = std::vector<std::uint8_t>(payload.begin(), payload.end())});
                return;
            case kToken:
                if (!hosting) {
                    peer.token = get32(payload);
                }
                return;
            case kPing:
                write_fast(peer, kPong, payload);
                return;
            case kPong:
                peer.round_trip = static_cast<int>(static_cast<std::uint32_t>(SDL_GetTicks()) - get32(payload));
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
                peer.gone = true;
                break;
            }
            if (got == 0) {
                break;
            }
            peer.read.insert(peer.read.end(), buffer.begin(), buffer.begin() + got);
        }
        std::size_t at = 0;
        while (peer.read.size() - at >= kHeader) {
            const std::uint32_t length = get32(std::span{peer.read}.subspan(at));
            if (length == 0 || length > kMostFrame) {
                peer.gone = true;
                break;
            }
            if (peer.read.size() - at < 4 + std::size_t{length}) {
                break;
            }
            const std::uint8_t kind = peer.read[at + 4];
            take(peer, kind, std::span{peer.read}.subspan(at + kHeader, length - 1), events);
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
                std::vector<std::uint8_t> token;
                put32(token, peer.token);
                write_tcp(peer, kToken, token);
                events.push_back({.kind = Event::Kind::kJoined, .peer = peer.number, .bytes = {}});
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
            if (peer.udp_ok && now - peer.udp_last > kQuietUdp) {
                peer.udp_ok = false;  // the fast messages over TCP until UDP comes through again
            }
            if (!hosting && !peer.udp_ok && peer.token != 0 && now - peer.hello_last >= kHelloEvery) {
                write_udp(peer, kHello, {});
                peer.hello_last = now;
            }
            if (now - peer.ping_last >= kPingEvery) {
                std::vector<std::uint8_t> sent;
                put32(sent, static_cast<std::uint32_t>(now));
                write_fast(peer, kPing, sent);
                if (peer.udp_ok) {
                    write_tcp(peer, kPing, sent);  // (the TCP connection too, so it is known to be there)
                }
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
                    if (hosting && kind == kHello) {  // his UDP: where to send to
                        if (peer->udp_address != nullptr) {
                            NET_UnrefAddress(peer->udp_address);
                        }
                        peer->udp_address = NET_RefAddress(datagram->addr);
                        peer->udp_port = datagram->port;
                        peer->udp_ok = true;
                        write_udp(*peer, kWelcome, {});
                    } else if (!hosting && kind == kWelcome) {
                        peer->udp_ok = true;
                    } else {
                        take(*peer, kind, bytes.subspan(kHeader), events);
                    }
                    peer->udp_last = now;
                }
            }
            NET_DestroyDatagram(datagram);
            datagram = nullptr;
        }
    }
};

Transport::Transport(std::unique_ptr<Inner> inner) : inner_{std::move(inner)} {
    inner_->start_pumping();
}

Transport::~Transport() = default;

std::expected<std::unique_ptr<Transport>, std::string> Transport::host(std::uint16_t port) {
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
    return std::unique_ptr<Transport>{new Transport{std::move(inner)}};
}

std::expected<std::unique_ptr<Transport>, std::string> Transport::join(const std::string& address, std::uint16_t port,
                                                                       int wait_ms) {
    if (!start_net()) {
        return std::unexpected{std::string{"the network: "} + SDL_GetError()};
    }
    auto inner = std::make_unique<Inner>();
    inner->port = port;
    NET_Address* host = NET_ResolveHostname(address.c_str());
    if (host == nullptr || NET_WaitUntilResolved(host, wait_ms) != NET_SUCCESS) {
        const std::string why = SDL_GetError();
        if (host != nullptr) {
            NET_UnrefAddress(host);
        }
        return std::unexpected{std::format("{}: not found ({})", address, why)};
    }
    Inner::Peer peer;
    peer.number = 0;
    peer.stream = NET_CreateClient(host, port, 0);
    peer.udp_address = host;  // (the reference resolving gave)
    peer.udp_port = port;
    if (peer.stream == nullptr || NET_WaitUntilConnected(peer.stream, wait_ms) != NET_SUCCESS) {
        const std::string why = SDL_GetError();
        Inner::close(peer);
        return std::unexpected{std::format("{} port {}: no game there ({})", address, port, why)};
    }
    inner->datagrams = NET_CreateDatagramSocket(nullptr, 0, 0);
    inner->peers.push_back(peer);
    if (inner->datagrams == nullptr) {
        return std::unexpected{std::format("UDP: {}", SDL_GetError())};
    }
    return std::unique_ptr<Transport>{new Transport{std::move(inner)}};
}

std::vector<Event> Transport::poll() {
    Inner& in = *inner_;
    const std::scoped_lock held{in.lock};
    std::erase_if(in.peers, [](const Inner::Peer& peer) { return peer.gone && peer.stream == nullptr; });
    return std::exchange(in.gathered, {});
}

void Transport::send(int peer, std::span<const std::uint8_t> bytes, Channel channel) {
    const std::scoped_lock held{inner_->lock};
    if (Inner::Peer* to = inner_->find(peer)) {
        if (channel == Channel::kReliable) {
            inner_->write_tcp(*to, kMessage, bytes);
        } else {
            inner_->write_fast(*to, kFast, bytes);
        }
    }
}

void Transport::drop(int peer) {
    const std::scoped_lock held{inner_->lock};
    if (Inner::Peer* gone = inner_->find(peer)) {
        gone->gone = true;
    }
}

std::optional<int> Transport::round_trip_ms(int peer) const {
    const std::scoped_lock held{inner_->lock};
    const auto found = std::ranges::find(inner_->peers, peer, &Inner::Peer::number);
    return found != inner_->peers.end() ? found->round_trip : std::nullopt;
}

bool Transport::fast_link(int peer) const {
    const std::scoped_lock held{inner_->lock};
    const auto found = std::ranges::find(inner_->peers, peer, &Inner::Peer::number);
    return found != inner_->peers.end() && found->udp_ok;
}

void Transport::simulate_loss(int percent) {
    const std::scoped_lock held{inner_->lock};
    inner_->loss = percent;
    if (inner_->datagrams != nullptr) {
        NET_SimulateDatagramPacketLoss(inner_->datagrams, percent);
    }
}

bool Transport::hosting() const noexcept {
    return inner_->hosting;
}

std::uint16_t Transport::port() const noexcept {
    return inner_->port;
}

}  // namespace lvr::net
