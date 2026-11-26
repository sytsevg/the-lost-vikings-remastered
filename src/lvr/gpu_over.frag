#version 450
// The minimap laid over what is shown: a square of its picture, smoothly scaled, with the Vikings on it and a frame
// around it, its corners rounded off in the original's pixels as the rewind's bar's. The picture has the whole map, and in its alpha what of it was seen; what the view shows now is seen
// too, to the exact edge.
layout(location = 0) in vec2 at;  // in the minimap's pixels; outside it where the square reaches past the level
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform sampler2D picture;
layout(set = 3, binding = 0) uniform Over {
    vec4 size;        // the picture's width and height; how wide a line is, in its pixels; how much it is faded in
    vec4 window;      // the square that is shown: left, top, right, bottom
    vec4 view;        // what the view shows now, the same way
    vec4 vikings[3];  // Erik, Baleog, Olaf: where (x, y); there at all; the one being played
    vec4 dark;        // the levels a picture's fade takes off its colours, of all of them (as many off these)
};

// How much of the pixel at `point` is in the box: 1 inside, 0 outside, between at its edge.
float inside(vec2 point, vec4 box) {
    vec2 from = clamp(point - box.xy + 0.5, 0.0, 1.0);
    vec2 to = clamp(box.zw - point + 0.5, 0.0, 1.0);
    return from.x * from.y * to.x * to.y;
}

// How far `point` is outside a box of `half_size` around (0, 0) with corners of radius `radius`; less than 0 inside.
float outside(vec2 point, vec2 half_size, float radius) {
    vec2 corner = abs(point) - half_size + radius;
    return length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - radius;
}

void main() {
    const vec3 unseen = vec3(6.0, 6.0, 12.0) / 255.0;
    const vec3 colours[3] = vec3[](vec3(240.0, 84.0, 60.0) / 255.0, vec3(96.0, 220.0, 96.0) / 255.0,
                                   vec3(250.0, 216.0, 72.0) / 255.0);
    const vec2 half_viking = vec2(2.0, 3.0);  // 16 x 24 of the level
    float line = size.z;
    bool in_level = at.x >= 0.0 && at.y >= 0.0 && at.x < size.x && at.y < size.y;
    // Sharp, but without the shimmer of picking the nearest pixel: each of the picture's pixels keeps its colour
    // right up to its edge, and only the edge itself, as wide as one pixel of the screen, is mixed.
    vec2 nearest = floor(at + 0.5);
    vec2 sharp = nearest + clamp((at - nearest) / fwidth(at), -0.5, 0.5);
    vec4 map = texture(picture, sharp / size.xy);
    float seen = in_level ? max(map.a, inside(at, view)) : 0.0;
    vec3 shown = mix(unseen, map.rgb, seen);
    for (int who = 0; who < 3; ++who) {
        vec4 viking = vikings[who];
        if (viking.z > 0.5) {
            vec4 box = vec4(viking.xy - half_viking, viking.xy + half_viking);
            if (viking.w > 0.5) {
                float rim = line * 0.4;  // a thin one
                shown = mix(shown, vec3(1.0), inside(at, box + vec4(-rim, -rim, rim, rim)));
            }
            shown = mix(shown, colours[who], inside(at, box));
        }
    }
    // The frame and its corners in the original's pixels (`line` of the minimap's each), each one colour, judged by
    // its middle: two pixels cut off each corner, as the rewind's bar's.
    const float radius = 3.5;
    vec2 half_square = (window.zw - window.xy) / line * 0.5;
    vec2 pixel = floor((at - window.xy) / line) + 0.5 - half_square;
    if (outside(pixel, half_square, radius) > 0.0) {
        discard;
    }
    bool framed = outside(pixel, half_square - 1.0, radius - 1.0) > 0.0;
    colour = (framed ? vec4(0.59, 0.60, 0.67, 1.0) : vec4(shown, 0.94)) * vec4(1.0, 1.0, 1.0, size.w);
    colour.rgb = max(colour.rgb - vec3(dark.x), vec3(0.0));  // (as the picture's own colours lose them)
}
