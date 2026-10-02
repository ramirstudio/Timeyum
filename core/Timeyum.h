#pragma once

#include "Image.h"
#include "Params.h"
#include "Warp.h"

namespace timeyum {

// Params with the per-frame shake jitter applied and pixel quantities scaled by pixel_scale.
Params resolveParams(const Params& in, double frame, double fps);

// Applies the timing shift look. src and dst are (H, W, 3|4) float images; dst is resized.
// Colour values are interpreted in p.colorspace and returned in the same space.
// history[0] is the frame before the current one, history[1] the one before that, and so on;
// empty entries mean "not available". Only the warp reaction looks at it (see warpHistoryCount).
void process(const Params& p, const Image& src, Image& dst, double frame, double fps,
             const std::vector<LumaGrid>* history = nullptr);

// Colour transfer functions, exposed for the CLI and the tests.
float toLinear(float v, int colorspace);
float fromLinear(float v, int colorspace);

}  // namespace timeyum
