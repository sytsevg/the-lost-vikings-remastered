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
        stepped.go =
            Go{.to = Go::To::kDone, .why = {}, .failed = !session_->error().empty(), .host = false, .level = 0};
        return stepped;
    }
    if (may_go_online_ && session_->world().title.online != 0) {  // the title's HOST GAME or JOIN GAME
        open_page(ui, session_->world().title.online == 1, {});
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
                         .level = menu.level()}};
    }
    return {};
}

// --- Connecting ---

ConnectingPlace::ConnectingPlace(Session& offline, Join join, net::Identity identity, const std::string& address)
    : offline_{&offline}, join_{std::move(join)}, identity_{std::move(identity)} {
    std::tie(address_, port_) = split_address(address);
}

void ConnectingPlace::begin(Ui& ui) {
    ui.open_menu().open_join();
    ui.redraw();
}

Place::Answer ConnectingPlace::asked(Menu::Asked asked, Menu& /*menu*/) {
    if (asked == Menu::Asked::kResume) {  // BACK, or Escape: given up, the title as it was
        return {.handled = false,
                .go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0}};
    }
    return {.handled = true, .go = {}};  // (JOIN again, or a line typed into: it is under way already)
}

Go ConnectingPlace::refresh(Ui& ui) {
    const std::string where = std::format("{}:{}", address_, port_);
    const auto back = [](std::string why) {
        return Go{.to = Go::To::kOffline, .why = std::move(why), .failed = false, .host = false, .level = 0};
    };
    constexpr int kTurnEvery = 6;  // refreshes a phase of the turning mark
    if (Menu* menu = ui.menu(); menu != nullptr && refreshes_ % kTurnEvery == 0) {
        menu->set_joining(refreshes_ / kTurnEvery % 4);
        ui.redraw();
    }
    if (connection_ == nullptr) {
        if (++refreshes_ < 2) {
            return {};  // (the page shown first: joining waits for the connection)
        }
        auto joined = join_(address_, port_);
        if (!joined) {
            std::println(stderr, "vikings-remaster: {}", joined.error());
            return back(said(std::format("NO GAME AT {}", where)));
        }
        connection_ = std::make_unique<net::Connection>(std::move(*joined), identity_);
        lobby_ = std::make_unique<net::GuestLobby>(*connection_, false);
        began_ = connection_->now_ms();
    }
    auto polled = lobby_->poll();
    if (!polled) {
        std::println(stderr, "vikings-remaster: {}", polled.error());
        return back(said(polled.error()));
    }
    if (*polled) {  // the game is under way: into it
        started_ = **polled;
        std::println("vikings-remaster: joined {}", where);
        return Go{.to = Go::To::kOnline, .why = {}, .failed = false, .host = false, .level = 0};
    }
    if (lobby_->welcomed()) {
        std::println("vikings-remaster: joined {}", where);
        return Go{.to = Go::To::kLobby, .why = {}, .failed = false, .host = false, .level = 0};
    }
    constexpr std::uint32_t kPatienceMs = 10000;
    if (connection_->now_ms() - began_ > kPatienceMs) {
        return back(said(std::format("NO ANSWER FROM {}", where)));
    }
    ++refreshes_;
    return {};
}

Stepped ConnectingPlace::step(const Tick& tick, Ui& /*ui*/) {
    Stepped stepped;
    stepped.frames = offline_->held(false);
    stepped.cues.assign(stepped.frames.size(), {});
    (void)std::exchange(offline_->cues, {});
    if (tick.escaped) {  // given up: the title as it was
        stepped.go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0};
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
    ui.open_menu().open_lobby(connection_->hosting());
    ui.redraw();
}

Go LobbyPlace::refresh(Ui& ui) {
    auto polled = host_ != nullptr ? host_->poll() : guest_->poll();
    if (!polled) {
        std::println(stderr, "vikings-remaster: {}", polled.error());
        return Go{.to = Go::To::kOffline, .why = said(polled.error()), .failed = false, .host = false, .level = 0};
    }
    if (*polled) {
        started_ = **polled;
        return Go{.to = Go::To::kOnline, .why = {}, .failed = false, .host = false, .level = 0};
    }
    if (Menu* menu = ui.menu(); menu != nullptr && menu->in_lobby()) {
        if (const std::optional<int> level = menu->level_typed(); level && host_ != nullptr) {
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
        if (lines != menu->players() || start_with != menu->start() || countdown != menu->countdown()) {
            menu->set_players(std::move(lines));
            menu->set_start(std::move(start_with));
            menu->set_countdown(countdown);
            ui.redraw();
        }
    }
    return {};
}

Stepped LobbyPlace::step(const Tick& /*tick*/, Ui& ui) {
    Stepped stepped;
    const Menu* menu = ui.menu();
    stepped.frames = offline_->held(menu != nullptr && !menu->of_title());  // (its page under the logo, lit)
    stepped.cues = std::move(offline_->cues);
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
            return {.handled = false,
                    .go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0}};
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
        (*opened)->fade_in();  // (from the lobby; one joining under way fades in once he is there)
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

std::string OnlinePlace::logged(const std::string& error) {
    if (!match_.out_of_step() || logged_ || logs_.empty()) {
        return error;
    }
    logged_ = true;  // what went before, for lv-replay --coop
    std::error_code ignored;
    std::filesystem::create_directories(logs_, ignored);
    const std::filesystem::path file =
        logs_ / std::format("coop-{}-player{}.log", connection_->identity().build, connection_->player() + 1);
    std::ofstream out{file};
    out << match_.log();
    return out ? std::format("{} (the game's log: {})", error, file.string()) : error;
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
    if (leaving_) {
        return {};  // (fading out: what comes is the lobby's)
    }
    Menu* menu = ui.menu();
    session_->set_dimmed(menu != nullptr);  // (the game goes on under the menu: darkened, as offline)
    auto polled = match_.poll();
    if (!polled) {
        const std::string why = logged(polled.error());
        std::println(stderr, "vikings-remaster: {}", why);
        return Go{.to = Go::To::kOffline, .why = said(why), .failed = false, .host = false, .level = 0};
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
            stepped.go = Go{.to = Go::To::kLobby, .why = {}, .failed = false, .host = false, .level = 0};
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
        stepped.go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0};
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
            session_->fade_in();
        }
        ran = there && match_.step();
    } else {
        ran = match_.step();
    }
    if (auto flushed = match_.flush(); !flushed) {  // (this machine's input on its way at once)
        const std::string why = logged(flushed.error());
        std::println(stderr, "vikings-remaster: {}", why);
        stepped.go = Go{.to = Go::To::kOffline, .why = said(why), .failed = false, .host = false, .level = 0};
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
                        .level = 0};
    }
    return stepped;
}

Place::Answer OnlinePlace::asked(Menu::Asked asked, Menu& /*menu*/) {
    if (asked == Menu::Asked::kQuit && !connection_->hosting()) {  // a guest quitting leaves the game
        return {.handled = false,
                .go = Go{.to = Go::To::kOffline, .why = {}, .failed = false, .host = false, .level = 0}};
    }
    if (asked == Menu::Asked::kLobby && connection_->hosting()) {  // the host: everyone back to the lobby, told now
        match_.back_to_lobby();
        leaving_ = true;  // (the game fades out first)
    }
    return {};
}

CoopOverlay OnlinePlace::overlay() const {
    constexpr int kWaitedRefreshes = 18;  // a third of a second before the box says so
    CoopOverlay overlay;
    if (leaving_) {
        return overlay;
    }
    const std::optional<int> whom = match_.waiting_for();
    if (whom && match_.waited() >= kWaitedRefreshes && std::cmp_less(*whom, connection_->names().size())) {
        overlay.waiting = "WAITING FOR " + connection_->names().at(static_cast<std::size_t>(*whom));
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
    for (assets::Rgb& colour : picture.palette) {
        colour = {.r = static_cast<std::uint8_t>(colour.r * left / of),
                  .g = static_cast<std::uint8_t>(colour.g * left / of),
                  .b = static_cast<std::uint8_t>(colour.b * left / of)};
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
            offline(hosting_, go.why);
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

void Places::offline(bool host, const std::string& why) {
    current_ = offline_;
    connecting_.reset();  // (the connection dropped: the others see this one go now)
    lobby_.reset();
    game_.reset();
    if (!why.empty()) {
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

Ui headless_ui(std::optional<Menu>& menu, Place& place, Settings settings) {
    return Ui{
        menu,
        [&menu, &place, settings = std::move(settings)]() -> Menu& { return menu.emplace(settings, place.choices()); },
        {}};
}

}  // namespace lvr
