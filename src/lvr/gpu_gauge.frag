#version 450
// Remaster rules' rewind bar, along the minimap's bottom, in the original's pixels as the game around it is (each
// one colour, nothing mixed at an edge), and a little see-through: a white frame with its corners rounded off,
// filled light grey from the left as far as there is rewind left, dark past that, and the rewind's mark in its
// middle (two triangles pointing left), dark on the fill and light on the rest.
layout(location = 0) in vec2 at;  // in the original's pixels, from the box's top left corner
layout(location = 0) out vec4 colour;
layout(set = 3, binding = 0) uniform Gauge {
    vec4 size;  // the box's width and height; its corners' radius; how wide its frame is
    vec4 left;  // how much of the rewind is left (0 to 1); how much of the minimap is there (0 to 1); then unused
};

// How far `point` is outside a box of `half_size` around (0, 0) with corners of radius `radius`; less than 0 inside.
float outside(vec2 point, vec2 half_size, float radius) {
    vec2 corner = abs(point) - half_size + radius;
    return length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - radius;
}

void main() {
    const vec3 frame = vec3(251.0) / 255.0;  // the game's white, as its text boxes'
    const vec3 fill = vec3(170.0) / 255.0;
    const vec3 empty = vec3(6.0, 6.0, 12.0) / 255.0;
    const float opacity = 0.85;
    const int kTriangle = 3;  // the mark's triangles: this wide, as high as twice that less one
    vec2 pixel = floor(at);  // the original's pixel this is in: judged by its middle
    vec2 half_size = size.xy * 0.5;
    vec2 point = pixel + 0.5 - half_size;
    float line = size.w;
    if (outside(point, half_size, size.z) > 0.0) {
        discard;
    }
    bool on_frame = outside(point, half_size - line, max(size.z - line, 0.0)) > 0.0;
    // Whole columns of the inside, as many as are left; any of it at least one.
    bool filled = pixel.x - line < ceil(left.x * (size.x - (2.0 * line)));
    // The mark, in the middle: a triangle's point at its left, half way down.
    ivec2 mark = ivec2(pixel) - ivec2((int(size.x) / 2) - kTriangle, int(half_size.y) - (kTriangle - 1));
    bool on_mark = mark.x >= 0 && mark.x < 2 * kTriangle && mark.y >= 0 && mark.y < (2 * kTriangle) - 1 &&
                   mark.x % kTriangle >= abs(mark.y - (kTriangle - 1));
    vec3 inside = filled != on_mark ? fill : empty;
    colour = vec4(on_frame ? frame : inside, opacity * left.y);
}
