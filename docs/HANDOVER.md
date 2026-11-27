# Handover: where the project stands

For whoever picks the work up next, person or session. How the game is built: [`architecture.md`](architecture.md);
why: [`decisions.md`](decisions.md); what is ahead: [`ROADMAP.md`](ROADMAP.md).

## Where it stands

- **The whole game plays** under Vanilla rules with the Classic picture and sound: all 37 levels and every scene,
  each kind of entity written by hand (`src/lvr/`).
- **It matches the original.** At every refresh of every replay the engine shows the picture and asks for the
  sounds that the original's own bytecode did (the goldens in `replays/`), and `tools/lockstep.py` compares it with
  the original in DOSBox directly.
- **Remaster rules** (`--rules remaster`, or chosen in the box at the start) add, on the GPU renderer: zoom-out,
  smooth movement, no limit on entities, a minimap, rewind, a quicksave (with the cheat on), the SNES backdrops
  (parallax), light, glow and particles, the screen's looks (CRT and xBR), the Modern sound with its effects placed
  by position, a menu for the settings and the controls (keyboard and gamepad, rebindable, a stick walking as fast
  as it is pushed), and co-op online for up to three by rollback.
- **Linux and Windows.** The Linux build is the one tested; the Windows build is cross-built with llvm-mingw and
  only built (ADR-0021). `make release` packs both ([`development.md`](development.md), "Releases").
- **Published** under MIT at <https://github.com/sytsevg/the-lost-vikings-remastered>.

## What the Remaster rules have

The decisions are in [`decisions.md`](decisions.md) (ADR-0005, ADR-0006, ADR-0011, ADR-0012). All of it only
under Remaster rules; Vanilla rules must stay exactly what they are, and the replays are what proves that.

- **The rules** are set once, as a world is made (`World::rules`). Without `--rules` a game in a window asks in a
  box (`ask_rules` in `main.cpp`: Vanilla, Remaster or Quit; Vanilla rules if the box can't be shown, which on
  Wayland needs zenity or kdialog), with a note of what Remaster rules would play without (`remaster_notes`). A
  dump plays Vanilla rules.
- **The GPU renderer** (`src/lvr/gpu.*`, SDL_GPU on Vulkan, shaders `gpu_*.vert` and `gpu_*.frag` compiled by
  `glslc` into the build): what a Remaster window shows. Vanilla rules and every check use the Classic renderer
  (`render.*`). How it works: "The visuals" below.
- **Zoom-out:** `-` and `=`, or the mouse wheel. The view is a value of the camera (`view_width`, `view_height`),
  and everything that was 320 × 176 reads it. `Session` owns the zoom and its transition (`zoom_target_`);
  `World::set_view` changes the world's view, and leaves creating what comes into view to the next tick's start
  (`view_to_fill`), as the original creates what comes into view right before its first turn. The camera goes 64
  pixels past the level's bottom so the HUD never hides the floor (`Camera::covered`).
- **The text layer** is as large as the view; a box at a speaker is placed in it as it is, one at a fixed cell in
  its middle (`Line::by_point`).
- **512 entity slots and 2,048 sprite pieces** (`World::set_rules`; `kSlots` and `kOriginalPieces` are Vanilla's
  20 and 128). The layer's size and its three pools are a `PieceLayout`.
- **The minimap** (`minimap.*`, M): a picture of the whole level from the map's tile types, with how much of it a
  view has shown in its alpha (`Session::seen_`, per level and per pixel of the minimap, kept in the profile).
  The GPU renderer shows a square of it around the view in the bottom left corner (`gpu_over.frag`): it adds what
  the view shows now and the Vikings (`Screen::level_x`, `Screen::vikings`) at every refresh, and moves the HUD
  right for it. Every fade, a scene's and the places' online, goes through `darken` (`render.hpp`), which darkens
  the colours, the lights and `Screen::bright` together.
- **Rewind** (`Session`, Backspace): `World` can be copied (its slots are `OwnedEntity`, whose copy is the
  entity's clone), and `Play::Snapshot` is a world with its level. `Session` keeps one before every step in a
  level being played with the step's keys, and the last 10 seconds of the sound as it was heard, for playing
  backwards. Going back undoes two steps a tick and runs each again from its snapshot for the pictures in
  between (`Session::back_to`), each with the refresh it was first shown for (`Kept::refresh`). Two triangles
  flash meanwhile (`gpu_mark.frag`). A level has 199 steps of it (`Session::kRewindSteps`); a scene's start
  (`Play::scenes_started`) lets go of what was kept and gives all 199 again.
- **The rewind's bar:** `Screen::rewind_left` says how much is left; the GPU renderer draws the bar along the
  minimap's bottom when it draws the minimap (`kGaugeAt` in `gpu.cpp`, `gpu_gauge.frag`). The Classic picture
  never has either.
- **State that can be written out** (`archive.hpp`): every kind's own state is the plain struct `Saved` nested in
  it (member `s`), `Entity`'s and `World`'s are `EntityState` and `WorldState`. `World::save` and `World::load`
  write and read all of it, fixed-width and field by field, with `kStateVersion` to tell versions apart.
- **Quicksave** (F7 and F8, only with the game's cheat on: its password typed at the title, or `--cheat`): that
  written world as a file.
- **Where their keys act:** the zoom, the minimap's M, the quicksave's F7 and the rewind act only in a level being
  played (`Session::takes_play_keys`: not a scene, a demo, or the pause menu or quit box); F8 brings the quicksave
  back from anywhere but a menu. A key that doesn't act is passed to the game like any other (any key ends a
  demo), so nothing pressed in a scene is kept for the next level.
- **The profile** (`profile.*`): the folder `profiles` beside the game's program, or `--profile DIR`. In it
  `minimap.txt`, `quicksave.bin` and `settings.txt`. `profile_writable` checks at the start that it can be
  written; what goes wrong with it is said in a box.
- **The menu, the settings and the controls:** Escape or a gamepad's Start in a level being played opens the menu
  (`menu.*`, drawn on the CPU by `menu_picture` in the game's font; `GpuRenderer::set_overlay` lays it over the
  screen, `gpu_panel.frag`). While it is open the window's loop runs no step: `Session::held` makes a tick's
  pictures of the world as it is, darkened by the original's menus' tint (`kMenuTint`), with a zoom asked for in
  the menu going on. The GPU renderer draws the menu's picture at the game's pixel size (`fit`), mixing a window's
  pixel only where two of the menu's meet, so it is sharp at whole sizes and even at the zoom's others. Its
  options are a `Settings` (`profile.hpp`, `settings.txt`): the sound, the music's and the effects' volumes
  (`Session::set_volume`, gains on the sound made), the screen's look, the zoom, the minimap, the visual effects,
  and the controls (`input::Controls`: keys and gamepad inputs by action, `input.hpp`). `main.cpp` reads each
  gamepad as a `PadReading` (every button, stick direction and trigger, 0 to 1) and `input::read_pad` turns it
  into actions, for both rules: Vanilla's are `original_controls()`, the joystick's mapping. A stick that walks
  gives the step a pace across and up and down (`Pace`: kept for the rewind; `World::pace`, `Sprite::pace`):
  walking, steering in the air and climbing (`Viking::Saved::walked_at` and `climbs` keep what that needs).
  Stills: `vikings-remaster --rules remaster --dump STEPS FILE --menu KEYS`.
- **The sound by rules:** Vanilla rules play the Classic sound only (no SoundFont loaded, `--sound modern`
  refused); under Remaster rules the menu chooses it, Modern by default when the SoundFont is there.

## The visuals

- **How a picture gets to the window.** A step of the game (`Session::step`) is a tick: three pictures, each a
  `View` (the camera, the sprites' pieces in order, the text layer, the HUD, the palette) made by `view_of`. The
  Classic renderer draws it into a `Screen` of palette indices at once. The GPU renderer keeps its list and
  the `Screen` only says which one (`Screen::kept`, of the last 512), with the palette and the `Motion`; the
  window shows one a refresh of its own (`gpu_show.frag` looks up the palette and scales, then the minimap and
  the rewind's mark go over it). It fills the window, the 4:3 shape kept, in whole pixels where they fill 90% of
  it, else drawn the next whole number of times as large, a pixel of the window the mean of what it covers
  (`GpuRenderer::shown_in`).
- **Smooth movement.** The GPU renderer keeps a picture as its list of atlas rectangles and draws it when it is
  shown, as large as it is shown (`GpuRenderer::keep`, `show`), with every entity's pieces and the camera moved
  evenly over the tick instead of in the original's three whole-pixel shares, a third of a tick behind (`Motion`
  in `render.hpp`). The window's loop passes how far into its refresh each show is, at the screen's own rate.
- **The atlas** has pages (`GpuRenderer::State::Page`): the shared pictures, the scene's (tileset, the tiles'
  masks, its full pictures) and the sprites' pieces, each put there the first time it is shown and found again
  by a hash of its pixels. A kept picture is drawn from one page; a page is used again only when no kept
  picture is drawn from it.
- **How visuals are checked.** `lv-replay --check --gpu` (one or two replays are enough for most changes; all of
  them take five minutes on the container's software driver) draws the kept list at the original's size with
  nothing moved: it must be the Classic picture. Everything beyond that has no golden: under Remaster rules
  `vikings-remaster --dump STEPS FILE.png` writes what the window would show (three times the size) for looking
  at in the container; `--zoom BY AT_STEP` zooms out, `--hold KEY` holds a key, `--tap KEY AT_STEP` presses one
  (a level's password box waits for F), `--rewind FROM UNTIL` holds the rewind over those steps, `--whole
  AT_STEP` writes the whole level, `--showcase N` a spot of the effects', `--shader crt|xbr` a look, and
  `--moments N` writes the last step's pictures at N moments of their refresh each. Motion is judged by eye, in a
  window. Timings in the renderer are counted in pictures (a zoom change takes 20, a third of a second).

### Effects

The Modern visuals' effects, in a window or a dump on the GPU renderer (`--no-effects` leaves them out of a
dump; the Classic renderer and every check never have them). In the game's pixels (every light worked out at the
middle of a picture's pixel and in steps of brightness), subtle.
- **What gives off what** (`effects.cpp`, a table by the names of `animations.hpp`): what an entity was last
  started on (`Sprite::started`, set by `start_animation`; saved, so a rewind or a quickload keeps it). Fire (fire
  jets, flames, fireballs, arcing fire, fire arrows): an orange light, round, flickering, and embers. Electricity
  (barriers, fields, Tomator's bars): a blue light along the beam, and sparks. Laser shots (the bullets of the
  guns, gunners and troopers; the factory's turret shots; drones'; Tomator's): a red glow that goes with them, and
  a small one where they burst. The map's electricity cells: a light along each run of them, and sparks; the
  spaceship's turning colours (its screens and lights): a glow around them.
- **The Vikings' deaths** (`kVikingDeaths`, each Viking's own): electrocuted, a white-blue light on him that
  flashes and showers of sparks (`Emits::kShower`); burning up, the fire's orange light, embers, and grey smoke
  rising slower. While he dies only. Each death's rule names the worlds it happens in, because an animation is
  found in another world by the pairing of the programs, and a world without that death can have another
  animation in its place (Egypt's dissolving is where the spaceship's electrocution is). The death from a long
  fall is `dies_from_a_fall` (its sound `kHeavyLanding`).
- **Teleporting** (`kVikingBeamedAway`, `kVikingBeamedIn`, the spaceship's): a cool light that shimmers, and sparks
  on a twirl round him, a funnel wider higher up (`Effects::twirl`; particles that orbit: `Particle::radius`,
  `angle`, `turn`, `widen`), the near side white, the far side blue, each with a short trail.
- **Bombs:** a Viking's bomb put down (`kPlacedBombTicks`) has a small flickering light at the tip of its fuse
  (`Effects::fuse_of`) and sparks off it; going off (`kPlacedBombBlowsUp`), a bomber's bomb bursting
  (`kBombBursts`) and the factory's blasts (`kBlastLarge`, `kBlastSmall`) give a great warm light while they last,
  and as they go off embers flying out all round, smoke and sparks (`Effects::blast`).
- **How light is drawn** (`gpu_show.frag`): a light (`Screen::lights`, a box and a reach) casts: it brightens what
  it falls on, stopped by the ground (`Screen::ground`: the map's ground, walls and slopes pixel by pixel, made by
  `Effects` when the map changes), with a whiter highlight where it meets it; and its glow is laid over that,
  fainter in the ground. A colour that glows has it in the palette's alpha. What is in front of what gives off light
  hides its glow: the kept picture is drawn with a second target, at each pixel the place in its order of the quad
  that shows there (`Quad::order`, `work_order`); a light says what gives it off (`Light::from`), and its halo is
  laid only where what shows was drawn no later. The light it casts still falls on what is in front.
- **Particles** are pixels of the colours 1-15 put after the view's sprites (`Effects::add`), moved a picture at
  a time, only while the play goes on (not over a fade, not going back); a rewind, a quickload or another scene
  lets them go. While a scene fades the lights darken with it and hold still.
- **Checking them:** `vikings-remaster --rules remaster --showcase N` holds the view on spot N of 21
  (`showcase.hpp`: laser shots, barriers, electricity, screens, fire in four worlds; PgDn or `]` on, PgUp or `[`
  back). Spots 17-21 are the deaths: the level is started again, and two steps after the view is held a Viking is
  put there with half a point of health (`Session::put_viking`; `Entity::move_to`, which a replay's `place` uses
  too). With `--dump STEPS FILE.png` it writes that spot.

### Screen look

How the window shows the finished picture: as it is, as Hyllian's CRT shader shows it, or with its stair-steps
rounded off by Hyllian's xBR (ADR-0006). The menu chooses it; `--shader NAME` only in a dump. Everything on the
screen goes through it, the minimap, the rewind's bar and the menu too.
- **How:** `GpuRenderer::set_look`. With a look, `State::show` draws what it always draws into `crt_source` (a
  texture the target's size) in the game's whole pixels (no smooth movement), and `show_xbr` puts it onto the
  target through the look's pass, which finds the picture's pixels on the grid where it was drawn, `fit` wide.
  A dump (`--dump`, `--shader`) shows it too; `shown_whole` and the checks never do.
- **crt** (`gpu_hyllian.frag`): a scanline a row, its beam wider where it is brighter, a Catmull-Rom along the row
  kept from ringing, in linear light (2.4 in, 2.2 out); then the aperture grille (a window's column magenta, the
  next green), lit parts brightened. Averaged, it shifts no colour; seen small, the grille looks pink.
- **xbr** (`gpu_xbr.frag`): the stair-steps of the pixels rounded off and their edges blended; the tiles join up.

### Parallax

The SNES version has a **backdrop** behind every level's map, scrolled slower than it, where the DOS game shows
black. With the player's SNES image (`*.sfc`) in `gamedata/original/` beside `DATA.DAT` (ADR-0014), Remaster
rules show it; without the image, or under Vanilla rules, nothing changes.
- **Where:** a pixel of the map in colour 0 of one of the palette rows 0-7 (`shows_backdrop`, `backdrop.hpp`).
  The DOS palette's rows 0-7 are the SNES level's colours, and a row's colour 0 is where a SNES tile shows what is
  behind it; the DOS game blacks those colours out. The backdrop's own pixels go in as they are, with the level's
  palette, so they fade and turn with it. Sprites and text never use those colours, and the HUD's place is left
  alone. Other black colours stay black: they are black on the console too.
- **Checked against the console's own picture:** `lv-export assets` writes each level and scene with its backdrop
  (`snes/NN-level.png`, `snes/<scene>-scene.png`). Drawn the same way from the SNES level's own map, 32 of the 37
  levels and the village are the same pixel for pixel; levels 21, 24, 25, 33 differ in less than 1% (their maps
  differ a little), 28 is a column wider on DOS.
- **How it moves** (`backdrop_offset`, from the SNES code, [`formats/snes.md`](formats/snes.md)): the camera
  without the shake times the level's speed in 256ths, repeated past its size; the spaceship's stars move by
  themselves, 5 pixels a refresh across. Shown, it is where the smooth camera is at that moment, and the stars at
  the refresh plus `between`. `Screen::refresh` counts the refreshes the session has handed out pictures for, so
  the stars keep their pace in a text box's wait.
- **Where it lives:** `load_slot` takes the image (`Session` opens it under Remaster rules and gives it to `Play`)
  and puts the backdrop into the scene's `SceneGraphics::backdrop`; the GPU renderer puts it on the atlas page with
  the scene's tiles, and the show pass (`gpu_show.frag`) looks it up where the picture has such a colour inside the
  map. Past the map's edges it stays black. The Classic renderer and `GpuRenderer::draw` (the checks) never draw
  it.
- **Scenes:** the village's three scenes get the SNES sky and mountains. The ship and the game-over scene have
  backdrops too, but the DOS pictures cover every such pixel, so nothing shows there; the night scene's (the ship
  flying past behind the houses) is left out (`kSnesSceneHeaders`).

## Co-op online

ADR-0012. Up to three players, each on his own machine, by rollback after GGPO's design. The words of its places
are in [`../GLOSSARY.md`](../GLOSSARY.md).
- **Players in the world:** a world has up to three players (`WorldState::players`, `Player`), each with his keys,
  his stick's pace, his Viking and his camera; a step is given each one's input (`StepInput`). A Viking goes by the
  keys of the player who plays him, or of the last who did (`World::keys_of`; with one player all three read his,
  as the original's read its one keyboard). The active area is every view's together, and an enemy turns where he
  would leave every view he is in (`World::at_edge_of_view`). Scenes, menus and the texts that hold the game read
  the first player's keys (`World::host`). With one player everything is as before.
- **The rules of play online** (`WorldState::online`): a Viking's player and his viewers in line
  (`World::move_player`); what is said to one player alone (`World::Aside` in the kinds that talk to a Viking: his
  `Player::lines`, answered by his key at a tick's start, `World::show_players_lines`); each player's inventory in
  the tick (`inventories_in_tick`), its clicks his alone (`SoundCue::heard_by`); text boxes at their place in the
  level in every view (`Line::in_level`, composed per view in `view_of`). The furnace's glow follows the host's
  Viking (it decides whether the furnace stays active, so per player it would change the game). The intro and the
  night after it play the recording's keys for the first player; the others watch his Viking
  (`World::watch_recording`, `followed_by`).
- **Rollback-ready play:** `Play::PlayState` (the pause box, the sound switches, the demo, each player's chosen
  view) is in a snapshot, and `Play::restore` is exact (`Play::zoom_back` puts the zoom back after it). All a
  step's input is `StepInput`: each player's keys and zoom, the character typed, the menu keys. `Play::state_hash`.
  The cue ledger (`CueLedger`, `sound.cpp`) plays a step's sounds once however often it runs.
- **Rollback** (`src/net/rollback.*`, target `lvr_net`, plain C++): the game behind a small interface (`net::Game`:
  save and load a slot, advance a step shown or not, hash, predict, a state written out and taken). The input
  queues and what each step was last run with, the first step a prediction got wrong and the steps run again from
  it, the window (8 steps), the input delay, time sync by frame advantage (`ahead()`: wait a refresh), arrivals and
  departures (`arrive`, `leave`, `decided_until`), the hashes of every 8th confirmed step compared, per player
  (`out_of_step(player)`), and the host's state taken to go on from (`take_state`: the steps since run again, none
  before it ever again).
- **The transport** (`src/net/transport.*`, target `lvr_transport`, on SDL3_net with `cmake/sdl_net.patch`): host
  and join, a TCP connection and UDP to each client (introduced by a token over TCP), reliable and fast messages,
  the fast ones by UDP and TCP both, numbered (`net::Bundle`), the newest taken; pings for the round trip, answered
  by its own thread (`Transport::Inner::pump_once`); a peer silent for 10 s gone. `SimulatedNetwork`
  (`simulated.*`) is the same seam for tests: seeded, the test's clock, losses, reordering, a reliable message held
  back as over TCP.
- **The protocol** (`src/net`): the messages (`protocol.*`: Hello, Welcome, Refused, LobbyStatus, Start, Back,
  Inputs, Status, Hash, Leave, Quit, Echo, Arrive, Presence, Decided, Joining, Bundle, Resync); `Connection` (who is
  who, names, round trips, the game's number, what came a message at a time); `HostLobby` and `GuestLobby` (the
  lobby's checks of protocol, state version, build and game files; the delay from the round trips); `Match` (one game
  online, the host in the middle passing each player's inputs on, joining under way, a guest out of step put right
  from the host's state (`put_right`, `take_resync`; put right often: `unsteady`), held up too long: `held_up`).
- **The game's side** (`src/lvr/coop.*`): a step's input as 16 bytes a player; `CoopGame`, the session as
  `net::Game` (its snapshots, `Session::run` shown and `Session::run_again` not, the host's state taken with
  `Session::put_right`: the level kept); `replay_log`, which plays a game
  online's log again (`lv-replay --coop LOG`) and names the first step whose state isn't the log's.
- **The places** (`src/lvr/front.*`): `OfflinePlace` (the game alone; the only place under Vanilla rules),
  `ConnectingPlace`, `LobbyPlace`, `OnlinePlace`, each behind `Place` (its session, the menu's choices, `begin`,
  `refresh` every refresh, `step` when the pictures are used up, `asked`, `overlay`, `leave`); every move in
  `Places::go` (the connection handed from the lobby to the game and back). The window's loop is
  `run(Window&, Place&)` in `main.cpp`, the menu a place reaches through `Ui` (`HeadlessUi`: none).
- **The window's input** (`src/lvr/handler.*`, `InputHandler`): the keyboard word as the original's handler keeps
  it, the gamepads, Remaster's own keys, the menu's GIVE UP and QUIT kept until a step takes them, a step's input
  (`Tick`).
- **In the HUD and the menu:** each player's name under the portrait of the Viking he plays (`HudView::names`), the
  names of those watching him above it, grey (`HudView::watching`), a box at the top while the game waits for
  someone's input, with the seconds left before he is let go, and under it OUT OF STEP WITH HOST for a guest put
  right often (`CoopOverlay::warning`); the menu's first page lists the players with their round trips (NETWORK and GAME).
- **Tests:** the protocol on a toy game (`src/net/tests/toy.hpp`, `table.hpp`, `test_lobby.cpp`, `test_match.cpp`,
  `test_rollback.cpp`: latency, jitter, lost and reordered packets; two and three machines; one going its own way
  found out and put right, twice, three times: unsteady; a guest stuck let go, a host stuck left; a state given, by
  itself), the transport over 127.0.0.1 (`test_transport.cpp`), the real game in `test_coop.cpp` (two sessions under
  rollback over a lossy network against one with every input; joining under way; a guest out of step put right, his
  level kept; a state of another level) and `test_front.cpp` (a walk through every move; a machine stuck: the
  countdown, let go, joining again, the host stuck). `lvr_tests "[scratch]"`, hidden, writes stills of the lobby, the
  names under the portraits and the waiting box and the out-of-step box under it into `gamedata/previews/online/`.
- **Logs:** out of step, each machine that finds it (the host, the guest) writes the game's log beside the program
  (`logs/coop-BUILD-playerN.log`: the start, every confirmed step's inputs, its hashes), once a game, and plays on.

## Open

- **Frames now and then a refresh or more late** in the window on one AMD card (RADV), mostly noticed while
  moving. `--slow-frames` shows the time is spent waiting for the window's next picture (the card or the
  compositor holding frames), not in the game's step, its drawing, or its uploads; mailbox presenting or a third
  frame in flight doesn't change it. Likely the system (the GPU's power management); not confirmed.
- **Resizing under Wayland is slow while the window is dragged:** each new size has SDL rebuild the swapchain
  (100-270 ms waiting for the window). Left as it is. Under Windows polling stops for the whole drag; drawing from
  SDL_EVENT_WINDOW_EXPOSED or SDL's main callbacks would keep it going (wiki.libsdl.org/SDL3/AppFreezeDuringDrag).
- **Zoom with a text box open is slow.**
- **Co-op's snapshots.** Rollback keeps a copy of the world before every step, as the rewind does (an allocation
  per entity under Remaster rules): to be measured before anything is made flatter.
- **A gamepad can't type** into a page's lines (the names, the address); it chooses them.
- **The tests:** the Vanilla kinds' unit tests that the goldens pin could go, after measuring what each adds (the
  unit tests' coverage is 82%, the minimum 80%, ADR-0002).
- **Not checked by anything but the eye:** whatever the GPU renderer shows beyond the original's view, and
  whether a level still plays well zoomed out (enemies run for longer and further; nothing resets as often).
- **The GPU renderer has run on few graphics cards** and the container's software driver. Vulkan only.
- **A gamepad on Windows** hasn't been tried.
- **To be judged by ear:** the Modern effects' placing (`kEdge`, 48 of 63, and how fast an effect fades outside
  the view, in `Session::placed`); the volume sliders (the Classic chip's steps are steeper than the General MIDI
  ones). The fire jets' roar, one sound for every jet that is on, isn't placed.
- **Walking at the slowest:** a Viking moves a whole pixel every few ticks; if that looks jerky, smooth movement
  could use the position's leftover fraction.
- Written from the script's listing and never compared with the original: that Olaf's shield stays, put away,
  when Olaf himself is gone and the cheat is on. And whatever else no replay reaches
  ([`formats/unknowns.md`](formats/unknowns.md), section 5).
- One reading of the original that the goldens carry was checked only indirectly: a level loaded again reloads
  its palette. (F5 in level 1 loads the same level again and the pictures match; the case it was first written
  for, an exit reached with a Viking gone, was not tried.)
- The live sound (the sound card pulling, `Session::render_live`) is tested by ear only; no automated test opens a
  device.

## The layout

| Where | What |
|---|---|
| `src/assets/` | reading the original's files: `DATA.DAT`, levels, graphics, texts, object databases and their scripts, the SNES image |
| `src/audio/`, `src/image/`, `src/input/` | sound (OPL3 and General MIDI), PNG, keys and gamepad |
| `src/lvr/` | the engine and the game (`vikings-remaster`); [`src/lvr/README.md`](../src/lvr/README.md) names every file |
| `src/net/` | co-op online: rollback, the protocol, the transport and a simulated network |
| `src/replay/` | replays and goldens, and `lv-replay`, which plays one against the other |
| `src/exporter/` | `lv-export`, a development tool: the game's data as PNG and JSON, script listings |
| `replays/` | the replays with their goldens ([`replays/README.md`](../replays/README.md)) |
| `scripts/` | the checks: `check.sh`, `goldens.sh`, coverage, `replay_cover.sh`, the small checkers `check.sh` runs; `windows.sh`, `release.sh` |
| `tools/` | research tools: the scriptable DOSBox (`dosbox.py`, `lockstep.py`), disassemblers, the replay maker |
| `docs/original/`, `docs/formats/` | our write-ups of the original's engine and file formats |
| `docs/screenshots/` | the README's screenshots |

## How the work is done

- **Before a commit that touches the engine:** `./scripts/goldens.sh` (every replay against its golden, with the
  world written out and read back and the last 8 steps run again before every step: about a minute and a half)
  and `./scripts/check.sh` (format, constants defined once, every kind's state in its `Saved`, ASan/UBSan build
  with lint, unit tests, coverage of at least 80%). After a change that is only formatting, comments or docs
  neither needs running again. On the host, `./run-dev-env.sh test` does both in the container.
- **After adding state to `World` or to a kind:** raise `kStateVersion` (a test in `test_play.cpp` fails until
  its sizes and version are brought up to date). `goldens.sh` says when the new state isn't all written out or
  copied: a replay that fails, and matches with `--plain` (the goldens alone).
- **After a change to how the GPU renderer draws:** `lv-replay --check --gpu` on a replay or two.
- **What only the eye or the ear can judge is tried in a window before it is committed;** stills go into
  `gamedata/previews/` (not in the repository).
- **The game itself runs on the host**, not in the container (no display or sound there):
  `./build/release/bin/vikings-remaster --slot 0` (Vanilla rules), or `--rules remaster`. What a Remaster window
  would show can be written as a picture in the container (`--dump`, with `--zoom`). Rebuild the release preset
  before trying something: `goldens.sh` only builds the replay tool.
- **A golden is never recorded again to make a difference go away.** There is no oracle left to record from: a
  difference is a change of behaviour until shown otherwise. `lv-replay` writes what the engine shows at the
  first refresh that differs; run the commit before the change the same way to see what it showed before. Goldens
  are recorded again only when DOSBox shows the original does what the new ones hold
  ([`replays/README.md`](../replays/README.md)).
- **Something new or in doubt about the original:** `tools/lockstep.py` plays the same input to the original in
  DOSBox and to the engine and compares the pictures ([`dosbox.md`](dosbox.md)). It starts at the first logo;
  with the cheat's password typed, F6 goes on a level at a time, and `--place` puts the Vikings where they are
  needed. A run worth keeping becomes a replay. It can also be played by hand on the host (a Wayland session:
  `SDL_VIDEODRIVER=wayland`, [`dosbox.md`](dosbox.md), "By hand").
- **A fault of the original is not copied.** When a comparison turns up an accident of the original (a stray
  write, a glitch pixel), trace it far enough to be sure, write it into [`quirks.md`](quirks.md) as "not kept", and
  leave it out.
- **Classic is the original; Modern is ours.** The Classic sound keeps the original's limits and faults. The
  Modern sound is shaped by ear: its effects have their own synthesizer, and an effect that grates is remade in
  the `kRemade` table (`src/audio/player.cpp`).
- **Nothing of the game goes into the repository:** no data, art, text or decompiled code, in code, comments,
  tests or docs. Passwords as test input, and the README's screenshots, are the exceptions.
- **What the replays don't reach** is measured by `./scripts/replay_coverage.sh` (about 82% of the engine's lines
  are reached). The unit tests in `src/lvr/tests/` guard much of the rest. The last level's fight and the cranes
  are the thinnest.
- **Conventions** are in [`development.md`](development.md): hex only for what points into the original's files
  or defines a bit, decimal and `px()` for values; 16-bit arithmetic wraps on purpose.
- **Writing a kind:** [`architecture.md`](architecture.md), "Entities and kinds".

## Known differences from the original

Three, all meant ([`quirks.md`](quirks.md), "Drawing"): the game-over lightning that the original never erases, a
stray white dot in Erik's portrait when he is gone, and a scene's fade-out, which holds the last picture where the
original may flicker between its last two. `tools/lockstep.py` reports the second as one pixel at
(46, 197) whenever Erik is gone in a game begun from the title's first choice (not by password).
