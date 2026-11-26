#include "net/simulated.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace lvr::net {

struct SimulatedNetwork::State {
    // A message (or a connection's coming or going) on its way to a machine.
    struct Flight {
        std::uint32_t at = 0;     // when it comes
        std::uint64_t order = 0;  // when it was sent, among all (of two coming at once, the first sent first)
        int from = 0;             // the machines
        int to = 0;
        Event event;                          // as the machine it comes to sees it
        std::optional<std::uint32_t> number;  // a fast one's, from its sender
    };
    // One way between two machines.
    struct Link {
        bool open = true;
        // Once closed: what was sent before (and the word that it closed, sent as it did) still comes; nothing after.
        std::uint64_t until = UINT64_MAX;
        std::uint32_t fast_sent = 0;    // the last fast one's number
        std::uint32_t fast_taken = 0;   // the last fast one taken at the other end
        std::uint32_t reliable_at = 0;  // when the last reliable one comes (the next comes no earlier)
        bool left = false;              // the other end has heard it close: nothing more is taken from it
    };
    struct Machine {
        bool hosting = false;
        bool open = true;
        int peer = 0;              // a guest: the host's number for him (from 1)
        std::vector<Event> local;  // what this machine's next poll has from itself (the host: one he let go)
        // Its transport closed (said by the transport's end, which takes no lock): then, the `order`th sent; the
        // others told at the network's next turn (settle), as though then.
        struct Closed {
            std::atomic<bool> is{false};
            std::atomic<std::uint32_t> when{0};
            std::atomic<std::uint64_t> order{0};
        };
        std::shared_ptr<Closed> closed = std::make_shared<Closed>();
    };

    Conditions conditions;
    std::minstd_rand random;
    std::atomic<std::uint32_t> now{0};  // (atomic: a transport's end reads it without the lock)
    std::atomic<std::uint64_t> sent{0};
    int host = -1;
    int next_peer = 1;
    std::vector<Machine> machines;
    std::map<std::pair<int, int>, Link> links;
    std::vector<Flight> flights;
    std::mutex lock;  // (its transports may be used on other threads: a guest's made while joining)

    State(Conditions with, std::uint32_t seed) : conditions{with}, random{seed} {}

    std::uint32_t jitter() {
        return conditions.jitter_ms > 0
                   ? static_cast<std::uint32_t>(random() % static_cast<unsigned>(conditions.jitter_ms + 1))
                   : 0;
    }

    // The machine a peer number is from this machine (a guest's 0: the host), if they are connected that way.
    std::optional<int> machine_of(int machine, int peer) const {
        if (!machines.at(static_cast<std::size_t>(machine)).hosting) {
            return peer == 0 && host >= 0 ? std::optional{host} : std::nullopt;
        }
        for (std::size_t guest = 0; guest < machines.size(); ++guest) {
            if (std::cmp_not_equal(guest, machine) && machines[guest].peer == peer) {
                return static_cast<int>(guest);
            }
        }
        return std::nullopt;
    }
    bool open(int from, int to) const {
        const auto found = links.find({from, to});
        return found != links.end() && found->second.open;
    }
    // How `to` numbers `from`.
    int peer_of(int from, int to) const {
        return machines.at(static_cast<std::size_t>(to)).hosting ? machines.at(static_cast<std::size_t>(from)).peer : 0;
    }

    void post(int from, int to, Event event, bool fast) { post(from, to, std::move(event), fast, now, ++sent); }
    // As sent at `when`, the `order`th.
    void post(int from, int to, Event event, bool fast, std::uint32_t when, std::uint64_t order) {
        Link& link = links[{from, to}];
        if (!link.open) {
            return;
        }
        Flight flight{.at = when + static_cast<std::uint32_t>(conditions.delay_ms) + jitter(),
                      .order = order,
                      .from = from,
                      .to = to,
                      .event = std::move(event),
                      .number = std::nullopt};
        if (std::cmp_less(random() % 100, conditions.loss_percent)) {
            flight.at += static_cast<std::uint32_t>(conditions.late_ms);  // lost on the way: sent again
        }
        if (fast) {
            flight.number = ++link.fast_sent;
        } else {
            flight.at += static_cast<std::uint32_t>(conditions.behind_ms);
            flight.at = std::max(flight.at, link.reliable_at);  // (in order: what comes after a lost one waits)
            link.reliable_at = flight.at;
        }
        flights.push_back(std::move(flight));
    }

    // The way from `from` to `to` closes (at `when`, the `order`th sent): what was sent on it comes, then that it
    // closed (to `to`, as Left).
    void close(int from, int to, std::uint32_t when, std::uint64_t order) {
        if (!links[{from, to}].open) {
            return;
        }
        post(from, to, Event{.kind = Event::Kind::kLeft, .peer = peer_of(from, to), .bytes = {}}, false, when, order);
        Link& link = links[{from, to}];
        link.open = false;
        link.until = order;
    }
    // Nothing more from `from` is taken at `to`, not even what is on its way.
    void deafen(int from, int to) {
        Link& link = links[{from, to}];
        link.open = false;
        link.until = 0;
    }

    std::vector<Event> poll(int machine) {
        settle();
        std::vector<Event> events = std::exchange(machines.at(static_cast<std::size_t>(machine)).local, {});
        std::vector<Flight> come;
        std::erase_if(flights, [&](Flight& flight) {
            if (flight.to != machine || flight.at > now) {
                return false;
            }
            come.push_back(std::move(flight));
            return true;
        });
        std::ranges::sort(
            come, [](const Flight& a, const Flight& b) { return std::pair{a.at, a.order} < std::pair{b.at, b.order}; });
        for (Flight& flight : come) {
            Link& link = links[{flight.from, flight.to}];
            if (link.left || flight.order > link.until) {
                continue;  // (after the way closed)
            }
            if (flight.number) {
                if (*flight.number <= link.fast_taken) {
                    continue;  // a newer one was taken
                }
                link.fast_taken = *flight.number;
            }
            link.left = flight.event.kind == Event::Kind::kLeft;
            events.push_back(std::move(flight.event));
        }
        return events;
    }

    // The host lets a guest go: told after what the host sent him; nothing more taken from him.
    void drop(int guest) {
        close(host, guest, now, ++sent);
        if (links[{guest, host}].open) {
            deafen(guest, host);
            machines.at(static_cast<std::size_t>(host))
                .local.push_back({.kind = Event::Kind::kLeft, .peer = peer_of(guest, host), .bytes = {}});
        }
    }

    // The machines whose transports closed: the others see each go, after what it sent them.
    void settle() {
        for (std::size_t closing = 0; closing < machines.size(); ++closing) {
            Machine& gone = machines[closing];
            if (!gone.closed->is.load() || !gone.open) {
                continue;
            }
            gone.open = false;
            const std::uint32_t when = gone.closed->when.load();
            const std::uint64_t order = gone.closed->order.load();
            const int machine = static_cast<int>(closing);
            for (std::size_t other = 0; other < machines.size(); ++other) {
                const int one = static_cast<int>(other);
                if (one != machine && links.contains({machine, one})) {
                    close(machine, one, when, order);
                    deafen(one, machine);
                }
            }
            if (machine == host) {
                host = -1;
            }
        }
    }
};

namespace {

// One machine's transport on the simulated network.
class Simulated final : public Transport {
  public:
    Simulated(std::shared_ptr<SimulatedNetwork::State> state, int machine)
        : state_{std::move(state)},
          machine_{machine},
          hosting_{state_->machines.at(static_cast<std::size_t>(machine)).hosting},
          closed_{state_->machines.at(static_cast<std::size_t>(machine)).closed} {}  // (made under the lock)
    Simulated(const Simulated&) = delete;
    Simulated& operator=(const Simulated&) = delete;
    Simulated(Simulated&&) = delete;
    Simulated& operator=(Simulated&&) = delete;
    // (Only when: the others are told at the network's next turn, as though then.)
    ~Simulated() override {
        closed_->when = state_->now.load();
        closed_->order = ++state_->sent;
        closed_->is.store(true);
    }

    std::vector<Event> poll() override {
        const std::scoped_lock held{state_->lock};
        return state_->poll(machine_);
    }
    void send(int peer, std::span<const std::uint8_t> bytes, Channel channel) override {
        const std::scoped_lock held{state_->lock};
        state_->settle();
        if (const std::optional<int> to = state_->machine_of(machine_, peer)) {
            state_->post(machine_, *to,
                         Event{.kind = Event::Kind::kMessage,
                               .peer = state_->peer_of(machine_, *to),
                               .bytes = std::vector<std::uint8_t>(bytes.begin(), bytes.end())},
                         channel == Channel::kFast);
        }
    }
    void drop(int peer) override {
        const std::scoped_lock held{state_->lock};
        state_->settle();
        if (const std::optional<int> guest = state_->machine_of(machine_, peer); guest && hosting()) {
            state_->drop(*guest);
        }
    }
    void drain(int /*wait_ms*/) override {}  // (what was sent comes all the same)
    std::optional<int> round_trip_ms(int peer) const override {
        const std::scoped_lock held{state_->lock};
        const std::optional<int> other = state_->machine_of(machine_, peer);
        if (!other || !state_->open(machine_, *other) || !state_->open(*other, machine_) ||
            state_->machines[static_cast<std::size_t>(*other)].closed->is.load()) {
            return std::nullopt;
        }
        return 2 * state_->conditions.delay_ms;
    }
    bool hosting() const noexcept override { return hosting_; }
    std::uint32_t now_ms() const override {
        const std::scoped_lock held{state_->lock};
        return state_->now;
    }

  private:
    std::shared_ptr<SimulatedNetwork::State> state_;
    int machine_;
    bool hosting_;
    std::shared_ptr<SimulatedNetwork::State::Machine::Closed> closed_;
};

}  // namespace

SimulatedNetwork::SimulatedNetwork() : SimulatedNetwork(Conditions{}) {}

SimulatedNetwork::SimulatedNetwork(Conditions conditions, std::uint32_t seed)
    : state_{std::make_shared<State>(conditions, seed)} {}

std::unique_ptr<Transport> SimulatedNetwork::host() {
    const std::scoped_lock held{state_->lock};
    state_->settle();
    if (state_->host >= 0) {
        return nullptr;  // (one a network)
    }
    state_->host = static_cast<int>(state_->machines.size());
    state_->machines.push_back(
        {.hosting = true, .open = true, .peer = 0, .local = {}, .closed = std::make_shared<State::Machine::Closed>()});
    return std::make_unique<Simulated>(state_, state_->host);
}

std::unique_ptr<Transport> SimulatedNetwork::join() {
    const std::scoped_lock held{state_->lock};
    state_->settle();
    if (state_->host < 0) {
        return nullptr;
    }
    const int guest = static_cast<int>(state_->machines.size());
    state_->machines.push_back({.hosting = false,
                                .open = true,
                                .peer = state_->next_peer++,
                                .local = {},
                                .closed = std::make_shared<State::Machine::Closed>()});
    state_->links[{guest, state_->host}] = {};
    state_->links[{state_->host, guest}] = {};
    // (Sent the reliable way: what the guest says after it comes after it.)
    state_->post(guest, state_->host,
                 Event{.kind = Event::Kind::kJoined, .peer = state_->machines.back().peer, .bytes = {}}, false);
    return std::make_unique<Simulated>(state_, guest);
}

void SimulatedNetwork::advance(std::uint32_t ms) {
    const std::scoped_lock held{state_->lock};
    state_->now += ms;
}

std::uint32_t SimulatedNetwork::now() const {
    const std::scoped_lock held{state_->lock};
    return state_->now;
}

}  // namespace lvr::net
