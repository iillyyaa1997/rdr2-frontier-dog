// Deterministic random numbers (splitmix64). Dog rolls take a seed so results can be reproduced in tests and logs.
#pragma once
#include <cstdint>
#include <vector>

namespace fd {

class Rng {
public:
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ULL) : s_(seed) {}
    uint64_t next() {
        uint64_t z = (s_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // integer in [lo, hi]
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + int(next() % uint64_t(hi - lo + 1));
    }
    double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    bool chance(double p) { return unit() < p; }
    // index picked by weight; -1 if all weights are zero
    int weighted(const std::vector<double>& w) {
        double sum = 0;
        for (double x : w) sum += x > 0 ? x : 0;
        if (sum <= 0) return -1;
        double r = unit() * sum;
        for (size_t i = 0; i < w.size(); ++i) {
            if (w[i] <= 0) continue;
            if (r < w[i]) return int(i);
            r -= w[i];
        }
        for (size_t i = w.size(); i-- > 0;)
            if (w[i] > 0) return int(i);
        return -1;
    }
    uint64_t state() const { return s_; }

private:
    uint64_t s_;
};

}  // namespace fd
