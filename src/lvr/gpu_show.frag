#version 450
// A pixel as it is seen: the picture's palette index, looked up in the palette. Where the picture is shown smaller
// than it is, a pixel is the mean of the colours it covers: `samples` x `samples` of them, evenly spread.
// With a backdrop (Remaster rules' parallax, backdrop.hpp): where the map shows colour 0 of one of the palette rows
// 0-7, the backdrop's index instead, from the atlas, repeated.
// The Modern visuals' light (effects.hpp), worked out at the middle of each of the picture's pixels and in steps of
// brightness, so it is in the game's pixels as everything else: around the colours that give light (the palette's
// alpha says how much) a glow laid over the picture; and the lights, which brighten what they fall on, from their
// box out, stopped by the ground (what is solid to them: a texture of the level), whose surface they light a little
// more where they meet it.
layout(location = 0) in vec2 at;
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform usampler2D picture;
layout(set = 2, binding = 1) uniform sampler2D palette;
layout(set = 2, binding = 2) uniform usampler2D atlas;
layout(set = 2, binding = 3) uniform usampler2D solid;
layout(set = 2, binding = 4) uniform usampler2D order;  // the place in the picture's order of what shows
layout(set = 3, binding = 0) uniform Sampled {
    vec4 within;    // the part of the picture this is taken from: left, top, right, bottom
    vec4 backdrop;  // in the atlas: left, top, width, height; a width of 0: none
    vec4 field;     // where the map is, in the picture's own pixels: left, top, right, bottom
    vec2 offset;    // where the picture's top left corner is in the backdrop
    float samples;  // along each side
    float scale;    // the picture's texels a pixel of it
    vec4 lit;       // how many lights; how far a glow reaches, in the picture's pixels; how far the camera moved since
                    // the picture was kept (x, y)
    vec4 ground;    // where the picture's corner is in the level (x, y); what stops light: its width and height (0:
                    // nothing does)
    vec4 lights[3 * 64];  // each: the box that gives it off (its middle x, y, half its width, height), then its
                          // colour and how far from the box it reaches, then the place in the picture's order of
                          // what gives it off (x): what was drawn after that hides its halo
};

uint index_at(ivec2 texel) {
    uint index = texelFetch(picture, texel, 0).r;
    if (backdrop.z > 0.0 && index < 128u && (index & 15u) == 0u) {
        vec2 spot = (vec2(texel) + 0.5) / scale;
        if (all(greaterThanEqual(spot, field.xy)) && all(lessThan(spot, field.zw))) {
            vec2 behind = mod(floor(offset + spot), backdrop.zw);
            index = texelFetch(atlas, ivec2(backdrop.xy + behind), 0).r;
        }
    }
    return index;
}

// Whether light stops at this pixel of the level.
bool solid_at(vec2 level) {
    ivec2 at_level = ivec2(floor(level));
    if (ground.z <= 0.0 || any(lessThan(at_level, ivec2(0))) || at_level.x >= int(ground.z) ||
        at_level.y >= int(ground.w)) {
        return false;
    }
    return texelFetch(solid, at_level, 0).r != 0u;
}

// How much of a light from `from` gets to `spot` (both in the picture's pixels): all of it through the open (1),
// on the ground's surface where it meets it a highlight (2), none further in (0). A light inside the ground shines
// from where it comes out.
float reaching(vec2 spot, vec2 from) {
    vec2 start = from + ground.xy;
    vec2 to = spot + ground.xy;
    int steps = int(ceil(distance(start, to)));
    vec2 step = (to - start) / max(float(steps), 1.0);
    bool out_of_ground = !solid_at(start);
    for (int i = 1; i <= steps; ++i) {
        bool here = solid_at(start + (step * float(i)));
        if (!out_of_ground) {
            out_of_ground = !here;
        } else if (here) {
            return steps - i < 2 ? 2.0 : 0.0;
        }
    }
    return 1.0;
}

// In steps of brightness, as the game's colours are.
vec3 stepped(vec3 light) {
    float most = max(light.r, max(light.g, light.b));
    return most > 0.0 ? light * ((floor((most * 6.0) + 0.3) / 6.0) / most) : light;
}

// The lights at `spot`: what they cast on what is there, and their glow laid over it (fainter in the ground), where
// what shows (`here`, its place in the picture's order) isn't in front of what gives it off.
vec3 cast_at(vec2 spot, uint here, out vec3 halo) {
    vec3 light = vec3(0.0);
    halo = vec3(0.0);
    for (int i = 0; i < int(lit.x); ++i) {
        vec4 box = lights[3 * i];
        vec4 shade = lights[(3 * i) + 1];
        bool behind = float(here) <= lights[(3 * i) + 2].x;
        vec2 middle = box.xy - lit.zw;
        vec2 nearest = clamp(spot, middle - box.zw, middle + box.zw);
        float near = 1.0 - (distance(spot, nearest) / shade.a);
        if (near > 0.0) {
            float reached = reaching(spot, nearest);
            // On the ground's surface the light is stronger, and whiter, so that it shows on any colour.
            vec3 shown = reached > 1.5 ? mix(shade.rgb, vec3(max(shade.r, max(shade.g, shade.b))), 0.6) * 1.8
                                       : shade.rgb * reached;
            light += shown * near * near;
            halo += behind ? shade.rgb * near * near * (reached > 0.5 ? 1.0 : 0.35) : vec3(0.0);
        }
    }
    halo = stepped(halo * 0.55);
    return stepped(light);
}

// The glow around the colours that give light at `spot`: every pixel of the picture within reach, the nearer
// counting for more; not from one that what shows here (`here`) was drawn in front of (the user, 2026-11-26: a
// Viking before a screen).
vec3 glow_at(vec2 spot, uint here) {
    vec3 glow = vec3(0.0);
    int reach = int(lit.y);
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            float near = 1.0 - (length(vec2(x, y)) / (lit.y + 1.0));
            if (near > 0.0) {
                ivec2 texel = clamp(ivec2(floor((spot + vec2(x, y)) * scale)), ivec2(within.xy), ivec2(within.zw) - 1);
                if (here <= texelFetch(order, texel, 0).r) {
                    vec4 colour_there = texelFetch(palette, ivec2(int(texelFetch(picture, texel, 0).r), 0), 0);
                    glow += colour_there.rgb * colour_there.a * near;
                }
            }
        }
    }
    return stepped(glow * 0.065);  // (the user, 2026-11-26: the spaceship's screens a little brighter)
}

void main() {
    vec2 covers = vec2(abs(dFdx(at.x)), abs(dFdy(at.y)));
    int count = int(samples);
    vec3 sum = vec3(0.0);
    for (int y = 0; y < count; ++y) {
        for (int x = 0; x < count; ++x) {
            vec2 spot = at + (((vec2(x, y) + 0.5) / samples) - 0.5) * covers;
            ivec2 texel = clamp(ivec2(floor(spot)), ivec2(within.xy), ivec2(within.zw) - 1);
            sum += texelFetch(palette, ivec2(int(index_at(texel)), 0), 0).rgb;
        }
    }
    vec3 seen = sum / float(count * count);
    vec2 spot = floor(at / scale) + 0.5;  // the middle of the picture's pixel this is in
    // What shows here: its place in the picture's order (what was drawn after what gives light hides its glow).
    uint here = texelFetch(order, clamp(ivec2(floor(at)), ivec2(within.xy), ivec2(within.zw) - 1), 0).r;
    if (lit.x > 0.0) {
        vec3 halo;
        vec3 light = cast_at(spot, here, halo);
        seen = min(seen + (seen * light * 2.0) + (light * 0.06), vec3(1.0));
        seen += halo * (1.0 - seen);
    }
    if (lit.y > 0.0) {
        seen += glow_at(spot, here) * (1.0 - seen);
    }
    colour = vec4(seen, 1.0);
}
