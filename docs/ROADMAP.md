# Roadmap

What is built, and what is ahead in build order. How the game is built: [`architecture.md`](architecture.md); the
decisions behind it: [`decisions.md`](decisions.md); where the work stands today: [`HANDOVER.md`](HANDOVER.md).

## Built

The whole game under Vanilla rules, with Classic visuals and sound; Remaster rules with Modern visuals and sound.
- **The original's data read directly:** every `DATA.DAT` entry the game loads, the texts and tables in
  `VIKINGS.EXE`, and the SNES image's backdrops ([`formats/`](formats/)).
- **The original understood:** its engine and file formats written up from the disassembly and from runs in a
  scriptable DOSBox ([`original/`](original/), [`dosbox.md`](dosbox.md), [`quirks.md`](quirks.md)).
- **The engine:** all 37 levels and every scene, each kind of entity written by hand, matching the original's
  picture and sound requests at every refresh of every replay ([`../replays/README.md`](../replays/README.md)).
- **Sound:** Classic (OPL3 as the original's driver played it, matching its register writes) and, under Remaster
  rules, Modern (General MIDI through a SoundFont, its effects placed by position), swappable while playing.
- **`vikings-remaster`:** the game in a window, keyboard and gamepad, rebindable keys; under Remaster rules a menu
  for the settings and the controls (keyboard and gamepad), kept in the profile.
- **Remaster rules** (`--rules remaster`; M7 and M8 below): the window on the GPU renderer, zoom-out in three
  steps with the HUD laid over the view, text boxes that follow the view, no limit on entities or sprite pieces,
  a minimap with fog of war, ten seconds of rewind a level (a bar in the minimap), a quicksave with the cheat on,
  and the Modern visuals and sound.

## M7: Remaster rules
[`decisions.md`](decisions.md), ADR-0011. Zoom-out, the GPU renderer it is shown by, the minimap, the
rewind, a quicksave, and the player's profile on disk that keeps the last two are built. Nothing of M7 is open.

## M8: Modern visuals and sound (done, 2026-11-25)
- Smooth movement between ticks on the GPU renderer (ADR-0006; Remaster rules show the Modern visuals, ADR-0005)
- Parallax: the SNES backdrops behind the levels and the village's scenes, from the user's own ROM image
  (HANDOVER.md, "Parallax")
- Effects: light, glow and particles answering to the animation an entity plays, the map's electricity and the
  turning colours, the Vikings' deaths by electricity and fire; `--showcase` to see them (HANDOVER.md, "Effects")
- The screen's look: Hyllian's CRT, and the pixels rounded off by xBR; chosen in the menu (HANDOVER.md, "Screen
  look")
- The Modern sound's effects placed by position, and its music with voices enough (ADR-0015)
- With it, under Remaster rules: a menu for the settings and the controls, keyboard and gamepad, a stick walking
  and climbing as fast as it is pushed (ADR-0011)
- Dropped (the user, 2026-11-25): art packs (sprites replaced one for one) and the SNES version's sound

## M9: Co-op online
[`decisions.md`](decisions.md), ADR-0012. Online only (the user, 2026-11-25: no local co-op); up to three, by
rollback. In this order, a commit each:
- Players in the world: keys, a camera and a Viking each; one player exactly as before (built, 2026-11-25:
  HANDOVER.md, "Players in the world")
- Several views and the rules of play online: the active area every view's together, a Viking's player and
  viewers, the texts that stop the game and those that don't, an inventory each and handing over within reach,
  the host's keys
- Rollback-ready play: all of a step's state in its snapshot, steps run again without pictures, each sound played
  once, the state hashed; the sync test on every replay
- Rollback after GGPO's design: prediction, a window of 8 steps, input delay, time sync, the out-of-step check
- The transport: SDL3_net, UDP with TCP behind it
- The front end: hosting and joining, the names under the Vikings, the latency in the menu, a mark while waiting
- Joining a game under way, and joining again
- Hosting and joining from the menu

## M10: Extra sound effects (Modern sound)
- **What:** new effects on top of the original's: an electric buzz near the electric traps, the bow drawn and
  the arrow loosed, Olaf's shield up and down (a woosh), footsteps of Vikings and enemies, the slide when a
  running Viking turns, sword swings, the thump of a landing; steps and landings sound by surface (metal, rock,
  dirt, …). Placed by position as the original's effects are (ADR-0015). The Classic sound stays the original's,
  without them.
- **Sound requests:** the kinds report what happened (`SoundCue`: the entity, where, and the surface for steps
  and landings); the sound side decides what plays. The original's requests keep their effect numbers, the only
  ones the Classic sound plays.
- **Surfaces:** a material per metatile per world, a default per world, assigned with the level dump's collision
  map (`lv-export`); the user reviews them.
- **Where the sounds come from:** still open. Free CC0 libraries are one way; but many of the game's effects are
  MIDI, so new ones could also be **MIDI sequences through the same SoundFont** (General MIDI has sound-effect
  programs and a percussion kit) and fit the game's own sound; or **synthesized from parameters** (noise, tones,
  envelopes, filters, as retro sound tools do). Claude can't hear: it can build candidates and measure an example
  the user gives (its length, loudness over time, pitch, spectrum) to get close, and the user judges by ear. To be
  worked out as an exercise together when this starts. Files loaded from `gamedata/`, WAV first.

## Later
- Public distribution; the SoundFont (host our own copy of GeneralUser GS or link the author's site, not his
  download files)
- Windows and macOS
