#version 450
// The screen's look xbr (gpu.hpp): the finished picture, drawn in whole pixels of the game, made larger with its
// stair-steps rounded off and their edges blended by Hyllian's xBR rules. The source is what was shown, with
// the picture's pixels `grid.z` of its pixels wide from `grid.xy`; the target has as many pixels as the source.
//
// Adapted from Hyllian's xBR-lv2 shader (libretro glsl-shaders, xbr/shaders/xbr-lv2.glsl): the same rules
// (corner rule C, smooth tips, small details not kept), the pixels found on the picture's grid, and the blending
// as wide as a pixel of the target. Its notice:
//
//   Hyllian's xBR-lv2 Shader
//
//   Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//   THE SOFTWARE.
//
//   Incorporates some of the ideas from SABR shader. Thanks to Joshua Street.
layout(location = 0) in vec2 at;  // in the source, its pixels
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform sampler2D source;
layout(set = 3, binding = 0) uniform Grid {
    vec4 screen;  // the screen's part of the source: left, top, width, height (black around it)
    vec4 grid;    // where the picture's first pixel starts (x, y); the source's pixels a pixel of it; unused
};

const float kEqThreshold = 15.0;
const float kLv2Coefficient = 2.0;
const vec3 kWeights = vec3(14.352, 28.176, 5.472);

// A pixel of the picture, by its place on the grid: the source at its middle, kept within the screen.
vec3 pixel_at(vec2 p) {
    vec2 spot = clamp(grid.xy + ((p + 0.5) * grid.z), screen.xy + 0.5, screen.xy + screen.zw - 0.5);
    return texelFetch(source, ivec2(spot), 0).rgb;
}

vec4 df(vec4 a, vec4 b) { return abs(a - b); }
vec4 diff(vec4 a, vec4 b) { return vec4(notEqual(a, b)); }
vec4 eq(vec4 a, vec4 b) { return step(df(a, b), vec4(kEqThreshold)); }
vec4 neq(vec4 a, vec4 b) { return vec4(1.0) - eq(a, b); }
vec4 wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
    return df(a, b) + df(a, c) + df(d, e) + df(d, f) + 4.0 * df(g, h);
}
float c_df(vec3 c1, vec3 c2) {
    vec3 d = abs(c1 - c2);
    return d.r + d.g + d.b;
}

void main() {
    if (any(lessThan(at, screen.xy)) || any(greaterThanEqual(at, screen.xy + screen.zw))) {
        colour = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec2 on_grid = (at - grid.xy) / grid.z;
    vec2 p = floor(on_grid);
    vec2 fp = on_grid - p;

    vec3 A1 = pixel_at(p + vec2(-1, -2)), B1 = pixel_at(p + vec2(0, -2)), C1 = pixel_at(p + vec2(1, -2));
    vec3 A = pixel_at(p + vec2(-1, -1)), B = pixel_at(p + vec2(0, -1)), C = pixel_at(p + vec2(1, -1));
    vec3 D = pixel_at(p + vec2(-1, 0)), E = pixel_at(p), F = pixel_at(p + vec2(1, 0));
    vec3 G = pixel_at(p + vec2(-1, 1)), H = pixel_at(p + vec2(0, 1)), I = pixel_at(p + vec2(1, 1));
    vec3 G5 = pixel_at(p + vec2(-1, 2)), H5 = pixel_at(p + vec2(0, 2)), I5 = pixel_at(p + vec2(1, 2));
    vec3 A0 = pixel_at(p + vec2(-2, -1)), D0 = pixel_at(p + vec2(-2, 0)), G0 = pixel_at(p + vec2(-2, 1));
    vec3 C4 = pixel_at(p + vec2(2, -1)), F4 = pixel_at(p + vec2(2, 0)), I4 = pixel_at(p + vec2(2, 1));

    vec4 b = vec4(dot(B, kWeights), dot(D, kWeights), dot(H, kWeights), dot(F, kWeights));
    vec4 c = vec4(dot(C, kWeights), dot(A, kWeights), dot(G, kWeights), dot(I, kWeights));
    vec4 d = b.yzwx;
    vec4 e = vec4(dot(E, kWeights));
    vec4 f = b.wxyz;
    vec4 g = c.zwxy;
    vec4 h = b.zwxy;
    vec4 i = c.wxyz;
    vec4 i4 = vec4(dot(I4, kWeights), dot(C1, kWeights), dot(A0, kWeights), dot(G5, kWeights));
    vec4 i5 = vec4(dot(I5, kWeights), dot(C4, kWeights), dot(A1, kWeights), dot(G0, kWeights));
    vec4 h5 = vec4(dot(H5, kWeights), dot(F4, kWeights), dot(B1, kWeights), dot(D0, kWeights));
    vec4 f4 = h5.yzwx;

    // The blending is a pixel of the target wide.
    float scale = grid.z;
    vec4 delta = vec4(1.0 / scale);
    vec4 delta_l = vec4(0.5 / scale, 1.0 / scale, 0.5 / scale, 1.0 / scale);
    vec4 delta_u = delta_l.yxwz;

    const vec4 Ao = vec4(1.0, -1.0, -1.0, 1.0);
    const vec4 Bo = vec4(1.0, 1.0, -1.0, -1.0);
    const vec4 Co = vec4(1.5, 0.5, -0.5, 0.5);
    const vec4 Ax = vec4(1.0, -1.0, -1.0, 1.0);
    const vec4 Bx = vec4(0.5, 2.0, -0.5, -2.0);
    const vec4 Cx = vec4(1.0, 1.0, -0.5, 0.0);
    const vec4 Ay = vec4(1.0, -1.0, -1.0, 1.0);
    const vec4 By = vec4(2.0, 0.5, -2.0, -0.5);
    const vec4 Cy = vec4(2.0, 0.0, -1.0, 0.5);
    const vec4 Ci = vec4(0.25);

    // The lines below which a pixel is blended with a neighbour.
    vec4 fx = Ao * fp.y + Bo * fp.x;
    vec4 fx_l = Ax * fp.y + Bx * fp.x;
    vec4 fx_u = Ay * fp.y + By * fp.x;

    vec4 irlv0 = diff(e, f) * diff(e, h);
    vec4 irlv1 = irlv0 * (neq(f, b) * neq(f, c) + neq(h, d) * neq(h, g) +
                          eq(e, i) * (neq(f, f4) * neq(f, i4) + neq(h, h5) * neq(h, i5)) + eq(e, g) + eq(e, c));
    vec4 irlv2l = diff(e, g) * diff(d, g);
    vec4 irlv2u = diff(e, c) * diff(b, c);

    vec4 fx45i = clamp((fx + delta - Co - Ci) / (2.0 * delta), 0.0, 1.0);
    vec4 fx45 = clamp((fx + delta - Co) / (2.0 * delta), 0.0, 1.0);
    vec4 fx30 = clamp((fx_l + delta_l - Cx) / (2.0 * delta_l), 0.0, 1.0);
    vec4 fx60 = clamp((fx_u + delta_u - Cy) / (2.0 * delta_u), 0.0, 1.0);

    vec4 wd1 = wd(e, c, g, i, h5, f4, h, f);
    vec4 wd2 = wd(h, d, i5, f, i4, b, e, i);

    vec4 edri = step(wd1, wd2) * irlv0;
    vec4 edr = step(wd1 + vec4(0.1), wd2) * step(vec4(0.5), irlv1);
    vec4 edr_l = step(kLv2Coefficient * df(f, g), df(h, c)) * irlv2l * edr;
    vec4 edr_u = step(kLv2Coefficient * df(h, c), df(f, g)) * irlv2u * edr;

    fx45 = edr * fx45;
    fx30 = edr_l * fx30;
    fx60 = edr_u * fx60;
    fx45i = edri * fx45i;

    vec4 px = step(df(e, f), df(e, h));
    vec4 most = max(max(fx30, fx60), max(fx45, fx45i));

    vec3 res1 = E;
    res1 = mix(res1, mix(H, F, px.x), most.x);
    res1 = mix(res1, mix(B, D, px.z), most.z);
    vec3 res2 = E;
    res2 = mix(res2, mix(F, B, px.y), most.y);
    res2 = mix(res2, mix(D, H, px.w), most.w);
    colour = vec4(mix(res1, res2, step(c_df(E, res1), c_df(E, res2))), 1.0);
}
