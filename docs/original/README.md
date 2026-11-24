# The original's engine

The clean-room record of how the original game behaves. These documents are the **spec**: code is written from them, not from decompiled source ([`../decisions.md`](../decisions.md), ADR-0001).

## Method
1. Read the code in **`VIKINGS.EXE`** (static disassembly). Use the scriptable DOSBox ([`../dosbox.md`](../dosbox.md))
   for what only shows at run time, and to check findings.
   Tools, recipes and the address book of everything identified so far: [`static-analysis.md`](static-analysis.md)
   (`tools/disasm.py`).
2. Write the rule down here, with how it was found (addresses, test inputs).
3. Implement it, and lock it down with a replay.

## Each document should state
- **Status:** hypothesis / observed / compared with the game
- **Evidence:** how we know
- **Rule:** exact integer values, in the order they are applied

## The documents
- [`static-analysis.md`](static-analysis.md): how to read the executables; segments, recipes, known routines and variables
- [`timing.md`](timing.md): 19.90 ticks per second (3 refreshes of 59.71 Hz Mode X), 3 rendered sub-frames per tick (confirmed in DOSBox)
- [`input.md`](input.md): the input word, default keys, hot keys, demo recordings (observed)
- [`game-flow.md`](game-flow.md): start-up, copy protection, the 48 level/scene slots, demos, passwords (observed)
- [`vikings.md`](vikings.md): Erik, Baleog, Olaf: their keys and abilities, running, jumping, gravity, landing hard (observed, compared by lockstep); collision in [`terrain.md`](terrain.md), the items in [`menus.md`](menus.md)
- [`script-vm.md`](script-vm.md): the object scripts: execution model, operands, all 216 opcodes, RNG (observed; lengths verified on all data)
- [`objects.md`](objects.md): spawning from records, sprite pieces, removing, the level start, objects streaming with the camera (observed)
- [`animation.md`](animation.md): the animation programs that move the sprite pieces, and the speed step (observed)
- [`hits.md`](hits.md): the hit opcodes, their two passes per tick, the object tests (observed; terrain tests pending)
- [`terrain.md`](terrain.md): the tile map and its types, slopes, the tile tests, the terrain hits, movement (observed)
- [`dialogue.md`](dialogue.md): the 390 texts in `VIKINGS.EXE`, the dialogue queue, how it pauses the game (observed)
- [`camera.md`](camera.md): the camera's limits, start, how it follows the current Viking in thirds, the strips (observed)
- [`drawing.md`](drawing.md): what is drawn in which order: tiles, sprite pieces (modes, frames, packed colours), foreground tiles, the dialogue's text cells, the HUD (observed)
- [`game-loop.md`](game-loop.md): one tick in order, the state requests, the level start, the dialogue player, demo playback, golden replays (observed)
- [`sound.md`](sound.md): what the game asks of the sound driver: the effect slots, the music actions, Alt+S/M (observed)
- [`menus.md`](menus.md): the pause menu (give up) and the inventory screen (items moved between Vikings, the trash can), the HUD's items and picker (observed)
- The random-number generator is in [`script-vm.md`](script-vm.md#random-numbers-00002312)
