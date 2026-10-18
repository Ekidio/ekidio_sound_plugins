// OPTEKI offline mérés: statikus görbe, felfutás, kétlépcsős elengedés, programfüggés, THD
//   clang++ -std=c++20 -O2 -IShared -IPlugins/OPTEKI Tools/opteki_measure.cpp -o build/opteki_measure && build/opteki_measure
#include "OptekiDSP.h"
#include <cstdio>
#include <vector>
using namespace eki;

static const float SR = 48000;

struct Run {
    OptekiDSP d; OptekiParams p;
    Run(OptekiParams pp) : p(pp) { d.prepare(SR); }
    // szinusz blokk, visszaadja a kimenet utolsó periódusainak csúcsát
    float sine(float amp, float hz, float seconds, std::vector<float>* grTrace = nullptr, std::vector<float>* outRec = nullptr) {
        int n = int(seconds * SR); float pk = 0; static float ph = 0;
        float buf[64], ob[64];
        for (int i = 0; i < n; i += 64) {
            int m = std::min(64, n - i);
            for (int k = 0; k < m; ++k) { buf[k] = amp * std::sin(ph); ph += 2 * kPi * hz / SR; if (ph > 2 * kPi) ph -= 2 * kPi; }
            const float* in[2] = {buf, buf}; float ob2[64]; float* out[2] = {ob, ob2};
            d.process(in, out, 2, m, p);
            if (grTrace) grTrace->push_back(d.gainReductionDb());
            if (outRec) outRec->insert(outRec->end(), ob, ob + m);
            if (i > n - int(SR * 0.1f)) for (int k = 0; k < m; ++k) pk = std::max(pk, std::fabs(ob[k]));
        }
        return pk;
    }
};

static double harmonic(const std::vector<float>& x, double hz) {
    double re = 0, im = 0; for (size_t i = 0; i < x.size(); ++i) { re += x[i] * cos(2 * M_PI * hz * i / SR); im -= x[i] * sin(2 * M_PI * hz * i / SR); }
    return 2 * sqrt(re * re + im * im) / x.size();
}

int main() {
    for (int mode = 0; mode < 2; ++mode) {
        printf("\n=== Statikus görbe, %s, PR 40/70, ANALOG 0 (1 kHz) ===\n in dBFS |  PR40 out  GR  |  PR70 out  GR\n", mode ? "LIMIT" : "COMPRESS");
        for (float inDb = -40; inDb <= 0.1f; inDb += 5) {
            printf(" %6.1f  |", inDb);
            for (float pr : {40.f, 70.f}) {
                OptekiParams p; p.peakReduction = pr; p.limit = mode; p.analog = 0; p.autoGain = 0;
                Run r(p); float pk = r.sine(dbToGain(inDb), 1000, 4.0f);
                printf("  %6.1f %5.1f |", gainToDb(pk), gainToDb(pk) - inDb);
            }
            printf("\n");
        }
    }
    printf("\n=== Dinamika: PR 60, COMPRESS, -6 dBFS burst, majd csend ===\n");
    for (float burst : {0.2f, 3.0f}) {
        OptekiParams p; p.peakReduction = 60; p.analog = 0; p.autoGain = 0; Run r(p);
        std::vector<float> gr;
        r.sine(dbToGain(-40), 1000, 1.0f);
        size_t a0 = gr.size(); r.sine(dbToGain(-6), 1000, burst, &gr);
        float grMax = 0; for (size_t i = a0; i < gr.size(); ++i) grMax = std::min(grMax, gr[i]);
        // felfutás: mikor éri el a végső GR 63%-át
        int ia = 0; for (size_t i = a0; i < gr.size(); ++i) if (gr[i] <= grMax * 0.63f) { ia = int(i - a0); break; }
        size_t r0 = gr.size(); r.sine(dbToGain(-60), 1000, 8.0f, &gr);
        auto tTo = [&](float frac) { for (size_t i = r0; i < gr.size(); ++i) if (gr[i] >= grMax * frac) return (i - r0) * 64.0f / SR; return -1.0f; };
        printf(" burst %.1fs: GR %.1f dB, felfutás(63%%) %.1f ms | elengedés 50%%: %.0f ms, 90%%: %.2f s, 99%%: %.2f s\n",
               burst, grMax, ia * 64.0f / SR * 1000, tTo(0.5f) * 1000, tTo(0.1f), tTo(0.01f));
    }
    printf("\n=== Harmonikus torzítás, PR 0 (nincs kompresszió), 100 Hz és 1 kHz ===\n");
    for (float an : {0.f, 50.f, 100.f}) for (float lvl : {-18.f, -6.f, 0.f}) for (float hz : {100.f, 1000.f}) {
        OptekiParams p; p.peakReduction = 0; p.analog = an; p.autoGain = 0; Run r(p);
        r.sine(dbToGain(lvl), hz, 0.5f);
        std::vector<float> rec; r.sine(dbToGain(lvl), hz, 1.0f, nullptr, &rec);
        double h1 = harmonic(rec, hz), h2 = harmonic(rec, 2 * hz), h3 = harmonic(rec, 3 * hz);
        printf(" ANALOG %3.0f%% %5.0f dBFS %5.0f Hz: szint %+5.2f dB  H2 %6.1f dB  H3 %6.1f dB\n", an, lvl, hz,
               20 * log10(h1) - lvl, 20 * log10(h2 / h1 + 1e-12), 20 * log10(h3 / h1 + 1e-12));
    }

    printf("\n=== AUTO GAIN: lüktető jel (220 Hz, 4 Hz-es burkoló, csúcs −6 dBFS), RMS be/ki az utolsó 4 s-ban ===\n");
    for (int mode = 0; mode < 2; ++mode) for (float pr : {0.f, 20.f, 40.f, 60.f, 80.f, 100.f}) {
        OptekiParams p; p.peakReduction = pr; p.limit = mode; p.analog = 50;
        OptekiDSP d; d.prepare(SR);
        double ei = 0, eo = 0; float ph = 0; int n = int(SR * 10);
        float b[64], o0[64], o1[64];
        for (int i = 0; i < n; i += 64) {
            for (int k = 0; k < 64; ++k) {
                float t = float(i + k) / SR;
                float env = 0.15f + 0.85f * std::pow(0.5f + 0.5f * std::sin(2 * kPi * 4 * t), 3.0f);
                b[k] = 0.5f * env * std::sin(ph); ph += 2 * kPi * 220 / SR; if (ph > 2 * kPi) ph -= 2 * kPi;
            }
            const float* in[2] = {b, b}; float* out[2] = {o0, o1};
            d.process(in, out, 2, 64, p);
            if (i > n - int(SR * 4)) for (int k = 0; k < 64; ++k) { ei += b[k] * b[k]; eo += o0[k] * o0[k]; }
        }
        printf(" %s PR %3.0f: RMS eltérés %+5.2f dB, auto makeup %+5.1f dB\n", mode ? "LIMIT   " : "COMPRESS", pr,
               10 * log10(eo / ei), d.autoMakeupDb());
    }
}
