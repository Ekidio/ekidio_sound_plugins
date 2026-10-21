// EKIMasterCurves.h — a MASTEQUI mért harang- és polcmodellje (a referencia-mérésekből illesztve), közös: MASTEQUI, EK1.
//   harang: állandó Q-jú sáváteresztő hozzáadva (emelés) / visszacsatolásban levonva (vágás) + kétoldali lapos farok;
//   polc: elsőrendű (mély 41,4 Hz / LOW SHIFT 83 Hz, magas 11,6 kHz), illesztett digitális megvalósítással.
#pragma once
#include "EKIFilters.h"
#include <complex>

namespace eki {

// a harang sáváteresztőjének Q-ja és a csúcs-erősítés szorzója a Q-állásonként (emelés / vágás), a mérésre illesztve
// (a széles vágás mélyebb: Q1-en −13,9 dB a névleges −12 helyett)
inline constexpr float kBoostQ[5] = {0.755f, 1.187f, 2.129f, 2.475f, 3.676f};
inline constexpr float kCutQ[5]   = {1.210f, 1.635f, 2.522f, 2.878f, 4.039f};
inline constexpr float kBoostG[5] = {1.012f, 1.014f, 1.020f, 1.023f, 1.030f};
inline constexpr float kCutG[5]   = {1.160f, 1.065f, 0.994f, 0.982f, 0.960f};
inline constexpr float kFreqTrim = 1.025f;   // a valódi középfrekvencia a felirat felett (mérés)

/// harang: H = (den + k·B)/den (emelés) vagy den/(den + k'·B) (vágás); B = állandó Q-jú sáváteresztő + aszimmetrikus farok
inline Analog2 bellProto(double f, double gDb, double q, double c0, double c2) {
    const double w = 2 * M_PI * f, g = std::pow(10.0, gDb / 20.0);
    const double norm = std::sqrt(1.0 + std::pow((c0 - c2) * q, 2.0));   // a csúcson pontosan g legyen
    const double nb2 = c2 / norm, nb1 = (w / q) / norm, nb0 = c0 * w * w / norm;
    const double d2 = 1, d1 = w / q, d0 = w * w;
    if (gDb >= 0) { const double k = g - 1; return {d2 + k * nb2, d1 + k * nb1, d0 + k * nb0, d2, d1, d0}; }
    const double k = 1.0 / g - 1; return {d2, d1, d0, d2 + k * nb2, d1 + k * nb1, d0 + k * nb0};
}

/// elsőfokú polc illesztve: a pólus impulzus-invariáns, a zérus és a szorzó a DC és a Nyquist nagyságát adja vissza pontosan
struct Shelf1 {
    float b0 = 1, b1 = 0, a1 = 0, z1 = 0;
    inline float tick(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y; return y; }
    void reset() { z1 = 0; }
    /// low: H = 1 + (g−1)·(ω/(s+ω)·(1−c) + c); high: H = 1 + (g−1)·(s/(s+ω)); vágásnál a reciprok
    void design(bool low, double f, double gDb, double c, double fs) {
        const double g = std::pow(10.0, gDb / 20.0), w = 2 * M_PI * f, T = 1.0 / fs;
        auto mag = [&](double om) {   // analóg nagyság egy körfrekvencián
            const std::complex<double> s(0, om);
            const std::complex<double> B = low ? (w / (s + w)) * (1 - c) + c : s / (s + w);
            const std::complex<double> H = gDb >= 0 ? 1.0 + (g - 1) * B : 1.0 / (1.0 + (1 / g - 1) * B);
            return std::abs(H);
        };
        // a pólus: emelésnél a sáváteresztő ω-ja, vágásnál a visszacsatolt rendszeré
        double pole = w;
        if (low) pole = gDb >= 0 ? w : w * (1 / g) / (1 + (1 / g - 1) * c);   // ω(1+k')/(1+k'c), k' = 1/g − 1
        else pole = gDb >= 0 ? w : w / (1 + (1 / g - 1));
        const double pz = std::exp(-std::max(pole, 1e-6) * T);
        const double hDC = mag(0), hNy = mag(M_PI * fs);
        // H(z) = k (1 − z0 z⁻¹)/(1 − pz z⁻¹): DC: k(1−z0)/(1−pz) = hDC, Nyquist: k(1+z0)/(1+pz) = hNy
        const double A = hDC * (1 - pz), Bn = hNy * (1 + pz);
        const double k = 0.5 * (A + Bn), z0 = (Bn - A) / (A + Bn);
        b0 = float(k); b1 = float(-k * z0); a1 = float(-pz);
    }
};


} // namespace eki
