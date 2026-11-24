#version 450
// The mark that time is running backwards: two triangles pointing left, side by side, in one colour.
layout(location = 0) in vec2 at;  // across: 0 to 2, a triangle to each 1; down: 0 to 1
layout(location = 0) out vec4 colour;
layout(set = 3, binding = 0) uniform Mark {
    vec4 tint;
};

void main() {
    // A triangle's point is at its left, half way down; it is as high as it is far from there.
    float inside = (fract(at.x) * 0.5) - abs(at.y - 0.5);
    float covered = clamp((inside / fwidth(inside)) + 0.5, 0.0, 1.0);  // its edge over a pixel
    colour = vec4(tint.rgb, tint.a * covered);
}
