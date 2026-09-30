#pragma once

#include <vector>

namespace timeyum {

enum Profile { kProfileFade = 0, kProfileExponential = 1, kProfileCamera = 2, kProfileCurve = 3 };
enum EdgeMode { kEdgeWrap = 0, kEdgeExtend = 1, kEdgeMirror = 2, kEdgeBlack = 3 };
enum BlendMode { kBlendExposure = 0, kBlendAdd = 1, kBlendScreen = 2, kBlendLighten = 3 };
enum ColorSpace { kCsLinear = 0, kCsSrgb = 1, kCsGamma24 = 2, kCsLogC3 = 3, kCsSLog3 = 4 };

// Percentages are fractions (0.35 = 35%), angles are degrees. Lengths and offsets
// are fractions of the frame height unless the name ends in _px.
struct Params {
    // Output
    double opacity = 1.0;
    int blend = kBlendExposure;
    int colorspace = kCsLinear;
    bool affect_alpha = true;

    // Streak geometry. angle 90 = streak goes up, 0 = right.
    int profile = kProfileFade;
    double length = 0.6;
    double angle = 90.0;
    bool symmetric = false;
    double back_length = 0.0;
    double start_offset = 0.0;
    int edge = kEdgeWrap;
    double frame_gap = 0.0;

    // Profile shape
    double falloff = 0.55;
    double falloff_curve = 1.0;
    double decay = 4.0;
    std::vector<double> curve;  // density samples from start to tip, used by kProfileCurve

    // Camera (timing shift) model, degrees of the 360 degree film cycle
    double timing_shift = 90.0;
    double shutter_angle = 180.0;
    double pulldown_angle = 180.0;
    double claw_ease = 1.0;

    // Exposure
    double smear = 0.35;
    double gain = 1.0;
    double threshold = 0.0;
    double knee = 0.5;
    double cleanup = 0.0;  // pixels

    // Ghost echoes
    bool ghost = false;
    int ghost_count = 1;
    double ghost_offset = 0.12;
    double ghost_strength = 0.25;
    double ghost_decay = 0.5;
    double ghost_length = 0.01;

    // Frame displacement
    double roll = 0.0;
    double roll_bar = 0.0;
    double roll_bar_soft = 0.3;

    // Smear colour and texture
    double tint[3] = {1.0, 1.0, 1.0};
    double saturation = 1.0;
    double chroma = 0.0;
    double breakup = 0.0;
    double breakup_scale = 6.0;  // pixels

    // Shake
    bool shake = false;
    double shake_amount = 1.0;
    double shake_freq = 12.0;  // new random values per second
    double shake_smooth = 0.3;
    int shake_seed = 1234;
    double shake_length = 0.35;
    double shake_angle = 0.0;
    double shake_smear = 0.25;
    double shake_timing = 20.0;
    double shake_roll = 0.0;
    double shake_weave_x = 0.0;  // pixels
    double shake_weave_y = 1.5;  // pixels
    double shake_ghost = 0.0;

    // Set by resolveShake
    double weave_x = 0.0;
    double weave_y = 0.0;
    int breakup_seed = 1234;

    // Host resolution factor (0.5 at half resolution): scales every pixel quantity.
    double pixel_scale = 1.0;
};

}  // namespace timeyum
