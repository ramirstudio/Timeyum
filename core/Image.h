#pragma once

#include <cstddef>
#include <vector>

namespace timeyum {

// Interleaved float image, top row first, 3 (RGB) or 4 (RGBA, premultiplied or straight) channels.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<float> data;

    Image() = default;
    Image(int w, int h, int c) : width(w), height(h), channels(c), data(static_cast<size_t>(w) * h * c) {}

    float* row(int y) { return data.data() + static_cast<size_t>(y) * width * channels; }
    const float* row(int y) const { return data.data() + static_cast<size_t>(y) * width * channels; }
};

}  // namespace timeyum
