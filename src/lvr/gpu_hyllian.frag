#version 450
// The screen's look crt (gpu.hpp): the finished picture, drawn in whole pixels of the game, as Hyllian's CRT shader
// shows it with its own settings. Each of the picture's rows is a scanline whose beam is wider where it is brighter,
// softened along the row (a Catmull-Rom curve, kept from ringing past its two nearest pixels), all in linear light;
// then an aperture grille's mask in the window's pixels, a column magenta and the next green, the lit parts
// brightened by it. Zoomed out the scanlines are the game's rows as at the original's view: not cut off past 400 rows
// as in Hyllian's (the user: twice the view was brighter without them); where a row is less than kFewest of the
// window's pixels high, a window's pixel shows the beam as it is over the whole of it, so the lines don't beat
// against the window's (and it is as bright). The source is what was shown, with the picture's pixels `grid.z` of
// its pixels wide from `grid.xy`, as xbr's.
//
// Adapted from Hyllian's CRT shader (libretro slang-shaders, crt/crt-hyllian.slangp): its main pass
// (crt/shaders/hyllian/crt-hyllian-base.slang) and the mask of its last (support/glow/blur-glow-mask-geom.slang), in
// one pass, with the preset's settings. Left out: its colour grading (a table of a Sony television's colours, on in
// the preset: the game keeps its own), and its glow and curvature (off in the preset). Its notice:
//
//   Hyllian's CRT Shader
//
//   Copyright (C) 2011-2024 Hyllian
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
layout(location = 0) in vec2 at;  // in the source, its pixels
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform sampler2D source;
layout(set = 3, binding = 0) uniform Grid {
    vec4 screen;  // the screen's part of the source: left, top, width, height (black around it)
    vec4 grid;    // where the picture's first pixel starts (x, y); the source's pixels a pixel of it; unused
};

const float kInputGamma = 2.4;
const float kOutputGamma = 2.2;
const float kBeamMinWidth = 0.72;
const float kBeamMaxWidth = 1.0;
const float kScanlinesStrength = 0.72 - 0.16;  // its strength, less with the soft shape (the gaussian below)
const float kFewest = 3.0;
const float kMaskStrength = 1.0;
const float kMaskGamma = 2.4;

// The horizontal cubic filter, B = 0 and C = 0.5: Catmull-Rom.
const float B = 0.0;
const float C = 0.5;
const mat4 kInvX = mat4((-B - 6.0 * C) / 6.0, (3.0 * B + 12.0 * C) / 6.0, (-3.0 * B - 6.0 * C) / 6.0, B / 6.0,
                        (12.0 - 9.0 * B - 6.0 * C) / 6.0, (-18.0 + 12.0 * B + 6.0 * C) / 6.0, 0.0, (6.0 - 2.0 * B) / 6.0,
                        -(12.0 - 9.0 * B - 6.0 * C) / 6.0, (18.0 - 15.0 * B - 12.0 * C) / 6.0, (3.0 * B + 6.0 * C) / 6.0,
                        B / 6.0, (B + 6.0 * C) / 6.0, -C, 0.0, 0.0);

// A pixel of the picture, by its place on the grid, in linear light: the source at its middle, within the screen.
vec3 pixel_at(vec2 p) {
    vec2 spot = clamp(grid.xy + ((p + 0.5) * grid.z), screen.xy + 0.5, screen.xy + screen.zw - 0.5);
    return pow(texelFetch(source, ivec2(spot), 0).rgb, vec3(kInputGamma));
}

// The scanlines at `on_grid` (in the picture's pixels, from the first one's middle): this row's beam and the
// next's, each as wide as it is bright, softened along the row; in linear light.
vec3 scanned(vec2 on_grid) {
    vec2 p = floor(on_grid);
    vec2 fp = on_grid - p;
    vec3 c00 = pixel_at(p + vec2(-1, 0)), c01 = pixel_at(p), c02 = pixel_at(p + vec2(1, 0)),
         c03 = pixel_at(p + vec2(2, 0));
    vec3 c10 = pixel_at(p + vec2(-1, 1)), c11 = pixel_at(p + vec2(0, 1)), c12 = pixel_at(p + vec2(1, 1)),
         c13 = pixel_at(p + vec2(2, 1));
    vec4 weights = vec4(fp.x * fp.x * fp.x, fp.x * fp.x, fp.x, 1.0) * kInvX;
    vec3 colour0 = clamp(mat4x3(c00, c01, c02, c03) * weights, min(c01, c02), max(c01, c02));
    vec3 colour1 = clamp(mat4x3(c10, c11, c12, c13) * weights, min(c11, c12), max(c11, c12));
    vec3 width0 = mix(vec3(kBeamMinWidth), vec3(kBeamMaxWidth), colour0);
    vec3 width1 = mix(vec3(kBeamMinWidth), vec3(kBeamMaxWidth), colour1);
    vec3 d0 = kScanlinesStrength * fp.y / ((width0 * width0) + 0.0000001);
    vec3 d1 = kScanlinesStrength * (1.0 - fp.y) / ((width1 * width1) + 0.0000001);
    return (colour0 * exp(-16.0 * d0 * d0)) + (colour1 * exp(-16.0 * d1 * d1));
}

void main() {
    if (any(lessThan(at, screen.xy)) || any(greaterThanEqual(at, screen.xy + screen.zw))) {
        colour = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec2 on_grid = ((at - grid.xy) / grid.z) - 0.5;  // in the picture's pixels, from the first one's middle
    vec3 crt;
    if (grid.z >= kFewest) {
        crt = scanned(on_grid);
    } else {  // over the window's pixel: at a quarter of it either side of its middle, and at its middle
        float quarter = 0.25 / grid.z;
        vec3 above = scanned(on_grid - vec2(0.0, quarter));
        vec3 below = scanned(on_grid + vec2(0.0, quarter));
        crt = (above + (2.0 * scanned(on_grid)) + below) / 4.0;
    }
    crt = pow(clamp(crt, 0.0, 1.0), vec3(1.0 / kOutputGamma));

    // The aperture grille, a column of the window magenta and the next green.
    vec3 mask = mod(floor(at.x), 2.0) < 1.0 ? vec3(1.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    crt = mask + (1.0 - 2.0 * mask) * pow(abs(mask - crt), mask * kMaskStrength * (kMaskGamma - 1.0) + 1.0);
    colour = vec4(crt, 1.0);
}
