// TAPEKI offline mérések: frekvenciagörbe gépenként és sebességenként, THD a szint szerint (és a magasak korábbi telítése),
// wow & flutter mértéke, szalagzaj, AUTO GAIN (K-súlyozott), REC LEVEL 0 tisztasága, stabilitás.
//   clang++ -std=c++20 -O2 -IShared -IPlugins/TAPEKI Tools/tapeki_measure.cpp -o build/tapeki_measure && build/tapeki_measure
#include "TapekiDSP.h"
#include <cstdio>
#include <random>
#include <vector>

using namespace eki;
constexpr float SR = 48000;
const char* kMach[3] = {"STUDIO  ", "MASTER  ", "CASSETTE"};
const char* kSpd[3] = {"7.5", "15 ", "30 "};

/// mérésekhez: zaj és ingadozás nélkül
static TapekiParams P0() { TapekiParams p; p.wow = 0; p.noise = 0; p.autoGain = 0; p.recLevel = 5; p.speed = 2; p.bias = 1; p.cal = 2; p.reproLf = 0; p.reproHf = -6; p.bump = 4; return p; }

static std::vector<float> run(const TapekiParams& p, const std::vector<float>& x, float sr = SR, float* vu = nullptr, int lat = 0) {
    TapekiDSP d; d.prepare(sr);
    std::vector<float> y(x.size()), r(x.size());
    const int B = 256;
    for (size_t i0 = 0; i0 + B <= x.size(); i0 += B) { const float* in[2] = {&x[i0], &x[i0]}; float* out[2] = {&y[i0], &r[i0]}; d.process(in, out, 2, B, p); }
    if (vu) *vu = d.vuDb();
    return y;
}
static std::vector<float> sine(float hz, float amp, float sec) { std::vector<float> v(size_t(SR * sec)); for (size_t i = 0; i < v.size(); ++i) v[i] = amp * std::sin(2 * float(M_PI) * hz * i / SR); return v; }
static double mag(const std::vector<float>& y, double f) {
    const size_t s0 = y.size() / 2, N = size_t(SR / f * std::floor((y.size() - s0) * f / SR));
    double re = 0, im = 0; for (size_t i = 0; i < N; ++i) { re += y[s0 + i] * std::cos(2 * M_PI * f * i / SR); im += y[s0 + i] * std::sin(2 * M_PI * f * i / SR); }
    return std::hypot(re, im) / std::max<size_t>(N, 1) * 2;
}
static double thd(const std::vector<float>& y, float hz, double* h2 = nullptr, double* h3 = nullptr) {
    double h = 0; const double f1 = mag(y, hz);
    for (int k = 2; k <= 9; ++k) if (hz * k < SR / 2) { const double m = mag(y, hz * k); h += m * m; if (k == 2 && h2) *h2 = m / f1; if (k == 3 && h3) *h3 = m / f1; }
    return 100 * std::sqrt(h) / f1;
}
static void pink(std::vector<float>& v, unsigned seed) {
    std::mt19937 g(seed); std::normal_distribution<float> nd; float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& s : v) { const float w = nd(g);
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        s = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f; b6 = w * 0.115926f; }
}
struct KW { Biquad s, h; KW() { s.designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), SR, 1681); h.designMatched(Analog2::highPass(38.0, 0.5), SR, 38); }
            float tick(float x) { return h.tick(s.tick(x)); } };
static float lu(const TapekiParams& p, float levelDb) {
    std::vector<float> x(size_t(SR * 6)); pink(x, 3);
    double e = 0; for (float s : x) e += s * s; const float k = dbToGain(levelDb) / float(std::sqrt(e / x.size())); for (auto& s : x) s *= k;
    auto y = run(p, x); KW a, b; double ei = 0, eo = 0;
    for (size_t i = 0; i < x.size(); ++i) { const float u = a.tick(x[i]), v = b.tick(y[i]); if (i > x.size() / 2) { ei += u * u; eo += v * v; } }
    return float(10 * std::log10(eo / ei));
}

int main() {
    std::printf("=== 1) frekvenciagörbe (−30 dBFS szinusz, REC 5, BUMP 5, BIAS NORM; dB az 1 kHz-hez) ===\n");
    const float fs[] = {20, 30, 45, 62, 95, 150, 300, 5000, 10000, 15000, 20000};
    std::printf("                 "); for (float f : fs) std::printf("%7.0f", f); std::printf("\n");
    for (int m = 0; m < 3; ++m) for (int s = 0; s < 3; ++s) {
        TapekiParams p = P0(); p.machine = m; p.speed = s;
        const double r1k = mag(run(p, sine(1000, 0.0316f, 0.6f)), 1000);
        std::printf("  %s %s ips", kMach[m], kSpd[s]);
        for (float f : fs) std::printf("%+7.1f", 20 * std::log10(mag(run(p, sine(f, 0.0316f, f < 100 ? 1.5f : 0.6f)), f) / r1k));
        std::printf("\n");
    }
    std::printf("\n=== 2) THD %% 1 kHz-en a szint szerint (30 ips, REC 5, CAL +9) — h2/h3 arány: a szalag főleg páratlan (3.) felhangot ad ===\n");
    for (int m = 0; m < 3; ++m) {
        std::printf("  %s", kMach[m]);
        for (float lv : {-24.f, -18.f, -12.f, -6.f, 0.f}) { TapekiParams p = P0(); p.machine = m; double h2 = 0, h3 = 0;
            const double t = thd(run(p, sine(1000, dbToGain(lv), 0.6f)), 1000, &h2, &h3);
            std::printf("  %+3.0f dBFS %5.2f%% (h3/h2 %4.1f)", lv, t, h3 / std::max(h2, 1e-9)); }
        std::printf("\n");
    }
    std::printf("  BIAS (STUDIO, −12 dBFS):");
    for (int b = 0; b < 3; ++b) { TapekiParams p = P0(); p.bias = b; double h2 = 0, h3 = 0; const double t = thd(run(p, sine(1000, dbToGain(-12), 0.6f)), 1000, &h2, &h3);
        std::printf("  %s %5.2f%% (h2 %.2f%%)", b == 0 ? "UNDER" : b == 1 ? "NORM" : "OVER", t, 100 * h2); }
    std::printf("\n  REC LEVEL 0 / 5 / 10 (STUDIO, −18 dBFS):");
    for (float r : {0.f, 5.f, 10.f}) { TapekiParams p = P0(); p.recLevel = r; std::printf("  %.0f: %.2f%%", r, thd(run(p, sine(1000, dbToGain(-18), 0.6f)), 1000)); }
    std::printf("\n\n=== 3) a magasak hamarabb telnek: 10 kHz szintváltozás −30 → −6 dBFS bemenetnél (a lineárishoz képest, dB) ===\n ");
    for (int m = 0; m < 3; ++m) { TapekiParams p = P0(); p.machine = m;
        const double lo = mag(run(p, sine(10000, dbToGain(-30), 0.6f)), 10000) / dbToGain(-30), hi = mag(run(p, sine(10000, dbToGain(-6), 0.6f)), 10000) / dbToGain(-6);
        const double lo1 = mag(run(p, sine(1000, dbToGain(-30), 0.6f)), 1000) / dbToGain(-30), hi1 = mag(run(p, sine(1000, dbToGain(-6), 0.6f)), 1000) / dbToGain(-6);
        std::printf(" %s 10k %+5.1f dB (1k %+5.1f dB) ", kMach[m], 20 * std::log10(hi / lo), 20 * std::log10(hi1 / lo1)); }
    std::printf("\n\n=== 3b) dobon (lábdob + pergő + hi-hat, csúcs ≈ −6 dBFS): tranziens-lekerekítés (csúcstényező-csökkenés, dB) és sávarány-változás (szintillesztve) ===\n");
    {
        std::vector<float> drum(size_t(SR * 4)); std::mt19937 g(5); std::normal_distribution<float> nd;
        for (size_t k = 0; k < 32; ++k) for (int i = 0; i < 6000; ++i) { const double t = i / SR;
            drum[k * 6000 + i] += float((k % 4 == 2 ? 0.10 : 0.03) * std::exp(-t / (k % 4 == 2 ? 0.05 : 0.015)) * nd(g) + (k % 4 == 2 ? 0.25 * std::exp(-t / 0.06) * std::sin(2 * M_PI * 190 * t) : 0) + (k % 4 == 0 ? 0.45 * std::exp(-t / 0.15) * std::sin(2 * M_PI * 55 * t) : 0)); }
        auto crest = [](const std::vector<float>& v) { double pk = 0, e = 0; for (size_t i = v.size() / 4; i < v.size(); ++i) { pk = std::max(pk, double(std::fabs(v[i]))); e += v[i] * v[i]; } return 20 * std::log10(pk / std::sqrt(e / (v.size() * 0.75))); };
        auto band = [](const std::vector<float>& y, double lo, double hi) { Biquad h, l; h.designMatched(Analog2::highPass(lo, 0.707), SR, lo); l.designMatched(Analog2::lowPass(hi, 0.707), SR, hi);
            double e = 0; for (size_t i = y.size() / 4; i < y.size(); ++i) { const float v = l.tick(h.tick(y[i])); e += v * v; } return e; };
        const double cDry = crest(drum);
        struct C { const char* n; int m, s, cal; float rec; int bias; };
        for (C c : {C{"STUDIO 30 CAL+9 REC 5 (alap)", 0, 2, 2, 5, 1}, C{"STUDIO 30 CAL+9 REC 7", 0, 2, 2, 7, 1}, C{"STUDIO 15 CAL+9 REC 5", 0, 1, 2, 5, 1},
                    C{"STUDIO 30 CAL+3 REC 5", 0, 2, 0, 5, 1}, C{"MASTER 30 REC 5", 1, 2, 2, 5, 1}, C{"CASSETTE 7.5 REC 5", 2, 0, 2, 5, 1}, C{"STUDIO REC 9 UNDER", 0, 1, 2, 9, 0}}) {
            TapekiParams p = P0(); p.machine = c.m; p.speed = c.s; p.cal = c.cal; p.recLevel = c.rec; p.bias = c.bias; p.autoGain = 1;
            auto y = run(p, drum);
            double tot = 0, tr = 0; double b[5], r[5]; const double ed[6] = {20, 150, 500, 3000, 10000, 20000};
            for (int i = 0; i < 5; ++i) { b[i] = band(y, ed[i], ed[i + 1]); r[i] = band(drum, ed[i], ed[i + 1]); tot += b[i]; tr += r[i]; }
            // a magas sáv (3 kHz fölött) tranziens-lekerekítése, szintillesztve
            auto hp3 = [](const std::vector<float>& v) { std::vector<float> o(v.size()); Biquad h1, h2; h1.designHP2(3000, SR); h2.designHP2(3000, SR); for (size_t i = 0; i < v.size(); ++i) o[i] = h2.tick(h1.tick(v[i])); return o; };
            const double cHf = crest(hp3(y)) - crest(hp3(drum));
            std::printf("  %-30s magas sáv csúcstényező %+5.1f dB |", c.n, cHf);
            std::printf("  %-30s csúcstényező %+5.1f dB  | <150 %+4.1f  150-500 %+4.1f  0.5-3k %+4.1f  3-10k %+4.1f  10k+ %+4.1f\n", c.n, crest(y) - cDry,
                        10 * std::log10(b[0] / tot / (r[0] / tr)), 10 * std::log10(b[1] / tot / (r[1] / tr)), 10 * std::log10(b[2] / tot / (r[2] / tr)), 10 * std::log10(b[3] / tot / (r[3] / tr)), 10 * std::log10(b[4] / tot / (r[4] / tr)));
        }
    }
    std::printf("\n\n=== 4) wow & flutter: a 3 kHz-es hang pillanatnyi frekvenciájának ingadozása (csúcs, %%) ===\n");
    for (int m = 0; m < 3; ++m) {
        std::printf("  %s", kMach[m]);
        for (float w : {2.f, 5.f, 10.f}) {
            TapekiParams p = P0(); p.machine = m; p.wow = w; auto y = run(p, sine(3000, 0.1f, 4.0f));
            // nullátmenetek közti idő → pillanatnyi frekvencia
            std::vector<double> zc; for (size_t i = y.size() / 4; i + 1 < y.size(); ++i) if (y[i] <= 0 && y[i + 1] > 0) zc.push_back(i + y[i] / (y[i] - y[i + 1]));
            double mx = 0, mean = 0; std::vector<double> f; for (size_t k = 8; k < zc.size(); k += 8) f.push_back(8 * SR / (zc[k] - zc[k - 8]));
            for (double v : f) mean += v; mean /= std::max<size_t>(f.size(), 1); for (double v : f) mx = std::max(mx, std::fabs(v - mean));
            std::printf("  W&F %2.0f: ±%.3f %%", w, 100 * mx / mean);
        }
        std::printf("\n");
    }
    std::printf("\n=== 4b) \"kosz\" (WOW & FLUTTER felső fele): pattogások száma / s csendben, és kiesések egy 1 kHz-es hangban (STUDIO) ===\n");
    for (float w : {2.f, 5.f, 8.f, 10.f}) {
        TapekiParams p = P0(); p.wow = w; auto y = run(p, std::vector<float>(size_t(SR * 6), 0.0f));
        int clicks = 0; bool on = false; for (size_t i = size_t(SR); i < y.size(); ++i) { const bool hit = std::fabs(y[i]) > 0.003f; if (hit && !on) ++clicks; on = hit; }
        auto t = run(p, sine(1000, 0.1f, 8.0f)); int drops = 0; bool low = false;
        for (size_t i = size_t(SR); i + 480 < t.size(); i += 480) { double e = 0; for (int k = 0; k < 480; ++k) e += t[i + k] * t[i + k]; const bool d = std::sqrt(e / 480) < 0.0707 * 0.85; if (d && !low) ++drops; low = d; }
        std::printf("  W&F %2.0f: pattogás %5.1f / s   kiesés (≥ −1,4 dB) %4.2f / s\n", w, clicks / 5.0, drops / 7.0);
    }
    std::printf("\n=== 5) szalagzaj csendben (dBFS RMS, A-súlyozás nélkül) ===\n");
    for (int m = 0; m < 3; ++m) { std::printf("  %s", kMach[m]);
        for (float nz : {2.f, 5.f, 10.f}) { TapekiParams p = P0(); p.machine = m; p.noise = nz; auto y = run(p, std::vector<float>(size_t(SR * 2), 0.0f));
            double e = 0; for (size_t i = y.size() / 2; i < y.size(); ++i) e += y[i] * y[i]; std::printf("  NOISE %2.0f: %6.1f", nz, 10 * std::log10(e / (y.size() / 2) + 1e-30)); }
        std::printf("\n"); }
    std::printf("\n=== 6) AUTO GAIN: K-súlyozott hangerő-eltérés (LU), rózsaszín zaj −18 dBFS ===\n");
    for (int m = 0; m < 3; ++m) for (float r : {5.f, 9.f}) { TapekiParams p; p.machine = m; p.recLevel = r; TapekiParams o = p; o.autoGain = 0;
        std::printf("  %s REC %.0f  AUTO be %+.2f LU  (ki %+.2f LU)\n", kMach[m], r, lu(p, -18), lu(o, -18)); }
    std::printf("\n=== 7) stabilitás: minden max, hangos zaj, 44.1/96 kHz; latencia ===\n");
    for (float sr : {44100.f, 96000.f}) { TapekiParams p; p.recLevel = 10; p.wow = 10; p.noise = 10; p.bump = 10; p.machine = 2; p.speed = 0; p.bias = 0;
        std::vector<float> x(size_t(sr * 2)); pink(x, 5); for (auto& s : x) s *= 3; auto y = run(p, x, sr);
        bool ok = true; for (float v : y) if (!std::isfinite(v)) ok = false;
        TapekiDSP d; d.prepare(sr); std::printf("  %g Hz: %s, latencia %.2f ms\n", sr, ok ? "OK (véges)" : "HIBA", 1000 * d.latencySeconds()); }
    return 0;
}
