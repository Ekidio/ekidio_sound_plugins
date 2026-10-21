// MastequiDSP.h — MASTEQUI: háromsávos mastering EQ két polccal, AU-független.
//
// A referencia (mérések + saját impulzusválasz-mérések, 2026-10-22) alapján:
//   · három harang (LOW 11–570 Hz, MID 125 Hz–6,2 kHz, HIGH 3,4–25 kHz, 24-24 lépés) + mély és magas POLC; RANGE 6 / 9 / 12 dB;
//   · a harang felépítése: egy állandó Q-jú sáváteresztő HOZZÁADVA (emelés) vagy visszacsatolásban LEVONVA (vágás) — ettől
//     erősebb állásnál szélesebb, a vágás keskenyebb (külön Q-tábla, a széles vágás mélyebb is); a sáváteresztőnek mindkét
//     oldalon lapos "farka" van (alul- és felüláteresztő rész, ≈ 0,054 / Q) — ettől a harang távolabb sem tér vissza 0-ra;
//   · a polcok elsőrendűek: mély 41,4 Hz (LOW SHIFT: 83 Hz), magas 11,7 kHz; a mély polc picit "átszivárog" a teljes sávra
//     (1,5 / 3 %) — ezért mozdul kicsit az egész szint is a mély polccal, mint a referenciánál.
//   Mérve a modell: a harangok 0,01–0,07 dB-en (a 25 kHz-es szélső állás 0,13 dB), a polcok 0,03–0,25 dB-en belül.
// Pontos ("illesztett") digitális szűrők az analóg prototípusból, 2× túlmintavételezve (lineáris fázisú FIR, 96 csapó/fázis, késés 96 minta = 2 ms, 20 kHz-ig lapos):
// így a 10 kHz feletti harangok és a magas polc is az analóg görbét követik (a referencia is túlmintavételez).
// Jelút: LOW SHELF → LOW → MID → HIGH → HIGH SHELF → OUTPUT / AUTO GAIN (alapban KI)
#pragma once
#include "EKIAnalog.h"
#include "EKIFilters.h"
#include "EKIOversample.h"
#include "EKIMasterCurves.h"

namespace eki {

inline constexpr float kLowHz[24]  = {11, 13, 15, 18, 20, 25, 30, 36, 42, 50, 60, 71, 85, 100, 120, 140, 170, 200, 240, 280, 340, 400, 450, 570};
inline constexpr float kMidHz[24]  = {125, 150, 180, 210, 250, 300, 350, 420, 500, 600, 700, 840, 1000, 1200, 1400, 1700, 2000, 2400, 2800, 3400, 4000, 4500, 5600, 6200};
inline constexpr float kHighHz[24] = {3400, 3700, 4000, 4400, 4800, 5200, 5700, 6200, 6800, 7400, 8100, 8800, 9500, 10000, 11000, 12000, 13000, 14000, 16000, 17000, 19000, 21000, 23000, 25000};
inline constexpr float kRangeDb[3] = {6, 9, 12};
struct MastequiParams {
    float lowShelf = 0, highShelf = 0;               // −12…+12 lépés (a RANGE szerint skálázva)
    float gain[3] = {0, 0, 0};                       // LOW, MID, HIGH: −12…+12 lépés
    int freq[3] = {12, 12, 12};                      // 85 Hz, 1 kHz, 9,5 kHz
    int q[3] = {1, 1, 1};
    int range = 2;                                   // 12 dB
    float lowShift = 0;
    float outDb = 0;
    float autoGain = 0;                              // alapban KI (a user döntése)
};

class MastequiDSP {
public:
    static constexpr int kMaxCh = 2, kSub = 32, kLatency = FirOversamplerHQ::K;

    void prepare(float sampleRate) {
        sr = sampleRate; srOS = 2 * sampleRate;
        for (auto& o : os) o.design(2);
        for (int c = 0; c < kMaxCh; ++c) {
            kwIn[c][0].designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr, 1681.0);
            kwIn[c][1].designMatched(Analog2::highPass(38.0, 0.5), sr, 38.0);
            kwOut[c][0].designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr, 1681.0);
            kwOut[c][1].designMatched(Analog2::highPass(38.0, 0.5), sr, 38.0);
        }
        smoothK = 1.0f - onePoleCoef(0.03f, sr / kSub);
        autoMk.prepare(sr);
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            os[c].reset();
            for (auto& b : bell[c]) b.reset();
            loSh[c].reset(); hiSh[c].reset();
            for (auto& k : kwIn[c]) k.reset();
            for (auto& k : kwOut[c]) k.reset();
        }
        autoMk.reset(); primed = false;
        for (auto& k : key) k = -1e9f;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const MastequiParams& p) {
        ch = std::min(ch, kMaxCh);
        const float rng = kRangeDb[std::clamp(p.range, 0, 2)] / 12.0f;
        float tg[5] = {clampf(p.lowShelf, -12, 12) * rng, clampf(p.gain[0], -12, 12) * rng, clampf(p.gain[1], -12, 12) * rng,
                       clampf(p.gain[2], -12, 12) * rng, clampf(p.highShelf, -12, 12) * rng};
        const float outT = clampf(p.outDb, -12, 12);
        const bool autoOn = p.autoGain >= 0.5f, shift = p.lowShift >= 0.5f;
        if (!primed) { for (int k = 0; k < 5; ++k) sG[k] = tg[k]; sOut = outT; primed = true; }

        for (int i0 = 0; i0 < n; i0 += kSub) {
            const int m = std::min(kSub, n - i0);
            for (int k = 0; k < 5; ++k) sG[k] += (tg[k] - sG[k]) * smoothK;
            sOut += (outT - sOut) * smoothK;
            design(p, shift);
            for (int i = i0; i < i0 + m; ++i) {
                float pIn = 0, pOut = 0, y[kMaxCh];
                for (int c = 0; c < ch; ++c) {
                    const float x = in[c][i];
                    float u[2];
                    os[c].up(x, u);
                    for (float& w : u) {
                        w = loSh[c].tick(w);
                        for (auto& b : bell[c]) w = b.tick(w);
                        w = hiSh[c].tick(w);
                    }
                    const float v = os[c].down(u);
                    const float xd = dly[c][dpos]; dly[c][dpos] = x;   // a bemenet a kimenettel egy időben (AUTO GAIN-hez)
                    const float ki = kwIn[c][1].tick(kwIn[c][0].tick(xd)), ko = kwOut[c][1].tick(kwOut[c][0].tick(v));
                    pIn += ki * ki; pOut += ko * ko;
                    y[c] = v;
                }
                dpos = (dpos + 1) % kLatency;
                autoMk.trackEnergy(pIn, pOut);
                const float g = dbToGain(autoOn ? autoMk.tickEnergyDb(true) : sOut + autoMk.tickEnergyDb(false));
                for (int c = 0; c < ch; ++c) out[c][i] = y[c] * g;
            }
        }
    }

    float autoMakeupDb() const { return autoMk.value(); }
    double latencySeconds() const { return double(kLatency) / sr; }

private:
    void design(const MastequiParams& p, bool shift) {
        const int fi[3] = {std::clamp(p.freq[0], 0, 23), std::clamp(p.freq[1], 0, 23), std::clamp(p.freq[2], 0, 23)};
        const int qi[3] = {std::clamp(p.q[0], 0, 4), std::clamp(p.q[1], 0, 4), std::clamp(p.q[2], 0, 4)};
        // a 25 kHz-es állás a Nyquist fölé esne: a középfrekvencia legfeljebb 0,477 × a mintavétel (48 kHz-en ≈ 22,9 kHz, mint a referenciánál)
        const float hz[3] = {kLowHz[fi[0]] * kFreqTrim, kMidHz[fi[1]] * kFreqTrim, std::min(kHighHz[fi[2]] * kFreqTrim, 0.477f * sr)};
        // csak akkor tervez újra, ha valami érdemben változott
        const float nk[8] = {sG[0], sG[1], sG[2], sG[3], sG[4], float(fi[0] * 100 + fi[1] * 10000 + fi[2]), float(qi[0] * 100 + qi[1] * 10 + qi[2]), shift ? 1.0f : 0.0f};
        bool same = true;
        for (int k = 0; k < 8; ++k) if (std::fabs(nk[k] - key[k]) > 0.002f) same = false;
        if (same) return;
        for (int k = 0; k < 8; ++k) key[k] = nk[k];
        Biquad b[3];
        for (int band = 0; band < 3; ++band) {
            const float g0 = sG[band + 1];
            const float q = g0 >= 0 ? kBoostQ[qi[band]] : kCutQ[qi[band]];
            const float gDb = g0 * (g0 >= 0 ? kBoostG[qi[band]] : kCutG[qi[band]]);
            const float tail = std::min(0.07f, 0.054f / q);
            if (std::fabs(gDb) < 1e-4f) { b[band].setIdentity(); continue; }
            // a LOW sávnak nincs alsó (aluláteresztő) farka — a mérés szerint 10 Hz-en visszatér 0-ra
            b[band].designMatched(bellProto(hz[band], gDb, q, band == 0 ? 0.0f : tail, tail), srOS, hz[band]);
        }
        Shelf1 lo, hi;
        lo.design(true, shift ? 83.3 : 41.4, sG[0], shift ? 0.03 : 0.015, srOS);
        hi.design(false, 11624.0, sG[4], 0.0, srOS);
        for (int c = 0; c < kMaxCh; ++c) {
            for (int band = 0; band < 3; ++band) { const float s1 = bell[c][band].z1, s2 = bell[c][band].z2; bell[c][band] = b[band]; bell[c][band].z1 = s1; bell[c][band].z2 = s2; }
            const float zl = loSh[c].z1, zh = hiSh[c].z1;
            loSh[c] = lo; loSh[c].z1 = zl; hiSh[c] = hi; hiSh[c].z1 = zh;
        }
    }

    float sr = 48000, srOS = 96000, smoothK = 0.1f;
    FirOversamplerHQ os[kMaxCh];
    float dly[kMaxCh][kLatency] = {};
    int dpos = 0;
    Biquad bell[kMaxCh][3];
    Shelf1 loSh[kMaxCh], hiSh[kMaxCh];
    Biquad kwIn[kMaxCh][2], kwOut[kMaxCh][2];
    AutoMakeup autoMk;
    float sG[5] = {0, 0, 0, 0, 0}, sOut = 0, key[8];
    bool primed = false;
};

} // namespace eki
