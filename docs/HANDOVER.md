# Handover: where the project stands

For whoever picks the work up next, person or session. How the game is built: [`architecture.md`](architecture.md);
why: [`decisions.md`](decisions.md); what is ahead: [`ROADMAP.md`](ROADMAP.md).

## Where it stands

- **The whole game plays** under Vanilla rules with Classic visuals: all 37 levels and every scene, each kind of
  entity written by hand (`src/lvr/`), with Classic and Modern sound.
- **It matches the original.** At every refresh of every replay the engine shows the picture and asks for the
  sounds that the original's own bytecode did (the goldens in `replays/`; a scene's fade-out as the original's
  screen shows it, since 2026-11-25), and `tools/lockstep.py` compares it with the original in DOSBox directly.
- **It is published:** MIT, at <https://github.com/sytsevg/the-lost-vikings-remastered>. History is kept: new
  work is new commits; the user pushes.
- **M7, the Remaster rules, is built** (`--rules remaster`): the GPU renderer, zoom-out, no limit on entities,
  a minimap, rewind, a quicksave, a profile on disk, and the game's state written out and read back. What each
  is and where it lives is in the next section.
- **M8, Modern visuals and sound**, is nearly done, before co-op (M9): smooth movement, parallax, the effects
  (light, glow, particles), the screen's looks (Hyllian's CRT and xBR) and the Modern sound's effects placed by
  position are built (below, "M8"); only the extra sound effects are left on its list.
- **Remaster rules have a menu** (Escape or a gamepad's Start in a level): the sound and its volumes, the screen's
  look, zoom, minimap, visual effects, and the keyboard's and gamepad's controls, kept in the profile; a gamepad's
  stick walks and climbs as fast as it is pushed (next section, "The menu, the settings and the controls").

## What the Remaster rules have

The decisions are in [`decisions.md`](decisions.md) (ADR-0005, ADR-0006, ADR-0011, ADR-0018, ADR-0020). All of it
only under Remaster rules; Vanilla rules must stay exactly what they are, and the replays are what proves that.

- **The Rules switch:** `--rules vanilla|remaster`, for the whole run (`World::rules`, set as a world is made). It
  is never changed while playing.
- **The GPU renderer** (`src/lvr/gpu.*`, SDL_GPU on Vulkan, shaders `gpu_*.vert` and `gpu_*.frag` compiled by
  `glslc` into the build): what a Remaster window shows. Vanilla rules and every check use the Classic renderer
  (`render.*`), which stays. How the GPU one works is under "M8".
- **Zoom-out:** `-` and `=`, or the mouse wheel; the original's view, one and a half times and twice it. The view
  is a value of the camera (`view_width`, `view_height`), and everything that was 320 × 176 reads it. `Session`
  owns the zoom and its transition; `World::set_view` changes the world's view. A zoomed view is as high as the
  screen with the HUD over its bottom; the camera goes 64 pixels past the level's bottom so the HUD never hides
  the floor (`Camera::covered`).
- **The text layer** is as large as the view; a box at a speaker is placed in it as it is, one at a fixed cell in
  its middle (`Line::by_point`).
- **512 entity slots and 2,048 sprite pieces** (`World::set_rules`; `kSlots` and `kOriginalPieces` are Vanilla's
  20 and 128). The layer's size and its three pools are a `PieceLayout`.
- **The minimap** (`minimap.*`, M): a picture of the whole level from the map's tile types, with how much of it a
  view has shown in its alpha (`Session::seen_`, per level and per pixel of the minimap, kept in the profile).
  The GPU renderer shows a square of it around the view in the bottom left corner (`gpu_over.frag`): it adds what
  the view shows now and the Vikings (`Screen::level_x`, `Screen::vikings`) at every refresh, and moves the HUD
  right for it.
- **Rewind** (`Session`, Backspace): `World` can be copied (its slots are `OwnedEntity`, whose copy is the
  entity's clone), and `Play::Snapshot` is a world with its level. `Session` keeps one before every step in a
  level being played with the step's keys, and the last 10 seconds of the sound as it was heard, for playing
  backwards. Going back undoes two steps a tick and runs each again from its snapshot for the pictures in
  between (`Session::back_to`). Two triangles flash meanwhile (`gpu_mark.frag`). A level has 199 steps of it
  (10 seconds, `Session::kRewindSteps`; the user, 2026-11-25): each step undone uses one, and no more steps are
  kept than are left; a scene's start (`Play::scenes_started`, a level started again too) lets go of what was
  kept and gives all 199 again. Only in a level being played: never back from the game-over scene.
- **The rewind's bar** (2026-11-25): `Screen::rewind_left` (set by `Session::picture` in a level being played,
  not over a fade) says how much is left; the GPU renderer draws the bar along the minimap's bottom when it draws
  the minimap, faded in and out with it (`kGaugeAt` in `gpu.cpp`, in the original's pixels from the minimap's
  corner; `gpu_gauge.frag`: in the original's pixels, each one colour, as the user asked, 85% opaque: a white frame with
  two pixels cut off each corner, light grey inside from the left in whole columns, dark past that, and the
  rewind's mark in its middle, dark on the grey and grey on the dark). The minimap's frame has the same corners
  (`gpu_over.frag`). The Classic picture never has either. Turned down on the way there: a vertical gauge
  framed as the text's boxes are, a smooth (anti-aliased) bar with a grey frame and a blue fill, and the bar
  over the middle of the HUD's bottom row (it took too much from the original's HUD).
- **State that can be written out** (`archive.hpp`): every kind's own state is the plain struct `Saved` nested in
  it (member `s`), `Entity`'s and `World`'s are `EntityState` and `WorldState`. `World::save` and `World::load`
  write and read all of it, fixed-width and field by field, with `kStateVersion` to tell versions apart.
- **Quicksave** (F7 and F8, only with the game's cheat on: its password typed at the title, or `--cheat`): that
  written world as a file.
- **Where their keys act** (2026-11-24): the zoom, the minimap's M and the quicksave's F7 act only in a level
  being played (`Session::takes_play_keys`: not a scene, a demo, or the pause menu or quit box), and so does
  the rewind (since 2026-11-25: no longer from the game-over scene back into the level just lost); F8 brings the
  quicksave back from anywhere but a menu. A key that doesn't act is passed to the game like any other (any key ends a demo), so nothing
  pressed in a scene is kept for the next level. The Classic/Modern sound switch (F12) and the screen's look
  (F11) worked everywhere; under Remaster rules both are the menu's alone now, and Vanilla rules play the Classic sound only (the user). These are the defaults: the keys are the player's since 2026-11-25 ("The menu,
  the settings and the controls" below).
- **The profile** (`profile.*`): the folder `profiles` beside the game's program, or `--profile DIR`. In it
  `minimap.txt`, `quicksave.bin` and `settings.txt` (the player's settings and controls, which took the place of
  `screen.txt`). What the game writes goes there, not into the user's home.
- **The menu, the settings and the controls** (built 2026-11-25, to be tried on the host; ADR-0011): Escape or a
  gamepad's Start in a level being played opens the menu (`menu.*`, drawn on the CPU by `menu_picture` in the
  game's font; `GpuRenderer::set_overlay` lays it over the screen, `gpu_panel.frag`). While it is open the
  window's loop (`main.cpp`) runs no step: `Session::held` makes a tick's pictures of the world as it is, darkened
  by the original's menus' tint (`kMenuTint`), with a zoom asked for in the menu going on. The GPU renderer draws
  the menu's picture at the game's pixel size (`fit`), mixing a window's pixel only where two of the menu's meet
  (`gpu_panel.frag`), so it is sharp at whole sizes and even at the zoom's others. Its options are a `Settings`
  (`profile.hpp`, `settings.txt`): the sound, the music's and the effects' volumes (`Session::set_volume`, through
  `audio::Sequencer::set_loudness`), the screen's look, the zoom, the minimap, the visual effects, and the controls
  (`input::Controls`: keys and gamepad inputs by action, `input.hpp`). `main.cpp` reads each gamepad as a
  `PadReading` (every button, stick direction and trigger, 0 to 1) and `input::read_pad` turns it into actions,
  for both rules: Vanilla's are `original_controls()`, the joystick's mapping as it always was. A stick that walks
  gives the step a pace across and up and down (`Pace`: `Session::step`'s, kept for the rewind; `World::pace`,
  `Sprite::pace`: kStateVersion 5): walking, steering in the air and climbing (`Viking::Saved::walked_at` and
  `climbs` keep what that needs). The pad's pause is called GIVE UP LEVEL in the menu, as the original's pause
  box asks that.
  Stills: `gamedata/previews/menu/` (`vikings-remaster --rules remaster --dump STEPS FILE --menu KEYS`).

## M8: the visuals

Decided with the user (2026-11-24): the visuals go with the rules, chosen at launch (ADR-0005): Vanilla rules
show the Classic picture, Remaster rules the Modern one. At the original's view under Vanilla rules the GPU
renderer must still make the Classic picture; under Remaster rules a visual may differ. Smooth movement first.
The rest of the list is in [`ROADMAP.md`](ROADMAP.md): sound placed by position (art packs and the SNES sound
were dropped by the user, 2026-11-25). The screen's look is built ("Screen look" below).

### Next
The state at the end of 2026-11-25: M8 has everything on its list but the extra sound effects. What the user still
has to judge on the host, and what is open:

- **Next to pick (the user):** the extra sound effects (footsteps by surface, the bow, Olaf's shield, an electric
  buzz; ROADMAP.md, "Extra sound effects"), which want a decision on where the sounds come from and a surface for
  every tile; or M9, co-op, whose first decision is the active area with several views (ADR-0011, ADR-0012).
- **The Modern sound's effects placed by position (built and checked 2026-11-25; being heard on the host):**
  ADR-0015. To judge by ear: how far to the sides the view's edges are (`kEdge`, 48 of 63, in `Session::placed`)
  and how fast an effect fades outside the view (silent half a view away). The fire jets' roar, one sound for every
  jet that is on, isn't placed. The Modern sound had no roar at all: the General MIDI arrangement's is empty. It
  is now remade from the Classic one's notes on the melodic tom (`kRemade` in `src/audio/player.cpp`), turned down
  twice by the user, to 45% of that (TinySoundFont's loudness goes with the cube of the volume: 76). Chosen by measuring, not by ear: General MIDI's effect
  sounds (surf, a helicopter) swell in too slowly for its short notes. Under Remaster rules the Classic roar is
  at 60% of its loudness (the user), which the chip gives at 87% of the volume (`Player::set_effect_loudness`,
  set in `Session`'s start); Vanilla rules' is the original's.
- **The Modern sound's voices (fixed 2026-11-25):** the synthesizer had 64 voices and the tunes want up to 106 at
  once (a note of GeneralUser GS takes one or two, and keeps one while it dies away), so it cut notes short to make
  room (the user heard a bass or a guitar stop early). Now 256 (`kMaxVoices`, `src/audio/gm.cpp`).
- **The sound by rules (the user, 2026-11-25):** Vanilla rules play the Classic sound only (no F12, no SoundFont
  loaded, `--sound modern` refused); under Remaster rules the menu chooses it. F11 is gone too: the screen's look
  is the menu's alone.
- **The menu, the settings and the controls (built and approved 2026-11-25):** to keep an eye on: the volume
  sliders by ear (percent of the game's volume; the Classic chip's steps are steeper than the General MIDI
  ones), and walking at the slowest (a Viking moves a whole pixel every few ticks: if that looks jerky, smooth
  movement could use the position's leftover fraction).
- **The screen's look, the effects, the deaths' effects:** approved by the user on the host (2026-11-25); the menu
  chooses the look (none, crt, xbr: "Screen look" below). More effects can be added to the table any time (water
  and lava cross-fading between their colour steps was talked about, not tried).
- **The rewind's bar:** approved by the user on the host (2026-11-25). It belongs with the minimap: put away
  with it (M), the bar goes too (the user, 2026-11-25). The minimap's frame stays grey (the bar's is white).
- **F8 outside a played level:** the quicksave's load still acts from anywhere but a menu; the user decided to
  leave it so for now (2026-11-25). (The rewind acts only in a level being played, decided the same day.)
- **Parallax in play:** judged so far on dumps, stills and pans in the container
  (`gamedata/previews/parallax/`); how it looks moving is the user's to say on the host (the factory's brick
  backdrop looks much like its walls, as on the console; no dimming, decided).

The fixes of this round, for the record:

- **A scene's fade-out shows everyone (fixed, 2026-11-24; both rules).** The end of a scene removes every entity
  (the original's game-over pass) before the 70 fade pictures, which were drawn from that empty world. The
  original's fade (`0FA0`) only darkens the palette and flips its two pages, which still hold the last pictures
  drawn; the user saw in DOSBox that everyone stays while it goes dark. `Play::change_scene` now keeps a copy of
  the world from before the pass, and `Play::shown()` gives it to what draws (`Session::picture`, `lv-replay`)
  until the fade is over; the pass and its sounds are where they were. The user decided to record the goldens of
  the 102 replays that change scene again for it ([`replays/README.md`](../replays/README.md)): compared refresh
  by refresh, only fade-out refreshes changed. (The original flips between its two pages, so where something
  moved in the last tick it alternates between the last two pictures, seen in DOSBox too; ours holds the last
  one: a quirk not kept.)
- **The jumble at the top left (fixed, 2026-11-24).** Zooming out while the world holds (every level opens with
  its password box, which waits for a key) created what the larger view showed at once; their first turn, where
  their animations put their pieces in place, only comes with the next tick. Meanwhile their pieces were drawn
  where the pieces' last owners had left them, and the Remaster layer's back pool (from piece 512) is new at the
  level's start: at (0, 0), stacked in the level's corner. `World::set_view` now leaves the creating to the next
  tick's start (`view_to_fill`, saved: kStateVersion 2), as the original creates what comes into view: right
  before its first turn. While the world holds, the newly shown part has no entities yet.
How the visuals work so far:

- **Smooth movement (built, 2026-11-24).** The GPU renderer keeps a picture as its list of
  atlas rectangles and draws it when it is shown, as large as it is shown (`GpuRenderer::keep`, `show`), with
  every entity's pieces and the camera moved evenly over the tick instead of in the original's three
  whole-pixel shares, a third of a tick behind (`Motion` in `render.hpp`, ADR-0006). The window's loop passes
  how far into its refresh each show is, at the screen's own rate.
- **How a picture gets to the window.** A step of the game (`Session::step`) is a tick: three pictures, each a
  `View` (the camera, the sprites' pieces in order, the text layer, the HUD, the palette) made by `view_of`. The
  Classic renderer draws it into a `Screen` of palette indices at once. The GPU renderer keeps its list and
  the `Screen` only says which one (`Screen::kept`, of the last 512), with the palette and the `Motion`; the
  window shows one a refresh of its own (`gpu_show.frag` looks up the palette and scales, then the minimap and
  the rewind's mark go over it).
- **The atlas** has pages (`GpuRenderer::State::Page`): the shared pictures, the scene's (tileset, the tiles'
  masks, its full pictures) and the sprites' pieces, each put there the first time it is shown and found again
  by a hash of its pixels. A kept picture is drawn from one page; a page is used again only when no kept
  picture is drawn from it.
- **What effects have.** Animations are the original's programs, played as they are (`sprites.*`); a program
  reports only the sounds it asks for. The roadmap's "events" (a frame shown, a sound asked for, an end), named
  beside the animations in `animations.hpp`, don't exist yet.
- **How visuals are checked.** `lv-replay --check --gpu` (one or two replays are enough for most changes, all of
  them take five minutes on the container's software driver) draws the kept list at the original's size with
  nothing moved: it must be the Classic picture. Everything beyond that has no golden: under Remaster rules
  `vikings-remaster --dump STEPS FILE.png` writes what the window would show (three times the size) for looking
  at in the container; `--zoom BY AT_STEP` zooms out, `--hold KEY` holds a key, `--tap KEY AT_STEP` presses one
  (a level's password box waits for F), `--rewind FROM UNTIL` holds the rewind over those steps, `--whole
  AT_STEP` writes the whole level (the view as large as it), `--showcase N` a spot of the effects', and
  `--moments N` writes the last step's pictures at N moments of their refresh each. The user judges motion on the host. Timings in the renderer are counted in pictures (a zoom
  change takes 20, a third of a second).

### Effects (built, 2026-11-25)

The Modern visuals' effects, Remaster rules only, in a window or a dump on the GPU renderer (`--no-effects`
leaves them out; the Classic renderer and every check never have them). Shaped with the user over several rounds
of stills; the look they settled on: in the game's pixels (every light worked out at the middle of a picture's
pixel and in steps of brightness), subtle.
- **What gives off what** (`effects.cpp`, a table by the names of `animations.hpp`): what an entity was last
  started on (`Sprite::started`, set by `start_animation`; saved, `kStateVersion` 3, so a rewind or a quickload
  keeps it). Fire (fire jets, flames, fireballs, arcing fire, fire arrows): an orange light, round, flickering,
  and embers. Electricity (barriers, fields, Tomator's bars): a blue light along the beam, and sparks. Laser shots
  (the bullets of the guns, gunners and troopers; the factory's turret shots; drones'; Tomator's): a red glow
  that goes with them, and a small one where they burst. Not a blow or a hit landing, nor an impact's burst of
  fire (the user: on the fence, so none). The map's electricity cells: a light along each run of them, and sparks;
  the spaceship's turning colours (its screens and lights, not its electricity's): a glow around them.
- **The Vikings' deaths** (the user, 2026-11-25: electrocution and burning up only; `kVikingDeaths`, each Viking's
  own). Electrocuted (by a barrier or a bar: `electrocuted`; by the map's electricity: `electrocuted_from_below`
  and `_from_above` in the spaceship, `kVikingShockedFromBelow` and `FromAbove` in the factory and the wacky
  world): a white-blue light on him that flashes, and sparks. Burning up (`kVikingBurnsUp`, every world but the
  spaceship, which has no such death): the fire's orange light, embers, and grey smoke rising slower. While he
  dies only: when he is dead his pieces are hidden, and with them the light. Each death's rule names the worlds it
  happens in, because an animation is found in another world by the pairing of the programs, and a world without
  that death can have another animation in its place (Egypt's dissolving is where the spaceship's electrocution
  is), or none (the place is then taken as it is). The animation that was called `burns` is the death from a long
  fall (`dies_from_a_fall`; its sound, called `kBurnt`, is a ledge's or a ball's landing too: `kHeavyLanding`).
- **How light is drawn** (`gpu_show.frag`): a light (`Screen::lights`, a box and a reach) casts: it brightens what
  it falls on, stopped by the ground (`Screen::ground`: the map's ground, walls and slopes pixel by pixel, made
  by `Effects` when the map changes), with a whiter highlight where it meets it; and its glow is laid over that,
  fainter in the ground. A colour that glows has it in the palette's alpha.
- **Particles** are pixels of the colours 1-15 put after the view's sprites (`Effects::add`), moved a picture at
  a time, only while the play goes on (not over a fade, not going back); a rewind, a quickload or another scene
  lets them go. While a scene fades the lights darken with it and hold still.
- **Checking them:** `vikings-remaster --rules remaster --showcase N` holds the view on spot N of 21
  (`showcase.hpp`: laser shots, barriers, electricity, screens, fire in four worlds; PgDn or `]` on, PgUp or `[`
  back; it goes to the level and answers what waits for a key itself). Spots 17-21 are the deaths: the level is
  started again, and two steps after the view is held a Viking is put there with half a point of health
  (`Session::put_viking`; `Entity::move_to`, which a replay's `place` uses too). A test plays each with and
  without him and finds his light. With `--dump STEPS FILE.png` it writes that
  spot; `--whole AT_STEP` writes a whole level. Stills: `gamedata/previews/effects/` (not in the repository).

### Screen look (built, 2026-11-25)

How the window shows the finished picture, under Remaster rules (ADR-0005, ADR-0006), decided with the user: as
it is, as Hyllian's CRT shader shows it, or with its stair-steps rounded off by Hyllian's xBR. The menu chooses
it (F11 went through them until the user left it to the menu) (`--shader NAME` only in a dump); the profile keeps the one the window ended
with (`settings.txt`). Everything on the screen goes through it, the minimap, the rewind's bar and the menu too
(the user). Tried and dropped after the user saw them on the host: a VGA monitor's look (480 fine lines, a fine
mask; "just blurry"), a television of our own (240 lines with dark gaps, a slot mask, flat glass: "not great"
beside crt), and curved glass with rounded corners.
- **How:** `GpuRenderer::set_look`. With a look, `State::show` draws what it always draws into `crt_source` (a
  texture the target's size, with the pipelines for our own format) in the game's whole pixels (no smooth
  movement), and `show_xbr` puts it onto the target through the look's pass, which finds the picture's pixels on
  the grid where it was drawn, `fit` wide (the minimap and a sliding HUD lie off that grid; the menu is put on
  it). A dump (`--dump`, `--shader`) shows it too; `shown_whole` and the checks never do.
- **crt** (approved, 2026-11-25; Hyllian's CRT shader, MIT, THIRD-PARTY-NOTICES.md, `gpu_hyllian.frag`): chosen
  by the user over crt-royale (GPL v2+, 12 passes and 6 mask pictures) and crt-lottes. Its main pass and its last
  pass's mask in one: a scanline a row, its beam wider where it is brighter, a Catmull-Rom along the row kept
  from ringing, in linear light (2.4 in, 2.2 out); then the aperture grille (a window's column magenta, the next
  green), lit parts brightened. No cutoff: zoomed out the scanlines stay (Hyllian's dropped them past 400 rows,
  and twice the view was brighter, the user); a row less than three of the window's pixels high is taken over
  each window's pixel whole (at two, the beam's samples would both be as far from its middle: no lines and a
  different brightness). Left out: its colour grading (a Sony television's colours, on in its preset; the game
  keeps its own), glow and curvature (off in its preset). Averaged, it shifts no colour (a grey stays grey); seen
  small, the grille looks pink. Stills: `gamedata/previews/shaders/hyllian/`.
- **xbr** (approved; ADR-0006, "The xbr look"; Hyllian's xBR-lv2, MIT, THIRD-PARTY-NOTICES.md): the
  stair-steps of the pixels rounded off and their edges blended (`gpu_xbr.frag`); the tiles join up. Checked on a
  card of lines, circles, steps, small shapes and a round sprite of our own
  (`gamedata/previews/shaders/xbr-card-*`). The unblended one (each tile and piece rounded by itself, so smooth
  movement stayed) was tried and dropped by the user.

### Parallax (built, 2026-11-24)

The SNES version has a **backdrop** behind every level's map, scrolled slower than it, where the DOS game shows
black. Under Remaster rules, with the user's SNES image (`*.sfc`) in `gamedata/original/` beside `DATA.DAT`
(ADR-0014), the game shows it; without the image (a note on stderr) or under Vanilla rules nothing changes.
- **Where:** a pixel of the map in colour 0 of one of the palette rows 0-7 (`shows_backdrop`, `backdrop.hpp`).
  The DOS palette's rows 0-7 are the SNES level's colours (all 37 levels compared), and a row's colour 0 is where
  a SNES tile shows what is behind it; the DOS game blacks those colours out. The backdrop's own pixels go in as
  they are, with the level's palette, so they fade and turn with it. Sprites and text never use those colours (all
  sprite sets checked), and the HUD's place is left alone. Other black colours stay black: they are black on the
  console too.
- **Checked against the console's own picture:** `lv-export assets` writes each level and scene with its backdrop
  (`snes/NN-level.png`, `snes/<scene>-scene.png`, each 320 x 176 screen with the camera there). Drawn the same
  way from the SNES level's own map, 32 of the 37 levels and the village are the same pixel for pixel; levels 21,
  24, 25, 33 differ in less than 1% (their maps differ a little), 28 is a column wider on DOS. Stills of both and
  camera pans for levels 5-7, 12, 26 and 27: `gamedata/previews/parallax/`.
- **How it moves** (`backdrop_offset`, from the SNES code, [`formats/snes.md`](formats/snes.md)): the camera
  without the shake times the level's speed in 256ths, repeated past its size; the spaceship's stars move by
  themselves, 5 pixels a refresh across (the DOS and SNES refreshes are 59.7 and 60.1 a second). Shown, it is
  where the smooth camera is at that moment, and the stars at the refresh plus `between`. `Screen::refresh`
  counts the refreshes the session has handed out pictures for (a picture shown again counts again), so the stars
  keep their pace in a text box's wait; they don't go back while the play rewinds.
- **Where it lives:** `load_slot` takes the image (`Session` opens it under Remaster rules and gives it to
  `Play`) and puts the backdrop into the scene's `SceneGraphics::backdrop`; the GPU renderer puts it on the atlas
  page with the scene's tiles, a kept picture records where it is and where the map is, and the show pass
  (`gpu_show.frag`) looks it up where the picture has such a colour inside the map. Past the map's edges (a view
  larger than a small level) it stays black. The Classic renderer and `GpuRenderer::draw` (the checks) never
  draw it.
- **Scenes:** the village's three scenes get the SNES sky and mountains (the DOS village leaves its sky in colour
  0). The ship and the game-over scene have backdrops too, but the DOS pictures cover every such pixel (sprites
  over the ship's few, none at all in game over), so nothing shows there.

## Open

- **Frames now and then a refresh or more late** in the window (the user's AMD Radeon 890M, RADV), mostly noticed
  while moving. `--slow-frames` shows the time is spent waiting for the window's next picture (the card or the
  compositor holding frames), not in the game's step, its drawing, or its uploads; it was there before smooth
  movement too, and mailbox presenting or a third frame in flight didn't change it. Likely the system (the
  GPU's power management); not confirmed (vkcube, a fixed clock).
- **Co-op and the active area:** with several views of one world, either the active area is all the views
  together or everything runs (ADR-0011). Not decided.
- **Co-op's snapshots.** What is written of a world (ADR-0020) is all of it, 25 to 90 KB. Sent over a network
  many times a second it wants trimming: the sprite layer's free pieces (most of a Remaster world's size), the
  level's own data that both sides have, and only what changed since the last one.
- **Input per player** was planned from the start (ADR-0012) but the engine takes one keyboard word.
- **Not checked by anything but the eye:** whatever the GPU renderer shows beyond the original's view, and
  whether a level still plays well zoomed out (enemies run for longer and further; nothing resets as often).
- **The GPU renderer has run on one graphics card** (the user's) and the container's software driver. Vulkan
  only.
- Written from the script's listing and never compared with the original: that Olaf's shield stays, put away,
  when Olaf himself is gone and the cheat is on. And whatever else no replay reaches (below;
  [`formats/unknowns.md`](formats/unknowns.md), section 5).
- One reading of the original that the goldens carry was checked only indirectly: a level loaded again reloads
  its palette. (F5 in level 1 loads the same level again and the pictures match; the case it was first written
  for, an exit reached with a Viking gone, was not tried.)
- The live sound (the sound card pulling, `Session::render_live`) was tested by ear on the user's machine only;
  no automated test opens a device.
- Screenshots for the README: wanted, undecided. If added, hosted outside the repository.

## The layout

| Where | What |
|---|---|
| `src/assets/` | reading the original's files: `DATA.DAT`, levels, graphics, texts, object databases and their scripts |
| `src/audio/`, `src/image/`, `src/input/` | sound (OPL3 and General MIDI), PNG, keys and gamepad |
| `src/lvr/` | the engine and the game (`vikings-remaster`); [`src/lvr/README.md`](../src/lvr/README.md) names every file |
| `src/replay/` | replays and goldens, and `lv-replay`, which plays one against the other |
| `src/exporter/` | `lv-export`, a development tool: the game's data as PNG and JSON, script listings |
| `replays/` | the replays with their goldens ([`replays/README.md`](../replays/README.md)) |
| `scripts/` | the checks: `check.sh`, `goldens.sh`, `saves.sh`, coverage, `replay_cover.sh`, and the small checkers `check.sh` runs |
| `tools/` | research tools: the scriptable DOSBox (`dosbox.py`, `lockstep.py`), disassemblers, the replay maker |
| `docs/original/`, `docs/formats/` | our write-ups of the original's engine and file formats |

## How the work is done

- **Before a commit that touches the engine:** `./scripts/goldens.sh` (every replay against its golden, under a
  minute) and `./scripts/check.sh` (format, constants defined once, every kind's state in its `Saved`,
  ASan/UBSan build with lint, unit tests, coverage of at least 80%). After a change that is only formatting,
  comments or docs neither needs running again. On the host, `./run-dev-env.sh test` does both in the container.
- **After adding state to `World` or to a kind:** `./scripts/saves.sh` (a way through each level, the demo, the
  ending and a new game, with the world written out and read back before every step: cut down from every replay
  by the user, 2026-11-25; `--all` for every replay, about a minute), and raise `kStateVersion` (a test in `test_play.cpp` fails
  until its sizes and version are brought up to date).
- **After a change to how the GPU renderer draws:** `lv-replay --check --gpu` on a replay or two.
- **What only the eye can judge is tried before it is committed.** Since M7 the user tries a change on the host
  first and says when to commit; stills for them go into `gamedata/previews/` (not in the repository). When a
  new request comes while finished work is still uncommitted, say "let's commit this first" before starting it;
  what is in the tree goes in as one commit, never rebuilt into several (the user, 2026-11-25).
- **The game itself runs on the host**, not in the container (no display or sound there):
  `./build/release/bin/vikings-remaster --slot 0` (Vanilla rules: the Classic sound only), or `--rules remaster`
  for the Remaster rules (whose sound, screen's look and visual effects are the menu's and the profile's, not
  start-up options: the user, 2026-11-25).
  What a Remaster window would show can be written as a picture in the container (`--dump`, with `--zoom`).
  Rebuild the release preset before asking the user to try something: `goldens.sh` only builds the replay tool.
- **A golden is never recorded again to make a difference go away.** There is no oracle left to record from: a
  difference is a change of behaviour until shown otherwise. `lv-replay` writes what the engine shows at the
  first refresh that differs; run the commit before the change the same way to see what it showed before. The
  one exception so far is the user's: the fade-outs, once DOSBox showed the original's ([`replays/README.md`](../replays/README.md)).
- **Something new or in doubt about the original:** `tools/lockstep.py` plays the same input to the original in
  DOSBox and to the engine and compares the pictures ([`dosbox.md`](dosbox.md)). It starts at the first logo;
  with the cheat's password typed, F6 goes on a level at a time, and `--place` puts the Vikings where they are
  needed. A run worth keeping becomes a replay. The user can also play it by hand on the host (a Wayland
  session: `SDL_VIDEODRIVER=wayland`, [`dosbox.md`](dosbox.md), "By hand").
- **A fault of the original is not copied.** When a comparison turns up an accident of the original (a stray
  write, a glitch pixel), trace it far enough to be sure, write it into [`quirks.md`](quirks.md) as "not kept", and
  leave it out.
- **Classic is the original; Modern is ours.** The Classic sound keeps the original's limits and faults. The
  Modern sound is shaped by ear: its effects have their own synthesizer, and an effect that grates is remade in
  the `kRemade` table (`src/audio/player.cpp`). The user judges sound; it can't be judged from here.
- **Nothing of the game goes into the repository:** no data, art, text or decompiled code, in code, comments,
  tests or docs. Passwords as test input are the one exception the user allowed.
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
