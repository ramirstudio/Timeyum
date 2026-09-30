#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Parallel.h"

namespace timeyum {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline Cf mul(Cf a, Cf b) { return {a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r}; }
inline Cf add(Cf a, Cf b) { return {a.r + b.r, a.i + b.i}; }
inline Cf sub(Cf a, Cf b) { return {a.r - b.r, a.i - b.i}; }

// One Stockham autosort pass: n is the current sequence length, s the stride. x -> y.
void pass2(int n, int s, const Cf* x, Cf* y, const Cf* tw) {
    const int m = n / 2;
    for (int p = 0; p < m; ++p) {
        const Cf w = tw[p];
        for (int q = 0; q < s; ++q) {
            const Cf a = x[q + s * p], b = x[q + s * (p + m)];
            y[q + s * (2 * p)] = add(a, b);
            y[q + s * (2 * p + 1)] = mul(sub(a, b), w);
        }
    }
}

void pass3(int n, int s, const Cf* x, Cf* y, const Cf* tw) {
    const int m = n / 3;
    constexpr float kS = 0.86602540378443864676f;
    for (int p = 0; p < m; ++p) {
        const Cf w1 = tw[2 * p], w2 = tw[2 * p + 1];
        for (int q = 0; q < s; ++q) {
            const Cf a0 = x[q + s * p], a1 = x[q + s * (p + m)], a2 = x[q + s * (p + 2 * m)];
            const Cf t1 = add(a1, a2);
            const Cf t2 = {a0.r - 0.5f * t1.r, a0.i - 0.5f * t1.i};
            const Cf d = sub(a1, a2);
            const Cf t3 = {kS * d.i, -kS * d.r};
            y[q + s * (3 * p)] = add(a0, t1);
            y[q + s * (3 * p + 1)] = mul(add(t2, t3), w1);
            y[q + s * (3 * p + 2)] = mul(sub(t2, t3), w2);
        }
    }
}

void pass4(int n, int s, const Cf* x, Cf* y, const Cf* tw) {
    const int m = n / 4;
    for (int p = 0; p < m; ++p) {
        const Cf w1 = tw[3 * p], w2 = tw[3 * p + 1], w3 = tw[3 * p + 2];
        for (int q = 0; q < s; ++q) {
            const Cf a0 = x[q + s * p], a1 = x[q + s * (p + m)], a2 = x[q + s * (p + 2 * m)], a3 = x[q + s * (p + 3 * m)];
            const Cf t0 = add(a0, a2), t1 = sub(a0, a2), t2 = add(a1, a3);
            const Cf d = sub(a1, a3);
            const Cf t3 = {d.i, -d.r};
            y[q + s * (4 * p)] = add(t0, t2);
            y[q + s * (4 * p + 1)] = mul(add(t1, t3), w1);
            y[q + s * (4 * p + 2)] = mul(sub(t0, t2), w2);
            y[q + s * (4 * p + 3)] = mul(sub(t1, t3), w3);
        }
    }
}

void pass5(int n, int s, const Cf* x, Cf* y, const Cf* tw) {
    const int m = n / 5;
    constexpr float c1 = 0.30901699437494742f, c2 = -0.80901699437494742f;
    constexpr float s1 = 0.95105651629515357f, s2 = 0.58778525229247313f;
    for (int p = 0; p < m; ++p) {
        const Cf w1 = tw[4 * p], w2 = tw[4 * p + 1], w3 = tw[4 * p + 2], w4 = tw[4 * p + 3];
        for (int q = 0; q < s; ++q) {
            const Cf a0 = x[q + s * p], a1 = x[q + s * (p + m)], a2 = x[q + s * (p + 2 * m)];
            const Cf a3 = x[q + s * (p + 3 * m)], a4 = x[q + s * (p + 4 * m)];
            const Cf t1 = add(a1, a4), t2 = add(a2, a3), t3 = sub(a1, a4), t4 = sub(a2, a3);
            const Cf b1 = {a0.r + c1 * t1.r + c2 * t2.r, a0.i + c1 * t1.i + c2 * t2.i};
            const Cf b2 = {a0.r + c2 * t1.r + c1 * t2.r, a0.i + c2 * t1.i + c1 * t2.i};
            const Cf u1 = {s1 * t3.r + s2 * t4.r, s1 * t3.i + s2 * t4.i};
            const Cf u2 = {s2 * t3.r - s1 * t4.r, s2 * t3.i - s1 * t4.i};
            y[q + s * (5 * p)] = {a0.r + t1.r + t2.r, a0.i + t1.i + t2.i};
            y[q + s * (5 * p + 1)] = mul({b1.r + u1.i, b1.i - u1.r}, w1);
            y[q + s * (5 * p + 2)] = mul({b2.r + u2.i, b2.i - u2.r}, w2);
            y[q + s * (5 * p + 3)] = mul({b2.r - u2.i, b2.i + u2.r}, w3);
            y[q + s * (5 * p + 4)] = mul({b1.r - u1.i, b1.i + u1.r}, w4);
        }
    }
}

void passGeneric(int r, int n, int s, const Cf* x, Cf* y, const Cf* tw) {
    const int m = n / r;
    Cf wr[8];
    for (int k = 0; k < r; ++k) {
        const double a = -2.0 * kPi * k / r;
        wr[k] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
    }
    Cf a[8];
    for (int p = 0; p < m; ++p) {
        for (int q = 0; q < s; ++q) {
            for (int j = 0; j < r; ++j) a[j] = x[q + s * (p + m * j)];
            for (int k = 0; k < r; ++k) {
                Cf sum = a[0];
                for (int j = 1; j < r; ++j) sum = add(sum, mul(a[j], wr[(j * k) % r]));
                y[q + s * (r * p + k)] = k == 0 ? sum : mul(sum, tw[(r - 1) * p + k - 1]);
            }
        }
    }
}

}  // namespace

int smoothSize(int n) {
    if (n < 1) n = 1;
    for (;; ++n) {
        int m = n;
        for (int f : {2, 3, 5})
            while (m % f == 0) m /= f;
        if (m == 1) return n;
    }
}

Fft::Fft(int n) : n_(n) {
    std::vector<int> factors;
    int m = n;
    while (m % 4 == 0) { factors.push_back(4); m /= 4; }
    for (int p : {2, 3, 5, 7}) {
        while (m % p == 0) {
            factors.push_back(p);
            m /= p;
        }
    }
    if (m != 1) {
        bluestein_ = true;
        m_ = 1;
        while (m_ < 2 * n - 1) m_ <<= 1;
        sub_.reset(new Fft(m_));
        chirp_.resize(n);
        for (int k = 0; k < n; ++k) {
            const double a = kPi * static_cast<double>((static_cast<int64_t>(k) * k) % (2LL * n)) / n;
            chirp_[k] = {static_cast<float>(std::cos(a)), static_cast<float>(-std::sin(a))};
        }
        bspec_.assign(m_, Cf{});
        bspec_[0] = {chirp_[0].r, -chirp_[0].i};
        for (int k = 1; k < n; ++k) {
            const Cf c = {chirp_[k].r, -chirp_[k].i};
            bspec_[k] = c;
            bspec_[m_ - k] = c;
        }
        sub_->forward(bspec_.data());
        return;
    }
    int len = n, stride = 1;
    for (int r : factors) {
        Stage st;
        st.radix = r;
        st.n = len;
        st.stride = stride;
        const int cnt = len / r;
        st.tw.resize(static_cast<size_t>(cnt) * (r - 1));
        for (int p = 0; p < cnt; ++p)
            for (int k = 1; k < r; ++k) {
                const double a = -2.0 * kPi * static_cast<double>(p) * k / len;
                st.tw[static_cast<size_t>(p) * (r - 1) + k - 1] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
            }
        stages_.push_back(std::move(st));
        len /= r;
        stride *= r;
    }
}

void Fft::mixed(Cf* data) const {
    if (stages_.empty()) return;
    thread_local std::vector<Cf> scratch;
    if (scratch.size() < static_cast<size_t>(n_)) scratch.resize(n_);
    Cf* a = data;
    Cf* b = scratch.data();
    for (const Stage& st : stages_) {
        switch (st.radix) {
            case 2: pass2(st.n, st.stride, a, b, st.tw.data()); break;
            case 3: pass3(st.n, st.stride, a, b, st.tw.data()); break;
            case 4: pass4(st.n, st.stride, a, b, st.tw.data()); break;
            case 5: pass5(st.n, st.stride, a, b, st.tw.data()); break;
            default: passGeneric(st.radix, st.n, st.stride, a, b, st.tw.data()); break;
        }
        std::swap(a, b);
    }
    if (a != data) std::copy(a, a + n_, data);
}

void Fft::bluestein(Cf* data) const {
    thread_local std::vector<Cf> a;
    a.assign(m_, Cf{});
    for (int k = 0; k < n_; ++k) a[k] = mul(data[k], chirp_[k]);
    sub_->forward(a.data());
    for (int k = 0; k < m_; ++k) a[k] = mul(a[k], bspec_[k]);
    sub_->inverse(a.data());
    for (int k = 0; k < n_; ++k) data[k] = mul(a[k], chirp_[k]);
}

void Fft::run(Cf* data, bool inverse) const {
    if (n_ <= 1) return;
    if (inverse)
        for (int i = 0; i < n_; ++i) data[i].i = -data[i].i;
    if (bluestein_) bluestein(data);
    else mixed(data);
    if (inverse) {
        const float s = 1.0f / static_cast<float>(n_);
        for (int i = 0; i < n_; ++i) {
            data[i].r *= s;
            data[i].i *= -s;
        }
    }
}

void fft2d(Cf* data, const Fft& rowFft, const Fft& colFft, bool inverse) {
    const int w = rowFft.size();
    const int h = colFft.size();
    parallelFor(h, [&](int y) {
        Cf* row = data + static_cast<size_t>(y) * w;
        inverse ? rowFft.inverse(row) : rowFft.forward(row);
    });
    constexpr int kBlock = 16;
    const int blocks = (w + kBlock - 1) / kBlock;
    parallelFor(blocks, [&](int b) {
        thread_local std::vector<Cf> tile;
        tile.resize(static_cast<size_t>(kBlock) * h);
        const int x0 = b * kBlock;
        const int cols = std::min(kBlock, w - x0);
        for (int y = 0; y < h; ++y)
            for (int c = 0; c < cols; ++c) tile[static_cast<size_t>(c) * h + y] = data[static_cast<size_t>(y) * w + x0 + c];
        for (int c = 0; c < cols; ++c) {
            Cf* col = tile.data() + static_cast<size_t>(c) * h;
            inverse ? colFft.inverse(col) : colFft.forward(col);
        }
        for (int y = 0; y < h; ++y)
            for (int c = 0; c < cols; ++c) data[static_cast<size_t>(y) * w + x0 + c] = tile[static_cast<size_t>(c) * h + y];
    });
}

}  // namespace timeyum
