#version 450
// A pixel of a picture: its palette index, as the atlas has it. Nothing is filtered or blended: an index is
// written or it isn't.
layout(location = 0) in vec2 at;
layout(location = 1) flat in vec4 how;
layout(location = 0) out uint index;
layout(set = 2, binding = 0) uniform usampler2D atlas;

void main() {
    ivec2 texel = ivec2(floor(at));
    uint colour = texelFetch(atlas, texel, 0).r;
    if (how.z > 0.5 && colour == 0u) {
        discard;
    }
    if (how.z > 1.5 && colour == 3u) {  // a letter alone, without its cell (a name in the HUD)
        discard;
    }
    if (how.z > 2.5 && colour == 2u) {  // and grey, not white (a player watching)
        colour = 12u;
    }
    if (how.w > 0.5 && texelFetch(atlas, texel + ivec2(how.xy), 0).r == 0u) {
        discard;
    }
    index = colour;
}
