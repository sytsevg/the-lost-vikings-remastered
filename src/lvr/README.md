# src/lvr: the engine

Design: [`docs/architecture.md`](../../docs/architecture.md),
[ADR-0017](../../docs/decisions.md), [ADR-0018](../../docs/decisions.md).
Target `lvr::engine`, plain C++ on `lvr::assets`.

- `level.*`: the level model: `Level`, `Map` (cells with a metatile and a tile type), `Placement`, the start
  entities; built from the decoders' `LevelData` / `SceneData` (`make_level`, `make_cutscene`). `lv-export assets`
  dumps every level and cutscene from it ([`docs/formats/level.md`](../../docs/formats/level.md#exported-output)).
- `render.*`: the Classic renderer: a `View` (what one picture shows) drawn into a 320 × 240 `Screen`. It draws;
  it decides nothing.
- `gpu.*`, `gpu_quad.*`, `gpu_show.*`, `gpu_over.frag`: the GPU renderer (target `lvr::gpu`, on SDL_GPU) and its shaders: the same
  `View` drawn on the graphics card, at any view size, kept there and shown scaled. What Remaster rules show.
- `sprites.*`: sprite pieces (`Piece`, 128 to a layer in three pools; 2,048 under Remaster rules), an entity's `Sprite`, taking and letting go
  of pieces, the animation player (the original's animation programs, imported and played as they are), and the
  pieces made into pictures for the renderer (`PiecePainter`).
- `profile.*`: the player's profile on disk (Remaster rules): so far what the minimap has seen.
- `minimap.*`: Remaster rules' minimap: the level small, by what its cells are, as far as it was seen.
- `camera.*`: the camera: its limits, where it starts, how it follows the Viking being played; the size of the
  view (the original's, or a zoomed one).
- `entity.hpp`: `Entity`, the base class of every kind (ADR-0018): what the engine reads and moves of each.
- `movement.*`: speed and gravity into a tick's push, and the move itself.
- `world.*`: `World`: the map, the entity slots (20, or 512 under Remaster rules), the pieces, the camera; kinds' records, creating and removing
  entities, placements and the active area. Grows with the engine.
- `terrain.*`: what an entity finds around it and how the map stops it (walls, floors, slopes, the ground).
- `hits.*`: which entities touch which, and who stands on or hangs under an entity.
- `play.*`: a level loaded from the game's files in the engine's terms, its world as it starts, and the view of a
  world for the renderer.
- `sound.hpp`: `SoundCue`: what the game reports to the sound side.
- `dialogue.*`: a conversation's lines and the text layer.
- `inventory.*`: the inventory screen: what the Vikings carry looked through, handed over, thrown away.
- `session.*`, `main.cpp`: the engine as a game to play (the scenes one after another, a picture and the sound
  for every refresh), and `vikings-remaster`, the front end with its window and devices.
- `places.*`: where each world keeps the animation programs that the kinds name by their place in their home
  database; paired up as a level is loaded.

**What the kinds are written with** (the vocabulary; [`docs/architecture.md`](../../docs/architecture.md), "How a
kind reads now"):
- `names.hpp`: one name for each number several files need: keys, databases, scenes, levels, slots, hit classes
  and bits, sound effects, items, text colours; `px()` for a speed, `bits()` for several bits.
- `animations.hpp`: every animation by name (`anim::`), with the database it is named in.
- `dialogues.hpp`: every conversation as rows of who says which text and where (`talk::`), said with `speak()`.
- `kinds.hpp`, `kinds.cpp`: each world's kinds by number, the messages entities tell each other, the makers of
  each file's kinds, and `make_kind`.
- `looks.*`: what a kind looks for after a tick's move and what it saw (ADR-0019).
- `shared.hpp`: the tile sets, and the steps many kinds take (`harm`, `tell`, `switches_on`, `vikings_in_row`,
  `squash`, `leave`, `StraightShot` ...).
- `enemies.hpp`: `Enemy`, what the enemies of every world share: being hurt, being led by a scene, turning,
  seeing a Viking ahead.

**The kinds**, each family in a file of its own. A family's file ends with its list: which class each kind's
number is, and whether the number means that in every world or in one world's own list (`kinds.cpp` reads the
lists):
- `viking.hpp` (the class), `vikings.cpp` (moving), `viking_abilities.cpp` (what only one of them can do),
  `viking_things.cpp` (items, keys, the action key), `viking_harm.cpp` (being hurt and dying),
  `viking_worlds.cpp` (what the worlds and scenes have them do), `viking_looks.hpp` (each one's animations and
  speeds).
- The enemies: `walkers.cpp` (walk and strike), `chargers.cpp` (hop, roll, charge), `shooters.cpp` (shoot at a
  Viking ahead, walking or flying), `bombers.cpp` (throw in an arc, and the bomb); `enemies.hpp` is what they
  all share.
- `shots.cpp` (the guns on walls, every shot that flies, what is left when something bursts), `weapons.cpp`
  (the Vikings' own: the sword, arrows, bullets, sparks), `items.cpp`.
- `switches.cpp` (what the Vikings work: levers, buttons, locks, targets, places for an item, the teleporter),
  `switched.cpp` (what the switches work: walls, doors, floors, bridges, barriers, beams), `machines.cpp` (what
  runs while its switches are on: presses, pistons, furnaces, fans).
- `blocks.cpp` (pushed, falling, dropping and sinking blocks, what breaks), `drops.cpp` (what keeps coming from
  above, bubbles, and their makers), `platforms.cpp` (the moving platforms, the lift, the spring), `crane.cpp`
  (the magnet, what it lifts, the trolley).
- `conversations.cpp` (a level's start and end, the hint boxes, the ones to talk to), `tomator.cpp` (the last
  level), `gameover.cpp`.
- The scenes outside the levels: `title.cpp` (logos, the title and its menu, the galaxy), `ship_scenes.cpp`,
  `village_scenes.cpp`; a scene's script is a list of cues (`cues.hpp`: `cue::say(...)`, `cue::wait(...)`).
