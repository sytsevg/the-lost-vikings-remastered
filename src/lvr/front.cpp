#include "lvr/front.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <print>
#include <utility>

#include "lvr/gpu.hpp"
#include "lvr/names.hpp"

namespace lvr {

namespace {

// A round trip as the menu shows it.
std::string round_trip_words(std::optional<int> ms) {
    return ms ? std::format("{} MS", *ms) : std::string{"-"};
}

std::optional<int> parse_number(std::string_view text) {
    try {
        return static_cast<int>(std::stoul(std::string{text}, nullptr, 0));
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

Menu::Choices menu_choices(const Session& session, bool hosting, bool lobby) {
    Menu::Choices choices{.has_modern = session.has_modern(),
                          .screens = {},
                          .online = session.online(),
                          .hosting = hosting,
                          .lobby = lobby,
                          .passwords = {}};
    for (const GpuRenderer::Look look : GpuRenderer::kLooks) {
        choices.screens.emplace_back(GpuRenderer::name_of(look));
    }
    for (const auto& password : session.world().passwords) {  // (HOST GAME's level: by its password)
        choices.passwords.emplace_back(password.begin(), password.end());
    }
    return choices;
}

std::string start_words(const Session& session, int slot) {
    if (slot == scene::kIntro) {
        return "NEW GAME";
    }
    const auto& passwords = session.world().passwords;
    if (slot >= 0 && std::cmp_less(slot, passwords.size())) {
        const auto& password = passwords.at(static_cast<std::size_t>(slot));
        return std::format("LEVEL {}, {}", slot + 1, std::string(password.begin(), password.end()));
    }
    return std::format("SCENE {}", slot);
}

std::string said(std::string_view text) {
    constexpr std::size_t kFits = 37;  // (the menu's box, in characters)
    std::string out;
    for (const char c : text.substr(0, kFits)) {
        out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return out;
}

std::pair<std::string, std::uint16_t> split_address(std::string_view address) {
    constexpr int kPorts = 65536;
    std::string_view host = address;
    std::optional<int> port;
    if (address.starts_with('[')) {
        if (const std::size_t close = address.find(']'); close != std::string_view::npos) {
            host = address.substr(1, close - 1);
            if (address.substr(close + 1).starts_with(':')) {
                port = parse_number(address.substr(close + 2));
            }
        }
    } else if (std::ranges::count(address, ':') == 1) {  // (more than one: an IPv6 address alone)
        const std::size_t colon = address.find(':');
        port = parse_number(address.substr(colon + 1));
        host = port ? address.substr(0, colon) : address;
    }
    return {std::string{host}, static_cast<std::uint16_t>(port && *port > 0 && *port < kPorts ? *port : kDefaultPort)};
}

// --- Offline ---

void OfflinePlace::tell(bool host, std::string why) {
    tell_ = std::pair{host, std::move(why)};
}

void OfflinePlace::open_page(Ui& ui, bool host, std::string_view said) {
    Menu& menu = ui.open_menu();
    host ? menu.open_host() : menu.open_join();
    if (!said.empty()) {
        menu.tell(std::string{said});
    }
    ui.redraw();
}

void OfflinePlace::begin(Ui& ui) {
    if (tell_ && may_go_online_) {  // why the game online ended
        open_page(ui, tell_->first, tell_->second);
    }
    tell_.reset();
}

Stepped OfflinePlace::step(const Tick& tick, Ui& ui) {
    Stepped stepped;
    if (const Menu* menu = ui.menu()) {  // the game holds: its pictures as it is, a zoom shown at once
        stepped.frames = session_->held(!menu->of_title());  // (the title's own pages: under its logo, lit)
        stepped.cues = std::move(session_->cues);
        return stepped;
    }
    stepped.frames = session_->step(showcase_ != nullptr ? showcase_->before_step(*session_, tick.keys) : tick.keys,
                                    tick.menu_keys, tick.pace);
    stepped.cues = std::move(session_->cues);
    if (session_->quit()) {  // quit, or a scene that can't be loaded
        if (!session_->error().empty()) {
            std::println(stderr, "vikings-remaster: {}", session_->error());
        }
        stepped.go = Go{.to = Go::To::kDone,
                        .why = {},
                        .failed = !session_->error().empty(),
                        .host = false,
                        .level = 0,
                        .page = false};
        return stepped;
    }
    const std::uint8_t page = session_->world().title.page;
    if (page == World::Title::kOptions) {  // the title's OPTIONS: the menu's settings, over the title
        ui.open_menu().open_options();
        ui.redraw();
    } else if (may_go_online_ && page != 0) {  // its HOST GAME or JOIN GAME
        open_page(ui, page == World::Title::kHostGame, {});
    }
    return stepped;
}

Place::Answer OfflinePlace::asked(Menu::Asked asked, Menu& menu) {
    if ((asked == Menu::Asked::kHost || asked == Menu::Asked::kJoin) && may_go_online_) {
        const bool host = asked == Menu::Asked::kHost;
        return {.handled = false,
                .go = Go{.to = host ? Go::To::kLobby : Go::To::kConnecting,
                         .why = {},
                         .failed = false,
                         .host = host,
                         .level = menu.level(),
                         .page = false}};
    }
    return {};
}

// --- Connecting ---

ConnectingPlace::ConnectingPlace(Session& offline, Join join, net::Identity identity, const std::string& address)
    : offline_{&offline}, join_{std::move(join)}, identity_{std::move(identity)} {
    std::tie(address_, port_) = split_address(address);
}

ConnectingPlace::~ConnectingPlace() {
    cancelled_ = true;  // (given up, or on to the lobby: the thread is done, or ends within its slice)
    if (joining_.joinable()) {
        joining_.join();
    }
}

void ConnectingPlace::begin(Ui& ui) {
    ui.open_menu().open_join();
    ui.redraw();
}

Place::Answer ConnectingPlace::asked(Menu::Asked asked, Menu& /*menu*/) {
    // (While JOIN is under way the menu asks nothing: Menu::press. Once it has failed, the page is JOIN GAME's.)
    if (asked == Menu::Asked::kResume) {  // BACK, or Escape: the title as it was
        return {.handled = false,
                .go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0, .page = false}};
    }
    if (asked == Menu::Asked::kJoin && failed_said_at_) {  // JOIN again (the menu closes: what was typed kept)
        return {
            .handled = false,
            .go = Go{.to = Go::To::kConnecting, .why = {}, .failed = false, .host = false, .level = 0, .page = false}};
    }
    return {.handled = true, .go = {}};  // (a line typed into)
}

Go ConnectingPlace::refresh(Ui& ui) {
    constexpr int kTurnEvery = 6;       // refreshes a phase of the turning mark
    constexpr int kLeastTurning = 120;  // a failure is said no sooner than two seconds in, the mark turning till then
    const std::string where = std::format("{}:{}", address_, port_);
    Menu* menu = ui.menu();
    if (!began_at_) {
        began_at_ = ui.refreshes();
    }
    refreshes_ = static_cast<int>(ui.refreshes() - *began_at_);
    if (failed_) {
        // Why, beside JOIN, and the page JOIN GAME's again at once: back, the lines, JOIN once more: control
        // is given back as soon as it fails.
        if (!failed_said_at_ && refreshes_ >= kLeastTurning && menu != nullptr) {
            failed_said_at_ = refreshes_;
            menu->set_joining(std::nullopt);
            menu->set_join_said(*failed_);
            ui.redraw();
        }
    }
    if (const int phase = refreshes_ / kTurnEvery % 4; menu != nullptr && !failed_said_at_ && phase != phase_) {
        phase_ = phase;
        menu->set_joining(phase);
        ui.redraw();
    }
    if (failed_) {
        return {};
    }
    const auto fail = [&](std::string_view why, std::string_view shown) {
        std::println(stderr, "vikings-remaster: {}", why);
        failed_ = said(shown);
        connection_.reset();  // (the host told this one goes: a place in his lobby given back)
        lobby_.reset();
        return Go{};
    };
    if (connection_ == nullptr) {
        if (!joining_.joinable()) {  // the connection made on a thread of its own: the page goes on meanwhile
            joining_ = std::thread{[this] {
                auto joined = join_(address_, port_, cancelled_);
                const std::scoped_lock held{joined_lock_};
                joined_ = std::move(joined);
            }};
        }
        std::optional<std::expected<std::unique_ptr<net::Transport>, std::string>> joined;
        {
            const std::scoped_lock held{joined_lock_};
            joined = std::exchange(joined_, std::nullopt);
        }
        if (!joined) {
            return {};  // (still being made)
        }
        if (!*joined) {
            return fail(joined->error(), "NO GAME THERE");
        }
        connection_ = std::make_unique<net::Connection>(std::move(**joined), identity_);
        lobby_ = std::make_unique<net::GuestLobby>(*connection_, false);
        began_ = connection_->now_ms();
    }
    auto polled = lobby_->poll();
    if (!polled) {
        const std::string& why = polled.error();  // (turned away, or the host gone: in words that fit beside JOIN)
        const std::string_view shown = why.contains("version")      ? "ANOTHER VERSION"
                                       : why.contains("build")      ? "ANOTHER BUILD"
                                       : why.contains("game files") ? "OTHER GAME FILES"
                                       : why.contains("full")       ? "THE GAME IS FULL"
                                                                    : "THE HOST IS GONE";
        return fail(why, shown);
    }
    if (*polled) {  // the game is under way: into it
        started_ = **polled;
        std::println("vikings-remaster: joined {}", where);
        return Go{.to = Go::To::kOnline, .why = {}, .failed = false, .host = false, .level = 0, .page = false};
    }
    if (lobby_->welcomed()) {
        std::println("vikings-remaster: joined {}", where);
        return Go{.to = Go::To::kLobby, .why = {}, .failed = false, .host = false, .level = 0, .page = false};
    }
    constexpr std::uint32_t kPatienceMs = 10000;
    if (connection_->now_ms() - began_ > kPatienceMs) {
        return fail(std::format("no answer from {}", where), "NO ANSWER");
    }
    return {};
}

Stepped ConnectingPlace::step(const Tick& tick, Ui& /*ui*/) {
    Stepped stepped;
    stepped.frames = offline_->held(false);
    stepped.cues.assign(stepped.frames.size(), {});
    (void)std::exchange(offline_->cues, {});
    if (tick.escaped) {  // given up: the title as it was
        stepped.go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0, .page = false};
    }
    return stepped;
}

// --- Lobby ---

LobbyPlace::LobbyPlace(Session& offline, std::unique_ptr<net::Connection> connection,
                       std::unique_ptr<net::HostLobby> lobby)
    : offline_{&offline}, connection_{std::move(connection)}, host_{std::move(lobby)} {}

LobbyPlace::LobbyPlace(Session& offline, std::unique_ptr<net::Connection> connection,
                       std::unique_ptr<net::GuestLobby> lobby)
    : offline_{&offline}, connection_{std::move(connection)}, guest_{std::move(lobby)} {}

void LobbyPlace::begin(Ui& ui) {
    Menu& menu = ui.open_menu();
    menu.open_lobby(connection_->hosting());
    (void)show_lobby(menu);  // (its first picture as it is, not empty: else a flash of it so)
    ui.redraw();
}

bool LobbyPlace::show_lobby(Menu& menu) {
    if (std::optional<int> level = menu.level_typed(); level && host_ != nullptr) {
        host_->set_slot(*level < 0 ? scene::kIntro : *level);  // (the others see it as it is typed)
    }
    std::vector<Menu::PlayerLine> lines;
    for (int who = 0; std::cmp_less(who, connection_->names().size()); ++who) {
        lines.push_back({.name = connection_->names().at(static_cast<std::size_t>(who)),
                         .network = round_trip_words(connection_->network_ms(who)),
                         .engine = round_trip_words(connection_->engine_ms(who))});
    }
    std::string start_with = start_words(*offline_, host_ != nullptr ? host_->slot() : guest_->slot());
    const int countdown = host_ != nullptr ? host_->countdown() : guest_->countdown();
    if (lines == menu.players() && start_with == menu.start() && countdown == menu.countdown()) {
        return false;
    }
    // A tick for each second of the countdown, the original's menus' own.
    tick_due_ = tick_due_ || (countdown > 0 && countdown != menu.countdown());
    menu.set_players(std::move(lines));
    menu.set_start(std::move(start_with));
    menu.set_countdown(countdown);
    return true;
}

namespace {
constexpr int kLobbyFade = 30;  // refreshes the lobby takes to go dark as its game starts
}  // namespace

Go LobbyPlace::refresh(Ui& ui) {
    if (started_) {  // going dark, the page with it; then the game (rising from black)
        fading_ = kLobbyFade - static_cast<int>(ui.refreshes() - fading_since_);
        if (fading_ <= 0) {
            return Go{.to = Go::To::kOnline, .why = {}, .failed = false, .host = false, .level = 0, .page = false};
        }
        ui.fade(fading_, kLobbyFade);
        return {};
    }
    auto polled = host_ != nullptr ? host_->poll() : guest_->poll();
    if (!polled) {
        std::println(stderr, "vikings-remaster: {}", polled.error());
        return Go{.to = Go::To::kOffline,
                  .why = said(polled.error()),
                  .failed = false,
                  .host = false,
                  .level = 0,
                  .page = false};
    }
    if (*polled) {
        started_ = **polled;
        fading_ = kLobbyFade;
        fading_since_ = ui.refreshes();
        return {};
    }
    if (Menu* menu = ui.menu(); menu != nullptr && menu->in_lobby() && show_lobby(*menu)) {
        ui.redraw();
    }
    return {};
}

Stepped LobbyPlace::step(const Tick& /*tick*/, Ui& ui) {
    Stepped stepped;
    const Menu* menu = ui.menu();
    stepped.frames = offline_->held(menu != nullptr && !menu->of_title());  // (its page under the logo, lit)
    stepped.cues = std::move(offline_->cues);
    if (started_) {
        for (Screen& picture : stepped.frames) {
            darken(picture, static_cast<float>(fading_) / static_cast<float>(kLobbyFade));
        }
    }
    if (std::exchange(tick_due_, false) && !stepped.frames.empty()) {  // (heard with the next picture)
        stepped.cues.resize(stepped.frames.size());
        stepped.cues.front().push_back({.kind = SoundCue::Kind::kEffect, .value = effect::kMenuMoves});
    }
    return stepped;
}

Place::Answer LobbyPlace::asked(Menu::Asked asked, Menu& menu) {
    switch (asked) {
        case Menu::Asked::kStart:  // the host's: it starts, after a countdown
            if (host_ != nullptr) {
                host_->set_slot(menu.level() < 0 ? scene::kIntro : menu.level());
                host_->start();
            }
            return {.handled = true, .go = {}};
        case Menu::Asked::kCancel:
            if (host_ != nullptr) {
                host_->cancel_start();
            }
            return {.handled = true, .go = {}};
        case Menu::Asked::kLeave:  // (the connection dropped: the title again)
            return {
                .handled = false,
                .go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0, .page = false}};
        default:
            return {};
    }
}

// --- Online ---

std::expected<std::unique_ptr<OnlinePlace>, std::string> OnlinePlace::open(std::unique_ptr<net::Connection> connection,
                                                                           net::Started started, const Needs& needs) {
    if (!started.state.empty()) {  // under way: the scene the state is of
        started.slot = Session::scene_of(started.state).value_or(0);
    }
    auto opened = Session::open(needs.dir, started.slot, needs.sound, Rules::kRemaster, needs.draw,
                                Online{.on = true, .players = started.players}, started.player);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    if (!started.state.empty()) {  // a game under way: its state as the host gave it
        if (const auto taken = (*opened)->take_state(started.state); !taken) {
            return std::unexpected{taken.error()};
        }
    }
    if (started.cheat) {
        (*opened)->cheat();
    }
    if (started.state.empty()) {
        // From the lobby, which has gone dark: of the original's level start (Session::open) only its tick and its fade
        // in, the start's picture rising from black as the original's level does, its sound with it (not two
        // seconds of black with the minimap already there). One joining under way fades in once he is there.
        constexpr std::size_t kTick = 4;
        constexpr std::size_t kFadeIn = 71;
        std::vector<Screen>& frames = (*opened)->start_frames;
        if (frames.size() > kTick + kFadeIn) {
            const auto dropped = static_cast<std::ptrdiff_t>(frames.size() - (kTick + kFadeIn));
            if (std::vector<std::vector<SoundCue>>& cues = (*opened)->cues; cues.size() == frames.size()) {
                cues.erase(cues.begin(), cues.begin() + dropped);
            }
            frames.erase(frames.begin(), frames.begin() + dropped);
        }
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const float share = i < kTick ? 0.0F : static_cast<float>(i + 1 - kTick) / static_cast<float>(kFadeIn);
            darken(frames[i], std::min(share, 1.0F));
        }
    }
    std::println("vikings-remaster: the game starts: player {}, {} step(s) of input delay", started.player + 1,
                 started.delay);
    return std::unique_ptr<OnlinePlace>{
        new OnlinePlace{std::move(connection), std::move(started), std::move(*opened), needs.logs}};
}

OnlinePlace::OnlinePlace(std::unique_ptr<net::Connection> connection, net::Started started,
                         std::unique_ptr<Session> session, std::filesystem::path logs)
    : connection_{std::move(connection)},
      started_{std::move(started)},
      session_{std::move(session)},
      game_{*session_},
      match_{*connection_, game_, started_},
      logs_{std::move(logs)},
      last_{session_->start_frames.empty() ? Screen{} : session_->start_frames.back()} {
    names_to_session();
}

void OnlinePlace::log_out_of_step() {
    const std::optional<std::uint32_t> out_of_step = match_.out_of_step();
    if (!out_of_step || logged_ || logs_.empty()) {
        return;
    }
    logged_ = true;  // what went before, for lv-replay --coop (once a game: the host's state puts it right)
    std::error_code ignored;
    std::filesystem::create_directories(logs_, ignored);
    const std::filesystem::path file =
        logs_ / std::format("coop-{}-player{}.log", connection_->identity().build, connection_->player() + 1);
    std::ofstream out{file};
    out << match_.log();
    std::println(stderr, "vikings-remaster: out of step with another machine at step {}, put right from the host's{}",
                 *out_of_step, out ? std::format(" (the game's log: {})", file.string()) : std::string{});
}

void OnlinePlace::names_to_session() {
    if (connection_->names() != names_given_) {
        names_given_ = connection_->names();
        session_->set_player_names(names_given_);
    }
}

std::vector<Menu::PlayerLine> OnlinePlace::player_lines() const {
    std::vector<Menu::PlayerLine> lines;
    const World& world = session_->world();
    for (int who = 0; who < world.player_count && std::cmp_less(who, connection_->names().size()); ++who) {
        if (!world.players.at(static_cast<std::size_t>(who)).here) {
            continue;  // (a place nobody has: not listed)
        }
        lines.push_back({.name = connection_->names().at(static_cast<std::size_t>(who)),
                         .network = round_trip_words(connection_->network_ms(who)),
                         .engine = round_trip_words(connection_->engine_ms(who))});
    }
    return lines;
}

Go OnlinePlace::refresh(Ui& ui) {
    if (rise_since_) {  // the pictures made from now on as dark as half a second of refreshes from black has left
        const int left = kFadeInRefreshes - static_cast<int>(ui.refreshes() - *rise_since_);
        if (left > 0) {
            session_->fade_in(left);
        } else {
            rise_since_.reset();
        }
    }
    if (leaving_) {
        return {};  // (fading out: what comes is the lobby's)
    }
    Menu* menu = ui.menu();
    session_->set_dimmed(menu != nullptr);  // (the game goes on under the menu: darkened, as offline)
    auto polled = match_.poll();
    log_out_of_step();
    if (!polled) {
        const std::string& why = polled.error();
        std::println(stderr, "vikings-remaster: {}", why);
        return Go{.to = Go::To::kOffline, .why = said(why), .failed = false, .host = false, .level = 0, .page = false};
    }
    names_to_session();
    if (*polled == net::Match::Polled::kBack) {  // the host: back to the lobby, once the game has faded out
        leaving_ = true;
        return {};
    }
    if (menu != nullptr) {  // its page of the players, as they are now
        if (std::vector<Menu::PlayerLine> lines = player_lines(); lines != menu->players()) {
            menu->set_players(std::move(lines));
            ui.redraw();
        }
    }
    return {};
}

Stepped OnlinePlace::step(const Tick& tick, Ui& ui) {
    Stepped stepped;
    if (leaving_) {  // the last picture fading out, half a second; then the lobby
        constexpr int kFadeOut = 30;
        stepped.took_input = false;
        if (std::exchange(faded_, true)) {
            stepped.go = Go{.to = Go::To::kLobby, .why = {}, .failed = false, .host = false, .level = 0, .page = false};
            stepped.frames = {faded(0, kFadeOut)};
            stepped.cues = {{}};
            return stepped;
        }
        for (int refresh = 1; refresh <= kFadeOut; ++refresh) {
            stepped.frames.push_back(faded(kFadeOut - refresh, kFadeOut));
            stepped.cues.emplace_back();
        }
        return stepped;
    }
    const bool in_menu = ui.menu() != nullptr;
    if (!connection_->hosting() && tick.menu_keys.quit) {  // a guest's F10 or Alt+X: he leaves the game
        stepped.go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0, .page = false};
        return stepped;
    }
    // This machine's input when it can be given (in the menu: nothing held, the game goes on), and the next step
    // when it can run; else the last picture again for this refresh.
    stepped.took_input = false;
    if (match_.wants_input()) {
        match_.give(pack(session_->take_input(in_menu ? std::uint16_t{0} : tick.keys,
                                              in_menu ? Play::MenuKeys{} : tick.menu_keys, tick.pace),
                         connection_->player()));
        stepped.took_input = true;
    }
    bool ran = false;
    if (match_.joining()) {  // joined under way: up to his arrival at once, unseen, then fading in
        const bool there = match_.run_to_arrival();
        session_->drop_carried_effects();
        if (there) {
            session_->fade_in(kFadeInRefreshes);
            rise_since_ = ui.refreshes();
        }
        ran = there && match_.step();
    } else {
        ran = match_.step();
    }
    auto flushed = match_.flush();  // (this machine's input on its way at once)
    log_out_of_step();
    if (!flushed) {
        const std::string& why = flushed.error();
        std::println(stderr, "vikings-remaster: {}", why);
        stepped.go =
            Go{.to = Go::To::kOffline, .why = said(why), .failed = false, .host = false, .level = 0, .page = false};
    }
    if (ran) {
        stepped.frames = game_.take_pictures();
        stepped.cues = std::move(session_->cues);
        if (!stepped.frames.empty()) {
            last_ = stepped.frames.back();
        }
    } else {
        stepped.frames = {last_};
        stepped.cues = {{}};
    }
    if (session_->quit()) {  // the game ended (the host quit, or a scene can't be loaded): for everyone
        std::string why;
        if (!session_->error().empty()) {
            std::println(stderr, "vikings-remaster: {}", session_->error());
            why = said(session_->error());
        } else if (!connection_->hosting()) {
            why = said("the host ended the game");
        }
        // (The host's game is his to end, as offline; the others are back at their title.)
        stepped.go = Go{.to = connection_->hosting() ? Go::To::kDone : Go::To::kOffline,
                        .why = why,
                        .failed = false,
                        .host = false,
                        .level = 0,
                        .page = false};
    }
    return stepped;
}

Place::Answer OnlinePlace::asked(Menu::Asked asked, Menu& /*menu*/) {
    if (asked == Menu::Asked::kQuit && !connection_->hosting()) {  // a guest quitting leaves the game
        return {.handled = false,
                .go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0, .page = false}};
    }
    if (asked == Menu::Asked::kLobby && connection_->hosting()) {  // the host: everyone back to the lobby, told now
        match_.back_to_lobby();
        leaving_ = true;  // (the game fades out first)
    }
    return {};
}

CoopOverlay OnlinePlace::overlay() const {
    constexpr std::uint32_t kHeldMs = 300;  // a third of a second before the box says so
    CoopOverlay overlay;
    if (leaving_) {
        return overlay;
    }
    const std::optional<int> whom = match_.waiting_for();
    const std::optional<std::uint32_t> held = match_.held_ms();
    if (whom && held && *held >= kHeldMs && std::cmp_less(*whom, connection_->names().size())) {
        // With the seconds left before the one held up for is let go (the host's ten).
        const std::uint32_t left = (net::Match::kHeldMs - std::min(*held, net::Match::kHeldMs) + 999) / 1000;
        overlay.waiting =
            std::format("WAITING FOR {}  {}", connection_->names().at(static_cast<std::size_t>(*whom)), left);
    }
    if (match_.unsteady()) {  // (the host's state taken again and again: a fault; he may stay, or leave from the menu)
        overlay.warning = "OUT OF STEP WITH HOST";
    }
    return overlay;
}

void OnlinePlace::leave(const Go& go) {
    if (go.to != Go::To::kLobby) {
        match_.quit();  // (a guest tells the host he goes)
    }
}

std::unique_ptr<net::Connection> OnlinePlace::take_connection() {
    return std::move(connection_);
}

Screen OnlinePlace::faded(int left, int of) const {
    Screen picture = last_;
    darken(picture, static_cast<float>(left) / static_cast<float>(of));
    if (left == 0) {
        picture.minimap.reset();  // (all dark: as what comes is, the lobby, which has none)
    }
    return picture;
}

// --- The moves ---

void Places::go(const Go& go) {
    if (go.to == Go::To::kStay) {
        return;
    }
    current_->leave(go);
    failed_ = failed_ || go.failed;
    switch (go.to) {
        case Go::To::kStay:
            return;
        case Go::To::kDone:
            done_ = true;
            return;
        case Go::To::kOffline:
            offline(hosting_, go.why, go.page);
            return;
        case Go::To::kConnecting:  // JOIN GAME
            hosting_ = false;
            enter(std::make_unique<ConnectingPlace>(offline_->session(), needs_.join, identity(),
                                                    needs_.settings->join_address));
            return;
        case Go::To::kLobby:
            if (current_ == offline_) {  // HOST GAME: hosting on the port, the lobby at once
                hosting_ = true;
                host(go.level);
            } else if (connecting_ != nullptr) {  // a guest given a place
                enter(std::make_unique<LobbyPlace>(offline_->session(), connecting_->take_connection(),
                                                   connecting_->take_lobby()));
            } else if (game_ != nullptr) {  // back from the game, the connection kept
                const int slot = game_->slot();
                std::unique_ptr<net::Connection> connection = game_->take_connection();
                net::Connection& kept = *connection;
                enter(kept.hosting()
                          ? std::make_unique<LobbyPlace>(offline_->session(), std::move(connection),
                                                         std::make_unique<net::HostLobby>(kept, slot, false))
                          : std::make_unique<LobbyPlace>(offline_->session(), std::move(connection),
                                                         std::make_unique<net::GuestLobby>(kept, true, slot)));
            }
            return;
        case Go::To::kOnline:  // the game starts, or a guest comes into it under way
            if (lobby_ != nullptr) {
                if (const std::optional<net::Started> started = lobby_->started()) {
                    online(lobby_->take_connection(), *started);
                }
            } else if (connecting_ != nullptr) {
                if (const std::optional<net::Started> started = connecting_->started()) {
                    online(connecting_->take_connection(), *started);
                }
            }
            return;
    }
}

net::Identity Places::identity() const {
    net::Identity identity = needs_.identity;
    identity.name = needs_.settings->name;
    return identity;
}

void Places::offline(bool host, const std::string& why, bool page) {
    current_ = offline_;
    connecting_.reset();  // (the connection dropped: the others see this one go now)
    lobby_.reset();
    game_.reset();
    if (!why.empty() || page) {
        offline_->tell(host, why);
    }
}

void Places::host(int level) {
    const int port = needs_.settings->host_port;
    auto hosted = needs_.host(static_cast<std::uint16_t>(port));
    if (!hosted) {
        std::println(stderr, "vikings-remaster: {}", hosted.error());
        offline(true, std::format("CAN'T HOST ON PORT {}", port));
        return;
    }
    std::println("vikings-remaster: hosting on port {}", port);
    auto connection = std::make_unique<net::Connection>(std::move(*hosted), identity());
    net::Connection& made = *connection;
    auto lobby = std::make_unique<net::HostLobby>(made, level < 0 ? scene::kIntro : level, false);
    enter(std::make_unique<LobbyPlace>(offline_->session(), std::move(connection), std::move(lobby)));
}

void Places::online(std::unique_ptr<net::Connection> connection, const net::Started& started) {
    auto opened = OnlinePlace::open(std::move(connection), started, needs_.online);
    if (!opened) {
        std::println(stderr, "vikings-remaster: {}", opened.error());
        offline(hosting_, said(opened.error()));
        return;
    }
    enter(std::move(*opened));
}

void Places::enter(std::unique_ptr<ConnectingPlace> place) {
    current_ = place.get();
    connecting_ = std::move(place);
    lobby_.reset();
    game_.reset();
}

void Places::enter(std::unique_ptr<LobbyPlace> place) {
    current_ = place.get();
    lobby_ = std::move(place);
    connecting_.reset();
    game_.reset();
}

void Places::enter(std::unique_ptr<OnlinePlace> place) {
    current_ = place.get();
    game_ = std::move(place);
    connecting_.reset();
    lobby_.reset();
}

Ui headless_ui(std::optional<Menu>& menu, Place& place, Settings settings, std::function<std::uint64_t()> clock) {
    return Ui{
        menu,
        [&menu, &place, settings = std::move(settings)]() -> Menu& { return menu.emplace(settings, place.choices()); },
        {},
        {},
        std::move(clock)};
}

}  // namespace lvr
