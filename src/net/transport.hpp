#pragma once

// The transport for co-op online (ADR-0012), on SDL3_net: between the host and each client one TCP connection and
// UDP. A message goes reliably (over TCP: the handshake, a snapshot) or fast (over UDP: the inputs, each of which
// carries what the other side hasn't acknowledged, so a datagram lost costs nothing; over the TCP connection instead
// while no UDP gets through, the fallback). The client's UDP is introduced to the host by a token the host gives it
// over TCP, which also finds the way through a router's NAT. Pings both ways give each one's round trip. Nothing
// here blocks but joining: the front end polls it every refresh.

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lvr::net {

// kReliable: TCP. kFast, the game's own messages: UDP and TCP both, the same bytes, numbered, the first to come wins
// (UDP while it gets through; it may drop, reorder or double them, and TCP is behind it): a fast message is taken
// only if it is newer than the last taken from that peer, so never twice, nor after a newer one. Each must hold the
// newest of what it says (the game's: all it has to say to that peer, once a pass).
enum class Channel : std::uint8_t { kReliable, kFast };

// What came since the last poll.
struct Event {
    enum class Kind : std::uint8_t {
        kJoined,   // host: a client connected (`peer` his number, from 1 on)
        kMessage,  // `bytes` came from `peer` (on a client: 0, the host)
        kLeft,     // the connection to `peer` is gone
    };
    Kind kind = Kind::kMessage;
    int peer = 0;
    std::vector<std::uint8_t> bytes;
};

class Transport {
  public:
    // Hosts a game on `port` (TCP and UDP both), for clients to join.
    static std::expected<std::unique_ptr<Transport>, std::string> host(std::uint16_t port);
    // Joins the game hosted at `address` (a name or a number), waiting up to `wait_ms` for the connection.
    static std::expected<std::unique_ptr<Transport>, std::string> join(const std::string& address, std::uint16_t port,
                                                                       int wait_ms = 5000);
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;
    ~Transport();

    // What came, and the pings and UDP introductions due. Never blocks.
    std::vector<Event> poll();
    // To one peer (on a client, 0: the host). A message too long for a datagram goes over TCP.
    void send(int peer, std::span<const std::uint8_t> bytes, Channel channel);
    // Host: lets a client go.
    void drop(int peer);
    // Waits up to `wait_ms` for what was sent reliably to have gone (before this machine leaves the game).
    void drain(int wait_ms);
    // The last round trip to the peer, in milliseconds, once a ping has come back.
    std::optional<int> round_trip_ms(int peer) const;
    // Whether UDP gets through to the peer (else the fast messages go over TCP).
    bool fast_link(int peer) const;
    // Tests: that many percent of the datagrams lost, both ways (SDL3_net's own simulation); or nothing this one
    // sends getting through at all, TCP too (a cable pulled); and how long a silent peer is waited for (10 s).
    void simulate_loss(int percent);
    void simulate_cut();
    void set_patience(std::uint64_t ms);
    bool hosting() const noexcept;
    std::uint16_t port() const noexcept;

  private:
    struct Inner;
    explicit Transport(std::unique_ptr<Inner> inner);
    std::unique_ptr<Inner> inner_;
};

}  // namespace lvr::net
