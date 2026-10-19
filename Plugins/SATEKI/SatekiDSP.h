// SatekiDSP.h — SATEKI: MULTI TUBE SATURATION (3. koncepció: lágy "levegő"), AU-független.
//
// A user célja egy régi, A-típusú stúdió-zajcsökkentő kódoló "air trükkje" (dekódolás nélkül): a magas sáv HALK részeit
// erősen (≈ +10 dB) emeli, a hangosakat alig — selymes, levegős, soha nem szúr; mindez lágyan ("minden legyen lágy").
// Három egység (LOW 5–8k / MID 9–12k / HIGH 13–16k), mindegyik a saját frekvenciája FELETT dolgozik (alatta érintetlen):
//   Linkwitz–Riley (24 dB/okt) kettéosztás → a felső sávból egy "különbség-út": gyors felfutású (1,5 ms), lassabb
//   visszatérésű (80 ms) szintkövető alapján halk jelre nagy, hangos jelre kicsi erősítés (a hangos rész felső korlátja
//   állandó, mint a kódoló határolója) → a különbség-út lágy (tanh) határolón megy át (csak a csúcsokon ad szelíd
//   felhangot) → hozzáadva. A küszöb a teljes jel szintjéhez igazodik (bármilyen bemeneti szinten ugyanúgy szól).
// Mindegyik egység a SZÁRAZ jelből dolgozik (a hatásuk összeadódik, nem keveredik egymásba). DRIVE 0 = átlátszó.
// Jelút (2x túlmintavételezve): LEVEL IN → LOW → MID → HIGH → MIX → AUTO GAIN / LEVEL OUT
#pragma once
#include "EKIAnalog.h"
#include "EKIFilters.h"

namespace eki {

inline constexpr float kUnitHz[3][4] = {{5000, 6000, 7000, 8000}, {9000, 10000, 11000, 12000}, {13000, 14000, 15000, 16000}};

struct SatekiParams {
    float drive[3] = {5, 5, 5};   // LOW, MID, HIGH: 0…10 (alap = a user jóváhagyott beállítása)
    int freq[3] = {1, 1, 3};      // kUnitHz index (alap: 6k / 10k / 16k)
    float levelInDb = 12;         // −12…+12
    float levelOutDb = 0;         // −12…+12
    float mix = 60;               // %
    float autoGain = 1;
};

class SatekiDSP {
public:
    static constexpr int kMaxCh = 2;
    static constexpr int kSub = 32;

    void prepare(float sampleRate) {
        sr = sampleRate; sr2 = 2 * sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            dc[c].set(sr, 5.0f);
            kwIn[c][0].designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr2, 1681.0);
            kwIn[c][1].designMatched(Analog2::highPass(38.0, 0.5), sr2, 38.0);
            kwOut[c][0].designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr2, 1681.0);
            kwOut[c][1].designMatched(Analog2::highPass(38.0, 0.5), sr2, 38.0);
        }
        kEnv = 1.0f - onePoleCoef(0.3f, sr2);
        kMeter = 1.0f - onePoleCoef(0.3f, sr2);
        smoothK = 1.0f - onePoleCoef(0.03f, sr / kSub);
        kAtt = 1.0f - onePoleCoef(0.0015f, sr2); kRel = 1.0f - onePoleCoef(0.08f, sr2);
        autoMk.prepare(sr2);
        needle.set(sr);
        for (auto& f : lastFreq) f = -1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            os[c].reset(); dc[c].reset();
            for (auto& k : kwIn[c]) k.reset();
            for (auto& k : kwOut[c]) k.reset();
            for (auto& u : unit) u.reset(c);
        }
        mWW = 1e-14f; mSig = 1e-9f;
        autoMk.reset(); needle.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const SatekiParams& p) {
        ch = std::min(ch, kMaxCh);
        for (int k = 0; k < 3; ++k) {
            const int fi = std::clamp(p.freq[k], 0, 3);
            if (fi != lastFreq[k]) { unit[k].design(kUnitHz[k][fi], sr2, k); lastFreq[k] = fi; }
        }
        const bool autoOn = p.autoGain >= 0.5f;
        const float inT = clampf(p.levelInDb, -12, 12), outT = clampf(p.levelOutDb, -12, 12), mixT = clampf(p.mix, 0, 100) * 0.01f;
        float drvT[3];
        for (int k = 0; k < 3; ++k) drvT[k] = clampf(p.drive[k], 0, 10) * 0.1f;
        if (!primed) { for (int k = 0; k < 3; ++k) sDrv[k] = drvT[k]; sIn = inT; sOut = outT; sMix = mixT; primed = true; }

        for (int i0 = 0; i0 < n; i0 += kSub) {
            const int m = std::min(kSub, n - i0);
            for (int k = 0; k < 3; ++k) sDrv[k] += (drvT[k] - sDrv[k]) * smoothK;
            sIn += (inT - sIn) * smoothK; sOut += (outT - sOut) * smoothK; sMix += (mixT - sMix) * smoothK;
            const float gIn = dbToGain(sIn);
            for (int i = i0; i < i0 + m; ++i) {
                float u[kMaxCh][2];
                for (int c = 0; c < ch; ++c) os[c].up(in[c][i] * gIn, u[c][0], u[c][1]);
                for (int ph = 0; ph < 2; ++ph) {
                    float sww = 0, swd = 0, sdd = 0, pIn = 0, pOut = 0;
                    float x[kMaxCh], d[kMaxCh];
                    for (int c = 0; c < ch; ++c) { x[c] = u[c][ph]; d[c] = x[c]; }
                    // a három egység sorban, de mindegyik a száraz jelet telíti (a színük összeadódik, nem keveredik egymásba);
                    // a száraz jel ugyanazon a keresztváltó-fázison megy (MIX fésűszűrés nélkül)
                    for (int k = 0; k < 3; ++k) unit[k].process(x, d, ch, sDrv[k], kEnv, kAtt, kRel);
                    for (int c = 0; c < ch; ++c) {
                        const float wet = x[c], dry = d[c];
                        sww += (wet - dry) * (wet - dry); sdd += dry * dry;
                        const float o = dry + (wet - dry) * sMix;
                        const float ki = kwIn[c][1].tick(kwIn[c][0].tick(dry)), ko = kwOut[c][1].tick(kwOut[c][0].tick(o));
                        pIn += ki * ki; pOut += ko * ko;
                        u[c][ph] = o;
                    }
                    mWW += (sww - mWW) * kMeter; mSig += (sdd - mSig) * kMeter;
                    autoMk.trackEnergy(pIn, pOut);
                    const float gOut = dbToGain(autoOn ? autoMk.tickEnergyDb(true) - sIn : sOut + autoMk.tickEnergyDb(false));
                    for (int c = 0; c < ch; ++c) u[c][ph] *= gOut;
                }
                for (int c = 0; c < ch; ++c) out[c][i] = dc[c].tick(os[c].down(u[c][0], u[c][1]));
                // a hozzáadott levegő energiája a száraz jelhez képest
                const float rel = 10.0f * std::log10(std::max(mWW, 1e-14f) / std::max(mSig, 1e-12f));
                needle.tick(dbToGain(clampf(rel + kMeterOffset, -30.0f, 6.0f)));
            }
        }
    }

    float vuDb() const { return clampf(gainToDb(std::max(needle.pos, 1e-6f)), -20.0f, 3.0f); }
    float autoMakeupDb() const { return autoMk.value(); }
    static constexpr float kMeterOffset = 11.0f;

private:
    /// egy lágy "levegő"-egység a saját frekvenciája felett
    struct Unit {
        Biquad lp1[kMaxCh], lp2[kMaxCh], hp1[kMaxCh], hp2[kMaxCh];       // LR4 keresztváltó (alatta érintetlen)
        Biquad lp1d[kMaxCh], lp2d[kMaxCh], hp1d[kMaxCh], hp2d[kMaxCh];   // ugyanez a száraz útnak (azonos fázis)
        float env[kMaxCh] = {0, 0}, full[kMaxCh] = {1e-6f, 1e-6f};
        int kind = 1;               // 0 = LOW, 1 = MID, 2 = HIGH
        float maxLiftDb = 10, thrDb = -30, ceil = 1.4f;

        void reset(int c) {
            for (Biquad* b : {&lp1[c], &lp2[c], &hp1[c], &hp2[c], &lp1d[c], &lp2d[c], &hp1d[c], &hp2d[c]}) b->reset();
            env[c] = 0; full[c] = 1e-6f;
        }
        void design(double hz, double fs, int k) {
            kind = k;
            // a LOW kicsit szerényebb (a felső közép hamar sok), a HIGH a legtetején bátrabb
            maxLiftDb = kind == 0 ? 13.0f : kind == 1 ? 14.0f : 15.0f;
            thrDb = kind == 0 ? -10.0f : kind == 1 ? -11.0f : -14.0f;
            for (int c = 0; c < kMaxCh; ++c) {
                for (Biquad* b : {&lp1[c], &lp2[c], &lp1d[c], &lp2d[c]}) b->designLP2(hz, fs);   // LR4: az összeg pontosan lapos
                for (Biquad* b : {&hp1[c], &hp2[c], &hp1d[c], &hp2d[c]}) b->designHP2(hz, fs);
            }
        }
        /// x: a feldolgozott jel (helyben), d: a száraz jel (ugyanazon a keresztváltó-fázison); az egység a SZÁRAZ jelből dolgozik
        void process(float* x, float* d, int ch, float drive, float kEnv, float kAtt, float kRel) {
            for (int c = 0; c < ch; ++c) {
                const float lo = lp2[c].tick(lp1[c].tick(x[c])), xh = hp2[c].tick(hp1[c].tick(x[c]));
                const float hi = hp2d[c].tick(hp1d[c].tick(d[c]));
                full[c] += (d[c] * d[c] - full[c]) * kEnv;
                d[c] = lp2d[c].tick(lp1d[c].tick(d[c])) + hi;
                const float a = std::fabs(hi);
                env[c] += (a - env[c]) * (a > env[c] ? kAtt : kRel);
                if (drive <= 1e-4f) { x[c] = lo + xh; continue; }
                const float dp = std::pow(drive, 0.6f);
                // halk magas → nagy emelés, hangos magas → a különbség-út felső korlátja állandó (a kódoló jellege)
                const float G = dbToGain(maxLiftDb * dp) - 1.0f;
                const float thr = std::max(std::sqrt(full[c]) * dbToGain(thrDb), 3e-5f);
                const float diff = hi * G / (1.0f + std::pow(env[c] / thr, 0.7f));
                // lágy határoló a különbség-úton: csak a hirtelen csúcsokat kerekíti (szelíd felhang, nem recseg)
                const float lim = ceil * G * thr + 1e-9f;
                x[c] = lo + xh + lim * std::tanh(diff / lim);
            }
        }
    };

    float sr = 48000, sr2 = 96000, smoothK = 0.1f;
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    Biquad kwIn[kMaxCh][2], kwOut[kMaxCh][2];
    Unit unit[3];
    int lastFreq[3] = {-1, -1, -1};
    AutoMakeup autoMk;
    VUNeedle needle;
    float kEnv = 0, kMeter = 0, mWW = 1e-14f, mSig = 1e-9f;
    float kAtt = 0, kRel = 0;
    float sDrv[3] = {0, 0, 0}, sIn = 0, sOut = 0, sMix = 1;
    bool primed = false;
};

} // namespace eki
