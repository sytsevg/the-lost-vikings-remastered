#pragma once

// The kinds whose behaviour is written (ADR-0017): each a class derived from Entity, grouped by family in a file of
// its own (conversations.cpp, ...). Until the kinds have names they go by the original's object type number.

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "lvr/dialogues.hpp"
#include "lvr/entity.hpp"

namespace lvr {

namespace kind {
inline constexpr std::uint16_t kBaleog = 0x00;
inline constexpr std::uint16_t kErik = 0x01;
inline constexpr std::uint16_t kOlaf = 0x02;
inline constexpr std::uint16_t kShield = 0x03;  // Olaf's
inline constexpr std::uint16_t kGun = 0x04;     // on a wall, shooting
inline constexpr std::uint16_t kBullet = 0x05;
inline constexpr std::uint16_t kSword = 0x06;  // Baleog's, while it swings
inline constexpr std::uint16_t kArrow = 0x07;
inline constexpr std::uint16_t kFireArrow = 0x08;
inline constexpr std::uint16_t kSpark = 0x0c;             // where a blow lands
inline constexpr std::uint16_t kRemains = 0x0d;           // of an enemy that was killed
inline constexpr std::uint16_t kOpeningWall = 0x11;       // cells cleared one after the other when its switches come on
inline constexpr std::uint16_t kBuildingFloor = 0x12;     // cells made solid the same way
inline constexpr std::uint16_t kLever = 0x13;             // flips its switches each time it is worked
inline constexpr std::uint16_t kLeverOfSecondSet = 0x14;  // the same, for the level's second set of switches
inline constexpr std::uint16_t kOtherLever = 0x15;        // the same as the first, looking different
inline constexpr std::uint16_t kButton = 0x16;            // its switches on for a moment
inline constexpr std::uint16_t kButtonForGood = 0x17;     // once and for all
inline constexpr std::uint16_t kButtonForAWhile = 0x18;
inline constexpr std::uint16_t kSmartBomb = 0x21;  // an item used: the bomb put down (the name is the item's)
inline constexpr std::uint16_t kLitBomb = 0x22;    // an item used: the flash that strikes every enemy
inline constexpr std::uint16_t kLock = 0x1a;       // three locks, for three keys
inline constexpr std::uint16_t kSecondLock = 0x1b;
inline constexpr std::uint16_t kThirdLock = 0x1c;
inline constexpr std::uint16_t kItem = 0x1d;
inline constexpr std::uint16_t kHintOnAsking = 0x1f;  // a hint box that only speaks when a Viking asks
inline constexpr std::uint16_t kHint = 0x20;          // one that also speaks the first time a Viking touches it
inline constexpr std::uint16_t kRubble = 0x10;        // where a wall breaks
inline constexpr std::uint16_t kSinkingPlate = 0x39;  // gives way under a block
inline constexpr std::uint16_t kExit = 0x40;
inline constexpr std::uint16_t kWeightlessness = 0x45;  // a place without gravity
inline constexpr std::uint16_t kBarrier = 0x46;
inline constexpr std::uint16_t kElectricField = 0x47;  // a barrier that blocks nothing and spares no one
inline constexpr std::uint16_t kTeleporter = 0x49;
inline constexpr std::uint16_t kBombableWall = 0x57;
inline constexpr std::uint16_t kUnseenBarrier = 0x59;
inline constexpr std::uint16_t kLevelStart = 0x48;
inline constexpr std::uint16_t kLift = 0x5e;  // three kinds are the same lift; one looks different
inline constexpr std::uint16_t kLift5F = 0x5f;
inline constexpr std::uint16_t kBomber = 0x67;
inline constexpr std::uint16_t kCloseGunner = 0x77;  // a gunner that only shoots from close by
inline constexpr std::uint16_t kGunner = 0x78;
inline constexpr std::uint16_t kSpring = 0x86;
inline constexpr std::uint16_t kLiftA8 = 0xa8;
inline constexpr std::uint16_t kOtherLift = 0xad;
inline constexpr std::uint16_t kBomb = 0xd0;          // a bomber's
inline constexpr std::uint16_t kDoor = 0x4f;          // opens when a Viking is near
inline constexpr std::uint16_t kSwitchedDoor = 0x50;  // open while its switches are on
inline constexpr std::uint16_t kBlast = 0xc7;         // the burst where something is blown up or wrecked
// More of the kinds that several worlds' databases have, or that the files of several worlds name.
inline constexpr std::uint16_t kFallingDrop = 0x19;
inline constexpr std::uint16_t kOtherItem = 0x1e;  // an item, placed under another number
inline constexpr std::uint16_t kCaveman = 0x2d;    // the prehistoric world's two to talk to
inline constexpr std::uint16_t kElder = 0x2e;
inline constexpr std::uint16_t kJanitor = 0x3c;
inline constexpr std::uint16_t kMechanic = 0x3d;
inline constexpr std::uint16_t kRemark = 0x3f;
inline constexpr std::uint16_t kPrisoner = 0x41;
inline constexpr std::uint16_t kLevelEnd = 0x43;
inline constexpr std::uint16_t kGenerator = 0x44;
inline constexpr std::uint16_t kSwitchedBeam = 0x53;
inline constexpr std::uint16_t kOtherSwitchedBeam = 0x54;
inline constexpr std::uint16_t kSwitchSequence = 0x55;
inline constexpr std::uint16_t kShipStop = 0x56;
inline constexpr std::uint16_t kDrone = 0x75;
inline constexpr std::uint16_t kDroneShot = 0x76;
inline constexpr std::uint16_t kNearTrooper = 0x79;  // a trooper that only shoots from near
inline constexpr std::uint16_t kTrooper = 0x7a;
inline constexpr std::uint16_t kRisingPlatform =
    0x7e;  // the wacky world's platforms that go one way only; the spaceship has the first too
inline constexpr std::uint16_t kLightBridge = 0x81;
inline constexpr std::uint16_t kCharger = 0x84;  // the wacky world's, which the spaceship and the scenes have too
inline constexpr std::uint16_t kSwitchedRisingPlatform = 0xac;
}  // namespace kind

// The other worlds' kinds. A number means another kind in another world's database, so each world has its own
// names; a class that takes several numbers has a name for each (what sets them apart is in the class).
// The prehistoric world's kinds (its database, 450).
namespace prehistoric_kind {
inline constexpr std::uint16_t kFallingBridge = 0x09;
inline constexpr std::uint16_t kGate = 0x0e;
inline constexpr std::uint16_t kGateThatStaysOpen = 0x0f;
inline constexpr std::uint16_t kSmallBubble = 0x23;
inline constexpr std::uint16_t kMiddleBubble = 0x24;
inline constexpr std::uint16_t kBubble = 0x25;
inline constexpr std::uint16_t kDeadlyPool = 0x26;
inline constexpr std::uint16_t kFireShooter = 0x27;
inline constexpr std::uint16_t kArcingFireShooter = 0x28;
inline constexpr std::uint16_t kFireball = 0x29;
inline constexpr std::uint16_t kArcingFire = 0x2a;
inline constexpr std::uint16_t kFireDrop = 0x2b;
inline constexpr std::uint16_t kHangingLedge = 0x2c;
inline constexpr std::uint16_t kBreakable = 0x2f;
inline constexpr std::uint16_t kSwitchRelay = 0x30;
inline constexpr std::uint16_t kFireShooterInAWall = 0x3e;
inline constexpr std::uint16_t kSender = 0x4a;
inline constexpr std::uint16_t kBrawler = 0x60;
inline constexpr std::uint16_t kFastBrawler = 0x61;
inline constexpr std::uint16_t kRoller = 0x62;
inline constexpr std::uint16_t kSpitter = 0x63;
inline constexpr std::uint16_t kSpit = 0x64;
inline constexpr std::uint16_t kFallingBlock = 0x65;
inline constexpr std::uint16_t kQuake = 0x66;
inline constexpr std::uint16_t kOtherFallingBlock = 0xb9;
}  // namespace prehistoric_kind

// Egypt's (451).
namespace egypt_kind {
inline constexpr std::uint16_t kDropMaker = 0x0a;
inline constexpr std::uint16_t kSwitchedDropMaker = 0x0b;
inline constexpr std::uint16_t kTallWallSign = 0x31;
inline constexpr std::uint16_t kWallSign = 0x32;
inline constexpr std::uint16_t kDial = 0x33;
inline constexpr std::uint16_t kBall = 0x34;
inline constexpr std::uint16_t kDripPlace = 0x35;
inline constexpr std::uint16_t kDrop = 0x36;
inline constexpr std::uint16_t kSwitchedCell = 0x37;
inline constexpr std::uint16_t kDropper = 0x38;
inline constexpr std::uint16_t kArrowMark = 0x3a;
inline constexpr std::uint16_t kGate = 0x3b;
inline constexpr std::uint16_t kTalker = 0x42;
inline constexpr std::uint16_t kBoulder = 0x5d;
inline constexpr std::uint16_t kStoneThrower = 0x67;
inline constexpr std::uint16_t kMummy = 0x68;
inline constexpr std::uint16_t kHopper = 0x69;
inline constexpr std::uint16_t kPotDweller = 0x6a;
inline constexpr std::uint16_t kBlockRain = 0x6b;
inline constexpr std::uint16_t kRainedBlock = 0x6c;
inline constexpr std::uint16_t kDroppingPlatform = 0x6d;
inline constexpr std::uint16_t kSinkingBlock = 0x71;
inline constexpr std::uint16_t kPushedBlock = 0x72;
inline constexpr std::uint16_t kDroppingBlock = 0x73;
inline constexpr std::uint16_t kSpikedFloor = 0x74;
}  // namespace egypt_kind

// The factory's (452).
namespace factory_kind {
inline constexpr std::uint16_t kWalker = 0x87;
inline constexpr std::uint16_t kShooter = 0x88;
inline constexpr std::uint16_t kTurret = 0x89;
inline constexpr std::uint16_t kShot = 0x8a;
inline constexpr std::uint16_t kFlyer = 0x8b;
inline constexpr std::uint16_t kDroppedShot = 0x8c;
inline constexpr std::uint16_t kConveyor = 0x8d;
inline constexpr std::uint16_t kConveyor8E = 0x8e;
inline constexpr std::uint16_t kNothing8F = 0x8f;
inline constexpr std::uint16_t kNothing90 = 0x90;
inline constexpr std::uint16_t kMagnet = 0x91;
inline constexpr std::uint16_t kCraneControls = 0x92;  // what a Viking stands at to drive a crane
inline constexpr std::uint16_t kPlate = 0x93;
inline constexpr std::uint16_t kCrate = 0x94;
inline constexpr std::uint16_t kBlinkingLight = 0x95;
inline constexpr std::uint16_t kSlammingPress = 0x96;
inline constexpr std::uint16_t kPress = 0x97;
inline constexpr std::uint16_t kMagneticBlock = 0x9a;
inline constexpr std::uint16_t kTrolley = 0x9b;
inline constexpr std::uint16_t kMagneticBlockWaitingForSwitch = 0x9c;
inline constexpr std::uint16_t kHatch = 0x9d;
inline constexpr std::uint16_t kTarget = 0x9e;
inline constexpr std::uint16_t kFireJet = 0x9f;
inline constexpr std::uint16_t kShotAwayTile = 0xa6;
inline constexpr std::uint16_t kCrumblingTile = 0xa7;
inline constexpr std::uint16_t kPiston = 0xa9;
inline constexpr std::uint16_t kUpwardPiston = 0xaa;
inline constexpr std::uint16_t kDroppedBlock = 0xab;
inline constexpr std::uint16_t kSlammingPressAE = 0xae;
inline constexpr std::uint16_t kPressAF = 0xaf;
inline constexpr std::uint16_t kCrumblingTileB6 = 0xb6;
inline constexpr std::uint16_t kShotAwayTileB8 = 0xb8;
inline constexpr std::uint16_t kMachine = 0xba;
inline constexpr std::uint16_t kDispenser = 0xbb;
inline constexpr std::uint16_t kVendor = 0xbc;
inline constexpr std::uint16_t kItemPlace = 0xbd;
inline constexpr std::uint16_t kGlowingFurnace = 0xbe;
inline constexpr std::uint16_t kFurnace = 0xbf;
inline constexpr std::uint16_t kBlast = 0xc7;
}  // namespace factory_kind

// The wacky world's (453).
namespace wacky_kind {
inline constexpr std::uint16_t kPump =
    0x95;  // what a Viking works to be blown up like a balloon (the factory's blinking light there)
inline constexpr std::uint16_t kPushedBlock = 0x5a;
inline constexpr std::uint16_t kThrower = 0x82;
inline constexpr std::uint16_t kThrown = 0x83;
inline constexpr std::uint16_t kCharger = 0x84;
inline constexpr std::uint16_t kClown = 0x85;
inline constexpr std::uint16_t kFan = 0x98;
inline constexpr std::uint16_t kUpwardFan = 0x99;
inline constexpr std::uint16_t kPushedBlockB5 = 0xb5;
inline constexpr std::uint16_t kPushedBlockWaitingForSwitch = 0xb7;
inline constexpr std::uint16_t kSwitchedCells = 0xc0;
inline constexpr std::uint16_t kSwitchedCellsOfSecondSet = 0xc1;
inline constexpr std::uint16_t kArrowTarget = 0xc2;
inline constexpr std::uint16_t kWall = 0xc3;
inline constexpr std::uint16_t kPlug = 0xc4;
inline constexpr std::uint16_t kFlame = 0xc5;
inline constexpr std::uint16_t kBurner = 0xc6;
inline constexpr std::uint16_t kFlameShot = 0xc8;
}  // namespace wacky_kind

// The kinds of Tomator's level (in the spaceship's database).
namespace tomator_kind {
inline constexpr std::uint16_t kSlidingWall = 0x51;
inline constexpr std::uint16_t kElectricBar = 0x58;
inline constexpr std::uint16_t kTomator = 0xc9;
inline constexpr std::uint16_t kTomatorsFirstPart = 0xca;
inline constexpr std::uint16_t kTomatorsSecondPart = 0xcb;
inline constexpr std::uint16_t kTomatorsThirdPart = 0xcc;
inline constexpr std::uint16_t kForceField = 0xcd;
inline constexpr std::uint16_t kTomatorShot = 0xce;
inline constexpr std::uint16_t kAirLock = 0xcf;
}  // namespace tomator_kind

// The kinds of the scenes outside the levels (454).
namespace scene_kind {
inline constexpr std::uint16_t kCurtainCall = 0x4b;
inline constexpr std::uint16_t kShipPart = 0x4c;
inline constexpr std::uint16_t kAlarm = 0x4d;
inline constexpr std::uint16_t kShipPart4E = 0x4e;
inline constexpr std::uint16_t kDroppedIn = 0x52;
inline constexpr std::uint16_t kTrooper = 0x79;
inline constexpr std::uint16_t kTrooper7A = 0x7a;
inline constexpr std::uint16_t kTitle = 0xd1;
inline constexpr std::uint16_t kLogo = 0xd8;
inline constexpr std::uint16_t kGalaxy = 0xda;
inline constexpr std::uint16_t kGalaxyLight = 0xdb;
inline constexpr std::uint16_t kAbduction = 0xdc;
inline constexpr std::uint16_t kTakenViking = 0xdd;
inline constexpr std::uint16_t kAlienShip = 0xde;
inline constexpr std::uint16_t kBeamLight = 0xdf;
inline constexpr std::uint16_t kNightThing = 0xe0;
inline constexpr std::uint16_t kNightThingE1 = 0xe1;
inline constexpr std::uint16_t kVillageVoices = 0xe2;
inline constexpr std::uint16_t kVillageThing = 0xe3;
inline constexpr std::uint16_t kHomecoming = 0xe4;
inline constexpr std::uint16_t kVillager = 0xe5;
inline constexpr std::uint16_t kVillagerE6 = 0xe6;
inline constexpr std::uint16_t kStarfield = 0xe7;
inline constexpr std::uint16_t kBobbingShip = 0xe8;
inline constexpr std::uint16_t kShipsTail = 0xe9;
inline constexpr std::uint16_t kShipsRider = 0xea;
}  // namespace scene_kind

// The game-over scene's, which every database has.
namespace game_over_kind {
inline constexpr std::uint16_t kGameOver = 0xd2;
inline constexpr std::uint16_t kBaleogsFigure = 0xd3;
inline constexpr std::uint16_t kEriksFigure = 0xd4;
inline constexpr std::uint16_t kOlafsFigure = 0xd5;
inline constexpr std::uint16_t kBurningShip = 0xd6;
inline constexpr std::uint16_t kLetters = 0xd7;
}  // namespace game_over_kind

// What one entity tells another (Entity::message): the original's numbers. The one told answers at its next turn,
// each kind in its own way; what it has no answer to it forgets.
namespace message {
// Harm. With it (message_value): how much health it costs, and kFromTheRight if it came from someone facing left.
inline constexpr std::uint16_t kDrowning = 0x01;          // to a Viking: water came down on him
inline constexpr std::uint16_t kSwordBlow = 0x02;         // to an enemy, or a thing that can be struck
inline constexpr std::uint16_t kArrowBlow = 0x03;         // the same, by an arrow
inline constexpr std::uint16_t kHeadButt = 0x04;          // the same, by Erik running into it
inline constexpr std::uint16_t kStruck = 0x06;            // to a Viking: an enemy struck him
inline constexpr std::uint16_t kShot = 0x07;              // to a Viking: something shot, thrown or dropped hit him
inline constexpr std::uint16_t kBurned = 0x08;            // to a Viking: fire
inline constexpr std::uint16_t kDissolved = 0x09;         // to a Viking: Egypt's drops (he dies of it there)
inline constexpr std::uint16_t kBlast = 0x0a;             // to anyone: a bomb went off
inline constexpr std::uint16_t kMummified = 0x0b;         // to a Viking: a mummy has him
inline constexpr std::uint16_t kElectrocuted = 0x0c;      // to a Viking: he touched an electric barrier
inline constexpr std::uint16_t kImpaledFromAbove = 0x1a;  // to a Viking: a piston's spikes came down on him
inline constexpr std::uint16_t kImpaled = 0x1b;           // to a Viking: spikes under him, or coming up
inline constexpr std::uint16_t kScorched = 0x1e;          // to a Viking: a furnace
// Being moved. With it: how fast.
inline constexpr std::uint16_t kBlown = 0x13;         // to a Viking: a puff of air pushes him along
inline constexpr std::uint16_t kSquashed = 0x15;      // to a Viking: something came down on him
inline constexpr std::uint16_t kDropped = 0x16;       // to whoever stood on something: it is gone, or threw him off
inline constexpr std::uint16_t kHeldInTheAir = 0x18;  // to Olaf: a magnet has his shield
inline constexpr std::uint16_t kLetGo = 0x19;         // to Olaf: and lets it go
inline constexpr std::uint16_t kPushed = 0x1c;        // to what a Viking pushes against
inline constexpr std::uint16_t kCrushed = 0x1d;       // to a Viking: pushed against a wall by something
inline constexpr std::uint16_t kThrown = 0x1f;        // to a Viking: thrown up or down
inline constexpr std::uint16_t kBlownBack = 0x29;     // to Olaf: a blow on his shield pushes him back
// Dealings.
inline constexpr std::uint16_t kAsked = 0x14;         // to a thing: a Viking (message_value: his slot) acts on it
inline constexpr std::uint16_t kConversation = 0x17;  // to a Viking: a conversation is about to start
inline constexpr std::uint16_t kBeamed = 0x20;        // to a Viking (the spaceship's): he is beamed to World::beam
inline constexpr std::uint16_t kVanish = 0x21;        // to anyone: gone without a trace
// What a scene's director tells its cast (the cutscenes, and the ship's curtain call).
inline constexpr std::uint16_t kLedWalk = 0x22;
inline constexpr std::uint16_t kLedStand = 0x23;
inline constexpr std::uint16_t kLedCheer = 0x24;    // to a Viking: cheer
inline constexpr std::uint16_t kLedAttack = 0x25;   // do what you do to a Viking, where you stand
inline constexpr std::uint16_t kLedTurn = 0x26;     // turn round and walk
inline constexpr std::uint16_t kLedLeave = 0x27;    // go, for good
inline constexpr std::uint16_t kLedColours = 0x28;  // bring your own colours (in a scene), or go
}  // namespace message

// A new entity of that kind, or nullptr if its behaviour isn't written yet (the world then makes one that stands
// still).
std::unique_ptr<Entity> make_kind(std::uint16_t kind, int database = database::kSpaceship);

// conversations.cpp
std::unique_ptr<Entity> make_conversation(std::uint16_t kind, int database);
std::unique_ptr<Entity> make_level_start();
std::unique_ptr<Entity> make_hint(std::uint16_t kind);
// items.cpp
std::unique_ptr<Entity> make_item();
// The two bombs a Viking sets off: the one that is put down and blows up, and the one that strikes every enemy.
std::unique_ptr<Entity> make_bomb(std::uint16_t kind);
// Where the teleporter with that argument leads: a Viking is put there.
struct TeleporterDestination {
    int x = 0;
    int y = 0;
};
std::optional<TeleporterDestination> teleporter_destination(std::uint16_t argument);
// The kinds are written by family, a file each (walkers.cpp, blocks.cpp ...). Each family ends with its list:
// which class a kind's number is, and where the number means that: in every world, or in one world's own list
// (some numbers mean another kind in another world).
enum class Home : std::uint8_t { kAnywhere, kPrehistoric, kEgypt, kFactory, kWacky, kScenes };
struct KindOf {
    Home home;
    std::uint16_t kind;
    std::unique_ptr<Entity> (*make)();
};
template <class Kind>
std::unique_ptr<Entity> make() {
    return make_entity<Kind>();
}
std::span<const KindOf> walkers();
std::span<const KindOf> chargers();
std::span<const KindOf> shooters();
std::span<const KindOf> bombers();
std::span<const KindOf> shots();
std::span<const KindOf> blocks();
std::span<const KindOf> drops();
std::span<const KindOf> switched();
std::span<const KindOf> machines();
std::span<const KindOf> switches();
std::span<const KindOf> crane();
std::span<const KindOf> carriers();      // platforms.cpp: the lift and the spring
std::span<const KindOf> talkers();       // conversations.cpp: Egypt's one to talk to
std::span<const KindOf> title_scenes();  // title.cpp
std::span<const KindOf> ship_scenes();   // the scenes outside the levels (the cutscenes' database, 454)
std::span<const KindOf> village_scenes();
// The kind with that number in that home's list, or nullptr if the list has none.
std::unique_ptr<Entity> make_of(Home home, std::uint16_t kind);
std::unique_ptr<Entity> make_tomator(std::uint16_t kind);
// gameover.cpp: the game-over scene's kinds, which every world's database has
std::unique_ptr<Entity> make_game_over(std::uint16_t kind);
std::unique_ptr<Entity> make_platform(std::uint16_t kind);

// Says a row of a conversation (dialogues.hpp): the box in the speaker's colour, held until a key is pressed.
// `host` and `viking`: for the rows of a talk between a Viking and someone he met.
void speak(World& world, const talk::Said& said, const Entity* host = nullptr, int viking = kNobody);
void speak(World& world, std::span<const talk::Said> rows, const Entity* host = nullptr, int viking = kNobody);
// The wacky world's platforms that go one way only (AC, 7E).
std::unique_ptr<Entity> make_one_way_platform();
// vikings.cpp
std::unique_ptr<Entity> make_viking(std::uint16_t kind);
std::unique_ptr<Entity> make_shield();
// Whether Olaf holds his shield over his head.
bool olaf_holds_shield_up(const World& world);
// Whether he can't hold it at all now (climbing, pushing, at a crane's controls, dead).
bool olaf_shield_away(const World& world);
// weapons.cpp
std::unique_ptr<Entity> make_weapon(std::uint16_t kind);

}  // namespace lvr
