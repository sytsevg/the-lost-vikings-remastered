#include "lvr/effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "lvr/animations.hpp"
#include "lvr/terrain.hpp"
#include "lvr/world.hpp"

namespace lvr {

namespace {

// What an animation gives off while an entity plays it (from its start until another is started). What is there to
// be seen (traps, fire and shots on their way); not a blow or a hit landing (the user, 2026-11-25).
enum class Glow : std::uint8_t { kNone, kFire, kElectric, kBarrier, kShot, kShotBurst };
enum class Emits : std::uint8_t { kNothing, kEmbers, kSparks };
struct Rule {
    Animation animation;
    Glow glow = Glow::kNone;
    Emits emits = Emits::kNothing;
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
};

// The colours of the HUD and the text, the same in every level (palette.md): what particles are drawn in.
constexpr std::uint8_t kWhite = 2;
constexpr std::uint8_t kYellow = 8;
constexpr std::uint8_t kLightOrange = 11;
constexpr std::uint8_t kRed = 9;
constexpr std::uint8_t kDarkRed = 10;
constexpr std::uint8_t kBlue = 15;
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
                              .ember = false});
    }
}

Effects::Effects() {
    for (std::size_t colour = 0; colour < dots_.size(); ++colour) {
        dots_.at(colour) = assets::make_image(1, 1);
        dots_.at(colour).set(0, 0, static_cast<std::uint8_t>(colour));
    }
}

void Effects::add(const World& world, const SceneGraphics& scene, View& view, Screen& screen, bool moving,
                  float brightness) {
    const auto chance = [this] { return std::uniform_real_distribution<float>{0.0F, 1.0F}(random_); };
    // A light's flicker: between `low` and 1, now and then (`dips` of the time) down to `dip`; still while nothing
    // moves (a scene's fade: what is there holds still as it goes dark or comes in).
    const auto flickering = [&](float low, float dips, float dip) {
        if (!moving) {
            return (low + 1) / 2;
        }
        return chance() < dips ? dip : low + ((1 - low) * chance());
    };
    // A light as bright as the picture is (darker while a scene fades).
    const auto add_light = [&screen, brightness](Light light) {
        for (float& part : light.colour) {
            part *= brightness;
        }
        screen.lights.push_back(light);
    };
    find_electric_colours(world, scene);
    // The spaceship's lights and screens: the colours its levels turn (the cycles), where the map has them; not its
    // electricity's, whose light is cast (below).
    if (world.database == database::kSpaceship) {
        for (const World::ColourCycle& cycle : world.cycles) {
            const int low = std::min(cycle.from, cycle.to);
            const int high = std::max(cycle.from, cycle.to);
            for (int colour = low; colour <= high && colour < 128; ++colour) {
                if (!electric_colours_.at(static_cast<std::size_t>(colour))) {
                    screen.glow.at(static_cast<std::size_t>(colour)) = 255;
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
                              .colour = {0.2F * flicker, 0.55F * flicker, 0.85F * flicker}};
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
    // What gives off light and particles: the entities playing an animation of the table.
    std::vector<std::pair<std::uint16_t, const Rule*>> rules;
    rules.reserve(kRules.size());
    for (const Rule& rule : kRules) {
        rules.emplace_back(world.program_of(rule.animation), &rule);
    }
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
                        .colour = {}};
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
                case Glow::kNone:
                    break;
            }
            add_light(light);
        }
        if (!moving || particles_.size() >= kMostParticles) {
            continue;
        }
        const auto somewhere = [&](float across, float down) {
            return std::array<float, 2>{static_cast<float>(left) + (across * static_cast<float>(right - left)),
                                        static_cast<float>(top) + (down * static_cast<float>(bottom - top))};
        };
        if (rule.emits == Emits::kEmbers && chance() < std::clamp(size / 36, 0.2F, 0.75F)) {
            const auto [x, y] = somewhere(chance(), chance() * 0.6F);
            particles_.push_back({.x = x,
                                  .y = y,
                                  .speed_x = (chance() - 0.5F) * 0.5F,
                                  .speed_y = -0.3F - (chance() * 0.5F),
                                  .fall = -0.005F,
                                  .age = 0,
                                  .life = 25 + static_cast<int>(chance() * 30),
                                  .ember = true});
        } else if (rule.emits == Emits::kSparks && chance() < 0.16F) {
            const auto [x, y] = somewhere(chance(), chance());
            spark(x, y);
        }
    }
    // The particles: drawn where they are, and on.
    for (const Particle& particle : particles_) {
        const float aged = static_cast<float>(particle.age) / static_cast<float>(std::max(particle.life, 1));
        std::uint8_t colour = kWhite;
        if (particle.ember) {
            colour = aged < 0.25F ? kYellow : (aged < 0.5F ? kLightOrange : (aged < 0.8F ? kRed : kDarkRed));
        } else {
            colour = aged < 0.5F ? kWhite : kBlue;
        }
        view.sprites.push_back(DrawnSprite{.image = &dots_.at(colour),
                                           .x = static_cast<int>(std::lround(particle.x)),
                                           .y = static_cast<int>(std::lround(particle.y)),
                                           .mirrored = false,
                                           .flipped = false,
                                           .moves = {}});
    }
    if (moving) {
        for (Particle& particle : particles_) {
            particle.x += particle.speed_x;
            particle.y += particle.speed_y;
            particle.speed_y += particle.fall;
            ++particle.age;
        }
        std::erase_if(particles_, [](const Particle& particle) { return particle.age >= particle.life; });
    }
}

}  // namespace lvr
