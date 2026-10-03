// Exercises ae/TimeyumAE.cpp against the mock SDK in tests/ae_mock.
#include "../ae/TimeyumAE.cpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::vector<MockParam> g_params;  // index == slot
static PF_InData* g_in_data = nullptr;

PF_Err mockCheckout(PF_InData*, int index, PF_ParamDef* d) {
    auto ov = mockOverrides().find(index);
    if (ov != mockOverrides().end()) { *d = ov->second; return PF_Err_NONE; }
    const MockParam& m = g_params[index];
    if (m.kind == "float") d->u.fs_d.value = m.dflt;
    else if (m.kind == "popup" || m.kind == "int") d->u.pd.value = static_cast<A_long>(m.dflt);
    else if (m.kind == "check") d->u.bd.value = m.dflt != 0;
    else if (m.kind == "angle") d->u.ad.value = static_cast<PF_Fixed>(m.dflt * 65536.0);
    else if (m.kind == "point") {  // the host reads the default as a percentage of the layer
        const double px = std::floor(m.dflt / 1000.0), py = m.dflt - px * 1000.0;
        d->u.td.x_value = static_cast<PF_Fixed>(px / 100.0 * g_in_data->width * 65536.0);
        d->u.td.y_value = static_cast<PF_Fixed>(py / 100.0 * g_in_data->height * 65536.0);
    }
    else if (m.kind == "color") {
        const int c = static_cast<int>(m.dflt);
        d->u.cd.value = {255, static_cast<A_u_char>(c >> 16), static_cast<A_u_char>((c >> 8) & 255), static_cast<A_u_char>(c & 255)};
    }
    return PF_Err_NONE;
}

static PF_EffectWorld *g_in, *g_out;
static PF_EffectWorld* g_past[32];  // checkout id -> world of a past frame (id 0 is the input)
static std::vector<std::pair<A_long, A_long>> g_pastCheckouts;  // (checkout id, time) requested in pre-render
static int g_checkins = 0, g_checkouts = 0;
static int g_controlRequests = 0;  // pre-render checkouts of the control layer
static PF_Err coLayerPixels(void*, A_long id, PF_EffectWorld** w) {
    ++g_checkouts;
    *w = id == 0 ? g_in : g_past[id];
    return 0;
}
static PF_Err coOutput(void*, PF_EffectWorld** w) { *w = g_out; return 0; }
static PF_Err ciLayerPixels(void*, A_long) { ++g_checkins; return 0; }
static PF_LRect g_layerRect = {0, 0, 96, 54};  // where the layer sits, in layer coordinates
static A_long g_refW = 96, g_refH = 54;            // full resolution size of the layer
static PF_LRect g_inputRequest = {0, 0, 0, 0};     // what pre-render asked for the input
static PF_LRect clip(const PF_LRect& a, const PF_LRect& b) {
    PF_LRect r = {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}
static PF_Err coLayer(void*, A_long index, A_long id, const PF_RenderRequest* r, A_long time, A_long, A_long, PF_CheckoutResult* out) {
    if (id == CHECKOUT_CONTROL) {
        if (index == P_CONTROL_LAYER) ++g_controlRequests;
    } else if (id > 0) {
        g_pastCheckouts.push_back({id, time});
    } else {
        g_inputRequest = r->rect;
    }
    out->result_rect = clip(r->rect, g_layerRect);  // After Effects clips the request to what the layer has
    out->max_result_rect = g_layerRect;
    out->ref_width = g_refW;
    out->ref_height = g_refH;
    return 0;
}

static bool same(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    PF_InData in;
    PF_OutData out;
    PF_ParamDef* params[1] = {nullptr};
    in.width = 96;
    in.height = 54;
    g_in_data = &in;
    in.inter.add_param = mockAddParam;
    in.inter.register_ui = mockRegisterUI;

    // Setup: flags, parameter table.
    CHECK(EffectMain(PF_Cmd_GLOBAL_SETUP, &in, &out, params, nullptr, nullptr) == 0, "global setup");
    CHECK(out.out_flags == TY_OUT_FLAGS && out.out_flags2 == TY_OUT_FLAGS2, "flags");
    CHECK((out.out_flags & PF_OutFlag_SEND_UPDATE_PARAMS_UI) == 0, "the panel must not ask for UPDATE_PARAMS_UI");
#ifdef TIMEYUM_BANNER
    CHECK((out.out_flags & PF_OutFlag_CUSTOM_UI) != 0 && TY_OUT_FLAGS == 0x02008000, "custom UI flag is missing from the global out flags");
#endif
    CHECK(EffectMain(PF_Cmd_PARAMS_SETUP, &in, &out, params, nullptr, nullptr) == 0, "params setup");
    CHECK(out.num_params == P_COUNT, "num_params %d vs %d", out.num_params, P_COUNT);
    g_params.push_back({"input", "input", 0, 0, 0});
    for (const auto& m : mockRecord()) g_params.push_back(m);
    CHECK(static_cast<int>(g_params.size()) == P_COUNT, "registered %zu params, enum has %d", g_params.size(), P_COUNT);
    int depth = 0;
#ifdef TIMEYUM_BANNER
    // The banner is the first parameter after the input, a control without data, registered as custom UI.
    CHECK(g_params.size() > 1 && g_params[P_BANNER].kind == "banner" && g_params[P_BANNER].id == ID_BANNER, "banner is not at slot %d", P_BANNER);
    CHECK(P_BANNER == 1, "banner must sit right below the input layer");
    CHECK((g_params[P_BANNER].flags & PF_ParamFlag_CANNOT_TIME_VARY) != 0 && (static_cast<int>(g_params[P_BANNER].dflt) & PF_PUI_CONTROL) != 0, "banner flags");
    CHECK(mockUiRegistered() && mockUi().events == PF_CustomEFlag_EFFECT && mockUi().comp_ui_width == 0 && mockUi().layer_ui_width == 0 && mockUi().preview_ui_width == 0,
          "custom UI must be registered for the effect panel only");
#endif
    {
        // IDs on disk are unique and non-zero (they no longer equal the slot numbers: the banner id is appended last).
        std::vector<int> ids;
        for (size_t i = 1; i < g_params.size(); ++i) ids.push_back(g_params[i].id);
        std::sort(ids.begin(), ids.end());
        CHECK(ids.front() > 0 && std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "parameter ids are not unique");
        CHECK(ids.back() == ID_CONTROL_END, "the newest id is not the last in the table");
    }
    for (size_t i = 1; i < g_params.size(); ++i) {
        if (g_params[i].kind == "topic") ++depth;
        if (g_params[i].kind == "endtopic") --depth;
        CHECK(depth >= 0 && depth <= 1, "topic nesting at %zu", i);
    }
    CHECK(depth == 0, "unbalanced topics");
    auto kindAt = [&](int slot, const char* kind) { CHECK(g_params[slot].kind == kind, "slot %d is %s, expected %s", slot, g_params[slot].kind.c_str(), kind); };
    kindAt(P_OPACITY, "float"); kindAt(P_PROFILE, "popup"); kindAt(P_STREAK_TOPIC, "topic"); kindAt(P_ANGLE, "angle");
    kindAt(P_SYMMETRIC, "check"); kindAt(P_EDGE, "popup"); kindAt(P_STREAK_END, "endtopic"); kindAt(P_TIMING_SHIFT, "angle");
    kindAt(P_BLEND, "popup"); kindAt(P_GHOST, "check"); kindAt(P_GHOST_COUNT, "int"); kindAt(P_TINT, "color");
    kindAt(P_WARP_TOPIC, "topic"); kindAt(P_WARP, "check"); kindAt(P_WARP_VIEW, "popup"); kindAt(P_FLOW_DETAIL, "int");
    kindAt(P_DRIFT_ANGLE, "angle"); kindAt(P_PULL_POINT, "point"); kindAt(P_PULL_STRENGTH, "float"); kindAt(P_HISTORY, "int");
    kindAt(P_WARP_LEVELS, "int"); kindAt(P_WARP_END, "endtopic");
    kindAt(P_CONTROL_TOPIC, "topic"); kindAt(P_CONTROL_INPUT, "popup"); kindAt(P_CONTROL_LAYER, "layer"); kindAt(P_CONTROL_INVERT, "check");
    kindAt(P_CONTROL_BLACK, "float"); kindAt(P_CONTROL_MATTE, "float"); kindAt(P_CONTROL_VIEW, "check"); kindAt(P_CONTROL_END, "endtopic");
    kindAt(P_SHAKE_SEED, "int"); kindAt(P_COLORSPACE, "popup"); kindAt(P_AFFECT_ALPHA, "check"); kindAt(P_OUTPUT_END, "endtopic");

    // Defaults of the panel must equal the core defaults.
    Params p = readParams(&in);
    const Params d;
    CHECK(same(p.opacity, d.opacity), "opacity %g", p.opacity);
    CHECK(p.profile == d.profile && p.edge == d.edge && p.blend == d.blend && p.colorspace == d.colorspace, "enum defaults");
    CHECK(same(p.length, d.length) && same(p.angle, d.angle) && p.symmetric == d.symmetric, "streak defaults length %g angle %g", p.length, p.angle);
    CHECK(same(p.back_length, d.back_length) && same(p.start_offset, d.start_offset) && same(p.frame_gap, d.frame_gap), "streak offsets");
    CHECK(same(p.falloff, d.falloff) && same(p.falloff_curve, d.falloff_curve) && same(p.decay, d.decay), "profile defaults");
    CHECK(same(p.timing_shift, d.timing_shift) && same(p.shutter_angle, d.shutter_angle) && same(p.pulldown_angle, d.pulldown_angle) && same(p.claw_ease, d.claw_ease), "camera defaults");
    CHECK(same(p.smear, d.smear) && same(p.gain, d.gain) && same(p.threshold, d.threshold) && same(p.knee, d.knee) && same(p.cleanup, d.cleanup), "exposure defaults");
    CHECK(p.ghost == d.ghost && p.ghost_count == d.ghost_count && same(p.ghost_offset, d.ghost_offset) && same(p.ghost_strength, d.ghost_strength) &&
          same(p.ghost_decay, d.ghost_decay) && same(p.ghost_length, d.ghost_length), "ghost defaults");
    CHECK(same(p.roll, d.roll) && same(p.roll_bar, d.roll_bar) && same(p.roll_bar_soft, d.roll_bar_soft), "roll defaults");
    CHECK(same(p.tint[0], 1) && same(p.tint[1], 1) && same(p.tint[2], 1) && same(p.saturation, d.saturation) && same(p.chroma, d.chroma) &&
          same(p.breakup, d.breakup) && same(p.breakup_scale, d.breakup_scale), "colour defaults");
    CHECK(p.shake == d.shake && same(p.shake_amount, d.shake_amount) && same(p.shake_freq, d.shake_freq) && same(p.shake_smooth, d.shake_smooth) &&
          p.shake_seed == d.shake_seed && same(p.shake_length, d.shake_length) && same(p.shake_smear, d.shake_smear) && same(p.shake_angle, d.shake_angle) &&
          same(p.shake_timing, d.shake_timing) && same(p.shake_ghost, d.shake_ghost) && same(p.shake_roll, d.shake_roll) &&
          same(p.shake_weave_x, d.shake_weave_x) && same(p.shake_weave_y, d.shake_weave_y), "shake defaults");
    CHECK(p.affect_alpha == d.affect_alpha, "alpha default");
    CHECK(p.control == d.control && p.control == timeyum::kCtlOff && p.control_invert == d.control_invert && same(p.control_black, d.control_black) &&
          same(p.control_white, d.control_white) && same(p.control_near, d.control_near) && same(p.control_far, d.control_far) &&
          same(p.control_softness, d.control_softness) && same(p.control_matte, d.control_matte) && same(p.control_emit, d.control_emit) &&
          same(p.control_length, d.control_length) && same(p.control_warp, d.control_warp) && p.control_view == d.control_view, "control defaults");
    CHECK(p.warp == d.warp && p.warp_view == d.warp_view && same(p.warp_amount, d.warp_amount), "warp defaults");
    CHECK(same(p.flow_length, d.flow_length) && same(p.flow_wave, d.flow_wave) && same(p.flow_scale, d.flow_scale) && same(p.flow_speed, d.flow_speed) &&
          p.flow_detail == d.flow_detail && p.flow_seed == d.flow_seed && same(p.drift_angle, d.drift_angle) && same(p.drift_speed, d.drift_speed), "flow defaults");
    CHECK(same(p.luma_response, d.luma_response) && same(p.luma_softness, d.luma_softness) && same(p.motion_response, d.motion_response) &&
          same(p.motion_sensitivity, d.motion_sensitivity) && same(p.inertia, d.inertia) && p.history == d.history &&
          same(p.length_reaction, d.length_reaction) && same(p.wave_reaction, d.wave_reaction), "reaction defaults");
    CHECK(same(p.pull_x, d.pull_x) && same(p.pull_y, d.pull_y) && same(p.pull_strength, d.pull_strength) && same(p.pull_radius, d.pull_radius) &&
          same(p.pull_length, d.pull_length) && same(p.auto_strength, d.auto_strength) && same(p.base_follow, d.base_follow) && p.warp_levels == d.warp_levels,
          "pull defaults: point (%g, %g)", p.pull_x, p.pull_y);

    // Pre-render.
    in.width = 96; in.height = 54;
    PF_PreRenderCallbacks prc{coLayer};
    PF_PreRenderInput pri{}; pri.output_request.rect = {10, 10, 20, 20};
    PF_PreRenderOutput pro{};
    PF_PreRenderExtra pre{&pri, &pro, &prc};
    auto freePro = [&] {
        if (pro.pre_render_data) pro.delete_pre_render_data_func(pro.pre_render_data);
        pro.pre_render_data = nullptr;
    };
    CHECK(EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre) == 0, "pre-render");
    CHECK(pro.result_rect.left == 10 && pro.result_rect.top == 10 && pro.result_rect.right == 20 && pro.result_rect.bottom == 20,
          "the output is what was asked for: (%d,%d)-(%d,%d)", pro.result_rect.left, pro.result_rect.top, pro.result_rect.right, pro.result_rect.bottom);
    CHECK(pro.max_result_rect.right == 96 && pro.max_result_rect.bottom == 54, "max result rect is the layer");
    CHECK(g_inputRequest.left <= 0 && g_inputRequest.top <= 0 && g_inputRequest.right >= 96 && g_inputRequest.bottom >= 54,
          "the input is not requested whole: (%d,%d)-(%d,%d)", g_inputRequest.left, g_inputRequest.top, g_inputRequest.right, g_inputRequest.bottom);
    CHECK(pro.pre_render_data != nullptr && pro.delete_pre_render_data_func != nullptr, "pre-render data");
    freePro();

    // Pre-render asks for the past frames the warp reacts to, and only then.
    {
        mockOverrides().clear();
        g_pastCheckouts.clear();
        in.current_time = 10010;
        in.time_step = 1001;
        CHECK(EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre) == 0, "pre-render without warp");
        freePro();
        CHECK(g_pastCheckouts.empty(), "warp off asked for %zu past frames", g_pastCheckouts.size());
        PF_ParamDef on; on.u.bd.value = 1;
        mockOverrides()[P_WARP] = on;
        CHECK(EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre) == 0, "pre-render with warp");
        freePro();
        CHECK(static_cast<int>(g_pastCheckouts.size()) == Params().history, "asked for %zu past frames", g_pastCheckouts.size());
        for (size_t i = 0; i < g_pastCheckouts.size(); ++i)
            CHECK(g_pastCheckouts[i].first == static_cast<A_long>(i + 1) && g_pastCheckouts[i].second == 10010 - static_cast<A_long>(i + 1) * 1001,
                  "past frame %zu: id %d time %d", i, g_pastCheckouts[i].first, g_pastCheckouts[i].second);
        PF_ParamDef noMotion; noMotion.u.fs_d.value = 0.0;
        mockOverrides()[P_MOTION_RESPONSE] = noMotion;
        mockOverrides()[P_INERTIA] = noMotion;
        g_pastCheckouts.clear();
        EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre);
        freePro();
        CHECK(g_pastCheckouts.empty(), "no motion and no inertia still asked for past frames");
        mockOverrides().clear();
        in.current_time = 0;
    }

    // Render in every bit depth against the core.
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    Image scene(96, 54, 4);
    for (int y = 0; y < 54; ++y)
        for (int x = 0; x < 96; ++x) {
            float* px = scene.row(y) + x * 4;
            const float a = 0.6f + 0.4f * u(rng);
            const float bright = (x > 40 && x < 50 && y > 20 && y < 30) ? 1.0f : 0.05f;
            px[0] = bright * a; px[1] = bright * 0.8f * a; px[2] = bright * 0.5f * a; px[3] = a;
        }
    PF_SmartRenderCallbacks src{coLayerPixels, coOutput, ciLayerPixels};
    PF_SmartRenderInput sri{};
    PF_SmartRenderExtra sre{&sri, &src};
    // Runs a real pre-render for the requested window and hands its data to the render.
    auto prepare = [&](PF_LRect layer, PF_LRect request) {
        g_layerRect = layer;
        pri.output_request.rect = request;
        freePro();
        pro = PF_PreRenderOutput{};
        const PF_Err e = EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre);
        CHECK(e == 0, "pre-render for a window");
        sri.pre_render_data = pro.pre_render_data;
        sri.output_request.rect = request;
    };

    struct Case { PF_PixelFormat fmt; const char* name; double tol; int cs; };
    // Where the layer sits and which window of it is rendered: the layer fills the window, the window is a crop
    // of the layer, and the layer is larger than the window and starts at a negative position.
    struct Window { const char* name; PF_LRect layer; PF_LRect request; };
    const Window windows[] = {{"whole layer", {0, 0, 96, 54}, {0, 0, 96, 54}},
                              {"cropped", {0, 0, 96, 54}, {7, 5, 47, 35}},
                              {"moved layer, visible part", {-20, -10, 76, 44}, {0, 0, 60, 30}}};
    for (const Window& wd : windows)
    for (const Case& c : {Case{PF_PixelFormat_ARGB128, "32 bit", 1e-6, 0}, Case{PF_PixelFormat_ARGB64, "16 bit", 2e-4, 0},
                          Case{PF_PixelFormat_ARGB32, "8 bit", 6e-3, 0}, Case{PF_PixelFormat_ARGB32, "8 bit sRGB", 6e-3, 1}}) {
        mockFormat() = c.fmt;
        mockOverrides().clear();
        if (c.cs) { PF_ParamDef d; d.u.pd.value = 2; mockOverrides()[P_COLORSPACE] = d; }
        PF_ParamDef len; len.u.fs_d.value = 80.0; mockOverrides()[P_LENGTH] = len;

        const size_t bpp = c.fmt == PF_PixelFormat_ARGB128 ? 16 : (c.fmt == PF_PixelFormat_ARGB64 ? 8 : 4);
        const int ow = wd.request.right - wd.request.left, oh = wd.request.bottom - wd.request.top;
        std::vector<char> inBuf(96 * 54 * bpp + 64), outBuf(static_cast<size_t>(ow) * oh * bpp + 64);
        // the origins of the buffers are deliberately meaningless: the render must not depend on them
        PF_EffectWorld inW{inBuf.data(), static_cast<A_long>(96 * bpp), 96, 54, 321, -45};
        PF_EffectWorld outW{outBuf.data(), static_cast<A_long>(ow * bpp), ow, oh, -77, 123};
        // quantise the scene exactly like the host would
        Image quant;
        {
            Image tmp = scene;
            writeWorld(tmp, &inW, c.fmt, 0, 0);
            readWorld(&inW, c.fmt, quant);
        }
        g_in = &inW; g_out = &outW;
        prepare(wd.layer, wd.request);
        CHECK(EffectMain(PF_Cmd_SMART_RENDER, &in, &out, params, nullptr, &sre) == 0, "%s %s render call", wd.name, c.name);

        Params pp = readParams(&in);
        Image expected, dst;
        Image tmp = quant;
        decodeColor(tmp, pp.colorspace);
        Params core = pp; core.colorspace = timeyum::kCsLinear;
        timeyum::process(core, tmp, dst, 0.0, 24000.0 / 1001.0);
        encodeColor(dst, pp.colorspace);
        Image got;
        readWorld(&outW, c.fmt, got);
        const int ox = wd.request.left - wd.layer.left, oy = wd.request.top - wd.layer.top;
        double worst = 0, bright = 0;
        for (int y = 0; y < oh; ++y)
            for (int x = 0; x < ow; ++x)
                for (int k = 0; k < 4; ++k) {
                    worst = std::fmax(worst, std::fabs(got.row(y)[x * 4 + k] - dst.row(y + oy)[(x + ox) * 4 + k]));
                    bright = std::fmax(bright, got.row(y)[x * 4 + k]);
                }
        CHECK(worst <= c.tol, "%s, %s: output differs from core by %g", wd.name, c.name, worst);
        CHECK(bright > 0.05, "%s, %s: the output is black", wd.name, c.name);
        if (!std::strcmp(wd.name, "whole layer") || c.fmt == PF_PixelFormat_ARGB32) std::printf("%-26s %-10s max difference to core: %.2e\n", wd.name, c.name, worst);
    }
    // A warp render with history, in 32 bit and 8 bit sRGB, equals the core fed the same data.
    for (int cs : {0, 1}) {
        mockFormat() = cs ? PF_PixelFormat_ARGB32 : PF_PixelFormat_ARGB128;
        const size_t bpp = cs ? 4 : 16;
        mockOverrides().clear();
        PF_ParamDef on; on.u.bd.value = 1;
        mockOverrides()[P_WARP] = on;
        PF_ParamDef space; space.u.pd.value = cs ? 2 : 1;
        mockOverrides()[P_COLORSPACE] = space;
        PF_ParamDef pt; pt.u.td.x_value = static_cast<PF_Fixed>(0.3 * 96 * 65536.0); pt.u.td.y_value = static_cast<PF_Fixed>(0.6 * 54 * 65536.0);
        mockOverrides()[P_PULL_POINT] = pt;

        std::vector<std::vector<char>> bufs(5, std::vector<char>(96 * 54 * bpp + 64));
        std::vector<PF_EffectWorld> worlds(5);
        std::vector<Image> frames;
        for (int k = 0; k < 5; ++k) {
            Image im(96, 54, 4);
            for (int y = 0; y < 54; ++y)
                for (int x = 0; x < 96; ++x) {
                    float* px = im.row(y) + x * 4;
                    const double dxp = x - (20 + 6 * (4 - k)), dyp = y - 30;  // a blob that moves 6 px per frame
                    const float v = static_cast<float>(0.03 + 0.9 * std::exp(-(dxp * dxp + dyp * dyp) / 40.0));
                    px[0] = v; px[1] = v * 0.9f; px[2] = v * 0.7f; px[3] = 1.f;
                }
            worlds[k] = PF_EffectWorld{bufs[k].data(), static_cast<A_long>(96 * bpp), 96, 54, 0, 0};
            writeWorld(im, &worlds[k], mockFormat(), 0, 0);
            frames.push_back(im);
        }
        g_in = &worlds[0];
        for (int k = 1; k < 5; ++k) g_past[k] = &worlds[k];
        prepare({0, 0, 96, 54}, {0, 0, 96, 54});
        std::vector<char> outBuf(96 * 54 * bpp + 64);
        PF_EffectWorld outW{outBuf.data(), static_cast<A_long>(96 * bpp), 96, 54, 0, 0};
        g_out = &outW;
        g_checkouts = g_checkins = 0;
        in.current_time = 5005;
        CHECK(EffectMain(PF_Cmd_SMART_RENDER, &in, &out, params, nullptr, &sre) == 0, "warp render %d", cs);
        CHECK(g_checkouts == 5 && g_checkins == 5, "warp render checked out %d and in %d frames (expected 5 each)", g_checkouts, g_checkins);

        Params pp = readParams(&in);
        CHECK(pp.warp && std::fabs(pp.pull_x - 0.3) < 1e-4 && std::fabs(pp.pull_y - 0.6) < 1e-4, "point read as (%g, %g)", pp.pull_x, pp.pull_y);
        const int cspace = pp.colorspace;
        pp.colorspace = timeyum::kCsLinear;
        std::vector<timeyum::LumaGrid> hist;
        for (int k = 1; k < 5; ++k) {
            Image q;
            readWorld(&worlds[k], mockFormat(), q);
            decodeColor(q, cspace);
            hist.push_back(timeyum::makeLumaGrid(q, timeyum::kCsLinear));
        }
        Image sIn, expected;
        readWorld(&worlds[0], mockFormat(), sIn);
        decodeColor(sIn, cspace);
        timeyum::process(pp, sIn, expected, 5005 / 1001.0, 24000.0 / 1001.0, &hist);
        encodeColor(expected, cspace);
        Image got;
        readWorld(&outW, mockFormat(), got);
        double worst = 0;
        for (size_t i = 0; i < got.data.size(); ++i) worst = std::fmax(worst, std::fabs(got.data[i] - expected.data[i]));
        CHECK(worst < (cs ? 6e-3 : 1e-6), "warp render %d differs from the core by %g", cs, worst);
        // and the history really changes the picture
        std::vector<timeyum::LumaGrid> none(4);
        Image without;
        timeyum::process(pp, sIn, without, 5005 / 1001.0, 24000.0 / 1001.0, &none);
        double effect = 0;
        for (size_t i = 0; i < without.data.size(); ++i) effect = std::fmax(effect, std::fabs(without.data[i] - expected.data[i]));
        CHECK(effect > 1e-3, "past frames had no influence (%g)", effect);
        std::printf("warp render %-10s max difference to core: %.2e\n", cs ? "8 bit sRGB" : "32 bit", worst);
        in.current_time = 0;
    }
    mockOverrides().clear();

    // Control input: the layer is requested only when the control reads it, stretched to the layer's size, and a
    // missing layer simply switches the control off.
    {
        mockFormat() = PF_PixelFormat_ARGB128;
        const size_t bpp = 16;
        auto popup = [&](int v) { PF_ParamDef d; d.u.pd.value = v; mockOverrides()[P_CONTROL_INPUT] = d; };
        auto number = [&](int slot, double v) { PF_ParamDef d; d.u.fs_d.value = v; mockOverrides()[slot] = d; };
        mockOverrides().clear();
        g_controlRequests = 0;
        prepare({0, 0, 96, 54}, {0, 0, 96, 54});
        CHECK(g_controlRequests == 0, "control off still asked for the control layer");
        // the host numbers the menu items from 1: Off is 1, Control Layer: Luminance is 2, This Layer: Alpha is 8
        popup(8);
        prepare({0, 0, 96, 54}, {0, 0, 96, 54});
        CHECK(g_controlRequests == 0, "This Layer asked for the control layer");
        popup(2);
        g_controlRequests = 0;
        prepare({0, 0, 96, 54}, {0, 0, 96, 54});
        CHECK(g_controlRequests == 1, "a control layer mode asked %d times for the control layer", g_controlRequests);

        // pictures: this layer, and a control layer of another size
        Image sceneC(96, 54, 4);
        for (int y = 0; y < 54; ++y)
            for (int x = 0; x < 96; ++x) {
                float* px = sceneC.row(y) + x * 4;
                const bool dot = (std::abs(x - 24) < 2 || std::abs(x - 72) < 2) && std::abs(y - 40) < 2;
                px[0] = px[1] = px[2] = dot ? 3.0f : 0.02f;
                px[3] = x < 48 ? 1.0f : 0.5f;
            }
        std::vector<char> inBuf(96 * 54 * bpp), ctlBuf(48 * 27 * bpp), outBuf(96 * 54 * bpp);
        PF_EffectWorld inW{inBuf.data(), static_cast<A_long>(96 * bpp), 96, 54, 0, 0};
        PF_EffectWorld ctlW{ctlBuf.data(), static_cast<A_long>(48 * bpp), 48, 27, 0, 0};
        PF_EffectWorld outW{outBuf.data(), static_cast<A_long>(96 * bpp), 96, 54, 0, 0};
        writeWorld(sceneC, &inW, PF_PixelFormat_ARGB128, 0, 0);
        Image ctlImg(48, 27, 4);
        for (int y = 0; y < 27; ++y)
            for (int x = 0; x < 48; ++x) {
                float* px = ctlImg.row(y) + x * 4;
                px[0] = px[1] = px[2] = x < 24 ? 1.0f : 0.0f;  // the left half of the control is white
                px[3] = 1.0f;
            }
        writeWorld(ctlImg, &ctlW, PF_PixelFormat_ARGB128, 0, 0);
        g_in = &inW; g_out = &outW;
        number(P_LENGTH, 80.0);
        number(P_CONTROL_MATTE, 0.0);
        number(P_CONTROL_EMIT, 100.0);

        auto render = [&](Image& got) {
            CHECK(EffectMain(PF_Cmd_SMART_RENDER, &in, &out, params, nullptr, &sre) == 0, "control render");
            readWorld(&outW, PF_PixelFormat_ARGB128, got);
        };
        auto expect = [&](const Image* control, Image& dst) {
            Params pp = readParams(&in, 96, 54);
            Image tmp = sceneC;
            timeyum::process(pp, tmp, dst, 0.0, 24000.0 / 1001.0, nullptr, control);
        };

        // control layer, luminance, stretched from 48 x 27 to 96 x 54
        popup(2);  // Control Layer: Luminance
        prepare({0, 0, 96, 54}, {0, 0, 96, 54});
        g_past[CHECKOUT_CONTROL] = &ctlW;
        g_checkouts = g_checkins = 0;
        Image got, want;
        render(got);
        CHECK(g_checkouts == 2 && g_checkins == 2, "the render checked out %d and in %d layers (input and control, expected 2 each)", g_checkouts, g_checkins);
        const Image stretched = stretchWorld(&ctlW, PF_PixelFormat_ARGB128, 96, 54);
        CHECK(std::fabs(stretched.row(10)[10 * 4] - 1.f) < 1e-6f && std::fabs(stretched.row(10)[80 * 4]) < 1e-6f, "stretching the control layer");
        expect(&stretched, want);
        double worst = 0, streakLeft = 0, streakRight = 0;
        for (size_t i = 0; i < got.data.size(); ++i) worst = std::fmax(worst, std::fabs(got.data[i] - want.data[i]));
        for (int y = 0; y < 38; ++y) { streakLeft = std::fmax(streakLeft, got.row(y)[24 * 4] - sceneC.row(y)[24 * 4]); streakRight = std::fmax(streakRight, got.row(y)[72 * 4] - sceneC.row(y)[72 * 4]); }
        CHECK(worst < 1e-6, "control layer render differs from the core by %g", worst);
        CHECK(streakLeft > 0.01 && streakRight < 1e-6, "streaks should come from the white side only: left %g right %g", streakLeft, streakRight);

        // This Layer: Alpha reads the layer itself (alpha 1 on the left, 0.5 on the right)
        popup(8);
        prepare({0, 0, 96, 54}, {0, 0, 96, 54});
        render(got);
        expect(&sceneC, want);
        worst = 0;
        for (size_t i = 0; i < got.data.size(); ++i) worst = std::fmax(worst, std::fabs(got.data[i] - want.data[i]));
        CHECK(worst < 1e-6, "This Layer: Alpha differs from the core by %g", worst);

        // a control layer that is not there: the control is off and the plain effect is rendered
        popup(2);
        g_past[CHECKOUT_CONTROL] = nullptr;
        prepare({0, 0, 96, 54}, {0, 0, 96, 54});
        render(got);
        {
            Params pp = readParams(&in, 96, 54);
            pp.control = timeyum::kCtlOff;
            Image tmp = sceneC;
            timeyum::process(pp, tmp, want, 0.0, 24000.0 / 1001.0);
        }
        worst = 0;
        for (size_t i = 0; i < got.data.size(); ++i) worst = std::fmax(worst, std::fabs(got.data[i] - want.data[i]));
        CHECK(worst < 1e-6, "a missing control layer should give the plain effect (%g)", worst);
        mockOverrides().clear();
    }

    // The pull point is a fraction of the layer whatever the preview resolution: at half resolution the layer
    // is 96 x 54 pixels in the buffers and 192 x 108 at full size.
    {
        PF_InData half = in;
        half.downsample_x = {1, 2};
        half.downsample_y = {1, 2};
        PF_ParamDef pt; pt.u.td.x_value = static_cast<PF_Fixed>(0.3 * 96 * 65536.0); pt.u.td.y_value = static_cast<PF_Fixed>(0.6 * 54 * 65536.0);
        mockOverrides()[P_PULL_POINT] = pt;
        const Params ph = readParams(&half, 192, 108);
        CHECK(std::fabs(ph.pull_x - 0.3) < 1e-4 && std::fabs(ph.pull_y - 0.6) < 1e-4, "pull point at half resolution: (%g, %g)", ph.pull_x, ph.pull_y);
        mockOverrides().clear();
    }
    freePro();

    std::printf(g_fail ? "%d check(s) failed\n" : "all wrapper checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
