// Jiles–Atherton hiszterézis próbapad a TAPEKI szalagmagjához (paraméter-hangolás).
//   clang++ -std=c++20 -O2 -IShared Tools/ja_proto.cpp -o build/ja_proto && build/ja_proto [a k c alpha]
#include "EKIDSP.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>

struct JA {
    double a = 0.5, k = 0.3, c = 0.5, alpha = 0.01, T = 1.0 / 96000;
    double M = 0, Hp = 0, dHp = 0;
    static double L(double x) { return std::fabs(x) < 1e-4 ? x / 3 : 1.0 / std::tanh(x) - 1.0 / x; }
    static double Ld(double x) { if (std::fabs(x) < 1e-4) return 1.0 / 3; const double s = std::sinh(x); return 1.0 / (x * x) - 1.0 / (s * s); }
    double dMdt(double m, double H, double dH) const {
        const double Q = (H + alpha * m) / a, man = L(Q), dman = Ld(Q) / a, diff = man - m;
        const double delta = dH >= 0 ? 1.0 : -1.0;
        const double dM = delta * diff > 0 ? 1.0 : 0.0;
        // a nevező előjele a mozgás irányát követi; ne mehessen át nullán (stabilitás)
        const double den1 = delta * std::max(delta * ((1 - c) * delta * k - alpha * diff), 0.25 * (1 - c) * k);
        const double num = (1 - c) * dM * diff / den1 + c * dman;
        double den = 1 - c * alpha * dman;
        if (std::fabs(den) < 1e-6) den = 1e-6;
        return dH * num / den;
    }
    double tick(double H) {
        // adaptív al-lépések (RK2): nagy jelugrásnál (magas, hangos jel) több kisebb lépés — stabil és pontos marad
        const double dHtot = H - Hp;
        const int n = std::clamp(int(std::ceil(std::fabs(dHtot) / (0.04 * a))), 1, 16);
        const double h = T / n, dH = dHtot / T;
        for (int i = 0; i < n; ++i) {
            const double H0 = Hp + dHtot * i / n, H1 = Hp + dHtot * (i + 1) / n;
            const double k1 = h * dMdt(M, H0, dH);
            const double k2 = h * dMdt(M + 0.5 * k1, 0.5 * (H0 + H1), dH);
            M += k2;
        }
        if (!std::isfinite(M)) M = 0;
        M = std::clamp(M, -1.2, 1.2);
        Hp = H; dHp = dH;
        return M;
    }
};

static double mag(const std::vector<double>& y, double f, double fs) {
    const size_t s0 = y.size() / 2, N = size_t(fs / f * std::floor((y.size() - s0) * f / fs));
    double re = 0, im = 0; for (size_t i = 0; i < N; ++i) { re += y[s0 + i] * std::cos(2 * M_PI * f * i / fs); im += y[s0 + i] * std::sin(2 * M_PI * f * i / fs); }
    return std::hypot(re, im) / N * 2;
}

int main(int argc, char** argv) {
    JA p; if (argc > 4) { p.a = atof(argv[1]); p.k = atof(argv[2]); p.c = atof(argv[3]); p.alpha = atof(argv[4]); }
    const double fs = 96000;
    // kis jel erősítése (normálás)
    auto run = [&](double amp, double f, double* thd, double* g, double* h2r = nullptr) {
        JA j = p; std::vector<double> y(size_t(fs * 0.4));
        for (size_t i = 0; i < y.size(); ++i) y[i] = j.tick(amp * std::sin(2 * M_PI * f * i / fs));
        const double f1 = mag(y, f, fs); double h = 0;
        for (int n = 2; n <= 9; ++n) { const double m = mag(y, f * n, fs); h += m * m; if (n == 2 && h2r) *h2r = m / f1; }
        *thd = 100 * std::sqrt(h) / f1; *g = f1 / amp;
    };
    double t, g0; run(0.01, 1000, &t, &g0);
    std::printf("a=%.3f k=%.3f c=%.3f alpha=%.4f   kis jel erősítés %.3f\n", p.a, p.k, p.c, p.alpha, g0);
    for (double db : {-40.0, -30.0, -20.0, -12.0, -6.0, 0.0, 6.0}) {
        const double amp = std::pow(10, db / 20); double th, g;
        run(amp, 1000, &th, &g);
        double th10, g10; run(amp, 8000, &th10, &g10); double th15, g15; run(amp, 15000, &th15, &g15);
        std::printf("  %+5.0f dB: 1k THD %6.2f%%  gain %+5.2f dB | 8k %+5.2f dB | 15k %+5.2f dB\n", db, th, 20 * std::log10(g / g0), 20 * std::log10(g10 / g0), 20 * std::log10(g15 / g0));
    }
    return 0;
}
