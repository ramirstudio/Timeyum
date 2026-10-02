// After Effects SmartFX wrapper around the Timeyum core.
//
// NOT compiled or run in the environment this was written in: it needs the Adobe After Effects
// SDK. Expect to fix small API mismatches against the SDK version you build with.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "Smart_Utils.h"
#include "AEFX_SuiteHelper.h"
#include "AEGP_SuiteHandler.h"
#ifndef TIMEYUM_MOCK_SDK
#include "AE_EffectUI.h"
#include "adobesdk/DrawbotSuite.h"
#include "SPBasic.h"
#ifdef AE_OS_WIN
#include <windows.h>
#endif
#endif

#include "Timeyum.h"
#include "TimeyumFlags.h"
#include "TimeyumParams.h"

#ifdef TIMEYUM_BANNER
static_assert(TY_OUT_FLAGS == (PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_SEND_UPDATE_PARAMS_UI | PF_OutFlag_CUSTOM_UI),
              "TY_OUT_FLAGS does not match the SDK flags, fix TimeyumFlags.h");
#else
static_assert(TY_OUT_FLAGS == (PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_SEND_UPDATE_PARAMS_UI),
              "TY_OUT_FLAGS does not match the SDK flags, fix TimeyumFlags.h");
#endif
static_assert(TY_OUT_FLAGS2 == (PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_THREADED_RENDERING),
              "TY_OUT_FLAGS2 does not match the SDK flags, fix TimeyumFlags.h");
static_assert(TY_VERSION_VALUE == PF_VERSION(TY_VERSION_MAJOR, TY_VERSION_MINOR, TY_VERSION_BUG, PF_Stage_RELEASE, 0),
              "TY_VERSION_VALUE does not match PF_VERSION, fix TimeyumFlags.h");

namespace {

using timeyum::Image;
using timeyum::Params;

// ---------------------------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------------------------

constexpr float kMax16 = 32768.0f;  // PF_MAX_CHAN16

#ifdef TIMEYUM_BANNER
constexpr int kBannerUiWidth = 200;  // hint only: the banner is drawn across the whole row
constexpr int kBannerUiHeight = 160;
#endif

PF_Err addParams(PF_InData* in_data, PF_OutData* out_data) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

#ifdef TIMEYUM_BANNER
    // Banner: a parameter without data whose only job is to be drawn (see drawBanner).
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_NO_DATA;
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
    def.ui_flags = PF_PUI_CONTROL;
    def.ui_width = kBannerUiWidth;
    def.ui_height = kBannerUiHeight;
    PF_STRCPY(def.PF_DEF_NAME, " ");
    def.uu.id = ID_BANNER;
    if (const PF_Err e = (*in_data->inter.add_param)(in_data->effect_ref, -1, &def)) return e;
    // A custom UI must be registered (as in the SDK's Custom_ECW_UI sample); without this After
    // Effects crashes when it builds the panel. Only the Effect Controls panel is used.
    {
        PF_CustomUIInfo ci;
        AEFX_CLR_STRUCT(ci);
        ci.events = PF_CustomEFlag_EFFECT;
        ci.comp_ui_width = ci.comp_ui_height = 0;
        ci.comp_ui_alignment = PF_UIAlignment_NONE;
        ci.layer_ui_width = ci.layer_ui_height = 0;
        ci.layer_ui_alignment = PF_UIAlignment_NONE;
        ci.preview_ui_width = ci.preview_ui_height = 0;
        ci.preview_ui_alignment = PF_UIAlignment_NONE;
        if (const PF_Err e = (*in_data->inter.register_ui)(in_data->effect_ref, &ci)) return e;
    }
#endif

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_OPACITY);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Streak Model", 3, 1, "Fade|Exponential|Camera (Timing Shift)", PF_ParamFlag_SUPERVISE, ID_PROFILE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Streak", ID_STREAK_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Length", 0, 400, 0, 200, 60, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_LENGTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Angle", 90, ID_ANGLE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Symmetric", FALSE, PF_ParamFlag_SUPERVISE, ID_SYMMETRIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Back Length", 0, 200, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_BACK_LENGTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Start Offset", 0, 100, 0, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_START_OFFSET);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Edge Behavior", 4, 1, "Wrap-Around|Extend|Mirror|Black", PF_ParamFlag_SUPERVISE, ID_EDGE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Frame Line Gap", 0, 50, 0, 10, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_FRAME_GAP);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_STREAK_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Profile", ID_PROFILE_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Falloff", 0, 100, 0, 100, 55, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_FALLOFF);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Falloff Curve", 0.05, 20, 0.1, 5, 1, PF_Precision_HUNDREDTHS, 0, 0, ID_FALLOFF_CURVE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Decay", 0, 50, 0, 15, 4, PF_Precision_TENTHS, 0, 0, ID_DECAY);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_PROFILE_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Camera", ID_CAMERA_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Timing Shift", 90, ID_TIMING_SHIFT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shutter Angle", 1, 360, 1, 360, 180, PF_Precision_TENTHS, 0, 0, ID_SHUTTER_ANGLE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Pull-down Angle", 1, 359, 30, 330, 180, PF_Precision_TENTHS, 0, 0, ID_PULLDOWN_ANGLE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Claw Ease", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_CLAW_EASE);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_CAMERA_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Exposure", ID_EXPOSURE_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smear Amount", 0, 100, 0, 100, 35, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SMEAR);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smear Gain", 0, 100, 0, 5, 1, PF_Precision_HUNDREDTHS, 0, 0, ID_GAIN);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Blend", 4, 1, "Exposure|Add|Screen|Lighten", ID_BLEND);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Threshold", 0, 1000, 0, 2, 0, PF_Precision_HUNDREDTHS, 0, PF_ParamFlag_SUPERVISE, ID_THRESHOLD);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Knee", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_KNEE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Cleanup (px)", 0, 100, 0, 10, 0, PF_Precision_TENTHS, 0, 0, ID_CLEANUP);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_EXPOSURE_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Ghost", ID_GHOST_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Enable", FALSE, PF_ParamFlag_SUPERVISE, ID_GHOST);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Count", 1, 16, 1, 8, 1, ID_GHOST_COUNT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Offset", -200, 200, -50, 50, 12, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_GHOST_OFFSET);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Strength", 0, 100, 0, 100, 25, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_GHOST_STRENGTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Decay", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_GHOST_DECAY);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Length", 0, 100, 0, 20, 1, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_GHOST_LENGTH);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_GHOST_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Frame Roll", ID_FRAME_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Roll", -1000, 1000, -100, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_ROLL);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Frame Line", 0, 50, 0, 10, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_ROLL_BAR);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Frame Line Softness", 0, 500, 0, 100, 30, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_ROLL_BAR_SOFT);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_FRAME_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Color", ID_COLOR_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Smear Tint", 255, 255, 255, ID_TINT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smear Saturation", 0, 400, 0, 200, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SATURATION);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Chromatic Spread", -90, 90, -30, 30, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_CHROMA);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Breakup", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_BREAKUP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Breakup Scale (px)", 0.5, 1000, 1, 50, 6, PF_Precision_TENTHS, 0, 0, ID_BREAKUP_SCALE);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_COLOR_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Shake", ID_SHAKE_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Enable", FALSE, PF_ParamFlag_SUPERVISE, ID_SHAKE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Amount", 0, 1000, 0, 200, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SHAKE_AMOUNT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Frequency (Hz)", 0, 240, 0, 30, 12, PF_Precision_TENTHS, 0, 0, ID_SHAKE_FREQ);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smoothness", 0, 100, 0, 100, 30, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SHAKE_SMOOTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Seed", 0, 999999, 0, 9999, 1234, ID_SHAKE_SEED);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Length Jitter", 0, 100, 0, 100, 35, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SHAKE_LENGTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smear Jitter", 0, 100, 0, 100, 25, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SHAKE_SMEAR);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Angle Jitter", 0, 180, 0, 30, 0, PF_Precision_TENTHS, 0, 0, ID_SHAKE_ANGLE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Timing Jitter", 0, 360, 0, 90, 20, PF_Precision_TENTHS, 0, 0, ID_SHAKE_TIMING);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Ghost Jitter", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SHAKE_GHOST);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Roll Jitter", 0, 100, 0, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SHAKE_ROLL);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Weave X (px)", 0, 200, 0, 10, 0, PF_Precision_TENTHS, 0, 0, ID_SHAKE_WEAVE_X);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Weave Y (px)", 0, 200, 0, 10, 1.5, PF_Precision_TENTHS, 0, 0, ID_SHAKE_WEAVE_Y);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_SHAKE_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Output", ID_OUTPUT_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Input Space", 5, 1, "Linear|sRGB|Gamma 2.4|ARRI LogC3|Sony S-Log3", ID_COLORSPACE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Smear Alpha", "On", TRUE, 0, ID_AFFECT_ALPHA);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_OUTPUT_END);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Warp", ID_WARP_TOPIC);
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Enable", FALSE, PF_ParamFlag_SUPERVISE, ID_WARP);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("View", 4, 1, "Result|Length Map|Reaction|Displacement", ID_WARP_VIEW);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Amount", 0, 400, 0, 200, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_WARP_AMOUNT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Flow Length", 0, 100, 0, 100, 60, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_FLOW_LENGTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Flow Wave", 0, 30, 0, 8, 1.2, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_SUPERVISE, ID_FLOW_WAVE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Flow Scale", 2, 150, 5, 100, 35, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_FLOW_SCALE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Flow Speed", 0, 10, 0, 2, 0.25, PF_Precision_HUNDREDTHS, 0, 0, ID_FLOW_SPEED);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Flow Detail", 1, 6, 1, 5, 3, ID_FLOW_DETAIL);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Flow Seed", 0, 99999, 0, 999, 7, ID_FLOW_SEED);
    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Drift Angle", 90, ID_DRIFT_ANGLE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Drift Speed", 0, 200, 0, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_DRIFT_SPEED);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Brightness Response", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_SUPERVISE, ID_LUMA_RESPONSE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Reaction Softness", 0, 30, 0, 15, 4, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_LUMA_SOFTNESS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Motion Response", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_SUPERVISE, ID_MOTION_RESPONSE);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Motion Sensitivity", 0, 50, 0, 20, 4, PF_Precision_TENTHS, 0, 0, ID_MOTION_SENS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inertia", 0, 100, 0, 100, 60, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_SUPERVISE, ID_INERTIA);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("History Frames", 0, 8, 0, 8, 4, ID_HISTORY);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Length Reaction", 0, 300, 0, 200, 80, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_LENGTH_REACTION);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Wave Reaction", 0, 100, 0, 100, 70, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_WAVE_REACTION);
    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Pull Point", 50, 50, 0, ID_PULL_POINT);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Pull Strength (+ bulge, - pinch)", -100, 100, -100, 100, 30, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_PULL_STRENGTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Pull Radius", 2, 150, 5, 100, 45, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_PULL_RADIUS);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Pull Length", 0, 300, 0, 200, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_PULL_LENGTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Auto Target Strength", -100, 100, -100, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, PF_ParamFlag_SUPERVISE, ID_AUTO_STRENGTH);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Base Follow", 0, 100, 0, 100, 25, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_BASE_FOLLOW);
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Length Steps", 3, 12, 3, 12, 6, ID_WARP_LEVELS);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_WARP_END);

    out_data->num_params = P_COUNT;
    return err;
}

// Reads a parameter at the render time through checkout, as SmartFX requires.
class ParamReader {
public:
    explicit ParamReader(PF_InData* in) : in_(in) {}

    double real(int index) {
        PF_ParamDef d;
        if (!checkout(index, d)) return 0.0;
        const double v = d.u.fs_d.value;
        checkin(d);
        return v;
    }
    int popup(int index) {  // 0-based
        PF_ParamDef d;
        if (!checkout(index, d)) return 0;
        const int v = static_cast<int>(d.u.pd.value) - 1;
        checkin(d);
        return v;
    }
    bool check(int index) {
        PF_ParamDef d;
        if (!checkout(index, d)) return false;
        const bool v = d.u.bd.value != 0;
        checkin(d);
        return v;
    }
    int integer(int index) {
        PF_ParamDef d;
        if (!checkout(index, d)) return 0;
        const int v = static_cast<int>(d.u.sd.value);
        checkin(d);
        return v;
    }
    double angle(int index) {  // degrees
        PF_ParamDef d;
        if (!checkout(index, d)) return 0.0;
        const double v = static_cast<double>(d.u.ad.value) / 65536.0;
        checkin(d);
        return v;
    }
    // Point in pixels of the (down-sampled) layer, the space of the input world.
    void point(int index, double xy[2]) {
        PF_ParamDef d;
        xy[0] = in_->width * 0.5;
        xy[1] = in_->height * 0.5;
        if (!checkout(index, d)) return;
        xy[0] = static_cast<double>(d.u.td.x_value) / 65536.0;
        xy[1] = static_cast<double>(d.u.td.y_value) / 65536.0;
        checkin(d);
    }
    void color(int index, double rgb[3]) {
        PF_ParamDef d;
        rgb[0] = rgb[1] = rgb[2] = 1.0;
        if (!checkout(index, d)) return;
        rgb[0] = d.u.cd.value.red / 255.0;
        rgb[1] = d.u.cd.value.green / 255.0;
        rgb[2] = d.u.cd.value.blue / 255.0;
        checkin(d);
    }

private:
    bool checkout(int index, PF_ParamDef& d) {
        AEFX_CLR_STRUCT(d);
        return PF_CHECKOUT_PARAM(in_, index, in_->current_time, in_->time_step, in_->time_scale, &d) == PF_Err_NONE;
    }
    void checkin(PF_ParamDef& d) { PF_CHECKIN_PARAM(in_, &d); }
    PF_InData* in_;
};

Params readParams(PF_InData* in) {
    ParamReader r(in);
    Params p;
    p.opacity = r.real(P_OPACITY) / 100.0;
    p.profile = r.popup(P_PROFILE);  // fade, exponential, camera
    p.length = r.real(P_LENGTH) / 100.0;
    p.angle = r.angle(P_ANGLE);
    p.symmetric = r.check(P_SYMMETRIC);
    p.back_length = r.real(P_BACK_LENGTH) / 100.0;
    p.start_offset = r.real(P_START_OFFSET) / 100.0;
    p.edge = r.popup(P_EDGE);
    p.frame_gap = r.real(P_FRAME_GAP) / 100.0;
    p.falloff = r.real(P_FALLOFF) / 100.0;
    p.falloff_curve = r.real(P_FALLOFF_CURVE);
    p.decay = r.real(P_DECAY);
    p.timing_shift = r.angle(P_TIMING_SHIFT);
    p.shutter_angle = r.real(P_SHUTTER_ANGLE);
    p.pulldown_angle = r.real(P_PULLDOWN_ANGLE);
    p.claw_ease = r.real(P_CLAW_EASE) / 100.0;
    p.smear = r.real(P_SMEAR) / 100.0;
    p.gain = r.real(P_GAIN);
    p.blend = r.popup(P_BLEND);
    p.threshold = r.real(P_THRESHOLD);
    p.knee = r.real(P_KNEE) / 100.0;
    p.cleanup = r.real(P_CLEANUP);
    p.ghost = r.check(P_GHOST);
    p.ghost_count = r.integer(P_GHOST_COUNT);
    p.ghost_offset = r.real(P_GHOST_OFFSET) / 100.0;
    p.ghost_strength = r.real(P_GHOST_STRENGTH) / 100.0;
    p.ghost_decay = r.real(P_GHOST_DECAY) / 100.0;
    p.ghost_length = r.real(P_GHOST_LENGTH) / 100.0;
    p.roll = r.real(P_ROLL) / 100.0;
    p.roll_bar = r.real(P_ROLL_BAR) / 100.0;
    p.roll_bar_soft = r.real(P_ROLL_BAR_SOFT) / 100.0;
    r.color(P_TINT, p.tint);
    p.saturation = r.real(P_SATURATION) / 100.0;
    p.chroma = r.real(P_CHROMA) / 100.0;
    p.breakup = r.real(P_BREAKUP) / 100.0;
    p.breakup_scale = r.real(P_BREAKUP_SCALE);
    p.shake = r.check(P_SHAKE);
    p.shake_amount = r.real(P_SHAKE_AMOUNT) / 100.0;
    p.shake_freq = r.real(P_SHAKE_FREQ);
    p.shake_smooth = r.real(P_SHAKE_SMOOTH) / 100.0;
    p.shake_seed = r.integer(P_SHAKE_SEED);
    p.shake_length = r.real(P_SHAKE_LENGTH) / 100.0;
    p.shake_smear = r.real(P_SHAKE_SMEAR) / 100.0;
    p.shake_angle = r.real(P_SHAKE_ANGLE);
    p.shake_timing = r.real(P_SHAKE_TIMING);
    p.shake_ghost = r.real(P_SHAKE_GHOST) / 100.0;
    p.shake_roll = r.real(P_SHAKE_ROLL) / 100.0;
    p.shake_weave_x = r.real(P_SHAKE_WEAVE_X);
    p.shake_weave_y = r.real(P_SHAKE_WEAVE_Y);
    p.colorspace = r.popup(P_COLORSPACE);
    p.affect_alpha = r.check(P_AFFECT_ALPHA);

    p.warp = r.check(P_WARP);
    p.warp_view = r.popup(P_WARP_VIEW);
    p.warp_amount = r.real(P_WARP_AMOUNT) / 100.0;
    p.flow_length = r.real(P_FLOW_LENGTH) / 100.0;
    p.flow_wave = r.real(P_FLOW_WAVE) / 100.0;
    p.flow_scale = r.real(P_FLOW_SCALE) / 100.0;
    p.flow_speed = r.real(P_FLOW_SPEED);
    p.flow_detail = r.integer(P_FLOW_DETAIL);
    p.flow_seed = r.integer(P_FLOW_SEED);
    p.drift_angle = r.angle(P_DRIFT_ANGLE);
    p.drift_speed = r.real(P_DRIFT_SPEED) / 100.0;
    p.luma_response = r.real(P_LUMA_RESPONSE) / 100.0;
    p.luma_softness = r.real(P_LUMA_SOFTNESS) / 100.0;
    p.motion_response = r.real(P_MOTION_RESPONSE) / 100.0;
    p.motion_sensitivity = r.real(P_MOTION_SENS);
    p.inertia = r.real(P_INERTIA) / 100.0;
    p.history = r.integer(P_HISTORY);
    p.length_reaction = r.real(P_LENGTH_REACTION) / 100.0;
    p.wave_reaction = r.real(P_WAVE_REACTION) / 100.0;
    double pt[2];
    r.point(P_PULL_POINT, pt);
    p.pull_x = in->width > 0 ? pt[0] / in->width : 0.5;
    p.pull_y = in->height > 0 ? pt[1] / in->height : 0.5;
    p.pull_strength = r.real(P_PULL_STRENGTH) / 100.0;
    p.pull_radius = r.real(P_PULL_RADIUS) / 100.0;
    p.pull_length = r.real(P_PULL_LENGTH) / 100.0;
    p.auto_strength = r.real(P_AUTO_STRENGTH) / 100.0;
    p.base_follow = r.real(P_BASE_FOLLOW) / 100.0;
    p.warp_levels = r.integer(P_WARP_LEVELS);

    if (in->downsample_x.den > 0 && in->downsample_x.num > 0)
        p.pixel_scale = static_cast<double>(in->downsample_x.num) / in->downsample_x.den;
    return p;
}

// ---------------------------------------------------------------------------------------------
// Pixel conversion (AE stores ARGB; the core wants RGBA floats)
// ---------------------------------------------------------------------------------------------

// One row of the world as float RGBA, premultiplied like After Effects stores it.
void readRow(const PF_EffectWorld* w, PF_PixelFormat fmt, int y, float* o) {
    const char* row = reinterpret_cast<const char*>(w->data) + static_cast<size_t>(y) * w->rowbytes;
    for (int x = 0; x < w->width; ++x, o += 4) {
        if (fmt == PF_PixelFormat_ARGB128) {
            const PF_PixelFloat* p = reinterpret_cast<const PF_PixelFloat*>(row) + x;
            o[0] = p->red; o[1] = p->green; o[2] = p->blue; o[3] = p->alpha;
        } else if (fmt == PF_PixelFormat_ARGB64) {
            const PF_Pixel16* p = reinterpret_cast<const PF_Pixel16*>(row) + x;
            o[0] = p->red / kMax16; o[1] = p->green / kMax16; o[2] = p->blue / kMax16; o[3] = p->alpha / kMax16;
        } else {
            const PF_Pixel8* p = reinterpret_cast<const PF_Pixel8*>(row) + x;
            o[0] = p->red / 255.0f; o[1] = p->green / 255.0f; o[2] = p->blue / 255.0f; o[3] = p->alpha / 255.0f;
        }
    }
}

void readWorld(const PF_EffectWorld* w, PF_PixelFormat fmt, Image& out) {
    out = Image(w->width, w->height, 4);
    for (int y = 0; y < w->height; ++y) readRow(w, fmt, y, out.row(y));
}

inline A_u_short to16(float v) { return static_cast<A_u_short>(std::min(std::max(v, 0.f), 1.f) * kMax16 + 0.5f); }
inline A_u_char to8(float v) { return static_cast<A_u_char>(std::min(std::max(v, 0.f), 1.f) * 255.f + 0.5f); }

// Writes the result into a destination world. dx/dy: position of the destination origin inside the image.
void writeWorld(const Image& img, PF_EffectWorld* w, PF_PixelFormat fmt, int dx, int dy) {
    for (int y = 0; y < w->height; ++y) {
        char* row = reinterpret_cast<char*>(w->data) + static_cast<size_t>(y) * w->rowbytes;
        const int sy = y + dy;
        for (int x = 0; x < w->width; ++x) {
            const int sx = x + dx;
            float v[4] = {0.f, 0.f, 0.f, 0.f};
            if (sx >= 0 && sy >= 0 && sx < img.width && sy < img.height) std::memcpy(v, img.row(sy) + static_cast<size_t>(sx) * 4, sizeof(v));
            if (fmt == PF_PixelFormat_ARGB128) {
                PF_PixelFloat* p = reinterpret_cast<PF_PixelFloat*>(row) + x;
                p->red = v[0]; p->green = v[1]; p->blue = v[2]; p->alpha = v[3];
            } else if (fmt == PF_PixelFormat_ARGB64) {
                PF_Pixel16* p = reinterpret_cast<PF_Pixel16*>(row) + x;
                p->red = to16(v[0]); p->green = to16(v[1]); p->blue = to16(v[2]); p->alpha = to16(v[3]);
            } else {
                PF_Pixel8* p = reinterpret_cast<PF_Pixel8*>(row) + x;
                p->red = to8(v[0]); p->green = to8(v[1]); p->blue = to8(v[2]); p->alpha = to8(v[3]);
            }
        }
    }
}

// AE hands over premultiplied colour. Non-linear input spaces must be decoded on straight colour,
// so the wrapper converts around the core (which then runs in linear light, premultiplied).
void decodeRow(float* px, int count, int cs) {
    if (cs == timeyum::kCsLinear) return;
    for (int i = 0; i < count; ++i, px += 4) {
        const float a = px[3];
        for (int c = 0; c < 3; ++c) px[c] = a > 0.f ? timeyum::toLinear(px[c] / a, cs) * a : 0.f;
    }
}

void decodeColor(Image& img, int cs) {
    for (int y = 0; y < img.height; ++y) decodeRow(img.row(y), img.width, cs);
}

void encodeColor(Image& img, int cs) {
    if (cs == timeyum::kCsLinear) return;
    for (size_t i = 0; i < static_cast<size_t>(img.width) * img.height; ++i) {
        float* p = img.data.data() + i * 4;
        const float a = p[3];
        for (int c = 0; c < 3; ++c) p[c] = a > 0.f ? timeyum::fromLinear(p[c] / a, cs) * a : 0.f;
    }
}

// ---------------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------------

PF_Err about(PF_InData* in_data, PF_OutData* out_data) {
    (void)in_data;  // PF_SPRINTF expands to a call through in_data
    PF_SPRINTF(out_data->return_msg, "Timeyum Timeshift %d.%d\rARRI Timing Shift Box look.", TY_VERSION_MAJOR, TY_VERSION_MINOR);
    return PF_Err_NONE;
}

PF_Err globalSetup(PF_OutData* out_data) {
    out_data->my_version = PF_VERSION(TY_VERSION_MAJOR, TY_VERSION_MINOR, TY_VERSION_BUG, PF_Stage_RELEASE, 0);
    out_data->out_flags = TY_OUT_FLAGS;
    out_data->out_flags2 = TY_OUT_FLAGS2;
    return PF_Err_NONE;
}

void setEnabled(PF_InData* in_data, PF_ParamDef* params[], AEGP_SuiteHandler& suites, int index, bool enabled) {
    PF_ParamDef copy = *params[index];
    if (enabled) copy.ui_flags &= ~PF_PUI_DISABLED;
    else copy.ui_flags |= PF_PUI_DISABLED;
    suites.ParamUtilsSuite3()->PF_UpdateParamUI(in_data->effect_ref, index, &copy);
}

PF_Err updateParamsUI(PF_InData* in_data, PF_ParamDef* params[]) {
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const int profile = params[P_PROFILE]->u.pd.value - 1;
    const bool camera = profile == 2;
    const bool wrap = params[P_EDGE]->u.pd.value - 1 == 0;
    for (int i : {P_SMEAR, P_SYMMETRIC, P_START_OFFSET}) setEnabled(in_data, params, suites, i, !camera);
    setEnabled(in_data, params, suites, P_BACK_LENGTH, !camera && !params[P_SYMMETRIC]->u.bd.value);
    setEnabled(in_data, params, suites, P_FALLOFF, profile == 0);
    setEnabled(in_data, params, suites, P_FALLOFF_CURVE, profile == 0);
    setEnabled(in_data, params, suites, P_DECAY, profile == 1);
    for (int i : {P_TIMING_SHIFT, P_SHUTTER_ANGLE, P_PULLDOWN_ANGLE, P_CLAW_EASE}) setEnabled(in_data, params, suites, i, camera);
    setEnabled(in_data, params, suites, P_FRAME_GAP, camera || wrap);
    setEnabled(in_data, params, suites, P_KNEE, params[P_THRESHOLD]->u.fs_d.value > 0.0);
    const bool ghost = params[P_GHOST]->u.bd.value != 0;
    for (int i : {P_GHOST_COUNT, P_GHOST_OFFSET, P_GHOST_STRENGTH, P_GHOST_DECAY, P_GHOST_LENGTH}) setEnabled(in_data, params, suites, i, ghost);
    const bool shake = params[P_SHAKE]->u.bd.value != 0;
    for (int i : {P_SHAKE_AMOUNT, P_SHAKE_FREQ, P_SHAKE_SMOOTH, P_SHAKE_SEED, P_SHAKE_LENGTH, P_SHAKE_SMEAR, P_SHAKE_ANGLE,
                  P_SHAKE_TIMING, P_SHAKE_GHOST, P_SHAKE_ROLL, P_SHAKE_WEAVE_X, P_SHAKE_WEAVE_Y})
        setEnabled(in_data, params, suites, i, shake);
    const bool warp = params[P_WARP]->u.bd.value != 0;
    const bool lumaOn = params[P_LUMA_RESPONSE]->u.fs_d.value > 0.0;
    const bool motionOn = params[P_MOTION_RESPONSE]->u.fs_d.value > 0.0;
    const bool waveOn = params[P_FLOW_WAVE]->u.fs_d.value > 0.0;
    const bool autoOn = params[P_AUTO_STRENGTH]->u.fs_d.value != 0.0;
    for (int i = P_WARP_VIEW; i < P_WARP_END; ++i) setEnabled(in_data, params, suites, i, warp);
    if (warp) {
        setEnabled(in_data, params, suites, P_LUMA_SOFTNESS, lumaOn || motionOn || autoOn);
        setEnabled(in_data, params, suites, P_MOTION_SENS, motionOn);
        setEnabled(in_data, params, suites, P_INERTIA, lumaOn || motionOn || autoOn);
        setEnabled(in_data, params, suites, P_HISTORY, motionOn || ((lumaOn || autoOn) && params[P_INERTIA]->u.fs_d.value > 0.0));
        setEnabled(in_data, params, suites, P_LENGTH_REACTION, lumaOn || motionOn);
        setEnabled(in_data, params, suites, P_WAVE_REACTION, waveOn && (lumaOn || motionOn));
    }
    return PF_Err_NONE;
}

PF_Err preRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    (void)out_data;

    // The streak wraps around the whole layer, so every render needs the full input.
    PF_RenderRequest req = extra->input->output_request;
    req.rect.left = 0;
    req.rect.top = 0;
    req.rect.right = in_data->width;
    req.rect.bottom = in_data->height;
    req.preserve_rgb_of_zero_alpha = TRUE;

    PF_CheckoutResult in_result;
    ERR(extra->cb->checkout_layer(in_data->effect_ref, P_INPUT, P_INPUT, &req, in_data->current_time, in_data->time_step,
                                  in_data->time_scale, &in_result));
    if (!err) {
        extra->output->result_rect = in_result.result_rect;
        extra->output->max_result_rect = in_result.max_result_rect;
        extra->output->solid = FALSE;
        extra->output->pre_render_data = nullptr;
        // Register the parameter dependencies so cached frames invalidate correctly.
        for (int i = 1; i < P_COUNT && !err; ++i) {
#ifdef TIMEYUM_BANNER
            if (i == P_BANNER) continue;
#endif
            PF_ParamDef d;
            AEFX_CLR_STRUCT(d);
            ERR(PF_CHECKOUT_PARAM(in_data, i, in_data->current_time, in_data->time_step, in_data->time_scale, &d));
            ERR2(PF_CHECKIN_PARAM(in_data, &d));
        }
        // The warp reacts to motion: ask for the frames before this one.
        if (!err) {
            const int need = timeyum::warpHistoryCount(readParams(in_data));
            const A_long step = in_data->time_step > 0 ? in_data->time_step : in_data->time_scale / 24;
            for (int i = 1; i <= need && !err; ++i) {
                PF_CheckoutResult past;
                ERR(extra->cb->checkout_layer(in_data->effect_ref, P_INPUT, i, &req, in_data->current_time - i * step, in_data->time_step,
                                              in_data->time_scale, &past));
            }
        }
    }
    return err ? err : err2;
}

PF_Err smartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    PF_EffectWorld* input = nullptr;
    PF_EffectWorld* output = nullptr;

    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, P_INPUT, &input));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &output));
    if (!err && input && output) {
        try {
            AEFX_SuiteScoper<PF_WorldSuite2> worldSuite(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
            PF_PixelFormat fmt = PF_PixelFormat_INVALID;
            ERR(worldSuite->PF_GetPixelFormat(input, &fmt));

            if (!err) {
                Params p = readParams(in_data);
                const int cs = p.colorspace;
                p.colorspace = timeyum::kCsLinear;

                const double fps = in_data->time_step > 0 ? static_cast<double>(in_data->time_scale) / in_data->time_step : 24.0;
                const double frame = in_data->time_step > 0 ? static_cast<double>(in_data->current_time) / in_data->time_step : 0.0;

                Image src, dst;
                readWorld(input, fmt, src);
                decodeColor(src, cs);

                const int need = timeyum::warpHistoryCount(p);
                std::vector<timeyum::LumaGrid> history(need);
                for (int i = 1; i <= need; ++i) {
                    PF_EffectWorld* past = nullptr;
                    if (extra->cb->checkout_layer_pixels(in_data->effect_ref, i, &past) != PF_Err_NONE) continue;
                    if (past && past->width == input->width && past->height == input->height) {
                        timeyum::LumaGridBuilder grid(past->width, past->height);
                        std::vector<float> row(static_cast<size_t>(past->width) * 4);
                        for (int y = 0; y < past->height; ++y) {
                            readRow(past, fmt, y, row.data());
                            decodeRow(row.data(), past->width, cs);
                            grid.addRow(y, row.data(), 4, timeyum::kCsLinear);
                        }
                        history[i - 1] = grid.finish();
                    }
                    extra->cb->checkin_layer_pixels(in_data->effect_ref, i);
                }
                timeyum::process(p, src, dst, frame, fps, &history);
                encodeColor(dst, cs);

                // A buffer pixel x sits at layer coordinate x - origin_x, so the output pixel (0, 0)
                // corresponds to input pixel (in.origin_x - out.origin_x, in.origin_y - out.origin_y).
                const int dx = input->origin_x - output->origin_x;
                const int dy = input->origin_y - output->origin_y;
                writeWorld(dst, output, fmt, dx, dy);
            }
        } catch (const std::bad_alloc&) {
            err = PF_Err_OUT_OF_MEMORY;
        } catch (const std::exception&) {
            err = PF_Err_INTERNAL_STRUCT_DAMAGED;
        }
    }
    ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, P_INPUT));
    return err;
}

#if defined(TIMEYUM_BANNER) && !defined(TIMEYUM_MOCK_SDK)
// ---------------------------------------------------------------------------------------------
// Banner at the top of the Effect Controls panel
// ---------------------------------------------------------------------------------------------

// Problems this code already works around (they were solved first in the Lensyum plug-in):
//  - the custom UI must be registered and PF_OutFlag_CUSTOM_UI set both in GlobalSetup and in the PiPL;
//  - the parameter row is drawn in two events (title area and control area), so the picture is spread
//    over the union of both and each event paints its own share;
//  - pixels go to Drawbot as straight BGRA, the resource name must not equal a compile definition
//    (the resource compiler would substitute it), and the .rc must be compiled (enable_language(RC)).

void logLine(const std::string& text) {
#ifdef AE_OS_WIN
    char path[MAX_PATH];
    const DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n > MAX_PATH - 20) return;
    std::strcat(path, "timeyum_log.txt");
    if (FILE* f = std::fopen(path, "a")) {
        std::fprintf(f, "%s\n", text.c_str());
        std::fclose(f);
    }
#else
    (void)text;
#endif
}

void moduleAnchor() {}

// Raw BGRA picture embedded as the TY_BANNER_IMAGE resource: width and height (uint32 LE), pixels.
struct BannerImage {
    int w = 0, h = 0;
    const unsigned char* bgra = nullptr;
};

const BannerImage& bannerImage() {
    static const BannerImage img = [] {
        BannerImage r;
#ifdef AE_OS_WIN
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&moduleAnchor), &self)) {
            if (HRSRC res = FindResourceW(self, L"TY_BANNER_IMAGE", MAKEINTRESOURCEW(10)) /* RT_RCDATA */) {
                const DWORD n = SizeofResource(self, res);
                HGLOBAL mem = LoadResource(self, res);
                const unsigned char* p = mem ? static_cast<const unsigned char*>(LockResource(mem)) : nullptr;
                if (p && n > 8) {
                    unsigned w = 0, h = 0;
                    std::memcpy(&w, p, 4);
                    std::memcpy(&h, p + 4, 4);
                    if (w > 0 && h > 0 && w < 16384 && h < 16384 && static_cast<size_t>(w) * h * 4 + 8 <= n) {
                        r.w = static_cast<int>(w);
                        r.h = static_cast<int>(h);
                        r.bgra = p + 8;
                    }
                }
            }
        }
#endif
        return r;
    }();
    return img;
}

struct BannerSpan {
    int l = 0, t = 0, r = 0, b = 0;
    bool known = false;
};
BannerSpan g_bannerTitle, g_bannerControl;

// The banner is one picture spread over the parameter's title area and its control area: each
// draw event paints its own share, sized to the whole row (cover fit, centred).
PF_Err drawBanner(PF_InData* in_data, PF_EventExtra* ev) {
    const BannerImage& bm = bannerImage();
    if (!bm.bgra) {
        static bool logged = false;
        if (!logged) { logged = true; logLine("banner: resource TY_BANNER_IMAGE not found"); }
        return PF_Err_NONE;
    }
    BannerSpan cur;
    cur.l = ev->effect_win.current_frame.left;
    cur.t = ev->effect_win.current_frame.top;
    cur.r = ev->effect_win.current_frame.right;
    cur.b = ev->effect_win.current_frame.bottom;
    cur.known = true;
    if (ev->effect_win.area == PF_EA_PARAM_TITLE) g_bannerTitle = cur;
    else if (ev->effect_win.area == PF_EA_CONTROL) g_bannerControl = cur;
    else return PF_Err_NONE;
    const int fw = cur.r - cur.l, fh = cur.b - cur.t;
    if (fw <= 0 || fh <= 0 || fw > 4096 || fh > 4096) return PF_Err_NONE;

    BannerSpan row = cur;
    if (g_bannerTitle.known && g_bannerControl.known && std::abs(g_bannerTitle.t - g_bannerControl.t) <= 2) {
        row.l = std::min(g_bannerTitle.l, g_bannerControl.l);
        row.r = std::max(g_bannerTitle.r, g_bannerControl.r);
        row.t = std::min(g_bannerTitle.t, g_bannerControl.t);
        row.b = std::max(g_bannerTitle.b, g_bannerControl.b);
    }
    const double rw = row.r - row.l, rh = row.b - row.t;
    const double sc = std::max(rw / bm.w, rh / bm.h);

    std::vector<unsigned char> buf(static_cast<size_t>(fw) * fh * 4);
    auto fetch = [&](double sx, double sy, float* c) {
        sx = std::min(std::max(sx - 0.5, 0.0), bm.w - 1.001);
        sy = std::min(std::max(sy - 0.5, 0.0), bm.h - 1.001);
        const int x0 = static_cast<int>(sx), y0 = static_cast<int>(sy);
        const float fx = static_cast<float>(sx - x0), fy = static_cast<float>(sy - y0);
        const unsigned char* p00 = bm.bgra + (static_cast<size_t>(y0) * bm.w + x0) * 4;
        const unsigned char* p10 = p00 + 4;
        const unsigned char* p01 = p00 + static_cast<size_t>(bm.w) * 4;
        const unsigned char* p11 = p01 + 4;
        for (int k = 0; k < 3; ++k) {
            const float a = p00[k] + (p10[k] - p00[k]) * fx;
            const float b = p01[k] + (p11[k] - p01[k]) * fx;
            c[k] += a + (b - a) * fy;
        }
    };
    for (int y = 0; y < fh; ++y)
        for (int x = 0; x < fw; ++x) {
            float c[3] = {0, 0, 0};
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    const double X = cur.l - row.l + x + 0.25 + 0.5 * i, Y = cur.t - row.t + y + 0.25 + 0.5 * j;
                    fetch((X - rw * 0.5) / sc + bm.w * 0.5, (Y - rh * 0.5) / sc + bm.h * 0.5, c);
                }
            unsigned char* q = &buf[(static_cast<size_t>(y) * fw + x) * 4];
            for (int k = 0; k < 3; ++k) q[k] = static_cast<unsigned char>(std::lround(std::min(c[k] * 0.25f, 255.0f)));
            q[3] = 255;
        }

    SPBasicSuite* sp = in_data->pica_basicP;
    const PF_EffectCustomUISuite1* uiS = nullptr;
    const DRAWBOT_DrawbotSuite1* drawS = nullptr;
    const DRAWBOT_SupplierSuite1* supS = nullptr;
    const DRAWBOT_SurfaceSuite1* surfS = nullptr;
    const bool haveSuites =
        sp && sp->AcquireSuite(kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, reinterpret_cast<const void**>(&uiS)) == kSPNoError && uiS &&
        sp->AcquireSuite(kDRAWBOT_DrawSuite, kDRAWBOT_DrawSuite_VersionCurrent, reinterpret_cast<const void**>(&drawS)) == kSPNoError && drawS &&
        sp->AcquireSuite(kDRAWBOT_SupplierSuite, kDRAWBOT_SupplierSuite_VersionCurrent, reinterpret_cast<const void**>(&supS)) == kSPNoError && supS &&
        sp->AcquireSuite(kDRAWBOT_SurfaceSuite, kDRAWBOT_SurfaceSuite_VersionCurrent, reinterpret_cast<const void**>(&surfS)) == kSPNoError && surfS;
    PF_Err err = PF_Err_NONE;
    if (haveSuites) {
        DRAWBOT_DrawRef drawRef = nullptr;
        DRAWBOT_SupplierRef supplierRef = nullptr;
        DRAWBOT_SurfaceRef surfaceRef = nullptr;
        DRAWBOT_ImageRef imageRef = nullptr;
        err = uiS->PF_GetDrawingReference(ev->contextH, &drawRef);
        if (!err) err = drawS->GetSupplier(drawRef, &supplierRef);
        if (!err) err = drawS->GetSurface(drawRef, &surfaceRef);
        if (!err) err = supS->NewImageFromBuffer(supplierRef, fw, fh, fw * 4, kDRAWBOT_PixelLayout_32BGRA_Straight, buf.data(), &imageRef);
        if (!err) {
            DRAWBOT_PointF32 origin;
            origin.x = static_cast<float>(cur.l);
            origin.y = static_cast<float>(cur.t);
            err = surfS->DrawImage(surfaceRef, imageRef, &origin, 1.0f);
        }
        if (imageRef) supS->ReleaseObject((DRAWBOT_ObjectRef)imageRef);
        if (err) logLine("banner: drawing failed, error " + std::to_string(static_cast<int>(err)));
        ev->evt_out_flags |= PF_EO_HANDLED_EVENT;
    } else {
        static bool logged = false;
        if (!logged) { logged = true; logLine("banner: drawing suites not available"); }
    }
    if (sp) {
        if (surfS) sp->ReleaseSuite(kDRAWBOT_SurfaceSuite, kDRAWBOT_SurfaceSuite_VersionCurrent);
        if (supS) sp->ReleaseSuite(kDRAWBOT_SupplierSuite, kDRAWBOT_SupplierSuite_VersionCurrent);
        if (drawS) sp->ReleaseSuite(kDRAWBOT_DrawSuite, kDRAWBOT_DrawSuite_VersionCurrent);
        if (uiS) sp->ReleaseSuite(kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1);
    }
    return PF_Err_NONE;
}

PF_Err handleEvent(PF_InData* in_data, PF_EventExtra* ev) {
    if (!ev || !ev->contextH || (*ev->contextH)->w_type != PF_Window_EFFECT) return PF_Err_NONE;
    if (ev->e_type != PF_Event_DRAW || ev->effect_win.index != P_BANNER) return PF_Err_NONE;
    return drawBanner(in_data, ev);
}
#endif

}  // namespace

extern "C" {

DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output,
                            void* extra) {
    (void)output;
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT: err = about(in_data, out_data); break;
            case PF_Cmd_GLOBAL_SETUP: err = globalSetup(out_data); break;
            case PF_Cmd_PARAMS_SETUP: err = addParams(in_data, out_data); break;
            case PF_Cmd_UPDATE_PARAMS_UI: err = updateParamsUI(in_data, params); break;
            case PF_Cmd_SMART_PRE_RENDER: err = preRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra)); break;
            case PF_Cmd_SMART_RENDER: err = smartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra)); break;
#if defined(TIMEYUM_BANNER) && !defined(TIMEYUM_MOCK_SDK)
            case PF_Cmd_EVENT: err = handleEvent(in_data, reinterpret_cast<PF_EventExtra*>(extra)); break;
#endif
            default: break;
        }
    } catch (PF_Err& thrown) {
        err = thrown;
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}

#ifdef PF_REGISTER_EFFECT_EXT2
DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
                                          SPBasicSuite* inSPBasicSuitePtr, const char* inHostName, const char* inHostVersion) {
    (void)inSPBasicSuitePtr;
    (void)inHostName;
    (void)inHostVersion;
    PF_Err result = PF_Err_INVALID_CALLBACK;  // the macro assigns to `result`
    result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, TY_NAME, TY_MATCH_NAME, TY_CATEGORY, AE_RESERVED_INFO,
                                     "EffectMain", "https://github.com/ramirstudio/Timeyum");
    return result;
}
#endif

}  // extern "C"
