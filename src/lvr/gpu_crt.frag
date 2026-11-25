#version 450
// The screen's look (Remaster rules, gpu.hpp): what was shown, as a television's tube would show it. The picture is
// taken line by line, a line for each of its rows as shown (240 at the original's view, more zoomed out): a beam
// draws each line across the glass, as tall as it is bright, and softened along the line as a beam spot is; the
// glass's mask of red, green and blue lets each colour through where its phosphor is; light spreads a little around
// what is bright; the glass is flat, its edges a little darker. Worked out in linear light, as the tube's light adds
// up. Where the lines are too fine for the window's pixels to show the gaps between them, they close up.
layout(location = 0) in vec2 at;  // in the source, its pixels
layout(location = 0) out vec4 colour;
layout(set = 2, binding = 0) uniform sampler2D source;
layout(set = 3, binding = 0) uniform Look {
    vec4 screen;  // the screen's part of the source: left, top, width, height
    vec4 beam;    // the beam's height on a line (in lines) at its darkest and at its brightest; how soft along the
                  // line (in the picture's pixels); how much light spreads
    vec4 tube;    // its lines; how much the mask holds back; how much brighter it is all made for what the lines
                  // and the mask take; how much darker its corners are
};

const float kGamma = 2.2;

vec3 linear_at(vec2 spot) {  // in the source's pixels
    return pow(texture(source, spot / vec2(textureSize(source, 0))).rgb, vec3(kGamma));
}

// The source's pixels a pixel of the picture (and a line) is high.
float row_height() { return screen.w / tube.x; }

// The colour a line has at `x` (of the screen, 0 to 1): the picture's rows the line covers (one, when the picture
// lies on the lines; two mixed, while it is between them), softened along it.
vec3 line_colour(float line, float x) {
    float along = screen.x + (x * screen.z);
    float soft = beam.z * row_height();  // in the source's pixels
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int part = 0; part < 2; ++part) {
        // A quarter and three quarters down the line, at the source's row there (never mixed with the next).
        float row = floor(screen.y + ((line + 0.25 + (0.5 * float(part))) * row_height())) + 0.5;
        for (int tap = -2; tap <= 2; ++tap) {
            float weight = exp(-0.5 * float(tap * tap));
            sum += weight * linear_at(vec2(along + (float(tap) * soft), row));
            weights += weight;
        }
    }
    return sum / weights;
}

// How much of a line's light falls `distance` (in lines) from its middle: its beam is taller where it is brighter,
// and as tall as at its brightest where the lines are `closed` (1: too fine for gaps between them).
vec3 beam_light(vec3 line, float distance, float closed) {
    vec3 height = mix(vec3(mix(beam.x, beam.y, closed)), vec3(beam.y), sqrt(clamp(line, 0.0, 1.0)));
    return line * exp(-0.5 * (distance * distance) / (height * height));
}

void main() {
    vec2 spot = clamp((at - screen.xy) / screen.zw, 0.0, 1.0);  // on the glass, 0 to 1 across and down
    // The lines: this one and the ones beside it.
    float down = (spot.y * tube.x) - 0.5;
    float line = floor(down);
    float into = down - line;
    // The window's pixels a line is high: from four the gaps show fully, at two and less they close.
    float shown_height = 1.0 / max(abs(dFdy(down)), 1e-4);
    float closed = 1.0 - clamp((shown_height - 2.0) / 2.0, 0.0, 1.0);
    vec3 light = vec3(0.0);
    for (int beside = -1; beside <= 2; ++beside) {
        float which = line + float(beside);
        if (which >= 0.0 && which < tube.x) {
            light += beam_light(line_colour(which, spot.x), into - float(beside), closed);
        }
    }
    // Light spread around what is bright: a ring of the picture a few of its pixels away.
    if (beam.w > 0.0) {
        vec3 around = vec3(0.0);
        vec2 reach = vec2(2.5 * row_height());  // the picture's pixels
        for (int step = 0; step < 8; ++step) {
            float angle = float(step) * 0.785398;
            vec2 offset = vec2(cos(angle), sin(angle)) * reach;
            around += linear_at(screen.xy + (spot * screen.zw) + offset);
            around += linear_at(screen.xy + (spot * screen.zw) + (offset * 0.5));
        }
        light += beam.w * (around / 16.0);
    }
    // The mask, at the window's own pixels: red, green, blue in turn across, in slots broken every four rows,
    // staggered from one group of three to the next.
    ivec2 window = ivec2(gl_FragCoord.xy);
    vec3 through = vec3(1.0 - tube.y);
    through[window.x % 3] = 1.0;
    if (((window.y + (((window.x / 3) % 2) * 2)) % 4) == 0) {
        through *= 1.0 - (0.5 * tube.y);
    }
    light *= through * tube.z;
    // Darker towards the corners.
    vec2 from_middle = (spot * 2.0) - 1.0;
    light *= 1.0 - (tube.w * pow(dot(from_middle, from_middle) * 0.5, 1.5));
    colour = vec4(pow(clamp(light, 0.0, 1.0), vec3(1.0 / kGamma)), 1.0);
}
