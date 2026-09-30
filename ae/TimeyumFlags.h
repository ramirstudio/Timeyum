#pragma once

// Plain numbers shared by GlobalSetup (TimeyumAE.cpp) and the PiPL (TimeyumPiPL.r). The PiPL is
// preprocessed without the SDK enums, so the values are spelled out; TimeyumAE.cpp static_asserts
// that they equal the SDK flags. If that assert fails, fix the numbers here and nowhere else.
//
// OutFlags:  PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_SEND_UPDATE_PARAMS_UI
// OutFlags2: PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_THREADED_RENDERING
#define TY_OUT_FLAGS 0x06000000
#define TY_OUT_FLAGS2 0x08001400

#define TY_VERSION_MAJOR 1
#define TY_VERSION_MINOR 0
#define TY_VERSION_BUG 0
// PF_VERSION(1, 0, 0, PF_Stage_RELEASE, 0)
#define TY_VERSION_VALUE 525824

#define TY_NAME "Timeyum"
#define TY_CATEGORY "Timeyum"
#define TY_MATCH_NAME "Timeyum Timeshift"
