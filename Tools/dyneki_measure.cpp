// DYNEKI-6 offline mérések: görbe-pontosság (valódi szinusz-átvitel vs. analóg prototípus), M/S, SAT torzítás,
// DYN csökkentés, AUTO GAIN hangerő-tartás, stabilitás.
//   clang++ -std=c++20 -O2 -IShared -IPlugins/DYNEKI Tools/dyneki_measure.cpp -o build/dyneki_measure && build/dyneki_measure
#include "DynekiDSP.h"
#include <cstdio>
#include <vector>
#include <random>

using namespace eki;

static DynekiParams clean() {
    DynekiParams p;
    p.sat = 0; p.comp = 0; p.analog = 0; p.autoGain = 0; p.mix = 100;
    return p;
}

/// szinusz átvitele dB-ben (a beállás után RMS-arány), mindkét csatornán ugyanaz a jel vagy (side) ellentétes
static float sineGain(float sr, const DynekiParams& p, float hz, float amp = 0.1f, bool side = false) {
    DynekiDSP d; d.prepare(sr);
    const int n = int(sr * 0.6f), skip = int(sr * 0.35f), B = 256;
    std::vector<float> l(B), r(B), ol(B), orr(B);
    double ei = 0, eo = 0;
    for (int i0 = 0; i0 < n; i0 += B) {
        for (int i = 0; i < B; ++i) {
            const float x = amp * std::sin(2 * float(M_PI) * hz * (i0 + i) / sr);
            l[i] = x; r[i] = side ? -x : x;
        }
        const float* in[2] = {l.data(), r.data()}; float* out[2] = {ol.data(), orr.data()};
        d.process(in, out, 2, B, p);
        if (i0 >= skip) for (int i = 0; i < B; ++i) { ei += l[i] * l[i]; eo += ol[i] * ol[i]; }
    }
    return float(10 * std::log10(eo / ei));
}

static void pink(std::vector<float>& v, unsigned seed) {
    std::mt19937 g(seed); std::normal_distribution<float> nd;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& s : v) {
        const float w = nd(g);
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        s = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f; b6 = w * 0.115926f;
    }
}

/// K-súlyozó szűrő (BS.1770), a hangerő-érzet méréséhez
struct KW { Biquad s, h; void prep(float sr) {
    s.designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr, 1681); h.designMatched(Analog2::highPass(38.0, 0.5), sr, 38); }
    float tick(float x) { return h.tick(s.tick(x)); } };

/// rózsaszín zaj (sztereó, kissé eltérő csatornák) be/ki RMS-eltérése dB-ben az utolsó 4 s-on; gr: átlagos DYN csökkentés
static float pinkDelta(float sr, const DynekiParams& p, float levelDb, float* gr = nullptr, bool kWeighted = false) {
    DynekiDSP d; d.prepare(sr);
    const int n = int(sr * 8), B = 512;
    std::vector<float> l(n), r(n);
    pink(l, 1); pink(r, 2);
    for (int i = 0; i < n; ++i) r[i] = 0.7f * l[i] + 0.3f * r[i];
    double e = 0; for (float s : l) e += s * s;
    const float k = dbToGain(levelDb) / float(std::sqrt(e / n));
    for (int i = 0; i < n; ++i) { l[i] *= k; r[i] *= k; }
    std::vector<float> ol(B), orr(B);
    double ei = 0, eo = 0, gsum = 0; int gcount = 0;
    KW ki[2], ko[2]; for (int c = 0; c < 2; ++c) { ki[c].prep(sr); ko[c].prep(sr); }
    for (int i0 = 0; i0 + B <= n; i0 += B) {
        const float* in[2] = {&l[i0], &r[i0]}; float* out[2] = {ol.data(), orr.data()};
        d.process(in, out, 2, B, p);
        for (int i = 0; i < B; ++i) {
            float a0 = l[i0 + i], a1 = r[i0 + i], b0 = ol[i], b1 = orr[i];
            if (kWeighted) { a0 = ki[0].tick(a0); a1 = ki[1].tick(a1); b0 = ko[0].tick(b0); b1 = ko[1].tick(b1); }
            if (i0 > n / 2) { ei += a0 * a0 + a1 * a1; eo += b0 * b0 + b1 * b1; }
        }
        if (i0 > n / 2) {
            gsum += d.gainReductionDb(); ++gcount;
        }
    }
    if (gr) *gr = float(gsum / std::max(1, gcount));
    return float(10 * std::log10(eo / ei));
}

/// THD (%) 1 kHz-en (DFT a harmonikusokra)
static float thd(float sr, const DynekiParams& p, float amp) {
    DynekiDSP d; d.prepare(sr);
    const float hz = 1000;
    const int n = int(sr * 1.0f), B = 256, start = int(sr * 0.5f);
    std::vector<float> l(B), r(B), ol(B), orr(B), y;
    for (int i0 = 0; i0 < n; i0 += B) {
        for (int i = 0; i < B; ++i) l[i] = r[i] = amp * std::sin(2 * float(M_PI) * hz * (i0 + i) / sr);
        const float* in[2] = {l.data(), r.data()}; float* out[2] = {ol.data(), orr.data()};
        d.process(in, out, 2, B, p);
        if (i0 >= start) y.insert(y.end(), ol.begin(), ol.end());
    }
    const int N = int(sr / hz) * 400;
    y.resize(std::min<size_t>(y.size(), N));
    auto mag = [&](float f) {
        double re = 0, im = 0;
        for (size_t i = 0; i < y.size(); ++i) { re += y[i] * std::cos(2 * M_PI * f * i / sr); im += y[i] * std::sin(2 * M_PI * f * i / sr); }
        return std::hypot(re, im);
    };
    const double f1 = mag(hz);
    double h = 0;
    for (int k = 2; k <= 7; ++k) { const double m = mag(hz * k); h += m * m; }
    return float(100 * std::sqrt(h) / f1);
}

static double analogDb(const Analog2& a, double hz) { return 10 * std::log10(a.mag2(2 * M_PI * hz)); }

int main() {
    std::printf("=== 1) lapos (minden 0): átvitel ===\n");
    for (float sr : {44100.0f, 48000.0f}) {
        std::printf("  %5.0f Hz:", sr);
        for (float hz : {30.f, 100.f, 1000.f, 10000.f, 16000.f, 20000.f}) std::printf("  %gk %+.3f", hz / 1000, sineGain(sr, clean(), hz));
        std::printf("\n");
    }

    std::printf("\n=== 2) görbe-pontosság 44,1 kHz-en (mért szinusz vs. analóg prototípus, dB) ===\n");
    struct Case { const char* name; int voice; int band; int freqIdx; float gain; };
    const Case cases[] = {
        {"LOW 60 +8 BRIT", 0, 0, 2, 8}, {"LOW 110 -8 PUNCH", 1, 0, 3, -8},
        {"LM 400 +10 BRIT", 0, 1, 1, 10}, {"LM 400 +10 PUNCH", 1, 1, 1, 10},
        {"HM 10k +10 BRIT", 0, 2, 4, 10}, {"HIGH 16k +8 BRIT", 0, 3, 3, 8}, {"HIGH 12k -10 PUNCH", 1, 3, 2, -10},
        {"AIR 20k +12", 0, 4, 3, 12}, {"AIR 40k +15", 0, 4, 4, 15}, {"AIR 10k +10", 0, 4, 2, 10},
    };
    const float probe[] = {35, 60, 110, 220, 400, 800, 1500, 3000, 5000, 8000, 10000, 12000, 14000, 16000, 18000, 20000};
    for (const auto& c : cases) {
        DynekiParams p = clean(); p.voice = c.voice;
        Analog2 a;
        const bool punch = c.voice == 1;
        switch (c.band) {
            case 0: p.a.lowF = c.freqIdx; p.a.lowG = c.gain; a = Analog2::lowShelf(kLowHz[c.freqIdx], c.gain, punch ? 0.75 : 1.1); break;
            case 1: p.a.lmF = c.freqIdx; p.a.lmG = c.gain; a = Analog2::bell(kLmHz[c.freqIdx], c.gain, punch ? 0.5 + 0.1 * std::fabs(c.gain) : 0.9); break;
            case 2: p.a.hmF = c.freqIdx; p.a.hmG = c.gain; a = Analog2::bell(kHmHz[c.freqIdx], c.gain, punch ? 0.5 + 0.1 * std::fabs(c.gain) : 0.9); break;
            case 3: p.a.highF = c.freqIdx; p.a.highG = c.gain; a = Analog2::highShelf(kHighHz[c.freqIdx], c.gain, punch ? 0.9 : 0.6); break;
            case 4: p.a.airF = c.freqIdx; p.a.airG = c.gain; a = Analog2::bell(kAirHz[c.freqIdx], c.gain, 0.45); break;
        }
        float worst = 0, worstHz = 0;
        for (float hz : probe) {
            const float e = sineGain(44100, p, hz) - float(analogDb(a, hz));
            if (std::fabs(e) > std::fabs(worst)) { worst = e; worstHz = hz; }
        }
        std::printf("  %-20s legnagyobb eltérés %+.2f dB (%g Hz);  20k-en: mért %+.2f / analóg %+.2f\n", c.name, worst, worstHz,
                    sineGain(44100, p, 20000), float(analogDb(a, 20000)));
    }

    std::printf("\n=== 2b) szélső állások hallható tartományban (mért dB) ===\n");
    { DynekiParams p = clean(); p.a.lowF = 0; p.a.lowG = 8;
      std::printf("  LOW 15 Hz +8:  35Hz %+.1f  60Hz %+.1f  100Hz %+.1f  150Hz %+.1f\n", sineGain(48000, p, 35), sineGain(48000, p, 60), sineGain(48000, p, 100), sineGain(48000, p, 150));
      p.a.lowF = 1;
      std::printf("  LOW 35 Hz +8:  35Hz %+.1f  60Hz %+.1f  100Hz %+.1f  150Hz %+.1f\n", sineGain(48000, p, 35), sineGain(48000, p, 60), sineGain(48000, p, 100), sineGain(48000, p, 150));
      p = clean(); p.a.airF = 4; p.a.airG = 10;
      std::printf("  AIR 40k +10:   5k %+.1f  8k %+.1f  12k %+.1f  16k %+.1f  20k %+.1f\n", sineGain(48000, p, 5000), sineGain(48000, p, 8000), sineGain(48000, p, 12000), sineGain(48000, p, 16000), sineGain(48000, p, 20000)); }

    std::printf("\n=== 3) HPF 18 dB/okt (80 Hz): ===\n ");
    { DynekiParams p = clean(); p.a.hpf = 2;
      for (float hz : {20.f, 40.f, 80.f, 160.f, 320.f}) std::printf("  %gHz %+.1f", hz, sineGain(48000, p, hz)); std::printf("\n"); }

    std::printf("\n=== 4) M/S: SIDE készlet erősen állítva, mono (MID) jel ===\n");
    { DynekiParams p = clean(); p.ms = 1; p.b.lowG = 12; p.b.hmG = -12; p.b.airG = 15; p.b.hpf = 4;
      std::printf("  mono jel 100 Hz %+.3f dB, 5 kHz %+.3f dB (0 kell)\n", sineGain(48000, p, 100), sineGain(48000, p, 5000));
      std::printf("  side jel 5 kHz %+.2f dB (≈ -12 kell)\n", sineGain(48000, p, 5000, 0.1f, true)); }

    std::printf("\n=== 4b) M/S + AUTO GAIN: a SIDE emelése megmarad-e (side jel átvitele, dB) ===\n");
    { DynekiParams p = clean(); p.ms = 1; p.autoGain = 1; p.b.highG = 6; p.b.airG = 6; p.b.lowG = 4;
      std::printf("  SIDE: LOW +4, HIGH +6, AIR +6 → side jel 1k %+.2f, 10k %+.2f;  mid jel 1k %+.2f\n",
                  sineGain(48000, p, 1000, 0.1f, true), sineGain(48000, p, 10000, 0.1f, true), sineGain(48000, p, 1000)); }

    std::printf("\n=== 5) SAT torzítás (THD %%, 1 kHz) ===\n");
    for (int v = 0; v < 2; ++v) {
        std::printf("  %s  -18 dBFS:", v ? "PUNCH" : "BRIT ");
        for (float s : {0.f, 2.f, 5.f, 8.f, 10.f}) { DynekiParams p = clean(); p.voice = v; p.sat = s; std::printf("  %g→%.2f", s, thd(48000, p, 0.126f)); }
        std::printf("\n         -6 dBFS:");
        for (float s : {0.f, 2.f, 5.f, 8.f, 10.f}) { DynekiParams p = clean(); p.voice = v; p.sat = s; std::printf("  %g→%.2f", s, thd(48000, p, 0.5f)); }
        std::printf("\n");
    }

    std::printf("\n=== 6) DYN: átlagos csökkentés rózsaszín zajon (dB) ===\n");
    for (float lvl : {-24.f, -18.f, -12.f}) {
        std::printf("  %+.0f dBFS RMS:", lvl);
        for (float c : {0.f, 2.f, 4.f, 6.f, 8.f, 10.f}) { DynekiParams p = clean(); p.comp = c; float gr; pinkDelta(48000, p, lvl, &gr); std::printf("  %g→%.1f", c, gr); }
        std::printf("\n");
    }

    std::printf("\n=== 7) AUTO GAIN: hangerő-eltérés be/ki (K-súlyozott, rózsaszín zaj -18 dBFS; 0 körül kell) ===\n");
    struct AG { const char* name; DynekiParams p; };
    std::vector<AG> ags;
    { DynekiParams p; p.sat = 2; p.comp = 3; ags.push_back({"Default (SAT 2, COMP 3)", p}); }
    { DynekiParams p; p.a.lowG = 8; p.a.airG = 10; ags.push_back({"LOW +8, AIR +10", p}); }
    { DynekiParams p; p.a.hmG = -12; p.a.highG = -10; ags.push_back({"HM -12, HIGH -10", p}); }
    { DynekiParams p; p.a.hpf = 4; ags.push_back({"HPF 300", p}); }
    { DynekiParams p; p.sat = 10; p.comp = 10; ags.push_back({"SAT 10, COMP 10", p}); }
    { DynekiParams p; p.voice = 1; p.sat = 8; p.comp = 7; p.a.lowG = 6; p.a.hmG = 6; ags.push_back({"PUNCH, SAT 8, COMP 7, +6/+6", p}); }
    { DynekiParams p; p.ms = 1; p.b.highG = 8; p.b.airG = 10; p.a.lowG = 4; ags.push_back({"M/S: side +8/+10, mid low +4", p}); }
    { DynekiParams p; p.mix = 50; p.comp = 8; p.sat = 6; ags.push_back({"MIX 50, COMP 8, SAT 6", p}); }
    for (auto& a : ags) {
        DynekiParams off = a.p; off.autoGain = 0;
        std::printf("  %-32s AUTO be: %+.2f LU   (AUTO ki: %+.2f LU)\n", a.name, pinkDelta(48000, a.p, -18, nullptr, true), pinkDelta(48000, off, -18, nullptr, true));
    }

    std::printf("\n=== 8) stabilitás: szélsőséges beállítások, 0 dBFS zaj ===\n");
    { DynekiParams p; p.a = {4, 16, 0, 16, 0, 16, 4, 16, 3, 15, 4}; p.voice = 1; p.sat = 10; p.comp = 10; p.lmQ = 4; p.hmQ = 4;
      p.a.hpf = 4; p.a.lowG = 16; p.a.lmG = 16; p.a.hmG = 16; p.a.highG = 16; p.a.airG = 15;
      float gr; const float dlt = pinkDelta(44100, p, 0, &gr);
      std::printf("  kimenet/bemenet %+.2f dB, GR %.1f dB, %s\n", dlt, gr, std::isfinite(dlt) ? "OK (véges)" : "HIBA: NaN/Inf"); }

    std::printf("\n=== 9) szűrő-épség: minden sáv/frekvencia/Q/VOICE, 44.1/48/88.2/96 kHz (NaN, instabil pólus) ===\n");
    { int bad = 0, total = 0;
      for (float sr : {44100.f, 48000.f, 88200.f, 96000.f}) for (int v = 0; v < 2; ++v) for (int q = 0; q < 5; ++q)
      for (int fi = 0; fi < 5; ++fi) for (float g : {-16.f, -6.f, -0.5f, 0.5f, 6.f, 16.f}) for (int h = 0; h < 5; ++h) {
          DynekiParams p = clean(); p.voice = v; p.lmQ = q; p.hmQ = q;
          p.a = {h, g, g, g, g, std::fabs(g), fi, fi, fi, std::min(fi, 3), fi};
          DynekiDSP d; d.prepare(sr);
          float z[64] = {}, o[64]; z[0] = 1;
          const float* in[1] = {z}; float* out[1] = {o};
          d.process(in, out, 1, 64, p);
          ++total;
          bool ok = true;
          for (float hz : {20.f, 1000.f, 15000.f}) { const float r = d.responseDb(0, hz); if (!std::isfinite(r)) ok = false; }
          for (float y : o) if (!std::isfinite(y)) ok = false;
          if (!ok) { if (bad < 5) std::printf("  HIBA: sr %g voice %d q %d fi %d g %g hpf %d\n", sr, v, q, fi, g, h); ++bad; }
      }
      std::printf("  %d / %d beállítás rendben\n", total - bad, total); }
    return 0;
}
