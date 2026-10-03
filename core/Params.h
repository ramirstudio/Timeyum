#pragma once

#include <vector>

namespace timeyum {

enum Profile { kProfileFade = 0, kProfileExponential = 1, kProfileCamera = 2, kProfileCurve = 3 };
enum EdgeMode { kEdgeWrap = 0, kEdgeExtend = 1, kEdgeMirror = 2, kEdgeBlack = 3 };
enum BlendMode { kBlendExposure = 0, kBlendAdd = 1, kBlendScreen = 2, kBlendLighten = 3 };
enum ColorSpace { kCsLinear = 0, kCsSrgb = 1, kCsGamma24 = 2, kCsLogC3 = 3, kCsSLog3 = 4 };
enum WarpView { kViewResult = 0, kViewLength = 1, kViewReaction = 2, kViewDisplacement = 3 };
enum ControlChannel { kCtlOff = 0, kCtlLuma = 1, kCtlAlpha = 2, kCtlRed = 3, kCtlGreen = 4, kCtlBlue = 5, kCtlDepth = 6 };

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

    // Warp: streak length and a soft displacement that vary across the frame, driven by an
    // animated flow, by the video itself (brightness, motion) and by interactive points.
    bool warp = false;
    double warp_amount = 1.0;         // master gain of everything below
    double flow_length = 0.6;         // flow modulation of the streak length (+-60%)
    double flow_wave = 0.012;         // flow displacement amplitude, fraction of the frame height
    double flow_scale = 0.35;         // feature size of the flow, fraction of the frame height
    double flow_speed = 0.25;         // flow evolution, lattice units per second
    int flow_detail = 3;              // octaves
    int flow_seed = 7;
    double drift_angle = 90.0;        // direction the flow pattern travels, same convention as angle
    double drift_speed = 0.0;         // frame heights per second
    double luma_response = 0.5;       // reaction to brightness
    double luma_softness = 0.04;      // blur of the reaction, fraction of the frame height
    double motion_response = 0.5;     // reaction to frame to frame change
    double motion_sensitivity = 4.0;
    double inertia = 0.6;             // memory of past frames in the reaction, 0..1
    int history = 4;                  // past frames the reaction may look at
    double length_reaction = 0.8;     // how much the reaction lengthens the streaks
    double wave_reaction = 0.7;       // how much the reaction gates the flow displacement
    double pull_x = 0.5, pull_y = 0.5;  // interactive point, fractions of width and height
    double pull_strength = 0.3;       // signed: > 0 bulges the image around the point, < 0 pinches it
    double pull_radius = 0.45;        // fraction of the frame height
    double pull_length = 0.5;         // streak lengthening around the point
    double auto_strength = 0.0;       // signed pull toward the brightest area of the video
    double base_follow = 0.25;        // share of the displacement applied to the sharp image
    int warp_levels = 6;              // streak length steps blended per pixel (cost grows with it)
    int warp_view = kViewResult;

    // Control input: a matte, a mask, an alpha channel or a depth map that steers the effect. The caller
    // supplies the picture; control says which channel of it is read.
    int control = kCtlOff;
    bool control_invert = false;
    double control_black = 0.0, control_white = 1.0;  // levels applied to the channel
    double control_near = 0.0, control_far = 100.0;   // Z-Depth: the distances that map to 1 and to 0
    double control_softness = 0.0;                    // blur of the control, in pixels
    double control_matte = 1.0;                       // 1: the effect shows only where the control is white
    double control_emit = 0.0;                        // 1: streaks come only from where the control is white
    double control_length = 0.0;                      // 1: the streak length follows the control
    double control_warp = 0.0;                        // 1: the control drives the warp, like the reaction to the video
    bool control_view = false;                        // show the mapped control instead of the result

    // Set by resolveShake
    double weave_x = 0.0;
    double weave_y = 0.0;
    int breakup_seed = 1234;

    // Host resolution factor (0.5 at half resolution): scales every pixel quantity.
    double pixel_scale = 1.0;
};

}  // namespace timeyum
