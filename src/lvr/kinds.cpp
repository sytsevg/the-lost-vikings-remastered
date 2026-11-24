// Which class a kind's number is, in which world: the one place that knows all the makers.
#include "lvr/kinds.hpp"

#include <cstdint>
#include <memory>
#include <span>

#include "lvr/names.hpp"

namespace lvr {

std::unique_ptr<Entity> make_of(Home home, std::uint16_t kind) {
    for (const auto family :
         {walkers(), chargers(), shooters(), bombers(), shots(), blocks(), drops(), switched(), machines(), switches(),
          crane(), carriers(), talkers(), title_scenes(), ship_scenes(), village_scenes()}) {
        for (const KindOf& row : family) {
            if (row.home == home && row.kind == kind) {
                return row.make();
            }
        }
    }
    return nullptr;
}

std::unique_ptr<Entity> make_kind(std::uint16_t kind, int database) {
    if (kind == kind::kRisingPlatform &&
        database == database::kSpaceship) {  // the spaceship has the wacky world's rising platform
        return make_one_way_platform();
    }
    // A world's own kinds first: some numbers mean another kind in another world.
    if (database == database::kPrehistoric) {
        if (kind == kind::kCaveman || kind == kind::kElder) {
            return make_conversation(kind, database);
        }
        if (auto made = make_of(Home::kPrehistoric, kind)) {
            return made;
        }
    }
    if (database == database::kScenes) {
        if (auto made = make_of(Home::kScenes, kind)) {
            return made;
        }
        if (kind == kind::kCharger) {  // and the wacky world's charger
            return make_of(Home::kWacky, kind);
        }
        if (kind == egypt_kind::kStoneThrower) {  // the scenes have Egypt's thrower under this number, not the
                                                  // spaceship's bomber
            return make_of(Home::kEgypt, kind);
        }
    }
    if (database == database::kSpaceship &&
        kind == kind::kCharger) {  // the wacky world's charger, without the scenes' part
        return make_of(Home::kWacky, kind);
    }
    if (database == database::kWacky) {
        if (kind == kind::kSwitchedRisingPlatform || kind == kind::kRisingPlatform) {
            return make_one_way_platform();
        }
        if (auto made = make_of(Home::kWacky, kind)) {
            return made;
        }
    }
    if (database == database::kFactory) {
        if (auto made = make_of(Home::kFactory, kind)) {
            return made;
        }
    }
    if (database == database::kEgypt) {
        if (auto made = make_of(Home::kEgypt, kind)) {
            return made;
        }
    }
    if (auto made = make_of(Home::kAnywhere, kind)) {
        return made;
    }
    switch (kind) {
        case kind::kBaleog:
        case kind::kErik:
        case kind::kOlaf:
            return make_viking(kind);
        case kind::kShield:
            return make_shield();
        case kind::kBullet:
        case kind::kSword:
        case kind::kArrow:
        case kind::kFireArrow:
        case kind::kSpark:
            return make_weapon(kind);
        case kind::kSmartBomb:
        case kind::kLitBomb:
            return make_bomb(kind);
        case kind::kItem:
        case kind::kOtherItem:
            return make_item();
        case kind::kHintOnAsking:
        case kind::kHint:
            return make_hint(kind);
        case kind::kLevelStart:
            return make_level_start();
        case kind::kExit:
        case kind::kLevelEnd:
        case kind::kShipStop:
        case kind::kRemark:
        case kind::kPrisoner:
        case kind::kJanitor:
        case kind::kMechanic:
            if (auto made = make_conversation(kind, database)) {
                return made;
            }
            break;
        default:
            break;
    }
    // The families that know their own kinds.
    if (database == database::kSpaceship) {
        if (auto made = make_tomator(kind)) {
            return made;
        }
    }
    if (auto made = make_game_over(kind)) {
        return made;
    }
    if (auto platform = make_platform(kind)) {
        return platform;
    }
    // Kinds one world shares with another are written with the first that has them. (Not the prehistoric
    // world's breakable thing, 2F: a number of its own there.)
    if (kind != prehistoric_kind::kBreakable) {
        if (auto made = make_of(Home::kPrehistoric, kind)) {
            return made;
        }
    }
    // (Egypt's thrower is Egypt's and the scenes': the spaceship's 67 is its bomber. Its dropping block, 73,
    // is its own too.)
    if (kind != egypt_kind::kStoneThrower && kind != egypt_kind::kDroppingBlock && kind != egypt_kind::kDropMaker &&
        kind != egypt_kind::kSwitchedDropMaker && kind != egypt_kind::kBoulder && kind != egypt_kind::kTallWallSign &&
        kind != egypt_kind::kWallSign && kind != egypt_kind::kDial) {
        if (auto made = make_of(Home::kEgypt, kind)) {
            return made;
        }
    }
    if (auto made = make_of(Home::kFactory, kind)) {
        return made;
    }
    if (kind != kind::kCharger) {  // the charger is the wacky world's own
        if (auto made = make_of(Home::kWacky, kind)) {
            return made;
        }
    }
    return nullptr;
}

}  // namespace lvr
