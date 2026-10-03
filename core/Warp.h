#pragma once

#include <cstddef>
#include <vector>

#include "Image.h"
#include "Params.h"

namespace timeyum {

// Low resolution brightness of a frame. Node (i, j) sits at pixel position (i * cell, j * cell)
// in continuous pixel coordinates, so pixel (x, y) has grid coordinates ((x + 0.5) / cell, (y + 0.5) / cell).
struct LumaGrid {
    int w = 0, h = 0;
    double cell = 1.0;
    std::vector<float> v;  // tone mapped luma in 0..1
    bool empty() const { return v.empty(); }
};

double warpCellSize(int width, int height);

// Builds a LumaGrid one row at a time, so the host never has to hold a whole history frame.
class LumaGridBuilder {
public:
    LumaGridBuilder(int width, int height);
    void addRow(int y, const float* rgba, int channels, int colorspace);
    void addRowPlanar(int y, const float* r, const float* g, const float* b);
    LumaGrid finish();

private:
    void addLuma(int y, int x, float luma);
    int width_, gw_, gh_;
    double cell_;
    std::vector<float> sum_, cnt_;
    std::vector<int> colNode_;
};

LumaGrid makeLumaGrid(const Image& img, int colorspace);

// Number of frames before the current one that the warp needs (0 when it needs none).
int warpHistoryCount(const Params& p);

struct WarpField {
    int gw = 0, gh = 0;
    double cell = 1.0;
    std::vector<float> length;    // streak length multiplier at each node
    std::vector<float> dx, dy;    // where each pixel samples the smear from, as an offset in pixels
    std::vector<float> reaction;  // 0..1 response of the video
    bool modulates = false;       // the streak length varies across the frame
    bool displaces = false;
    double mMin = 1.0, mMax = 1.0;            // range the length multiplier can take (from the parameters)
    double actualMin = 1.0, actualMax = 1.0;  // range it takes in this frame
    double dispMax = 0.0;
    double waveScale = 1.0;  // < 1 when the flow displacement was flattened to keep the mapping free of folds
    double autoX = 0.0, autoY = 0.0, autoConf = 0.0;  // bright area the auto target follows
};

// controlGrid, when given (from makeControlGrid), lets the control input drive the field like the video does.
WarpField buildWarpField(const Params& p, int width, int height, const LumaGrid& current,
                         const std::vector<LumaGrid>& history, double timeSeconds,
                         const std::vector<float>* controlGrid = nullptr);

// Averages a width x height plane onto the grid the warp field uses.
std::vector<float> makeControlGrid(const float* plane, int width, int height);

float gridSample(const std::vector<float>& g, int gw, int gh, double u, double v);

// Streak length multipliers that are blended per pixel. Always contains 1 when the range spans it, so a
// warp that does not modulate anything reproduces the plain effect exactly.
std::vector<double> buildLevels(double mMin, double mMax, int count);
float levelWeight(const std::vector<double>& ms, size_t level, float m);
bool levelSupported(const std::vector<double>& ms, size_t level, double mMin, double mMax);

}  // namespace timeyum
