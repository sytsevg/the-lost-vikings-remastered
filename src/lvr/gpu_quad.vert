#version 450
// One picture drawn onto the target: a rectangle of the atlas, a pixel for a pixel. An instance is a picture; the
// six vertices are its two triangles.
layout(location = 0) in vec4 dest;    // on the target: left, top, width, height
layout(location = 1) in vec4 source;  // in the atlas: left, top; then mirrored, flipped (0 or 1)
layout(location = 2) in vec4 how;     // the mask's offset from the picture in the atlas (x, y); colour 0 is
                                      // see-through; masked (0 or 1)
layout(location = 3) in vec4 placed;  // its place in the picture's order (x), from 1
layout(location = 0) out vec2 at;
layout(location = 1) flat out vec4 how_drawn;
layout(location = 2) flat out uint order_drawn;
layout(set = 1, binding = 0) uniform Target {
    vec2 size;
};

void main() {
    const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    vec2 within = corners[gl_VertexIndex] * dest.zw;
    vec2 on_target = dest.xy + within;
    if (source.z > 0.5) {
        within.x = dest.z - within.x;
    }
    if (source.w > 0.5) {
        within.y = dest.w - within.y;
    }
    at = source.xy + within;
    how_drawn = how;
    order_drawn = uint(placed.x);
    gl_Position = vec4((on_target.x / size.x * 2.0) - 1.0, 1.0 - (on_target.y / size.y * 2.0), 0.0, 1.0);
}
