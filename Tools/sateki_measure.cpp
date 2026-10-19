// SATEKI (3. koncepció: lágy levegő) offline mérések: a sáv alatti rész érintetlensége, egységenként a hozzáadott szín és a sávarányok
// dob-ütéseken, DRIVE 0 átlátszóság, AUTO GAIN (K-súlyozott), stabilitás.
//   clang++ -std=c++20 -O2 -IShared -IPlugins/SATEKI Tools/sateki_measure.cpp -o build/sateki_measure && build/sateki_measure
#include "SatekiDSP.h"
#include <cstdio>
#include <random>
#include <vector>

using namespace eki;
constexpr float SR = 48000;

/// semleges kiindulás a mérésekhez (a plugin alapállása a user beállítása, az külön sorban szerepel)
static SatekiParams P0() { SatekiParams p; p.drive[0] = p.drive[1] = p.drive[2] = 0; p.freq[0] = 2; p.freq[1] = 1; p.freq[2] = 1;
                           p.levelInDb = 0; p.mix = 100; return p; }
static std::vector<float> run(const SatekiParams& p, const std::vector<float>& x, float sr = SR, float* vu = nullptr) {
    SatekiDSP d; d.prepare(sr);
    std::vector<float> y(x.size()), r(x.size());
    const int B = 256;
    for (size_t i0 = 0; i0 + B <= x.size(); i0 += B) { const float* in[2] = {&x[i0], &x[i0]}; float* out[2] = {&y[i0], &r[i0]}; d.process(in, out, 2, B, p); }
    if (vu) *vu = d.vuDb();
    return y;
}
static std::vector<float> sine(float hz, float amp, float sec) { std::vector<float> v(size_t(SR * sec)); for (size_t i = 0; i < v.size(); ++i) v[i] = amp * std::sin(2 * float(M_PI) * hz * i / SR); return v; }
static double thd(const std::vector<float>& y, float hz) {
    const size_t s0 = y.size() / 2, N = size_t(SR / hz) * size_t(std::floor((y.size() - s0) * hz / SR));
    auto mag = [&](double f) { double re = 0, im = 0; for (size_t i = 0; i < N; ++i) { re += y[s0 + i] * std::cos(2 * M_PI * f * i / SR); im += y[s0 + i] * std::sin(2 * M_PI * f * i / SR); } return std::hypot(re, im); };
    double h = 0; for (int k = 2; k <= 8; ++k) if (hz * k < SR / 2) { const double m = mag(hz * k); h += m * m; }
    return 100 * std::sqrt(h) / mag(hz);
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
static float lu(const SatekiParams& p, float levelDb) {
    std::vector<float> x(size_t(SR * 6)); pink(x, 3);
    double e = 0; for (float s : x) e += s * s; const float k = dbToGain(levelDb) / float(std::sqrt(e / x.size())); for (auto& s : x) s *= k;
    auto y = run(p, x); KW a, b; double ei = 0, eo = 0;
    for (size_t i = 0; i < x.size(); ++i) { const float u = a.tick(x[i]), v = b.tick(y[i]); if (i > x.size() / 2) { ei += u * u; eo += v * v; } }
    return float(10 * std::log10(eo / ei));
}
static double band(const std::vector<float>& y, double lo, double hi) {
    Biquad h, l; h.designMatched(Analog2::highPass(lo, 0.707), SR, lo); l.designMatched(Analog2::lowPass(hi, 0.707), SR, hi);
    double e = 0; for (size_t i = y.size() / 4; i < y.size(); ++i) { const float v = l.tick(h.tick(y[i])); e += v * v; } return e;
}

int main() {
    std::printf("=== 1) DRIVE 0 (minden egység): átlátszó? (mért szinusz-átvitel, dB) ===\n ");
    { SatekiParams p = P0(); p.drive[1] = 0; p.autoGain = 0;
      for (float hz : {100.f, 1000.f, 5000.f, 10000.f, 16000.f}) { auto x = sine(hz, 0.05f, 0.6f); auto y = run(p, x); double a = 0, b = 0;
          for (size_t i = x.size() / 2; i < x.size(); ++i) { a += x[i] * x[i]; b += y[i] * y[i]; } std::printf(" %gk %+.3f", hz / 1000, 10 * std::log10(b / a)); } }
    std::printf("\n\n=== 2) a sáv alatti rész érintetlen? THD %% (minden DRIVE 10): 1 kHz és 3 kHz kell ≈ 0, a sávban lágy (kis THD) ===\n");
    { SatekiParams p = P0(); p.drive[0] = p.drive[1] = p.drive[2] = 10; p.autoGain = 0;
      std::printf("  1 kHz %.3f %%   3 kHz %.3f %%   (LOW 7 kHz-től dolgozik)\n", thd(run(p, sine(1000, 0.126f, 1.2f)), 1000), thd(run(p, sine(3000, 0.126f, 1.2f)), 3000)); }
    std::printf("\n=== 2b) a levegő-emelés szintfüggése (MID 10): 200 Hz -12 dBFS alap + 12 kHz hang különböző szinten — halk magas sokat, hangos keveset kap ===\n ");
    for (float lv : {-60.f, -50.f, -40.f, -30.f, -20.f, -12.f}) {
        std::vector<float> x(size_t(SR * 1.5)); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.25f * std::sin(2 * float(M_PI) * 200 * i / SR) + dbToGain(lv) * std::sin(2 * float(M_PI) * 12000 * i / SR);
        SatekiParams p = P0(); p.drive[1] = 10; p.autoGain = 0; auto y = run(p, x); SatekiParams q = p; q.drive[1] = 0; auto yd = run(q, x);
        auto mag = [&](const std::vector<float>& v, double f) { double re = 0, im = 0; const size_t s0 = v.size() / 2; for (size_t i = s0; i < v.size(); ++i) { re += v[i] * std::cos(2 * M_PI * f * i / SR); im += v[i] * std::sin(2 * M_PI * f * i / SR); } return std::hypot(re, im); };
        std::printf(" %+.0f dBFS: %+5.1f dB ", lv, 20 * std::log10(mag(y, 12000) / mag(yd, 12000)));
    }
    std::printf("\n");
    std::printf("\n=== 3) dob-ütéseken (lábdob + pergő + kevés hi-hat, a referencia-loophoz hasonló): sávarány változás a száraz jelhez képest (dB) és a VU ===\n");
    std::vector<float> drum(size_t(SR * 4)); { std::mt19937 g(5); std::normal_distribution<float> nd;
        for (size_t k = 0; k < 32; ++k) for (int i = 0; i < 6000; ++i) { const double t = i / SR;
            drum[k * 6000 + i] += float((k % 4 == 2 ? 0.10 : 0.02) * std::exp(-t / (k % 4 == 2 ? 0.05 : 0.015)) * nd(g) + (k % 4 == 2 ? 0.25 * std::exp(-t / 0.06) * std::sin(2 * M_PI * 190 * t) : 0) + (k % 4 == 0 ? 0.45 * std::exp(-t / 0.15) * std::sin(2 * M_PI * 55 * t) : 0)); } }
    auto bands = [&](const std::vector<float>& y) { return std::vector<double>{band(y, 20, 2000), band(y, 2000, 6000), band(y, 6000, 10000), band(y, 10000, 16000), band(y, 16000, 22000)}; };
    SatekiParams dryP = P0(); dryP.drive[1] = 0; dryP.autoGain = 1; const auto ref = bands(run(dryP, drum));
    struct C { const char* n; float lo, mid, hi; };
    for (C c : {C{"MID 5", 0, 5, 0}, C{"MID 10", 0, 10, 0}, C{"LOW 10", 10, 0, 0}, C{"HIGH 10", 0, 0, 10}, C{"mind 6", 6, 6, 6}, C{"mind 10", 10, 10, 10}, C{"MID 7", 0, 7, 0}, C{"MID 7 + HIGH 10", 0, 7, 10}, C{"LOW 7 + HIGH 10", 7, 0, 10}}) {
        SatekiParams p = P0(); p.drive[0] = c.lo; p.drive[1] = c.mid; p.drive[2] = c.hi; float vu = 0;
        const auto b = bands(run(p, drum, SR, &vu)); double tot = 0, tr = 0; for (int i = 0; i < 5; ++i) { tot += b[i]; tr += ref[i]; }
        std::printf("  %-16s <2k %+5.1f  2-6k %+5.1f  6-10k %+5.1f  10-16k %+5.1f  16k+ %+5.1f   VU %+.1f\n", c.n,
                    10 * std::log10(b[0] / tot / (ref[0] / tr)), 10 * std::log10(b[1] / tot / (ref[1] / tr)), 10 * std::log10(b[2] / tot / (ref[2] / tr)),
                    10 * std::log10(b[3] / tot / (ref[3] / tr)), 10 * std::log10(b[4] / tot / (ref[4] / tr)), vu);
    }
    { SatekiParams p; float vu = 0; const auto b = bands(run(p, drum, SR, &vu)); SatekiParams q = p; q.drive[0] = q.drive[1] = q.drive[2] = 0;
      const auto r = bands(run(q, drum)); double tot = 0, tr = 0; for (int i = 0; i < 5; ++i) { tot += b[i]; tr += r[i]; }
      std::printf("  ALAP (jóváhagyott: 5/5/5, 6k/10k/16k, IN +12, MIX 60)  <2k %+5.1f  2-6k %+5.1f  6-10k %+5.1f  10-16k %+5.1f  16k+ %+5.1f   VU %+.1f\n",
                  10 * std::log10(b[0] / tot / (r[0] / tr)), 10 * std::log10(b[1] / tot / (r[1] / tr)), 10 * std::log10(b[2] / tot / (r[2] / tr)),
                  10 * std::log10(b[3] / tot / (r[3] / tr)), 10 * std::log10(b[4] / tot / (r[4] / tr)), vu);
      SatekiParams o = p; o.autoGain = 0; std::printf("  ALAP AUTO GAIN: be %+.2f LU (ki %+.2f LU)\n", lu(p, -18), lu(o, -18)); }
    std::printf("\n=== 3c) ütés / lecsengés: 5 kHz feletti szint a száraz jelhez képest (dB) — a levegő-emelés a halk lecsengést jobban emeli ===\n");
    for (C c : {C{"MID 5", 0, 5, 0}, C{"MID 10", 0, 10, 0}, C{"LOW 5", 5, 0, 0}, C{"LOW 10", 10, 0, 0}, C{"HIGH 10", 0, 0, 10}, C{"LOW+MID 7", 7, 7, 0}}) {
        {
            SatekiParams p = P0(); p.drive[0] = c.lo; p.drive[1] = c.mid; p.drive[2] = c.hi; p.autoGain = 0;
            SatekiParams q = p; q.drive[0] = q.drive[1] = q.drive[2] = 0;
            auto y = run(p, drum), yd = run(q, drum);
            auto tailHf = [&](const std::vector<float>& v) { Biquad h1, h2; h1.designHP2(5000, SR); h2.designHP2(5000, SR); double hit = 0, tail = 0;
                for (size_t i = 0; i < v.size(); ++i) { const float w = h2.tick(h1.tick(v[i])); const size_t ph = i % 6000;
                    if (i < v.size() / 4) continue; if (ph < 1500) hit += w * w; else if (ph > 3500) tail += w * w; }
                return std::pair<double, double>{hit, tail}; };
            const auto [hw, tw] = tailHf(y); const auto [hd, td] = tailHf(yd);
            std::printf("  %-10s ütés %+5.1f dB   lecsengés %+5.1f dB\n", c.n, 10 * std::log10(hw / hd), 10 * std::log10(tw / td));
        }
    }
    std::printf("\n=== 4) AUTO GAIN: K-súlyozott hangerő-eltérés (LU), rózsaszín zaj -18 dBFS ===\n");
    for (C c : {C{"MID 5", 0, 5, 0}, C{"mind 10", 10, 10, 10}}) { SatekiParams p = P0(); p.drive[0] = c.lo; p.drive[1] = c.mid; p.drive[2] = c.hi;
        SatekiParams off = p; off.autoGain = 0; std::printf("  %-8s AUTO be %+.2f LU  (ki %+.2f LU)\n", c.n, lu(p, -18), lu(off, -18)); }
    std::printf("\n=== 5) stabilitás: minden max, +12 dB LEVEL IN, hangos zaj, 44.1/96 kHz ===\n");
    for (float sr : {44100.f, 96000.f}) { SatekiParams p = P0(); p.drive[0] = p.drive[1] = p.drive[2] = 10; p.levelInDb = 12; p.autoGain = 0;
        std::vector<float> x(size_t(sr * 2)); pink(x, 5); for (auto& s : x) s *= 3; auto y = run(p, x, sr);
        bool ok = true; for (float v : y) if (!std::isfinite(v)) ok = false; std::printf("  %g Hz: %s\n", sr, ok ? "OK (véges)" : "HIBA"); }
    return 0;
}
