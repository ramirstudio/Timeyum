// The control input: how a matte, an alpha channel or a depth map is mapped, and what it steers.
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "../core/Control.h"
#include "../core/Parallel.h"
#include "../core/Timeyum.h"
#include "../core/Warp.h"

using namespace timeyum;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const int W = 192, H = 108;

static Image dots(const std::vector<std::pair<int, int>>& at, float value = 4.f) {
    Image img(W, H, 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float* p = img.row(y) + static_cast<size_t>(x) * 4;
            p[0] = p[1] = p[2] = 0.004f;
            p[3] = 1.f;
        }
    for (auto d : at)
        for (int y = d.second - 1; y <= d.second + 1; ++y)
            for (int x = d.first - 1; x <= d.first + 1; ++x) {
                float* p = img.row(y) + static_cast<size_t>(x) * 4;
                p[0] = p[1] = p[2] = value;
            }
    return img;
}

// A control picture whose value is f(x, y), in every colour channel; alpha 1.
template <class F>
static Image picture(F f) {
    Image img(W, H, 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float* p = img.row(y) + static_cast<size_t>(x) * 4;
            p[0] = p[1] = p[2] = static_cast<float>(f(x, y));
            p[3] = 1.f;
        }
    return img;
}

static double maxDiff(const Image& a, const Image& b) {
    double m = 0;
    for (size_t i = 0; i < a.data.size(); ++i) m = std::fmax(m, std::fabs(a.data[i] - b.data[i]));
    return m;
}
static double meanRgb(const Image& a) {
    double s = 0;
    size_t n = 0;
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x)
            for (int c = 0; c < 3; ++c, ++n) s += a.row(y)[static_cast<size_t>(x) * a.channels + c];
    return s / n;
}
static float at(const Image& a, int x, int y, int c = 0) { return a.row(y)[static_cast<size_t>(x) * a.channels + c]; }

// How many rows above (x, y) carry streak light, i.e. are brighter than the source there.
static int streakRows(const Image& out, const Image& src, int x, int y) {
    int n = 0;
    for (int r = y - 3; r >= 0; --r)
        if (at(out, x, r) > at(src, x, r) + 1e-4f) ++n;
    return n;
}

static Params base() {
    Params p;
    p.length = 0.6;
    return p;
}

int main() {
    // ---- mapping ----
    {
        Params p;
        p.control = kCtlLuma;
        Image gray = picture([](int x, int) { return x / double(W - 1); });
        auto c = makeControlPlane(p, gray, W, H);
        CHECK(c.size() == static_cast<size_t>(W) * H && std::fabs(c[100] - 100.0 / (W - 1)) < 1e-5, "luma maps to itself");

        Image rgba(W, H, 4);
        for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
            float* q = rgba.data.data() + i * 4;
            q[0] = 0.2f; q[1] = 0.4f; q[2] = 0.6f; q[3] = 0.8f;
        }
        const std::pair<int, float> chans[] = {{kCtlRed, 0.2f}, {kCtlGreen, 0.4f}, {kCtlBlue, 0.6f}, {kCtlAlpha, 0.8f}};
        for (auto ch : chans) {
            p.control = ch.first;
            CHECK(std::fabs(makeControlPlane(p, rgba, W, H)[5] - ch.second) < 1e-6, "channel %d", ch.first);
        }
        p.control = kCtlLuma;
        CHECK(std::fabs(makeControlPlane(p, rgba, W, H)[5] - (0.2126f * 0.2f + 0.7152f * 0.4f + 0.0722f * 0.6f)) < 1e-6, "luma of a colour");

        // levels and inversion
        p.control = kCtlRed;
        p.control_black = 0.1;
        p.control_white = 0.3;
        CHECK(std::fabs(makeControlPlane(p, rgba, W, H)[5] - 0.5f) < 1e-5, "levels: 0.2 between 0.1 and 0.3 is 0.5");
        p.control_black = 0.0; p.control_white = 1.0;
        p.control_invert = true;
        CHECK(std::fabs(makeControlPlane(p, rgba, W, H)[5] - 0.8f) < 1e-6, "invert");
        p.control_invert = false;

        // Z-Depth: near maps to 1, far to 0, beyond far stays 0, 32 bit values above 1 are fine
        p.control = kCtlDepth;
        p.control_near = 10.0;
        p.control_far = 50.0;
        Image z = picture([](int x, int) { return x < 64 ? 10.0 : (x < 128 ? 30.0 : 400.0); });
        auto cz = makeControlPlane(p, z, W, H);
        CHECK(std::fabs(cz[10] - 1.f) < 1e-6 && std::fabs(cz[100] - 0.5f) < 1e-6 && std::fabs(cz[150]) < 1e-6, "depth: near %g mid %g beyond far %g", cz[10], cz[100], cz[150]);
        p.control_invert = true;
        CHECK(std::fabs(makeControlPlane(p, z, W, H)[10]) < 1e-6, "depth inverted");

        // a picture of the wrong size, or no picture, switches the control off
        p.control_invert = false;
        CHECK(makeControlPlane(p, Image(W + 1, H, 4), W, H).empty(), "size mismatch is ignored");
        p.control = kCtlOff;
        CHECK(makeControlPlane(p, rgba, W, H).empty(), "control off");

        // NaN and infinity in the picture do not poison the map
        Image bad = picture([](int, int) { return 0.5; });
        bad.row(3)[4 * 3] = NAN;
        bad.row(3)[4 * 4] = INFINITY;
        p.control = kCtlRed;
        for (float v : makeControlPlane(p, bad, W, H)) CHECK(std::isfinite(v) && v >= 0.f && v <= 1.f, "non finite control value");
    }

    // ---- softness ----
    {
        Params p;
        p.control = kCtlLuma;
        Image step = picture([](int x, int) { return x < W / 2 ? 0.0 : 1.0; });
        auto sharp = makeControlPlane(p, step, W, H);
        p.control_softness = 6.0;
        auto soft = makeControlPlane(p, step, W, H);
        double ms = 0, mb = 0;
        for (size_t i = 0; i < sharp.size(); ++i) { ms += sharp[i]; mb += soft[i]; }
        CHECK(std::fabs(ms - mb) / ms < 5e-3, "blur changes the average (%g vs %g)", ms / sharp.size(), mb / sharp.size());
        const size_t row = 50 * W;
        const float edge = soft[row + W / 2 - 1], far = soft[row + 10];
        CHECK(edge > 0.2f && edge < 0.5f && far < 0.01f, "blurred edge %g, far %g", edge, far);
        for (size_t x = 1; x < static_cast<size_t>(W); ++x) CHECK(soft[row + x] + 1e-6f >= soft[row + x - 1], "blur is not monotonic at %zu", x);
    }

    // ---- control off, or mismatched, changes nothing ----
    {
        const Image src = dots({{32, 90}, {96, 90}, {160, 90}});
        Image plain, withPic, mismatched;
        process(base(), src, plain, 0, 24);
        Params p = base();
        p.control = kCtlOff;
        const Image pic = picture([](int, int) { return 0.0; });
        process(p, src, withPic, 0, 24, nullptr, &pic);
        CHECK(maxDiff(plain, withPic) == 0.0, "control off changed the result by %g", maxDiff(plain, withPic));
        p.control = kCtlLuma;
        const Image wrong(W / 2, H / 2, 4);
        process(p, src, mismatched, 0, 24, nullptr, &wrong);
        CHECK(maxDiff(plain, mismatched) == 0.0, "a picture of the wrong size changed the result");
    }

    // ---- matte: where the effect shows ----
    {
        const Image src = dots({{32, 90}, {160, 90}});
        const Image left = picture([](int x, int) { return x < W / 2 ? 1.0 : 0.0; });
        Image plain, out;
        process(base(), src, plain, 0, 24);
        Params p = base();
        p.control = kCtlLuma;
        p.control_matte = 1.0;
        process(p, src, out, 0, 24, nullptr, &left);
        double inside = 0, outside = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                for (int c = 0; c < 4; ++c) {
                    const double dIn = std::fabs(at(out, x, y, c) - at(plain, x, y, c)), dOut = std::fabs(at(out, x, y, c) - at(src, x, y, c));
                    if (x < W / 2) inside = std::fmax(inside, dIn); else outside = std::fmax(outside, dOut);
                }
        CHECK(inside < 1e-6, "inside the matte the effect differs from the plain one by %g", inside);
        CHECK(outside < 1e-6, "outside the matte the picture differs from the source by %g", outside);
        CHECK(maxDiff(plain, src) > 0.1, "the plain effect did nothing");
        p.control_matte = 0.5;
        process(p, src, out, 0, 24, nullptr, &left);
        const float mid = at(out, 160, 40), s = at(src, 160, 40), pl = at(plain, 160, 40);
        CHECK(std::fabs(mid - (s + (pl - s) * 0.5f)) < 1e-6f, "half matte outside: %g expected %g", mid, s + (pl - s) * 0.5f);
        p.control_matte = 0.0;
        process(p, src, out, 0, 24, nullptr, &left);
        CHECK(maxDiff(plain, out) < 1e-6, "matte 0 should be the plain effect");
        // and the matte multiplies with the opacity
        p.control_matte = 1.0;
        p.opacity = 0.5;
        process(p, src, out, 0, 24, nullptr, &left);
        process([&] { Params q = base(); q.opacity = 0.5; return q; }(), src, plain, 0, 24);
        CHECK(std::fabs(at(out, 32, 40) - at(plain, 32, 40)) < 1e-6f, "matte and opacity");
    }

    // ---- emit: where the streaks come from ----
    {
        const Image src = dots({{40, 90}, {150, 90}});
        const Image ctl = picture([](int x, int) { return x < W / 2 ? 1.0 : 0.0; });
        Params p = base();
        Image plain, out;
        process(p, src, plain, 0, 24);
        p.control = kCtlLuma;
        p.control_emit = 1.0;
        p.control_matte = 0.0;
        process(p, src, out, 0, 24, nullptr, &ctl);
        CHECK(streakRows(out, src, 40, 90) > 20, "no streak from the white side (%d rows)", streakRows(out, src, 40, 90));
        CHECK(streakRows(out, src, 150, 90) == 0, "a streak comes from the black side (%d rows)", streakRows(out, src, 150, 90));
        CHECK(std::fabs(at(out, 150, 90) - at(src, 150, 90)) < 1e-6f, "the black-side point lost light to a streak that is not there");
        CHECK(std::fabs(at(out, 40, 40) - at(plain, 40, 40)) < 1e-6f, "the white-side streak differs from the plain one");
        CHECK(std::fabs(meanRgb(out) - meanRgb(src)) < 2e-4 * meanRgb(src), "energy: in %g out %g", meanRgb(src), meanRgb(out));
        p.control_emit = 0.5;
        process(p, src, out, 0, 24, nullptr, &ctl);
        CHECK(streakRows(out, src, 150, 90) > 20, "half emit should leave a weaker streak on the black side");
        CHECK(std::fabs(meanRgb(out) - meanRgb(src)) < 2e-4 * meanRgb(src), "energy at half emit");
    }

    // ---- length follows the control ----
    {
        const Image src = dots({{30, 100}, {96, 100}, {162, 100}});
        const Image ctl = picture([](int x, int) { return x < 64 ? 0.25 : (x < 128 ? 0.5 : 1.0); });
        Params p = base();
        Image plain, out;
        process(p, src, plain, 0, 24);
        p.control = kCtlLuma;
        p.control_length = 1.0;
        p.control_matte = 0.0;
        process(p, src, out, 0, 24, nullptr, &ctl);
        const int a = streakRows(out, src, 30, 100), b = streakRows(out, src, 96, 100), c = streakRows(out, src, 162, 100);
        const int full = streakRows(plain, src, 162, 100);
        CHECK(a < b && b < c, "the streak should grow with the control: %d %d %d", a, b, c);
        CHECK(std::abs(c - full) <= 1, "control 1 is the plain length: %d vs %d", c, full);
        CHECK(std::fabs(meanRgb(out) - meanRgb(src)) < 2e-4 * meanRgb(src), "energy with length control: in %g out %g", meanRgb(src), meanRgb(out));
        const Image zero = picture([](int, int) { return 0.0; });
        process(p, src, out, 0, 24, nullptr, &zero);
        CHECK(maxDiff(out, src) < 1e-5, "control 0 with full length control leaves streaks (%g)", maxDiff(out, src));
        p.control_length = 0.0;
        process(p, src, out, 0, 24, nullptr, &ctl);
        CHECK(maxDiff(out, plain) < 1e-6, "no length control, matte 0: the plain effect");
        // the control combines with the warp: both lengthen or shorten
        p.control_length = 1.0;
        p.warp = true;
        p.flow_length = 0.5; p.flow_wave = 0; p.length_reaction = 0; p.pull_strength = 0; p.pull_length = 0;
        process(p, src, out, 5, 24, nullptr, &ctl);
        CHECK(std::fabs(meanRgb(out) - meanRgb(src)) < 3e-4 * meanRgb(src), "energy with warp and length control: in %g out %g", meanRgb(src), meanRgb(out));
    }

    // ---- the control drives the warp ----
    {
        const Image ctl = picture([](int x, int y) { return std::hypot(x - 0.3 * W, y - 0.5 * H) < 12 ? 1.0 : 0.0; });
        Params p;
        p.control = kCtlLuma;
        p.warp = true; p.luma_response = 0; p.motion_response = 0; p.flow_length = 0; p.flow_wave = 0; p.pull_strength = 0; p.pull_length = 0;
        p.length_reaction = 1; p.control_warp = 1.0; p.luma_softness = 0.0;
        const auto plane = makeControlPlane(p, ctl, W, H);
        const auto grid = makeControlGrid(plane.data(), W, H);
        const LumaGrid g = makeLumaGrid(Image(W, H, 3), 0);
        const WarpField f = buildWarpField(p, W, H, g, {}, 0.0, &grid);
        auto react = [&](double x, double y) { return gridSample(f.reaction, f.gw, f.gh, x / f.cell, y / f.cell); };
        CHECK(react(0.3 * W, 0.5 * H) > 0.9 && react(0.9 * W, 0.9 * H) < 0.02, "reaction at the control: %g far %g", react(0.3 * W, 0.5 * H), react(0.9 * W, 0.9 * H));
        CHECK(f.modulates, "a control-driven reaction should lengthen the streaks");
        const WarpField none = buildWarpField(p, W, H, g, {}, 0.0, nullptr);
        CHECK(react(0.3 * W, 0.5 * H) > gridSample(none.reaction, none.gw, none.gh, 0.3 * W / none.cell, 0.5 * H / none.cell) + 0.5, "without the grid there is no reaction");
        p.control_warp = 0.0;
        const WarpField off = buildWarpField(p, W, H, g, {}, 0.0, &grid);
        CHECK(gridSample(off.reaction, off.gw, off.gh, 0.3 * W / off.cell, 0.5 * H / off.cell) < 1e-6f, "control_warp 0 still reacts");
    }

    // ---- the control view shows the mapped control ----
    {
        const Image src = dots({{32, 90}});
        const Image ctl = picture([](int x, int) { return x / double(W - 1); });
        Params p = base();
        p.control = kCtlLuma;
        p.control_view = true;
        p.control_invert = true;
        Image out;
        process(p, src, out, 0, 24, nullptr, &ctl);
        CHECK(std::fabs(at(out, 10, 10) - (1.0f - 10.0f / (W - 1))) < 1e-5f && std::fabs(at(out, 10, 10, 1) - at(out, 10, 10)) < 1e-9f && at(out, 10, 10, 3) == 1.f, "control view");
    }

    // ---- deterministic and independent of the thread count ----
    {
        const Image src = dots({{40, 90}, {150, 90}});
        const Image ctl = picture([](int x, int y) { return 0.5 + 0.5 * std::sin(x * 0.07 + y * 0.03); });
        Params p = base();
        p.control = kCtlLuma;
        p.control_emit = 0.7; p.control_length = 0.6; p.control_matte = 0.8; p.control_softness = 3.0;
        p.warp = true; p.control_warp = 1.0;
        Image a, b, c;
        setMaxThreads(1);
        process(p, src, a, 3, 24, nullptr, &ctl);
        setMaxThreads(4);
        process(p, src, b, 3, 24, nullptr, &ctl);
        process(p, src, c, 3, 24, nullptr, &ctl);
        setMaxThreads(0);
        CHECK(maxDiff(b, c) == 0.0, "two runs differ");
        CHECK(maxDiff(a, b) < 1e-5, "thread count changes the result by %g", maxDiff(a, b));
        bool finite = true;
        for (float v : b.data) finite = finite && std::isfinite(v);
        CHECK(finite, "non finite output");
    }

    std::printf(g_fail ? "%d check(s) failed\n" : "all control checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
