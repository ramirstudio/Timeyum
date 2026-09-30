#include "Kernel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace timeyum {

namespace {

constexpr double kPi = 3.14159265358979323846;

double profileWeight(const Params& p, double t) {
    if (p.profile == kProfileExponential) return std::exp(-std::max(p.decay, 0.0) * t);
    if (p.profile == kProfileCurve && p.curve.size() >= 2) {
        const double pos = std::min(std::max(t, 0.0), 1.0) * (p.curve.size() - 1);
        const size_t i = std::min(static_cast<size_t>(pos), p.curve.size() - 2);
        const double f = pos - i;
        return std::max(p.curve[i] + (p.curve[i + 1] - p.curve[i]) * f, 0.0);
    }
    const double fall = std::min(std::max(p.falloff, 0.0), 1.0);
    const double power = std::max(p.falloff_curve, 0.01);
    return std::max(1.0 - fall * std::pow(t, power), 0.0);
}

// One side of an artistic streak, sampled at quarter pixel steps.
void segment(const Params& p, double start, double length, double sign, std::vector<Tap>& out, double& total) {
    if (length <= 0.0) return;
    const int n = static_cast<int>(std::min(std::max(std::ceil(length * 4.0), 2.0), 200000.0));
    for (int k = 0; k < n; ++k) {
        const double t = (k + 0.5) / n;
        const double w = profileWeight(p, t) * (length / n);
        out.push_back({sign * (start + t * length), w});
        total += w;
    }
}

// Film displacement histogram while the shutter is open. The film rests for (360 - pulldown)
// degrees of each cycle and then moves one frame pitch during the pull-down. Returns the share
// of the exposure taken with the film still at its home position.
double cameraTaps(const Params& p, double travel, std::vector<Tap>& out) {
    const double sa = std::min(std::max(p.shutter_angle, 1.0), 360.0);
    const double pd = std::min(std::max(p.pulldown_angle, 1.0), 359.0);
    const double ease = std::min(std::max(p.claw_ease, 0.0), 1.0);
    const int n = static_cast<int>(std::min(std::max(4096.0, 16.0 * travel), 262144.0));
    const double rest = 360.0 - pd;
    std::vector<int64_t> quarter;
    quarter.reserve(n);
    int64_t home = 0;
    for (int k = 0; k < n; ++k) {
        const double ph = p.timing_shift + (k + 0.5) / n * sa;
        const double cycle = std::floor(ph / 360.0);
        const double q = ph - cycle * 360.0;
        if (cycle == 0.0 && q < rest) {
            ++home;
            continue;
        }
        const double u = std::min(std::max((q - rest) / pd, 0.0), 1.0);
        const double e = (1.0 - ease) * u + ease * (u - std::sin(2.0 * kPi * u) / (2.0 * kPi));
        quarter.push_back(static_cast<int64_t>(std::nearbyint((cycle + e) * travel * 4.0)));
    }
    if (!quarter.empty()) {
        std::sort(quarter.begin(), quarter.end());
        for (size_t i = 0; i < quarter.size();) {
            size_t j = i;
            while (j < quarter.size() && quarter[j] == quarter[i]) ++j;
            out.push_back({quarter[i] / 4.0, static_cast<double>(j - i) / n});
            i = j;
        }
    }
    return static_cast<double>(home) / n;
}

}  // namespace

StreakKernel streakKernel(const Params& p, double H, double chromaScale) {
    StreakKernel k;
    double smearShare = 0.0;
    std::vector<Tap> main;

    if (p.profile == kProfileCamera) {
        const double pitch = H * (1.0 + std::max(p.frame_gap, 0.0));
        const double travel = std::max(p.length, 0.0) * pitch * chromaScale;
        const double home = cameraTaps(p, travel, main);
        smearShare = 1.0 - home;
    } else {
        smearShare = std::min(std::max(p.smear, 0.0), 1.0);
        const double length = std::max(p.length, 0.0) * H * chromaScale;
        const double start = std::max(p.start_offset, 0.0) * H;
        const double back = p.symmetric ? length : std::max(p.back_length, 0.0) * length;
        double total = 0.0;
        segment(p, start, length, 1.0, main, total);
        segment(p, start, back, -1.0, main, total);
        if (total > 0.0 && smearShare > 0.0) {
            const double s = smearShare / total;
            for (auto& t : main) t.weight *= s;
        } else {
            main.clear();
            smearShare = 0.0;
        }
    }
    k.taps = std::move(main);

    double ghostShare = 0.0;
    if (p.ghost && p.ghost_strength > 0.0 && p.ghost_count > 0) {
        const double glen = std::max(p.ghost_length, 0.0) * H;
        for (int i = 0; i < p.ghost_count; ++i) {
            const double gw = p.ghost_strength * std::pow(1.0 - std::min(std::max(p.ghost_decay, 0.0), 1.0), i);
            if (gw <= 0.0) continue;
            const double centre = (i + 1) * p.ghost_offset * H;
            if (glen > 0.5) {
                const int n = static_cast<int>(std::min(std::max(std::ceil(glen * 4.0), 2.0), 20000.0));
                for (int j = 0; j < n; ++j) k.taps.push_back({centre + ((j + 0.5) / n - 0.5) * glen, gw / n});
            } else {
                k.taps.push_back({centre, gw});
            }
            ghostShare += gw;
        }
    }

    if (k.taps.empty()) {
        k.share = 0.0;
        return k;
    }
    double share = smearShare + ghostShare;
    if (share > 1.0) {
        for (auto& t : k.taps) t.weight /= share;
        share = 1.0;
    }
    k.share = share;
    return k;
}

}  // namespace timeyum
