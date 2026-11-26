#pragma once

// A network in a test's hands (ADR-0012): the transports of a game online's machines in one process, on one clock
// the test moves on (advance), keeping what Transport promises and nothing more. A message comes after the delay and
// the jitter, or, lost on the way, later (sent again, or the copy behind it). A reliable one comes in order: what was
// sent after one that was lost waits behind it, as over TCP. A fast one is taken only if it is newer than the last
// taken from that machine, so never twice, nor after a newer one. A machine that closes its transport is seen to go
// after what it sent reliably; one the host drops is told so after what the host sent him. Seeded: every run the same.

#include <cstdint>
#include <memory>

#include "net/transport.hpp"

namespace lvr::net {

class SimulatedNetwork {
  public:
    struct Conditions {
        int delay_ms = 5;      // one way
        int jitter_ms = 0;     // up to this much later, each message its own
        int loss_percent = 0;  // of the messages: lost on the way, so late
        int late_ms = 200;     // how much later a lost one comes
        int behind_ms = 0;     // every reliable one this much later still (a TCP connection held up)
    };
    SimulatedNetwork();
    explicit SimulatedNetwork(Conditions conditions, std::uint32_t seed = 1);

    // The host's transport (one a network). A guest's, connected to the host, who sees him come after the delay;
    // none if there is no host there.
    std::unique_ptr<Transport> host();
    std::unique_ptr<Transport> join();
    // The clock every transport of this network reads, moved on.
    void advance(std::uint32_t ms) noexcept;
    std::uint32_t now() const noexcept;

    struct State;  // (its transports')

  private:
    std::shared_ptr<State> state_;  // (shared with the transports: either may go first)
};

}  // namespace lvr::net
