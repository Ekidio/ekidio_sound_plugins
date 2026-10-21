// EKIOversample.h — lineáris fázisú, polifázisú FIR túlmintavételező (2× / 4× / 8×), közös (EKIPPER, MASTEQUI).
// Késés: pontosan K alapminta (a fel- és a leszűrő együtt), a fázis érintetlen.
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace eki {

/// lineáris fázisú, polifázisú FIR túlmintavételező (L×); késés: K alapminta (a fel + le szűrő együtt)
/// KK: csapó fázisonként (= a késés alapmintában), CUT: a vágási frekvencia a Nyquist arányában
template <int KK = 32, int CUTPERMILLE = 920>
struct FirOversamplerT {
    static constexpr int K = KK;
    int L = 4, N = 4 * K + 1;
    std::vector<float> h;                 // N hosszú prototípus (összege 1), L·(K+1)-re nullával kiegészítve
    float xh[K + 1] = {};                 // a felmintavételező bemeneti előzménye (0 = legújabb)
    std::vector<float> ring; int pos = 0, mask = 0;

    void design(int factor) {
        L = factor; N = L * K + 1;
        h.assign(size_t(L * (K + 1)), 0.0f);
        const double fc = 0.5 / L * (CUTPERMILLE / 1000.0), beta = 9.0, M = N - 1;   // Kaiser-ablak
        auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / 2) / k; s += t * t; } return s; };
        double sum = 0;
        for (int n = 0; n < N; ++n) {
            const double m = n - M / 2, sinc = m == 0 ? 2 * fc : std::sin(2 * M_PI * fc * m) / (M_PI * m);
            const double r = 2.0 * n / M - 1.0, w = i0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0(beta);
            h[size_t(n)] = float(sinc * w); sum += sinc * w;
        }
        for (int n = 0; n < N; ++n) h[size_t(n)] = float(h[size_t(n)] / sum);
        int sz = 1; while (sz < N + L) sz <<= 1;
        ring.assign(size_t(sz), 0.0f); mask = sz - 1;
        reset();
    }
    void reset() { std::fill(std::begin(xh), std::end(xh), 0.0f); std::fill(ring.begin(), ring.end(), 0.0f); pos = 0; }

    /// egy alapmintából L túlmintavételezett minta
    inline void up(float x, float* o) {
        for (int k = K; k > 0; --k) xh[k] = xh[k - 1];
        xh[0] = x;
        for (int p = 0; p < L; ++p) {
            float acc = 0;
            for (int k = 0; k <= K; ++k) acc += h[size_t(p + k * L)] * xh[k];
            o[p] = acc * float(L);
        }
    }
    /// L túlmintavételezett mintából egy alapminta (a blokk első mintájánál tizedel → a teljes késés pontosan K alapminta)
    inline float down(const float* u) {
        const int first = pos;
        for (int p = 0; p < L; ++p) { ring[size_t(pos)] = u[p]; pos = (pos + 1) & mask; }
        float acc = 0;
        for (int j = 0; j < N; ++j) acc += h[size_t(j)] * ring[size_t((first - j) & mask)];
        return acc;
    }
};

using FirOversampler = FirOversamplerT<32, 920>;   // EKIPPER: rövid (0,67 ms), vágás ≈ 22 kHz
using FirOversamplerHQ = FirOversamplerT<96, 945>; // MASTEQUI: hosszú, meredek (2 ms), 20 kHz-ig lapos

} // namespace eki
