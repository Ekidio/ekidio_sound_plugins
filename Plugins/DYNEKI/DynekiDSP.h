// DynekiDSP.h — DYNEKI-6: hatsávos karakteres csatorna-EQ + SAT + egygombos DYN, AU-független.
//
// Jelút (2x túlmintavételezve):  EQ (L/R vagy M/S kettős EQ) → SAT → DYN → ANALOG szín → MIX → OUTPUT / AUTO GAIN
//   EQ: HPF (18 dB/okt) · LOW polc · LOW MID / HIGH MID harang · HIGH polc · AIR (széles, csak emel)
//       VOICE: BRIT = széles, tekercses görbék (polcok enyhe púppal/bemélyedéssel, állandó Q) + vas/trafó szín
//              PUNCH = arányos Q (nagyobb emelés → szűkebb harang), feszesebb polcok + op-amp/szalag szín
//       A szűrők az analóg prototípus "illesztett" (matched) digitális megfelelői: a pólusok pontosan,
//       a nagyság DC-n, Nyquist-en és a sáv frekvenciáján egyezik → nincs "összegyűrődés" a magasban.
//   SAT: lágy, aszimmetrikus telítés (a legmélyebb tartományt kíméli, felül szalagszerűen kerekít), a VOICE-t követi
//   DYN: a DBEKI motorja egy gombbal (valódi RMS, programfüggő felfutás, 120 dB/s elengedés, lágy térd), mindig sztereó
//   AUTO GAIN: EQ = a görbéből számolt állandó kompenzáció (nem pumpál) + SAT/DYN = energia-összevetés
//   MIX: a SAT+DYN rész párhuzamos keverése az EQ-zott jellel (azonos időalapon, nincs fésűszűrés)
#pragma once
#include "EKIAnalog.h"
#include "EKIFilters.h"
#include <array>
#include <complex>

namespace eki {

// ---- frekvencia-táblák (a paraméterek indexei) ----
inline constexpr float kHpfHz[] = {0, 50, 80, 160, 300};
inline constexpr float kLowHz[] = {15, 35, 60, 110, 220};
inline constexpr float kLmHz[] = {200, 400, 800, 1500, 3000};
inline constexpr float kHmHz[] = {1500, 3000, 5000, 7000, 10000};
inline constexpr float kHighHz[] = {8000, 10000, 12000, 16000};
inline constexpr float kAirHz[] = {2500, 5000, 10000, 20000, 40000};
inline constexpr float kQTable[] = {0, 0.5f, 1.0f, 2.0f, 4.0f};   // 0 = AUTO

/// egy EQ-készlet (L/R módban mindkét csatornán, M/S módban az egyik a MID, a másik a SIDE)
struct EqSet {
    int hpf = 0;
    float lowG = 0, lmG = 0, hmG = 0, highG = 0, airG = 0;
    int lowF = 2, lmF = 1, hmF = 2, highF = 2, airF = 3;
};

struct DynekiParams {
    EqSet a, b;                // a = L/R vagy MID, b = SIDE
    int voice = 0;             // 0 = BRIT, 1 = PUNCH
    int ms = 0;                // 0 = L/R, 1 = M/S
    float sat = 0;             // 0…10
    float comp = 0;            // 0…10
    float outputDb = 0;        // −12…+12 dB
    float autoGain = 1;
    float analog = 60;         // 0…100 %
    int lmQ = 0, hmQ = 0;      // kQTable index (0 = AUTO)
    float mix = 100;           // 0…100 % (SAT + DYN párhuzamos keverése)
};

// MARK: - a DSP

class DynekiDSP {
public:
    static constexpr int kMaxCh = 2;
    static constexpr int kBands = 7;      // HPF (2 szakasz) + LOW + LM + HM + HIGH + AIR
    static constexpr int kSub = 32;       // paraméter-frissítés ennyi mintánként

    void prepare(float sampleRate) {
        sr = sampleRate; sr2 = 2 * sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            satLow[c].set(90.0f, sr2);
            satHf[c].set(15000.0f, sr2);
            satPre[c].designMatched(Analog2::bell(500.0, 5.0, 0.7), sr2, 500.0);
            satDe[c].designMatched(Analog2::bell(500.0, -5.0, 0.7), sr2, 500.0);
            iron[c].prepare(sr2, 60000.0f, 90.0f);
            dc[c].set(sr, 3.0f);
        }
        kRms = 1.0f - onePoleCoef(0.004f, sr2);
        kSatEnv = 1.0f - onePoleCoef(0.3f, sr2);
        relPerSample = 120.0f / sr2;
        autoMk.prepare(sr2);
        for (Biquad* k : {&kwIn[0], &kwOut[0]}) k->designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr2, 1681.0);
        for (Biquad* k : {&kwIn[1], &kwOut[1]}) k->designMatched(Analog2::highPass(38.0, 0.5), sr2, 38.0);
        meter.prepare(sr);
        const float kSm = 1.0f - onePoleCoef(0.03f, sr / kSub);   // ≈30 ms simítás a blokk-ütemben
        smoothK = kSm;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            os[c].reset(); dc[c].reset(); satLow[c].reset(); satHf[c].reset(); iron[c].reset(); satPre[c].reset(); satDe[c].reset();
            for (auto& f : eq[c]) f.reset();
        }
        rms = 0; grDb = 0; gNow = 1; satEnv = 0.0625f; satK = 1;
        autoMk.reset(); meter.reset();
        for (auto& k : kwIn) k.reset();
        for (auto& k : kwOut) k.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const DynekiParams& p) {
        ch = std::min(ch, kMaxCh);
        const bool msOn = p.ms == 1 && ch == 2;
        const bool punch = p.voice == 1;
        const bool autoOn = p.autoGain >= 0.5f;
        const float satA = clampf(p.sat, 0, 10) * 0.1f;
        const float compA = clampf(p.comp, 0, 10) * 0.06f;   // a gomb teljes útja = a korábbi 0…60 % (felette már túl sok volt)
        const float an = clampf(p.analog, 0, 100) * 0.01f;
        const float mixT = clampf(p.mix, 0, 100) * 0.01f;
        const float outT = clampf(p.outputDb, -12, 12);

        // SAT: VOICE szerint — BRIT: vas (több páros felharmonikus), PUNCH: szalag/op-amp (feszesebb, páratlan)
        satStage.set(0, punch ? 0.07f : 0.24f, 4.0f);
        satStage.headroom = dbToGain(10.0f - (punch ? 22.0f : 20.0f) * satA);   // a −12 dBFS-es munkaszinthez: +10 dB (tiszta) … −10/−12 dB (dús)
        // ANALOG szín: BRIT = trafó, PUNCH = op-amp
        opamp.set(an * 0.45f, 0.04f, 16.0f);
        // DYN: egy gomb → küszöb és arány együtt (0 = ki)
        const float thrT = -8.0f - 22.0f * compA, slopeT = 1.0f - 1.0f / (1.5f + 4.0f * compA);

        if (!primed) {
            sOut = outT; sMix = mixT; sThr = thrT; sSlope = slopeT;
            for (int s = 0; s < 2; ++s) for (int k = 0; k < 5; ++k) sG[s][k] = gains(s == 0 ? p.a : p.b)[k];
            primed = true;
            dirty = true;
        }

        for (int i0 = 0; i0 < n; i0 += kSub) {
            const int m = std::min(kSub, n - i0);
            // ---- blokk-ütemű paraméter-simítás + EQ-frissítés ----
            sOut += (outT - sOut) * smoothK; sMix += (mixT - sMix) * smoothK;
            sThr += (thrT - sThr) * smoothK; sSlope += (slopeT - sSlope) * smoothK;
            bool changed = dirty;
            for (int s = 0; s < 2; ++s) {
                const auto tg = gains(s == 0 ? p.a : p.b);
                for (int k = 0; k < 5; ++k) {
                    const float d = tg[k] - sG[s][k];
                    if (std::fabs(d) > 1e-4f) { sG[s][k] = std::fabs(d) < 0.002f ? tg[k] : sG[s][k] + d * smoothK; changed = true; }
                }
            }
            const bool shapeChanged = !sameShape(p);
            if (changed || shapeChanged) { design(p); dirty = false; }

            const float outDb = sOut, mx = sMix, thr = sThr, slope = sSlope;
            for (int i = i0; i < i0 + m; ++i) {
                // ---- fel 2x ----
                float u[kMaxCh][2];
                for (int c = 0; c < ch; ++c) os[c].up(in[c][i], u[c][0], u[c][1]);
                float peak = 0;
                for (int ph = 0; ph < 2; ++ph) {
                    float x[kMaxCh];
                    for (int c = 0; c < ch; ++c) x[c] = u[c][ph];
                    // ---- EQ (M/S-ben a kódolt jelen) ----
                    if (msOn) { const float mm = 0.5f * (x[0] + x[1]), ss = 0.5f * (x[0] - x[1]); x[0] = mm; x[1] = ss; }
                    for (int c = 0; c < ch; ++c) {
                        float v = x[c];
                        for (auto& f : eq[c]) v = f.tick(v);
                        x[c] = v * (autoOn ? eqComp[c] : 1.0f);
                    }
                    if (msOn) { const float l = x[0] + x[1], r = x[0] - x[1]; x[0] = l; x[1] = r; }

                    // ---- SAT ----
                    // lassú (≈0,3 s) összekapcsolt szintkövető: a SAT mindig −12 dBFS RMS munkaszinten dolgozik,
                    // így halk és hangos sávon is ugyanannyit dúsít (a kimenet szintje nem változik)
                    if (satA > 0) {
                        float sp = 0;
                        for (int c = 0; c < ch; ++c) sp = std::max(sp, x[c] * x[c]);
                        satEnv += (sp - satEnv) * kSatEnv;
                        satK = 0.25f / std::sqrt(std::max(satEnv, 1e-6f));
                    }
                    float e[kMaxCh];
                    for (int c = 0; c < ch; ++c) {
                        e[c] = x[c];
                        if (satA > 0) {
                            const float lo = satLow[c].lowpass(x[c]) * 0.6f;           // a legmélyebb tartományt kíméli
                            // "dúsítás": az alsó közép (≈500 Hz) kiemelve hajtja a telítőt, utána pontosan visszavéve →
                            // a felharmonikusok főleg a testes középből születnek (vastagabb, melegebb, nem recsegős)
                            const float v = (x[c] - lo) * satK;                           // munkaszintre hozva (szintfüggetlen)
                            float y = satDe[c].tick(satStage.tick(satPre[c].tick(v))) / satK + lo;
                            const float hf = satHf[c].lowpass(y);                      // szalagszerű kerekítés felül
                            y += (hf - y) * satA * (punch ? 0.35f : 0.2f);
                            x[c] = y;
                        }
                    }

                    // ---- DYN (összekapcsolt sztereó, valódi RMS) ----
                    float pw = 0;
                    for (int c = 0; c < ch; ++c) pw = std::max(pw, x[c] * x[c]);
                    if (compA > 0) {
                        rms += (pw - rms) * kRms;
                        const float lvl = 10.0f * std::log10(std::max(rms * 2.0f, 1e-12f));
                        const float target = std::max(softKneeGR(lvl - thr, slope, 10.0f), -40.0f);
                        const float err = target - grDb;
                        if (err < 0) {
                            const float tau = clampf(0.004f * std::pow(10.0f / std::max(-err, 0.5f), 1.6f), 0.0005f, 0.040f);
                            grDb += err * (1.0f - std::exp(-1.0f / (tau * sr2)));
                        } else grDb += std::min(err, relPerSample);
                    } else {
                        rms = 0; grDb = std::min(0.0f, grDb + relPerSample);
                    }
                    gNow = dbToGain(grDb);

                    // ---- szín, keverés, kimenet ----
                    for (int c = 0; c < ch; ++c) {
                        float y = x[c] * gNow;
                        if (an > 0) y = punch ? opamp.tick(y) : iron[c].tick(y, 1.0f + 2.2f * an);
                        y = e[c] + (y - e[c]) * mx;
                        x[c] = y;
                    }
                    // a SAT/DYN kompenzáció hangerő-érzet szerint (K-súlyozott mono összeg) méri az energiát
                    float sIn = 0, sOut2 = 0;
                    for (int c = 0; c < ch; ++c) { sIn += e[c]; sOut2 += x[c]; }
                    const float kIn = kwIn[1].tick(kwIn[0].tick(sIn)), kOut = kwOut[1].tick(kwOut[0].tick(sOut2));
                    autoMk.trackEnergy(kIn * kIn, kOut * kOut);
                    const float g = dbToGain(autoOn ? autoMk.tickEnergyDb(true) : outDb + autoMk.tickEnergyDb(false));
                    for (int c = 0; c < ch; ++c) u[c][ph] = x[c] * g;
                }
                for (int c = 0; c < ch; ++c) {
                    const float y = dc[c].tick(os[c].down(u[c][0], u[c][1]));
                    out[c][i] = y;
                    peak = std::max(peak, std::fabs(y));
                }
                meter.tick(gNow, peak);
            }
        }
    }

    float gainReductionDb() const { return grDb; }
    float vuDb() const { return meter.db(); }
    /// az AUTO GAIN teljes kompenzációja (EQ + SAT/DYN), dB
    float autoMakeupDb() const { return eqCompDb + autoMk.value(); }
    float eqCompensationDb() const { return eqCompDb; }

    /// a beállított EQ (egy készlet) nagyság-válasza dB-ben f Hz-en (méréshez/kijelzéshez)
    float responseDb(int set, float f) const {
        double m = 1;
        const double om = 2 * M_PI * f / sr2;
        for (const auto& b : eq[set]) m *= b.mag2(om);
        return float(10 * std::log10(std::max(m, 1e-30)));
    }

private:
    float sr = 48000, sr2 = 96000, smoothK = 0.1f;
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    Biquad eq[kMaxCh][kBands];
    OnePole satLow[kMaxCh], satHf[kMaxCh];
    Biquad satPre[kMaxCh], satDe[kMaxCh];   // SAT: alsó-közép kiemelés / visszavétel
    Transformer iron[kMaxCh];
    TubeStage satStage, opamp;
    AutoMakeup autoMk;
    Biquad kwIn[2], kwOut[2];     // K-súlyozás az AUTO GAIN energia-méréséhez
    VUMeter meter;
    float sG[2][5] = {};
    float sOut = 0, sMix = 1, sThr = 0, sSlope = 0;
    float rms = 0, kRms = 0, grDb = 0, gNow = 1, relPerSample = 0;
    float satEnv = 0.0625f, satK = 1, kSatEnv = 0;   // SAT szintkövető (teljesítmény) és a munkaszint-szorzó
    float eqComp[kMaxCh] = {1, 1}, eqCompDb = 0;
    bool primed = false, dirty = true;
    // az utolsó tervezés "alakja" (a lépcsős értékek); ha változik, újratervezés
    int lastShape[16] = {-1};

    static std::array<float, 5> gains(const EqSet& s) { return {s.lowG, s.lmG, s.hmG, s.highG, s.airG}; }

    bool sameShape(const DynekiParams& p) {
        const int shape[16] = {p.voice, p.ms, p.lmQ, p.hmQ, p.a.hpf, p.a.lowF, p.a.lmF, p.a.hmF, p.a.highF, p.a.airF,
                               p.b.hpf, p.b.lowF, p.b.lmF, p.b.hmF, p.b.highF, p.b.airF};
        if (std::equal(shape, shape + 16, lastShape)) return true;
        std::copy(shape, shape + 16, lastShape);
        return false;
    }

    static int idx(int v, int n) { return std::clamp(v, 0, n - 1); }
    /// az illesztési pont: a sáv frekvenciája, de legfeljebb 15 kHz (a hallható tartományban legyen pontos)
    static double matchHz(double hz, double fs) { return std::min({hz, 0.42 * fs, 15000.0}); }

    /// egy EQ-készlet szűrőinek megtervezése
    void designSet(Biquad* f, const EqSet& s, const float* g, const DynekiParams& p) const {
        const bool punch = p.voice == 1;
        const double fs = sr2;
        // HPF: 18 dB/okt = elsőfokú + másodfokú (Q = 1) szakasz
        const float hp = kHpfHz[idx(s.hpf, 5)];
        if (hp > 0) {
            f[0].designHP1(hp, fs);
            f[1].designMatched(Analog2::highPass(hp, 1.0), fs, hp);
        } else { f[0].setIdentity(); f[1].setIdentity(); }
        // LOW polc: BRIT = púp/bemélyedés (Q 1.1), PUNCH = simább, feszesebb (Q 0.75)
        auto shelfOrFlat = [&](Biquad& b, bool low, double hz, float gDb, double q) {
            if (std::fabs(gDb) < 1e-3f) { b.setIdentity(); return; }
            b.designMatched(low ? Analog2::lowShelf(hz, gDb, q) : Analog2::highShelf(hz, gDb, q), fs, matchHz(hz, fs));
        };
        // a 15 és 35 Hz-es állás hallhatóra hangolva: nagyon széles polc 20 / 40 Hz-es sarokkal (a hatása felér a hallható mélyig)
        // és ×1,25 erősítés; a többi állás a VOICE szerinti klasszikus polc
        const int lf = idx(s.lowF, 5);
        const bool deep = lf <= 1;
        shelfOrFlat(f[2], true, lf == 0 ? 20.0 : lf == 1 ? 40.0 : kLowHz[lf], deep ? g[0] * 1.25f : g[0], deep ? 0.35 : (punch ? 0.75 : 1.1));
        // harangok: BRIT = állandó Q 0.9, PUNCH = arányos Q (0.5 … ~2.1 16 dB-nél); a fiókban felülírható
        auto bell = [&](Biquad& b, double hz, float gDb, int qIdx) {
            if (std::fabs(gDb) < 1e-3f) { b.setIdentity(); return; }
            double q = kQTable[idx(qIdx, 5)];
            if (q <= 0) q = punch ? 0.5 + 0.1 * std::fabs(gDb) : 0.9;
            b.designMatched(Analog2::bell(hz, gDb, q), fs, matchHz(hz, fs));
        };
        bell(f[3], kLmHz[idx(s.lmF, 5)], g[1], p.lmQ);
        bell(f[4], kHmHz[idx(s.hmF, 5)], g[2], p.hmQ);
        // HIGH polc: BRIT = selymes (Q 0.6), PUNCH = csillogóbb (Q 0.9)
        shelfOrFlat(f[5], false, kHighHz[idx(s.highF, 4)], g[3], punch ? 0.9 : 0.6);
        // AIR: nagyon széles harang (Q 0.45), csak emel
        const float air = std::max(0.0f, g[4]);
        if (air < 1e-3f) f[6].setIdentity();
        else {   // a 40 kHz-es állás hallhatóra hangolva: szélesebb harang (lejön 8–16 kHz-ig) és ×1,3 erősítés
            const bool top = idx(s.airF, 5) == 4;
            f[6].designMatched(Analog2::bell(kAirHz[idx(s.airF, 5)], top ? air * 1.3f : air, top ? 0.3 : 0.45), fs,
                               matchHz(kAirHz[idx(s.airF, 5)], fs));
        }
    }

    /// K-súlyozás (BS.1770 előszűrő): +4 dB-es magas polc ~1,7 kHz-től és 38 Hz-es felüláteresztő
    static double kWeight(double hz) {
        static const Analog2 shelf = Analog2::highShelf(1681.0, 4.0, 0.71), hp = Analog2::highPass(38.0, 0.5);
        const double w = 2 * M_PI * hz;
        return shelf.mag2(w) * hp.mag2(w);
    }

    /// "KEEP GAIN": a görbe zenei spektrummal súlyozott átlagos erősítésének ellentettje (dB)
    double compensationDb(const Biquad* f, const Biquad* side = nullptr) const {
        constexpr double kSideWeight = 0.1;    // tipikus zenén a SIDE energiája ≈ −10 dB a MID-hez képest
        double num = 0, den = 0;
        for (int k = 0; k < 48; ++k) {
            const double hz = 25.0 * std::pow(20000.0 / 25.0, k / 47.0);   // logaritmikus rács: oktávonként azonos súly (rózsaszín)
            const double w = kWeight(hz);                                    // hangerő-érzet: K-súlyozás (ITU-R BS.1770)
            double m = 1, ms = 1;
            for (int b = 0; b < kBands; ++b) m *= f[b].mag2(2 * M_PI * hz / sr2);
            if (side) {
                for (int b = 0; b < kBands; ++b) ms *= side[b].mag2(2 * M_PI * hz / sr2);
                m = (m + kSideWeight * ms) / (1 + kSideWeight);
            }
            num += w * m; den += w;
        }
        return -10.0 * std::log10(std::max(num / den, 1e-12));
    }

    void design(const DynekiParams& p) {
        const bool msOn = p.ms == 1;
        designSet(eq[0], p.a, sG[0], p);
        designSet(eq[1], msOn ? p.b : p.a, msOn ? sG[1] : sG[0], p);
        // M/S-ben KÖZÖS kompenzáció a teljes jelre (a SIDE kis súllyal, mint a hangerő-érzetben):
        // így a SIDE emelése tényleg szélesít, nem húzza vissza a KEEP GAIN
        const double c = compensationDb(eq[0], msOn ? eq[1] : nullptr);
        eqComp[0] = eqComp[1] = float(std::pow(10.0, c / 20));
        eqCompDb = float(c);
    }
};

} // namespace eki
