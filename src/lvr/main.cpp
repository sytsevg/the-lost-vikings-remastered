// vikings-remaster, the game on the engine (ADR-0017): plays The Lost Vikings from the original's files
// (gamedata/original by default) in the classic look, as the original starts: the logos, the title, and on from there.
// A tick every three refreshes of an artificial 59.71 Hz display; the keyboard as the original's handler reads it
// (docs/original/input.md).
//
//   vikings-remaster [--data DIR] [--slot N] [--keys FILE] [--sound classic|modern] [--soundfont FILE]
//                    [--rules vanilla|remaster]
//       play, from scene N (default 39, the first logo; 0-36 are the levels); FILE: other keys, lines
//       "KEY NAME = action" (input/input.hpp). F12 switches between the Classic and the Modern sound.
//   vikings-remaster [--data DIR] [--slot N] --dump STEPS FILE.png [--wav FILE.wav] [--zoom BY AT_STEP]
//       run STEPS steps without a window and without a key, write the last picture (and the sound)
//
// Keys: arrows, Ctrl/Home and Ins/PgUp (switch Viking), E, S, D, F/Space/Enter, Esc/P (pause; Y, N), Tab
// (inventory), Alt+X, Alt+Q, F10 (quit) as in the original; a gamepad as the original's joystick (A/B/X/Y: F, D, E,
// S), the d-pad or left stick, shoulders to switch Vikings, Start to pause, Back for the inventory.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
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
#include "lvr/render.hpp"
#include "lvr/session.hpp"

namespace lvr {
namespace {

constexpr int kWidth = kScreenWidth;
constexpr int kHeight = kScreenHeight;
constexpr int kInitialScale = 3;
constexpr const char* kWindowTitle = "The Lost Vikings";

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

// What the GPU renderer shows of `screen`, three times the original's size, as a PNG.
bool save_shown(GpuRenderer& gpu, const Screen& screen, const std::filesystem::path& out) {
    constexpr int kScale = 3;
    auto pixels = gpu.shown(screen, kScale);
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
int dump(Session& session, int steps, const std::filesystem::path& out, const std::filesystem::path& wav,
         GpuRenderer* gpu, int zoom, int zoom_at) {
    Screen last = session.start_frames.empty() ? Screen{} : session.start_frames.back();
    std::vector<std::int16_t> sound = std::move(session.samples);
    session.samples.clear();
    std::size_t refreshes = session.start_frames.size();
    for (int step = 0; step < steps && !session.quit(); ++step) {
        if (step == zoom_at) {
            session.zoom(zoom);
        }
        std::vector<Screen> frames = session.step(0);
        refreshes += frames.size();
        if (!frames.empty()) {
            last = std::move(frames.back());
        }
        sound.insert(sound.end(), session.samples.begin(), session.samples.end());
        session.samples.clear();
    }
    if (!session.error().empty()) {
        std::println(stderr, "vikings-remaster: {}", session.error());
        return EXIT_FAILURE;
    }
    std::println("scene {} after {} refreshes", session.scene(), refreshes);
    if (gpu != nullptr) {
        if (!save_shown(*gpu, last, out)) {
            return EXIT_FAILURE;
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

// The gamepads' input bits (the original or-s the joystick's into the keyboard's): every open gamepad's, the left
// stick past half way as the d-pad.
std::uint16_t gamepad_bits(const std::vector<SDL_Gamepad*>& pads) {
    constexpr int kHalfway = 16384;
    std::uint16_t bits = 0;
    for (SDL_Gamepad* pad : pads) {
        const auto button = [pad](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
        const int x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        bits = static_cast<std::uint16_t>(
            bits | input::pad_bits(input::Pad{.left = button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -kHalfway,
                                              .right = button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > kHalfway,
                                              .up = button(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -kHalfway,
                                              .down = button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > kHalfway,
                                              .south = button(SDL_GAMEPAD_BUTTON_SOUTH),
                                              .east = button(SDL_GAMEPAD_BUTTON_EAST),
                                              .west = button(SDL_GAMEPAD_BUTTON_WEST),
                                              .north = button(SDL_GAMEPAD_BUTTON_NORTH),
                                              .left_shoulder = button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
                                              .right_shoulder = button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
                                              .start = button(SDL_GAMEPAD_BUTTON_START),
                                              .back = button(SDL_GAMEPAD_BUTTON_BACK)}));
    }
    return bits;
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

// The game in a window. With `gpu` (and its window), the pictures are the ones it kept and it shows them;
// without, they are the Classic renderer's and a window is made here.
int play(Session& session, const input::Bindings& bindings, SDL_Window* window, GpuRenderer* gpu) {
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
    const std::uint64_t frequency = SDL_GetPerformanceFrequency();
    const std::uint64_t start = SDL_GetPerformanceCounter();
    std::uint64_t refreshes = 0;
    std::uint16_t keyboard = 0;  // the keyboard word, as the original's handler keeps it
    int status = EXIT_SUCCESS;
    bool running = true;
    std::vector<SDL_Gamepad*> pads;
    Play::MenuKeys sound_keys;  // Alt+S and Alt+M, until a tick acts on them
    const auto alt_held = [] {
        const bool* held = SDL_GetKeyboardState(nullptr);
        return held[SDL_SCANCODE_LALT] || held[SDL_SCANCODE_RALT];
    };
    const auto key_bit = [&bindings](SDL_Scancode key) { return input::key_bits(bindings, SDL_GetScancodeName(key)); };
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && alt_held() &&
                       (event.key.scancode == SDL_SCANCODE_S || event.key.scancode == SDL_SCANCODE_M)) {
                // Alt+S, Alt+M: the sound switches, acted on after the next tick (a repeat counts again). As
                // the original's handler, S with Alt held isn't a typed character.
                (event.key.scancode == SDL_SCANCODE_S ? sound_keys.effects : sound_keys.music) = true;
                if (event.key.scancode == SDL_SCANCODE_M) {
                    session.key_pressed(input::typed_character(SDL_GetScancodeName(event.key.scancode)));
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                       (event.key.scancode == SDL_SCANCODE_MINUS || event.key.scancode == SDL_SCANCODE_EQUALS)) {
                session.zoom(event.key.scancode == SDL_SCANCODE_MINUS ? 1 : -1);  // Remaster rules: out, in
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0) {
                session.zoom(event.wheel.y < 0 ? 1 : -1);
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F12 &&
                       !event.key.repeat) {
                session.set_modern(!session.modern());  // the Classic / Modern switch (ADR-0015)
                std::println(stderr, "{}",
                             !session.has_modern() ? "no Modern sound (no SoundFont)"
                             : session.modern()    ? "sound: Modern (General MIDI)"
                                                   : "sound: Classic (OPL3)");
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                // A press, or the keyboard's own repeat of a held key, which reaches the original's handler as
                // a press too: it types the character again, and sets the key's bit again if a scene's load
                // cleared it.
                session.key_pressed(input::typed_character(SDL_GetScancodeName(event.key.scancode)));
                keyboard |= key_bit(event.key.scancode);
                if (session.demo()) {  // the keyboard handler: any key during a demo
                    keyboard = 0xFFFF;
                }
            } else if (event.type == SDL_EVENT_KEY_UP) {
                // As the original's handler: a release clears the bit even if another key with it is held.
                keyboard &= ~unsigned{key_bit(event.key.scancode)};
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which)) {
                    pads.push_back(pad);
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                std::erase_if(pads, [&event](SDL_Gamepad* pad) {
                    if (SDL_GetGamepadID(pad) != event.gdevice.which) {
                        return false;
                    }
                    SDL_CloseGamepad(pad);
                    return true;
                });
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
        static Screen last_shown;  // what the GPU renderer shows again until there is a next one
        for (; refreshes < due && running; ++refreshes) {
            while (running && next_frame >= frames.size()) {
                const bool* held = SDL_GetKeyboardState(nullptr);
                std::uint16_t pad = gamepad_bits(pads);
                if (pad != 0 && session.demo()) {  // any joystick input during a demo
                    pad = 0xFFFF;
                }
                const bool alt = held[SDL_SCANCODE_LALT] || held[SDL_SCANCODE_RALT];
                frames = session.step(static_cast<std::uint16_t>(keyboard | pad),
                                      Play::MenuKeys{.yes = held[SDL_SCANCODE_Y],
                                                     .no = held[SDL_SCANCODE_N],
                                                     .quit = held[SDL_SCANCODE_F10] ||
                                                             (alt && (held[SDL_SCANCODE_X] || held[SDL_SCANCODE_Q])),
                                                     .effects = sound_keys.effects,
                                                     .music = sound_keys.music,
                                                     .previous_level = held[SDL_SCANCODE_F5],
                                                     .next_level = held[SDL_SCANCODE_F6]});
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
            if (const auto done = gpu->show(last_shown); !done) {
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
    int steps = 0;
    Rules rules = Rules::kVanilla;
    int zoom = 0;     // a dump: this many steps of zoom out ...
    int zoom_at = 0;  // ... asked for before this step
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto value = [&](std::size_t ahead) { return i + ahead < args.size() ? args[i + ahead] : ""; };
        if (args[i] == "--keys" && i + 1 < args.size()) {
            keys = args[i + 1];
            ++i;
        } else if (args[i] == "--sound" && (value(1) == "classic" || value(1) == "modern")) {
            sound.modern = value(1) == "modern";
            ++i;
        } else if (args[i] == "--rules" && (value(1) == "vanilla" || value(1) == "remaster")) {
            rules = value(1) == "remaster" ? Rules::kRemaster : Rules::kVanilla;
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
            zoom = *number;
            zoom_at = *at;
            i += 2;
        } else if (number && args[i] == "--dump" && i + 2 < args.size()) {
            steps = *number;
            dump_to = args[i + 2];
            i += 2;
        } else {
            std::println(stderr,
                         "usage: vikings-remaster [--data DIR] [--slot N] [--keys FILE] [--sound classic|modern] "
                         "[--soundfont FILE] [--rules vanilla|remaster] [--dump STEPS FILE.png [--wav FILE.wav]]");
            return EXIT_FAILURE;
        }
    }
    if (!std::filesystem::exists(sound.soundfont) || (!dump_to.empty() && !sound.modern)) {
        sound.soundfont.clear();  // no Modern sound; a dump renders only the version it is asked for
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
    auto session = Session::open(dir, slot, sound, rules, std::move(draw_with));
    if (!session) {
        std::println(stderr, "vikings-remaster: {}", session.error());
        return EXIT_FAILURE;
    }
    if (!dump_to.empty()) {
        return dump(**session, steps, dump_to, wav, gpu.get(), zoom, zoom_at);
    }
    input::Bindings bindings = input::default_bindings();
    if (!keys.empty()) {
        std::ifstream file(keys);
        std::stringstream text;
        text << file.rdbuf();
        auto parsed = input::parse_bindings(text.str(), std::move(bindings));
        if (!file || !parsed) {
            std::println(stderr, "vikings-remaster: {}: {}", keys.string(), parsed ? "can't read" : parsed.error());
            return EXIT_FAILURE;
        }
        bindings = std::move(*parsed);
    }
    const int status = play(**session, bindings, window, gpu.get());
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
