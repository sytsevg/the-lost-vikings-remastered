// vikings-remaster, the game on the engine (ADR-0017): plays The Lost Vikings from the original's files
// (gamedata/original by default) in the classic look, as the original starts: the logos, the title, and on from there.
// A tick every three refreshes of an artificial 59.71 Hz display; the keyboard as the original's handler reads it
// (docs/original/input.md).
//
//   vikings-remaster [--data DIR] [--slot N] [--keys FILE] [--soundfont FILE] [--rules vanilla|remaster] [--cheat]
//                    [--slow-frames]
//       --profile DIR: where Remaster rules keep the player's profile (what the minimap has seen, the settings); by
//       default the folder `profiles` beside this program.
//       --slow-frames: a step or a picture's showing that takes long is reported on the error output
//       --showcase N: under Remaster rules, the places where the Modern visuals' effects are seen (showcase.hpp),
//       from the Nth on: each with the view held on it; PgDn or ] and PgUp or [ go to the next and the one before (with
//       --dump: the Nth, as --whole writes it); the last ones put a Viking where he dies, starting the level again
//       --cheat: the game's cheat on from the start, as if its password had been typed at the title: F5 and F6
//       change the level, and under Remaster rules F7 and F8 are a quicksave and its load.
//       play, from scene N (default 39, the first logo; 0-36 are the levels); FILE: other keys, lines
//       "KEY NAME = action" (input/input.hpp).
//       Vanilla rules play the Classic sound, the original's, only (the user). Under Remaster rules the sound, the
//       screen's look and the visual effects are the menu's (Escape in a level), kept in the profile (settings.txt,
//       profile.hpp), not start-up options.
//   vikings-remaster [--data DIR] [--slot N] --dump STEPS FILE.png [--wav FILE.wav] [--zoom BY AT_STEP]
//                    [--hold KEY] [--tap KEY AT_STEP] [--rewind FROM UNTIL] [--whole AT_STEP] [--moments N]
//                    [--sound classic|modern] [--shader none|crt|xbr] [--no-effects] [--menu KEYS]
//       run STEPS steps without a window and without a key (or with KEY held, a name as in a keys file; and the
//       key of --tap alone at step AT_STEP, for what waits for a key to be pressed: a level's password), write
//       the last picture (and the sound). Under Remaster rules, --rewind: the rewind held from step FROM until
//       step UNTIL; --whole: the view is the whole level from step AT_STEP on, and the picture written is all of
//       it (twice the size, without the HUD); --moments: the last step's pictures each at N moments of its refresh
//       as well, FILE-P-M.png, for what moves between ticks; --menu: the menu over the last picture, with its keys
//       KEYS pressed (u, d, l, r: up, down, left, right; c choose, b back, x clear), as a still of it. A dump has no
//       menu or profile: under Remaster rules --sound the version of the sound written, --shader the screen's look
//       (as it is, Hyllian's CRT, or xBR's rounding, gpu.hpp), --no-effects the pictures without the Modern
//       visuals' effects (effects.hpp).
//
// Keys under Vanilla rules: arrows, Ctrl/Home and Ins/PgUp (switch Viking), E, S, D, F/Space/Enter, Esc/P (pause;
// Y, N), Tab (inventory), Alt+X, Alt+Q, F10 (quit) as in the original; a gamepad as the original's joystick
// (A/B/X/Y: F, D, E, S), the d-pad or left stick, shoulders to switch Vikings, Start to pause, Back for the
// inventory. Under Remaster rules the player's (input.hpp, remaster_controls(), changed in the menu): Escape or
// Start opens the menu in a level being played; a stick pushed part way walks slower.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/assets.hpp"
#include "image/png.hpp"
#include "input/input.hpp"
#include "lvr/gpu.hpp"
#include "lvr/menu.hpp"
#include "lvr/online.hpp"
#include "lvr/profile.hpp"
#include "lvr/render.hpp"
#include "lvr/session.hpp"
#include "lvr/showcase.hpp"

namespace lvr {
namespace {

constexpr int kWidth = kScreenWidth;
constexpr int kHeight = kScreenHeight;
constexpr int kInitialScale = 3;
constexpr const char* kWindowTitle = "The Lost Vikings";
// --slow-frames: a step longer than this is reported (it holds up the window's next picture), and a picture's
// showing longer than the other (which includes the wait for the screen's refresh: up to a 60 Hz one's).
constexpr double kSlowStepMilliseconds = 8;
constexpr double kSlowShowMilliseconds = 20;
constexpr std::uint16_t kDefaultPort = 47230;  // co-op online's, when --join gives none

bool write_wav(const std::filesystem::path& path, const std::vector<std::int16_t>& samples, std::uint32_t rate) {
    std::ofstream out(path, std::ios::binary);
    const auto put = [&out](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            out.put(static_cast<char>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
        }
    };
    const auto data = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4);
    put(36 + data, 4);
    out.write("WAVEfmt ", 8);
    put(16, 4);
    put(1, 2);  // PCM
    put(2, 2);  // stereo
    put(rate, 4);
    put(rate * 4, 4);
    put(4, 2);
    put(16, 2);
    out.write("data", 4);
    put(data, 4);
    for (const std::int16_t sample : samples) {
        put(static_cast<std::uint16_t>(sample), 2);
    }
    return static_cast<bool>(out);
}

// Remaster rules' settings as the game has them now: its keys change some of them too (the zoom, the minimap, the
// sound, the screen).
Settings settings_now(const Session& session, const GpuRenderer* gpu, Settings settings) {
    if (session.has_modern()) {  // (without a SoundFont the player's choice is kept for when there is one)
        settings.modern_sound = session.modern();
    }
    settings.zoom = session.zoom_level();
    settings.minimap = session.minimap_wanted();
    if (gpu != nullptr) {
        settings.visual_fx = session.effects();
        settings.screen = std::string{GpuRenderer::name_of(gpu->look())};
    }
    return settings;
}

// And the game set as they say.
void use_settings(Session& session, GpuRenderer* gpu, const Settings& settings) {
    session.set_modern(settings.modern_sound);
    session.set_volume(settings.music, settings.sound_fx);
    session.set_effects(settings.visual_fx && gpu != nullptr);
    session.set_minimap(settings.minimap);
    if (settings.zoom != session.zoom_level()) {
        session.set_zoom_level(settings.zoom);
    }
    if (gpu != nullptr) {
        gpu->set_look(GpuRenderer::look_named(settings.screen).value_or(GpuRenderer::Look::kNone));
    }
}

// What the menu has to choose from: the Modern sound if there is one, and the screen's looks.
Menu::Choices menu_choices(const Session& session) {
    Menu::Choices choices{.has_modern = session.has_modern(), .screens = {}, .online = session.online()};
    for (const GpuRenderer::Look look : GpuRenderer::kLooks) {
        choices.screens.emplace_back(GpuRenderer::name_of(look));
    }
    return choices;
}

// What the GPU renderer shows of `screen`, three times the original's size, as a PNG.
bool save_shown(GpuRenderer& gpu, const Screen& screen, const Minimap* minimap, const std::filesystem::path& out,
                float between = 0) {
    constexpr int kScale = 3;
    auto pixels = gpu.shown(screen, kScale, minimap, between);
    if (!pixels) {
        std::println(stderr, "vikings-remaster: {}", pixels.error());
        return false;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(kWidth * kScale, kHeight * kScale, SDL_PIXELFORMAT_RGBA32,
                                                 pixels->data(), kWidth * kScale * 4);
    const bool saved = surface != nullptr && SDL_SavePNG(surface, out.string().c_str());
    SDL_DestroySurface(surface);
    if (!saved) {
        std::println(stderr, "vikings-remaster: {}: {}", out.string(), SDL_GetError());
    }
    return saved;
}

// Without a window: `steps` steps with no key held, the last picture written as a PNG, the sound as a WAV. With
// `gpu` (Remaster rules) the picture is the one it shows, and `zoom` steps out are asked for before step `zoom_at`.
// Co-op online (ADR-0012): whom the game waits for, once it has waited a while. (The players' names are under
// the portraits: Session::set_player_names.)
CoopOverlay coop_overlay(const NetGame& net) {
    constexpr int kWaitedRefreshes = 18;  // a third of a second before the box says so
    CoopOverlay overlay;
    if (const std::optional<std::string> whom = net.waiting_for(); whom && net.waited() >= kWaitedRefreshes) {
        overlay.waiting = "WAITING FOR " + *whom;
    }
    return overlay;
}

// Online: the menu's players: each one's name and his round trips to the host, the network's and the game's, as the
// host measures them (the same on every machine; the host's own: none).
std::vector<Menu::PlayerLine> player_lines(const Session& session, const NetGame& net) {
    const auto shown = [](std::optional<int> ms) { return ms ? std::format("{} MS", *ms) : std::string{"-"}; };
    std::vector<Menu::PlayerLine> lines;
    for (int who = 0; who < session.world().player_count && std::cmp_less(who, net.names().size()); ++who) {
        if (!session.world().players.at(static_cast<std::size_t>(who)).here) {
            continue;  // (a place nobody has: not listed)
        }
        lines.push_back({.name = net.names().at(static_cast<std::size_t>(who)),
                         .network = shown(net.network_ms(who)),
                         .engine = shown(net.engine_ms(who))});
    }
    return lines;
}

// The lobby, before the game: in the terminal for now (hosting and joining from the menu come later). Who is
// there as they join; the game starts for all once the host has everyone.
std::expected<NetGame::Joined, std::string> wait_in_lobby(NetGame& net) {
    std::size_t shown = 0;
    while (true) {
        auto lobby = net.lobby();
        if (!lobby) {
            return std::unexpected{lobby.error()};
        }
        if (net.names().size() != shown) {
            shown = net.names().size();
            std::string names;
            for (const std::string& name : net.names()) {
                names += (names.empty() ? "" : ", ") + name;
            }
            std::println("vikings-remaster: in the game: {}", names);
        }
        if (*lobby) {
            return **lobby;
        }
        SDL_Delay(5);
    }
}

struct DumpOptions {
    int zoom = 0;     // this many steps of zoom out ...
    int zoom_at = 0;  // ... asked for before this step
    std::uint16_t held = 0;
    std::vector<std::pair<int, std::uint16_t>> taps;  // keys pressed once (--tap, as often as given): at which step
    int rewind_from = -1;                             // the rewind held from this step ...
    int rewind_until = -1;                            // ... until this one
    int whole_at = -1;                                // the view the whole level from this step on
    Showcase* showcase = nullptr;                     // its place, written as --whole writes the whole level
    int moments = 0;
    // The menu over the last picture (Remaster rules), with these of its keys pressed: u, d, l, r (up, down, left,
    // right), c (choose), b (back), x (clear); none: as it opens.
    std::optional<std::string> menu;
};

int dump(Session& session, int steps, const std::filesystem::path& out, const std::filesystem::path& wav,
         GpuRenderer* gpu, const DumpOptions& options, NetGame* net) {
    Screen last = session.start_frames.empty() ? Screen{} : session.start_frames.back();
    std::vector<std::int16_t> sound = std::move(session.samples);
    session.samples.clear();
    std::size_t refreshes = session.start_frames.size();
    std::vector<Screen> frames;
    for (int step = 0; step < steps && !session.quit(); ++step) {
        if (step == options.zoom_at && options.zoom != 0 && !session.zoom(options.zoom)) {
            std::println(stderr, "--zoom: step {} is not in a level being played (Remaster rules): no zoom", step);
        }
        session.rewind(step >= options.rewind_from && step < options.rewind_until);
        if (step == options.whole_at && !session.view_whole_level()) {
            std::println(stderr, "--whole: step {} is not in a level being played (Remaster rules)", step);
        }
        std::uint16_t keys = options.held;
        for (const auto& [at, tapped] : options.taps) {
            if (at == step) {
                keys = static_cast<std::uint16_t>(unsigned{keys} | unsigned{tapped});
            }
        }
        if (net != nullptr) {  // online: the step once every input allows it, what comes taken meanwhile
            std::optional<std::vector<Screen>> pictures;
            while (!pictures) {
                if (const auto polled = net->poll(); !polled) {
                    std::println(stderr, "vikings-remaster: {}", polled.error());
                    return EXIT_FAILURE;
                }
                if (net->wants_input()) {
                    net->give(session.take_input(keys, {}, {}));
                }
                pictures = net->step();
                (void)net->flush();
                if (!pictures) {
                    SDL_Delay(1);
                }
            }
            frames = std::move(*pictures);
        } else {
            frames = session.step(options.showcase != nullptr ? options.showcase->before_step(session, keys) : keys);
        }
        refreshes += frames.size();
        if (!frames.empty()) {
            last = frames.back();
        }
        sound.insert(sound.end(), session.samples.begin(), session.samples.end());
        session.samples.clear();
    }
    if (!session.error().empty()) {
        std::println(stderr, "vikings-remaster: {}", session.error());
        return EXIT_FAILURE;
    }
    if (net != nullptr) {  // a second for the others to finish theirs, before this machine is gone
        const std::uint64_t until = SDL_GetTicks() + 1000;
        while (SDL_GetTicks() < until && net->poll()) {
            SDL_Delay(2);
        }
        if (gpu != nullptr) {
            gpu->set_overlay(coop_picture(coop_overlay(*net), session.font()));
        }
    }
    std::println("scene {} after {} refreshes", session.scene(), refreshes);
    if (gpu != nullptr && (options.whole_at >= 0 || options.showcase != nullptr)) {
        constexpr int kWholeScale = 2;
        last.motion.phase = -1;  // (as the tick left it)
        auto whole = gpu->shown_whole(last, kWholeScale);
        SDL_Surface* surface = whole ? SDL_CreateSurfaceFrom(whole->width, whole->height, SDL_PIXELFORMAT_RGBA32,
                                                             whole->pixels.data(), whole->width * 4)
                                     : nullptr;
        const bool saved = surface != nullptr && SDL_SavePNG(surface, out.string().c_str());
        SDL_DestroySurface(surface);
        if (!saved) {
            std::println(stderr, "vikings-remaster: {}: {}", out.string(), whole ? SDL_GetError() : whole.error());
            return EXIT_FAILURE;
        }
    } else if (gpu != nullptr) {
        if (options.menu) {
            Menu menu{settings_now(session, gpu, Settings{}), menu_choices(session)};
            if (net != nullptr) {
                menu.set_players(player_lines(session, *net));
            }
            for (const char key : *options.menu) {
                constexpr std::string_view kKeys = "udlrcbx";
                if (const auto at = kKeys.find(key); at != std::string_view::npos) {
                    (void)menu.press(static_cast<Menu::Key>(at));
                }
            }
            // What it changed, as the game holds (a zoom's change of view all the way, a third of a second).
            use_settings(session, gpu, menu.settings());
            constexpr int kHeldSteps = 10;
            for (int step = 0; step < kHeldSteps; ++step) {
                last = session.held().back();
            }
            gpu->set_overlay(menu_picture(menu, session.font()));
        }
        if (!save_shown(*gpu, last, session.minimap(), out)) {
            return EXIT_FAILURE;
        }
        for (std::size_t picture = 0; picture < frames.size() && options.moments > 0; ++picture) {
            for (int moment = 0; moment < options.moments; ++moment) {
                std::filesystem::path each = out;
                each.replace_filename(
                    std::format("{}-{}-{}{}", out.stem().string(), picture, moment, out.extension().string()));
                const float between = static_cast<float>(moment) / static_cast<float>(options.moments);
                if (!save_shown(*gpu, frames[picture], session.minimap(), each, between)) {
                    return EXIT_FAILURE;
                }
            }
        }
    } else {
        const assets::IndexedImage image{.width = kScreenWidth, .height = kScreenHeight, .pixels = last.pixels};
        if (auto saved = image::save_indexed_png(out, image, last.palette); !saved) {
            std::println(stderr, "vikings-remaster: {}", saved.error());
            return EXIT_FAILURE;
        }
    }
    if (!wav.empty() && !write_wav(wav, sound, Session::sample_rate())) {
        std::println(stderr, "vikings-remaster: can't write {}", wav.string());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// The gamepads as input.hpp reads them: each of SDL's buttons, the sticks' four directions and the triggers, 0 to 1;
// with several, the furthest any is pushed (the original or-s the joystick's input into the keyboard's).
input::PadReading pads_reading(const std::vector<SDL_Gamepad*>& pads) {
    static const std::array<std::optional<std::size_t>, SDL_GAMEPAD_BUTTON_COUNT> buttons = [] {
        std::array<std::optional<std::size_t>, SDL_GAMEPAD_BUTTON_COUNT> found{};
        for (std::size_t b = 0; b < found.size(); ++b) {
            const char* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(b));
            found.at(b) = name != nullptr ? input::pad_input_index(name) : std::nullopt;
        }
        return found;
    }();
    input::PadReading reading{};
    const auto put = [&reading](std::string_view input, float pushed) {
        if (const auto index = input::pad_input_index(input)) {
            reading.at(*index) = std::max(reading.at(*index), pushed);
        }
    };
    for (SDL_Gamepad* pad : pads) {
        for (std::size_t b = 0; b < buttons.size(); ++b) {
            const std::optional<std::size_t> index = buttons.at(b);
            if (index && SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b))) {
                reading.at(*index) = 1;
            }
        }
        // (A stick half way is 16384 of its 32768 each way, as the original's joystick reading is cut.)
        const auto axis = [&](SDL_GamepadAxis axis, std::string_view less, std::string_view more) {
            const float value = static_cast<float>(SDL_GetGamepadAxis(pad, axis)) / 32768.0F;
            put(less, std::max(-value, 0.0F));
            put(more, std::max(value, 0.0F));
        };
        axis(SDL_GAMEPAD_AXIS_LEFTX, "leftx-", "leftx+");
        axis(SDL_GAMEPAD_AXIS_LEFTY, "lefty-", "lefty+");
        axis(SDL_GAMEPAD_AXIS_RIGHTX, "rightx-", "rightx+");
        axis(SDL_GAMEPAD_AXIS_RIGHTY, "righty-", "righty+");
        axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, "", "lefttrigger");
        axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, "", "righttrigger");
    }
    return reading;
}

// Whether any key `controls` has for `action` is held.
bool keys_held(const input::Controls& controls, input::Action action) {
    const bool* held = SDL_GetKeyboardState(nullptr);
    for (const std::string& name : controls.keys.at(static_cast<std::size_t>(action))) {
        const SDL_Scancode key = SDL_GetScancodeFromName(name.c_str());
        if (key != SDL_SCANCODE_UNKNOWN && held[key]) {
            return true;
        }
    }
    return false;
}

// The sound card wants more sound: `additional` bytes of it, made now.
void sound_wanted(void* userdata, SDL_AudioStream* stream, int additional, int /*total*/) {
    if (additional <= 0) {
        return;
    }
    auto& session = *static_cast<Session*>(userdata);
    static std::vector<std::int16_t> made;  // the sound card's thread only
    made.clear();
    session.render_live(static_cast<std::size_t>(additional) / (2 * sizeof(std::int16_t)), made);
    SDL_PutAudioStreamData(stream, made.data(), static_cast<int>(made.size() * sizeof(std::int16_t)));
}

// Remaster rules' part of the window's loop: the player's settings, as the menu leaves them, and where the profile
// keeps them (empty: nowhere).
struct Remastered {
    Settings settings;
    std::filesystem::path profile;
};

// The game in a window. With `gpu` (and its window), the pictures are the ones it kept and it shows them;
// without, they are the Classic renderer's and a window is made here. With `remastered` (Remaster rules): the
// player's controls and settings, the menu, Remaster's own keys and buttons, walking as fast as a stick is pushed.
int play(Session& session, input::Controls controls, SDL_Window* window, GpuRenderer* gpu, bool report_slow,
         Showcase* showcase, Remastered* remastered, NetGame* net) {
    using input::Action;
    using input::bit;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    if (gpu == nullptr) {
        if (!SDL_CreateWindowAndRenderer(kWindowTitle, kWidth * kInitialScale, kHeight * kInitialScale,
                                         SDL_WINDOW_RESIZABLE, &window, &renderer)) {
            std::println(stderr, "SDL_CreateWindowAndRenderer failed: {}", SDL_GetError());
            SDL_Quit();
            return EXIT_FAILURE;
        }
        SDL_SetRenderVSync(renderer, 1);
        SDL_SetRenderLogicalPresentation(renderer, kWidth, kHeight, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    // The sound card asks for its sound as it needs it (sound_wanted), a few milliseconds at a time.
    (void)SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = 2, .freq = static_cast<int>(Session::sample_rate())};
    SDL_AudioStream* sound =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sound_wanted, &session);
    if (sound == nullptr) {
        std::println(stderr, "no sound: {}", SDL_GetError());
    } else {
        SDL_ResumeAudioStreamDevice(sound);
    }

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kWidth) * kHeight);
    // The pictures still to show, and what each asks of the sound, heard as it is shown.
    std::vector<Screen> frames = std::move(session.start_frames);
    std::vector<std::vector<SoundCue>> cues = std::move(session.cues);
    std::size_t next_frame = 0;
    Screen last_shown;  // what the GPU renderer shows again until there is a next one
    const std::uint64_t frequency = SDL_GetPerformanceFrequency();
    const std::uint64_t start = SDL_GetPerformanceCounter();
    std::uint64_t refreshes = 0;
    std::uint16_t keyboard = 0;  // the keyboard word, as the original's handler keeps it
    int status = EXIT_SUCCESS;
    bool running = true;
    std::vector<SDL_Gamepad*> pads;
    input::PadReading pads_before{};  // as the last pass of the loop read them
    input::PadReading quiet{};        // held as the menu closed: not the game's until let go
    bool hush = false;                // the menu was closed: what the gamepads hold now is quiet
    Play::MenuKeys sound_keys;        // Alt+S and Alt+M, until a tick acts on them
    const bool remaster = remastered != nullptr;
    // The menu (Remaster rules), while it is open; the game holds meanwhile (Session::held). A note in it flashes.
    std::optional<Menu> menu;
    bool flash_shown = true;
    bool give_up = false;         // the menu asked for the original's pause box (where the level is given up) ...
    bool quit_box = false;        // ... or its quit box: with the next step
    CoopOverlay coop_shown;       // online: what is laid over the screen while the menu isn't
    std::uint64_t repeat_at = 0;  // a gamepad's direction held in the menu: when it counts again
    const auto alt_held = [] {
        const bool* held = SDL_GetKeyboardState(nullptr);
        return held[SDL_SCANCODE_LALT] || held[SDL_SCANCODE_RALT];
    };
    const auto key_name = [](SDL_Scancode key) { return std::string_view{SDL_GetScancodeName(key)}; };
    // The keys of a key pressed or let go: under Remaster rules Escape opens the menu in a level being played, and is
    // the original's pause key elsewhere, as it was.
    const auto key_actions = [&](SDL_Scancode key) {
        input::Actions actions = input::key_actions(controls, key_name(key));
        if (remaster && key == SDL_SCANCODE_ESCAPE) {
            actions |= bit(Action::kPause);
        }
        return actions;
    };
    // Remaster rules' own actions of a key or button pressed, taken where they act (Session::takes_play_keys): the
    // zoom, the minimap, the quicksave. Elsewhere a key is the
    // game's like any other: nothing pressed in a scene or a menu is kept for later. True if one took it.
    const auto act = [&](input::Actions actions) {
        bool taken = false;
        const auto has = [actions](Action action) { return (actions & bit(action)) != 0; };
        taken = (has(Action::kZoomOut) && session.zoom(1)) || taken;
        taken = (has(Action::kZoomIn) && session.zoom(-1)) || taken;
        taken = (has(Action::kMinimap) && session.toggle_minimap()) || taken;
        taken = (has(Action::kQuicksave) && session.quicksave()) || taken;
        taken = (has(Action::kQuickload) && session.quickload()) || taken;
        return taken;
    };
    // What the menu changed is used at once, and its picture drawn again.
    const auto draw_menu = [&] { gpu->set_overlay(menu_picture(*menu, session.font(), flash_shown)); };
    const auto menu_changed = [&] {
        use_settings(session, gpu, menu->settings());
        controls = menu->settings().controls;
        draw_menu();
    };
    const auto open_menu = [&] {
        menu.emplace(settings_now(session, gpu, remastered->settings), menu_choices(session));
        menu->set_has_pad(!pads.empty());
        keyboard = 0;  // what is held now is let go in the menu
        session.rewind(false);
        menu_changed();
    };
    const auto close_menu = [&] {
        remastered->settings = settings_now(session, gpu, menu->settings());
        if (!remastered->profile.empty()) {
            if (const auto saved = save_settings(remastered->profile, remastered->settings); !saved) {
                std::println(stderr, "vikings-remaster: the profile: {}", saved.error());
            }
        }
        menu.reset();
        gpu->set_overlay({});
        hush = true;
    };
    const auto menu_press = [&](Menu::Key key) {
        const Menu::Asked asked = menu->press(key);
        menu_changed();
        if (net != nullptr && !net->hosting() && asked == Menu::Asked::kQuit) {
            running = false;  // a client quitting leaves the game (the host's quit box is the host's)
        }
        if (asked != Menu::Asked::kNothing) {
            give_up = asked == Menu::Asked::kGiveUp;
            quit_box = asked == Menu::Asked::kQuit;
            close_menu();
        }
    };
    // A key in the menu: its own (the arrows, Enter, Escape, Delete), or the one to bind.
    const auto menu_key = [&](const SDL_Event& event) {
        if (event.type != SDL_EVENT_KEY_DOWN) {
            return;
        }
        const SDL_Scancode key = event.key.scancode;
        if (menu->waiting() != Menu::Waiting::kNothing) {
            if (key == SDL_SCANCODE_ESCAPE) {
                menu->stop_waiting();
            } else if (!event.key.repeat) {
                menu->take_key(key_name(key));
            }
            menu_changed();
            return;
        }
        switch (key) {
            case SDL_SCANCODE_UP:
            case SDL_SCANCODE_KP_8:
                return menu_press(Menu::Key::kUp);
            case SDL_SCANCODE_DOWN:
            case SDL_SCANCODE_KP_2:
                return menu_press(Menu::Key::kDown);
            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_KP_4:
                return menu_press(Menu::Key::kLeft);
            case SDL_SCANCODE_RIGHT:
            case SDL_SCANCODE_KP_6:
                return menu_press(Menu::Key::kRight);
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
            case SDL_SCANCODE_SPACE:
                return event.key.repeat ? void() : menu_press(Menu::Key::kChoose);
            case SDL_SCANCODE_ESCAPE:
                return event.key.repeat ? void() : menu_press(Menu::Key::kBack);
            case SDL_SCANCODE_DELETE:
            case SDL_SCANCODE_BACKSPACE:
                return menu_press(Menu::Key::kClear);
            default:
                return;
        }
    };
    // The gamepads in the menu, as the loop's pass reads them: the d-pad or the left stick choose (held, again and
    // again), A chooses, B goes back, Y clears, Start closes it; or the input to bind.
    const auto menu_pads = [&](const input::PadReading& reading) {
        const auto index = [](std::string_view input) { return input::pad_input_index(input).value_or(0); };
        const auto held = [&](std::string_view input) { return reading.at(index(input)) > 0.5F; };
        const auto pushed = [&](std::string_view input) { return held(input) && pads_before.at(index(input)) <= 0.5F; };
        if (pushed("start")) {
            if (menu->waiting() != Menu::Waiting::kNothing) {
                menu->stop_waiting();
                return menu_changed();
            }
            return close_menu();
        }
        if (menu->waiting() == Menu::Waiting::kPad) {
            for (const std::string_view input : input::pad_inputs()) {
                if (pushed(input)) {
                    menu->take_pad(input);
                    return menu_changed();
                }
            }
            return;
        }
        if (menu->waiting() != Menu::Waiting::kNothing) {
            return;
        }
        constexpr double kFirstRepeat = 0.4;  // seconds
        constexpr double kRepeat = 0.12;
        const std::uint64_t now = SDL_GetPerformanceCounter();
        const auto seconds = [frequency](double s) {
            return static_cast<std::uint64_t>(s * static_cast<double>(frequency));
        };
        const std::array<std::pair<Menu::Key, std::array<std::string_view, 2>>, 4> directions{{
            {Menu::Key::kUp, {"dpup", "lefty-"}},
            {Menu::Key::kDown, {"dpdown", "lefty+"}},
            {Menu::Key::kLeft, {"dpleft", "leftx-"}},
            {Menu::Key::kRight, {"dpright", "leftx+"}},
        }};
        for (const auto& [key, inputs] : directions) {
            if (pushed(inputs[0]) || pushed(inputs[1])) {
                repeat_at = now + seconds(kFirstRepeat);
                return menu_press(key);
            }
            if ((held(inputs[0]) || held(inputs[1])) && now >= repeat_at) {
                repeat_at = now + seconds(kRepeat);
                return menu_press(key);
            }
        }
        if (pushed("a")) {
            return menu_press(Menu::Key::kChoose);
        }
        if (pushed("b")) {
            return menu_press(Menu::Key::kBack);
        }
        if (pushed("y")) {
            return menu_press(Menu::Key::kClear);
        }
    };
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    std::erase_if(pads, [&event](SDL_Gamepad* pad) {
                        if (SDL_GetGamepadID(pad) != event.gdevice.which) {
                            return false;
                        }
                        SDL_CloseGamepad(pad);
                        return true;
                    });
                } else if (SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which)) {
                    pads.push_back(pad);
                }
                if (menu) {  // the gamepad's page says so at once
                    menu->set_has_pad(!pads.empty());
                    draw_menu();
                }
            } else if (menu) {
                menu_key(event);
            } else if (showcase != nullptr && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                       (event.key.scancode == SDL_SCANCODE_PAGEDOWN || event.key.scancode == SDL_SCANCODE_PAGEUP ||
                        event.key.scancode == SDL_SCANCODE_RIGHTBRACKET ||
                        event.key.scancode == SDL_SCANCODE_LEFTBRACKET)) {
                const bool on =
                    event.key.scancode == SDL_SCANCODE_PAGEDOWN || event.key.scancode == SDL_SCANCODE_RIGHTBRACKET;
                showcase->next(on ? 1 : -1);
                SDL_SetWindowTitle(window, showcase->title().c_str());
            } else if (event.type == SDL_EVENT_KEY_DOWN && alt_held() &&
                       (event.key.scancode == SDL_SCANCODE_S || event.key.scancode == SDL_SCANCODE_M)) {
                // Alt+S, Alt+M: the sound switches, acted on after the next tick (a repeat counts again). As
                // the original's handler, S with Alt held isn't a typed character.
                (event.key.scancode == SDL_SCANCODE_S ? sound_keys.effects : sound_keys.music) = true;
                if (event.key.scancode == SDL_SCANCODE_M) {
                    session.key_pressed(input::typed_character(key_name(event.key.scancode)));
                }
            } else if (remaster && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                       event.key.scancode == SDL_SCANCODE_ESCAPE && session.takes_play_keys()) {
                open_menu();
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0) {
                (void)session.zoom(event.wheel.y < 0 ? 1 : -1);
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                // A press, or the keyboard's own repeat of a held key, which reaches the original's handler as
                // a press too: it types the character again, and sets the key's bit again if a scene's load
                // cleared it. Remaster's own (not repeated) go first; one they take isn't the game's.
                const input::Actions actions = key_actions(event.key.scancode);
                session.key_pressed(input::typed_character(key_name(event.key.scancode)));
                if (!(remaster && !event.key.repeat && act(actions))) {
                    keyboard |= input::game_bits(actions);
                }
                if (session.demo()) {  // the keyboard handler: any key during a demo
                    keyboard = 0xFFFF;
                }
            } else if (event.type == SDL_EVENT_KEY_UP) {
                // As the original's handler: a release clears the bit even if another key with it is held.
                keyboard &= ~unsigned{input::game_bits(key_actions(event.key.scancode))};
            }
        }
        // The gamepads, once a pass: what is held as the menu closes isn't the game's until it is let go.
        input::PadReading reading = pads_reading(pads);
        if (std::exchange(hush, false)) {
            quiet = reading;
        }
        for (std::size_t i = 0; i < reading.size(); ++i) {
            quiet.at(i) = reading.at(i) > 0.5F ? quiet.at(i) : 0;
            reading.at(i) = quiet.at(i) > 0.5F ? 0 : reading.at(i);
        }
        if (menu) {
            menu_pads(reading);
        }
        if (menu) {
            constexpr std::uint64_t kFlashMilliseconds = 400;
            if (const bool flash = (SDL_GetTicks() / kFlashMilliseconds) % 2 == 0; flash != flash_shown) {
                flash_shown = flash;
                draw_menu();
            }
        } else if (remaster) {
            // Start opens the menu in a level being played (elsewhere it is the original's pause, below); Remaster's
            // own actions act as their buttons are pushed.
            const std::size_t start_button = input::pad_input_index("start").value_or(0);
            if (reading.at(start_button) > 0.5F && pads_before.at(start_button) <= 0.5F && session.takes_play_keys()) {
                open_menu();
            } else {
                const input::Actions pushed =
                    input::read_pad(controls, reading, true).held & ~input::read_pad(controls, pads_before, true).held;
                (void)act(pushed & ~input::kGameActions);
            }
        }
        pads_before = reading;
        if (net != nullptr) {  // what came, and what is due to be sent
            if (const auto polled = net->poll(); !polled) {
                std::println(stderr, "vikings-remaster: {}", polled.error());
                status = EXIT_FAILURE;
                running = false;
            }
            if (menu) {  // its page of the players, as they are now
                if (std::vector<Menu::PlayerLine> lines = player_lines(session, *net); lines != menu->players()) {
                    menu->set_players(std::move(lines));
                    draw_menu();
                }
            }
        }
        // Every refresh that is due shows the next picture; a step runs when the last one is used up.
        const double elapsed =
            static_cast<double>(SDL_GetPerformanceCounter() - start) / static_cast<double>(frequency);
        const auto due = static_cast<std::uint64_t>(elapsed * kDisplayHz);
        if (due > refreshes + 30) {
            refreshes = due;  // far behind (a breakpoint, a slow load): don't race to catch up
        }
        const Screen* shown = nullptr;
        for (; refreshes < due && running; ++refreshes) {
            while (running && next_frame >= frames.size()) {
                if (menu && net == nullptr) {  // the game holds: its pictures as it is, a zoom shown at once
                    frames = session.held();
                    cues = std::move(session.cues);
                    next_frame = 0;
                    continue;
                }
                const bool* held = SDL_GetKeyboardState(nullptr);
                const input::PadActions from_pads = input::read_pad(controls, reading, remaster);
                std::uint16_t pad = input::game_bits(from_pads.held);
                const std::size_t start_button = input::pad_input_index("start").value_or(0);
                if (remaster && reading.at(start_button) > 0.5F && !session.takes_play_keys()) {
                    pad |= input::game_bits(bit(Action::kPause));  // Start: the original's pause, as it was
                }
                if (pad != 0 && session.demo()) {  // any joystick input during a demo
                    pad = 0xFFFF;
                }
                const bool alt = held[SDL_SCANCODE_LALT] || held[SDL_SCANCODE_RALT];
                // Remaster rules: back in time while it is held; walking and climbing as fast as a stick is pushed
                // (a key: all the way).
                session.rewind(remaster &&
                               (keys_held(controls, Action::kRewind) || (from_pads.held & bit(Action::kRewind)) != 0));
                const auto part = [&](Action one, Action other, float stick) {  // a key: all of it
                    return static_cast<std::uint8_t>(
                        (keyboard & input::game_bits(bit(one) | bit(other))) != 0
                            ? kFullPace
                            : std::clamp(static_cast<int>(std::ceil(stick * kFullPace)), 1, int{kFullPace}));
                };
                const Pace pace{.across = part(Action::kLeft, Action::kRight, from_pads.walk),
                                .up_down = part(Action::kUp, Action::kDown, from_pads.climb)};
                std::uint16_t given = keyboard | pad;
                if (std::exchange(give_up, false)) {
                    given |= input::game_bits(bit(Action::kPause));  // the menu's: the original's pause box
                }
                const std::uint64_t step_began = SDL_GetPerformanceCounter();
                const std::size_t pages_before = gpu != nullptr ? gpu->pages_set_up() : 0;
                const Play::MenuKeys menu_keys{.yes = held[SDL_SCANCODE_Y],
                                               .no = held[SDL_SCANCODE_N],
                                               .quit = held[SDL_SCANCODE_F10] ||
                                                       (alt && (held[SDL_SCANCODE_X] || held[SDL_SCANCODE_Q])) ||
                                                       std::exchange(quit_box, false),
                                               .effects = sound_keys.effects,
                                               .music = sound_keys.music,
                                               .previous_level = held[SDL_SCANCODE_F5],
                                               .next_level = held[SDL_SCANCODE_F6]};
                if (net != nullptr && !net->hosting() && menu_keys.quit) {
                    running = false;  // a client's F10 or Alt+X: he leaves the game
                    continue;
                }
                if (net != nullptr) {
                    // Online: this machine's input when it can be given (in the menu: nothing held, the game goes
                    // on), and the next step when it can run; else the last picture again for this refresh.
                    if (net->wants_input()) {
                        net->give(session.take_input(menu ? std::uint16_t{0} : given,
                                                     menu ? Play::MenuKeys{} : menu_keys, pace));
                    }
                    const Screen again = frames.empty() ? Screen{} : frames.back();
                    std::optional<std::vector<Screen>> pictures = net->step();
                    (void)net->flush();  // (this machine's input on its way at once; poll says what went wrong)
                    if (pictures) {
                        frames = std::move(*pictures);
                        cues = std::move(session.cues);
                    } else {
                        frames = {again};
                        cues = {{}};
                    }
                    next_frame = 0;
                    sound_keys.effects = session.menu_keys().effects;
                    sound_keys.music = session.menu_keys().music;
                    if (session.scene_loaded()) {
                        keyboard = 0;
                    }
                    continue;
                }
                frames =
                    session.step(showcase != nullptr ? showcase->before_step(session, given) : given, menu_keys, pace);
                if (report_slow) {
                    const double took = static_cast<double>(SDL_GetPerformanceCounter() - step_began) * 1000.0 /
                                        static_cast<double>(frequency);
                    if (took > kSlowStepMilliseconds) {
                        std::println(
                            stderr, "slow: a step took {:.1f} ms ({} pictures{}{})", took, frames.size(),
                            session.scene_loaded() ? ", a scene loaded" : "",
                            gpu != nullptr && gpu->pages_set_up() != pages_before ? ", an atlas page set up" : "");
                    }
                }
                sound_keys.effects = session.menu_keys().effects;
                sound_keys.music = session.menu_keys().music;
                if (session.scene_loaded()) {
                    keyboard = 0;  // as the original's level start: nothing is held until it is pressed again
                }
                cues = std::move(session.cues);
                next_frame = 0;
                if (session.quit()) {  // quit, or a scene that can't be loaded
                    if (!session.error().empty()) {
                        std::println(stderr, "vikings-remaster: {}", session.error());
                        status = EXIT_FAILURE;
                    }
                    running = false;
                }
            }
            if (!running || next_frame >= frames.size()) {
                break;
            }
            if (next_frame < cues.size()) {
                session.hear_now(cues[next_frame]);
            }
            shown = &frames[next_frame++];
        }
        if (gpu != nullptr) {
            if (shown != nullptr) {
                last_shown = *shown;
            }
            // How far into its refresh the picture is shown (the window's own refreshes come at another rate):
            // what moves between ticks is shown that much further on.
            const double into = (static_cast<double>(SDL_GetPerformanceCounter() - start) /
                                 static_cast<double>(frequency) * kDisplayHz) -
                                static_cast<double>(refreshes);
            const auto between = static_cast<float>(std::clamp(into, 0.0, 1.0));
            if (net != nullptr && !menu) {  // the names, and whom the game waits for
                CoopOverlay now = coop_overlay(*net);
                if (now != coop_shown) {
                    coop_shown = std::move(now);
                    gpu->set_overlay(coop_shown.waiting.empty() ? std::vector<std::uint8_t>{}
                                                                : coop_picture(coop_shown, session.font()));
                }
            } else if (menu) {
                coop_shown = {};  // (drawn again when the menu closes)
            }
            const Minimap* minimap = last_shown.fading ? nullptr : session.minimap();  // when the level is there
            const std::uint64_t show_began = SDL_GetPerformanceCounter();
            const auto done = gpu->show(last_shown, minimap, between);
            if (report_slow) {
                const double took = static_cast<double>(SDL_GetPerformanceCounter() - show_began) * 1000.0 /
                                    static_cast<double>(frequency);
                if (took > kSlowShowMilliseconds) {
                    const GpuRenderer::ShowTimes& times = gpu->show_times();
                    std::println(
                        stderr,
                        "slow: showing a picture took {:.1f} ms: {:.1f} waiting for the window, {:.1f} "
                        "recording, {:.1f} handing over; drawn {}x into {}x{} for a window of {}x{}, {} "
                        "sample(s) a side{}{}; since the picture before: {} kept, {} sending {} KB to the atlas",
                        took, times.waiting, times.recording, times.handing_over, times.drawn_scale, times.width,
                        times.height, times.window_width, times.window_height, times.samples,
                        times.remade ? ", its texture made anew" : "",
                        times.redrawn ? "" : ", not drawn anew (as the show before)", times.kept, times.uploads,
                        times.uploaded_bytes / 1024);
                }
            }
            if (!done) {
                std::println(stderr, "vikings-remaster: {}", done.error());
                status = EXIT_FAILURE;
                running = false;
            }
            continue;
        }
        if (shown != nullptr) {
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                const assets::Rgb c = shown->palette.at(shown->pixels[i]);
                pixels[i] = (std::uint32_t{c.r} << 16U) | (std::uint32_t{c.g} << 8U) | c.b;
            }
            SDL_UpdateTexture(texture, nullptr, pixels.data(), kWidth * static_cast<int>(sizeof(std::uint32_t)));
        }
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }
    if (menu) {
        close_menu();
    } else if (remaster) {
        remastered->settings = settings_now(session, gpu, remastered->settings);
    }
    if (net != nullptr) {
        net->quit();  // (a client tells the host he goes)
    }
    for (SDL_Gamepad* pad : pads) {
        SDL_CloseGamepad(pad);
    }
    if (sound != nullptr) {
        SDL_DestroyAudioStream(sound);
    }
    if (gpu == nullptr) {  // (with one, the window and SDL are the caller's to close, after it)
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    return status;
}

std::optional<int> parse_number(std::string_view text) {
    try {
        return static_cast<int>(std::stoul(std::string{text}, nullptr, 0));
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace
}  // namespace lvr

int main(int argc, char* argv[]) try {
    using namespace lvr;
    std::filesystem::path dir = "gamedata/original";
    std::filesystem::path keys;
    std::filesystem::path dump_to;
    std::filesystem::path wav;
    SessionSound sound{.modern = false, .soundfont = "gamedata/GeneralUser-GS.sf2"};
    int slot = scene::kLogo;
    std::optional<Showcase> showcase;  // --showcase
    int steps = 0;
    Rules rules = Rules::kVanilla;
    bool cheat = false;
    bool report_slow = false;               // --slow-frames
    bool effects = true;                    // --no-effects: false
    std::optional<GpuRenderer::Look> look;  // --shader
    std::filesystem::path profile;          // Remaster rules: where the player's profile is kept
    DumpOptions dumping;
    std::string held;                                 // a dump: the key held
    std::vector<std::pair<int, std::string>> tapped;  // and the ones pressed once, at their steps
    bool sound_asked = false;                         // --sound (a dump under Remaster rules: in a window the menu's)
    // Co-op online (ADR-0012): hosting on a port, or joining a host; how many play, this one's name, the delay.
    std::optional<std::uint16_t> host_port;
    std::string join_address;
    std::uint16_t join_port = kDefaultPort;
    int players = 2;
    std::string name;
    std::optional<int> delay;
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto value = [&](std::size_t ahead) { return i + ahead < args.size() ? args[i + ahead] : ""; };
        if (args[i] == "--keys" && i + 1 < args.size()) {
            keys = args[i + 1];
            ++i;
        } else if (args[i] == "--sound" && (value(1) == "classic" || value(1) == "modern")) {
            sound.modern = value(1) == "modern";
            sound_asked = true;
            ++i;
        } else if (args[i] == "--rules" && (value(1) == "vanilla" || value(1) == "remaster")) {
            rules = value(1) == "remaster" ? Rules::kRemaster : Rules::kVanilla;
            ++i;
        } else if (args[i] == "--cheat") {
            cheat = true;
        } else if (args[i] == "--no-effects") {
            effects = false;
        } else if (args[i] == "--slow-frames") {
            report_slow = true;
        } else if (const auto named = GpuRenderer::look_named(value(1)); args[i] == "--shader" && named) {
            look = named;
            ++i;
        } else if (args[i] == "--profile" && i + 1 < args.size()) {
            profile = args[i + 1];
            ++i;
        } else if (args[i] == "--soundfont" && i + 1 < args.size()) {
            sound.soundfont = args[i + 1];
            ++i;
        } else if (args[i] == "--data" && i + 1 < args.size()) {
            dir = args[i + 1];
            ++i;
        } else if (args[i] == "--wav" && i + 1 < args.size()) {
            wav = args[i + 1];
            ++i;
        } else if (const auto number = parse_number(value(1)); number && args[i] == "--slot") {
            slot = *number;
            ++i;
        } else if (const auto at = parse_number(value(2)); number && at && args[i] == "--zoom") {
            dumping.zoom = *number;
            dumping.zoom_at = *at;
            i += 2;
        } else if (number && args[i] == "--moments") {
            dumping.moments = *number;
            ++i;
        } else if (const auto until = parse_number(value(2)); number && until && args[i] == "--rewind") {
            dumping.rewind_from = *number;
            dumping.rewind_until = *until;
            i += 2;
        } else if (number && args[i] == "--showcase" && *number >= 1) {
            showcase.emplace(static_cast<std::size_t>(*number - 1));
            ++i;
        } else if (number && args[i] == "--whole") {
            dumping.whole_at = *number;
            ++i;
        } else if (const auto step = parse_number(value(2)); step && args[i] == "--tap") {
            tapped.emplace_back(*step, std::string{args[i + 1]});
            i += 2;
        } else if (args[i] == "--menu" && i + 1 < args.size()) {
            dumping.menu = std::string{args[i + 1]};
            ++i;
        } else if (number && args[i] == "--host" && *number > 0 && *number < 65536) {
            host_port = static_cast<std::uint16_t>(*number);
            ++i;
        } else if (args[i] == "--join" && i + 1 < args.size()) {
            const std::string_view address = args[i + 1];
            const std::size_t colon = address.rfind(':');
            const std::optional<int> port =
                colon != std::string_view::npos ? parse_number(address.substr(colon + 1)) : std::nullopt;
            join_address = std::string{port ? address.substr(0, colon) : address};
            join_port = port && *port > 0 && *port < 65536 ? static_cast<std::uint16_t>(*port) : kDefaultPort;
            ++i;
        } else if (number && args[i] == "--players" && *number >= 1 && *number <= kMostPlayers) {
            players = *number;
            ++i;
        } else if (args[i] == "--name" && i + 1 < args.size()) {
            name = args[i + 1];
            ++i;
        } else if (number && args[i] == "--delay" && *number >= 0 && *number <= 4) {
            delay = *number;
            ++i;
        } else if (args[i] == "--hold" && i + 1 < args.size()) {
            held = args[i + 1];
            ++i;
        } else if (number && args[i] == "--dump" && i + 2 < args.size()) {
            steps = *number;
            dump_to = args[i + 2];
            i += 2;
        } else {
            std::println(
                stderr,
                "usage: vikings-remaster [--data DIR] [--slot N] [--keys FILE] [--soundfont FILE] "
                "[--rules vanilla|remaster] [--cheat] [--slow-frames] [--profile DIR] [--showcase N] "
                "[--host PORT [--players N] [--delay N] | --join ADDRESS[:PORT]] [--name NAME] "
                "[--dump STEPS FILE.png [--wav FILE.wav] [--sound classic|modern] [--no-effects] "
                "[--shader none|crt|xbr] [--zoom BY AT_STEP] [--hold KEY] [--tap KEY AT_STEP] [--rewind FROM UNTIL] "
                "[--whole AT_STEP] [--moments N] [--menu KEYS]]");
            return EXIT_FAILURE;
        }
    }
    // Co-op online (ADR-0012), under Remaster rules: in the lobby before anything else, until everyone is there; the
    // host's scene and cheat are the game's.
    std::unique_ptr<NetGame> net;
    NetGame::Joined joined;
    if (host_port || !join_address.empty()) {
        if (host_port && !join_address.empty()) {
            std::println(stderr, "vikings-remaster: --host or --join, not both");
            return EXIT_FAILURE;
        }
        rules = Rules::kRemaster;
        auto files = assets::GameFiles::open(dir);
        if (!files) {
            std::println(stderr, "vikings-remaster: {}", files.error());
            return EXIT_FAILURE;
        }
        const NetGame::Identity identity{
            .build = LVR_BUILD, .game = files->release().exe_hash ^ files->release().data_content_hash, .name = name};
        auto opened = host_port ? NetGame::host(*host_port, players, slot, cheat, delay, identity)
                                : NetGame::join(join_address, join_port, identity);
        if (!opened) {
            std::println(stderr, "vikings-remaster: {}", opened.error());
            return EXIT_FAILURE;
        }
        net = std::move(*opened);
        std::println("vikings-remaster: {}",
                     host_port ? std::format("hosting on port {}, for {} players", *host_port, players)
                               : std::format("joined {} port {}", join_address, join_port));
        auto started = wait_in_lobby(*net);
        if (!started) {
            std::println(stderr, "vikings-remaster: {}", started.error());
            return EXIT_FAILURE;
        }
        joined = *started;
        slot = joined.slot;
        cheat = joined.cheat;
        std::println("vikings-remaster: {}: player {}, {} step(s) of input delay",
                     joined.state.empty() ? "the game starts" : "into the game under way", joined.player + 1,
                     joined.delay);
    }
    if (rules == Rules::kRemaster && dump_to.empty() && (sound_asked || look || !effects)) {
        // (The user, 2026-11-25: what the menu sets and the profile keeps isn't a start-up option as well.)
        std::println(
            stderr,
            "vikings-remaster: under Remaster rules the sound, the screen's look and the visual effects are set in "
            "the menu (Escape in a level) and kept in the profile; --sound, --shader and --no-effects are "
            "for --dump");
        return EXIT_FAILURE;
    }
    if (rules == Rules::kVanilla && sound_asked && sound.modern) {
        std::println(stderr,
                     "vikings-remaster: Vanilla rules play the Classic sound only (--rules remaster for the Modern)");
        return EXIT_FAILURE;
    }
    // No Modern sound under Vanilla rules (the original's sound, the user); a dump renders only the version it is
    // asked for.
    if (rules == Rules::kVanilla || !std::filesystem::exists(sound.soundfont) || (!dump_to.empty() && !sound.modern)) {
        sound.soundfont.clear();
    }
    sound.live = dump_to.empty();  // in a window the sound card asks for the sound; a dump makes it step by step
    // Under Remaster rules a window's pictures are drawn by the GPU renderer (ADR-0006), which needs the window
    // first; Vanilla rules, and a dump, have the Classic one's.
    SDL_Window* window = nullptr;
    std::unique_ptr<GpuRenderer> gpu;
    Draw draw_with = draw;
    if (rules == Rules::kRemaster) {
        std::expected<std::unique_ptr<GpuRenderer>, std::string> opened = std::unexpected{std::string{}};
        if (!dump_to.empty()) {
            opened = GpuRenderer::open_headless();
        } else if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
            return EXIT_FAILURE;
        } else {
            window =
                SDL_CreateWindow(kWindowTitle, kWidth * kInitialScale, kHeight * kInitialScale, SDL_WINDOW_RESIZABLE);
            opened = window != nullptr ? GpuRenderer::open_for(window)
                                       : std::unexpected{std::string{"no window: "} + SDL_GetError()};
        }
        if (!opened) {
            std::println(stderr, "vikings-remaster: the GPU renderer: {}", opened.error());
            return EXIT_FAILURE;
        }
        gpu = std::move(*opened);
        draw_with = [renderer = gpu.get()](Screen& screen, const View& view, const SceneGraphics& scene,
                                           const SharedGraphics& shared) {
            if (const auto kept = renderer->keep(screen, view, scene, shared); !kept) {
                std::println(stderr, "vikings-remaster: the GPU renderer: {}", kept.error());
                std::exit(EXIT_FAILURE);
            }
        };
    }
    if (gpu != nullptr && report_slow) {
        std::println(stderr, "the GPU renderer draws on {}", gpu->device_name());
    }
    if (look && rules != Rules::kRemaster) {
        std::println(stderr, "vikings-remaster: --shader is for Remaster rules (--rules remaster)");
        return EXIT_FAILURE;
    }
    if (showcase) {  // it starts in its first scenario's level, under Remaster rules
        slot = showcase->current().slot;
        if (rules != Rules::kRemaster) {
            std::println(stderr, "vikings-remaster: --showcase is for Remaster rules (--rules remaster)");
            return EXIT_FAILURE;
        }
    }
    auto session =
        Session::open(dir, slot, sound, rules, std::move(draw_with),
                      Online{.on = net != nullptr, .players = net != nullptr ? joined.players : 1}, joined.player);
    if (!session) {
        std::println(stderr, "vikings-remaster: {}", session.error());
        return EXIT_FAILURE;
    }
    if (net != nullptr) {
        if (!joined.state.empty()) {  // a game under way: its state as the host gave it
            if (const auto taken = (*session)->take_state(joined.state); !taken) {
                std::println(stderr, "vikings-remaster: {}", taken.error());
                return EXIT_FAILURE;
            }
        }
        net->begin(**session);
        if (const char* beside = SDL_GetBasePath()) {  // (what the game writes is beside it)
            net->set_log_folder(std::filesystem::path{beside} / "logs");
        }
    }
    if (cheat) {
        (*session)->cheat();
    }
    (*session)->set_effects(effects && gpu != nullptr);  // (the Modern visuals: what the GPU renderer shows)
    if (gpu != nullptr && look) {
        gpu->set_look(*look);
    }
    // The player's profile (Remaster rules, in a window): in the folder asked for, or `profiles` beside the game's
    // own program, where it is found without knowing where to look; and its settings.
    std::optional<Remastered> remastered;
    if (rules == Rules::kRemaster && dump_to.empty()) {
        if (profile.empty()) {
            if (const char* beside = SDL_GetBasePath()) {
                profile = std::filesystem::path{beside} / "profiles";
            }
        }
        remastered.emplace();
        remastered->profile = profile;
        if (!profile.empty()) {
            if (const auto used = (*session)->use_profile(profile); !used) {
                std::println(stderr, "vikings-remaster: the profile: {}", used.error());
            }
            std::vector<std::string> problems;
            remastered->settings = load_settings(profile, problems);
            for (const std::string& problem : problems) {
                std::println(stderr, "vikings-remaster: the profile: {}", problem);
            }
        }
        use_settings(**session, gpu.get(), remastered->settings);
    }
    if (!dump_to.empty()) {
        const input::Controls original = input::original_controls();
        dumping.held = input::game_bits(input::key_actions(original, held));
        for (const auto& [at, key] : tapped) {
            dumping.taps.emplace_back(at, input::game_bits(input::key_actions(original, key)));
        }
        dumping.showcase = showcase ? &*showcase : nullptr;
        return dump(**session, steps, dump_to, wav, gpu.get(), dumping, net.get());
    }
    // The controls: the original's under Vanilla rules, the player's under Remaster's; a keys file over them.
    input::Controls controls = remastered ? remastered->settings.controls : input::original_controls();
    if (!keys.empty()) {
        std::ifstream file(keys);
        std::stringstream text;
        text << file.rdbuf();
        auto parsed = input::parse_bindings(text.str(), std::move(controls));
        if (!file || !parsed) {
            std::println(stderr, "vikings-remaster: {}: {}", keys.string(), parsed ? "can't read" : parsed.error());
            return EXIT_FAILURE;
        }
        controls = std::move(*parsed);
        if (remastered) {
            remastered->settings.controls = controls;
        }
    }
    if (showcase && window != nullptr) {
        SDL_SetWindowTitle(window, showcase->title().c_str());
    }
    const int status = play(**session, controls, window, gpu.get(), report_slow, showcase ? &*showcase : nullptr,
                            remastered ? &*remastered : nullptr, net.get());
    if (const auto saved = (*session)->save_profile(); !saved) {
        std::println(stderr, "vikings-remaster: the profile: {}", saved.error());
    }
    if (remastered && !profile.empty()) {
        if (const auto saved = save_settings(profile, remastered->settings); !saved) {
            std::println(stderr, "vikings-remaster: the profile: {}", saved.error());
        }
    }
    if (gpu != nullptr) {
        gpu.reset();
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    return status;
} catch (const std::exception& error) {
    std::fputs("vikings-remaster: fatal: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputs("\n", stderr);
    return EXIT_FAILURE;
} catch (...) {
    std::fputs("vikings-remaster: fatal: unhandled exception\n", stderr);
    return EXIT_FAILURE;
}
