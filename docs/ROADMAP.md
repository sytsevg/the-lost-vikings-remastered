# Roadmap

What is built, and what is ahead in build order. How the game is built: [`architecture.md`](architecture.md); the
decisions behind it: [`decisions.md`](decisions.md); where the work stands today: [`HANDOVER.md`](HANDOVER.md).

## Built

The whole game under Vanilla rules, with Classic visuals and both sounds.
- **The original's data read directly:** every `DATA.DAT` entry the game loads, the texts and tables in
  `VIKINGS.EXE`, and the SNES image's backdrops ([`formats/`](formats/)).
- **The original understood:** its engine and file formats written up from the disassembly and from runs in a
  scriptable DOSBox ([`original/`](original/), [`dosbox.md`](dosbox.md), [`quirks.md`](quirks.md)).
- **The engine:** all 37 levels and every scene, each kind of entity written by hand, matching the original's
  picture and sound requests at every refresh of every replay ([`../replays/README.md`](../replays/README.md)).
- **Sound:** Classic (OPL3 as the original's driver played it, matching its register writes) and Modern (General
  MIDI through a SoundFont), swappable while playing.
- **`vikings-remaster`:** the game in a window, keyboard and gamepad, rebindable keys.
- **Remaster rules, so far** (`--rules remaster`; M7 below): the window on the GPU renderer, zoom-out in three
  steps with the HUD laid over the view, text boxes that follow the view, no limit on entities or sprite pieces,
  a minimap with fog of war, ten seconds of rewind a level (a bar in the minimap), and a quicksave with the cheat
  on.

## M7: Remaster rules
[`decisions.md`](decisions.md), ADR-0011. Zoom-out, the GPU renderer it is shown by, the minimap, the
rewind, a quicksave, and the player's profile on disk that keeps the last two are built. Nothing of M7 is open.

## M8: Modern visuals and sound
- **Events for effects:** the moments in an animation an effect can answer to (a frame shown, a sound asked for,
  its end), named beside the animation in `animations.hpp` and reported by the engine
- **Effects on those events:** sparks where electricity strikes, a small burst where a bullet lands; shaders or
  particles, in the Modern visuals only
- On the GPU renderer (built for Remaster rules, ADR-0006): smooth movement between ticks (built; Remaster rules show
  the Modern visuals, ADR-0005)
- Parallax: the SNES backdrops behind the levels and the village's scenes, from the user's own ROM image (built,
  2026-11-24; HANDOVER.md, "Parallax")
- Light sources that affect sprites, shaders (a CRT look, for one)
- Art packs: replace sprites one for one
- Modern sound: the effects placed by position, and extra effects (notes below)
- **Sound: the SNES version, as an exercise first.** Its music and effects (sampled instruments with echo, and
  effects for things the DOS version is silent on: bow, sword). To find out: how the game starts tunes and effects,
  what playing them takes (an emulator of its sound CPU running the game's own driver, permissively licensed or
  ours), and how DOS tunes and effects map to SNES ones. Found so far: the sound is not in the ROM's chunk table; it
  sits with the code below `0x58000` (a second table at `0x2A64F`, not looked into). If it works out: a third
  sound choice, from the user's own ROM image

## M9: Co-op
[`decisions.md`](decisions.md), ADR-0012.
- Local, 2–3 players, a split screen that merges when everyone fits; an active area per player's view, or
  everything running (to be decided: ADR-0011)
- Online (lockstep or rollback), a full screen each

## Later
- Public distribution; the SoundFont (host our own copy of GeneralUser GS or link the author's site, not his
  download files)
- More sound devices on the same decoders and sequencer: OPL2 (what most Sound Blaster owners heard), the MT-32
  for players who own its ROMs
- Windows and macOS

## Notes for what is ahead

### Parallax from the collision map
- The SNES backdrops were laid in without it (2026-11-24): where the DOS game shows nothing it draws a palette
  row's colour 0, the very pixels the console shows its backdrop through. What follows is for parallax layers of
  our own, and other Modern visuals.
- The level dump writes a **collision map** next to the picture: each cell coloured by what its tile type does to
  entities (free, ground, slope, ladder, …), and the entities that act as terrain (elevators, bridges) outlined.
- For Modern visuals: whatever entities can touch (the edges of ground, slopes, platforms) is the playing
  layer, in front; cells that are free are background, which can be moved back and scrolled at a different speed
  for parallax. The collision map shows where that split falls.
- The collision map is also where the user decides which tile types to **animate or draw differently** in Modern
  visuals (electric traps, ladders, spikes, …).

### Extra sound effects, placed by position (Modern sound)
- **What:** new effects on top of the original's: an electric buzz near the electric traps, the bow drawn and
  the arrow loosed, Olaf's shield up and down (a woosh), footsteps of Vikings and enemies, the slide when a
  running Viking turns, sword swings, the thump of a landing; steps and landings sound by surface (metal, rock,
  dirt, …).
- **Switch:** Sound Classic is the original exactly (no panning, no extras). Sound Modern is all of it: the General
  MIDI music, the original effects placed by position, and the new effects. One setting.
- **Sound cues:** behaviours send cues (what happened, the entity, the surface for steps and landings); the sound
  side decides what plays. The original's effect calls are cues too, with their original effect number: Classic
  plays only those. Cues are written into each kind **as it is translated**, from level 1 on, before anything plays
  them.
- **Placement:** left/right by the entity's position across the player's view; entities off screen but in the
  active area quieter with distance, silent outside it; stereo only; the music not placed. In co-op, by the
  player's view. The original effects can only be placed if each plays on its own voice (one synthesizer mixes
  them with the music otherwise): a synth instance or channel per effect in Modern.
- **Surfaces:** a material per metatile per world, a default per world, assigned with the collision map; the user
  reviews them.
- **Where the sounds come from:** still open. Free CC0 libraries are one way; but many of the game's effects are
  MIDI, so new ones could also be **MIDI sequences through the same SoundFont** (General MIDI has sound-effect
  programs and a percussion kit) and fit the game's own sound; or **synthesized from parameters** (noise, tones,
  envelopes, filters, as retro sound tools do). Claude can't hear: it can build candidates and measure an example
  the user gives (its length, loudness over time, pitch, spectrum) to get close, and the user judges by ear. To be
  worked out as an exercise together when this starts. Files loaded from `gamedata/`, WAV first.
- **The SNES version** has effects for some of these (bow, sword swings: user, 2026-11-18), and a sampled
  soundtrack: a possible third sound choice and a source of effects (below, an exercise first).
