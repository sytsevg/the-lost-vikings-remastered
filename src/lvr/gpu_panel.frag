#version 450
// A picture laid over the screen in the game's pixels (Remaster rules' menu), as large as the game's pixels are shown:
// each of its pixels one colour where it covers the window's pixels whole, and only where one meets the next is a
// window's pixel mixed of the two, by how much of it each covers (at a whole number of the window's pixels a pixel,
// nothing is mixed). Mixed by their light, not by their clear parts' colour; see-through by its alpha.
layout(location = 0) in vec2 at;  // in the picture's pixels
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform sampler2D picture;

vec4 premultiplied(ivec2 pixel) {
    ivec2 size = textureSize(picture, 0);
    vec4 c = texelFetch(picture, clamp(pixel, ivec2(0), size - 1), 0);
    return vec4(c.rgb * c.a, c.a);
}

void main() {
    // The edge between pixels nearest this one, and how much of the window's pixel is past it.
    vec2 edge = floor(at + 0.5);
    vec2 past = clamp(((at - edge) / max(fwidth(at), vec2(1e-5))) + 0.5, 0.0, 1.0);
    ivec2 before = ivec2(edge) - 1;
    vec4 top = mix(premultiplied(before), premultiplied(before + ivec2(1, 0)), past.x);
    vec4 bottom = mix(premultiplied(before + ivec2(0, 1)), premultiplied(before + ivec2(1, 1)), past.x);
    vec4 mixed = mix(top, bottom, past.y);
    colour = mixed.a > 0.0 ? vec4(mixed.rgb / mixed.a, mixed.a) : vec4(0.0);
}
