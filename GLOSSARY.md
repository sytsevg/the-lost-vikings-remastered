# The Lost Vikings, remade

A clean-room remake of the game's engine, played under the original's rules or the Remaster's, alone or online
with up to three people.

## Rules and time

**Vanilla rules**:
The original's game exactly as it played, checked against the original picture by picture.
_Avoid_: classic mode, original mode

**Remaster rules**:
The game with what the remake adds: zoom, the minimap, rewind, the menu, the Modern visuals and sound, and going
online.
_Avoid_: modern mode, QoL mode

**Step**:
One tick of the game: every player's input for it in, the world moved on once. A step is three refreshes.
_Avoid_: frame (online it is also called one), turn

**Refresh**:
One picture of the display, about 59.7 a second.
_Avoid_: frame

## Places

**Place**:
Where the game as a whole is for the person at the machine: Offline, Connecting, Lobby or Online. One place at a
time; each move to another is a transition.
_Avoid_: state, screen, mode, phase

**Offline**:
The place where the game is played alone: the title, its levels and its scenes. The only place under Vanilla
rules.
_Avoid_: title (the title is only part of it), single player

**Connecting**:
The place where a guest waits for the host to answer, before the lobby or a game under way.
_Avoid_: joining (that is joining under way)

**Lobby**:
The place where everyone waits before a game online: who is there, their round trips, and what the game starts
with.
_Avoid_: waiting room

**Online**:
The place where a game is played online, every machine running it.
_Avoid_: co-op mode, multiplayer, match (as a name for the place)

## People online

**Host**:
The machine everyone connects to; it alone drives the game (the title, passwords, scenes, giving up).
_Avoid_: server

**Guest**:
Any machine in the game that isn't the host.
_Avoid_: client, peer, the others

**Player**:
A person in the game, playing a Viking or watching one.
_Avoid_: user

**Viewer**:
A player who watches a Viking someone else plays, waiting in that Viking's line to play him.
_Avoid_: spectator, observer

**Line**:
A Viking's viewers in the order they came; the first plays him when his player moves on.
_Avoid_: queue

**Joining under way**:
A guest coming into a game already being played, rather than from the lobby.
_Avoid_: hot join, connecting
