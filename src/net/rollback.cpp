#include "net/rollback.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace lvr::net {

namespace {

constexpr std::uint32_t kAll = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t kHashesKept = 64;  // steps' hashes kept to compare: enough for one sent late

}  // namespace

Rollback::Rollback(Game& game, const Settings& settings) : game_{&game}, settings_{settings} {
    settings_.players = std::clamp(settings_.players, 1, kMostPlayers);
    settings_.local = std::clamp(settings_.local, 0, settings_.players - 1);
    settings_.delay = std::max(settings_.delay, 0);
    settings_.window = std::max(settings_.window, 1);
    settings_.hash_every = std::max(settings_.hash_every, 1);
    // The local player's first steps, before his first input given can be for one: nothing.
    Known& local = players_.at(static_cast<std::size_t>(settings_.local));
    local.inputs.assign(static_cast<std::size_t>(settings_.delay), Input{});
    local.contiguous = static_cast<std::uint32_t>(settings_.delay);
}

std::uint32_t Rollback::known(int player) const {
    if (player < 0 || player >= settings_.players) {
        return kAll;
    }
    const Known& known = players_.at(static_cast<std::size_t>(player));
    if (known.gone_from && known.contiguous >= *known.gone_from) {
        return kAll;
    }
    return known.contiguous;
}

std::uint32_t Rollback::confirmed() const {
    std::uint32_t all = kAll;
    for (int player = 0; player < settings_.players; ++player) {
        all = std::min(all, known(player));
    }
    return all;
}

bool Rollback::add_local(const Input& input) {
    Known& local = players_.at(static_cast<std::size_t>(settings_.local));
    const std::uint32_t at = local.contiguous;
    if (at > step_ + static_cast<std::uint32_t>(settings_.delay)) {
        return false;  // as far ahead as the delay goes
    }
    local.inputs.resize(static_cast<std::size_t>(at) + 1);
    local.inputs.at(at) = input;
    ++local.contiguous;
    return true;
}

void Rollback::add_remote(int player, std::uint32_t step, const Input& input) {
    if (player < 0 || player >= settings_.players || player == settings_.local) {
        return;
    }
    Known& known = players_.at(static_cast<std::size_t>(player));
    if ((known.gone_from && step >= *known.gone_from) ||
        (step < known.inputs.size() && known.inputs.at(step).has_value())) {
        return;  // (it came before)
    }
    if (step >= known.inputs.size()) {
        known.inputs.resize(static_cast<std::size_t>(step) + 1);
    }
    known.inputs.at(step) = input;
    while (known.contiguous < known.inputs.size() && known.inputs.at(known.contiguous).has_value()) {
        ++known.contiguous;
    }
    if (step < step_ && step < known.used.size() && known.used.at(step) != input) {
        first_wrong_ = std::min(first_wrong_.value_or(step), step);
    }
}

void Rollback::leave(int player, std::uint32_t from) {
    if (player < 0 || player >= settings_.players || player == settings_.local) {
        return;
    }
    Known& known = players_.at(static_cast<std::size_t>(player));
    known.gone_from = std::min(known.gone_from.value_or(from), from);
    for (std::uint32_t step = from; step < step_ && step < known.used.size(); ++step) {
        if (known.used.at(step) != gone_input()) {  // run as if he were there: again, without him
            first_wrong_ = std::min(first_wrong_.value_or(step), step);
            break;
        }
    }
}

std::optional<Input> Rollback::input_of(int player, std::uint32_t step) const {
    const Known& known = players_.at(static_cast<std::size_t>(player));
    if (known.gone_from && step >= *known.gone_from) {
        return gone_input();
    }
    if (step < known.inputs.size()) {
        return known.inputs.at(step);
    }
    return std::nullopt;
}

std::vector<Input> Rollback::inputs_for(std::uint32_t step) const {
    std::vector<Input> inputs;
    inputs.reserve(static_cast<std::size_t>(settings_.players));
    for (int player = 0; player < settings_.players; ++player) {
        if (const std::optional<Input> given = input_of(player, step)) {
            inputs.push_back(*given);
            continue;
        }
        // Not come yet: like his last one that has.
        const Known& known = players_.at(static_cast<std::size_t>(player));
        std::optional<Input> last;
        for (std::size_t at = std::min<std::size_t>(step, known.inputs.size()); at-- > 0 && !last;) {
            last = known.inputs.at(at);
        }
        inputs.push_back(last ? game_->predict(*last) : Input{});
    }
    return inputs;
}

void Rollback::run(std::uint32_t step, bool shown) {
    game_->save(step % slots());
    const std::vector<Input> inputs = inputs_for(step);
    for (int player = 0; player < settings_.players; ++player) {
        Known& known = players_.at(static_cast<std::size_t>(player));
        if (known.used.size() <= step) {
            known.used.resize(static_cast<std::size_t>(step) + 1);
        }
        known.used.at(step) = inputs.at(static_cast<std::size_t>(player));
    }
    game_->advance(inputs, step, shown);
    if (!shown) {
        ++run_again_;
    }
    if ((step + 1) % static_cast<std::uint32_t>(settings_.hash_every) == 0) {
        hashes_[step].at(static_cast<std::size_t>(settings_.local)) = game_->hash();
        while (hashes_.size() > kHashesKept) {
            hashes_.erase(hashes_.begin());
        }
    }
}

void Rollback::catch_up() {
    if (first_wrong_ && *first_wrong_ < step_) {
        game_->load(*first_wrong_ % slots());
        for (std::uint32_t step = *first_wrong_; step < step_; ++step) {
            run(step, false);
        }
    }
    first_wrong_.reset();
    compare_hashes();
}

bool Rollback::advance() {
    catch_up();
    if (local_known() <= step_) {
        return false;  // this machine's input for it isn't given yet
    }
    if (game_->waits_for_inputs() && confirmed() <= step_) {
        return false;  // a step that is never to be run again: on every player's input only
    }
    if (const std::uint32_t all = confirmed();
        all != kAll && step_ >= all + static_cast<std::uint32_t>(settings_.window)) {
        return false;  // as far ahead of the others' inputs as it may run: it waits for them
    }
    run(step_, true);
    ++step_;
    return true;
}

std::vector<std::pair<std::uint32_t, Input>> Rollback::known_from(int player, std::uint32_t from) const {
    std::vector<std::pair<std::uint32_t, Input>> unsent;
    if (player < 0 || player >= settings_.players) {
        return unsent;
    }
    const Known& known = players_.at(static_cast<std::size_t>(player));
    for (std::uint32_t step = from; step < known.contiguous; ++step) {
        unsent.emplace_back(step, known.inputs.at(step).value_or(Input{}));  // (all there: they are contiguous)
    }
    return unsent;
}

void Rollback::report(int player, std::uint32_t step_there, int his_advantage) {
    if (player < 0 || player >= settings_.players || player == settings_.local) {
        return;
    }
    Known& known = players_.at(static_cast<std::size_t>(player));
    const std::size_t at = known.reports++ % known.seen.size();
    known.seen.at(at) = static_cast<int>(static_cast<std::int64_t>(step_) - static_cast<std::int64_t>(step_there));
    known.reported.at(at) = his_advantage;
}

int Rollback::advantage(int player) const {
    if (player < 0 || player >= settings_.players || player == settings_.local) {
        return 0;
    }
    const Known& known = players_.at(static_cast<std::size_t>(player));
    return known.reports == 0 ? 0 : known.seen.at((known.reports - 1) % known.seen.size());
}

bool Rollback::ahead() const {
    for (int player = 0; player < settings_.players; ++player) {
        const Known& known = players_.at(static_cast<std::size_t>(player));
        if (player == settings_.local || known.reports == 0 || known.gone_from) {
            continue;
        }
        const std::size_t count = std::min(known.reports, known.seen.size());
        const auto first = known.seen.begin();
        const auto reported = known.reported.begin();
        const int mine = std::accumulate(first, first + static_cast<std::ptrdiff_t>(count), 0);
        const int his = std::accumulate(reported, reported + static_cast<std::ptrdiff_t>(count), 0);
        // As GGPO has it: half the difference of the two advantages is how far this one is ahead, on average.
        if ((mine - his) / 2 >= static_cast<int>(count)) {
            return true;
        }
    }
    return false;
}

std::optional<std::uint64_t> Rollback::hash_at(std::uint32_t step) const {
    if (step >= confirmed() || (first_wrong_ && *first_wrong_ <= step)) {
        return std::nullopt;  // a rollback may still run it again, or is about to
    }
    const auto found = hashes_.find(step);
    return found != hashes_.end() ? found->second.at(static_cast<std::size_t>(settings_.local)) : std::nullopt;
}

void Rollback::add_remote_hash(int player, std::uint32_t step, std::uint64_t hash) {
    if (player < 0 || player >= settings_.players || player == settings_.local) {
        return;
    }
    if (!hashes_.empty() && step < hashes_.begin()->first) {
        return;  // too old to compare
    }
    hashes_[step].at(static_cast<std::size_t>(player)) = hash;
    compare_hashes();
}

void Rollback::compare_hashes() {
    const std::uint32_t all = std::min(confirmed(), first_wrong_.value_or(kAll));  // (what is final here)
    for (const auto& [step, by_player] : hashes_) {
        if (step >= all) {
            break;
        }
        const std::optional<std::uint64_t>& own = by_player.at(static_cast<std::size_t>(settings_.local));
        for (const std::optional<std::uint64_t>& theirs : by_player) {
            if (own && theirs && *own != *theirs && (!out_of_step_ || step < *out_of_step_)) {
                out_of_step_ = step;
            }
        }
    }
}

}  // namespace lvr::net
