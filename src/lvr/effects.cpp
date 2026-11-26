#include "lvr/effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include "lvr/animations.hpp"
#include "lvr/names.hpp"
#include "lvr/terrain.hpp"
#include "lvr/viking_looks.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

// How strongly the game-over scene's colours glow (its water: the spaceship's database, its colour cycles), of 255:
// less than the spaceship's screens (the user, 2026-11-26).
constexpr std::uint8_t kGameOverGlow = 128;

// Which worlds a rule holds in: a bit for each database from the spaceship's. An animation named in one world is
// found in another by the pairing of their programs (World::program_of), and a world without that death can have
// another animation in its place (Egypt's dissolving is where the spaceship's electrocution is): a death's rule
// names the worlds where the death happens.
constexpr unsigned in(int database) noexcept {
    return 1U << static_cast<unsigned>(database - database::kSpaceship);
}
constexpr unsigned kEveryWorld = 0xff;
constexpr bool holds_in(unsigned worlds, int database) noexcept {
    const int bit = database - database::kSpaceship;
    return worlds == kEveryWorld || (bit >= 0 && bit < 8 && (worlds & (1U << static_cast<unsigned>(bit))) != 0);
}

// What an animation gives off while an entity plays it (from its start until another is started). What is there to
// be seen (traps, fire and shots on their way, a Viking electrocuted or burning up); not a blow or a hit landing
// (the user, 2026-11-25). A bomb's fuse and a blast, though (the user, 2026-11-26).
enum class Glow : std::uint8_t { kNone, kFire, kElectric, kBarrier, kShot, kShotBurst, kShocked, kFuse, kBlast, kBeam };
// (kShower: a great deal of sparks, all over him; kBlast: as it goes off, a burst, then embers now and then;
// kTwirlAway, kTwirlIn: a teleport's, a twirl of sparks round him rising and widening as he goes, closing in on him as
// he comes.)
enum class Emits : std::uint8_t {
    kNothing,
    kEmbers,
    kSparks,
    kEmbersAndSmoke,
    kShower,
    kFuse,
    kBlast,
    kTwirlAway,
    kTwirlIn
};
struct Rule {
    Animation animation;
    Glow glow = Glow::kNone;
    Emits emits = Emits::kNothing;
    unsigned worlds = kEveryWorld;
};

constexpr std::array kRules{
    // Fire.
    Rule{.animation = anim::kFireJetLights, .glow = Glow::kFire, .emits = Emits::kNothing},
    Rule{.animation = anim::kFireJetBurns, .glow = Glow::kFire, .emits = Emits::kEmbers},
    Rule{.animation = anim::kFlameFlies, .glow = Glow::kFire, .emits = Emits::kEmbers},
    Rule{.animation = anim::kFlameShotFlies, .glow = Glow::kFire, .emits = Emits::kEmbers},
    Rule{.animation = anim::kFireballFlies, .glow = Glow::kFire, .emits = Emits::kEmbers},
    Rule{.animation = anim::kArcingFireFlies, .glow = Glow::kFire, .emits = Emits::kEmbers},
    Rule{.animation = anim::kFireArrowFlies, .glow = Glow::kFire, .emits = Emits::kNothing},
    // Electricity: a barrier's beam, while it is there and as it goes; fields and bars.
    Rule{.animation = anim::kBarrierHumming, .glow = Glow::kBarrier, .emits = Emits::kSparks},
    Rule{.animation = anim::kBarrierFading, .glow = Glow::kBarrier, .emits = Emits::kNothing},
    Rule{.animation = anim::kBarrierFieldHumming, .glow = Glow::kElectric, .emits = Emits::kSparks},
    Rule{.animation = anim::kBarrierFieldFading, .glow = Glow::kElectric, .emits = Emits::kNothing},
    Rule{.animation = anim::kElectricBarLooks, .glow = Glow::kElectric, .emits = Emits::kSparks},
    // Laser shots (the guns', the turrets', the gunners' and troopers', the drones', Tomator's): a glow that goes
    // with them, and a small one where they burst; no sparks.
    Rule{.animation = anim::kBulletFlies, .glow = Glow::kShot, .emits = Emits::kNothing},
    Rule{.animation = anim::kBulletBursts, .glow = Glow::kShotBurst, .emits = Emits::kNothing},
    Rule{.animation = anim::kShotFlies, .glow = Glow::kShot, .emits = Emits::kNothing},
    Rule{.animation = anim::kShotBursts, .glow = Glow::kShotBurst, .emits = Emits::kNothing},
    Rule{.animation = anim::kShotLands, .glow = Glow::kShotBurst, .emits = Emits::kNothing},
    Rule{.animation = anim::kDroneShotFlies, .glow = Glow::kShot, .emits = Emits::kNothing},
    Rule{.animation = anim::kTomatorShotFlies, .glow = Glow::kShot, .emits = Emits::kNothing},
    Rule{.animation = anim::kTomatorShotBursts, .glow = Glow::kShotBurst, .emits = Emits::kNothing},
    Rule{.animation = anim::kTomatorShotFizzles, .glow = Glow::kShotBurst, .emits = Emits::kNothing},
    // Bombs: a Viking's, its fuse burning, then going off; a bomber's bursting; the factory's blasts.
    Rule{.animation = anim::kPlacedBombTicks, .glow = Glow::kFuse, .emits = Emits::kFuse},
    Rule{.animation = anim::kPlacedBombBlowsUp, .glow = Glow::kBlast, .emits = Emits::kBlast},
    Rule{.animation = anim::kBombBursts, .glow = Glow::kBlast, .emits = Emits::kBlast},
    Rule{.animation = anim::kBlastLarge, .glow = Glow::kBlast, .emits = Emits::kBlast},
    Rule{.animation = anim::kBlastSmall, .glow = Glow::kBlast, .emits = Emits::kBlast},
};

// The Vikings' deaths by electricity and by fire (the user, 2026-11-25), each Viking's own; the other deaths give
// off nothing. And teleporting (the user, 2026-11-26), where there are teleporters: the spaceship's levels. Each where
// it happens: a barrier's (or a bar's) everywhere but the prehistoric world and Egypt, whose Vikings are taken away
// instead; the map's electricity's, named apart for the spaceship and for the factory and the wacky world; burning up,
// everywhere but the spaceship.
constexpr auto kVikingDeaths = [] {
    constexpr std::array<const VikingLooks*, 3> kLooks{&kErikLooks, &kBaleogLooks, &kOlafLooks};
    constexpr unsigned kBarriers = in(database::kSpaceship) | in(database::kFactory) | in(database::kWacky);
    constexpr unsigned kSpaceship = in(database::kSpaceship);
    constexpr unsigned kFactories = in(database::kFactory) | in(database::kWacky);
    constexpr unsigned kFires = in(database::kPrehistoric) | in(database::kEgypt) | kFactories;
    std::array<Rule, 8 * kLooks.size()> rules{};
    std::size_t at = 0;
    for (std::size_t who = 0; who < kLooks.size(); ++who) {
        const VikingLooks& looks = *kLooks.at(who);
        const auto add = [&rules, &at](Animation animation, Glow glow, Emits emits, unsigned worlds) {
            rules.at(at++) = Rule{.animation = animation, .glow = glow, .emits = emits, .worlds = worlds};
        };
        add(looks.electrocuted, Glow::kShocked, Emits::kShower, kBarriers);
        add(looks.electrocuted_from_below, Glow::kShocked, Emits::kShower, kSpaceship);
        add(looks.electrocuted_from_above, Glow::kShocked, Emits::kShower, kSpaceship);
        add(anim::kVikingShockedFromBelow.at(who), Glow::kShocked, Emits::kShower, kFactories);
        add(anim::kVikingShockedFromAbove.at(who), Glow::kShocked, Emits::kShower, kFactories);
        add(anim::kVikingBurnsUp.at(who), Glow::kFire, Emits::kEmbersAndSmoke, kFires);
        add(anim::kVikingBeamedAway.at(who), Glow::kBeam, Emits::kTwirlAway, kSpaceship);
        add(anim::kVikingBeamedIn.at(who), Glow::kBeam, Emits::kTwirlIn, kSpaceship);
    }
    return rules;
}();

// The colours of the HUD and the text, the same in every level (palette.md): what particles are drawn in.
constexpr std::uint8_t kWhite = 2;
constexpr std::uint8_t kYellow = 8;
constexpr std::uint8_t kLightOrange = 11;
constexpr std::uint8_t kRed = 9;
constexpr std::uint8_t kDarkRed = 10;
constexpr std::uint8_t kBlue = 15;
constexpr std::uint8_t kLightGrey = 12;
constexpr std::uint8_t kGrey = 4;
constexpr std::uint8_t kDarkGrey = 14;
constexpr std::size_t kMostParticles = 4000;

// Where an entity's pieces are drawn, in the level: left, top, right, bottom. None shown: nothing.
std::optional<std::array<int, 4>> bounds(const World& world, const Entity& entity) {
    std::optional<std::array<int, 4>> box;
    for (int index = entity.sprite.first; index < entity.sprite.first + entity.sprite.count; ++index) {
        const Piece& piece = world.pieces.at(static_cast<std::size_t>(index) % world.pieces.size());
        if (!piece.in_use || piece.hidden || piece.blinked_off) {
            continue;
        }
        const int width = piece.shape == 1 ? 8 : (piece.shape == 4 ? 16 : 32);
        const int height = piece.shape == 1 ? 8 : (piece.shape == 4 ? 16 : piece.size);
        const std::array<int, 4> here{piece.x, piece.y, piece.x + width, piece.y + height};
        box = box ? std::array<int, 4>{std::min((*box)[0], here[0]), std::min((*box)[1], here[1]),
                                       std::max((*box)[2], here[2]), std::max((*box)[3], here[3])}
                  : here;
    }
    return box;
}

// Whether a pixel of the level stops light: the map's ground and walls (not its platforms, which are thin), and a
// slope under its surface.
bool stops_light(const World& world, int x, int y) {
    const std::uint8_t type = world.map.tile_type_at(x, y);
    if (type >= tile::kFirstSlope) {
        return under_slope(world, x, y);
    }
    return type == tile::kGround || type == tile::kGround02 || type == tile::kLadderTop || type == tile::kGround20 ||
           type == tile::kBreakable21 || type == tile::kBreakable22;
}

}  // namespace

void Effects::make_ground(const World& world) {
    std::vector<std::uint8_t> types;
    types.reserve(world.map.cells.size());
    for (const Cell& cell : world.map.cells) {
        types.push_back(cell.tile_type);
    }
    if (ground_ != nullptr && types == ground_of_ && ground_->width == world.map.pixel_width()) {
        return;
    }
    auto ground = std::make_shared<Ground>();
    ground->width = world.map.pixel_width();
    ground->height = world.map.pixel_height();
    ground->solid.assign(static_cast<std::size_t>(ground->width) * static_cast<std::size_t>(ground->height), 0);
    ground->id = ++grounds_made_;
    for (int y = 0; y < ground->height; ++y) {
        for (int x = 0; x < ground->width; ++x) {
            if (stops_light(world, x, y)) {
                ground->solid[(static_cast<std::size_t>(y) * static_cast<std::size_t>(ground->width)) +
                              static_cast<std::size_t>(x)] = 1;
            }
        }
    }
    ground_ = std::move(ground);
    ground_of_ = std::move(types);
}

void Effects::find_electric_colours(const World& world, const SceneGraphics& scene) {
    if (scene.id != 0 && scene.id == electric_of_) {
        return;
    }
    electric_of_ = scene.id;
    electric_colours_ = {};
    for (const Cell& cell : world.map.cells) {
        if (cell.tile_type != tile::kElectricity || cell.metatile >= scene.metatiles.size()) {
            continue;
        }
        for (const assets::TileRef& part : scene.metatiles[cell.metatile].parts) {
            if (part.tile < scene.tileset.size()) {
                for (const std::uint8_t colour : scene.tileset[part.tile].pixels) {
                    electric_colours_.at(colour) = true;
                }
            }
        }
    }
}

void Effects::spark(float x, float y) {
    const auto chance = [this] { return std::uniform_real_distribution<float>{0.0F, 1.0F}(random_); };
    const int count = 4 + static_cast<int>(chance() * 5);
    for (int one = 0; one < count; ++one) {
        const float angle = chance() * 6.2832F;
        const float speed = 0.6F + (chance() * 1.4F);
        particles_.push_back({.x = x,
                              .y = y,
                              .speed_x = std::cos(angle) * speed,
                              .speed_y = (std::sin(angle) * speed) - 0.4F,
                              .fall = 0.08F,
                              .age = 0,
                              .life = 4 + static_cast<int>(chance() * 8),
                              .look = Particle::Look::kSpark});
    }
}

// Where a bomb's fuse burns, in the level: the tip of the fuse at its top right (the user, 2026-11-26: there, not
// above its middle), on the other side for one put down facing left (its picture is turned round).
std::array<float, 2> Effects::fuse_of(const Entity& bomb) {
    constexpr int kRight = 5;  // of its position (the bomb's pixels: level 1, put down from the item)
    constexpr int kUp = 9;
    return {static_cast<float>(bomb.facing_left() ? bomb.x - kRight : bomb.x + kRight),
            static_cast<float>(bomb.y - kUp)};
}

// Three sparks a picture on a twirl round (x, from top to bottom in the level), a funnel wider higher up: going, from
// anywhere on him, rising and widening; coming, from wide around him, closing in. They turn the same way round, a
// turn in about 11 pictures (the user, 2026-11-26: narrower, and swirling more).
void Effects::twirl(float x, float top, float bottom, bool in) {
    const auto chance = [this] { return std::uniform_real_distribution<float>{0.0F, 1.0F}(random_); };
    constexpr float kTurn = 0.55F;  // radians a picture
    for (int one = 0; one < 3; ++one) {
        const float up = chance();  // how far up him, 0 his feet
        const int life = 16 + static_cast<int>(chance() * 12);
        const float wide = 5 + (up * 11);
        const float radius = in ? wide + 9 : wide;
        particles_.push_back({.x = x,
                              .y = bottom - (up * (bottom - top)),
                              .speed_x = 0,
                              .speed_y = in ? 0.25F : -0.7F,
                              .fall = 0,
                              .age = 0,
                              .life = life,
                              .look = Particle::Look::kTwirl,
                              .orbit_x = x,
                              .radius = radius,
                              .angle = chance() * 6.2832F,
                              .turn = kTurn,
                              .widen = in ? -(radius - 2) / static_cast<float>(life) : 0.4F});
    }
}

void Effects::blast(float x, float y, float size) {
    const auto chance = [this] { return std::uniform_real_distribution<float>{0.0F, 1.0F}(random_); };
    const float scale = std::clamp(size / 32, 0.6F, 2.0F);
    const int embers = static_cast<int>((18 + (chance() * 10)) * scale);
    for (int one = 0; one < embers; ++one) {
        const float angle = chance() * 6.2832F;
        const float speed = (0.6F + (chance() * 1.8F)) * scale;
        particles_.push_back({.x = x,
                              .y = y,
                              .speed_x = std::cos(angle) * speed,
                              .speed_y = (std::sin(angle) * speed) - 0.8F,
                              .fall = 0.05F,
                              .age = 0,
                              .life = 14 + static_cast<int>(chance() * 26),
                              .look = Particle::Look::kEmber});
    }
    const int puffs = static_cast<int>((5 + (chance() * 4)) * scale);
    for (int one = 0; one < puffs; ++one) {
        particles_.push_back({.x = x + ((chance() - 0.5F) * size * 0.6F),
                              .y = y + ((chance() - 0.5F) * size * 0.4F),
                              .speed_x = (chance() - 0.5F) * 0.6F,
                              .speed_y = -0.2F - (chance() * 0.35F),
                              .fall = 0.002F,
                              .age = 0,
                              .life = 40 + static_cast<int>(chance() * 35),
                              .look = Particle::Look::kSmoke});
    }
    spark(x, y);
    spark(x, y);
}

Effects::Effects() {
    for (std::size_t colour = 0; colour < dots_.size(); ++colour) {
        dots_.at(colour) = assets::make_image(1, 1);
        dots_.at(colour).set(0, 0, static_cast<std::uint8_t>(colour));
    }
}

void Effects::add(const World& world, const SceneGraphics& scene, View& view, Screen& screen, bool moving) {
    const auto chance = [this] { return std::uniform_real_distribution<float>{0.0F, 1.0F}(random_); };
    // A light's flicker: between `low` and 1, now and then (`dips` of the time) down to `dip`; still while nothing
    // moves (a scene's fade: what is there holds still as it goes dark or comes in).
    const auto flickering = [&](float low, float dips, float dip) {
        if (!moving) {
            return (low + 1) / 2;
        }
        return chance() < dips ? dip : low + ((1 - low) * chance());
    };
    const auto add_light = [&screen](const Light& light) { screen.lights.push_back(light); };
    find_electric_colours(world, scene);
    // The spaceship's lights and screens: the colours its levels turn (the cycles), where the map has them; not its
    // electricity's, whose light is cast (below).
    if (world.database == database::kSpaceship) {
        for (const World::ColourCycle& cycle : world.cycles) {
            const int low = std::min(cycle.from, cycle.to);
            const int high = std::max(cycle.from, cycle.to);
            for (int colour = low; colour <= high && colour < 128; ++colour) {
                if (!electric_colours_.at(static_cast<std::size_t>(colour))) {
                    screen.glow.at(static_cast<std::size_t>(colour)) =
                        world.scene == scene::kGameOver ? kGameOverGlow : std::uint8_t{255};
                }
            }
        }
    }
    // Whether a light (a box: middle and half its size, and its reach) falls on the picture at all.
    const auto in_picture = [&view](float x, float y, float half_width, float half_height, float reach) {
        return x + half_width + reach >= 0 && y + half_height + reach >= 0 &&
               x - half_width - reach < static_cast<float>(view.width) &&
               y - half_height - reach < static_cast<float>(view.height);
    };
    // The map's electricity: a light along each row of its cells, from their middle row, and now and then sparks.
    for (int row = 0; row < world.map.height; ++row) {
        for (int column = 0; column < world.map.width;) {
            if (world.map.at(column, row).tile_type != tile::kElectricity) {
                ++column;
                continue;
            }
            const int first = column;
            while (column < world.map.width && world.map.at(column, row).tile_type == tile::kElectricity) {
                ++column;
            }
            const auto left = static_cast<float>(first * kCellSize);
            const auto width = static_cast<float>((column - first) * kCellSize);
            const float middle_y = (static_cast<float>(row) + 0.5F) * static_cast<float>(kCellSize);
            const float flicker = flickering(0.8F, 0.1F, 0.4F);
            const Light light{.x = left + (width / 2) - static_cast<float>(view.camera_x),
                              .y = middle_y - static_cast<float>(view.camera_y),
                              .half_width = width / 2,
                              .half_height = 2,
                              .reach = 15,
                              .colour = {0.2F * flicker, 0.55F * flicker, 0.85F * flicker},
                              .from = Light::kFromMap};
            if (screen.lights.size() < kMostLights &&
                in_picture(light.x, light.y, light.half_width, light.half_height, light.reach)) {
                add_light(light);
            }
            if (moving && particles_.size() < kMostParticles &&
                chance() < std::min(0.04F * static_cast<float>(column - first), 0.3F)) {
                spark(left + (chance() * width), middle_y);
            }
        }
    }
    // What stops light.
    if (!world.map.cells.empty()) {
        make_ground(world);
        screen.ground = ground_;
    }
    // What gives off light and particles: the entities playing an animation of the tables, in this world.
    std::vector<std::pair<std::uint16_t, const Rule*>> rules;
    rules.reserve(kRules.size() + kVikingDeaths.size());
    for (const auto& table : {std::span<const Rule>{kRules}, std::span<const Rule>{kVikingDeaths}}) {
        for (const Rule& rule : table) {
            if (holds_in(rule.worlds, world.database)) {
                rules.emplace_back(world.program_of(rule.animation), &rule);
            }
        }
    }
    std::vector<const Entity*> now_blasting;
    for (const auto& owned : world.entities) {
        const Entity* entity = owned.get();
        if (entity == nullptr || entity->sprite.started == kNoAnimation) {
            continue;
        }
        const auto found =
            std::ranges::find(rules, entity->sprite.started, &std::pair<std::uint16_t, const Rule*>::first);
        if (found == rules.end()) {
            continue;
        }
        const Rule& rule = *found->second;
        const auto box = bounds(world, *entity);
        if (!box) {
            continue;
        }
        const auto [left, top, right, bottom] = *box;
        // Its sprite drawn last: what is drawn after it is in front of it, and hides its halo.
        std::int32_t drawn_last = Light::kAnywhere;
        for (std::size_t i = 0; i < view.sprites.size(); ++i) {
            const int piece = view.sprites[i].piece;
            if (piece >= entity->sprite.first && piece < entity->sprite.first + entity->sprite.count) {
                drawn_last = static_cast<std::int32_t>(i);
            }
        }
        const float middle_x = static_cast<float>(left + right) / 2;
        const float middle_y = static_cast<float>(top + bottom) / 2;
        const float size = static_cast<float>(std::max(right - left, bottom - top));
        if (screen.lights.size() < kMostLights && rule.glow != Glow::kNone &&
            in_picture(middle_x - static_cast<float>(view.camera_x), middle_y - static_cast<float>(view.camera_y),
                       size / 2, size / 2, 24)) {
            Light light{.x = middle_x - static_cast<float>(view.camera_x),
                        .y = middle_y - static_cast<float>(view.camera_y),
                        .half_width = static_cast<float>(right - left) / 4,
                        .half_height = static_cast<float>(bottom - top) / 4,
                        .reach = 0,
                        .colour = {},
                        .from = drawn_last};
            switch (rule.glow) {
                case Glow::kFire: {
                    // Round: from a small box in its middle.
                    const float flicker = flickering(0.8F, 0.0F, 0.8F);
                    light.half_width = light.half_height = static_cast<float>(std::min(right - left, bottom - top)) / 6;
                    light.reach = 23 + (moving ? 3 * chance() : 1.5F);
                    light.colour = {0.95F * flicker, 0.48F * flicker, 0.16F * flicker};
                    break;
                }
                case Glow::kElectric: {
                    const float flicker = flickering(0.75F, 0.15F, 0.3F);
                    light.half_width = static_cast<float>(right - left) / 2;  // all of it
                    light.reach = 12;
                    light.colour = {0.3F * flicker, 0.45F * flicker, 0.9F * flicker};
                    break;
                }
                case Glow::kBarrier: {
                    // Along the whole beam, from its middle row (or column, standing up).
                    const bool standing = bottom - top > right - left;
                    light.half_width = standing ? 2 : static_cast<float>(right - left) / 2;
                    light.half_height = standing ? static_cast<float>(bottom - top) / 2 : 2;
                    light.reach = 20;
                    light.colour = {0.4F, 0.6F, 1.0F};
                    break;
                }
                case Glow::kShot:
                    light.reach = 14;
                    light.colour = {0.95F, 0.35F, 0.3F};  // the shots' red
                    break;
                case Glow::kShotBurst:
                    light.reach = 10;
                    light.colour = {0.7F, 0.3F, 0.25F};
                    break;
                case Glow::kShocked: {
                    // A Viking electrocuted: a white-blue light around him that flashes.
                    const float flicker = flickering(0.55F, 0.25F, 0.15F);
                    light.reach = 18;
                    light.colour = {0.55F * flicker, 0.7F * flicker, 1.0F * flicker};
                    break;
                }
                case Glow::kBeam: {  // teleported: a cool light that shimmers
                    const float flicker = flickering(0.7F, 0.1F, 0.45F);
                    light.reach = 20;
                    light.colour = {0.5F * flicker, 0.8F * flicker, 1.0F * flicker};
                    break;
                }
                case Glow::kFuse: {  // a small flickering light at the fuse
                    const float flicker = flickering(0.5F, 0.2F, 0.25F);
                    const auto [fuse_x, fuse_y] = fuse_of(*entity);
                    light.x = fuse_x - static_cast<float>(view.camera_x);
                    light.y = fuse_y - static_cast<float>(view.camera_y);
                    light.half_width = light.half_height = 1;
                    light.reach = 10;
                    light.colour = {1.0F * flicker, 0.6F * flicker, 0.2F * flicker};
                    break;
                }
                case Glow::kBlast: {  // a great warm flash, flickering as the fire does
                    const float flicker = flickering(0.75F, 0.1F, 0.5F);
                    light.half_width = light.half_height = size / 10;  // (round: from a small box in its middle)
                    light.reach = 36 + (size / 2);
                    light.colour = {1.0F * flicker, 0.68F * flicker, 0.32F * flicker};
                    break;
                }
                case Glow::kNone:
                    break;
            }
            add_light(light);
        }
        if (rule.emits == Emits::kBlast) {
            now_blasting.push_back(entity);
            if (moving && !std::ranges::contains(blasting_, entity)) {  // (it goes off: once)
                blast(middle_x, middle_y, size);
            }
        }
        if (!moving || particles_.size() >= kMostParticles) {
            continue;
        }
        const auto somewhere = [&](float across, float down) {
            return std::array<float, 2>{static_cast<float>(left) + (across * static_cast<float>(right - left)),
                                        static_cast<float>(top) + (down * static_cast<float>(bottom - top))};
        };
        const bool embers = rule.emits == Emits::kEmbers || rule.emits == Emits::kEmbersAndSmoke;
        if (embers && chance() < std::clamp(size / 36, 0.2F, 0.75F)) {
            const auto [x, y] = somewhere(chance(), chance() * 0.6F);
            particles_.push_back({.x = x,
                                  .y = y,
                                  .speed_x = (chance() - 0.5F) * 0.5F,
                                  .speed_y = -0.3F - (chance() * 0.5F),
                                  .fall = -0.005F,
                                  .age = 0,
                                  .life = 25 + static_cast<int>(chance() * 30),
                                  .look = Particle::Look::kEmber});
        } else if (rule.emits == Emits::kSparks && chance() < 0.16F) {
            const auto [x, y] = somewhere(chance(), chance());
            spark(x, y);
        } else if (rule.emits == Emits::kShower) {  // (one or two showers every picture, anywhere on him)
            for (int shower = chance() < 0.5F ? 2 : 1; shower > 0; --shower) {
                const auto [x, y] = somewhere(chance(), chance());
                spark(x, y);
            }
        } else if (rule.emits == Emits::kFuse) {  // sparks off the fuse, up and out: two or three a picture
            const auto [fuse_x, fuse_y] = fuse_of(*entity);
            for (int one = chance() < 0.5F ? 3 : 2; one > 0; --one) {
                particles_.push_back({.x = fuse_x + ((chance() - 0.5F) * 2),
                                      .y = fuse_y + ((chance() - 0.5F) * 2),
                                      .speed_x = (chance() - 0.5F) * 1.4F,
                                      .speed_y = -0.6F - (chance() * 0.9F),
                                      .fall = 0.09F,
                                      .age = 0,
                                      .life = 4 + static_cast<int>(chance() * 7),
                                      .look = chance() < 0.6F ? Particle::Look::kSpark : Particle::Look::kEmber});
            }
        } else if (rule.emits == Emits::kTwirlAway || rule.emits == Emits::kTwirlIn) {
            twirl(middle_x, static_cast<float>(top), static_cast<float>(bottom), rule.emits == Emits::kTwirlIn);
        } else if (rule.emits == Emits::kBlast && chance() < 0.3F) {  // (after it went off: embers now and then)
            const auto [x, y] = somewhere(chance(), chance());
            particles_.push_back({.x = x,
                                  .y = y,
                                  .speed_x = (chance() - 0.5F) * 0.8F,
                                  .speed_y = -0.4F - (chance() * 0.6F),
                                  .fall = 0.01F,
                                  .age = 0,
                                  .life = 15 + static_cast<int>(chance() * 20),
                                  .look = Particle::Look::kEmber});
        }
        // Smoke: now and then a wisp from his upper half, rising slower than the embers and drifting, for longer.
        if (rule.emits == Emits::kEmbersAndSmoke && chance() < 0.15F) {
            const auto [x, y] = somewhere(0.2F + (chance() * 0.6F), chance() * 0.5F);
            particles_.push_back({.x = x,
                                  .y = y,
                                  .speed_x = (chance() - 0.5F) * 0.3F,
                                  .speed_y = -0.25F - (chance() * 0.2F),
                                  .fall = 0.002F,
                                  .age = 0,
                                  .life = 35 + static_cast<int>(chance() * 25),
                                  .look = Particle::Look::kSmoke});
        }
    }
    blasting_ = std::move(now_blasting);
    // The particles: drawn where they are, and on.
    for (const Particle& particle : particles_) {
        const float aged = static_cast<float>(particle.age) / static_cast<float>(std::max(particle.life, 1));
        std::uint8_t colour = kWhite;
        switch (particle.look) {
            case Particle::Look::kSpark:
                colour = aged < 0.5F ? kWhite : kBlue;
                break;
            case Particle::Look::kEmber:
                colour = aged < 0.25F ? kYellow : (aged < 0.5F ? kLightOrange : (aged < 0.8F ? kRed : kDarkRed));
                break;
            case Particle::Look::kSmoke:
                colour = aged < 0.3F ? kLightGrey : (aged < 0.7F ? kGrey : kDarkGrey);
                break;
            case Particle::Look::kTwirl:  // the near side of the twirl white, the far side blue
                colour = std::sin(particle.angle) > 0 ? kWhite : kBlue;
                break;
        }
        view.sprites.push_back(DrawnSprite{.image = &dots_.at(colour),
                                           .x = static_cast<int>(std::lround(particle.x)),
                                           .y = static_cast<int>(std::lround(particle.y)),
                                           .mirrored = false,
                                           .flipped = false,
                                           .moves = {}});
        if (particle.look == Particle::Look::kTwirl) {  // a trail along its way round: it draws an arc of the spiral
            constexpr int kTrail = 3;
            for (int back = 1; back <= kTrail; ++back) {
                const float angle = particle.angle - (particle.turn * static_cast<float>(back));
                const float radius = std::max(particle.radius - (particle.widen * static_cast<float>(back)), 0.5F);
                view.sprites.push_back(DrawnSprite{
                    .image = &dots_.at(kBlue),
                    .x = static_cast<int>(std::lround(particle.orbit_x + (radius * std::cos(angle)))),
                    .y = static_cast<int>(std::lround(particle.y - (particle.speed_y * static_cast<float>(back)))),
                    .mirrored = false,
                    .flipped = false,
                    .moves = {}});
            }
        }
    }
    if (moving) {
        for (Particle& particle : particles_) {
            if (particle.radius > 0) {  // twirling: round its point, the circle seen from the side
                particle.angle += particle.turn;
                particle.radius = std::max(particle.radius + particle.widen, 0.5F);
                particle.x = particle.orbit_x + (particle.radius * std::cos(particle.angle));
            } else {
                particle.x += particle.speed_x;
            }
            particle.y += particle.speed_y;
            particle.speed_y += particle.fall;
            ++particle.age;
        }
        std::erase_if(particles_, [](const Particle& particle) { return particle.age >= particle.life; });
    }
}

}  // namespace lvr
