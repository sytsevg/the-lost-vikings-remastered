The Lost Vikings Remastered @VERSION@
==========================================

A remake of The Lost Vikings (Silicon & Synapse, 1993) on an engine of its own, which plays the original exactly
(Vanilla rules) or with zoom, a minimap, rewind, co-op online for up to three, new visuals and sound (Remaster
rules). It has none of the game in it: it plays from your own copy of the DOS release.

https://github.com/sytsevg/the-lost-vikings-remastered


Starting
--------

1. Put DATA.DAT and VIKINGS.EXE of your own copy of the DOS game in gamedata/original/ (the folder beside the
   program). The letter case doesn't matter.
2. Optional: your own SNES ROM of the game (*.sfc, the US release) in the same folder, for the backdrops behind
   the levels under Remaster rules.
3. Start vikings-remaster (vikings-remaster.exe on Windows). A box asks which rules to play by, and notes what
   Remaster would play without. If the game's files aren't found, a box says where they go.

The Modern sound's SoundFont, gamedata/GeneralUser-GS.sf2, is included (GeneralUser GS by S. Christian Collins,
https://www.schristiancollins.com; its licence is beside it). Without it Remaster plays the original's sound.

The game keeps your settings, the minimap and the quicksave in profiles/ beside the program. In logs/ it writes a
log of each run (the newest ten are kept), and the logs of a co-op game that went wrong: attach them to a bug
report. A log holds no paths or addresses, only the names given in the game. Nothing is written anywhere else.


What it needs
-------------

Linux:    x86-64, glibc 2.38 or later, a Wayland session (no X11) and a Vulkan driver. The box at the start needs
          zenity or kdialog; without either, start it with --rules vanilla or --rules remaster.
Windows:  Windows 10 or later, x86-64, and a Vulkan driver (the graphics card's own driver has it).


Controls
--------

Vanilla rules: the original's keys. Arrows move, Ctrl switches Viking, E, S, D and F are what each Viking can do,
Tab the inventory, Esc pauses, F10 quits. A gamepad works as the original's joystick.

Remaster rules (all of them can be changed in the menu):

                          Keyboard                  Gamepad
  Move, climb             arrows                    d-pad or left stick (pushed a little: walk slowly)
  Abilities               Space, D                  A, B
  Use item, act           E, S                      X, Y
  Previous/next Viking    Left Ctrl, Right Ctrl     LB, RB
  Inventory               Tab                       Back
  Rewind (hold)           Backspace                 left trigger
  Zoom out, in            -, = (or the wheel)       right stick up, down
  Minimap                 M                         right stick press
  Menu                    Escape                    Start


Co-op online (Remaster rules)
-----------------------------

One player hosts and the others join, each with a Viking of their own. Everyone needs the same release and the
same game files, and the host's port must be reachable from the others (a LAN, a forwarded port, or a VPN such as
Tailscale). In the title's menu: HOST GAME (a name, a port, and a level's password, or none for a new game) or
JOIN GAME (a name, and the host's IP:PORT). Everyone waits in the lobby until the host starts the game. Linux and
Windows play together.


Licence
-------

The game's code is MIT (LICENSE). The libraries in it have their own licences (THIRD-PARTY-NOTICES.md).
The Lost Vikings belongs to Blizzard Entertainment; this project isn't affiliated with or endorsed by them.
