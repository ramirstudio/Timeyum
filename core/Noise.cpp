#include "Noise.h"

#include <algorithm>
#include <cmath>

namespace timeyum {

namespace {

inline double hash01(int seed, int stream, int64_t i) {
    uint32_t h = static_cast<uint32_t>(i) * 0x9E3779B1u;
    h ^= static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(seed)) * 0x85EBCA77ull +
                               static_cast<uint64_t>(static_cast<int64_t>(stream)) * 0xC2B2AE3Dull);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h / 4294967296.0;
}

}  // namespace

double valueNoise(int seed, int stream, double t, double smooth) {
    const double i0 = std::floor(t);
    double f = t - i0;
    smooth = std::min(std::max(smooth, 0.0), 1.0);
    if (smooth <= 1e-6) f = 0.0;
    else f = std::min(std::max((f - (1.0 - smooth)) / smooth, 0.0), 1.0);
    f = f * f * (3.0 - 2.0 * f);
    const double a = hash01(seed, stream, static_cast<int64_t>(i0));
    const double b = hash01(seed, stream, static_cast<int64_t>(i0) + 1);
    return (a + (b - a) * f) * 2.0 - 1.0;
}

}  // namespace timeyum
