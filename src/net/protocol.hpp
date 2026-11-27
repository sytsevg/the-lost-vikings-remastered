#pragma once

// What the machines of a game online tell each other (ADR-0012), as bytes for the transport: a byte for which
// message, then its fields, little-endian. The host is in the middle: a client talks to the host only, and the host
// passes on to each client what the other players gave (a star). Pure: no sockets.
//
// Joining: Hello (client to host) is answered by Welcome, or Refused with why (another version of the game's state,
// another build, other game files). LobbyStatus (host to all, now and then) says who is there, their round trips and
// what the game starts with; Start (host to all) starts the game for everyone at step 0, and Back (host to all) ends it
// for everyone, back in the lobby with the same connections (to start another). Each game has a number, which the
// messages of a game carry (Inputs, Status, Hash, Resync): what is late from the one before is told apart. Then, every
// refresh: Inputs (each player's not yet acknowledged, one message a player), Status (where the sender is, his frame
// advantage, and how far he knows each player's inputs: the acknowledgements), and now and then Hash; a guest's hash
// that isn't the host's is answered by Resync (host to him: the host's state to go on from). Leave (host to all): a
// player is gone from that step on (the one who went, or was let go, too); Quit (client to host): he goes. A Hello
// while the game is under way is answered by Joining (a free place, the game's state, the step he comes at), and the
// others are told by Arrive.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "net/rollback.hpp"

namespace lvr::net {

inline constexpr std::uint32_t kMagic = 0x39'52'56'4c;  // "LVR9"
inline constexpr std::uint16_t kProtocol = 3;
inline constexpr std::size_t kMostName = 8;  // a player's name, in the game's font: what fits under a Viking

struct Hello {
    std::uint16_t protocol = kProtocol;
    std::uint32_t state_version = 0;  // lvr::kStateVersion
    std::string build;                // the build's git commit
    std::uint64_t game = 0;           // the game files' release (their hash)
    std::string name;
    bool operator==(const Hello&) const = default;
};
struct Refused {
    std::string why;
    bool operator==(const Refused&) const = default;
};
struct Welcome {
    std::uint8_t player = 0;  // his number in the game
    bool operator==(const Welcome&) const = default;
};
inline constexpr std::uint16_t kUnknown = 0xffff;  // a round trip not measured (yet)
struct LobbyStatus {
    std::vector<std::string> names;  // by player
    // The host's round trips to each, as in Status; and the scene the game starts in (a level, or the intro: a new
    // game), as the host has chosen it so far.
    std::array<std::uint16_t, kMostPlayers> network{kUnknown, kUnknown, kUnknown};
    std::array<std::uint16_t, kMostPlayers> engine{kUnknown, kUnknown, kUnknown};
    std::uint16_t slot = 0;
    std::uint8_t countdown = 0;  // the host has started it: seconds until it starts (0: he hasn't)
    bool operator==(const LobbyStatus&) const = default;
};
struct Start {
    std::uint16_t game = 0;  // its number
    std::uint8_t players = 1;
    std::uint8_t delay = 0;  // steps of input delay
    std::uint16_t slot = 0;  // the scene the game starts in
    bool cheat = false;      // the cheat's switch, the host's
    std::vector<std::string> names;
    bool operator==(const Start&) const = default;
};
struct Back {
    bool operator==(const Back&) const = default;
};
struct Inputs {
    std::uint16_t game = 0;
    std::uint8_t player = 0;  // whose
    std::uint32_t first = 0;  // the step of the first
    std::vector<Input> inputs;
    bool operator==(const Inputs&) const = default;
};
struct Status {
    std::uint16_t game = 0;
    std::uint8_t player = 0;                          // the sender
    std::uint32_t step = 0;                           // the next step he runs
    std::int16_t advantage = 0;                       // his frame advantage over the one he sends it to
    std::array<std::uint32_t, kMostPlayers> knows{};  // by player: how far he has that one's inputs, without a gap
    // The host's: his round trips to each player, in milliseconds: the network's (the transport's pings) and the
    // game's (Echo); a client sees the other clients' so.
    std::array<std::uint16_t, kMostPlayers> network{kUnknown, kUnknown, kUnknown};
    std::array<std::uint16_t, kMostPlayers> engine{kUnknown, kUnknown, kUnknown};
    bool operator==(const Status&) const = default;
};
struct Hash {
    std::uint16_t game = 0;
    std::uint8_t player = 0;
    std::uint32_t step = 0;
    std::uint64_t hash = 0;
    std::uint16_t resyncs = 0;  // the sender's: how many Resyncs he has taken (one of a state before them: none)
    bool operator==(const Hash&) const = default;
};
struct Leave {
    std::uint8_t player = 0;
    std::uint32_t from = 0;
    bool operator==(const Leave&) const = default;
};
struct Quit {
    bool operator==(const Quit&) const = default;
};
// The game's round trip: an Echo is answered (Echoed) by the other machine's game as it next takes what came, once a
// refresh; so it is the network's round trip and both games' rhythm.
struct Echo {
    std::uint32_t sent = 0;  // the sender's milliseconds
    bool operator==(const Echo&) const = default;
};
struct Echoed {
    std::uint32_t sent = 0;
    bool operator==(const Echoed&) const = default;
};

// Joining a game under way (host to all): a player comes into a place at a step no machine has confirmed yet.
struct Arrive {
    std::uint8_t player = 0;
    std::uint32_t at = 0;
    std::string name;
    bool operator==(const Arrive&) const = default;
};
// Host to the one joining under way, in place of Welcome and Start: his place, the game's state at a confirmed step
// (Session::state_bytes) to start from, the step he arrives at, the delay and everyone's names.
struct Presence {
    std::uint8_t player = 0;
    std::uint32_t at = 0;
    bool there = true;
    bool operator==(const Presence&) const = default;
};
// Host to all, as the game goes on (both ways, as its inputs): who is there is decided for every step before `until`
// (Rollback::decided_until), as `presence` has it, the comings and goings of the last steps (with it whichever copy
// comes first is enough: nothing waits for an Arrive).
struct Decided {
    std::uint16_t game = 0;
    std::uint32_t until = 0;
    std::vector<Presence> presence;
    bool operator==(const Decided&) const = default;
};
struct Joining {
    std::uint16_t game = 0;
    std::uint8_t player = 0;
    std::uint32_t step = 0;     // of the state
    std::uint32_t arrives = 0;  // his first step there
    std::uint8_t delay = 0;
    std::vector<std::string> names;
    std::vector<std::uint8_t> state;
    std::vector<Presence> presence;  // the changes of who is there the host has from the state's step on
    bool operator==(const Joining&) const = default;
};

// Host to a guest whose state's hash wasn't the host's: the host's state before a step every input is known for, to
// go on from (as Joining's), and how many he has been sent so far, this one counted (what his hashes say after it).
struct Resync {
    std::uint16_t game = 0;
    std::uint8_t player = 0;  // whom it is for
    std::uint32_t step = 0;   // of the state
    std::uint16_t resyncs = 0;
    std::vector<std::uint8_t> state;
    bool operator==(const Resync&) const = default;
};

// All one machine says fast to another in one pass, as one message (the transport takes only the newest).
struct Bundle {
    std::vector<std::vector<std::uint8_t>> parts;  // each message, encoded
    bool operator==(const Bundle&) const = default;
};

using Message = std::variant<Hello, Refused, Welcome, LobbyStatus, Start, Inputs, Status, Hash, Leave, Quit, Echo,
                             Echoed, Arrive, Joining, Back, Decided, Bundle, Resync>;

std::vector<std::uint8_t> encode(const Message& message);
// A message's messages: a bundle's, decoded (what can't be is left out); else the message itself.
std::vector<Message> unbundled(const Message& message);
// None if the bytes aren't a whole message, or one this protocol doesn't know.
std::optional<Message> decode(std::span<const std::uint8_t> bytes);

// A name as the game can show it: its letters made capitals, what the game's font doesn't have dropped, at most
// kMostName long; an empty one becomes PLAYER and the number (counted from 1).
std::string shown_name(std::string_view name, int player);

}  // namespace lvr::net
