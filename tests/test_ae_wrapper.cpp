// Exercises ae/TimeyumAE.cpp against the mock SDK in tests/ae_mock.
#include "../ae/TimeyumAE.cpp"

#include <cmath>
#include <cstdio>
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
static PF_EffectWorld* g_past[9];  // checkout id -> world of a past frame (id 0 is the input)
static std::vector<std::pair<A_long, A_long>> g_pastCheckouts;  // (checkout id, time) requested in pre-render
static int g_checkins = 0, g_checkouts = 0;
static PF_Err coLayerPixels(void*, A_long id, PF_EffectWorld** w) {
    ++g_checkouts;
    *w = id == 0 ? g_in : g_past[id];
    return 0;
}
static PF_Err coOutput(void*, PF_EffectWorld** w) { *w = g_out; return 0; }
static PF_Err ciLayerPixels(void*, A_long) { ++g_checkins; return 0; }
static PF_Err coLayer(void*, A_long, A_long id, const PF_RenderRequest* r, A_long time, A_long, A_long, PF_CheckoutResult* out) {
    if (id > 0) g_pastCheckouts.push_back({id, time});
    out->result_rect = r->rect;
    out->max_result_rect = r->rect;
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

    // Setup: flags, parameter table.
    CHECK(EffectMain(PF_Cmd_GLOBAL_SETUP, &in, &out, params, nullptr, nullptr) == 0, "global setup");
    CHECK(out.out_flags == TY_OUT_FLAGS && out.out_flags2 == TY_OUT_FLAGS2, "flags");
    CHECK(EffectMain(PF_Cmd_PARAMS_SETUP, &in, &out, params, nullptr, nullptr) == 0, "params setup");
    CHECK(out.num_params == P_COUNT, "num_params %d vs %d", out.num_params, P_COUNT);
    g_params.push_back({"input", "input", 0, 0, 0});
    for (const auto& m : mockRecord()) g_params.push_back(m);
    CHECK(static_cast<int>(g_params.size()) == P_COUNT, "registered %zu params, enum has %d", g_params.size(), P_COUNT);
    int depth = 0;
    for (size_t i = 1; i < g_params.size(); ++i) {
        CHECK(g_params[i].id == static_cast<int>(i), "slot %zu has id %d", i, g_params[i].id);
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
    CHECK(p.warp == d.warp && p.warp_view == d.warp_view && same(p.warp_amount, d.warp_amount), "warp defaults");
    CHECK(same(p.flow_length, d.flow_length) && same(p.flow_wave, d.flow_wave) && same(p.flow_scale, d.flow_scale) && same(p.flow_speed, d.flow_speed) &&
          p.flow_detail == d.flow_detail && p.flow_seed == d.flow_seed && same(p.drift_angle, d.drift_angle) && same(p.drift_speed, d.drift_speed), "flow defaults");
    CHECK(same(p.luma_response, d.luma_response) && same(p.luma_softness, d.luma_softness) && same(p.motion_response, d.motion_response) &&
          same(p.motion_sensitivity, d.motion_sensitivity) && same(p.inertia, d.inertia) && p.history == d.history &&
          same(p.length_reaction, d.length_reaction) && same(p.wave_reaction, d.wave_reaction), "reaction defaults");
    CHECK(same(p.pull_x, d.pull_x) && same(p.pull_y, d.pull_y) && same(p.pull_strength, d.pull_strength) && same(p.pull_radius, d.pull_radius) &&
          same(p.pull_length, d.pull_length) && same(p.auto_strength, d.auto_strength) && same(p.base_follow, d.base_follow) && p.warp_levels == d.warp_levels,
          "pull defaults: point (%g, %g)", p.pull_x, p.pull_y);

    // Enabling logic.
    std::vector<PF_ParamDef> defs(P_COUNT);
    std::vector<PF_ParamDef*> ptrs(P_COUNT);
    for (int i = 0; i < P_COUNT; ++i) { mockCheckout(&in, i, &defs[i]); ptrs[i] = &defs[i]; }
    defs[P_PROFILE].u.pd.value = 3;  // camera
    EffectMain(PF_Cmd_UPDATE_PARAMS_UI, &in, &out, ptrs.data(), nullptr, nullptr);
    CHECK(mockDisabled()[P_SMEAR] && !mockDisabled()[P_TIMING_SHIFT] && mockDisabled()[P_FALLOFF], "camera enabling");
    defs[P_PROFILE].u.pd.value = 1;
    EffectMain(PF_Cmd_UPDATE_PARAMS_UI, &in, &out, ptrs.data(), nullptr, nullptr);
    CHECK(!mockDisabled()[P_SMEAR] && mockDisabled()[P_TIMING_SHIFT] && !mockDisabled()[P_FALLOFF] && mockDisabled()[P_GHOST_COUNT] && mockDisabled()[P_SHAKE_AMOUNT], "fade enabling");

    // Warp enabling: everything but the checkbox is greyed out until it is on.
    defs[P_WARP].u.bd.value = 0;
    EffectMain(PF_Cmd_UPDATE_PARAMS_UI, &in, &out, ptrs.data(), nullptr, nullptr);
    CHECK(!mockDisabled()[P_WARP] && mockDisabled()[P_FLOW_LENGTH] && mockDisabled()[P_PULL_POINT] && mockDisabled()[P_WARP_LEVELS], "warp off enabling");
    defs[P_WARP].u.bd.value = 1;
    defs[P_MOTION_RESPONSE].u.fs_d.value = 0.0;
    defs[P_LUMA_RESPONSE].u.fs_d.value = 0.0;
    defs[P_AUTO_STRENGTH].u.fs_d.value = 0.0;
    EffectMain(PF_Cmd_UPDATE_PARAMS_UI, &in, &out, ptrs.data(), nullptr, nullptr);
    CHECK(!mockDisabled()[P_FLOW_LENGTH] && !mockDisabled()[P_PULL_POINT] && mockDisabled()[P_MOTION_SENS] && mockDisabled()[P_HISTORY] && mockDisabled()[P_LENGTH_REACTION],
          "warp on, no reaction enabling");
    defs[P_MOTION_RESPONSE].u.fs_d.value = 50.0;
    EffectMain(PF_Cmd_UPDATE_PARAMS_UI, &in, &out, ptrs.data(), nullptr, nullptr);
    CHECK(!mockDisabled()[P_MOTION_SENS] && !mockDisabled()[P_HISTORY] && !mockDisabled()[P_LENGTH_REACTION], "motion response enables its controls");

    // Pre-render.
    in.width = 96; in.height = 54;
    PF_PreRenderCallbacks prc{coLayer};
    PF_PreRenderInput pri{}; pri.output_request.rect = {10, 10, 20, 20};
    PF_PreRenderOutput pro{};
    PF_PreRenderExtra pre{&pri, &pro, &prc};
    CHECK(EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre) == 0, "pre-render");
    CHECK(pro.result_rect.right == 96 && pro.result_rect.bottom == 54 && pro.result_rect.left == 0, "pre-render requests the full layer");

    // Pre-render asks for the past frames the warp reacts to, and only then.
    {
        mockOverrides().clear();
        g_pastCheckouts.clear();
        in.current_time = 10010;
        in.time_step = 1001;
        CHECK(EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre) == 0, "pre-render without warp");
        CHECK(g_pastCheckouts.empty(), "warp off asked for %zu past frames", g_pastCheckouts.size());
        PF_ParamDef on; on.u.bd.value = 1;
        mockOverrides()[P_WARP] = on;
        CHECK(EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre) == 0, "pre-render with warp");
        CHECK(static_cast<int>(g_pastCheckouts.size()) == Params().history, "asked for %zu past frames", g_pastCheckouts.size());
        for (size_t i = 0; i < g_pastCheckouts.size(); ++i)
            CHECK(g_pastCheckouts[i].first == static_cast<A_long>(i + 1) && g_pastCheckouts[i].second == 10010 - static_cast<A_long>(i + 1) * 1001,
                  "past frame %zu: id %d time %d", i, g_pastCheckouts[i].first, g_pastCheckouts[i].second);
        PF_ParamDef noMotion; noMotion.u.fs_d.value = 0.0;
        mockOverrides()[P_MOTION_RESPONSE] = noMotion;
        mockOverrides()[P_INERTIA] = noMotion;
        g_pastCheckouts.clear();
        EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, params, nullptr, &pre);
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
    PF_SmartRenderExtra sre{&src};

    struct Case { PF_PixelFormat fmt; const char* name; double tol; int cs; };
    for (const Case& c : {Case{PF_PixelFormat_ARGB128, "32 bit", 1e-6, 0}, Case{PF_PixelFormat_ARGB64, "16 bit", 2e-4, 0},
                          Case{PF_PixelFormat_ARGB32, "8 bit", 6e-3, 0}, Case{PF_PixelFormat_ARGB32, "8 bit sRGB", 6e-3, 1}}) {
        mockFormat() = c.fmt;
        mockOverrides().clear();
        if (c.cs) { PF_ParamDef d; d.u.pd.value = 2; mockOverrides()[P_COLORSPACE] = d; }
        PF_ParamDef len; len.u.fs_d.value = 80.0; mockOverrides()[P_LENGTH] = len;

        const size_t bpp = c.fmt == PF_PixelFormat_ARGB128 ? 16 : (c.fmt == PF_PixelFormat_ARGB64 ? 8 : 4);
        const int ox = 7, oy = 5;  // output buffer is a crop with an origin offset
        const int ow = 40, oh = 30;
        std::vector<char> inBuf(96 * 54 * bpp + 64), outBuf(static_cast<size_t>(ow) * oh * bpp + 64);
        PF_EffectWorld inW{inBuf.data(), static_cast<A_long>(96 * bpp), 96, 54, 0, 0};
        PF_EffectWorld outW{outBuf.data(), static_cast<A_long>(ow * bpp), ow, oh, -ox, -oy};
        // quantise the scene exactly like the host would
        Image quant;
        {
            Image tmp = scene;
            writeWorld(tmp, &inW, c.fmt, 0, 0);
            readWorld(&inW, c.fmt, quant);
        }
        g_in = &inW; g_out = &outW;
        CHECK(EffectMain(PF_Cmd_SMART_RENDER, &in, &out, params, nullptr, &sre) == 0, "%s render call", c.name);

        Params pp = readParams(&in);
        Image expected, dst;
        Image tmp = quant;
        decodeColor(tmp, pp.colorspace);
        Params core = pp; core.colorspace = timeyum::kCsLinear;
        timeyum::process(core, tmp, dst, 0.0, 24000.0 / 1001.0);
        encodeColor(dst, pp.colorspace);
        Image got;
        readWorld(&outW, c.fmt, got);
        double worst = 0;
        for (int y = 0; y < oh; ++y)
            for (int x = 0; x < ow; ++x)
                for (int k = 0; k < 4; ++k) worst = std::fmax(worst, std::fabs(got.row(y)[x * 4 + k] - dst.row(y + oy)[(x + ox) * 4 + k]));
        CHECK(worst <= c.tol, "%s: output differs from core by %g", c.name, worst);
        std::printf("%-10s max difference to core: %.2e\n", c.name, worst);
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

    std::printf(g_fail ? "%d check(s) failed\n" : "all wrapper checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
