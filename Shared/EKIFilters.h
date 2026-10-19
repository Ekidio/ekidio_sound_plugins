// EKIFilters.h — pontos ("illesztett", matched) másodfokú szűrők analóg prototípusból
// (a DYNEKI-ből kiemelve, hogy minden plugin használhassa). A pólusok impulzus-invariánsak, a nagyság DC-n, Nyquist-en
// és egy választott frekvencián pontosan egyezik az analóg szűrőével → nincs "összegyűrődés" a magasban.
#pragma once
#include "EKIDSP.h"
#include <complex>
#include <algorithm>

namespace eki {

// MARK: - illesztett (matched) másodfokú szűrő analóg prototípusból

/// H(s) = (b2 s² + b1 s + b0) / (a2 s² + a1 s + a0)
struct Analog2 {
    double b2 = 0, b1 = 0, b0 = 1, a2 = 0, a1 = 0, a0 = 1;
    double mag2(double w) const {
        const double nr = b0 - b2 * w * w, ni = b1 * w, dr = a0 - a2 * w * w, di = a1 * w;
        return (nr * nr + ni * ni) / std::max(dr * dr + di * di, 1e-300);
    }
    static Analog2 bell(double f, double gDb, double q) {
        const double w = 2 * M_PI * f, A = std::pow(10.0, gDb / 40.0);
        return {1, A * w / q, w * w, 1, w / (A * q), w * w};
    }
    static Analog2 lowShelf(double f, double gDb, double q) {
        const double w = 2 * M_PI * f, A = std::pow(10.0, gDb / 40.0), sA = std::sqrt(A);
        return {A, A * sA * w / q, A * A * w * w, A, sA * w / q, w * w};
    }
    static Analog2 highShelf(double f, double gDb, double q) {
        const double w = 2 * M_PI * f, A = std::pow(10.0, gDb / 40.0), sA = std::sqrt(A);
        return {A * A, A * sA * w / q, A * w * w, 1, sA * w / q, A * w * w};
    }
    static Analog2 lowPass(double f, double q) {
        const double w = 2 * M_PI * f;
        return {0, 0, w * w, 1, w / q, w * w};
    }
    /// sáváteresztő, a csúcson egységnyi erősítéssel
    static Analog2 bandPass(double f, double q) {
        const double w = 2 * M_PI * f;
        return {0, w / q, 0, 1, w / q, w * w};
    }
    static Analog2 highPass(double f, double q) {
        const double w = 2 * M_PI * f;
        return {1, 0, 0, 1, w / q, w * w};
    }
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float tick(float xf) {
        const double x = xf, y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return float(y);
    }
    void reset() { z1 = z2 = 0; }
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    bool isIdentity() const { return b0 == 1 && b1 == 0 && b2 == 0 && a1 == 0 && a2 == 0; }
    /// |H(e^jΩ)|² a digitális szűrőn
    double mag2(double om) const {
        const std::complex<double> z1c = std::polar(1.0, -om), z2c = std::polar(1.0, -2 * om);
        const auto n = b0 + b1 * z1c + b2 * z2c, d = 1.0 + a1 * z1c + a2 * z2c;
        return std::norm(n) / std::max(std::norm(d), 1e-300);
    }

    /// pólusok: impulzus-invariáns leképezés; számláló: a nagyság egyezik DC-n, Nyquist-en és fm-en
    void designMatched(const Analog2& h, double fs, double fm) {
        const double T = 1.0 / fs;
        double w0 = std::sqrt(h.a0 / h.a2);
        const double zeta = h.a1 / (2 * h.a2 * w0);
        w0 = std::min(w0, 0.95 * M_PI * fs);   // a pólus szöge maradjon π alatt
        if (zeta < 1) {
            const double r = std::exp(-zeta * w0 * T);
            a1 = -2 * r * std::cos(w0 * T * std::sqrt(1 - zeta * zeta));
            a2 = r * r;
        } else {
            const double s = w0 * std::sqrt(zeta * zeta - 1);
            const double p1 = std::exp((-zeta * w0 + s) * T), p2 = std::exp((-zeta * w0 - s) * T);
            a1 = -(p1 + p2);
            a2 = p1 * p2;
        }
        auto den2 = [&](double om) { return std::norm(1.0 + a1 * std::polar(1.0, -om) + a2 * std::polar(1.0, -2 * om)); };
        const double B0 = h.mag2(0) * den2(0);
        const double B1 = h.mag2(M_PI * fs) * den2(M_PI);
        const double om = 2 * M_PI * clampf(float(fm / fs), 0.002f, 0.46f);
        const double p1 = std::sin(om / 2) * std::sin(om / 2), p0 = 1 - p1, p2 = 4 * p0 * p1;
        double B2 = (h.mag2(om * fs) * den2(om) - B0 * p0 - B1 * p1) / p2;
        const double sB0 = std::sqrt(std::max(B0, 0.0)), sB1 = std::sqrt(std::max(B1, 0.0));
        const double W = 0.5 * (sB0 + sB1);
        B2 = std::max(B2, -W * W);
        b0 = 0.5 * (W + std::sqrt(std::max(W * W + B2, 0.0)));   // kerekítés: ne legyen negatív a gyök alatt
        b1 = 0.5 * (sB0 - sB1);
        b2 = b0 > 1e-30 ? -B2 / (4 * b0) : 0;
    }
    /// Butterworth (Q 0,7071) alul-/felüláteresztő bilineárisan (RBJ) — a két fél közös nevezőjű, így az LR4 keresztváltó
    /// (LP² + HP²) összege digitálisan is pontosan mindentáteresztő (lapos)
    void designLP2(double f, double fs, double q = 0.7071) {
        const double w = 2 * M_PI * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void designHP2(double f, double fs, double q = 0.7071) {
        const double w = 2 * M_PI * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    /// másodfokú mindentáteresztő (fázistoló) — bilineáris (RBJ), mert a fázis számít, nem a nagyság
    void designAllpass(double f, double q, double fs) {
        const double w = 2 * M_PI * f / fs, al = std::sin(w) / (2 * q), a0 = 1 + al;
        b0 = (1 - al) / a0; b1 = -2 * std::cos(w) / a0; b2 = (1 + al) / a0;
        a1 = b1; a2 = b0;
    }
    /// elsőfokú mindentáteresztő
    void designAllpass1(double f, double fs) {
        const double k = std::tan(M_PI * f / fs), c = (k - 1) / (k + 1);
        b0 = c; b1 = 1; b2 = 0; a1 = c; a2 = 0;
    }
    /// elsőfokú aluláteresztő (bilineáris)
    void designLP1(double f, double fs) {
        const double k = std::tan(M_PI * f / fs), n = 1.0 / (1 + k);
        b0 = k * n; b1 = k * n; b2 = 0; a1 = (k - 1) * n; a2 = 0;
    }
    /// elsőfokú felüláteresztő (bilineáris, előtorzítva — az alacsony vágási frekvenciákon pontos)
    void designHP1(double f, double fs) {
        const double k = std::tan(M_PI * f / fs), n = 1.0 / (1 + k);
        b0 = n; b1 = -n; b2 = 0; a1 = (k - 1) * n; a2 = 0;
    }
};

} // namespace eki
