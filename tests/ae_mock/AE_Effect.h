// Minimal stand-in for the After Effects SDK, only what ae/TimeyumAE.cpp touches. It exists so the
// wrapper logic can be exercised without the SDK; it says nothing about the real SDK's signatures.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define DllExport
typedef int32_t PF_Err;
typedef int32_t A_long;
typedef uint8_t A_u_char;
typedef uint16_t A_u_short;
typedef uint8_t A_Boolean;
typedef int32_t PF_Fixed;
typedef float PF_FpShort;
#define TRUE 1
#define FALSE 0
#define PF_Err_NONE 0
#define PF_Err_OUT_OF_MEMORY 5
#define PF_Err_INTERNAL_STRUCT_DAMAGED 512

enum PF_Cmd {
    PF_Cmd_ABOUT, PF_Cmd_GLOBAL_SETUP, PF_Cmd_PARAMS_SETUP, PF_Cmd_UPDATE_PARAMS_UI,
    PF_Cmd_SMART_PRE_RENDER, PF_Cmd_SMART_RENDER
};

enum {
    PF_OutFlag_DEEP_COLOR_AWARE = 1 << 25,
    PF_OutFlag_SEND_UPDATE_PARAMS_UI = 1 << 26,
    PF_OutFlag2_SUPPORTS_SMART_RENDER = 1 << 10,
    PF_OutFlag2_FLOAT_COLOR_AWARE = 1 << 12,
    PF_OutFlag2_SUPPORTS_THREADED_RENDERING = 1 << 27,
    PF_ParamFlag_SUPERVISE = 1 << 0,
    PF_PUI_DISABLED = 1 << 1,
    PF_Stage_RELEASE = 3
};
#define PF_VERSION(v, s, b, st, bld) ((((v) & 0x7) << 19) | (((s) & 0xF) << 15) | (((b) & 0xF) << 11) | (((st) & 0x3) << 9) | ((bld) & 0x1FF))

enum PF_Precision { PF_Precision_INTEGER, PF_Precision_TENTHS, PF_Precision_HUNDREDTHS };
enum { PF_ValueDisplayFlag_PERCENT = 1 };

enum PF_PixelFormat { PF_PixelFormat_INVALID = 0, PF_PixelFormat_ARGB32, PF_PixelFormat_ARGB64, PF_PixelFormat_ARGB128 };
struct PF_Pixel8 { A_u_char alpha, red, green, blue; };
struct PF_Pixel16 { A_u_short alpha, red, green, blue; };
struct PF_PixelFloat { float alpha, red, green, blue; };

struct PF_LRect { A_long left, top, right, bottom; };
struct PF_RationalScale { A_long num, den; };

struct PF_LayerDef {
    void* data;
    A_long rowbytes, width, height, origin_x, origin_y;
};
typedef PF_LayerDef PF_EffectWorld;

struct PF_ParamDef {
    struct { double value; } fs_d_storage;
    union U {
        struct { double value; } fs_d;
        struct { A_long value; } pd;
        struct { A_long value; } sd;
        struct { A_Boolean value; } bd;
        struct { PF_Fixed value; } ad;
        struct { PF_Pixel8 value; } cd;
        struct { PF_Fixed x_value; PF_Fixed y_value; } td;
        U() { std::memset(this, 0, sizeof(*this)); }
    } u;
    A_long ui_flags = 0, flags = 0;
    char name[64] = {0};
    int param_type = 0;
    struct { A_long id = 0; } uu;
};

struct PF_InData {
    A_long current_time = 0, time_step = 1001, time_scale = 24000;
    A_long width = 0, height = 0;
    PF_RationalScale downsample_x{1, 1}, downsample_y{1, 1};
    void* effect_ref = nullptr;
    void* pica_basicP = nullptr;
    void* utils = nullptr;
};

struct PF_OutData {
    char return_msg[256];
    A_long num_params = 0, my_version = 0, out_flags = 0, out_flags2 = 0;
};

struct PF_RenderRequest {
    PF_LRect rect;
    A_Boolean preserve_rgb_of_zero_alpha = 0;
};
struct PF_CheckoutResult { PF_LRect result_rect, max_result_rect; };

struct PF_PreRenderCallbacks {
    PF_Err (*checkout_layer)(void*, A_long, A_long, const PF_RenderRequest*, A_long, A_long, A_long, PF_CheckoutResult*);
};
struct PF_PreRenderInput { PF_RenderRequest output_request; };
struct PF_PreRenderOutput { PF_LRect result_rect, max_result_rect; A_Boolean solid; void* pre_render_data; };
struct PF_PreRenderExtra { PF_PreRenderInput* input; PF_PreRenderOutput* output; PF_PreRenderCallbacks* cb; };

struct PF_SmartRenderCallbacks {
    PF_Err (*checkout_layer_pixels)(void*, A_long, PF_EffectWorld**);
    PF_Err (*checkout_output)(void*, PF_EffectWorld**);
    PF_Err (*checkin_layer_pixels)(void*, A_long);
};
struct PF_SmartRenderExtra { PF_SmartRenderCallbacks* cb; };

#define AEFX_CLR_STRUCT(s) do {} while (0)
#define ERR(x) do { if (!err) err = (x); } while (0)
#define ERR2(x) do { if (!err2) err2 = (x); } while (0)
// like the real macro, PF_SPRINTF reaches through the variable named in_data
#define PF_SPRINTF (in_data, sprintf)

// ---- parameter recording -------------------------------------------------------------------
struct MockParam { std::string kind, name; double dflt; int id; int flags; };
inline std::vector<MockParam>& mockRecord() { static std::vector<MockParam> r; return r; }
#define MOCK_ADD(kind, name, dflt, id, flags) mockRecord().push_back({kind, name, (double)(dflt), id, flags})
#define PF_ADD_FLOAT_SLIDERX(NAME, VMIN, VMAX, SMIN, SMAX, DFLT, PREC, DISP, FLAGS, ID) MOCK_ADD("float", NAME, DFLT, ID, FLAGS)
#define PF_ADD_POPUPX(NAME, N, DFLT, CHOICES, FLAGS, ID) MOCK_ADD("popup", NAME, DFLT, ID, FLAGS)
#define PF_ADD_POPUP(NAME, N, DFLT, CHOICES, ID) MOCK_ADD("popup", NAME, DFLT, ID, 0)
#define PF_ADD_CHECKBOXX(NAME, DFLT, FLAGS, ID) MOCK_ADD("check", NAME, DFLT, ID, FLAGS)
#define PF_ADD_CHECKBOX(NAME, CBNAME, DFLT, FLAGS, ID) MOCK_ADD("check", NAME, DFLT, ID, FLAGS)
#define PF_ADD_ANGLE(NAME, DFLT, ID) MOCK_ADD("angle", NAME, DFLT, ID, 0)
#define PF_ADD_POINT(NAME, X, Y, RESTRICT, ID) MOCK_ADD("point", NAME, (X) * 1000 + (Y), ID, 0)
#define PF_ADD_COLOR(NAME, R, G, B, ID) MOCK_ADD("color", NAME, (R) * 65536 + (G) * 256 + (B), ID, 0)
#define PF_ADD_SLIDER(NAME, VMIN, VMAX, SMIN, SMAX, DFLT, ID) MOCK_ADD("int", NAME, DFLT, ID, 0)
#define PF_ADD_TOPIC(NAME, ID) MOCK_ADD("topic", NAME, 0, ID, 0)
#define PF_END_TOPIC(ID) MOCK_ADD("endtopic", "", 0, ID, 0)

// ---- checkout ------------------------------------------------------------------------------
inline std::map<int, PF_ParamDef>& mockOverrides() { static std::map<int, PF_ParamDef> m; return m; }
PF_Err mockCheckout(PF_InData*, int index, PF_ParamDef* d);
#define PF_CHECKOUT_PARAM(IN, INDEX, T, STEP, SCALE, PARAM) mockCheckout((IN), (INDEX), (PARAM))
#define PF_CHECKIN_PARAM(IN, PARAM) (PF_Err_NONE)

// ---- suites --------------------------------------------------------------------------------
inline std::map<int, bool>& mockDisabled() { static std::map<int, bool> m; return m; }
struct PF_ParamUtilsSuite3 {
    PF_Err PF_UpdateParamUI(void*, int index, const PF_ParamDef* d) {
        mockDisabled()[index] = (d->ui_flags & PF_PUI_DISABLED) != 0;
        return PF_Err_NONE;
    }
};
class AEGP_SuiteHandler {
public:
    explicit AEGP_SuiteHandler(void*) {}
    PF_ParamUtilsSuite3* ParamUtilsSuite3() { static PF_ParamUtilsSuite3 s; return &s; }
};
struct PF_WorldSuite2 {
    PF_PixelFormat format;
    PF_Err PF_GetPixelFormat(const PF_EffectWorld*, PF_PixelFormat* f) { *f = format; return PF_Err_NONE; }
};
inline PF_PixelFormat& mockFormat() { static PF_PixelFormat f = PF_PixelFormat_ARGB128; return f; }
#define kPFWorldSuite 0
#define kPFWorldSuiteVersion2 0
template <class T>
struct AEFX_SuiteScoper {
    T suite;
    AEFX_SuiteScoper(PF_InData*, int, int, PF_OutData*) { suite.format = mockFormat(); }
    T* operator->() { return &suite; }
};

// PluginDataEntryFunction2 support: the real macro assigns to a local named `result`.
#define PF_Err_INVALID_CALLBACK 6
#define AE_RESERVED_INFO 8
typedef void* PF_PluginDataPtr;
typedef PF_Err (*PF_PluginDataCB2)(PF_PluginDataPtr, const char*, const char*, const char*, const char*, const char*, const char*);
struct SPBasicSuite;
#define PF_REGISTER_EFFECT_EXT2(PTR, CB, NAME, MATCH, CATEGORY, RESERVED, ENTRY, URL) ((void)(PTR), (void)(CB), result = PF_Err_NONE)
