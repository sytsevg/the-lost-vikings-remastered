#pragma once

// The transport for co-op online (ADR-0012): how the machines of a game online reach each other.

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lvr::net {

// What each promises. kReliable: it comes, in order. kFast, the game's own messages: it comes soon, or later, but
// is taken only if it is newer than the last taken from that peer, so never twice, nor after a newer one, and the
// newest always comes. Each must hold the newest of what it says (the game's: all it has to say to that peer, once a
// pass). (SdlTransport sends it by UDP and TCP both, the same numbered bytes, the first to come winning.)
enum class Channel : std::uint8_t { kReliable, kFast };

// What came since the last poll.
struct Event {
    enum class Kind : std::uint8_t {
        kJoined,   // host: a guest connected (`peer` his number, from 1 on)
        kMessage,  // `bytes` came from `peer` (on a guest: 0, the host)
        kLeft,     // the connection to `peer` is gone
    };
    Kind kind = Kind::kMessage;
    int peer = 0;
    std::vector<std::uint8_t> bytes;
};

// What the game online hears and says through, a machine's connections to the others: the host's to each
// guest, a guest's to the host. Two adapters: SdlTransport (the network, below) and net::SimulatedNetwork's (tests).
// Nothing here blocks: the game polls it every refresh.
class Transport {
  public:
    Transport() = default;
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;
    virtual ~Transport() = default;

    // What came since the last poll.
    virtual std::vector<Event> poll() = 0;
    // To one peer (on a guest, 0: the host).
    virtual void send(int peer, std::span<const std::uint8_t> bytes, Channel channel) = 0;
    // Host: lets a guest go (he is told; this machine's next poll has him left).
    virtual void drop(int peer) = 0;
    // Waits up to `wait_ms` for what was sent reliably to have gone (before this machine leaves the game).
    virtual void drain(int wait_ms) = 0;
    // The last round trip to the peer, in milliseconds, once it is known.
    virtual std::optional<int> round_trip_ms(int peer) const = 0;
    virtual bool hosting() const noexcept = 0;
    // Milliseconds on this machine's clock (only differences mean anything): what the game online times by.
    virtual std::uint32_t now_ms() const = 0;
};

// The transport on SDL3_net: between the host and each guest one TCP connection and UDP. A message goes reliably
// (over TCP: the handshake, a snapshot) or fast (UDP and TCP both, as Channel says). The guest's UDP is introduced to
// the host by a token the host gives it over TCP, which also finds the way through a router's NAT. Pings both ways
// give each one's round trip, answered by the network's own thread at once.
class SdlTransport final : public Transport {
  public:
    // Hosts a game on `port` (TCP and UDP both), for guests to join.
    static std::expected<std::unique_ptr<SdlTransport>, std::string> host(std::uint16_t port);
    // Joins the game hosted at `address` (a name or a number), waiting up to `wait_ms` for the name to be found and
    // as long for the connection; given up as soon as `cancelled` says so (it is looked at every 50 ms: joining may
    // run on a thread of its own).
    static std::expected<std::unique_ptr<SdlTransport>, std::string> join(const std::string& address,
                                                                          std::uint16_t port, int wait_ms = 5000,
                                                                          const std::atomic<bool>* cancelled = nullptr);
    SdlTransport(const SdlTransport&) = delete;
    SdlTransport& operator=(const SdlTransport&) = delete;
    SdlTransport(SdlTransport&&) = delete;
    SdlTransport& operator=(SdlTransport&&) = delete;
    ~SdlTransport() override;

    std::vector<Event> poll() override;
    // (A fast message too long for a datagram goes over TCP only.)
    void send(int peer, std::span<const std::uint8_t> bytes, Channel channel) override;
    void drop(int peer) override;
    void drain(int wait_ms) override;
    std::optional<int> round_trip_ms(int peer) const override;
    bool hosting() const noexcept override;
    std::uint32_t now_ms() const override;

    // Whether UDP gets through to the peer (else the fast messages go over TCP only).
    bool fast_link(int peer) const;
    // Tests of this adapter: that many percent of the datagrams lost, both ways (SDL3_net's own simulation); or
    // nothing this one sends getting through at all, TCP too (a cable pulled); and how long a silent peer is waited
    // for (10 s).
    void simulate_loss(int percent);
    void simulate_cut();
    void set_patience(std::uint64_t ms);
    std::uint16_t port() const noexcept;

  private:
    struct Inner;
    explicit SdlTransport(std::unique_ptr<Inner> inner);
    std::unique_ptr<Inner> inner_;
};

}  // namespace lvr::net
