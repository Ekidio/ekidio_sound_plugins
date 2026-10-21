// EkipperDSP.h — EKIPPER: SOFT CLIPPER, AU-független.
//
// A user döntése (2026-10-21): csak a WARM lágy vágás, a motorháztető alatt SEHOL kemény vágás; plafon fix 0 dBFS;
// nincs CEILING, MIX, AUTO GAIN, karakterválasztó. Előlap: DRIVE, OUTPUT; fiók: OVERSAMPLING.
// A görbe a referencia-mérésből (2026-10-21): DRIVE előerősítés egy lágy vágóba, a lágy szakasz −0,45 × DRIVE^1,2 dB-nél
//   kezdődik (de legalább −1,5 dB-nél, hogy DRIVE 0-n is lágy maradjon), felette tanh-ív, amely a 0 dBFS-t csak megközelíti.
//   WARM: a negatív félhullám lágyabb → páros felhang (csöves melegség); a keletkező DC-t egy 5 Hz-es szűrő viszi el.
// Túlmintavételezés: lineáris fázisú FIR (2× / 4× / 8×, 32 csapó fázisonként, késés 32 minta) — a fázis érintetlen, a teljes
//   meghajtott jel átmegy a vágón (a 22 kHz feletti sáv sem kerülheti meg, így a tranziensek sem lőnek túl).
// Jelút: DRIVE → túlmintavételezés → lágy vágó → vissza → DC-szűrő → enyhe lágy utófokozat (a visszaszűrés túllövéseire) → OUTPUT
#pragma once
#include <vector>
#include "EKIAnalog.h"
#include "EKIFilters.h"
#include "EKIOversample.h"

namespace eki {

struct EkipperParams {
    float driveDb = 0;        // 0…20
    float outDb = 0;          // −12…+12
    int oversample = 1;       // 0 = 2×, 1 = 4×, 2 = 8×
};

class EkipperDSP {
public:
    static constexpr int kMaxCh = 2, kSub = 32, kLatency = FirOversampler::K;
    static constexpr float kPostKnee = 0.8414f;   // −1,5 dB

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            dc[c].set(sr, 5.0f);
        }
        smoothK = 1.0f - onePoleCoef(0.03f, sr / kSub);
        grRel = onePoleCoef(0.3f, sr);
        holdDecay = onePoleCoef(1.5f, sr);
        lastFactor = -1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            os[c].reset(); dc[c].reset();
            peak[c] = 0; hold[c] = 0;
        }
        grDb = 0; primed = false;
    }

    double latencySeconds() const { return double(kLatency) / sr; }

    void process(const float* const* in, float* const* out, int ch, int n, const EkipperParams& p) {
        ch = std::min(ch, kMaxCh);
        const int factor = 2 << std::clamp(p.oversample, 0, 2);
        if (factor != lastFactor) { for (auto& o : os) o.design(factor); lastFactor = factor; }
        const int L = factor;
        const float drvT = clampf(p.driveDb, 0, 20), outT = clampf(p.outDb, -12, 12);
        if (!primed) { sDrv = drvT; sOut = outT; primed = true; }

        for (int i0 = 0; i0 < n; i0 += kSub) {
            const int m = std::min(kSub, n - i0);
            sDrv += (drvT - sDrv) * smoothK; sOut += (outT - sOut) * smoothK;
            const float g = dbToGain(sDrv), gOut = dbToGain(sOut);
            // a lágy szakasz kezdete (0 dBFS-hez mérten), a mérésre illesztve, de legalább −1,5 dB (soha nem kemény)
            const float t = dbToGain(-std::max(1.5f, 0.45f * std::pow(sDrv, 1.2f)));
            const float tNeg = std::pow(t, 1.6f) * 0.92f;   // WARM: a negatív fél lágyabb
            for (int i = i0; i < i0 + m; ++i) {
                float preMax = 0, postMax = 0;
                for (int k = 0; k < ch; ++k) {
                    const float x = in[k][i];
                    float u[8];
                    os[k].up(x * g, u);
                    for (int j = 0; j < L; ++j) {
                        const float a = u[j];
                        const float sc = shape(a, a >= 0 ? t : tNeg);
                        preMax = std::max(preMax, std::fabs(a));
                        postMax = std::max(postMax, std::fabs(sc));
                        u[j] = sc;
                    }
                    // a teljes (meghajtott, vágott) jel megy vissza a lineáris fázisú szűrőn: semmi nem kerüli meg a vágót
                    // a visszaszűrés a nagyon levágott éleken felcseng (Gibbs) → egy második, enyhe LÁGY fokozat fogja meg
                    // (ugyanaz a tanh-ív, −1,5 dB-es térd; a 0 dBFS-t csak megközelíti, kemény vágás így sehol sincs)
                    const float o = shape(dc[k].tick(os[k].down(u)), kPostKnee) * gOut;
                    out[k][i] = o;
                    const float ao = std::fabs(o);
                    peak[k] = std::max(peak[k], ao);
                    hold[k] = ao > hold[k] ? ao : hold[k] * holdDecay;
                }
                const float cut = preMax > 1e-6f ? std::min(0.0f, gainToDb(std::max(postMax, 1e-9f) / preMax)) : 0.0f;
                grDb = cut < grDb ? cut : cut + (grDb - cut) * grRel;
            }
        }
        if (ch == 1) { peak[1] = peak[0]; hold[1] = hold[0]; }
    }

    float clipDb() const { return grDb; }
    float takePeakDb(int c) { const float v = gainToDb(std::max(peak[c], 1e-6f)); peak[c] = 0; return v; }
    float holdDb(int c) const { return gainToDb(std::max(hold[c], 1e-6f)); }

    /// a lágy vágógörbe (0 dBFS-re normálva): t alatt egyenes, felette tanh-ív, amely az 1-et csak megközelíti
    static inline float shape(float u, float t) {
        const float a = std::fabs(u);
        if (a <= t) return u;
        const float w = 1.0f - t;
        const float y = w < 1e-4f ? 1.0f : t + w * std::tanh((a - t) / w);
        return u < 0 ? -std::min(y, 1.0f) : std::min(y, 1.0f);
    }

private:
    float sr = 48000, smoothK = 0.1f, grRel = 0.99f, holdDecay = 0.999f;
    FirOversampler os[kMaxCh];
    int lastFactor = -1;
    DCBlocker dc[kMaxCh];
    float peak[kMaxCh] = {0, 0}, hold[kMaxCh] = {0, 0}, grDb = 0;
    float sDrv = 0, sOut = 0;
    bool primed = false;
};

} // namespace eki
