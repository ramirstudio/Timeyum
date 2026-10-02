#include "Timeyum.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "Fft.h"
#include "Kernel.h"
#include "Noise.h"
#include "Parallel.h"

namespace timeyum {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kLuma[3] = {0.2126f, 0.7152f, 0.0722f};

using Plane = std::vector<float>;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int wrapIndex(int64_t v, int n) {
    int64_t m = v % n;
    return static_cast<int>(m < 0 ? m + n : m);
}

int reflectIndex(int64_t v, int n) {
    if (n == 1) return 0;
    const int64_t period = 2LL * (n - 1);
    int64_t m = v % period;
    if (m < 0) m += period;
    return static_cast<int>(m < n ? m : period - m);
}

// ---------------------------------------------------------------------------------------------
// Smear: circular convolution with the streak kernel through the FFT
// ---------------------------------------------------------------------------------------------

struct Domain {
    int w = 0, h = 0;              // padded size
    int padTop = 0, padLeft = 0;
    std::vector<int> rowMap, colMap;  // padded index -> source index, -1 for zero
};

Domain makeDomain(const Params& p, int W, int H, double maxOffset, const double dir[2]) {
    Domain d;
    if (p.edge == kEdgeWrap) {
        const int gap = static_cast<int>(std::nearbyint(std::max(p.frame_gap, 0.0) * H));
        d.h = H + gap;
        d.w = W;
        d.rowMap.assign(d.h, -1);
        for (int y = 0; y < H; ++y) d.rowMap[y] = y;
        d.colMap.resize(d.w);
        for (int x = 0; x < W; ++x) d.colMap[x] = x;
        return d;
    }
    const double margin = maxOffset + 3.0 * p.cleanup + 2.0;
    const int py = static_cast<int>(std::ceil(margin * std::fabs(dir[1]) + 3.0 * p.cleanup + 2.0));
    const int px = static_cast<int>(std::ceil(margin * std::fabs(dir[0]) + 3.0 * p.cleanup + 2.0));
    d.h = smoothSize(H + 2 * py);
    d.w = smoothSize(W + 2 * px);
    d.padTop = py;
    d.padLeft = px;
    auto fill = [&](std::vector<int>& map, int size, int pad, int n) {
        map.resize(size);
        for (int i = 0; i < size; ++i) {
            const int64_t s = static_cast<int64_t>(i) - pad;
            switch (p.edge) {
                case kEdgeExtend: map[i] = static_cast<int>(std::min<int64_t>(std::max<int64_t>(s, 0), n - 1)); break;
                case kEdgeMirror: map[i] = reflectIndex(s, n); break;
                default: map[i] = (s >= 0 && s < n) ? static_cast<int>(s) : -1; break;
            }
        }
    };
    fill(d.rowMap, d.h, py, H);
    fill(d.colMap, d.w, px, W);
    return d;
}

std::vector<Cf> kernelSpectrum(const StreakKernel& k, const double dir[2], const Domain& d, double cleanup,
                               const double perp[2], const Fft& fx, const Fft& fy) {
    const size_t count = static_cast<size_t>(d.w) * d.h;
    std::vector<Cf> spec(count);
    for (const Tap& t : k.taps) {
        const double x = t.offset * dir[0];
        const double y = t.offset * dir[1];
        const double x0 = std::floor(x), y0 = std::floor(y);
        const float fxr = static_cast<float>(x - x0), fyr = static_cast<float>(y - y0);
        const int64_t ix = static_cast<int64_t>(x0), iy = static_cast<int64_t>(y0);
        for (int dy = 0; dy < 2; ++dy) {
            const float wy = dy ? fyr : 1.0f - fyr;
            const int yy = wrapIndex(iy + dy, d.h);
            for (int dx = 0; dx < 2; ++dx) {
                const float wx = dx ? fxr : 1.0f - fxr;
                spec[static_cast<size_t>(yy) * d.w + wrapIndex(ix + dx, d.w)].r += static_cast<float>(t.weight) * wy * wx;
            }
        }
    }
    fft2d(spec.data(), fx, fy, false);
    if (cleanup > 0.0) {
        parallelFor(d.h, [&](int y) {
            const double fyq = (y < (d.h + 1) / 2 ? y : y - d.h) / static_cast<double>(d.h);
            for (int x = 0; x < d.w; ++x) {
                const double fxq = (x < (d.w + 1) / 2 ? x : x - d.w) / static_cast<double>(d.w);
                const double proj = fxq * perp[0] + fyq * perp[1];
                const float g = static_cast<float>(std::exp(-2.0 * kPi * kPi * cleanup * cleanup * proj * proj));
                Cf& c = spec[static_cast<size_t>(y) * d.w + x];
                c.r *= g;
                c.i *= g;
            }
        });
    }
    return spec;
}

// Writes a (and b in the imaginary part) into the FFT domain, weighting each source pixel by the
// share of it that belongs to the given length level.
void fillDomain(std::vector<Cf>& z, const Plane& a, const Plane* b, int W, const Domain& d, const Plane* mPix,
                const std::vector<double>* levels, size_t level) {
    z.resize(static_cast<size_t>(d.w) * d.h);
    parallelFor(d.h, [&](int y) {
        const int sy = d.rowMap[y];
        Cf* row = z.data() + static_cast<size_t>(y) * d.w;
        if (sy < 0) {
            std::fill(row, row + d.w, Cf{});
            return;
        }
        for (int x = 0; x < d.w; ++x) {
            const int sx = d.colMap[x];
            if (sx < 0) {
                row[x] = Cf{};
            } else {
                const size_t i = static_cast<size_t>(sy) * W + sx;
                const float w = mPix ? levelWeight(*levels, level, (*mPix)[i]) : 1.f;
                row[x] = {a[i] * w, b ? (*b)[i] * w : 0.f};
            }
        }
    });
}

void extractDomain(const std::vector<Cf>& z, Plane& outA, Plane* outB, int W, int H, const Domain& d) {
    outA.resize(static_cast<size_t>(W) * H);
    if (outB) outB->resize(static_cast<size_t>(W) * H);
    parallelFor(H, [&](int y) {
        const Cf* row = z.data() + static_cast<size_t>(y + d.padTop) * d.w + d.padLeft;
        for (int x = 0; x < W; ++x) {
            outA[static_cast<size_t>(y) * W + x] = row[x].r;
            if (outB) (*outB)[static_cast<size_t>(y) * W + x] = row[x].i;
        }
    });
}

// ---------------------------------------------------------------------------------------------
// Bilinear sampling with an edge policy
// ---------------------------------------------------------------------------------------------

struct Sampler {
    int W, H, mode;  // mode: kEdgeWrap, kEdgeExtend, kEdgeMirror, kEdgeBlack

    bool index(int64_t& i, int n) const {
        if (i >= 0 && i < n) return true;
        switch (mode) {
            case kEdgeWrap: i = wrapIndex(i, n); return true;
            case kEdgeMirror: i = reflectIndex(i, n); return true;
            case kEdgeBlack: return false;
            default: i = i < 0 ? 0 : n - 1; return true;
        }
    }
    float at(const Plane& pl, int64_t x, int64_t y) const {
        if (!index(x, W) || !index(y, H)) return 0.f;
        return pl[static_cast<size_t>(y) * W + x];
    }
    float bilinear(const Plane& pl, double x, double y) const {
        const double fx = std::floor(x), fy = std::floor(y);
        const int64_t ix = static_cast<int64_t>(fx), iy = static_cast<int64_t>(fy);
        const float tx = static_cast<float>(x - fx), ty = static_cast<float>(y - fy);
        const float a = at(pl, ix, iy), b = at(pl, ix + 1, iy), c = at(pl, ix, iy + 1), d = at(pl, ix + 1, iy + 1);
        const float top = a + (b - a) * tx, bot = c + (d - c) * tx;
        return top + (bot - top) * ty;
    }
};

// ---------------------------------------------------------------------------------------------
// Frame displacement
// ---------------------------------------------------------------------------------------------

Image rollFrame(const Image& img, double rollPx, double barPx, double soft, bool hasAlpha) {
    const int H = img.height, W = img.width, C = img.channels;
    const int bar = static_cast<int>(std::nearbyint(std::max(barPx, 0.0)));
    const int P = H + bar;
    const size_t rowLen = static_cast<size_t>(W) * C;
    Image padded(W, P, C);
    std::memcpy(padded.data.data(), img.data.data(), sizeof(float) * img.data.size());
    if (bar > 0 && hasAlpha) {
        for (int y = H; y < P; ++y)
            for (int x = 0; x < W; ++x) padded.row(y)[static_cast<size_t>(x) * C + 3] = 1.0f;
    }
    if (bar > 0) {
        const double feather = std::max(soft, 0.0) * bar;
        if (feather >= 0.5) {
            for (int y = 0; y < H; ++y) {
                const float dist = static_cast<float>(std::min(y + 0.5, H - y - 0.5));
                float m = clampf(dist / static_cast<float>(feather), 0.f, 1.f);
                m = m * m * (3.f - 2.f * m);
                float* r = padded.row(y);
                for (int x = 0; x < W; ++x) {
                    float* px = r + static_cast<size_t>(x) * C;
                    px[0] *= m;
                    px[1] *= m;
                    px[2] *= m;
                    if (hasAlpha) px[3] = 1.0f - (1.0f - px[3]) * m;
                }
            }
        }
    }
    double r = std::fmod(rollPx, static_cast<double>(P));
    if (r < 0) r += P;
    const int i = static_cast<int>(std::floor(r));
    const float f = static_cast<float>(r - i);
    Image out(W, H, C);
    parallelFor(H, [&](int y) {
        const float* a = padded.row(wrapIndex(static_cast<int64_t>(y) - i, P));
        const float* b = padded.row(wrapIndex(static_cast<int64_t>(y) - i - 1, P));
        float* o = out.row(y);
        if (f > 1e-4f) {
            for (size_t k = 0; k < rowLen; ++k) o[k] = a[k] + (b[k] - a[k]) * f;
        } else {
            std::memcpy(o, a, sizeof(float) * rowLen);
        }
    });
    return out;
}

Image shiftLinear(const Image& img, double dy, double dx) {
    const int H = img.height, W = img.width, C = img.channels;
    Image cur = img;
    if (std::fabs(dy) > 1e-4) {
        Image next(W, H, C);
        parallelFor(H, [&](int y) {
            const double s = y - dy;
            const int64_t i0 = static_cast<int64_t>(std::floor(s));
            const float f = static_cast<float>(s - i0);
            const float* a = cur.row(static_cast<int>(std::min<int64_t>(std::max<int64_t>(i0, 0), H - 1)));
            const float* b = cur.row(static_cast<int>(std::min<int64_t>(std::max<int64_t>(i0 + 1, 0), H - 1)));
            float* o = next.row(y);
            for (size_t k = 0; k < static_cast<size_t>(W) * C; ++k) o[k] = a[k] + (b[k] - a[k]) * f;
        });
        cur = std::move(next);
    }
    if (std::fabs(dx) > 1e-4) {
        Image next(W, H, C);
        std::vector<int> i0(W), i1(W);
        std::vector<float> fr(W);
        for (int x = 0; x < W; ++x) {
            const double s = x - dx;
            const int64_t f0 = static_cast<int64_t>(std::floor(s));
            fr[x] = static_cast<float>(s - f0);
            i0[x] = static_cast<int>(std::min<int64_t>(std::max<int64_t>(f0, 0), W - 1));
            i1[x] = static_cast<int>(std::min<int64_t>(std::max<int64_t>(f0 + 1, 0), W - 1));
        }
        parallelFor(H, [&](int y) {
            const float* s = cur.row(y);
            float* o = next.row(y);
            for (int x = 0; x < W; ++x)
                for (int c = 0; c < C; ++c) {
                    const float a = s[static_cast<size_t>(i0[x]) * C + c], b = s[static_cast<size_t>(i1[x]) * C + c];
                    o[static_cast<size_t>(x) * C + c] = a + (b - a) * fr[x];
                }
        });
        cur = std::move(next);
    }
    return cur;
}

}  // namespace

// ---------------------------------------------------------------------------------------------

Params resolveParams(const Params& in, double frame, double fps) {
    Params p = in;
    p.weave_x = 0.0;
    p.weave_y = 0.0;
    p.breakup_seed = p.shake_seed;
    if (p.shake && p.shake_amount > 0.0) {
        const double amount = p.shake_amount;
        const double t = frame / std::max(fps, 1e-6) * std::max(p.shake_freq, 0.0);
        const int seed = p.shake_seed;
        auto n = [&](int stream) { return valueNoise(seed, stream, t, p.shake_smooth); };
        p.length = std::max(0.0, p.length * (1.0 + amount * p.shake_length * n(1)));
        p.angle = p.angle + amount * p.shake_angle * n(2);
        p.smear = std::min(std::max(p.smear * (1.0 + amount * p.shake_smear * n(3)), 0.0), 1.0);
        p.timing_shift = p.timing_shift + amount * p.shake_timing * n(4);
        p.roll = p.roll + amount * p.shake_roll * n(5);
        p.weave_x = amount * p.shake_weave_x * n(6);
        p.weave_y = amount * p.shake_weave_y * n(7);
        p.ghost_offset = p.ghost_offset * (1.0 + amount * p.shake_ghost * n(8));
        p.breakup_seed = seed + static_cast<int>(std::floor(t)) * 7919;
    }
    p.cleanup *= p.pixel_scale;
    p.breakup_scale *= p.pixel_scale;
    p.weave_x *= p.pixel_scale;
    p.weave_y *= p.pixel_scale;
    return p;
}

namespace {

// Renders one of the debugging views of the warp field.
Image warpView(const Params& p, const Image& src, const WarpField& f, int W, int H) {
    const int C = src.channels;
    Image out(W, H, C);
    const double dmax = std::max(f.dispMax, 1e-3);
    const double lmax = std::max(f.mMax, 1.0);
    parallelFor(H, [&](int y) {
        const float* s = src.row(y);
        float* o = out.row(y);
        const double v = (y + 0.5) / f.cell;
        for (int x = 0; x < W; ++x) {
            const double u = (x + 0.5) / f.cell;
            float rgb[3];
            if (p.warp_view == kViewLength) {
                rgb[0] = rgb[1] = rgb[2] = static_cast<float>(gridSample(f.length, f.gw, f.gh, u, v) / lmax);
            } else if (p.warp_view == kViewReaction) {
                rgb[0] = rgb[1] = rgb[2] = gridSample(f.reaction, f.gw, f.gh, u, v);
            } else {
                rgb[0] = static_cast<float>(0.5 + 0.5 * gridSample(f.dx, f.gw, f.gh, u, v) / dmax);
                rgb[1] = static_cast<float>(0.5 + 0.5 * gridSample(f.dy, f.gw, f.gh, u, v) / dmax);
                rgb[2] = 0.5f;
            }
            float* px = o + static_cast<size_t>(x) * C;
            px[0] = rgb[0];
            px[1] = rgb[1];
            px[2] = rgb[2];
            if (C >= 4) px[3] = 1.f;
            for (int c = 4; c < C; ++c) px[c] = s[static_cast<size_t>(x) * C + c];
        }
    });
    return out;
}

}  // namespace

void process(const Params& params, const Image& srcIn, Image& dst, double frame, double fps,
             const std::vector<LumaGrid>* history) {
    const Params p = resolveParams(params, frame, fps);
    const int W = srcIn.width, H = srcIn.height, C = srcIn.channels;
    if (W <= 0 || H <= 0 || C < 3) {
        dst = srcIn;
        return;
    }
    const size_t N = static_cast<size_t>(W) * H;
    const bool hasAlpha = C >= 4;
    const bool alphaOn = hasAlpha && p.affect_alpha;

    // Clean input (NaN -> 0, +Inf -> 65504, -Inf -> 0) and split into planes.
    Image src(W, H, C);
    parallelFor(H, [&](int y) {
        const float* s = srcIn.row(y);
        float* o = src.row(y);
        for (size_t k = 0; k < static_cast<size_t>(W) * C; ++k) {
            float v = s[k];
            if (std::isnan(v)) v = 0.f;
            else if (std::isinf(v)) v = v > 0 ? 65504.f : 0.f;
            o[k] = v;
        }
    });
    Plane rgb[3], moving[3], alpha;
    for (int c = 0; c < 3; ++c) {
        rgb[c].resize(N);
        moving[c].resize(N);
    }
    if (hasAlpha) alpha.resize(N);
    Plane ratio;
    const bool useThreshold = p.threshold > 0.0;
    if (useThreshold) ratio.resize(N);
    const float thr = static_cast<float>(p.threshold);
    const float knee = static_cast<float>(std::max(p.threshold * p.knee, 1e-5));
    parallelFor(H, [&](int y) {
        const float* s = src.row(y);
        for (int x = 0; x < W; ++x) {
            const size_t i = static_cast<size_t>(y) * W + x;
            const float* px = s + static_cast<size_t>(x) * C;
            float lin[3], pos[3];
            for (int c = 0; c < 3; ++c) {
                lin[c] = toLinear(px[c], p.colorspace);
                pos[c] = std::max(lin[c], 0.f);
                rgb[c][i] = lin[c];
            }
            float r = 1.f;
            if (useThreshold) {
                const float lum = kLuma[0] * pos[0] + kLuma[1] * pos[1] + kLuma[2] * pos[2];
                float soft = clampf(lum - thr + knee, 0.f, 2.f * knee);
                soft = soft * soft / (4.f * knee);
                r = clampf(std::max(soft, lum - thr) / std::max(lum, 1e-6f), 0.f, 1.f);
                ratio[i] = r;
            }
            for (int c = 0; c < 3; ++c) moving[c][i] = pos[c] * r;
            if (hasAlpha) alpha[i] = px[3];
        }
    });

    // Warp field: how the streak length and the displacement vary across the frame.
    const bool warpOn = p.warp && p.warp_amount > 0.0;
    WarpField field;
    if (warpOn) {
        LumaGridBuilder gb(W, H);
        for (int y = 0; y < H; ++y) {
            const size_t o = static_cast<size_t>(y) * W;
            gb.addRowPlanar(y, rgb[0].data() + o, rgb[1].data() + o, rgb[2].data() + o);
        }
        static const std::vector<LumaGrid> kNoHistory;
        field = buildWarpField(p, W, H, gb.finish(), history ? *history : kNoHistory, frame / std::max(fps, 1e-6));
        if (p.warp_view != kViewResult) {
            dst = warpView(p, src, field, W, H);
            return;
        }
    }
    const bool modulate = warpOn && field.modulates;

    const double rad = p.angle * kPi / 180.0;
    const double dir[2] = {std::cos(rad), -std::sin(rad)};
    const double perp[2] = {-dir[1], dir[0]};

    const double chroma = p.chroma;
    const double scales[3] = {std::fabs(chroma) > 1e-4 ? 1.0 + chroma : 1.0, 1.0, std::fabs(chroma) > 1e-4 ? 1.0 - chroma : 1.0};
    std::vector<double> distinct;
    int scaleIdx[3];
    for (int c = 0; c < 3; ++c) {
        auto it = std::find(distinct.begin(), distinct.end(), scales[c]);
        if (it == distinct.end()) {
            distinct.push_back(scales[c]);
            it = distinct.end() - 1;
        }
        scaleIdx[c] = static_cast<int>(it - distinct.begin());
    }
    const int midIdx = scaleIdx[1];

    // Streak length levels. Without modulation there is one level: the plain effect.
    const std::vector<double> lvM = modulate ? buildLevels(field.mMin, field.mMax, p.warp_levels) : std::vector<double>{1.0};
    struct Level {
        std::vector<StreakKernel> kernels;
        double share = 0.0;
        bool active = false;
    };
    std::vector<Level> levels(lvM.size());
    bool anyActive = false;
    double maxOff = 0.0;
    for (size_t li = 0; li < lvM.size(); ++li) {
        Level& lv = levels[li];
        if (lvM[li] <= 1e-4) continue;
        if (modulate && !levelSupported(lvM, li, field.actualMin, field.actualMax)) continue;
        Params pl = p;
        pl.length = p.length * lvM[li];
        for (double s : distinct) lv.kernels.push_back(streakKernel(pl, H, std::max(s, 0.0)));
        lv.share = lv.kernels[midIdx].share;
        lv.active = lv.share > 0.0 && !lv.kernels[midIdx].taps.empty();
        if (!lv.active) continue;
        anyActive = true;
        for (const auto& k : lv.kernels)
            for (const auto& t : k.taps) maxOff = std::max(maxOff, std::fabs(t.offset));
    }

    // Per pixel length multiplier and displacement from the grid.
    Plane mPix, dxPix, dyPix;
    if (modulate) {
        mPix.resize(N);
        parallelFor(H, [&](int y) {
            const double v = (y + 0.5) / field.cell;
            for (int x = 0; x < W; ++x) mPix[static_cast<size_t>(y) * W + x] = gridSample(field.length, field.gw, field.gh, (x + 0.5) / field.cell, v);
        });
    }
    const bool displace = warpOn && field.displaces && (anyActive || p.base_follow > 0.0);
    if (displace) {
        dxPix.resize(N);
        dyPix.resize(N);
        parallelFor(H, [&](int y) {
            const double v = (y + 0.5) / field.cell;
            for (int x = 0; x < W; ++x) {
                const double u = (x + 0.5) / field.cell;
                dxPix[static_cast<size_t>(y) * W + x] = gridSample(field.dx, field.gw, field.gh, u, v);
                dyPix[static_cast<size_t>(y) * W + x] = gridSample(field.dy, field.gw, field.gh, u, v);
            }
        });
    }

    Plane outRgb[3];
    Plane outAlpha = alpha;
    Plane smear[3], alphaMoving, alphaSmear;

    if (anyActive) {
        const Domain dom = makeDomain(p, W, H, maxOff, dir);
        const Fft fx(dom.w), fy(dom.h);
        const std::vector<double>* lvPtr = &lvM;

        if (alphaOn) {
            alphaMoving.resize(N);
            for (size_t i = 0; i < N; ++i) alphaMoving[i] = useThreshold ? alpha[i] * ratio[i] : alpha[i];
        }
        // Planes sharing a kernel go through the FFT in pairs.
        struct Job { const Plane* a; const Plane* b; Plane* outA; Plane* outB; int cls; };
        struct Plain { const Plane* in; Plane* out; int cls; };
        std::vector<Plain> planes;
        for (int c = 0; c < 3; ++c) planes.push_back({&moving[c], &smear[c], scaleIdx[c]});
        if (alphaOn) planes.push_back({&alphaMoving, &alphaSmear, midIdx});
        std::vector<Job> jobs;
        std::vector<bool> taken(planes.size(), false);
        for (size_t a = 0; a < planes.size(); ++a) {
            if (taken[a]) continue;
            taken[a] = true;
            size_t b = planes.size();
            for (size_t j = a + 1; j < planes.size(); ++j)
                if (!taken[j] && planes[j].cls == planes[a].cls) { b = j; break; }
            if (b < planes.size()) {
                taken[b] = true;
                jobs.push_back({planes[a].in, planes[b].in, planes[a].out, planes[b].out, planes[a].cls});
            } else {
                jobs.push_back({planes[a].in, nullptr, planes[a].out, nullptr, planes[a].cls});
            }
        }

        // The levels are summed in the frequency domain, so each job needs one inverse transform.
        std::vector<std::vector<Cf>> acc(jobs.size());
        std::vector<bool> started(jobs.size(), false);
        std::vector<Cf> tmp;
        for (size_t li = 0; li < levels.size(); ++li) {
            if (!levels[li].active) continue;
            std::vector<std::vector<Cf>> spectra(distinct.size());
            std::vector<bool> needed(distinct.size(), false);
            for (const Job& j : jobs) needed[j.cls] = true;
            for (size_t k = 0; k < distinct.size(); ++k)
                if (needed[k]) spectra[k] = kernelSpectrum(levels[li].kernels[k], dir, dom, std::max(p.cleanup, 0.0), perp, fx, fy);
            for (size_t ji = 0; ji < jobs.size(); ++ji) {
                const Job& j = jobs[ji];
                std::vector<Cf>& z = started[ji] ? tmp : acc[ji];
                fillDomain(z, *j.a, j.b, W, dom, modulate ? &mPix : nullptr, lvPtr, li);
                fft2d(z.data(), fx, fy, false);
                const std::vector<Cf>& sp = spectra[j.cls];
                parallelFor(dom.h, [&](int y) {
                    Cf* row = z.data() + static_cast<size_t>(y) * dom.w;
                    const Cf* s = sp.data() + static_cast<size_t>(y) * dom.w;
                    for (int x = 0; x < dom.w; ++x) {
                        const Cf v = row[x];
                        row[x] = {v.r * s[x].r - v.i * s[x].i, v.r * s[x].i + v.i * s[x].r};
                    }
                });
                if (started[ji]) {
                    Cf* a = acc[ji].data();
                    parallelFor(dom.h, [&](int y) {
                        const size_t o = static_cast<size_t>(y) * dom.w;
                        for (int x = 0; x < dom.w; ++x) {
                            a[o + x].r += tmp[o + x].r;
                            a[o + x].i += tmp[o + x].i;
                        }
                    });
                }
                started[ji] = true;
            }
        }
        for (size_t ji = 0; ji < jobs.size(); ++ji) {
            fft2d(acc[ji].data(), fx, fy, true);
            extractDomain(acc[ji], *jobs[ji].outA, jobs[ji].outB, W, H, dom);
            std::vector<Cf>().swap(acc[ji]);
        }
    }

    // Compose: base (the sharp image minus the exposure that moved) and smear.
    const bool haveSmear = anyActive;
    const float gain = static_cast<float>(std::max(p.gain, 0.0));
    const float sat = static_cast<float>(p.saturation);
    const bool doSat = std::fabs(sat - 1.f) > 1e-4f;
    const float tint[3] = {static_cast<float>(p.tint[0]), static_cast<float>(p.tint[1]), static_cast<float>(p.tint[2])};
    const bool doTint = !(std::fabs(tint[0] - 1.f) < 1e-5f && std::fabs(tint[1] - 1.f) < 1e-5f && std::fabs(tint[2] - 1.f) < 1e-5f);
    const bool doBreakup = p.breakup > 0.0;
    const double bscale = std::max(p.breakup_scale, 0.5);
    const int blend = p.blend;
    const float baseFollow = static_cast<float>(std::min(std::max(p.base_follow, 0.0), 1.0));

    for (int c = 0; c < 3; ++c) outRgb[c].resize(N);
    Plane baseP[4], smearP[4];  // only used when the result is displaced
    if (displace) {
        for (int c = 0; c < 4; ++c) {
            if (c == 3 && !alphaOn) break;
            baseP[c].resize(N);
            if (haveSmear) smearP[c].resize(N);
        }
    }

    auto blendPixel = [&](size_t i, const float* b, const float* s, float ba, float sa, float* o, float& oa) {
        for (int c = 0; c < 3; ++c) {
            switch (blend) {
                case kBlendScreen: o[c] = b[c] + s[c] - b[c] * clampf(s[c], 0.f, 1.f); break;
                case kBlendLighten: o[c] = std::max(b[c], s[c]); break;
                default: o[c] = b[c] + s[c]; break;
            }
        }
        if (alphaOn) {
            const float v = blend == kBlendExposure ? ba + sa : ba + sa - ba * clampf(sa, 0.f, 1.f);
            oa = clampf(v, 0.f, 1.f);
        }
        (void)i;
    };

    parallelFor(H, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = static_cast<size_t>(y) * W + x;
            float shareEff = 0.f;
            if (haveSmear) {
                if (modulate) {
                    for (size_t li = 0; li < levels.size(); ++li)
                        if (levels[li].active) shareEff += levelWeight(lvM, li, mPix[i]) * static_cast<float>(levels[li].share);
                } else {
                    shareEff = static_cast<float>(levels[0].share);
                }
            }
            float b[3], s[3] = {0.f, 0.f, 0.f}, ba = 0.f, sa = 0.f;
            for (int c = 0; c < 3; ++c) b[c] = (blend == kBlendExposure && haveSmear) ? rgb[c][i] - shareEff * moving[c][i] : rgb[c][i];
            if (alphaOn) ba = (blend == kBlendExposure && haveSmear) ? alpha[i] - shareEff * alphaMoving[i] : alpha[i];
            if (haveSmear) {
                for (int c = 0; c < 3; ++c) s[c] = std::max(smear[c][i], 0.f);
                if (gain != 1.f) for (float& v : s) v *= gain;
                if (doBreakup) {
                    double cc;
                    if (std::fabs(perp[1]) < 1e-6) cc = x * perp[0];
                    else if (std::fabs(perp[0]) < 1e-6) cc = y * perp[1];
                    else cc = x * perp[0] + y * perp[1];
                    const double n1 = valueNoise(p.breakup_seed, 101, cc / bscale, 1.0);
                    const double n2 = valueNoise(p.breakup_seed, 202, cc / (bscale * 0.37), 1.0);
                    const float field2 = static_cast<float>(0.7 * n1 + 0.3 * n2);
                    const float m = std::max(1.f + static_cast<float>(p.breakup) * field2, 0.f);
                    for (float& v : s) v *= m;
                }
                if (doSat) {
                    const float lum = kLuma[0] * s[0] + kLuma[1] * s[1] + kLuma[2] * s[2];
                    for (float& v : s) v = std::max(lum + (v - lum) * sat, 0.f);
                }
                if (doTint) for (int c = 0; c < 3; ++c) s[c] *= tint[c];
                if (alphaOn) sa = std::max(alphaSmear[i], 0.f) * gain;
            }
            if (displace) {
                for (int c = 0; c < 3; ++c) {
                    baseP[c][i] = b[c];
                    if (haveSmear) smearP[c][i] = s[c];
                }
                if (alphaOn) {
                    baseP[3][i] = ba;
                    if (haveSmear) smearP[3][i] = sa;
                }
            } else {
                float o[3], oa = 0.f;
                if (haveSmear) {
                    blendPixel(i, b, s, ba, sa, o, oa);
                } else {
                    for (int c = 0; c < 3; ++c) o[c] = b[c];
                    oa = ba;
                }
                for (int c = 0; c < 3; ++c) outRgb[c][i] = o[c];
                if (alphaOn) outAlpha[i] = alphaOn && haveSmear ? oa : alpha[i];
            }
        }
    });

    if (displace) {
        // The smear follows the full displacement, the sharp image only baseFollow of it.
        const Sampler smearSampler{W, H, p.edge};
        const Sampler baseSampler{W, H, p.edge == kEdgeWrap ? static_cast<int>(kEdgeWrap) : static_cast<int>(kEdgeExtend)};
        parallelFor(H, [&](int y) {
            for (int x = 0; x < W; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                const double dx = dxPix[i], dy = dyPix[i];
                float b[3], s[3] = {0.f, 0.f, 0.f}, ba = 0.f, sa = 0.f;
                for (int c = 0; c < 3; ++c) {
                    b[c] = baseFollow > 0.f ? baseSampler.bilinear(baseP[c], x + baseFollow * dx, y + baseFollow * dy) : baseP[c][i];
                    if (haveSmear) s[c] = smearSampler.bilinear(smearP[c], x + dx, y + dy);
                }
                if (alphaOn) {
                    ba = baseFollow > 0.f ? baseSampler.bilinear(baseP[3], x + baseFollow * dx, y + baseFollow * dy) : baseP[3][i];
                    if (haveSmear) sa = smearSampler.bilinear(smearP[3], x + dx, y + dy);
                }
                float o[3], oa = 0.f;
                if (haveSmear) {
                    blendPixel(i, b, s, ba, sa, o, oa);
                } else {
                    for (int c = 0; c < 3; ++c) o[c] = b[c];
                    oa = ba;
                }
                for (int c = 0; c < 3; ++c) outRgb[c][i] = o[c];
                if (alphaOn) outAlpha[i] = clampf(oa, 0.f, 1.f);
            }
        }
        );
    }

    Image result(W, H, C);
    parallelFor(H, [&](int y) {
        const float* s = src.row(y);
        float* o = result.row(y);
        for (int x = 0; x < W; ++x) {
            const size_t i = static_cast<size_t>(y) * W + x;
            float* px = o + static_cast<size_t>(x) * C;
            for (int c = 0; c < 3; ++c) px[c] = fromLinear(outRgb[c][i], p.colorspace);
            if (hasAlpha) px[3] = outAlpha[i];
            for (int c = 4; c < C; ++c) px[c] = s[static_cast<size_t>(x) * C + c];
        }
    });

    const double rollPx = p.roll * H;
    const double barPx = std::max(p.roll_bar, 0.0) * H;
    if (std::fabs(rollPx) > 1e-3 || barPx >= 0.5) result = rollFrame(result, rollPx, barPx, p.roll_bar_soft, hasAlpha);
    if (std::fabs(p.weave_x) > 1e-4 || std::fabs(p.weave_y) > 1e-4) result = shiftLinear(result, p.weave_y, p.weave_x);

    const float opacity = static_cast<float>(std::min(std::max(p.opacity, 0.0), 1.0));
    if (opacity < 1.f) {
        parallelFor(H, [&](int y) {
            const float* s = src.row(y);
            float* o = result.row(y);
            for (size_t k = 0; k < static_cast<size_t>(W) * C; ++k) o[k] = s[k] + (o[k] - s[k]) * opacity;
        });
    }
    dst = std::move(result);
}

}  // namespace timeyum
