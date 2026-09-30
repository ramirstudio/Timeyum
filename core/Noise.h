#pragma once

#include <cstdint>

namespace timeyum {

// Deterministic 1D value noise in [-1, 1]. smooth = 0 holds each random value for one step,
// 1 interpolates over the whole step, values in between hold and then glide.
double valueNoise(int seed, int stream, double t, double smooth);

}  // namespace timeyum
