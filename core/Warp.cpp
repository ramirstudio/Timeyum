#include "Warp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Parallel.h"
#include "Timeyum.h"

namespace timeyum {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Calibration: with this factor the RMS displacement of the flow equals about half of flow_wave.
constexpr double kWaveGain = 0.62;
constexpr double kMaxLengthMultiplier = 4.0;

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Tone mapped brightness: 0.25 maps to about 0.62 and 1.0 to 1.0, so dim areas still respond.
inline float toneLuma(float l) {
    l = std::max(l, 0.f);
    return std::min(1.f, 1.25f * l / (l + 0.25f));
}

// ---- 3D lattice noise ------------------------------------------------------------------------

inline double lattice(int seed, int stream, int i, int j, int k) {
    uint32_t h = static_cast<uint32_t>(i) * 0x9E3779B1u ^ static_cast<uint32_t>(j) * 0x85EBCA77u ^ static_cast<uint32_t>(k) * 0xC2B2AE3Du;
    h ^= static_cast<uint32_t>(seed) * 0x27D4EB2Fu + static_cast<uint32_t>(stream) * 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h / 2147483648.0 - 1.0;
}

inline double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

double noise3(int seed, int stream, double x, double y, double z) {
    const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int i = static_cast<int>(fx), j = static_cast<int>(fy), k = static_cast<int>(fz);
    const double u = fade(x - fx), v = fade(y - fy), w = fade(z - fz);
    double c[2][2][2];
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b)
            for (int d = 0; d < 2; ++d) c[a][b][d] = lattice(seed, stream, i + a, j + b, k + d);
    double x00 = c[0][0][0] + (c[1][0][0] - c[0][0][0]) * u;
    double x10 = c[0][1][0] + (c[1][1][0] - c[0][1][0]) * u;
    double x01 = c[0][0][1] + (c[1][0][1] - c[0][0][1]) * u;
    double x11 = c[0][1][1] + (c[1][1][1] - c[0][1][1]) * u;
    const double y0 = x00 + (x10 - x00) * v, y1 = x01 + (x11 - x01) * v;
    return y0 + (y1 - y0) * w;
}

// Smooth multi octave noise, roughly in -1..1. Higher octaves evolve a little faster in time.
// gain is the amplitude ratio between octaves: the displacement uses the gradient of the noise, which
// weighs each octave by its frequency, so it needs a lower gain than the length map to stay smooth.
double fbm(int seed, int stream, double x, double y, double z, int octaves, double gain) {
    double sum = 0.0, amp = 1.0, norm = 0.0, freq = 1.0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * noise3(seed, stream, x * freq + 17.3 * o, y * freq + 31.7 * o, z * (1.0 + 0.5 * o));
        norm += amp;
        amp *= gain;
        freq *= 2.0;
    }
    return sum / norm;
}

void gaussBlur(std::vector<float>& g, int gw, int gh, double sigma) {
    if (sigma < 0.3 || g.empty()) return;
    sigma = std::min(sigma, 60.0);
    const int r = static_cast<int>(std::ceil(3.0 * sigma));
    std::vector<float> k(2 * r + 1);
    double sum = 0.0;
    for (int i = -r; i <= r; ++i) sum += (k[i + r] = static_cast<float>(std::exp(-0.5 * i * i / (sigma * sigma))));
    for (float& v : k) v = static_cast<float>(v / sum);
    std::vector<float> tmp(g.size());
    parallelFor(gh, [&](int y) {
        for (int x = 0; x < gw; ++x) {
            float s = 0.f;
            for (int i = -r; i <= r; ++i) s += k[i + r] * g[static_cast<size_t>(y) * gw + std::min(std::max(x + i, 0), gw - 1)];
            tmp[static_cast<size_t>(y) * gw + x] = s;
        }
    });
    parallelFor(gh, [&](int y) {
        for (int x = 0; x < gw; ++x) {
            float s = 0.f;
            for (int i = -r; i <= r; ++i) s += k[i + r] * tmp[static_cast<size_t>(std::min(std::max(y + i, 0), gh - 1)) * gw + x];
            g[static_cast<size_t>(y) * gw + x] = s;
        }
    });
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Luma grid
// ---------------------------------------------------------------------------------------------

double warpCellSize(int width, int height) { return std::max(1.0, std::max(width, height) / 192.0); }

LumaGridBuilder::LumaGridBuilder(int width, int height) : width_(width) {
    cell_ = warpCellSize(width, height);
    gw_ = static_cast<int>(std::floor(width / cell_ + 0.5)) + 1;
    gh_ = static_cast<int>(std::floor(height / cell_ + 0.5)) + 1;
    sum_.assign(static_cast<size_t>(gw_) * gh_, 0.f);
    cnt_.assign(sum_.size(), 0.f);
    colNode_.resize(width);
    for (int x = 0; x < width; ++x) colNode_[x] = std::min(static_cast<int>(std::floor((x + 0.5) / cell_ + 0.5)), gw_ - 1);
}

void LumaGridBuilder::addLuma(int y, int x, float luma) {
    const int j = std::min(static_cast<int>(std::floor((y + 0.5) / cell_ + 0.5)), gh_ - 1);
    const size_t i = static_cast<size_t>(j) * gw_ + colNode_[x];
    sum_[i] += toneLuma(luma);
    cnt_[i] += 1.f;
}

void LumaGridBuilder::addRow(int y, const float* rgba, int channels, int colorspace) {
    for (int x = 0; x < width_; ++x) {
        const float* p = rgba + static_cast<size_t>(x) * channels;
        const float l = 0.2126f * toLinear(p[0], colorspace) + 0.7152f * toLinear(p[1], colorspace) + 0.0722f * toLinear(p[2], colorspace);
        addLuma(y, x, l);
    }
}

void LumaGridBuilder::addRowPlanar(int y, const float* r, const float* g, const float* b) {
    for (int x = 0; x < width_; ++x) addLuma(y, x, 0.2126f * r[x] + 0.7152f * g[x] + 0.0722f * b[x]);
}

LumaGrid LumaGridBuilder::finish() {
    LumaGrid g;
    g.w = gw_;
    g.h = gh_;
    g.cell = cell_;
    g.v.resize(sum_.size());
    for (size_t i = 0; i < sum_.size(); ++i) g.v[i] = cnt_[i] > 0.f ? sum_[i] / cnt_[i] : 0.f;
    return g;
}

LumaGrid makeLumaGrid(const Image& img, int colorspace) {
    LumaGridBuilder b(img.width, img.height);
    for (int y = 0; y < img.height; ++y) b.addRow(y, img.row(y), img.channels, colorspace);
    return b.finish();
}

int warpHistoryCount(const Params& p) {
    if (!p.warp || p.warp_amount <= 0.0 || p.history <= 0) return 0;
    const bool motion = p.motion_response > 0.0;
    const bool smooth = (p.luma_response > 0.0 || p.auto_strength != 0.0) && p.inertia > 0.0;
    return (motion || smooth) ? std::min(p.history, 8) : 0;
}

float gridSample(const std::vector<float>& g, int gw, int gh, double u, double v) {
    u = std::min(std::max(u, 0.0), static_cast<double>(gw - 1));
    v = std::min(std::max(v, 0.0), static_cast<double>(gh - 1));
    const int i0 = std::min(static_cast<int>(u), gw - 1), j0 = std::min(static_cast<int>(v), gh - 1);
    const int i1 = std::min(i0 + 1, gw - 1), j1 = std::min(j0 + 1, gh - 1);
    const float fu = static_cast<float>(u - i0), fv = static_cast<float>(v - j0);
    const float a = g[static_cast<size_t>(j0) * gw + i0], b = g[static_cast<size_t>(j0) * gw + i1];
    const float c = g[static_cast<size_t>(j1) * gw + i0], d = g[static_cast<size_t>(j1) * gw + i1];
    const float top = a + (b - a) * fu, bot = c + (d - c) * fu;
    return top + (bot - top) * fv;
}

// ---------------------------------------------------------------------------------------------
// Levels
// ---------------------------------------------------------------------------------------------

std::vector<double> buildLevels(double mMin, double mMax, int count) {
    count = std::min(std::max(count, 3), 12);
    const double lowSpan = std::max(0.0, 1.0 - mMin), highSpan = std::max(0.0, mMax - 1.0);
    if (lowSpan < 1e-6 && highSpan < 1e-6) return {1.0};
    const int segs = count - 1;
    int nLow = 0, nHigh = 0;
    if (lowSpan < 1e-6) {
        nHigh = segs;
    } else if (highSpan < 1e-6) {
        nLow = segs;
    } else {
        nLow = std::min(std::max(static_cast<int>(std::lround(segs * lowSpan / (lowSpan + highSpan))), 1), segs - 1);
        nHigh = segs - nLow;
    }
    std::vector<double> m;
    if (nLow > 0) {
        for (int i = 0; i < nLow; ++i) m.push_back(mMin + lowSpan * i / nLow);
        m.push_back(1.0);
    } else {
        m.push_back(1.0);
    }
    for (int j = 1; j <= nHigh; ++j) m.push_back(1.0 + highSpan * j / nHigh);
    return m;
}

float levelWeight(const std::vector<double>& ms, size_t li, float m) {
    const size_t n = ms.size();
    if (n == 1) return 1.f;
    const double lo = li > 0 ? ms[li - 1] : -1e30, mid = ms[li], hi = li + 1 < n ? ms[li + 1] : 1e30;
    if (m <= lo || m >= hi) return 0.f;
    if (m <= mid) return li > 0 ? static_cast<float>((m - lo) / (mid - lo)) : 1.f;
    return li + 1 < n ? static_cast<float>((hi - m) / (hi - mid)) : 1.f;
}

bool levelSupported(const std::vector<double>& ms, size_t li, double mMin, double mMax) {
    const size_t n = ms.size();
    if (n == 1) return true;
    const double lo = li > 0 ? ms[li - 1] : -1e30, hi = li + 1 < n ? ms[li + 1] : 1e30;
    return mMax > lo && mMin < hi;
}

// ---------------------------------------------------------------------------------------------
// Field
// ---------------------------------------------------------------------------------------------

std::vector<float> makeControlGrid(const float* plane, int W, int H) {
    const double cell = warpCellSize(W, H);
    const int gw = static_cast<int>(std::floor(W / cell + 0.5)) + 1, gh = static_cast<int>(std::floor(H / cell + 0.5)) + 1;
    std::vector<float> sum(static_cast<size_t>(gw) * gh, 0.f), cnt(sum.size(), 0.f);
    std::vector<int> col(W);
    for (int x = 0; x < W; ++x) col[x] = std::min(static_cast<int>(std::floor((x + 0.5) / cell + 0.5)), gw - 1);
    for (int y = 0; y < H; ++y) {
        const int j = std::min(static_cast<int>(std::floor((y + 0.5) / cell + 0.5)), gh - 1);
        for (int x = 0; x < W; ++x) {
            const size_t i = static_cast<size_t>(j) * gw + col[x];
            sum[i] += plane[static_cast<size_t>(y) * W + x];
            cnt[i] += 1.f;
        }
    }
    for (size_t i = 0; i < sum.size(); ++i) sum[i] = cnt[i] > 0.f ? sum[i] / cnt[i] : 0.f;
    return sum;
}

WarpField buildWarpField(const Params& p, int W, int H, const LumaGrid& cur, const std::vector<LumaGrid>& hist, double t,
                         const std::vector<float>* ctlGrid) {
    WarpField f;
    f.gw = cur.w;
    f.gh = cur.h;
    f.cell = cur.cell;
    const int gw = f.gw, gh = f.gh;
    const size_t n = static_cast<size_t>(gw) * gh;
    const double amount = std::max(p.warp_amount, 0.0);
    const double cell = f.cell;
    f.length.assign(n, 1.f);
    f.dx.assign(n, 0.f);
    f.dy.assign(n, 0.f);
    f.reaction.assign(n, 0.f);

    auto valid = [&](const LumaGrid& g) { return g.w == gw && g.h == gh && !g.v.empty(); };
    const double inertia = clamp01(p.inertia);
    const double decay = 0.15 + 0.8 * inertia;
    const double sigma = p.luma_softness * H / cell;

    // Brightness, smoothed over the past frames when the reaction has inertia.
    const bool needLuma = p.luma_response > 0.0 || p.auto_strength != 0.0;
    std::vector<float> luma;
    if (needLuma) {
        luma = cur.v;
        if (inertia > 0.0) {
            double wsum = 1.0, w = 1.0;
            for (const LumaGrid& g : hist) {
                if (!valid(g)) break;
                w *= decay;
                for (size_t i = 0; i < n; ++i) luma[i] += static_cast<float>(w) * g.v[i];
                wsum += w;
            }
            for (float& v : luma) v = static_cast<float>(v / wsum);
        }
        gaussBlur(luma, gw, gh, sigma);
    }

    // Motion: weighted mean of the frame to frame differences over the recent past.
    std::vector<float> motion;
    if (p.motion_response > 0.0) {
        std::vector<float> acc(n, 0.f);
        double wsum = 0.0, w = 1.0;
        for (size_t j = 0; j < hist.size(); ++j) {
            const LumaGrid& a = j == 0 ? cur : hist[j - 1];
            const LumaGrid& b = hist[j];
            if (!valid(a) || !valid(b)) break;
            for (size_t i = 0; i < n; ++i) acc[i] += static_cast<float>(w) * std::fabs(a.v[i] - b.v[i]);
            wsum += w;
            w *= decay;
        }
        if (wsum > 0.0) {
            const double k = std::max(p.motion_sensitivity, 0.0) * 2.0 / wsum;
            motion.resize(n);
            for (size_t i = 0; i < n; ++i) motion[i] = static_cast<float>(1.0 - std::exp(-k * acc[i]));
            gaussBlur(motion, gw, gh, sigma);
        }
    }

    // Reaction of the video and of the control input: a OR b OR c.
    const double lr = clamp01(p.luma_response), mr = clamp01(p.motion_response), cr = clamp01(p.control_warp);
    const bool haveCtl = ctlGrid && ctlGrid->size() == n && cr > 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double a = needLuma ? lr * clamp01(luma[i]) : 0.0;
        const double b = motion.empty() ? 0.0 : mr * clamp01(motion[i]);
        const double c = haveCtl ? cr * clamp01((*ctlGrid)[i]) : 0.0;
        f.reaction[i] = static_cast<float>(1.0 - (1.0 - a) * (1.0 - b) * (1.0 - c));
    }

    // Attractors: the interactive point and the bright area of the video.
    struct Attractor { double x, y, strength, weight; };
    std::vector<Attractor> att;
    att.push_back({p.pull_x * W, p.pull_y * H, p.pull_strength, 1.0});
    if (p.auto_strength != 0.0 && !luma.empty()) {
        // Centroid of what is brighter than the frame average, so a uniform frame has no target.
        double mean = 0.0;
        for (float v : luma) mean += v;
        mean /= static_cast<double>(n);
        double sw = 0.0, sx = 0.0, sy = 0.0;
        for (int j = 0; j < gh; ++j)
            for (int i = 0; i < gw; ++i) {
                const double l = std::max(0.0, luma[static_cast<size_t>(j) * gw + i] - mean);
                const double w2 = l * l;
                sw += w2;
                sx += w2 * i * cell;
                sy += w2 * j * cell;
            }
        if (sw > 1e-9) {
            f.autoX = sx / sw;
            f.autoY = sy / sw;
            f.autoConf = clamp01(std::sqrt(sw / n) * 10.0);
            att.push_back({f.autoX, f.autoY, p.auto_strength * f.autoConf, f.autoConf});
        }
    }

    const bool reactive = p.luma_response > 0.0 || p.motion_response > 0.0 || haveCtl;
    const double flowLen = std::max(p.flow_length, 0.0);
    const double lenReact = reactive ? std::max(p.length_reaction, 0.0) : 0.0;
    const double pullLen = std::max(p.pull_length, 0.0);
    const double waveAmp = std::max(p.flow_wave, 0.0) * H;
    const double wr = clamp01(p.wave_reaction);

    f.modulates = amount > 0.0 && (flowLen > 0.0 || lenReact > 0.0 || pullLen > 0.0);
    f.displaces = amount > 0.0 && (waveAmp > 0.0 || p.pull_strength != 0.0 || p.auto_strength != 0.0);
    if (f.modulates) {
        f.mMin = std::max(0.0, 1.0 - amount * flowLen);
        f.mMax = std::min(kMaxLengthMultiplier, 1.0 + amount * (flowLen + lenReact + pullLen));
    }

    // Flow
    const int octaves = std::min(std::max(p.flow_detail, 1), 6);
    const double S = std::max(p.flow_scale, 0.02) * H;
    const double drift = p.drift_speed * H;
    const double rad = p.drift_angle * kPi / 180.0;
    const double ox = std::cos(rad) * drift * t, oy = -std::sin(rad) * drift * t;
    const double tau = t * p.flow_speed;
    const int seed = p.flow_seed;

    std::vector<float> psi;  // wave potential with a one node border for the central differences
    const int pw = gw + 2, ph = gh + 2;
    if (waveAmp > 0.0) {
        psi.resize(static_cast<size_t>(pw) * ph);
        parallelFor(ph, [&](int j) {
            for (int i = 0; i < pw; ++i) {
                const double x = (i - 1) * cell - ox, y = (j - 1) * cell - oy;
                psi[static_cast<size_t>(j) * pw + i] = static_cast<float>(fbm(seed, 2, x / S, y / S, tau, octaves, 0.27));
            }
        });
    }
    const double gradScale = S / cell * 0.5;  // lattice units per grid step, with the 1/2 of the central difference
    std::vector<float> waveX(n, 0.f), waveY(n, 0.f);

    parallelFor(gh, [&](int j) {
        for (int i = 0; i < gw; ++i) {
            const size_t idx = static_cast<size_t>(j) * gw + i;
            const double x = i * cell, y = j * cell;
            const double D = f.reaction[idx];

            double flowL = 0.0;
            if (flowLen > 0.0) flowL = std::tanh(1.6 * fbm(seed, 1, (x - ox) / S, (y - oy) / S, tau, octaves, 0.45));

            double wsum = 0.0, px = 0.0, py = 0.0;
            const double R = std::max(p.pull_radius, 0.01) * H;
            for (size_t a = 0; a < att.size(); ++a) {
                const double ddx = att[a].x - x, ddy = att[a].y - y;
                const double w = std::exp(-2.5 * (ddx * ddx + ddy * ddy) / (R * R));
                wsum += w * att[a].weight;
                const double s = att[a].strength;
                const double fac = (s > 0.0 ? 0.9 : 1.5) * s * w;
                px += fac * ddx;
                py += fac * ddy;
            }

            f.length[idx] = static_cast<float>(std::min(std::max(1.0 + amount * (flowLen * flowL + lenReact * D + pullLen * clamp01(wsum)), 0.0), kMaxLengthMultiplier));

            double wx = 0.0, wy = 0.0;
            if (waveAmp > 0.0) {
                const size_t c = static_cast<size_t>(j + 1) * pw + (i + 1);
                const double dpdu = (psi[c + 1] - psi[c - 1]) * gradScale;
                const double dpdv = (psi[c + pw] - psi[c - pw]) * gradScale;
                const double gate = (1.0 - wr) + wr * D;
                const double k = waveAmp * kWaveGain * gate;
                wx = k * dpdv;
                wy = -k * dpdu;
            }
            waveX[idx] = static_cast<float>(wx);
            waveY[idx] = static_cast<float>(wy);
            f.dx[idx] = static_cast<float>(px);
            f.dy[idx] = static_cast<float>(py);
        }
    });

    // Keep x + d(x) free of folds: if the Jacobian of the mapping gets too small, flatten the wave
    // (not the pull, which is fold free by construction) by the largest factor that restores it.
    auto minDet = [&](double c) {
        double m = 1e9;
        for (int j = 1; j + 1 < gh; ++j)
            for (int i = 1; i + 1 < gw; ++i) {
                const size_t k = static_cast<size_t>(j) * gw + i;
                auto D = [&](size_t q, const std::vector<float>& wv, const std::vector<float>& pv) { return amount * (c * wv[q] + pv[q]); };
                const double dxx = (D(k + 1, waveX, f.dx) - D(k - 1, waveX, f.dx)) / (2.0 * cell);
                const double dxy = (D(k + gw, waveX, f.dx) - D(k - gw, waveX, f.dx)) / (2.0 * cell);
                const double dyx = (D(k + 1, waveY, f.dy) - D(k - 1, waveY, f.dy)) / (2.0 * cell);
                const double dyy = (D(k + gw, waveY, f.dy) - D(k - gw, waveY, f.dy)) / (2.0 * cell);
                m = std::min(m, (1.0 + dxx) * (1.0 + dyy) - dxy * dyx);
            }
        return m;
    };
    constexpr double kMinJacobian = 0.15;
    if (waveAmp > 0.0 && gw > 2 && gh > 2 && minDet(1.0) < kMinJacobian) {
        double lo = 0.0, hi = 1.0;
        for (int it = 0; it < 14; ++it) {
            const double mid = 0.5 * (lo + hi);
            (minDet(mid) >= kMinJacobian ? lo : hi) = mid;
        }
        f.waveScale = lo;
    }
    for (size_t i = 0; i < n; ++i) {
        f.dx[i] = static_cast<float>(amount * (f.waveScale * waveX[i] + f.dx[i]));
        f.dy[i] = static_cast<float>(amount * (f.waveScale * waveY[i] + f.dy[i]));
    }

    f.actualMin = f.actualMax = f.length.empty() ? 1.0 : f.length[0];
    for (size_t i = 0; i < n; ++i) {
        f.actualMin = std::min<double>(f.actualMin, f.length[i]);
        f.actualMax = std::max<double>(f.actualMax, f.length[i]);
        f.dispMax = std::max<double>(f.dispMax, std::hypot(f.dx[i], f.dy[i]));
    }
    return f;
}

}  // namespace timeyum
