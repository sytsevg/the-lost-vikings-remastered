#pragma once

// The game's numbers that more than one file needs, each with one name: the keys, the worlds' databases, the scenes,
// the Vikings' slots. (The kinds, the messages and the tile types have their names in kinds.hpp and level.hpp.)
//
// They are constants in namespaces, not enum classes: they are the original's numbers, kept in its 16-bit fields,
// read from its files and compared with them, and an enum class would want a cast at each of those places.

#include <cstdint>

namespace lvr {

// The keyboard word (World::held, World::pressed): a bit per key the game knows (docs/original/input.md).
namespace key {
inline constexpr std::uint16_t kNext = 0x0010;      // the next Viking
inline constexpr std::uint16_t kPrevious = 0x0020;  // the previous one
inline constexpr std::uint16_t kE = 0x0040;         // use the item
inline constexpr std::uint16_t kS = 0x0080;         // act on what is there
inline constexpr std::uint16_t kRight = 0x0100;
inline constexpr std::uint16_t kLeft = 0x0200;
inline constexpr std::uint16_t kDown = 0x0400;
inline constexpr std::uint16_t kUp = 0x0800;
inline constexpr std::uint16_t kEnter = 0x1000;      // also: pause
inline constexpr std::uint16_t kInventory = 0x2000;  // Tab
inline constexpr std::uint16_t kD = 0x4000;          // each Viking's second ability
inline constexpr std::uint16_t kF = 0x8000;          // each Viking's first ability (also Space)
inline constexpr std::uint16_t kSOrE = 0x00c0;
inline constexpr std::uint16_t kFOrD = 0xc000;
inline constexpr std::uint16_t kAction = 0xc0c0;  // F, D, S or E: what answers a line of text
}  // namespace key

// The object databases, a world each: their entries in DATA.DAT. A world's kinds, scripts and animation programs
// are in its database; World::database says which one a scene uses.
namespace database {
inline constexpr int kSpaceship = 449;
inline constexpr int kPrehistoric = 450;
inline constexpr int kEgypt = 451;
inline constexpr int kFactory = 452;
inline constexpr int kWacky = 453;
inline constexpr int kScenes = 454;  // the scenes outside the levels
}  // namespace database

// An animation: where its program is in the database of the world it is named in (the spaceship's, unless said).
// A world plays it from its own database: the same program lies elsewhere there, and where is found as a level
// is loaded (lvr/places.hpp).
struct Animation {
    std::uint16_t place = 0;
    int home = database::kSpaceship;

    constexpr bool operator==(const Animation&) const = default;
};

// The levels, 1 to 37 (World::level; a level's scene is one less), and what is special about some of them.
namespace level {
inline constexpr int kFirst = 1;
inline constexpr int kCountsWithValues = 15;  // its kinds count with World::values
inline constexpr int kWayHome = 36;           // the Vikings find the way home
inline constexpr int kLast = 37;              // Tomator's
// The last level of each world but the last: after it comes the galaxy, and the next world.
constexpr bool ends_a_world(int level) noexcept {
    return level == 4 || level == 11 || level == 17 || level == 25 || level == 33;
}
// A world's first level, but not the game's: the Vikings fall in from above, and have a word about it.
constexpr bool begins_a_later_world(int level) noexcept {
    return ends_a_world(level - 1);
}
}  // namespace level

// The scenes, as the original numbers them (World::scene): 0-36 are the 37 levels in order.
namespace scene {
inline constexpr int kLastLevel = 36;  // Tomator's
inline constexpr int kGameOver = 37;
inline constexpr int kTitle = 38;
inline constexpr int kLogo = 39;
inline constexpr int kSecondLogo = 40;  // Silicon & Synapse's
inline constexpr int kGalaxy = 41;
inline constexpr int kTitleCopy = 42;  // (the title's, as the original has it twice)
inline constexpr int kIntro = 43;      // the village, and the night the Vikings are taken
inline constexpr int kVillageDemo = 44;
inline constexpr int kStarfield = 45;
inline constexpr int kEnding = 46;  // the village the Vikings come home to
inline constexpr int kShip = 47;    // the ship's curtain call
}  // namespace scene

// The Vikings' slots in a world: Erik first. (Their kinds are numbered otherwise: kind::kBaleog is 0.)
namespace slot {
inline constexpr int kErik = 0;
inline constexpr int kBaleog = 1;
inline constexpr int kOlaf = 2;
}  // namespace slot

// No entity: where a slot is asked for and there is none.
inline constexpr int kNobody = -1;

// What an entity is to the others' touch tests that ask for one kind of thing (Entity::hit_class; touching()).
namespace hit_class {
inline constexpr std::uint16_t kViking = 1;  // a Viking; also Olaf's shield held before him
inline constexpr std::uint16_t kEnemy = 2;
inline constexpr std::uint16_t kShieldOverhead = 6;  // Olaf's shield held up
inline constexpr std::uint16_t kLock = 7;            // the three locks, by their key (item::kKey + 1 ...)
inline constexpr std::uint16_t kSecondLock = 8;
inline constexpr std::uint16_t kThirdLock = 9;
inline constexpr std::uint16_t kBlast = 0x0a;         // a bomb's blast
inline constexpr std::uint16_t kTaker = 0x0b;         // what takes a thing a level asks for
inline constexpr std::uint16_t kAirStream = 0x0c;     // weightlessness that is on; Tomator's air lock
inline constexpr std::uint16_t kInTheWay = 0x0d;      // what a sender waits for to be gone
inline constexpr std::uint16_t kTomatorsBomb = 0x0e;  // what blows him over when he is thrown onto it
}  // namespace hit_class

// The bits of an entity's hit mask (Entity::hit_mask): what it is to the others' touch tests. A test asks for one
// or more of them (touching_any).
namespace hit {
inline constexpr std::uint16_t kHurtByArrow = 0x0001;
inline constexpr std::uint16_t kStopsArrow = 0x0002;      // and other weapons: a wall, a shut gate, an armoured enemy
inline constexpr std::uint16_t kViking = 0x0004;          // what shots and falling things look for
inline constexpr std::uint16_t kShield = 0x0008;          // Olaf's, held before him: it stops what flies at it
inline constexpr std::uint16_t kShieldOverhead = 0x0010;  // and held up: it stops what falls on it
inline constexpr std::uint16_t kPushable = 0x0020;        // a Viking walking into it pushes it
inline constexpr std::uint16_t kThing = 0x0040;           // a Viking can act on it
inline constexpr std::uint16_t kEnemy = 0x0080;           // Erik's charge strikes it
inline constexpr std::uint16_t kHurtBySword = 0x0100;
inline constexpr std::uint16_t kBombable = 0x0200;       // a bomb put down beside it blows it up
inline constexpr std::uint16_t kVikingInField = 0x0400;  // what barriers, fields and bars look for in a Viking
inline constexpr std::uint16_t kWeight = 0x0800;         // it presses a plate down; a magnet lifts it
inline constexpr std::uint16_t kHurtByFire = 0x1000;     // by a burning arrow
inline constexpr std::uint16_t kStopsFire = 0x2000;
inline constexpr std::uint16_t kToldOfSword = 0x4000;  // a sword's swing is told to it, without hurting it
}  // namespace hit

// The sound effects, by the original's numbers (World::sound). The names are what the kinds that play them called
// them, gathered here: by what makes the sound, not by listening to it. Where one word had several numbers the
// later ones carry theirs; numbers nobody had named are still bare where they are played.
namespace effect {
inline constexpr std::uint8_t kNothingToUse = 0x03;
inline constexpr std::uint8_t kHeavyLanding = 0x06;  // a ledge or a ball landing; a Viking's deadly fall
inline constexpr std::uint8_t kShot = 0x07;
inline constexpr std::uint8_t kDrowned = 0x09;
inline constexpr std::uint8_t kSplash = 0x0a;
inline constexpr std::uint8_t kImpaled = 0x0b;
inline constexpr std::uint8_t kSquashed = 0x0c;
inline constexpr std::uint8_t kSpiked = 0x0e;
inline constexpr std::uint8_t kSinking = 0x0f;
inline constexpr std::uint8_t kMummified = 0x10;
inline constexpr std::uint8_t kItemUsed = 0x11;
inline constexpr std::uint8_t kLandedInScene = 0x13;
inline constexpr std::uint8_t kEaten = 0x14;
inline constexpr std::uint8_t kArrowLoosed = 0x1b;
inline constexpr std::uint8_t kArrowBreaks = 0x1c;
inline constexpr std::uint8_t kBurst = 0x1d;
inline constexpr std::uint8_t kTaken = 0x1e;
inline constexpr std::uint8_t kLandedHard = 0x1f;
inline constexpr std::uint8_t kVikingHurt = 0x20;
inline constexpr std::uint8_t kEnemyHurt = 0x21;
inline constexpr std::uint8_t kTeleportedIn = 0x22;
inline constexpr std::uint8_t kTeleportedAway = 0x23;
inline constexpr std::uint8_t kHeadButt = 0x24;
inline constexpr std::uint8_t kDoor = 0x25;
inline constexpr std::uint8_t kPopped = 0x27;
inline constexpr std::uint8_t kSwitch = 0x28;
inline constexpr std::uint8_t kPumped = 0x2a;
inline constexpr std::uint8_t kLifted = 0x2c;
inline constexpr std::uint8_t kSprung = 0x2d;
inline constexpr std::uint8_t kTalk = 0x2f;
inline constexpr std::uint8_t kShot30 = 0x30;
inline constexpr std::uint8_t kRumble = 0x32;
inline constexpr std::uint8_t kShot33 = 0x33;
inline constexpr std::uint8_t kThud = 0x36;
inline constexpr std::uint8_t kDirge = 0x3a;
inline constexpr std::uint8_t kThud3C = 0x3c;
inline constexpr std::uint8_t kBackOnHisFeet = 0x44;
inline constexpr std::uint8_t kCreak = 0x46;
inline constexpr std::uint8_t kGoingUp = 0x47;
inline constexpr std::uint8_t kGoingDown = 0x48;
inline constexpr std::uint8_t kScrape = 0x49;
inline constexpr std::uint8_t kWhoosh = 0x4a;
inline constexpr std::uint8_t kRoar = 0x4c;
inline constexpr std::uint8_t kShot4D = 0x4d;
inline constexpr std::uint8_t kAlarm = 0x4e;
inline constexpr std::uint8_t kPowerDown = 0x4f;
inline constexpr std::uint8_t kSpit = 0x52;
inline constexpr std::uint8_t kRumble53 = 0x53;
inline constexpr std::uint8_t kThrow = 0x54;
inline constexpr std::uint8_t kRumble56 = 0x56;
inline constexpr std::uint8_t kMotor = 0x57;
inline constexpr std::uint8_t kWorks = 0x58;
inline constexpr std::uint8_t kSplat = 0x5a;
inline constexpr std::uint8_t kFallingIn = 0x5d;
inline constexpr std::uint8_t kStopped = 0x5e;
// Named by where they are played, not by how they sound.
inline constexpr std::uint8_t kMenuOpens = 0x00;
inline constexpr std::uint8_t kMenuMoves = 0x01;
inline constexpr std::uint8_t kFlashBomb = 0x12;
inline constexpr std::uint8_t kTargetBreaks = 0x15;
inline constexpr std::uint8_t kMagnetHums = 0x16;
inline constexpr std::uint8_t kPoolDrains = 0x17;
inline constexpr std::uint8_t kClownStrikes3 = 0x18;
inline constexpr std::uint8_t kClownStrikes1 = 0x19;
inline constexpr std::uint8_t kClownStrikes2 = 0x1a;
inline constexpr std::uint8_t kBlockLands = 0x2b;
inline constexpr std::uint8_t kPotDwellerThrows = 0x31;
inline constexpr std::uint8_t kDispenserWorks = 0x34;
inline constexpr std::uint8_t kFurnaceGlows = 0x35;
inline constexpr std::uint8_t kDropLands = 0x37;
inline constexpr std::uint8_t kBallIsHit = 0x38;
inline constexpr std::uint8_t kStarfieldSound = 0x3b;
inline constexpr std::uint8_t kTitlePassword = 0x3d;
inline constexpr std::uint8_t kTitleChosen = 0x3e;
inline constexpr std::uint8_t kAbductionBeam = 0x3f;
inline constexpr std::uint8_t kAbductionShip = 0x40;
inline constexpr std::uint8_t kPressSlams = 0x4b;
inline constexpr std::uint8_t kPistonGoes = 0x59;
inline constexpr std::uint8_t kBurnerFires = 0x5c;
}  // namespace effect

// What a Viking carries (World::items; an item's placement has its number as argument). The names are from what
// using one does (Viking::use_item); the four foods and the six things a level asks for differ only in looks.
namespace item {
inline constexpr std::uint16_t kFood1 = 0x01;  // two points of health
inline constexpr std::uint16_t kFood2 = 0x02;
inline constexpr std::uint16_t kBomb = 0x03;  // put down where he stands
inline constexpr std::uint16_t kFood4 = 0x04;
inline constexpr std::uint16_t kBigFood = 0x05;  // four points
inline constexpr std::uint16_t kKey = 0x06;      // the three keys, each for the lock of its kind
inline constexpr std::uint16_t kSecondKey = 0x07;
inline constexpr std::uint16_t kThirdKey = 0x08;
inline constexpr std::uint16_t kShield = 0x09;       // a point of health on top of his own
inline constexpr std::uint16_t kSmartBomb = 0x0a;    // strikes every enemy in view
inline constexpr std::uint16_t kFireArrows = 0x0b;   // Baleog's arrows burn from then on
inline constexpr std::uint16_t kBoots = 0x0c;        // his feet stay down where there is no gravity
inline constexpr std::uint16_t kFirstWanted = 0x0d;  // 0x0d-0x12: things a level asks for, handed to what wants them
inline constexpr std::uint16_t kLastWanted = 0x12;
// With an item's number, as the argument of the kind that is an item lying about: it falls to the ground first.
inline constexpr std::uint16_t kFalling = 0x8000;
}  // namespace item

inline constexpr std::uint16_t kFullHealth = 6;  // a Viking's three points of health, kept doubled

// A box's anchor: where on the box its tail is, pointing at whoever speaks. The box sits the other way from the
// speaker: with its tail below at the left, it is up and to the right of him.
namespace tail {
inline constexpr int kBelowMiddle = 0;
inline constexpr int kBelowLeft = 1;
inline constexpr int kBelowRight = 2;
inline constexpr int kAboveMiddle = 3;
inline constexpr int kAboveLeft = 4;
inline constexpr int kAboveRight = 5;
inline constexpr int kNone = 6;  // no tail to a speaker
}  // namespace tail

// The colour each speaker's lines are written in.
namespace text_colour {
inline constexpr std::uint16_t kErik = 0x0012;
inline constexpr std::uint16_t kBaleog = 0x01e3;
inline constexpr std::uint16_t kOlaf = 0x110d;
inline constexpr std::uint16_t kOthers = 0x7c00;  // what someone other than a Viking says
constexpr std::uint16_t of(int viking) noexcept {
    return viking == slot::kBaleog ? kBaleog : (viking == slot::kOlaf ? kOlaf : kErik);
}
}  // namespace text_colour

// The halves of a 16-bit number that holds two things, as many kinds' arguments do, and its top bit.
inline constexpr unsigned kLowByte = 0x00ffU;
inline constexpr unsigned kHighByte = 0xff00U;
inline constexpr unsigned kTopBit = 0x8000U;

// Several of a set of bits together: bits(hit::kEnemy, hit::kHurtByArrow).
template <typename... Bits>
constexpr std::uint16_t bits(Bits... each) noexcept {
    return static_cast<std::uint16_t>((0U | ... | unsigned{each}));
}

// A number as the original keeps it: in 16 bits, wrapping round.
constexpr std::int16_t wrapped(int value) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

// A speed in pixels a tick, as the 256ths of a pixel that speeds and moves are kept in: px(2.5) is 0x0280.
consteval std::int16_t px(double pixels) noexcept {
    return static_cast<std::int16_t>(pixels * 256.0);
}

}  // namespace lvr
