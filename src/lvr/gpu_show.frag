#version 450
// A pixel as it is seen: the picture's palette index, looked up in the palette. Where the picture is shown smaller
// than it is, a pixel is the mean of the colours it covers: `samples` x `samples` of them, evenly spread.
// With a backdrop (Remaster rules' parallax, backdrop.hpp): where the map shows colour 0 of one of the palette rows
// 0-7, the backdrop's index instead, from the atlas, repeated.
layout(location = 0) in vec2 at;
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform usampler2D picture;
layout(set = 2, binding = 1) uniform sampler2D palette;
layout(set = 2, binding = 2) uniform usampler2D atlas;
layout(set = 3, binding = 0) uniform Sampled {
    vec4 within;    // the part of the picture this is taken from: left, top, right, bottom
    vec4 backdrop;  // in the atlas: left, top, width, height; a width of 0: none
    vec4 field;     // where the map is, in the picture's own pixels: left, top, right, bottom
    vec2 offset;    // where the picture's top left corner is in the backdrop
    float samples;  // along each side
    float scale;    // the picture's texels a pixel of it
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
    colour = vec4(sum / float(count * count), 1.0);
}
