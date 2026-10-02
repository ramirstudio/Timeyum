// Properties of the warp: continuity in space and time, energy conservation, reaction to motion,
// the interactive point, and equivalence with the plain effect when nothing is modulated.
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "../core/Parallel.h"
#include "../core/Timeyum.h"
#include "../core/Warp.h"

using namespace timeyum;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static Image scene(int W, int H, double bx, double by, double lamp = 1.0) {
    Image img(W, H, 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float* p = img.row(y) + static_cast<size_t>(x) * 4;
            double v = 0.004 + lamp * 1.2 * std::exp(-((x - 0.78 * W) * (x - 0.78 * W) + (y - 0.3 * H) * (y - 0.3 * H)) / (0.05 * H * 0.05 * H));
            v += 1.6 * std::exp(-((x - bx * W) * (x - bx * W) + (y - by * H) * (y - by * H)) / (0.07 * H * 0.07 * H));
            p[0] = static_cast<float>(v);
            p[1] = static_cast<float>(v * 0.9);
            p[2] = static_cast<float>(v * 0.7);
            p[3] = 1.f;
        }
    return img;
}

static double maxDiff(const Image& a, const Image& b) {
    double m = 0;
    for (size_t i = 0; i < a.data.size(); ++i) m = std::fmax(m, std::fabs(a.data[i] - b.data[i]));
    return m;
}
static double meanDiff(const Image& a, const Image& b) {
    double s = 0;
    for (size_t i = 0; i < a.data.size(); ++i) s += std::fabs(a.data[i] - b.data[i]);
    return s / a.data.size();
}
static double meanRgb(const Image& a) {
    double s = 0;
    size_t n = 0;
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x)
            for (int c = 0; c < 3; ++c, ++n) s += a.row(y)[static_cast<size_t>(x) * a.channels + c];
    return s / n;
}

static Params plainWarpOff() {
    Params p;
    p.length = 0.5;
    return p;
}
static Params warpNoModulation() {
    Params p = plainWarpOff();
    p.warp = true;
    p.flow_length = 0; p.flow_wave = 0; p.length_reaction = 0; p.pull_strength = 0; p.pull_length = 0; p.auto_strength = 0;
    return p;
}

int main() {
    const int W = 192, H = 108;
    const Image a = scene(W, H, 0.3, 0.6), b = scene(W, H, 0.34, 0.58);
    Image off, on;

    // Levels form a partition of unity and contain 1.
    for (auto range : {std::pair<double, double>{0.4, 2.5}, {1.0, 3.0}, {0.0, 1.0}, {0.7, 1.0}, {1.0, 1.0}, {0.0, 4.0}}) {
        for (int k : {3, 4, 6, 9}) {
            const auto ms = buildLevels(range.first, range.second, k);
            bool hasOne = false, sorted = true;
            for (size_t i = 0; i < ms.size(); ++i) {
                if (std::fabs(ms[i] - 1.0) < 1e-12) hasOne = true;
                if (i && ms[i] <= ms[i - 1]) sorted = false;
            }
            CHECK(hasOne && sorted && static_cast<int>(ms.size()) <= k, "levels %g..%g k=%d", range.first, range.second, k);
            double worst = 0;
            for (int t = 0; t <= 200; ++t) {
                const float m = static_cast<float>(range.first + (range.second - range.first) * t / 200.0);
                double sum = 0, mean = 0;
                for (size_t i = 0; i < ms.size(); ++i) { const float w = levelWeight(ms, i, m); sum += w; mean += w * ms[i]; }
                worst = std::fmax(worst, std::fmax(std::fabs(sum - 1.0), std::fabs(mean - m)));
            }
            CHECK(worst < 1e-5, "levels %g..%g k=%d: weights do not reproduce m (%g)", range.first, range.second, k, worst);
        }
    }

    // Warp with amount 0, or with nothing to modulate, is the plain effect.
    {
        Params p = plainWarpOff();
        process(p, a, off, 5, 24);
        Params q = p;
        q.warp = true;
        q.warp_amount = 0.0;
        process(q, a, on, 5, 24);
        CHECK(maxDiff(off, on) == 0.0, "amount 0 differs from plain by %g", maxDiff(off, on));
        process(warpNoModulation(), a, on, 5, 24);
        CHECK(maxDiff(off, on) < 1e-6, "no modulation differs from plain by %g", maxDiff(off, on));
    }

    // Length modulation alone conserves the exposure (wrap edge, exposure blend).
    {
        Params p = plainWarpOff();
        p.warp = true;
        p.flow_wave = 0; p.pull_strength = 0; p.auto_strength = 0;
        std::vector<LumaGrid> hist = {makeLumaGrid(b, 0), makeLumaGrid(b, 0), makeLumaGrid(b, 0), makeLumaGrid(b, 0)};
        process(p, a, on, 9, 24, &hist);
        const double m0 = meanRgb(a), m1 = meanRgb(on);
        CHECK(std::fabs(m1 - m0) < 2e-4 * m0, "energy: in %g out %g", m0, m1);
        process(plainWarpOff(), a, off, 9, 24);
        CHECK(meanDiff(on, off) > 1e-4, "warp changed nothing (%g)", meanDiff(on, off));
    }

    // Deterministic and independent of the thread count.
    {
        Params p = plainWarpOff();
        p.warp = true;
        std::vector<LumaGrid> hist = {makeLumaGrid(b, 0), makeLumaGrid(b, 0)};
        Image r1, r2, r3;
        setMaxThreads(1);
        process(p, a, r1, 12, 24, &hist);
        setMaxThreads(4);
        process(p, a, r2, 12, 24, &hist);
        process(p, a, r3, 12, 24, &hist);
        setMaxThreads(0);
        CHECK(maxDiff(r2, r3) == 0.0, "two runs differ");
        CHECK(maxDiff(r1, r2) < 1e-6, "thread count changes the result by %g", maxDiff(r1, r2));
    }

    // The field is continuous in time and moves.
    {
        Params p = plainWarpOff();
        p.warp = true;
        p.flow_wave = 0.03;
        const LumaGrid g = makeLumaGrid(a, 0);
        const WarpField f0 = buildWarpField(p, W, H, g, {}, 3.0);
        const WarpField f1 = buildWarpField(p, W, H, g, {}, 3.0 + 1.0 / 24.0);
        const WarpField f2 = buildWarpField(p, W, H, g, {}, 4.0);
        double d1 = 0, d2 = 0, e1 = 0, e2 = 0;
        for (size_t i = 0; i < f0.length.size(); ++i) {
            d1 = std::fmax(d1, std::fabs(f1.length[i] - f0.length[i]));
            d2 = std::fmax(d2, std::fabs(f2.length[i] - f0.length[i]));
            e1 = std::fmax(e1, std::fabs(f1.dx[i] - f0.dx[i]));
            e2 = std::fmax(e2, std::fabs(f2.dx[i] - f0.dx[i]));
        }
        CHECK(d1 < 0.06 && d2 > 3.0 * d1, "length map: one frame %g, one second %g", d1, d2);
        CHECK(e1 < 0.6 && e2 > 3.0 * e1, "displacement: one frame %g px, one second %g px", e1, e2);

        Image t0, t1, t2;
        Params q = plainWarpOff();
        q.warp = true;
        q.flow_wave = 0.03;
        process(q, a, t0, 72, 24);
        process(q, a, t1, 73, 24);
        process(q, a, t2, 96, 24);
        CHECK(meanDiff(t0, t1) * 3.0 < meanDiff(t0, t2), "image: one frame %g, one second %g", meanDiff(t0, t1), meanDiff(t0, t2));
    }

    // The field is smooth in space, and the displacement has the documented size.
    {
        const int WW = 1920, HH = 1080;
        Image big(WW, HH, 3);
        const LumaGrid g = makeLumaGrid(big, 0);
        for (double amp : {0.012, 0.05, 0.3}) {
            Params p;
            p.warp = true; p.flow_wave = amp; p.pull_strength = 0; p.pull_length = 0; p.wave_reaction = 0; p.flow_length = 0.6;
            const WarpField f = buildWarpField(p, WW, HH, g, {}, 1.7);
            double maxLenGrad = 0, sq = 0;
            for (int j = 0; j < f.gh; ++j)
                for (int i = 0; i < f.gw; ++i) {
                    const size_t k = static_cast<size_t>(j) * f.gw + i;
                    sq += f.dx[k] * f.dx[k] + f.dy[k] * f.dy[k];
                    if (i + 1 < f.gw) {
                        maxLenGrad = std::fmax(maxLenGrad, std::fabs(f.length[k + 1] - f.length[k]) / f.cell * HH);
                        CHECK(std::fabs(f.length[k + 1] - f.length[k]) < 0.12, "length multiplier jumps between neighbouring nodes");
                    }
                }
            const double rms = std::sqrt(sq / f.dx.size()), rel = rms / (amp * HH);
            // The mapping x + d(x) must not fold: its Jacobian stays positive everywhere.
            double minDet = 1e9;
            for (int j = 1; j + 1 < f.gh; ++j)
                for (int i = 1; i + 1 < f.gw; ++i) {
                    const size_t k = static_cast<size_t>(j) * f.gw + i;
                    const double dxx = (f.dx[k + 1] - f.dx[k - 1]) / (2 * f.cell), dxy = (f.dx[k + f.gw] - f.dx[k - f.gw]) / (2 * f.cell);
                    const double dyx = (f.dy[k + 1] - f.dy[k - 1]) / (2 * f.cell), dyy = (f.dy[k + f.gw] - f.dy[k - f.gw]) / (2 * f.cell);
                    minDet = std::fmin(minDet, (1 + dxx) * (1 + dyy) - dxy * dyx);
                }
            CHECK(minDet > 0.1, "wave %g: the displacement folds the image (min Jacobian %g)", amp, minDet);
            CHECK(maxLenGrad < 12.0, "length multiplier changes %g per frame height", maxLenGrad);
            if (amp < 0.1) {
                CHECK(f.waveScale == 1.0, "wave %g must not be flattened (%g)", amp, f.waveScale);
                CHECK(rel > 0.3 && rel < 0.75, "wave %g: rms displacement is %.2f of the amplitude", amp, rel);
            } else {
                CHECK(f.waveScale < 1.0, "an extreme wave should have been flattened");
            }
            std::printf("wave %.3f: rms %.2f of amplitude, min Jacobian %.2f, wave scale %.2f, length slope %.2f per height\n", amp, rel, minDet, f.waveScale, maxLenGrad);
        }
    }

    // Reaction to motion and brightness, with memory.
    {
        const Image pa = scene(W, H, 0.20, 0.55, 0.0), pb = scene(W, H, 0.30, 0.55, 0.0);
        Params p;
        p.warp = true; p.luma_response = 0; p.motion_response = 1; p.flow_length = 0; p.flow_wave = 0; p.pull_strength = 0; p.pull_length = 0;
        p.length_reaction = 1; p.history = 4;
        const LumaGrid cur = makeLumaGrid(pb, 0), old = makeLumaGrid(pa, 0);
        WarpField moving = buildWarpField(p, W, H, cur, {old, old, old, old}, 0.0);
        WarpField still = buildWarpField(p, W, H, cur, {cur, cur, cur, cur}, 0.0);
        auto at = [&](const WarpField& f, double x, double y) { return gridSample(f.reaction, f.gw, f.gh, x / f.cell, y / f.cell); };
        CHECK(at(moving, 0.25 * W, 0.55 * H) > 0.4, "motion between the blobs: %g", at(moving, 0.25 * W, 0.55 * H));
        CHECK(at(moving, 0.9 * W, 0.9 * H) < 0.02, "far from the motion: %g", at(moving, 0.9 * W, 0.9 * H));
        CHECK(at(still, 0.25 * W, 0.55 * H) < 0.02, "no motion: %g", at(still, 0.25 * W, 0.55 * H));
        CHECK(std::fabs(gridSample(moving.length, moving.gw, moving.gh, 0.25 * W / moving.cell, 0.55 * H / moving.cell)) > 1.3, "streaks do not lengthen where it moves");
        // inertia: the motion happened two frames ago and the last frame is still
        Params lo = p, hi = p;
        lo.inertia = 0.0;
        hi.inertia = 1.0;
        const WarpField fl = buildWarpField(lo, W, H, cur, {cur, old, old, old}, 0.0);
        const WarpField fh = buildWarpField(hi, W, H, cur, {cur, old, old, old}, 0.0);
        CHECK(at(fh, 0.25 * W, 0.55 * H) > at(fl, 0.25 * W, 0.55 * H) + 0.05, "inertia: low %g high %g", at(fl, 0.25 * W, 0.55 * H), at(fh, 0.25 * W, 0.55 * H));
        // a missing history frame means no motion, not garbage
        const WarpField fm = buildWarpField(p, W, H, cur, {LumaGrid(), LumaGrid()}, 0.0);
        CHECK(at(fm, 0.25 * W, 0.55 * H) < 0.02, "missing history: %g", at(fm, 0.25 * W, 0.55 * H));
    }

    // Brightness reaction
    {
        Params p;
        p.warp = true; p.luma_response = 1; p.motion_response = 0; p.luma_softness = 0.02; p.flow_length = 0; p.flow_wave = 0; p.pull_strength = 0; p.pull_length = 0;
        const WarpField f = buildWarpField(p, W, H, makeLumaGrid(a, 0), {}, 0.0);
        auto at = [&](double x, double y) { return gridSample(f.reaction, f.gw, f.gh, x / f.cell, y / f.cell); };
        CHECK(at(0.78 * W, 0.3 * H) > 0.5 && at(0.5 * W, 0.95 * H) < 0.15, "brightness reaction: lamp %g, dark %g", at(0.78 * W, 0.3 * H), at(0.5 * W, 0.95 * H));
    }

    // Interactive point: bulge (+) samples toward the point, pinch (-) away from it; zero at the point, gone far away.
    {
        Params p;
        p.warp = true; p.flow_wave = 0; p.flow_length = 0; p.length_reaction = 0; p.pull_x = 0.4; p.pull_y = 0.5; p.pull_radius = 0.4; p.pull_length = 0;
        const LumaGrid g = makeLumaGrid(Image(W, H, 3), 0);
        for (double s : {0.6, -0.6}) {
            p.pull_strength = s;
            const WarpField f = buildWarpField(p, W, H, g, {}, 0.0);
            auto dx = [&](double x, double y) { return gridSample(f.dx, f.gw, f.gh, x / f.cell, y / f.cell); };
            const double right = dx(0.4 * W + 0.15 * H, 0.5 * H), left = dx(0.4 * W - 0.15 * H, 0.5 * H);
            CHECK(std::fabs(dx(0.4 * W, 0.5 * H)) < 0.5, "pull is not zero at the point");
            CHECK((s > 0) == (right < 0) && (s > 0) == (left > 0), "pull sign s=%g: right %g left %g", s, right, left);
            CHECK(std::fabs(dx(0.95 * W, 0.95 * H)) < 0.05 * std::fabs(right), "pull does not fade out: %g vs %g", dx(0.95 * W, 0.95 * H), right);
        }
        p.pull_strength = 0; p.pull_length = 1.0;
        const WarpField f = buildWarpField(p, W, H, g, {}, 0.0);
        const double near = gridSample(f.length, f.gw, f.gh, 0.4 * W / f.cell, 0.5 * H / f.cell), far = gridSample(f.length, f.gw, f.gh, 0.95 * W / f.cell, 0.95 * H / f.cell);
        CHECK(near > 1.9 && far < 1.05, "pull length: near %g far %g", near, far);
    }

    // Auto target follows the bright area.
    {
        Params p;
        p.warp = true; p.auto_strength = 0.5; p.luma_softness = 0.02; p.inertia = 0; p.flow_wave = 0; p.flow_length = 0;
        const Image blob = scene(W, H, 0.3, 0.6, 0.0);
        const WarpField f = buildWarpField(p, W, H, makeLumaGrid(blob, 0), {}, 0.0);
        CHECK(std::fabs(f.autoX - 0.3 * W) < 0.04 * W && std::fabs(f.autoY - 0.6 * H) < 0.04 * H && f.autoConf > 0.5, "auto target at (%g, %g) conf %g", f.autoX, f.autoY, f.autoConf);
        const WarpField dark = buildWarpField(p, W, H, makeLumaGrid(Image(W, H, 3), 0), {}, 0.0);
        CHECK(dark.autoConf < 0.01, "auto target on a black frame has confidence %g", dark.autoConf);
    }

    // History frames the warp asks for.
    {
        Params p;
        CHECK(warpHistoryCount(p) == 0, "warp off");
        p.warp = true;
        CHECK(warpHistoryCount(p) == p.history, "default asks for %d", warpHistoryCount(p));
        p.motion_response = 0; p.inertia = 0;
        CHECK(warpHistoryCount(p) == 0, "no motion, no inertia");
        p.inertia = 0.5;
        CHECK(warpHistoryCount(p) == p.history, "luma with inertia");
        p.history = 20;
        CHECK(warpHistoryCount(p) == 8, "capped at 8");
    }

    // Views
    {
        Params p = plainWarpOff();
        p.warp = true;
        for (int v : {kViewLength, kViewReaction, kViewDisplacement}) {
            p.warp_view = v;
            process(p, a, on, 3, 24);
            bool ok = on.width == W && on.height == H && on.channels == 4;
            for (float f : on.data) ok = ok && std::isfinite(f) && f >= -1e-6f && f <= 1.0001f;
            CHECK(ok, "view %d", v);
        }
    }

    // Pull displacement moves the sharp image too, and conserves nothing it should not.
    {
        Params p = plainWarpOff();
        p.warp = true; p.flow_length = 0; p.flow_wave = 0; p.length_reaction = 0; p.pull_length = 0; p.pull_strength = 0.8; p.pull_radius = 0.3; p.base_follow = 1.0;
        p.smear = 0.0;  // no streak at all: the warp is a pure lens
        process(p, a, on, 0, 24);
        process(plainWarpOff(), a, off, 0, 24);
        Image plain0;
        Params none;
        none.smear = 0.0;
        process(none, a, plain0, 0, 24);
        CHECK(meanDiff(on, plain0) > 1e-3, "pull without streaks did not move the image (%g)", meanDiff(on, plain0));
        CHECK(maxDiff(plain0, a) < 1e-6, "smear 0 should leave the image alone");
    }

    std::printf(g_fail ? "%d check(s) failed\n" : "all warp checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
