#version 450
// A kept picture (or a part of it) onto what is shown, at any size: no vertices are given, the six of one
// rectangle are made here.
layout(location = 0) out vec2 at;
layout(set = 1, binding = 0) uniform Shown {
    vec4 dest;    // on the target: left, top, width, height
    vec4 source;  // of the picture: left, top, width, height
    vec2 size;    // the target's
};

void main() {
    const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    vec2 corner = corners[gl_VertexIndex];
    vec2 on_target = dest.xy + (corner * dest.zw);
    at = source.xy + (corner * source.zw);
    gl_Position = vec4((on_target.x / size.x * 2.0) - 1.0, 1.0 - (on_target.y / size.y * 2.0), 0.0, 1.0);
}
