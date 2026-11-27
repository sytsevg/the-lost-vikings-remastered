# Roadmap

What is built, and what is ahead in build order. How the game is built: [`architecture.md`](architecture.md); the
decisions behind it: [`decisions.md`](decisions.md); where the work stands: [`HANDOVER.md`](HANDOVER.md).

## Built

The whole game under Vanilla rules, with the Classic picture and sound; Remaster rules with the Modern visuals and
sound, and co-op online.
- **The original's data read directly:** every `DATA.DAT` entry the game loads, the texts and tables in
  `VIKINGS.EXE`, and the SNES image's backdrops ([`formats/`](formats/)).
- **The original understood:** its engine and file formats written up from the disassembly and from runs in a
  scriptable DOSBox ([`original/`](original/), [`dosbox.md`](dosbox.md), [`quirks.md`](quirks.md)).
- **The engine:** all 37 levels and every scene, each kind of entity written by hand, matching the original's
  picture and sound requests at every refresh of every replay ([`../replays/README.md`](../replays/README.md)).
- **Sound:** Classic (OPL3 as the original's driver played it, matching its register writes) and, under Remaster
  rules, Modern (General MIDI through a SoundFont, its effects placed by position), swappable while playing.
- **`vikings-remaster`:** the game in a window, keyboard and gamepad; a box at the start for the rules, and boxes
  that say what is missing.
- **Remaster rules** (ADR-0011): the window on the GPU renderer, zoom-out in three steps with the HUD laid over the
  view, text boxes that follow the view, no limit on entities or sprite pieces, a minimap with fog of war, ten
  seconds of rewind a level (a bar in the minimap), a quicksave with the cheat on, and a menu for the settings and
  the controls (keyboard and gamepad, rebindable; a stick walking and climbing as fast as it is pushed), kept in
  the profile.
- **The Modern visuals** (ADR-0005, ADR-0006): smooth movement between ticks; parallax, the SNES backdrops behind
  the levels and the village's scenes from the player's own ROM image; light, glow and particles answering to the
  animation an entity plays, the map's electricity and the turning colours, the Vikings' deaths by electricity and
  fire, teleporting, bombs and blasts; the screen's looks, Hyllian's CRT and the pixels rounded off by xBR.
- **Co-op online** (ADR-0012): up to three, one Viking each, by rollback; hosting and joining from the title's
  menu, a lobby the host starts, back to it from a game, joining a game under way.
- **Linux and Windows** (ADR-0013, ADR-0021), and `make release` for both.

## Next: extra sound effects (Modern sound)
- **What:** new effects on top of the original's: an electric buzz near the electric traps, the bow drawn and
  the arrow loosed, Olaf's shield up and down (a woosh), footsteps of Vikings and enemies, the slide when a
  running Viking turns, sword swings, the thump of a landing; steps and landings sound by surface (metal, rock,
  dirt, …). Placed by position as the original's effects are (ADR-0015). The Classic sound stays the original's,
  without them.
- **Sound requests:** the kinds report what happened (`SoundCue`: the entity, where, and the surface for steps
  and landings); the sound side decides what plays. The original's requests keep their effect numbers, the only
  ones the Classic sound plays.
- **Surfaces:** a material per metatile per world, a default per world, assigned with the level dump's collision
  map (`lv-export`).
- **Where the sounds come from:** still open. Free CC0 libraries are one way; but many of the game's effects are
  MIDI, so new ones could also be **MIDI sequences through the same SoundFont** (General MIDI has sound-effect
  programs and a percussion kit) and fit the game's own sound; or **synthesized from parameters** (noise, tones,
  envelopes, filters, as retro sound tools do). Files loaded from `gamedata/`, WAV first.

## Later
- **Verify the sound further** (the music and the effects are the least checked part). Checked so far: the Classic
  sound's register writes against the original's in DOSBox-X, the logo's tune note for note and ten minutes of the
  title's demos, music and effects together (`tools/oplcompare.py`,
  [`original/sound-driver.md`](original/sound-driver.md#checked-in-dosbox)). Still to do:
  - the **Modern sound's MIDI stream** against the original's driver 463 set up for General MIDI: DOSBox-X's MIDI
    capture (`CAPTURE_MidiEvent`, a control command for it in `tools/dosbox/control.cpp`), the game's
    configuration (`DATA.DAT` entry 0) set to the General MIDI driver, and a tap on what our synthesizer is sent;
    the notes, programs and volumes compared as the Classic sound's are (the SoundFont's sound itself has no
    reference);
  - the **sound in play**: a level played with keys (replays), not only the demos, so that more effects and their
    meeting with the music are covered, every world's tunes too;
  - the **Remaster's own**: the volumes as gains and the Classic effects' own chip under Remaster rules, against
    Vanilla's sound at the full volume (the music's notes the same, only louder or quieter).
- macOS.
