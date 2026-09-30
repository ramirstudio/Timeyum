// FFT against a direct O(n^2) DFT for power of two, smooth, prime and mixed sizes.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "../core/Fft.h"

using namespace timeyum;

static const double kPi = 3.14159265358979323846;  // kPi is not defined by MSVC

int main() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    int bad = 0;
    for (int n : {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 25, 27, 30, 49, 60, 64, 90, 97, 100, 121, 143, 251, 360, 1080, 1081, 2160}) {
        std::vector<Cf> x(n), y;
        for (auto& v : x) v = {u(rng), u(rng)};
        y = x;
        Fft f(n);
        f.forward(y.data());
        double err = 0, mag = 0;
        for (int k = 0; k < n; ++k) {
            double sr = 0, si = 0;
            for (int j = 0; j < n; ++j) {
                const double a = -2.0 * kPi * (static_cast<long long>(j) * k % n) / n;
                sr += x[j].r * std::cos(a) - x[j].i * std::sin(a);
                si += x[j].r * std::sin(a) + x[j].i * std::cos(a);
            }
            err = std::fmax(err, std::fmax(std::fabs(sr - y[k].r), std::fabs(si - y[k].i)));
            mag = std::fmax(mag, std::fmax(std::fabs(sr), std::fabs(si)));
        }
        f.inverse(y.data());
        double rt = 0;
        for (int k = 0; k < n; ++k) rt = std::fmax(rt, std::fmax(std::fabs(y[k].r - x[k].r), std::fabs(y[k].i - x[k].i)));
        const bool ok = err < 2e-5 * std::fmax(mag, 1.0) * std::log2(n + 2) && rt < 2e-5;
        if (!ok) ++bad;
        std::printf("%-5d forward err %.1e  roundtrip err %.1e %s\n", n, err, rt, ok ? "" : "FAIL");
    }
    return bad ? 1 : 0;
}
