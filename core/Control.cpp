#include "Control.h"

#include <algorithm>
#include <cmath>

#include "Parallel.h"

namespace timeyum {

namespace {

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Horizontal box blur with clamped edges, in place, using a running sum.
void boxRows(std::vector<float>& a, std::vector<float>& tmp, int W, int H, int r) {
    parallelFor(H, [&](int y) {
        const float* in = a.data() + static_cast<size_t>(y) * W;
        float* out = tmp.data() + static_cast<size_t>(y) * W;
        const float inv = 1.0f / static_cast<float>(2 * r + 1);
        double sum = 0.0;
        for (int i = -r; i <= r; ++i) sum += in[std::min(std::max(i, 0), W - 1)];
        for (int x = 0; x < W; ++x) {
            out[x] = static_cast<float>(sum) * inv;
            sum += in[std::min(x + r + 1, W - 1)] - in[std::max(x - r, 0)];
        }
    });
}

void boxColumns(std::vector<float>& a, std::vector<float>& tmp, int W, int H, int r) {
    parallelFor(W, [&](int x) {
        const float inv = 1.0f / static_cast<float>(2 * r + 1);
        double sum = 0.0;
        for (int i = -r; i <= r; ++i) sum += a[static_cast<size_t>(std::min(std::max(i, 0), H - 1)) * W + x];
        for (int y = 0; y < H; ++y) {
            tmp[static_cast<size_t>(y) * W + x] = static_cast<float>(sum) * inv;
            sum += a[static_cast<size_t>(std::min(y + r + 1, H - 1)) * W + x] - a[static_cast<size_t>(std::max(y - r, 0)) * W + x];
        }
    });
}

}  // namespace

void blurPlane(std::vector<float>& plane, int W, int H, double sigma) {
    if (sigma < 0.4 || plane.empty()) return;
    // Three box passes of the same width approximate a Gaussian: width = sqrt(12 sigma^2 / 3 + 1).
    const double ideal = std::sqrt(4.0 * sigma * sigma + 1.0);
    const int r = std::max(1, static_cast<int>(std::lround((ideal - 1.0) / 2.0)));
    std::vector<float> tmp(plane.size());
    for (int pass = 0; pass < 3; ++pass) {
        boxRows(plane, tmp, W, H, r);
        plane.swap(tmp);
        boxColumns(plane, tmp, W, H, r);
        plane.swap(tmp);
    }
}

std::vector<float> makeControlPlane(const Params& p, const Image& pic, int W, int H) {
    if (p.control == kCtlOff || pic.width != W || pic.height != H || pic.channels < 3) return {};
    std::vector<float> c(static_cast<size_t>(W) * H);

    const float black = static_cast<float>(p.control_black);
    float range = static_cast<float>(p.control_white - p.control_black);
    if (std::fabs(range) < 1e-6f) range = range < 0.f ? -1e-6f : 1e-6f;
    const float nearD = static_cast<float>(p.control_near);
    float farSpan = static_cast<float>(p.control_far - p.control_near);
    if (std::fabs(farSpan) < 1e-9f) farSpan = 1e-9f;
    const bool invert = p.control_invert;
    const int channel = p.control;
    const int C = pic.channels;

    parallelFor(H, [&](int y) {
        const float* row = pic.row(y);
        float* out = c.data() + static_cast<size_t>(y) * W;
        for (int x = 0; x < W; ++x) {
            const float* px = row + static_cast<size_t>(x) * C;
            float v;
            switch (channel) {
                case kCtlAlpha: v = C >= 4 ? px[3] : 1.f; break;
                case kCtlRed: v = px[0]; break;
                case kCtlGreen: v = px[1]; break;
                case kCtlBlue: v = px[2]; break;
                default: v = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2]; break;
            }
            if (!std::isfinite(v)) v = 0.f;
            if (channel == kCtlDepth) v = 1.f - clamp01((v - nearD) / farSpan);  // near = 1, far = 0
            v = clamp01((v - black) / range);
            out[x] = invert ? 1.f - v : v;
        }
    });
    blurPlane(c, W, H, p.control_softness);
    return c;
}

}  // namespace timeyum
