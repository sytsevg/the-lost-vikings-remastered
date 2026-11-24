#pragma once

// Every animation the kinds play, by name: where its program is in the database of the world it is named in. The
// one place in the engine with these addresses (the Vikings' own, which are tables by Viking, are in
// viking_looks.hpp). A name is the kind that plays it and what it shows: anim::kGunnerWalks.
//
// What an effect of the Modern visuals would answer to (a bullet's burst, electricity striking) is named here too:
// docs/ROADMAP.md, M8.

#include <array>

#include "lvr/names.hpp"

namespace lvr::anim {

// --- Every enemy ---
inline constexpr Animation kEnemyIsHurt{0x1722};

// --- The spaceship's enemies ---
inline constexpr Animation kGunnerAppears{0x260e};
inline constexpr Animation kGunnerWalks{0x263f};
inline constexpr Animation kCloseGunnerWalks{0x2618};
inline constexpr Animation kGunnerWaits{0x2666};
inline constexpr Animation kGunnerShoots{0x266d};
inline constexpr Animation kGunnerFalls{0x2674};

inline constexpr Animation kBomberAppears{0x19aa};
inline constexpr Animation kBomberStrolls{0x19b4};
inline constexpr Animation kBomberRuns{0x19cf};
inline constexpr Animation kBomberThrows{0x19ee};

inline constexpr Animation kBombFlies{0xbeff};
inline constexpr Animation kBombBursts{0xbf0d};

inline constexpr Animation kRemainsFallsApart{0x75bd};
inline constexpr Animation kRemainsFallsApartInTheAir{0x7610};

inline constexpr Animation kTrooperAppears{0x28a7};
inline constexpr Animation kTrooperWalks{0x28b1};
inline constexpr Animation kTrooperWaits{0x28d9};
inline constexpr Animation kTrooperShoots{0x28e3};
inline constexpr Animation kTrooperVanishes{0x2903};
inline constexpr Animation kTrooperComesBack{0x291b};

inline constexpr Animation kDroneAppears{0x2a32};
inline constexpr Animation kDroneFlies{0x2a3c};

inline constexpr Animation kDroneShotFlies{0x3186};

// --- Machines, switches, doors, lifts ---
inline constexpr Animation kBarrierHumming{0x9558};
inline constexpr Animation kBarrierFading{0x959e};
inline constexpr Animation kBarrierFieldHumming{0x94ee};
inline constexpr Animation kBarrierFieldFading{0x9534};

inline constexpr Animation kDoorFrame{0xab7d};
inline constexpr Animation kDoorOpens{0xab90};
inline constexpr Animation kDoorStandsOpen{0xabb1};
inline constexpr Animation kDoorCloses{0xabbf};
inline constexpr Animation kDoorStandsClosed{0xabe0};

inline constexpr Animation kGunWaits{0x37bc};
inline constexpr Animation kGunFires{0x37c8};

inline constexpr Animation kButtonOut{0x77fe};
inline constexpr Animation kButtonGoesIn{0x780b};
inline constexpr Animation kButtonComesOut{0x7806};

inline constexpr Animation kLockSetUp{0x786f};

inline constexpr Animation kLiftPlatform{0x240e};
inline constexpr Animation kLiftOtherPlatform{0x2419};

inline constexpr Animation kTeleporterStands{0xa56f};

inline constexpr Animation kRubbleDust{0x796b};
inline constexpr Animation kRubbleFalls{0x7976};

inline constexpr Animation kSpringRests{0x2bf1};
inline constexpr Animation kSpringGivesWay{0x2bfa};

inline constexpr Animation kSwitchedCellsOpeningWaits{0x7a60};
inline constexpr Animation kSwitchedCellsBuildingWaits{0x7b25};

inline constexpr Animation kSinkingPlateRests{0x8f5f, database::kEgypt};
inline constexpr Animation kSinkingPlateSinks{0x8f71, database::kEgypt};

inline constexpr Animation kSwitchedBeamShines{0x94ee};
inline constexpr Animation kSwitchedBeamFades{0x9534};
inline constexpr Animation kSwitchedBeamShinesToo{0x9558};
inline constexpr Animation kSwitchedBeamFadesToo{0x959e};

inline constexpr Animation kFallingDropFalls{0x8891};
inline constexpr Animation kFallingDropSplashes{0x88a1};
inline constexpr Animation kFallingDropGone{0x88b0};

inline constexpr Animation kLightBridgeAppears{0x2af1};
inline constexpr Animation kLightBridgeShines{0x2afc};
inline constexpr Animation kLightBridgeSolid{0x2b0e};

inline constexpr Animation kGeneratorHums{0x8fb2};

// --- Items and bombs ---
inline constexpr Animation kItemSetUp{0x7f09};
inline constexpr Animation kOtherItemSetUp{0x7f14};

inline constexpr Animation kPlacedBombTicks{0x82c2};
inline constexpr Animation kPlacedBombBlowsUp{0x82d8};

// --- Weapons ---
inline constexpr Animation kSparkSmall{0x7560};
inline constexpr Animation kSparkBig{0x7571};
inline constexpr Animation kArrowFlies{0x6e02};
inline constexpr Animation kArrowBroken{0x6e13};
inline constexpr Animation kFireArrowFlies{0x6e2f};
inline constexpr Animation kFireArrowBroken{0x6e42};
inline constexpr Animation kBulletFlies{0x3831};
inline constexpr Animation kBulletBursts{0x3841};

// --- The level's start and end, and the ones to talk to ---
inline constexpr Animation kHintStands{0x8017};

inline constexpr Animation kJanitorStands{0x850c};

inline constexpr Animation kMechanicStands{0x86a0};

inline constexpr Animation kElderStands{0x7e4d};

inline constexpr Animation kPrisonerStands{0x89c7};

inline constexpr Animation kCavemanStands{0x7c37};

inline constexpr Animation kExitStands{0x87a7};

// --- The game-over scene ---
inline constexpr Animation kGameOverStands{0x3608};

inline constexpr Animation kBurningShipAppears{0x32e3};
inline constexpr Animation kBurningShipBurns{0x32fa};

inline constexpr Animation kLettersAppears{0x371f};

// --- Platforms ---
inline constexpr Animation kOneWayPlatformSwitchedLooks{0x31e8, database::kWacky};
inline constexpr Animation kOneWayPlatformLooks{0x31fa, database::kWacky};

// --- The prehistoric world ---
inline constexpr Animation kBrawlerAppears{0x1948, database::kPrehistoric};
inline constexpr Animation kBrawlerWalks{0x1951, database::kPrehistoric};
inline constexpr Animation kBrawlerStrikes{0x1964, database::kPrehistoric};

inline constexpr Animation kRollerAppears{0x1d75, database::kPrehistoric};
inline constexpr Animation kRollerRolls{0x1d7f, database::kPrehistoric};
inline constexpr Animation kRollerWalks{0x1d92, database::kPrehistoric};
inline constexpr Animation kRollerStrikes{0x1db1, database::kPrehistoric};

inline constexpr Animation kSpitterAppears{0x207e, database::kPrehistoric};
inline constexpr Animation kSpitterWalks{0x2085, database::kPrehistoric};
inline constexpr Animation kSpitterSpits{0x209a, database::kPrehistoric};
inline constexpr Animation kSpitterIsHurt{0x20c0, database::kPrehistoric};

inline constexpr Animation kSpitFlies{0x2135, database::kPrehistoric};
inline constexpr Animation kSpitBursts{0x214f, database::kPrehistoric};

inline constexpr Animation kFallingBridgeAppears{0x66d2, database::kPrehistoric};
inline constexpr Animation kFallingBridgeFalls{0x66dc, database::kPrehistoric};
inline constexpr Animation kFallingBridgeLies{0x6709, database::kPrehistoric};
inline constexpr Animation kFallingBridgeStands{0x671e, database::kPrehistoric};

inline constexpr Animation kGateAppears{0x6b5a, database::kPrehistoric};
inline constexpr Animation kGateOpens{0x6b79, database::kPrehistoric};
inline constexpr Animation kGateIsOpen{0x6b85, database::kPrehistoric};
inline constexpr Animation kGateShuts{0x6b91, database::kPrehistoric};
inline constexpr Animation kGateIsShut{0x6b9a, database::kPrehistoric};

inline constexpr Animation kFallingBlockLooks{0x2488, database::kPrehistoric};
inline constexpr Animation kFallingBlockOtherLooks{0x2471, database::kPrehistoric};

inline constexpr Animation kBubbleRises{0x7441, database::kPrehistoric};
inline constexpr Animation kBubbleBursts{0x744c, database::kPrehistoric};
inline constexpr Animation kBubbleMiddleRises{0x7472, database::kPrehistoric};
inline constexpr Animation kBubbleMiddleBursts{0x747f, database::kPrehistoric};
inline constexpr Animation kBubbleSmallRises{0x74a5, database::kPrehistoric};
inline constexpr Animation kBubbleSmallBursts{0x74b0, database::kPrehistoric};

inline constexpr Animation kFireballFlies{0x773b, database::kPrehistoric};
inline constexpr Animation kFireballBursts{0x774e, database::kPrehistoric};
inline constexpr Animation kFireballGivesOut{0x7759, database::kPrehistoric};
inline constexpr Animation kFireballDropFalls{0x78d8, database::kPrehistoric};
inline constexpr Animation kFireballDropSplashes{0x78eb, database::kPrehistoric};

inline constexpr Animation kArcingFireFlies{0x783e, database::kPrehistoric};
inline constexpr Animation kArcingFireGoesOut{0x7851, database::kPrehistoric};
inline constexpr Animation kArcingFireFades{0x785c, database::kPrehistoric};

inline constexpr Animation kHangingLedgeHangs{0x79ca, database::kPrehistoric};
inline constexpr Animation kHangingLedgeIsHit{0x79d6, database::kPrehistoric};

inline constexpr Animation kDeadlyPoolLooks{0x750e, database::kPrehistoric};
inline constexpr Animation kDeadlyPoolDrains{0x7550, database::kPrehistoric};

inline constexpr Animation kSwitchRelayAppears{0x7f44, database::kPrehistoric};
inline constexpr Animation kSwitchRelayWaits{0x7fab, database::kPrehistoric};
inline constexpr Animation kSwitchRelayGoes{0x7fc7, database::kPrehistoric};
inline constexpr Animation kSwitchRelayIsDone{0x8018, database::kPrehistoric};

inline constexpr Animation kBreakableWhole{0x7ec7, database::kPrehistoric};
inline constexpr Animation kBreakableCracked{0x7ed4, database::kPrehistoric};
inline constexpr Animation kBreakableNearlyBroken{0x7ed9, database::kPrehistoric};

// --- Egypt ---
inline constexpr Animation kHopperAppears{0x1fe8, database::kEgypt};
inline constexpr Animation kHopperRuns{0x1ff2, database::kEgypt};
inline constexpr Animation kHopperStrikes{0x2005, database::kEgypt};
inline constexpr Animation kHopperHops{0x2038, database::kEgypt};
inline constexpr Animation kHopperComesDown{0x203d, database::kEgypt};

inline constexpr Animation kStoneThrowerAppears{0x1a42, database::kEgypt};
inline constexpr Animation kStoneThrowerWalks{0x1a4c, database::kEgypt};
inline constexpr Animation kStoneThrowerRuns{0x1a67, database::kEgypt};
inline constexpr Animation kStoneThrowerThrows{0x1a86, database::kEgypt};

inline constexpr Animation kBallHangs{0x8bed, database::kEgypt};
inline constexpr Animation kBallFalls{0x8bfa, database::kEgypt};
inline constexpr Animation kBallIsHit{0x8c0d, database::kEgypt};

inline constexpr Animation kEgyptPushedBlockLooks{0x345a, database::kEgypt};

inline constexpr Animation kSwitchedCellAppears{0x8e32, database::kEgypt};
inline constexpr Animation kSwitchedCellGoesOn{0x8e3c, database::kEgypt};
inline constexpr Animation kSwitchedCellGoesOff{0x8e47, database::kEgypt};
inline constexpr Animation kSwitchedCellIsOff{0x8e4c, database::kEgypt};

inline constexpr Animation kTalkerStands{0x98a3, database::kEgypt};

inline constexpr Animation kDroppingPlatformLooks{0x310e, database::kEgypt};
inline constexpr Animation kDroppingPlatformBreaks{0x312b, database::kEgypt};

inline constexpr Animation kSpikedFloorLooks{0x39ce, database::kEgypt};
inline constexpr Animation kSpikedFloorFoldsAway{0x39dd, database::kEgypt};

inline constexpr Animation kSinkingBlockLooks{0x324b, database::kEgypt};

inline constexpr Animation kDroppingBlockLooks{0x32f1, database::kEgypt};
inline constexpr Animation kDroppingBlockCrumbles{0x330e, database::kEgypt};
inline constexpr Animation kDroppingBlockFalls{0x3305, database::kEgypt};

inline constexpr Animation kPotDwellerAppears{0x218d, database::kEgypt};
inline constexpr Animation kPotDwellerKeepsDown{0x2197, database::kEgypt};
inline constexpr Animation kPotDwellerComesUp{0x21a6, database::kEgypt};

inline constexpr Animation kDialTurns{0x8b36, database::kEgypt};
inline constexpr Animation kDialPictures{0x8b1e, database::kEgypt};  // four of them, six bytes apart

inline constexpr Animation kBoulderLooks{0x2a92, database::kEgypt};
inline constexpr Animation kBoulderShatters{0x2aae, database::kEgypt};

inline constexpr Animation kSwitchedDropMakerLooks{0x828f, database::kEgypt};

inline constexpr Animation kDropperHangs{0x8eb7, database::kEgypt};
inline constexpr Animation kDropperBreaks{0x8ec4, database::kEgypt};

inline constexpr Animation kRainedBlockLooks{0x302a, database::kEgypt};

inline constexpr Animation kMummyAppears{0x1c9c, database::kEgypt};
inline constexpr Animation kMummyWalks{0x1ca5, database::kEgypt};
inline constexpr Animation kMummyFalls{0x1cd0, database::kEgypt};
inline constexpr Animation kMummyGrabs{0x1cd5, database::kEgypt};

inline constexpr Animation kDropForms{0x8d78, database::kEgypt};
inline constexpr Animation kDropFalls{0x8d8b, database::kEgypt};
inline constexpr Animation kDropSplashes{0x8d90, database::kEgypt};

// --- The factory ---
inline constexpr Animation kPlateAppears{0x2b06, database::kFactory};
inline constexpr Animation kPlateIsUp{0x2b0d, database::kFactory};
inline constexpr Animation kPlateIsDown{0x2b12, database::kFactory};

inline constexpr Animation kPressLooks{0x3377, database::kFactory};
inline constexpr Animation kPressSlammerLooks{0x3393, database::kFactory};

inline constexpr Animation kCrumblingTileLooks{0x396a, database::kFactory};
inline constexpr Animation kCrumblingTileCrumbles{0x3975, database::kFactory};

inline constexpr Animation kHatchAppears{0x3744, database::kFactory};
inline constexpr Animation kHatchOpens{0x3755, database::kFactory};
inline constexpr Animation kHatchIsOpen{0x3798, database::kFactory};
inline constexpr Animation kHatchShuts{0x37a6, database::kFactory};
inline constexpr Animation kHatchIsShut{0x37e9, database::kFactory};

inline constexpr Animation kPistonLooks{0x3ba2, database::kFactory};
inline constexpr Animation kPistonUpwardsLooks{0x3b74, database::kFactory};

inline constexpr Animation kDroppedBlockLooks{0x3d47, database::kFactory};

inline constexpr Animation kBlastLarge{0xaf55, database::kFactory};
inline constexpr Animation kBlastSmall{0xafe7, database::kFactory};

inline constexpr Animation kWalkerAppears{0x253a, database::kFactory};
inline constexpr Animation kWalkerWalks{0x2544, database::kFactory};
inline constexpr Animation kWalkerIsHurt{0x254f, database::kFactory};

inline constexpr Animation kShooterAppears{0x275a, database::kFactory};
inline constexpr Animation kShooterWalks{0x2761, database::kFactory};
inline constexpr Animation kShooterShoots{0x2777, database::kFactory};

inline constexpr Animation kTurretWaits{0x3c46, database::kFactory};
inline constexpr Animation kTurretShoots{0x3c51, database::kFactory};

inline constexpr Animation kShotFlies{0x28f7, database::kFactory};
inline constexpr Animation kShotBursts{0x2902, database::kFactory};
inline constexpr Animation kShotDrops{0x2969, database::kFactory};
inline constexpr Animation kShotLands{0x2974, database::kFactory};

inline constexpr Animation kFlyerAppears{0x2876, database::kFactory};
inline constexpr Animation kFlyerFlies{0x2880, database::kFactory};

inline constexpr Animation kMagnetAppears{0x30ef, database::kFactory};
inline constexpr Animation kMagnetIsOff{0x3120, database::kFactory};
inline constexpr Animation kMagnetIsOn{0x312c, database::kFactory};

inline constexpr Animation kCrateLooks{0x32b0, database::kFactory};

inline constexpr Animation kMagneticBlockLooks{0x363d, database::kFactory};
inline constexpr Animation kMagneticBlockWaitingLooks{0x3654, database::kFactory};

inline constexpr Animation kTrolleyLooks{0x3670, database::kFactory};

inline constexpr Animation kFireJetLooks{0x38c5, database::kFactory};
inline constexpr Animation kFireJetLights{0x38d2, database::kFactory};
inline constexpr Animation kFireJetBurns{0x38d3, database::kFactory};
inline constexpr Animation kFireJetOut{0x38e5, database::kFactory};

// --- The wacky world ---
inline constexpr Animation kPlugStands{0xa95a, database::kWacky};
inline constexpr Animation kPlugGoes{0xa96a, database::kWacky};

inline constexpr Animation kFlameFlies{0xaa0e, database::kWacky};
inline constexpr Animation kFlameBursts{0xaa27, database::kWacky};
inline constexpr Animation kFlameGivesOut{0xaa32, database::kWacky};
inline constexpr Animation kFlameShotFlies{0xaaf1, database::kWacky};
inline constexpr Animation kFlameShotBursts{0xab02, database::kWacky};

inline constexpr Animation kWackyPushedBlockLooks{0x1d61, database::kWacky};
inline constexpr Animation kPushedBlockFixedLooks{0x323f, database::kWacky};

inline constexpr Animation kFanAppears{0x2eaa, database::kWacky};
inline constexpr Animation kFanBlows{0x2eb8, database::kWacky};
inline constexpr Animation kFanStill{0x2ee7, database::kWacky};
inline constexpr Animation kFanUpwardsAppears{0x2e43, database::kWacky};
inline constexpr Animation kFanUpwardsBlows{0x2e5e, database::kWacky};
inline constexpr Animation kFanUpwardsStill{0x2e95, database::kWacky};
inline constexpr Animation kFanUpwardsStillFlipped{0x2e80, database::kWacky};

inline constexpr Animation kThrowerAppears{0x2659, database::kWacky};
inline constexpr Animation kThrowerWalks{0x2663, database::kWacky};
inline constexpr Animation kThrowerThrows{0x267e, database::kWacky};
inline constexpr Animation kThrowerFalls{0x269e, database::kWacky};

inline constexpr Animation kThrownFlies{0x270d, database::kWacky};
inline constexpr Animation kThrownBursts{0x271d, database::kWacky};

inline constexpr Animation kClownAppears{0x34e5, database::kWacky};
inline constexpr Animation kClownRuns{0x34e9, database::kWacky};
inline constexpr Animation kClownStrikes{0x3512, database::kWacky};
inline constexpr Animation kClownAims{0x352c, database::kWacky};
inline constexpr Animation kClownHasShot{0x354d, database::kWacky};
inline constexpr Animation kClownFalls{0x3568, database::kWacky};
inline constexpr Animation kClownIsHurt{0x3579, database::kWacky};
inline constexpr Animation kClownBows{0x3586, database::kWacky};
inline constexpr Animation kClownStands{0x359f, database::kWacky};

inline constexpr Animation kChargerWalks{0x2a70, database::kWacky};  // the scenes' only

// --- Tomator's level ---
inline constexpr Animation kTomatorStands{0xb8ed};  // an animation his parts share with him
inline constexpr Animation kTomatorAims{0xb913};
inline constexpr Animation kTomatorReels{0xb91e};
inline constexpr Animation kTomatorWalks{0xb900};
inline constexpr Animation kTomatorAppears{0xb955};
inline constexpr Animation kTomatorIsBlownOut{0xb961};
inline constexpr Animation kTomatorIsKnocked{0xb966};
inline constexpr Animation kTomatorComesBack{0xb96d};
inline constexpr Animation kTomatorVanishes{0xb976};

inline constexpr Animation kForceFieldStands{0xba01};
inline constexpr Animation kForceFieldGoes{0xba68};

inline constexpr Animation kTomatorShotArcs{0xbccc};
inline constexpr Animation kTomatorShotBursts{0xbcdd};
inline constexpr Animation kTomatorShotFlies{0xbce4};
inline constexpr Animation kTomatorShotFizzles{0xbcf4};

inline constexpr Animation kAirLockShut{0xbd53};
inline constexpr Animation kAirLockOpens{0xbd77};

inline constexpr Animation kElectricBarLooks{0xac3a};

inline constexpr Animation kSlidingWallLooks{0xabef};

// --- The scenes outside the levels ---
inline constexpr Animation kLogoShows{0x8ad0};

inline constexpr Animation kTitleStands{0x8a0e};
inline constexpr Animation kTitlePasswordStands{0x8a15};
inline constexpr Animation kTitlePointer{0x8a23};

inline constexpr Animation kBobbingShipFlies{0x9ead};

inline constexpr Animation kShipsTailLooks{0x9edd};

inline constexpr Animation kShipsRiderLooks{0x9f25};

inline constexpr Animation kGalaxyLightFirst{0x9332};
inline constexpr Animation kGalaxyLightOwn{0x9339};
inline constexpr Animation kGalaxyLightFlies{0x9361};

inline constexpr Animation kSceneTrooperAppears{0x306c};
inline constexpr Animation kSceneTrooperWalks{0x3094};
inline constexpr Animation kSceneTrooperFires{0x30b8};

inline constexpr Animation kDroppedInLooks{0x8468};

inline constexpr Animation kAlienShipFlies{0x9703};
inline constexpr Animation kAlienShipBeams{0x97ad};
inline constexpr Animation kAlienShipBeamOff{0x979e};

inline constexpr Animation kBeamLightLooks{0x982e};

// --- The Vikings (their tables by Viking are in viking_looks.hpp) ---
inline constexpr Animation kOlafStandsShieldUp{0x5885};
inline constexpr Animation kOlafWalksShieldUp{0x592f};
inline constexpr Animation kOlafStopsShieldUp{0x597f};
inline constexpr Animation kOlafGlides{0x5a5e};
inline constexpr Animation kOlafGlidesFast{0x5a4d};
inline constexpr Animation kOlafSteppedOffShieldUp{0x5a1e};
inline constexpr Animation kBaleogLooksAround{0x409a};  // what he does when left standing: one of two, at random
inline constexpr Animation kBaleogFidgets{0x40e5};
inline constexpr Animation kBaleogDrawsBow{0x412f};
inline constexpr Animation kBaleogLooses{0x4150};
inline constexpr Animation kErikCharges{0x4d40};
inline constexpr Animation kErikHitsHisHead{0x4d75};
inline constexpr Animation kErikBouncesBack{0x4d7f};
inline constexpr Animation kErikBouncesAgain{0x4d90};
inline constexpr Animation kErikGetsUp{0x4d97};
inline constexpr Animation kErikStopsCharging{0x4dc6};
inline constexpr Animation kOntoLadder{0x6cf2};  // all three: the step sideways onto a ladder

// One for each Viking: Erik's, Baleog's, Olaf's (Viking::by_who). Named in the world that has them.
using ByViking = std::array<Animation, 3>;
inline constexpr ByViking kVikingImpaledFromBelow{
    {{0x5ad0, database::kPrehistoric}, {0x5aee, database::kPrehistoric}, {0x5b09, database::kPrehistoric}}};
inline constexpr ByViking kVikingImpaledFromAbove{
    {{0x5b24, database::kPrehistoric}, {0x5b3f, database::kPrehistoric}, {0x5b5a, database::kPrehistoric}}};
inline constexpr ByViking kVikingDissolves{
    {{0x6e68, database::kEgypt}, {0x6e77, database::kEgypt}, {0x6e84, database::kEgypt}}};
inline constexpr ByViking kVikingMummified{
    {{0x71c9, database::kEgypt}, {0x71dd, database::kEgypt}, {0x71f1, database::kEgypt}}};
inline constexpr ByViking kVikingBeamedAway{{{0x69ff}, {0x6a13}, {0x6a27}}};
inline constexpr ByViking kVikingBeamedIn{{{0x6a57}, {0x6a84}, {0x6ab8}}};
inline constexpr ByViking kVikingBurnsUp{
    {{0x59e2, database::kPrehistoric}, {0x59f6, database::kPrehistoric}, {0x5a0c, database::kPrehistoric}}};
inline constexpr ByViking kVikingGoesUnder{
    {{0x5895, database::kPrehistoric}, {0x58b1, database::kPrehistoric}, {0x58cd, database::kPrehistoric}}};
inline constexpr ByViking kVikingSinks{
    {{0x72ac, database::kEgypt}, {0x72c4, database::kEgypt}, {0x72dc, database::kEgypt}}};
inline constexpr ByViking kVikingShockedFromBelow{
    {{0x77dd, database::kFactory}, {0x77e3, database::kFactory}, {0x77e9, database::kFactory}}};
inline constexpr ByViking kVikingShockedFromAbove{
    {{0x77ef, database::kFactory}, {0x77f5, database::kFactory}, {0x77fb, database::kFactory}}};
inline constexpr ByViking kVikingPumps{
    {{0x7414, database::kWacky}, {0x7427, database::kWacky}, {0x743a, database::kWacky}}};
inline constexpr ByViking kVikingLetsAirOut{
    {{0x74b0, database::kWacky}, {0x74c1, database::kWacky}, {0x74d2, database::kWacky}}};
inline constexpr ByViking kVikingWobbles{
    {{0x744d, database::kWacky}, {0x746e, database::kWacky}, {0x748f, database::kWacky}}};
inline constexpr ByViking kVikingAtControls{
    {{0x7939, database::kFactory}, {0x7944, database::kFactory}, {0x794f, database::kFactory}}};
inline constexpr ByViking kVikingLedWalks{
    {{0x56ab, database::kScenes}, {0x4a68, database::kScenes}, {0x62b3, database::kScenes}}};
inline constexpr ByViking kVikingLedStands{
    {{0x56fe, database::kScenes}, {0x4a91, database::kScenes}, {0x6306, database::kScenes}}};
inline constexpr ByViking kVikingCheers{
    {{0x6db1, database::kScenes}, {0x6dcd, database::kScenes}, {0x6de1, database::kScenes}}};
inline constexpr Animation kOlafCheersOnTheShip{0x6df0, database::kScenes};
inline constexpr Animation kVikingPops{0x74e3, database::kWacky};  // all three

// --- Tables by an index ---
// An item's picture, by its number less one (item:: in names.hpp).
inline constexpr std::array<Animation, 18> kItemPictures{{{0x7f22},
                                                          {0x7f45},
                                                          {0x7f27},
                                                          {0x7f4a},
                                                          {0x7f2c},
                                                          {0x7f36},
                                                          {0x7f4f},
                                                          {0x7f31},
                                                          {0x7f1d},
                                                          {0x7f59},
                                                          {0x7f40},
                                                          {0x7f3b},
                                                          {0x7f54},
                                                          {0x7f5e},
                                                          {0x7f63},
                                                          {0x7f68},
                                                          {0x7f6d},
                                                          {0x7f72}}};

// The levers: the usual two kinds, and the third, which looks different and has no animation of going.
inline constexpr Animation kLeverStandsOn{0x76fe};
inline constexpr Animation kLeverStandsOff{0x770b};
inline constexpr Animation kLeverGoesOn{0x7706};
inline constexpr Animation kLeverGoesOff{0x7713};
inline constexpr Animation kOtherLeverOn{0x7718};
inline constexpr Animation kOtherLeverOff{0x7726};

// A lock's colour, by its kind.
inline constexpr Animation kLockFirstColour{0x787a};
inline constexpr Animation kLockSecondColour{0x787f};
inline constexpr Animation kLockThirdColour{0x7884};

// The walls that go when their switches say so: Egypt's gate, the wacky world's wall.
inline constexpr Animation kEgyptGateStands{0x8fcf, database::kEgypt};
inline constexpr Animation kEgyptGateGoes{0x8fe6, database::kEgypt};
inline constexpr Animation kSwitchedWallStands{0xa8fc, database::kWacky};
inline constexpr Animation kSwitchedWallGoes{0xa911, database::kWacky};

// --- More tables by an index, and the last of the single ones ---
// The game-over scene: the lightning at each Viking's place (Erik's, Baleog's, Olaf's), and him after it.
inline constexpr std::array<Animation, 3> kGameOverStrikes{{{0x363b}, {0x360f}, {0x3667}}};
inline constexpr std::array<Animation, 3> kGameOverStruck{{{0x365e}, {0x3632}, {0x368b}}};
// ... and each of them standing.
inline constexpr std::array<Animation, 3> kGameOverVikingStands{{{0x3211}, {0x31c9}, {0x3259}}};
// The burning ship's parts, by argument.
inline constexpr std::array<Animation, 4> kBurningShipParts{{{0x32ea}, {0x32ee}, {0x32f2}, {0x32f6}}};
// The letters' parts, by argument.
inline constexpr std::array<Animation, 4> kGameOverLetters{{{0x3726}, {0x372e}, {0x3736}, {0x373e}}};
// The title's pointer: at each choice, and at each letter of the password.
inline constexpr std::array<Animation, 3> kTitlePointerAtChoice{{{0x8a31}, {0x8a3e}, {0x8a4b}}};
inline constexpr std::array<Animation, 4> kTitlePointerAtLetter{{{0x8a58}, {0x8a65}, {0x8a72}, {0x8a7f}}};
inline constexpr Animation kShipPartLooks{0x8394};
inline constexpr Animation kOtherShipPartLooks{0x83f4};
// The scene's three lights, by argument.
inline constexpr std::array<Animation, 3> kSceneLights{{{0x9685}, {0x9694}, {0x96a3}}};
inline constexpr Animation kNightThingLooks{0x98fa};
// ... and the other kind's, by argument.
inline constexpr std::array<Animation, 2> kNightOtherLooks{{{0x991f}, {0x9914}}};
// The villagers, by argument (0: none).
inline constexpr std::array<Animation, 3> kVillagerLooks{{{0x9ca2}, {0x9cb6}, {0x9cca}}};
inline constexpr std::array<Animation, 3> kOtherVillagerLooks{{{0x9d11}, {0}, {0x9cfd}}};
// The charger's animations: the wacky world's (which the scenes outside the levels use too), and the spaceship's,
// whose charger is the same but for the scenes' part.
struct ChargerLooks {
    Animation appears, stands, lands, jumps, comes_down, winds_up, dashes, stops;
};
inline constexpr ChargerLooks kWackyChargerLooks{
    {0x2a07, database::kWacky}, {0x2a11, database::kWacky}, {0x2a21, database::kWacky}, {0x2a32, database::kWacky},
    {0x2a3d, database::kWacky}, {0x2a48, database::kWacky}, {0x2a58, database::kWacky}, {0x2a65, database::kWacky}};
inline constexpr ChargerLooks kSpaceshipChargerLooks{{0x2e29, database::kSpaceship}, {0x2e33, database::kSpaceship},
                                                     {0x2e43, database::kSpaceship}, {0x2e54, database::kSpaceship},
                                                     {0x2e5f, database::kSpaceship}, {0x2e6a, database::kSpaceship},
                                                     {0x2e7a, database::kSpaceship}, {0x2e87, database::kSpaceship}};
// Each of Tomator's three parts: as it appears, blown out, knocked, coming back, vanishing.
inline constexpr std::array<std::array<Animation, 5>, 3> kTomatorPartLooks{{
    {{{0xb97f}, {0xb98b}, {0xb996}, {0xb99b}, {0xb9a4}}},
    {{{0xb9ad}, {0xb9b9}, {0xb9be}, {0xb9c3}, {0xb9cc}}},
    {{{0xb9d5}, {0xb9e1}, {0xb9e6}, {0xb9ef}, {0xb9f8}}},
}};
inline constexpr Animation kTallWallSignSetUp{0x88f5, database::kEgypt};
inline constexpr Animation kWallSignSetUp{0x8910, database::kEgypt};
// A wall sign's picture, by its argument less one.
inline constexpr std::array<Animation, 3> kTallWallSignPictures{
    {{0x8901, database::kEgypt}, {0x8906, database::kEgypt}, {0x890b, database::kEgypt}}};
inline constexpr std::array<Animation, 2> kWallSignPictures{{{0x891b, database::kEgypt}, {0x8920, database::kEgypt}}};
inline constexpr Animation kDialFacingLeft{0x8b0e, database::kEgypt};
inline constexpr Animation kDialFacingRight{0x8aff, database::kEgypt};
inline constexpr Animation kRollerOnTheShip{0x1d6f, database::kScenes};

}  // namespace lvr::anim
