#include "Parallel.h"

namespace timeyum {

namespace {
std::atomic<int> g_maxThreads{0};
}

void setMaxThreads(int n) { g_maxThreads = n; }

int maxThreads() {
    int n = g_maxThreads.load();
    if (n > 0) return n;
    unsigned hw = std::thread::hardware_concurrency();
    return hw ? static_cast<int>(hw) : 1;
}

}  // namespace timeyum
