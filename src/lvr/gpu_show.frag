#version 450
// A pixel as it is seen: the picture's palette index, looked up in the palette. Where the picture is shown smaller
// than it is, a pixel is the mean of the colours it covers: `samples` x `samples` of them, evenly spread.
layout(location = 0) in vec2 at;
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform usampler2D picture;
layout(set = 2, binding = 1) uniform sampler2D palette;
layout(set = 3, binding = 0) uniform Sampled {
    vec4 within;    // the part of the picture this is taken from: left, top, right, bottom
    float samples;  // along each side
};

void main() {
    vec2 covers = vec2(abs(dFdx(at.x)), abs(dFdy(at.y)));
    int count = int(samples);
    vec3 sum = vec3(0.0);
    for (int y = 0; y < count; ++y) {
        for (int x = 0; x < count; ++x) {
            vec2 spot = at + (((vec2(x, y) + 0.5) / samples) - 0.5) * covers;
            ivec2 texel = clamp(ivec2(floor(spot)), ivec2(within.xy), ivec2(within.zw) - 1);
            uint index = texelFetch(picture, texel, 0).r;
            sum += texelFetch(palette, ivec2(int(index), 0), 0).rgb;
        }
    }
    colour = vec4(sum / float(count * count), 1.0);
}
