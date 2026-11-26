# Architecture

How the game is built, as it stands. Why it is built this way: [`decisions.md`](decisions.md). Where
the project is and what is open: [`HANDOVER.md`](HANDOVER.md).

## The parts

| Part | What it is | Depends on |
|---|---|---|
| `src/assets/` (`lvr::assets`) | Reads the original's files: the `DATA.DAT` container and its compression, palettes and pictures, sprites, level headers and maps, the texts and tables in `VIKINGS.EXE`, the object databases and their scripts (decoded and listed, not run), XMIDI, the optional SNES image. Plain C++. | nothing |
| `src/lvr/` (`lvr::engine`) | The engine: a level or scene as a world of entities, run a tick at a time and drawn. Every kind of entity. Plain C++. | assets |
| `src/audio/` (`lvr::audio`) | The XMIDI sequencer, the OPL3 driver's behaviour on ymfm (Classic), General MIDI on TinySoundFont (Modern). | assets |
| `src/lvr/session.*`, `main.cpp` | The game: the scenes one after another, a picture and the sound for every refresh (`Session`), and `vikings-remaster`, the window and devices (SDL3), its loop playing one place at a time. | engine, audio, input, image, front |
| `src/lvr/front.*`, `handler.*` (`lvr::front`) | The game's places (`GLOSSARY.md`): Offline, Connecting, Lobby and Online, each saying what is played and shown and where to go next, and `Places`, every move between them; `InputHandler`, the player's input between steps as the window gives it. Plain C++. | session, input, net |
| `src/net/` (`lvr::net`, `lvr::transport`) | Co-op online (ADR-0012): rollback, and the protocol (the connection, the lobby, a game online) behind `net::Game` and `net::Transport`; the transport on SDL3_net, and a simulated network for tests. | nothing (the transport: SDL3_net) |
| `src/lvr/gpu.*` (`lvr::gpu`) | The GPU renderer, what Remaster rules are shown by (SDL_GPU; its shaders are `gpu_*.vert` and `gpu_*.frag`). | engine |
| `src/input/`, `src/image/` | Keys and gamepad into the original's keyboard word, with rebinding; indexed PNG through SDL3. | assets |
| `src/replay/` | Replays, goldens, and `lv-replay`, which plays one against the other. | engine |
| `src/exporter/` | `lv-export`, a development tool: the game's data as PNG and JSON, the scripts as listings. | assets, engine |

[`../src/lvr/README.md`](../src/lvr/README.md) names every file of the engine.

## A step, a tick, a refresh

- The display's **refresh** (59.71 Hz) is the clock. A **tick** of a level is three refreshes; fades, menus, the
  inventory and a text waiting for a key count refreshes their own way.
- `Play::step(keys)` is one pass of input as the original's loop reads it: a tick, a pass of a menu, or a pass of
  the wait for a key. It reports how many refreshes it took and hands each picture out as it is ready. A replay is
  a list of such steps.
- A tick (`World`): read the keys; every entity's turn, in slot order; the moves (speed and gravity into a push,
  the push taken as whole pixels); what each entity looks for after its move (walls, ground, others it touches);
  the animations; the camera; the HUD. Three pictures are drawn on the way, with the original's sub-steps of the
  movement.
- A scene change fades out, lets every entity leave, loads the next scene from the game's files and fades in, all
  inside the step that asked for it.
- Everything is integers, and nothing reads the time: the same input gives the same pictures.

## Scenes and worlds

The original numbers its scenes 0 to 47: the 37 levels, the game-over scene, the logos, the title, the galaxy
between worlds, the village's scenes, the starfield and the ship. Each is loaded into a `World`: a map (or one
picture), 20 entity slots, the sprite pieces, the level's switches, the players (one, or up to three online:
each with his keys, the Viking he plays and the camera of his view; ADR-0012), and what a level's rules need (the
inventory, the text being shown). The title, the menus and the cutscenes are entities
like any other; a cutscene's script is a list of cues (`cues.hpp`).

What carries from one scene to the next is little and listed in one place (`Play::change_scene`): the password,
the cheat's switch, the keys held, a few counters.

## Entities and kinds

An entity is an object of its kind's class ([`decisions.md`](decisions.md), ADR-0018): its own state and data,
and what the engine reads and moves of every entity (place, box, speed, flags, health, sprite, what it acts as to
others, what it was told). What a kind remembers is all in one plain struct nested in the class, `Saved`, as its
member `s`, with `LVR_SAVES(Base)` after it: that is what a snapshot, a quicksave and later a network snapshot
write (ADR-0020), and a new field needs nothing but being added there. A kind is a class with a state and a turn; what it needs from the original's files it
gets by name, and what many kinds do is a verb.

The kinds are sorted into files by family (`walkers.cpp`, `shooters.cpp`, `blocks.cpp`, `switched.cpp` …). Each
family's file ends with its list: which class a kind's number is, and where the number means that, since some
numbers mean another kind in another world. `make_kind` in `kinds.cpp` reads the lists.

- **Tables, not numbers.** `animations.hpp` (`anim::kGunnerWalks`: every animation, with the database it is named
  in), `dialogues.hpp` (every conversation as rows, said with `speak()`), `kinds.hpp` (each world's kinds and the
  messages), `names.hpp` (keys, scenes, levels, slots, hit bits, sound effects, items). A number in a kind's code
  is hex only if it points into the original's files or defines a bit; a speed is `px(2.5)`, the rest decimal.
- **What it looks for** (ADR-0019): `looks_for()` (an `Enemy`: `looks()`) returns a list by state, such as
  `{stopped_by(walls()), lands_on(ground()), touching_a_viking()}`; the engine does the looking after the move,
  and the next turn reads the answers in `saw` (`saw.wall`, `saw.landed`, `saw.viking`).
- **Verbs** (`entity.hpp`, `shared.hpp`, `enemies.hpp`, `world.hpp`): `first_turn()`, `animation_over()`,
  `start_falling()` / `stop_falling()` / `is_falling()`, `stay_active()`, `count_down_from(30)`;
  `world.play(*this, anim::k...)`, `world.animate(*this)`, `world.sound(*this, effect::k...)`,
  `world.spawn(*this, kind, x, y)`; `harm(whom, what, how_much, from_the_right)`, `tell(whom, what, with)`,
  `switches_on(world, argument)`, `vikings_in_row(world, *this, Row::kAbove)`, `squash`, `leave`,
  `gone_for_good`, `was_asked`; an enemy's `appear`, `turn_round`, `turn_from_colleague`, `viking_ahead`,
  `landed_from_fall`, `leave_remains`, `told_all`, `follow`.
- **Families** share a base only where the rule is one: `Enemy` (being hurt, being led by a scene),
  `StraightShot` (what flies until something stops it). The walkers each list their own steps in their own
  order, with the verbs above: on purpose, since they differ in too many small ways to share one.

A short one, whole:

```cpp
// Where an item (its argument's low byte) drops out, thirty ticks after its switches (the high byte) come on.
class Dispenser : public Entity {
  public:
    void update(World& world) override {
        switch (state_) {
            case State::kStart:
                item_ = argument & kLowByte;
                argument &= kHighByte;
                state_ = State::kWaiting;
                return;
            case State::kWaiting:
                if (switches_on(world, argument)) {
                    world.sound(*this, effect::kDispenserWorks);
                    count_down_from(30);
                    state_ = State::kWorking;
                }
                return;
            case State::kWorking:
                if (countdown != 0) {
                    return;
                }
                world.spawn(*this, kind::kItem, x, y, bits(item_, item::kFalling), entity_flag::kAlwaysActive);
                gone_for_good(world, *this);
                return;
        }
    }
    ...
};
```

### Writing a kind from its script
1. Read its script in `gamedata/exported/scripts/449.txt` (`lv-export assets` writes it): find
   `; ---- object type(s) NN`. The first label is its turn when it has left the active area (most jump to a
   `remove`; the enemies first write their place and health back into their placement); the second is where a
   new entity starts. A copy without the bytes column reads faster.
2. Each `yield` is where a tick's turn ends: the places it waits become the class's states (ADR-0018).
   Instructions between a `yield` and the next `end` are hit and terrain tests: they run later in the tick
   (the kind lists them in `looks_for()`, the engine runs them) and are acted on, in the order they stand
   there, at the start of the next turn (read from `saw`). A handler that returns goes on with the next test.
3. Write the class in its family's file (`walkers.cpp`, `shooters.cpp`, `blocks.cpp`, `switched.cpp` ...:
   `src/lvr/README.md` names them all) and add a row to the list at the end of that file: the world whose
   number it is (`Home::kAnywhere` for a number that means the same everywhere), the kind, `make<TheClass>`.
4. Build the release preset and run `lv-replay` on a replay that has it; the report names the refresh and the
   entities (slot, kind, place, flags). A sound that differs, or an entity a pixel or two off, usually
   says which branch was read the wrong way round.

### Reading the listing
- **Which way a test jumps:** `tile1E`–`tile21` and `op49` jump if something is **found**; `tile22`–`tile25` and
  `op4A` if **not**. `if_anim_running @X` jumps when the animation is **not** waiting (`sprite.delay == 0`).
- **Bits:** `A=bit f BB, FF` is bit `BB / 2` of field `FF`; `A=bit g 1E, 03B8` bit 15 of the keys newly pressed.
  The keys: `1E` F, `1C` D, `0E` S, `0C` E, `16` up, `14` down, `12` left, `10` right. `setbit f 1E, 08` is the
  flag "falls" (gravity); `bit f 0C, 08` facing left.
- `target=A` takes a slot × 2: 0, 2, 4 are Erik, Baleog, Olaf (slots 0, 1, 2; kinds `01`, `00`, `02`).
  `mov t,A 30` tells the target something: field `30` is `Entity::message`, field `32` what goes with it
  (`message_value`; its top bit is the side a blow came from).
- Fields: `00`/`02` the box's top and bottom, `04`/`06` left and right, `08` flags, `0A` health, `16` argument,
  `1E`/`20` x/y, `12`/`14` speed (ahead and down), `26` `acts_as`, `0E`/`10` hit class and mask, `1A` kind, `1C`
  countdown, `28` creator, `36` the packed frame showing (`sprite.packed_frame`), `38`/`3A` this tick's push,
  `3C` target, `42` the animation. `2E` and `34` are each kind's own.
- Globals: `25AD` the slot (level − 1), `25AB` the slot before (`25` is game over), `03CC` a demo is playing,
  `25C5` the level's switches, `03C2` the Viking being played and `0042` the slot whose turn it is, `03B6`/`03B8`
  the keys held / newly pressed, `0044`/`0046` the camera, `034E`/`0350` how far it last moved, `0276` the
  Vikings still falling in, `0278` still arriving, `027A` the retries, `021E` the hints seen, `03E4`–`03FA` the
  items by square, `0414`–`0418` the chosen squares, `0206`–`020A` scratch.
- The tile type lists (`tile23 17`: list `17`) are the same in all six databases; the kinds name the ones they
  use as sets (`ground`, `solid`, `ladder`, ...).
- `anim NNNN` starts the animation program at that place in the database; `anim_step` is `World::animate`.
  A kind names an animation by its place in the database it was read from, its **home**: the spaceship's (449)
  unless its entry in `animations.hpp` says so (`{0x8fcf, database::kEgypt}`). `places.*` pairs every home's places with the level's
  database at load, by reading the two databases' scripts side by side, so one class serves every world that
  has the kind. A kind of another world is read from that world's listing (`scripts/450.txt` … `453.txt`).
- `rel_target g206, g208` is how far the target is ahead and below (`World::offset_to`); `rel_nearest_viking` the
  same for `World::nearest_viking`.
- `gflag_set` is `World::gone` for the entity's placement; `set_flag_2000` is `entity_flag::kPutOnGround`;
  `hdr_list…` writes into the entity's placement (flags masked with `entity_flag::kKeptInPlacement`).
- A `call` is one level deep. A routine that waits inside and then jumps on by field `2E` (the Vikings' "act"
  and "use an item") is why `Viking` keeps `Doing`: it says where to go on.

## Entities out of view: removed, not paused

- **What happens:** nothing is paused. An entity whose box leaves the view (camera −16 … +336 across, −16 … +192
  down) gets the restart flag, and its kind's restart entry runs on its next tick. For almost every kind that
  removes it. When the camera comes back, the placement creates it again from the start of its behaviour.
- **Three kinds of restart entry** (count of placements, levels they occur in):

  | Restart entry | Placements | What it means | Kinds |
  |---|---|---|---|
  | remove | ~1,000 | starts over when seen again | items, lasers, keyholes, buttons, hazard doors, crumbling walls/tiles, question boxes, turrets, crushers, fans, invisible logic (336) |
  | save, then remove | ~250 | writes x, y, flags (some also field `0A`, the health) back into its placement, so it comes back where it was, facing the same way | **almost every enemy** (192, the kinds that draw from the shared sprite pool), elevators, bridges, switches, magnets, presses, girders, trusses, blocks |
  | always active (placement flag `0x0800`) | ~65 | never leaves | seesaws, pistons, some crumbling tiles and blocks, a bridge, a door, a laser, 45 invisible logic entities (about one per level) |
  | keeps running | 111 | its restart entry doesn't remove it | springs (type `86`) |

  What a save keeps is only position, facing and sometimes health: not the animation, timers or where in its
  behaviour it was. Placement flag `0x0100` creates the entity again at once if its placement is still in view.
- **Why:** the engine has room for **20 entities** at a time (3 of them the Vikings) and small pools of sprite
  pieces (32 for the enemies' pool). A level has up to ~70 placements. So it is a limit of the machine, with the
  save written so puzzles stay as the player left them (an elevator stays up, a switch stays thrown, an enemy
  stays where it was pushed). It also gives the game its rhythm: an enemy starts its behaviour as it comes on
  screen, never off it.
- **In the engine:** under Vanilla rules this is reproduced. Under Remaster rules it works the same way around a
  larger view: the active area, and the edge the enemies turn at, follow the zoomed view
  ([`decisions.md`](decisions.md), ADR-0011), and there are 512 slots instead of 20.

## Sprites and animations

- A sprite is made of **pieces** (8 × 8, 16 × 16 or 32 wide). The engine keeps the original's 128 pieces in three
  pools with its way of handing them out, because that decides what is drawn over what.
- **Animations are imported, not translated.** The original's animation programs (set a frame, move the pieces,
  wait, ask for a sound, jump) are played as they are by one small player (`sprites.*`). A kind names an
  animation; `places.*` finds where each world's database keeps it.
- The Vikings and the enemies use packed frames, unpacked into one of eight 16-colour palettes by the entity's
  flags.

## The picture

`render.*` draws a `View` (the map, the sprites in order, the text layer, the HUD, the palette) into a 320 × 240
picture of palette indices. It draws the whole picture every time and decides nothing. The original's hardware
tricks (panning, page flips, restoring under sprites) are not copied; where they left something visible that is
part of the game it is modelled, and two accidents are deliberately not ([`quirks.md`](quirks.md), "Drawing").

Under Remaster rules the window's pictures are drawn by `gpu.*` instead (ADR-0006), from the same `View`:
- **Drawing:** everything a scene is made of is in an atlas of palette indices (the shared pictures, the
  scene's, a sprite's picture the first time it is shown). A picture is a list of rectangles of the atlas,
  drawn in the Classic renderer's order into a texture of indices as large as the view, with the HUD's rows
  under it.
- **Kept, then drawn when shown:** for the window a picture is kept as that list (the last 512, since a step
  can make 150 of them before the first is shown; a fade's identical ones once) and drawn when it is shown, as
  many pixels a pixel as the window shows it at; one that doesn't move between ticks is drawn once, however
  many times the window refreshes while it is shown (a fade's colours are applied after). The atlas has pages: a scene's pictures stay on theirs while
  a kept picture is drawn from it, and a new scene (or a page full of sprites) takes a free one.
- **Smooth movement:** each picture carries its `Motion` (`render.hpp`): which of a tick's three it is, each
  sprite's entity's movement in the tick (`DrawnSprite::moves`, from `view_of`), and where the camera was at the
  end of the tick before and is at this one's (`Session::step`). Shown `between` (0 to 1) into its refresh,
  each rectangle is moved by the difference between an even movement and the original's shares
  (`moved_since`, `camera_moved`). The window passes `between` from its clock, at the screen's own rate.
- **Showing:** a second pass looks each index up in the palette and scales: the playfield's part, then the HUD,
  at one scale. Shown smaller than it is, a pixel is the mean of the pixels it covers.
- **The view is the world's** (`Camera::view_width`, `view_height`): the camera's limits and follow box, the
  active area, "in view" for the kinds and the text layer's size all read it. `Session` owns the zoom: which
  view is wanted, `World::set_view`, and the part of each picture that is shown while the view changes
  (`Screen::part`).

## Sound

The engine asks for sounds as **cues** (an effect, a tune's action, effects or music off and on), in the refresh
they happen in. In the window the sound card pulls: it asks for a few milliseconds of sound at a time
(`Session::render_live`), and each picture's cues are handed over at the moment it is shown
(`Session::hear_now`), so an effect starts within the device's buffer of its picture, and the music plays on
through a stall. Without a device (a dump to a file, the tests) a step's sound is made with the step, exactly a
refresh at a time. Classic and Modern play the same cues from different data. Classic is one player, as the original's driver: four
effects at most, which borrow channels from the music. Modern has a player and a synthesizer for the music and
another pair for the effects, mixed.

## How it is checked

- **Unit tests** (`src/*/tests/`): the decoders on hand-made data, the engine's services and kinds on small stages,
  and `[real-data]` cases on the user's own files. `./scripts/check.sh` runs them with the sanitizers, the lint and
  the coverage floor.
- **Replays against goldens** (`replays/`, `./scripts/goldens.sh`): every replay played in the engine, and a hash of
  each refresh's picture, palette and sound cues compared with the recorded run
  ([`../replays/README.md`](../replays/README.md)); in the same pass, before every step, the world written out and
  read back and the last 8 steps run again from a snapshot (ADR-0020, ADR-0012).
- **The original in DOSBox** (`tools/lockstep.py`, [`dosbox.md`](dosbox.md)): the same input to both and the
  pictures compared, for anything new or in doubt.
