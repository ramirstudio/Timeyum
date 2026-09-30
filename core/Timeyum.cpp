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

// Convolves up to two real planes at once: a goes in the real part, b in the imaginary part.
void convolvePair(const Plane& a, const Plane* b, Plane& outA, Plane* outB, int W, int H, const Domain& d,
                  const std::vector<Cf>& spec, const Fft& fx, const Fft& fy) {
    std::vector<Cf> z(static_cast<size_t>(d.w) * d.h);
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
                row[x] = {a[i], b ? (*b)[i] : 0.f};
            }
        }
    });
    fft2d(z.data(), fx, fy, false);
    parallelFor(d.h, [&](int y) {
        Cf* row = z.data() + static_cast<size_t>(y) * d.w;
        const Cf* s = spec.data() + static_cast<size_t>(y) * d.w;
        for (int x = 0; x < d.w; ++x) {
            const Cf v = row[x];
            row[x] = {v.r * s[x].r - v.i * s[x].i, v.r * s[x].i + v.i * s[x].r};
        }
    });
    fft2d(z.data(), fx, fy, true);
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

void process(const Params& params, const Image& srcIn, Image& dst, double frame, double fps) {
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
    std::vector<StreakKernel> kernels;
    for (double s : distinct) kernels.push_back(streakKernel(p, H, std::max(s, 0.0)));
    const int midIdx = scaleIdx[1];
    const double share = kernels[midIdx].share;

    Plane outRgb[3];
    for (int c = 0; c < 3; ++c) outRgb[c] = rgb[c];
    Plane outAlpha = alpha;

    if (share > 0.0) {
        double maxOff = 0.0;
        for (const auto& k : kernels)
            for (const auto& t : k.taps) maxOff = std::max(maxOff, std::fabs(t.offset));
        const Domain dom = makeDomain(p, W, H, maxOff, dir);
        const Fft fx(dom.w), fy(dom.h);
        std::vector<std::vector<Cf>> spectra;
        for (const auto& k : kernels) spectra.push_back(kernelSpectrum(k, dir, dom, std::max(p.cleanup, 0.0), perp, fx, fy));

        // Planes sharing a kernel go through the FFT in pairs.
        Plane smear[3], alphaMoving, alphaSmear;
        struct Job { const Plane* in; Plane* out; int spec; };
        std::vector<Job> jobs;
        for (int c = 0; c < 3; ++c) jobs.push_back({&moving[c], &smear[c], scaleIdx[c]});
        if (alphaOn) {
            alphaMoving.resize(N);
            for (size_t i = 0; i < N; ++i) alphaMoving[i] = useThreshold ? alpha[i] * ratio[i] : alpha[i];
            jobs.push_back({&alphaMoving, &alphaSmear, midIdx});
        }
        std::vector<bool> done(jobs.size(), false);
        for (size_t a = 0; a < jobs.size(); ++a) {
            if (done[a]) continue;
            size_t b = jobs.size();
            for (size_t j = a + 1; j < jobs.size(); ++j)
                if (!done[j] && jobs[j].spec == jobs[a].spec) { b = j; break; }
            done[a] = true;
            if (b < jobs.size()) {
                done[b] = true;
                convolvePair(*jobs[a].in, jobs[b].in, *jobs[a].out, jobs[b].out, W, H, dom, spectra[jobs[a].spec], fx, fy);
            } else {
                convolvePair(*jobs[a].in, nullptr, *jobs[a].out, nullptr, W, H, dom, spectra[jobs[a].spec], fx, fy);
            }
        }

        const float gain = static_cast<float>(std::max(p.gain, 0.0));
        const float sat = static_cast<float>(p.saturation);
        const bool doSat = std::fabs(sat - 1.f) > 1e-4f;
        const float tint[3] = {static_cast<float>(p.tint[0]), static_cast<float>(p.tint[1]), static_cast<float>(p.tint[2])};
        const bool doTint = !(std::fabs(tint[0] - 1.f) < 1e-5f && std::fabs(tint[1] - 1.f) < 1e-5f && std::fabs(tint[2] - 1.f) < 1e-5f);
        const bool doBreakup = p.breakup > 0.0;
        const double bscale = std::max(p.breakup_scale, 0.5);
        const float fshare = static_cast<float>(share);
        const int blend = p.blend;

        parallelFor(H, [&](int y) {
            for (int x = 0; x < W; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                float s[3] = {std::max(smear[0][i], 0.f), std::max(smear[1][i], 0.f), std::max(smear[2][i], 0.f)};
                if (gain != 1.f) for (float& v : s) v *= gain;
                if (doBreakup) {
                    double c;
                    if (std::fabs(perp[1]) < 1e-6) c = x * perp[0];
                    else if (std::fabs(perp[0]) < 1e-6) c = y * perp[1];
                    else c = x * perp[0] + y * perp[1];
                    const double n1 = valueNoise(p.breakup_seed, 101, c / bscale, 1.0);
                    const double n2 = valueNoise(p.breakup_seed, 202, c / (bscale * 0.37), 1.0);
                    const float field = static_cast<float>(0.7 * n1 + 0.3 * n2);
                    const float m = std::max(1.f + static_cast<float>(p.breakup) * field, 0.f);
                    for (float& v : s) v *= m;
                }
                if (doSat) {
                    const float lum = kLuma[0] * s[0] + kLuma[1] * s[1] + kLuma[2] * s[2];
                    for (float& v : s) v = std::max(lum + (v - lum) * sat, 0.f);
                }
                if (doTint) for (int c = 0; c < 3; ++c) s[c] *= tint[c];
                for (int c = 0; c < 3; ++c) {
                    const float base = rgb[c][i];
                    float o;
                    switch (blend) {
                        case kBlendAdd: o = base + s[c]; break;
                        case kBlendScreen: o = base + s[c] - base * clampf(s[c], 0.f, 1.f); break;
                        case kBlendLighten: o = std::max(base, s[c]); break;
                        default: o = base - fshare * moving[c][i] + s[c]; break;
                    }
                    outRgb[c][i] = o;
                }
                if (alphaOn) {
                    const float a = alpha[i];
                    const float am = alphaMoving[i];
                    const float as = std::max(alphaSmear[i], 0.f) * gain;
                    const float o = blend == kBlendExposure ? a - fshare * am + as : a + as - a * clampf(as, 0.f, 1.f);
                    outAlpha[i] = clampf(o, 0.f, 1.f);
                }
            }
        });
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
