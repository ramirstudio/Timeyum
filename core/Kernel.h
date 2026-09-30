#pragma once

#include <vector>

#include "Params.h"

namespace timeyum {

struct Tap {
    double offset;  // pixels along the streak direction
    double weight;
};

struct StreakKernel {
    std::vector<Tap> taps;
    double share = 0.0;  // fraction of the exposure that leaves the base image
};

// Streak of the moving exposure for a frame of the given height. chromaScale stretches
// the streak length (used for the per channel chromatic spread).
StreakKernel streakKernel(const Params& p, double height, double chromaScale);

}  // namespace timeyum
