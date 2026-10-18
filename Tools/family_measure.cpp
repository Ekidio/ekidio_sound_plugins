// VARIEKI / FETEKI / BUSEKI offline mérések
//   clang++ -std=c++20 -O2 -IShared -IPlugins/VARIEKI -IPlugins/FETEKI -IPlugins/BUSEKI -IPlugins/APEKI -IPlugins/DBEKI -IPlugins/TUBEKI -IPlugins/FAIREKI Tools/family_measure.cpp -o build/family_measure
//   build/family_measure [vari|fet|bus]
#include "VariekiDSP.h"
#include "FetekiDSP.h"
#include "BusekiDSP.h"
#include "ApekiDSP.h"
#include "DbekiDSP.h"
#include "TubekiDSP.h"
#include "FairekiDSP.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
using namespace eki;

static const float SR = 48000;

template <class DSP, class P>
struct Bench {
    DSP d; P p; float ph = 0;
    explicit Bench(const P& pp) : p(pp) { d.prepare(SR); }
    // amp(t) burkolójú szinusz; visszaad: utolsó 0,1 s csúcsa; opcionálisan GR-nyom és kimenet
    float run(float seconds, float hz, std::function<float(float)> amp, std::vector<float>* gr = nullptr,
              std::vector<float>* rec = nullptr, double* ei = nullptr, double* eo = nullptr, float tStart = 0) {
        int n = int(seconds * SR); float pk = 0; float b[64], o0[64], o1[64];
        for (int i = 0; i < n; i += 64) {
            int m = std::min(64, n - i);
            for (int k = 0; k < m; ++k) {
                b[k] = amp((i + k) / SR) * std::sin(ph);
                ph += 2 * kPi * hz / SR; if (ph > 2 * kPi) ph -= 2 * kPi;
            }
            const float* in[2] = {b, b}; float* out[2] = {o0, o1};
            d.process(in, out, 2, m, p);
            if (gr) gr->push_back(d.gainReductionDb());
            if (rec) rec->insert(rec->end(), o0, o0 + m);
            if ((i + m) / SR >= tStart) for (int k = 0; k < m; ++k) {
                if (ei) *ei += b[k] * b[k];
                if (eo) *eo += o0[k] * o0[k];
            }
            if (i > n - int(SR * 0.1f)) for (int k = 0; k < m; ++k) pk = std::max(pk, std::fabs(o0[k]));
        }
        return pk;
    }
};

static double harmonic(const std::vector<float>& x, double hz) {
    double re = 0, im = 0;
    for (size_t i = 0; i < x.size(); ++i) { re += x[i] * cos(2 * M_PI * hz * i / SR); im -= x[i] * sin(2 * M_PI * hz * i / SR); }
    return 2 * sqrt(re * re + im * im) / x.size();
}

// --- általános mérések ---
template <class DSP, class P>
void staticCurve(const char* title, std::vector<std::pair<std::string, P>> cols) {
    printf("\n=== %s — statikus görbe (1 kHz), kimenet / GR ===\n in dBFS |", title);
    for (auto& c : cols) printf(" %-16s|", c.first.c_str());
    printf("\n");
    for (float inDb = -40; inDb <= 0.1f; inDb += 5) {
        printf(" %6.1f  |", inDb);
        for (auto& c : cols) {
            Bench<DSP, P> b(c.second);
            float pk = b.run(3.0f, 1000, [&](float) { return dbToGain(inDb); });
            printf(" %6.1f  %6.1f  |", gainToDb(pk), b.d.gainReductionDb());
        }
        printf("\n");
    }
}

template <class DSP, class P>
void dynamics(const char* name, const P& p, float burstDb, std::vector<float> bursts) {
    for (float burst : bursts) {
        Bench<DSP, P> b(p);
        std::vector<float> gr;
        b.run(1.0f, 1000, [](float) { return dbToGain(-50); });
        size_t a0 = gr.size();
        b.run(burst, 1000, [&](float) { return dbToGain(burstDb); }, &gr);
        float grMax = 0; for (size_t i = a0; i < gr.size(); ++i) grMax = std::min(grMax, gr[i]);
        float tAtk = -1; for (size_t i = a0; i < gr.size(); ++i) if (gr[i] <= grMax * 0.63f) { tAtk = (i - a0 + 1) * 64 / SR; break; }
        size_t r0 = gr.size();
        b.run(15.0f, 1000, [](float) { return dbToGain(-60); }, &gr);
        auto tTo = [&](float frac) { for (size_t i = r0; i < gr.size(); ++i) if (gr[i] >= grMax * frac) return (i - r0) * 64 / SR; return -1.0f; };
        printf(" %-22s burst %4.1fs: GR %5.1f dB  felfutás(63%%) ≤%6.2f ms | elengedés 50%%: %5.0f ms  90%%: %5.2f s\n",
               name, burst, grMax, tAtk * 1000, tTo(0.5f) * 1000, tTo(0.1f));
    }
}

template <class DSP, class P>
void distortion(const char* name, const P& p, float lvl, float hz) {
    Bench<DSP, P> b(p);
    b.run(1.5f, hz, [&](float) { return dbToGain(lvl); });
    std::vector<float> rec;
    b.run(1.0f, hz, [&](float) { return dbToGain(lvl); }, nullptr, &rec);
    double h1 = harmonic(rec, hz), h2 = harmonic(rec, 2 * hz), h3 = harmonic(rec, 3 * hz);
    printf(" %-34s %4.0f dBFS %5.0f Hz: GR %5.1f  H2 %6.1f dB  H3 %6.1f dB\n", name, lvl, hz, b.d.gainReductionDb(),
           20 * log10(h2 / h1 + 1e-12), 20 * log10(h3 / h1 + 1e-12));
}

template <class DSP, class P>
void autoGain(const char* name, const P& p) {
    Bench<DSP, P> b(p);
    double ei = 0, eo = 0;
    b.run(10.0f, 220, [](float t) { return 0.5f * (0.15f + 0.85f * std::pow(0.5f + 0.5f * std::sin(2 * kPi * 4 * t), 3.0f)); },
          nullptr, nullptr, &ei, &eo, 6.0f);
    printf(" %-34s RMS eltérés %+5.2f dB  (auto %+5.1f dB, GR most %5.1f)\n", name, 10 * log10(eo / ei), b.d.autoMakeupDb(), b.d.gainReductionDb());
}

int main(int argc, char** argv) {
    std::string only = argc > 1 ? argv[1] : "";

    if (only.empty() || only == "vari") {
        auto V = [](float thr, float ratio, float crest = 6, float atk = 10, float rp = 50, float rr = 250, float an = 0, float ag = 0) {
            VariekiParams p; p.thresholdDb = thr; p.ratio = ratio; p.crestDb = crest; p.attackMs = atk; p.relPeakMs = rp;
            p.relRmsMs = rr; p.analog = an; p.autoGain = ag; return p; };
        staticCurve<VariekiDSP, VariekiParams>("VARIEKI", {{"thr-20 2:1", V(-20, 2)}, {"thr-20 4:1", V(-20, 4)}, {"thr-20 10:1 k0", [&] { auto p = V(-20, 10); p.kneeDb = 0; return p; }()}});
        printf("\n=== VARIEKI — dinamika (thr −24, 2:1, −6 dBFS löket) ===\n");
        dynamics<VariekiDSP>("ATK 10 RMS 250ms", V(-24, 2), -6, {0.2f, 4.0f});
        dynamics<VariekiDSP>("ATK 1 RMS 1000ms", V(-24, 2, 6, 1, 50, 1000), -6, {0.2f, 4.0f});
        printf("\n=== VARIEKI — crest: 1 ms-os tüskék (−3 dBFS) halk alapon (−30 dBFS), 4:1 thr −24 ===\n");
        for (float crest : {0.f, 6.f, 12.f, 20.f}) {
            Bench<VariekiDSP, VariekiParams> b(V(-24, 4, crest, 0.1f, 30, 250));
            std::vector<float> gr;
            b.run(3.0f, 1000, [](float t) { return std::fmod(t, 0.25f) < 0.001f ? dbToGain(-3) : dbToGain(-30); }, &gr);
            float mn = 0; for (size_t i = gr.size() / 2; i < gr.size(); ++i) mn = std::min(mn, gr[i]);
            printf(" PEAK CREST %4.0f dB: legmélyebb GR a tüskéken %6.1f dB\n", crest, mn);
        }
        printf("\n=== VARIEKI — tisztaság (thr −30, 2:1, ~9 dB GR) ===\n");
        for (float an : {0.f, 40.f, 100.f}) for (float rr : {250.f, 50.f}) {
            char nm[48]; snprintf(nm, 48, "ANALOG %3.0f%% RMS rel %4.0f ms", an, rr);
            distortion<VariekiDSP>(nm, V(-30, 2, 6, 10, 50, rr, an), -6, 100);
        }
        printf("\n=== VARIEKI — AUTO GAIN ===\n");
        for (float thr : {-10.f, -20.f, -30.f}) for (float ratio : {2.f, 8.f}) {
            char nm[48]; snprintf(nm, 48, "thr %.0f ratio %.0f", thr, ratio);
            autoGain<VariekiDSP>(nm, V(thr, ratio, 6, 10, 50, 250, 30, 1));
        }
    }

    if (only.empty() || only == "fet") {
        auto F = [](int ratio, float inDb, float atk = 3, float rel = 5, float an = 0, float ag = 0) {
            FetekiParams p; p.ratio = ratio; p.inputDb = inDb; p.attack = atk; p.release = rel; p.analog = an; p.autoGain = ag; return p; };
        staticCurve<FetekiDSP, FetekiParams>("FETEKI", {{"4:1", F(0, 0)}, {"8:1", F(1, 0)}, {"20:1", F(3, 0)}, {"ALL", F(4, 0)}});
        printf("\n=== FETEKI — dinamika (4:1, −6 dBFS löket) ===\n");
        for (auto ar : std::vector<std::pair<float, float>>{{1, 1}, {3, 5}, {7, 7}}) {
            char nm[32]; snprintf(nm, 32, "ATK %.0f REL %.0f", ar.first, ar.second);
            dynamics<FetekiDSP>(nm, F(0, 0, ar.first, ar.second), -6, {0.3f});
        }
        printf("\n=== FETEKI — torzítás (ANALOG 50%%, INPUT +10) ===\n");
        for (auto ar : std::vector<std::pair<float, float>>{{3, 5}, {7, 7}}) for (int ratio : {0, 4}) {
            char nm[48]; snprintf(nm, 48, "ATK %.0f REL %.0f %s", ar.first, ar.second, ratio == 4 ? "ALL" : "4:1");
            distortion<FetekiDSP>(nm, F(ratio, 10, ar.first, ar.second, 50), -6, 100);
        }
        printf("\n=== FETEKI — AUTO GAIN ===\n");
        for (int ratio : {0, 3, 4}) for (float inDb : {0.f, 15.f, 30.f}) {
            char nm[48]; snprintf(nm, 48, "ratio#%d INPUT %+.0f", ratio, inDb);
            autoGain<FetekiDSP>(nm, F(ratio, inDb, 3, 5, 50, 1));
        }
    }

    if (only.empty() || only == "bus") {
        auto B = [](float thr, int ratio, int atk = 5, int rel = 4, float an = 0, float ag = 0, int hpf = 0) {
            BusekiParams p; p.thresholdDb = thr; p.ratio = ratio; p.attack = atk; p.release = rel; p.analog = an; p.autoGain = ag; p.scHpf = hpf; return p; };
        staticCurve<BusekiDSP, BusekiParams>("BUSEKI", {{"thr-20 2:1", B(-20, 0)}, {"thr-20 4:1", B(-20, 1)}, {"thr-20 10:1", B(-20, 2)}});
        printf("\n=== BUSEKI — dinamika (thr −24, 4:1, −6 dBFS löket) ===\n");
        dynamics<BusekiDSP>("ATK 0.1ms REL 0.1s", B(-24, 1, 0, 0), -6, {0.3f});
        dynamics<BusekiDSP>("ATK 30ms REL 1.2s", B(-24, 1, 5, 3), -6, {0.3f});
        dynamics<BusekiDSP>("ATK 30ms AUTO", B(-24, 1, 5, 4), -6, {0.1f, 3.0f});
        printf("\n=== BUSEKI — SC HPF (50 Hz-es szinusz, thr −20, 4:1) ===\n");
        for (int h : {0, 2, 4}) { char nm[32]; snprintf(nm, 32, "HPF #%d", h); distortion<BusekiDSP>(nm, B(-20, 1, 5, 4, 0, 0, h), -6, 50); }
        printf("\n=== BUSEKI — torzítás (thr 0: nincs kompresszió) ===\n");
        for (float an : {0.f, 30.f, 100.f}) { char nm[32]; snprintf(nm, 32, "ANALOG %.0f%%", an); distortion<BusekiDSP>(nm, B(0, 1, 5, 4, an), -1, 1000); }
        printf("\n=== BUSEKI — AUTO GAIN ===\n");
        for (float thr : {-10.f, -20.f, -30.f}) for (int ratio : {0, 2}) {
            char nm[48]; snprintf(nm, 48, "thr %.0f ratio#%d", thr, ratio);
            autoGain<BusekiDSP>(nm, B(thr, ratio, 5, 4, 30, 1));
        }
    }

    if (only.empty() || only == "api") {
        auto A = [](float thr, int ratio, int type = 0, int knee = 1, int thrust = 1, int atk = 5, int rel = 2, float an = 0, float ag = 0) {
            ApekiParams p; p.thresholdDb = thr; p.ratio = ratio; p.type = type; p.knee = knee; p.thrust = thrust;
            p.attack = atk; p.release = rel; p.analog = an; p.autoGain = ag; return p; };
        staticCurve<ApekiDSP, ApekiParams>("APEKI", {{"FF 2:1 MED", A(-20, 1)}, {"FF 4:1 HARD", A(-20, 3, 0, 0)}, {"FF inf HARD", A(-20, 6, 0, 0)}, {"FB 4:1 MED", A(-20, 3, 1)}});
        printf("\n=== APEKI — dinamika (thr −24, 4:1, −6 dBFS löket) ===\n");
        dynamics<ApekiDSP>("ATK .03ms REL .05s", A(-24, 3, 0, 1, 0, 0, 0), -6, {0.3f});
        dynamics<ApekiDSP>("ATK 10ms REL .2s", A(-24, 3, 0, 1, 0, 5, 2), -6, {0.3f});
        dynamics<ApekiDSP>("ATK 30ms REL 2s", A(-24, 3, 0, 1, 0, 6, 5), -6, {0.3f});
        dynamics<ApekiDSP>("OLD (FB) 10ms .2s", A(-24, 3, 1, 1, 0, 5, 2), -6, {0.3f});
        printf("\n=== APEKI — THRUST: 50 Hz és 2 kHz azonos szinten (−6 dBFS, thr −20, 4:1) ===\n");
        for (int th : {0, 1, 2}) for (float hz : {50.f, 2000.f}) {
            char nm[48]; snprintf(nm, 48, "THRUST %s", th == 0 ? "NORM" : th == 1 ? "MED" : "LOUD");
            distortion<ApekiDSP>(nm, A(-20, 3, 0, 1, th), -6, hz);
        }
        printf("\n=== APEKI — torzítás (thr 0, nincs kompresszió) ===\n");
        for (float an : {0.f, 50.f, 100.f}) { char nm[32]; snprintf(nm, 32, "ANALOG %.0f%%", an); distortion<ApekiDSP>(nm, A(0, 3, 0, 1, 1, 5, 2, an), -3, 100); }
        printf("\n=== APEKI — AUTO GAIN ===\n");
        for (float thr : {-10.f, -20.f, -30.f}) for (int type : {0, 1}) {
            char nm[48]; snprintf(nm, 48, "thr %.0f %s", thr, type ? "OLD" : "NEW");
            autoGain<ApekiDSP>(nm, A(thr, 3, type, 1, 1, 5, 2, 50, 1));
        }
    }

    if (only.empty() || only == "dbx") {
        auto D = [](float thr, float ratio, float oe = 0, float an = 0, float ag = 0) {
            DbekiParams p; p.thresholdDb = thr; p.ratio = ratio; p.overEasy = oe; p.analog = an; p.autoGain = ag; return p; };
        staticCurve<DbekiDSP, DbekiParams>("DBEKI", {{"2:1 hard", D(-20, 2)}, {"4:1 hard", D(-20, 4)}, {"4:1 OVEREASY", D(-20, 4, 1)}, {"inf hard", D(-20, 60)}});
        printf("\n=== DBEKI — programfüggő felfutás és állandó sebességű elengedés (−6 dBFS löket, ∞:1) ===\n");
        for (float over : {10.f, 20.f, 30.f}) {
            char nm[48]; snprintf(nm, 48, "túllépés %.0f dB", over);
            dynamics<DbekiDSP>(nm, D(-6 - over, 60), -6, {0.3f});
        }
        printf("  (elvárt elengedés: ~120 dB/s → 10 dB ≈ 83 ms, 20 dB ≈ 167 ms, 30 dB ≈ 250 ms a teljes visszatérésig)\n");
        printf("\n=== DBEKI — torzítás (thr 0) ===\n");
        for (float an : {0.f, 40.f, 100.f}) { char nm[32]; snprintf(nm, 32, "ANALOG %.0f%%", an); distortion<DbekiDSP>(nm, D(0, 4, 0, an), -3, 100); }
        printf("\n=== DBEKI — AUTO GAIN ===\n");
        for (float thr : {-10.f, -20.f, -30.f}) for (float oe : {0.f, 1.f}) {
            char nm[48]; snprintf(nm, 48, "thr %.0f %s", thr, oe > 0.5f ? "OVEREASY" : "hard");
            autoGain<DbekiDSP>(nm, D(thr, 4, oe, 40, 1));
        }
    }

    if (only.empty() || only == "tube") {
        auto T = [](float thr, float ratio, int mode = 0, float atk = 30, float rel = 0.8f, float gain = 0, float an = 0, float ag = 0) {
            TubekiParams p; p.thresholdDb = thr; p.ratio = ratio; p.mode = mode; p.attackMs = atk; p.releaseS = rel;
            p.gainDb = gain; p.analog = an; p.autoGain = ag; return p; };
        staticCurve<TubekiDSP, TubekiParams>("TUBEKI", {{"2:1", T(-20, 2)}, {"4:1", T(-20, 4)}, {"10:1", T(-20, 10)}});
        printf("\n=== TUBEKI — dinamika (thr −24, 4:1, −6 dBFS löket) ===\n");
        dynamics<TubekiDSP>("FIXED", T(-24, 4, 0), -6, {0.2f, 3.0f});
        dynamics<TubekiDSP>("MANUAL 5ms/0.3s", T(-24, 4, 1, 5, 0.3f), -6, {0.2f, 3.0f});
        dynamics<TubekiDSP>("FIXED+MAN 5ms/0.3s", T(-24, 4, 2, 5, 0.3f), -6, {0.2f, 3.0f});
        printf("\n=== TUBEKI — csöves fokozat (thr 0): GAIN-nel belehajtva (−18 dBFS bemenet) ===\n");
        for (float g : {0.f, 12.f, 24.f}) for (float an : {0.f, 50.f, 100.f}) {
            char nm[48]; snprintf(nm, 48, "GAIN %+.0f ANALOG %.0f%%", g, an);
            distortion<TubekiDSP>(nm, T(0, 4, 0, 30, 0.8f, g, an), -18, 100);
        }
        printf("\n=== TUBEKI — AUTO GAIN ===\n");
        for (float thr : {-10.f, -20.f, -30.f}) for (int mode : {0, 1}) {
            char nm[48]; snprintf(nm, 48, "thr %.0f %s", thr, mode ? "MANUAL" : "FIXED");
            autoGain<TubekiDSP>(nm, T(thr, 4, mode, 30, 0.8f, 0, 50, 1));
        }
    }

    if (only.empty() || only == "fair") {
        auto F = [](float thr, int tc = 1, float in = 0, int lv = 0, float an = 0, float ag = 0) {
            FairekiParams p; p.thresholdDb = thr; p.timeConstant = tc; p.inputDb = in; p.latVert = lv; p.analog = an; p.autoGain = ag; return p; };
        staticCurve<FairekiDSP, FairekiParams>("FAIREKI", {{"thr -20", F(-20)}, {"thr -30", F(-30)}, {"thr -20 IN+10", F(-20, 1, 10)}});
        printf("\n=== FAIREKI — TIME CONSTANT (thr −24, −6 dBFS löket) ===\n");
        for (int tc = 0; tc < 6; ++tc) { char nm[16]; snprintf(nm, 16, "TC%d", tc + 1); dynamics<FairekiDSP>(nm, F(-24, tc), -6, {0.2f, 4.0f}); }
        printf("\n=== FAIREKI — torzítás (ANALOG 60%%) ===\n");
        for (float thr : {0.f, -20.f, -30.f}) { char nm[32]; snprintf(nm, 32, "thr %.0f", thr); distortion<FairekiDSP>(nm, F(thr, 1, 0, 0, 60), -6, 100); }
        printf("\n=== FAIREKI — AUTO GAIN ===\n");
        for (float thr : {-10.f, -20.f, -30.f}) for (int lv : {0, 1}) {
            char nm[48]; snprintf(nm, 48, "thr %.0f %s", thr, lv ? "LAT/VERT" : "L/R");
            autoGain<FairekiDSP>(nm, F(thr, 1, 0, lv, 60, 1));
        }
    }
}
