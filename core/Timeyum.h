#pragma once

#include "Image.h"
#include "Params.h"

namespace timeyum {

// Params with the per-frame shake jitter applied and pixel quantities scaled by pixel_scale.
Params resolveParams(const Params& in, double frame, double fps);

// Applies the timing shift look. src and dst are (H, W, 3|4) float images; dst is resized.
// Colour values are interpreted in p.colorspace and returned in the same space.
void process(const Params& p, const Image& src, Image& dst, double frame, double fps);

// Colour transfer functions, exposed for the CLI and the tests.
float toLinear(float v, int colorspace);
float fromLinear(float v, int colorspace);

}  // namespace timeyum
