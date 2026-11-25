#include "net/protocol.hpp"

#include <algorithm>
#include <format>
#include <type_traits>
#include <utility>

namespace lvr::net {

namespace {

constexpr std::size_t kMostInputs = 64;  // in one message: more than the window ever leaves unacknowledged
constexpr std::size_t kMostString = 64;  // a build's commit, a name, why one was refused
constexpr std::size_t kMostNames = kMostPlayers;

class Out {
  public:
    explicit Out(std::vector<std::uint8_t>& bytes) noexcept : bytes_{&bytes} {}
    template <typename T>
        requires std::is_integral_v<T>
    void operator()(T value) {
        const auto bits = static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<T>>(value));
        for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
            bytes_->push_back(static_cast<std::uint8_t>(bits >> (8 * byte)));
        }
    }
    void operator()(const std::string& text) {
        const std::size_t length = std::min(text.size(), kMostString);
        (*this)(static_cast<std::uint8_t>(length));
        bytes_->insert(bytes_->end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(length));
    }
    void operator()(const Input& input) { bytes_->insert(bytes_->end(), input.bytes.begin(), input.bytes.end()); }

  private:
    std::vector<std::uint8_t>* bytes_;
};

class In {
  public:
    explicit In(std::span<const std::uint8_t> bytes) noexcept : bytes_{bytes} {}
    template <typename T>
        requires std::is_integral_v<T>
    void operator()(T& value) {
        std::uint64_t bits = 0;
        for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
            bits |= std::uint64_t{next()} << (8 * byte);
        }
        value = static_cast<T>(static_cast<std::make_unsigned_t<T>>(bits));
    }
    void operator()(bool& value) {
        std::uint8_t byte = 0;
        (*this)(byte);
        value = byte != 0;
    }
    void operator()(std::string& text) {
        std::uint8_t length = 0;
        (*this)(length);
        text.clear();
        if (length > kMostString) {
            ok_ = false;
            return;
        }
        for (std::uint8_t i = 0; i < length; ++i) {
            text.push_back(static_cast<char>(next()));
        }
    }
    void operator()(Input& input) {
        for (std::uint8_t& byte : input.bytes) {
            byte = next();
        }
    }
    bool done() const noexcept { return ok_ && at_ == bytes_.size(); }
    bool ok() const noexcept { return ok_; }

  private:
    std::uint8_t next() {
        if (at_ >= bytes_.size()) {
            ok_ = false;
            return 0;
        }
        return bytes_[at_++];
    }
    std::span<const std::uint8_t> bytes_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

// Each message's fields, in order, written or read the same way.
template <typename Io>
void fields(Io& io, auto& message) {
    using M = std::remove_cvref_t<decltype(message)>;
    if constexpr (std::is_same_v<M, Hello>) {
        io(message.protocol);
        io(message.state_version);
        io(message.build);
        io(message.game);
        io(message.name);
    } else if constexpr (std::is_same_v<M, Refused>) {
        io(message.why);
    } else if constexpr (std::is_same_v<M, Welcome>) {
        io(message.player);
    } else if constexpr (std::is_same_v<M, Hash>) {
        io(message.player);
        io(message.step);
        io(message.hash);
    } else if constexpr (std::is_same_v<M, Leave>) {
        io(message.player);
        io(message.from);
    } else if constexpr (std::is_same_v<M, Status>) {
        io(message.player);
        io(message.step);
        io(message.advantage);
        for (auto& knows : message.knows) {
            io(knows);
        }
        for (auto& network : message.network) {
            io(network);
        }
        for (auto& engine : message.engine) {
            io(engine);
        }
    } else if constexpr (std::is_same_v<M, Echo> || std::is_same_v<M, Echoed>) {
        io(message.sent);
    } else if constexpr (std::is_same_v<M, Quit>) {
        // nothing more
    }
}

void write_names(Out& out, const std::vector<std::string>& names) {
    out(static_cast<std::uint8_t>(std::min(names.size(), kMostNames)));
    for (std::size_t i = 0; i < names.size() && i < kMostNames; ++i) {
        out(names[i]);
    }
}

bool read_names(In& in, std::vector<std::string>& names) {
    std::uint8_t count = 0;
    in(count);
    if (count > kMostNames) {
        return false;
    }
    names.resize(count);
    for (std::string& name : names) {
        in(name);
    }
    return in.ok();
}

}  // namespace

std::vector<std::uint8_t> encode(const Message& message) {
    std::vector<std::uint8_t> bytes;
    Out out{bytes};
    out(static_cast<std::uint8_t>(message.index()));
    if (message.index() == 0) {
        out(kMagic);  // (a Hello begins with it, after its kind: what isn't this game's is told apart)
    }
    std::visit(
        [&out](const auto& one) {
            using M = std::remove_cvref_t<decltype(one)>;
            if constexpr (std::is_same_v<M, Lobby>) {
                write_names(out, one.names);
            } else if constexpr (std::is_same_v<M, Start>) {
                out(one.players);
                out(one.delay);
                out(one.slot);
                out(static_cast<std::uint8_t>(one.cheat ? 1 : 0));
                write_names(out, one.names);
            } else if constexpr (std::is_same_v<M, Inputs>) {
                out(one.player);
                out(one.first);
                out(static_cast<std::uint8_t>(std::min(one.inputs.size(), kMostInputs)));
                for (std::size_t i = 0; i < one.inputs.size() && i < kMostInputs; ++i) {
                    out(one.inputs[i]);
                }
            } else {
                M copy = one;
                fields(out, copy);
            }
        },
        message);
    return bytes;
}

namespace {

template <std::size_t Index>
std::optional<Message> read_one(In& in) {
    using M = std::variant_alternative_t<Index, Message>;
    M message;
    if constexpr (std::is_same_v<M, Lobby>) {
        if (!read_names(in, message.names)) {
            return std::nullopt;
        }
    } else if constexpr (std::is_same_v<M, Start>) {
        in(message.players);
        in(message.delay);
        in(message.slot);
        in(message.cheat);
        if (!read_names(in, message.names)) {
            return std::nullopt;
        }
    } else if constexpr (std::is_same_v<M, Inputs>) {
        std::uint8_t count = 0;
        in(message.player);
        in(message.first);
        in(count);
        if (count > kMostInputs) {
            return std::nullopt;
        }
        message.inputs.resize(count);
        for (Input& input : message.inputs) {
            in(input);
        }
    } else {
        fields(in, message);
    }
    if (!in.done()) {
        return std::nullopt;
    }
    return Message{message};
}

template <std::size_t... Index>
std::optional<Message> read_kind(In& in, std::size_t kind, std::index_sequence<Index...> /*all*/) {
    std::optional<Message> read;
    (void)((kind == Index ? (read = read_one<Index>(in), true) : false) || ...);
    return read;
}

}  // namespace

std::optional<Message> decode(std::span<const std::uint8_t> bytes) {
    In in{bytes};
    std::uint8_t kind = 0;
    in(kind);
    if (!in.ok() || kind >= std::variant_size_v<Message>) {
        return std::nullopt;
    }
    if (kind == 0) {
        std::uint32_t magic = 0;
        in(magic);
        if (magic != kMagic) {
            return std::nullopt;
        }
    }
    return read_kind(in, kind, std::make_index_sequence<std::variant_size_v<Message>>{});
}

std::string shown_name(std::string_view name, int player) {
    std::string shown;
    for (const char character : name) {
        const char upper = character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : character;
        const bool letter = upper >= 'A' && upper <= 'Z';
        const bool digit = upper >= '0' && upper <= '9';
        if ((letter || digit || upper == ' ') && shown.size() < kMostName) {
            shown.push_back(upper);
        }
    }
    while (!shown.empty() && shown.back() == ' ') {
        shown.pop_back();
    }
    if (shown.empty()) {
        shown = std::format("PLAYER {}", player + 1);
    }
    return shown;
}

}  // namespace lvr::net
