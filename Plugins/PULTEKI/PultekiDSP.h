// PultekiDSP.h — PULTEKI: a klasszikus passzív csöves EQ-sor három modulja, extra tiszta motorral, AU-független.
//
// Jelút (2x túlmintavételezve, lineáris fázisú FIR):  PROGRAM EQ → MID-RANGE EQ → FILTER → ANALOG (alapban 0) → OUTPUT / AUTO GAIN
//   PROGRAM EQ (referencia-mérésre illesztve, 2026-10-23; ±0,15 dB): a gombok 0…10-es állása táblázatból (0 / 2,5 / 5 / 7,5 / 10)
//     · LOW BOOST: mély polc, a mélysége ÉS a sarokpontja is a gombbal változik (feltekerve +18,75 dB, sarok = 2,15 × a választott
//       frekvencia, félállásban +6 dB és 7,9 ×) — a LOW ATTEN polca kicsit feljebb fordul (−20 dB, 3,2 ×): együtt a jól ismert
//       "emelés + vágás" púp és bemélyedés
//     · HIGH BOOST: széles harang a választott frekvencia 1,06 ×-osán (feltekerve +12 dB), BANDWIDTH: Q 2,5 (SHARP) … 0,3 (BROAD)
//     · HIGH ATTEN: magas polc az ATTEN SEL frekvencia 0,71 ×-osán (feltekerve −22,9 dB), Q 0,4
//     A többi frekvencia-állás ugyanezekkel az arányokkal skálázódik.
//   MID-RANGE EQ: két kiemelő és egy vágó harang (±10 dB, Q 1), a mért tekerési jelleggörbével
//   FILTER: 18 dB/okt alul- és felülvágó (első + másodfokú, Q 1)
//   Minden modul saját IN kapcsolóval (simán áttűnik); ha semmi nem dolgozik, a késleltetett eredeti jel megy át (bitre átlátszó).
#pragma once
#include "EKIAnalog.h"
#include "EKIFilters.h"
#include "EKIOversample.h"
#include <cstring>

namespace eki {

inline constexpr float kPtLowHz[4] = {20, 30, 60, 100};
inline constexpr float kPtHighHz[7] = {3000, 4000, 5000, 8000, 10000, 12000, 16000};
inline constexpr float kPtAttenHz[3] = {5000, 10000, 20000};
inline constexpr float kPtMidLowHz[5] = {200, 300, 500, 700, 1000};
inline constexpr float kPtDipHz[11] = {200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000};
inline constexpr float kPtMidHighHz[5] = {1500, 2000, 3000, 4000, 5000};
inline constexpr float kPtLowCutHz[11] = {0, 50, 80, 100, 150, 250, 500, 750, 1000, 1500, 2000};      // 0 = OFF
inline constexpr float kPtHighCutHz[11] = {1500, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 12000, 15000, 0};   // 0 = OFF

struct PultekiParams {
    float lowBoost = 0, lowAtten = 0, highBoost = 0, highAtten = 0, bandwidth = 5;   // 0…10
    int lowFreq = 1, highFreq = 4, attenSel = 1;
    float eqpIn = 1;
    float midLowG = 0, dipG = 0, midHighG = 0;                                      // 0…10
    int midLowF = 2, dipF = 4, midHighF = 2;
    float meqIn = 1;
    int lowCut = 0, highCut = 10;
    float hlfIn = 1;
    float outDb = 0, autoGain = 0, analog = 0;
};

class PultekiDSP {
public:
    static constexpr int kMaxCh = 2, kSub = 32, kLatency = FirOversamplerHQ::K;

    void prepare(float sampleRate) {
        sr = sampleRate; srOS = 2 * sampleRate;
        for (auto& o : os) o.design(2);
        for (int c = 0; c < kMaxCh; ++c) {
            for (Biquad* k : {&kwIn[c][0], &kwOut[c][0]}) k->designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr, 1681.0);
            for (Biquad* k : {&kwIn[c][1], &kwOut[c][1]}) k->designMatched(Analog2::highPass(38.0, 0.5), sr, 38.0);
        }
        smoothK = 1.0f - onePoleCoef(0.03f, sr / kSub);
        autoMk.prepare(sr);
        needle.set(sr);
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            os[c].reset();
            for (auto& b : eq[c]) b.reset();
            for (auto& b : cut[c]) b.reset();
            for (auto& k : kwIn[c]) k.reset();
            for (auto& k : kwOut[c]) k.reset();
        }
        std::memset(dly, 0, sizeof dly); dpos = 0;
        autoMk.reset(); needle.reset(); primed = false;
        for (auto& k : key) k = -1e9f;
    }

    double latencySeconds() const { return double(kLatency) / sr; }
    float autoMakeupDb() const { return autoMk.value(); }
    /// kimeneti VU (dB): 0 VU = −18 dBFS RMS (szinusz: az egyenirányított átlag 0,9 × RMS) — a tipikus mixszint
    float vuDb() const { return clampf(gainToDb(std::max(needle.pos, 1e-6f) / 0.9f) + 18.0f, -20.0f, 3.0f); }

    void process(const float* const* in, float* const* out, int ch, int n, const PultekiParams& p) {
        ch = std::min(ch, kMaxCh);
        // célértékek: a modul IN kapcsolója a gombokat simán 0-ra húzza (a görbe kisimul, nincs kattanás)
        const float eqp = p.eqpIn >= 0.5f ? 1.0f : 0.0f, meq = p.meqIn >= 0.5f ? 1.0f : 0.0f, hlf = p.hlfIn >= 0.5f ? 1.0f : 0.0f;
        const float tk[8] = {clampf(p.lowBoost, 0, 10) * eqp, clampf(p.lowAtten, 0, 10) * eqp, clampf(p.highBoost, 0, 10) * eqp,
                             clampf(p.highAtten, 0, 10) * eqp, clampf(p.midLowG, 0, 10) * meq, clampf(p.dipG, 0, 10) * meq,
                             clampf(p.midHighG, 0, 10) * meq, clampf(p.bandwidth, 0, 10)};
        const bool lowCutOn = hlf > 0.5f && p.lowCut > 0, highCutOn = hlf > 0.5f && p.highCut < 10;
        const float outT = clampf(p.outDb, -12, 12), anT = clampf(p.analog, 0, 100) * 0.01f;
        const bool autoOn = p.autoGain >= 0.5f;
        if (!primed) {
            for (int k = 0; k < 8; ++k) sk[k] = tk[k];
            sOut = outT; sAn = anT; sCut = (lowCutOn || highCutOn) ? 1.0f : 0.0f; primed = true;
        }
        // van-e bármi, ami a hangot alakítja? ha nincs, a késleltetett eredeti megy át (bitre átlátszó)
        bool active = lowCutOn || highCutOn || anT > 0;
        for (int k = 0; k < 7; ++k) if (tk[k] > 1e-4f) active = true;

        for (int i0 = 0; i0 < n; i0 += kSub) {
            const int m = std::min(kSub, n - i0);
            for (int k = 0; k < 8; ++k) sk[k] += (tk[k] - sk[k]) * smoothK;
            sOut += (outT - sOut) * smoothK; sAn += (anT - sAn) * smoothK;
            sCut += (((lowCutOn || highCutOn) ? 1.0f : 0.0f) - sCut) * smoothK;
            sWet += ((active ? 1.0f : 0.0f) - sWet) * smoothK;
            if (!active && sWet < 1e-4f) sWet = 0;
            design(p, lowCutOn, highCutOn);
            tube.set(sAn * 0.5f, 0.15f, 16.0f);
            for (int i = i0; i < i0 + m; ++i) {
                float pIn = 0, pOut = 0, y[kMaxCh];
                for (int c = 0; c < ch; ++c) {
                    const float x = in[c][i];
                    const float xd = dly[c][dpos]; dly[c][dpos] = x;   // az eredeti, a túlmintavételezés késésével
                    // a feldolgozott ág mindig fut (a szűrők és a túlmintavételező állapota friss marad), de ha semmi nem
                    // dolgozik, a késleltetett eredeti megy ki — bitre átlátszóan
                    float u[2];
                    os[c].up(x, u);
                    for (float& w : u) {
                        for (auto& b : eq[c]) w = b.tick(w);
                        float f = w;
                        for (auto& b : cut[c]) f = b.tick(f);
                        w += (f - w) * sCut;                            // a FILTER simán áttűnik
                        if (sAn > 1e-4f) w = tube.tick(w);               // ANALOG: lágy, páros felhangú csőszín (alapban 0)
                    }
                    const float wet = os[c].down(u);
                    const float v = sWet > 0 ? xd + (wet - xd) * sWet : xd;
                    const float ki = kwIn[c][1].tick(kwIn[c][0].tick(xd)), ko = kwOut[c][1].tick(kwOut[c][0].tick(v));
                    pIn += ki * ki; pOut += ko * ko;
                    y[c] = v;
                }
                dpos = (dpos + 1) % kLatency;
                autoMk.trackEnergy(pIn, pOut);
                const float g = dbToGain(autoOn ? autoMk.tickEnergyDb(true) : sOut + autoMk.tickEnergyDb(false));
                float av = 0;
                for (int c = 0; c < ch; ++c) { out[c][i] = y[c] * g; av += std::fabs(out[c][i]); }
                needle.tick(av / float(ch));   // kimeneti VU: egyenirányított átlag, VU-ballisztika
            }
        }
    }

private:
    /// a mért gombtáblázat (0 / 2,5 / 5 / 7,5 / 10 állás) lineáris interpolációja
    static float table(const float (&t)[5], float k) {
        const float x = clampf(k, 0, 10) / 2.5f;
        const int i = std::min(3, int(x));
        return t[i] + (t[i + 1] - t[i]) * (x - float(i));
    }

    void design(const PultekiParams& p, bool lowCutOn, bool highCutOn) {
        const int lf = std::clamp(p.lowFreq, 0, 3), hf = std::clamp(p.highFreq, 0, 6), as = std::clamp(p.attenSel, 0, 2);
        const int ml = std::clamp(p.midLowF, 0, 4), dp = std::clamp(p.dipF, 0, 10), mh = std::clamp(p.midHighF, 0, 4);
        const int lc = std::clamp(p.lowCut, 0, 10), hc = std::clamp(p.highCut, 0, 10);
        const float nk[10] = {sk[0], sk[1], sk[2], sk[3], sk[4], sk[5], sk[6], sk[7],
                              float(lf + 4 * hf + 28 * as), float(ml + 5 * dp + 55 * mh + 275 * (lc + 11 * hc + 121 * (lowCutOn + 2 * highCutOn)))};
        bool same = true;
        for (int k = 0; k < 10; ++k) if (std::fabs(nk[k] - key[k]) > 0.002f) same = false;
        if (same) return;
        for (int k = 0; k < 10; ++k) key[k] = nk[k];

        const double fs = srOS;
        auto match = [&](double hz) { return std::min({hz, 0.42 * fs, 15000.0}); };
        Biquad b[7];
        // ---- PROGRAM EQ (mért táblázatok; a sarokpont a választott frekvencia többszöröse, log10-ben) ----
        static const float lbG[5] = {0, 0.75f, 6.0f, 12.0f, 18.75f}, lbR[5] = {1.733f, 1.733f, 0.90f, 0.60f, 0.333f}, lbQ[5] = {0.6f, 0.6f, 0.5f, 0.5f, 0.4f};
        static const float laG[5] = {0, -0.75f, -5.25f, -12.0f, -20.0f}, laR[5] = {1.80f, 1.80f, 1.167f, 0.80f, 0.50f}, laQ[5] = {0.4f, 0.4f, 0.5f, 0.5f, 0.4f};
        static const float hbG[5] = {0, 1.0f, 3.25f, 6.62f, 12.0f};
        static const float haG[5] = {0, -1.88f, -5.75f, -12.0f, -22.88f};
        static const float pkN[5] = {0, 0.04f, 0.32f, 0.64f, 1.0f};   // a MID-RANGE EQ tekerési jelleggörbéje (a mért LOW BOOST-ból)
        auto shelf = [&](Biquad& q, bool low, double hz, float gDb, double qq) {
            if (std::fabs(gDb) < 1e-3f) { q.setIdentity(); return; }
            q.designMatched(low ? Analog2::lowShelf(hz, gDb, qq) : Analog2::highShelf(hz, gDb, qq), fs, match(hz));
        };
        auto bell = [&](Biquad& q, double hz, float gDb, double qq) {
            if (std::fabs(gDb) < 1e-3f) { q.setIdentity(); return; }
            q.designMatched(Analog2::bell(hz, gDb, qq), fs, match(hz));
        };
        const double fLow = kPtLowHz[lf];
        shelf(b[0], true, fLow * std::pow(10.0, table(lbR, sk[0])), table(lbG, sk[0]), table(lbQ, sk[0]));
        shelf(b[1], true, fLow * std::pow(10.0, table(laR, sk[1])), table(laG, sk[1]), table(laQ, sk[1]));
        const double bwQ = 2.5 * std::pow(0.3 / 2.5, double(sk[7]) / 10.0);                       // SHARP 2,5 … BROAD 0,3
        bell(b[2], std::min(kPtHighHz[hf] * 1.059, 0.45 * fs), table(hbG, sk[2]), bwQ);
        shelf(b[3], false, std::min(kPtAttenHz[as] * 0.708, 0.42 * fs), table(haG, sk[3]), 0.40);
        // ---- MID-RANGE EQ ----
        bell(b[4], kPtMidLowHz[ml], 10.0f * table(pkN, sk[4]), 1.0);
        bell(b[5], kPtDipHz[dp], -10.0f * table(pkN, sk[5]), 1.0);
        bell(b[6], kPtMidHighHz[mh], 10.0f * table(pkN, sk[6]), 1.0);
        // ---- FILTER: 18 dB/okt ----
        Biquad c[4];
        if (lowCutOn) { const double f = kPtLowCutHz[lc]; c[0].designHP1(f, fs); c[1].designMatched(Analog2::highPass(f, 1.0), fs, f); }
        else { c[0].setIdentity(); c[1].setIdentity(); }
        if (highCutOn) { const double f = kPtHighCutHz[hc]; c[2].designLP1(f, fs); c[3].designMatched(Analog2::lowPass(f, 1.0), fs, match(f)); }
        else { c[2].setIdentity(); c[3].setIdentity(); }
        for (int ch = 0; ch < kMaxCh; ++ch) {
            for (int k = 0; k < 7; ++k) { const double z1 = eq[ch][k].z1, z2 = eq[ch][k].z2; eq[ch][k] = b[k]; eq[ch][k].z1 = z1; eq[ch][k].z2 = z2; }
            for (int k = 0; k < 4; ++k) { const double z1 = cut[ch][k].z1, z2 = cut[ch][k].z2; cut[ch][k] = c[k]; cut[ch][k].z1 = z1; cut[ch][k].z2 = z2; }
        }
    }

    float sr = 48000, srOS = 96000, smoothK = 0.1f;
    FirOversamplerHQ os[kMaxCh];
    float dly[kMaxCh][kLatency] = {};
    int dpos = 0;
    Biquad eq[kMaxCh][7], cut[kMaxCh][4];
    Biquad kwIn[kMaxCh][2], kwOut[kMaxCh][2];
    TubeStage tube;
    AutoMakeup autoMk;
    VUNeedle needle;
    float sk[8] = {}, sOut = 0, sAn = 0, sCut = 0, sWet = 0, key[10];
    bool primed = false;
};

} // namespace eki
