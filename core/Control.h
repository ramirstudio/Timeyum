#pragma once

#include <vector>

#include "Image.h"
#include "Params.h"

namespace timeyum {

// Maps the picture to a 0..1 value per pixel according to the control parameters: the channel is read,
// a Z-Depth is turned into "near = 1", the levels (black and white point) and the inversion are
// applied, then the result is blurred. Returns an empty vector when the control is off or the picture
// does not have the size width x height.
std::vector<float> makeControlPlane(const Params& p, const Image& picture, int width, int height);

// Box blur applied three times, close to a Gaussian of the given sigma (pixels).
void blurPlane(std::vector<float>& plane, int width, int height, double sigma);

}  // namespace timeyum
