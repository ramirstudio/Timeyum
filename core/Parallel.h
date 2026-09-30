#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace timeyum {

// Upper bound on worker threads; 0 means hardware concurrency.
void setMaxThreads(int n);
int maxThreads();

template <class F>
void parallelFor(int n, F&& f) {
    int t = std::min(maxThreads(), n);
    if (t <= 1) {
        for (int i = 0; i < n; ++i) f(i);
        return;
    }
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    pool.reserve(t - 1);
    auto work = [&] {
        for (;;) {
            int i = next.fetch_add(1);
            if (i >= n) break;
            f(i);
        }
    };
    for (int k = 1; k < t; ++k) pool.emplace_back(work);
    work();
    for (auto& th : pool) th.join();
}

}  // namespace timeyum
