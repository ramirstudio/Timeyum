#pragma once

#include <memory>
#include <vector>

namespace timeyum {

struct Cf {
    float r = 0.f, i = 0.f;
};

// Complex FFT of any length: mixed radix for sizes built from primes up to 7, Bluestein otherwise.
class Fft {
public:
    explicit Fft(int n);
    int size() const { return n_; }
    void forward(Cf* data) const { run(data, false); }
    void inverse(Cf* data) const { run(data, true); }  // scaled by 1/n

private:
    void run(Cf* data, bool inverse) const;
    void mixed(Cf* data) const;
    void bluestein(Cf* data) const;

    struct Stage {
        int radix;
        int n;       // sequence length at this stage
        int stride;  // product of the radices of the earlier stages
        std::vector<Cf> tw;  // (n / radix) * (radix - 1) twiddles
    };

    int n_;
    bool bluestein_ = false;
    std::vector<Stage> stages_;
    int m_ = 0;
    std::unique_ptr<Fft> sub_;
    std::vector<Cf> chirp_, bspec_;
};

// In-place 2D transform of a w x h row-major array. The inverse is scaled by 1/(w*h).
void fft2d(Cf* data, const Fft& rowFft, const Fft& colFft, bool inverse);

// Smallest size >= n made only of the factors 2, 3 and 5.
int smoothSize(int n);

}  // namespace timeyum
