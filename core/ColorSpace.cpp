#include <algorithm>
#include <cmath>

#include "Timeyum.h"

namespace timeyum {

namespace {

// ARRI LogC3, EI 800
constexpr double kLcA = 5.555556, kLcB = 0.052272, kLcC = 0.247190, kLcD = 0.385537;
constexpr double kLcE = 5.367655, kLcF = 0.092809, kLcCut = 0.010591;

double srgbDecode(double x) { return x <= 0.04045 ? x / 12.92 : std::pow((std::max(x, 0.0) + 0.055) / 1.055, 2.4); }
double srgbEncode(double x) { return x <= 0.0031308 ? x * 12.92 : 1.055 * std::pow(std::max(x, 0.0), 1.0 / 2.4) - 0.055; }

double logc3Decode(double t) {
    if (t > kLcE * kLcCut + kLcF) return (std::pow(10.0, (t - kLcD) / kLcC) - kLcB) / kLcA;
    return (t - kLcF) / kLcE;
}
double logc3Encode(double x) {
    if (x > kLcCut) return kLcC * std::log10(std::max(kLcA * x + kLcB, 1e-10)) + kLcD;
    return kLcE * x + kLcF;
}

// Sony S-Log3
constexpr double kSlCut = 171.2102946929;
double slog3Decode(double t) {
    if (t >= kSlCut / 1023.0) return std::pow(10.0, (t * 1023.0 - 420.0) / 261.5) * (0.18 + 0.01) - 0.01;
    return (t * 1023.0 - 95.0) * 0.01125 / (kSlCut - 95.0);
}
double slog3Encode(double x) {
    if (x >= 0.01125) return (420.0 + std::log10(std::max((x + 0.01) / (0.18 + 0.01), 1e-10)) * 261.5) / 1023.0;
    return (x * (kSlCut - 95.0) / 0.01125 + 95.0) / 1023.0;
}

}  // namespace

float toLinear(float v, int cs) {
    switch (cs) {
        case kCsSrgb: return static_cast<float>(srgbDecode(v));
        case kCsGamma24: return static_cast<float>(std::pow(std::max(v, 0.0f), 2.4f));
        case kCsLogC3: return static_cast<float>(logc3Decode(v));
        case kCsSLog3: return static_cast<float>(slog3Decode(v));
        default: return v;
    }
}

float fromLinear(float v, int cs) {
    switch (cs) {
        case kCsSrgb: return static_cast<float>(srgbEncode(v));
        case kCsGamma24: return static_cast<float>(std::pow(std::max(v, 0.0f), 1.0f / 2.4f));
        case kCsLogC3: return static_cast<float>(logc3Encode(v));
        case kCsSLog3: return static_cast<float>(slog3Encode(v));
        default: return v;
    }
}

}  // namespace timeyum
