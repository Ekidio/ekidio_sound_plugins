// OptekiDSP.h — optikai (T4-szerű) kompresszor modell, AU-független.
//
// Jelút:  bemenő trafó → optikai osztó (LDR) → [oldallánc leágazás, visszacsatolt] → GAIN →
//         csöves fokozat (2x túlmintavételezve) → DC-blokk → kimenő trafó → MIX
//
// Optikai cella: az oldallánc (HF EMPHASIS szűrővel) egy EL-panelt hajt (gyors csúcskövető),
// a fotoellenállás vezetése két részből áll: gyors (~10 ms fel / ~60 ms le) és lassú
// (~10 ms fel / 0,35…3,5 s le), a lassú rész elengedése a "fénymemóriától" függ: minél tovább és
// erősebben világított a cella, annál lassabban enged el (programfüggő elengedés).
// Az osztó erősítése 1/(1+G); G ~ fény^γ, így a visszacsatolás miatt az arány ≈ 1+γ
// (COMPRESS: γ=2 → ~3:1 lágy térddel, LIMIT: γ=7 → ~8:1 keményebb térddel).
#pragma once
#include "EKIAnalog.h"

namespace eki {

struct OptekiParams {
    float peakReduction = 40;   // 0…100
    float gainDb = 0;           // −20…+30 dB
    float limit = 0;            // 0 = COMPRESS, 1 = LIMIT
    float hfEmphasis = 0;       // 0…100 %
    float analog = 50;          // 0…100 % (cső + trafó)
    float mix = 100;            // 0…100 %
    int meterMode = 0;          // 0 = GR, 1 = OUTPUT +4, 2 = OUTPUT +10
    float autoGain = 1;         // 1 = automatikus gain-kompenzáció (átlagos GR visszaemelése)
};

class OptekiDSP {
public:
    static constexpr int kMaxCh = 2;

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            inX[c].prepare(sr, 40000.0f);
            outX[c].prepare(sr, 30000.0f);
            dc[c].set(sr, 5.0f);
            dry[c].prepare(sr, 40000.0f, 90.0f, 30000.0f, 90.0f, 5.0f, true, true);
            scHP[c].set(1800.0f, sr);
        }
        elAtt = onePoleCoef(0.004f, sr);
        elRel = onePoleCoef(0.012f, sr);
        kAttF = 1.0f - onePoleCoef(0.030f, sr);
        kRelF = 1.0f - onePoleCoef(0.045f, sr);
        kAttS = 1.0f - onePoleCoef(0.040f, sr);
        kMemUp = 1.0f - onePoleCoef(2.5f, sr);
        kMemDown = 1.0f - onePoleCoef(8.0f, sr);
        for (Smooth* s : {&sGain, &sMix, &sMode, &sSC, &sHF, &sDrive}) s->setTime(0.02f, sr);
        autoMk.prepare(sr);
        needle.set(sr);
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) { inX[c].reset(); outX[c].reset(); os[c].reset(); dc[c].reset(); scHP[c].reset(); dry[c].reset(); }
        el = Gf = Gs = mem = 0;
        autoMk.reset();
        gainNow = 1;
        needle.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const OptekiParams& p) {
        ch = std::min(ch, kMaxCh);
        // célértékek
        const float pr = clampf(p.peakReduction, 0, 100);
        const float onsetDb = 0.0f - 0.45f * pr;                      // PR 0 → 0 dBFS, PR 100 → −45 dBFS
        const float scTarget = dbToGain(-onsetDb) * std::min(1.0f, pr * 0.5f);
        const float gainDbTarget = clampf(p.gainDb, -20, 30);
        const float autoOn = p.autoGain >= 0.5f ? 1.0f : 0.0f;
        const float mixTarget = clampf(p.mix, 0, 100) * 0.01f;
        const float modeTarget = p.limit >= 0.5f ? 1.0f : 0.0f;
        const float hfTarget = clampf(p.hfEmphasis, 0, 100) * 0.01f * 3.0f;
        const float a01 = clampf(p.analog, 0, 100) * 0.01f;
        const float driveTarget = 0.12f + 0.5f * a01;                 // trafó LF-telítés
        tube.set(a01);
        if (!primed) {
            sGain.value = gainDbTarget; sMix.value = mixTarget; sMode.value = modeTarget;
            sSC.value = scTarget; sHF.value = hfTarget; sDrive.value = driveTarget;
            primed = true;
        }
        const float vuRef = p.meterMode == 2 ? dbToGain(8.0f) : dbToGain(18.0f);   // 0 VU = −8 / −18 dBFS

        for (int i = 0; i < n; ++i) {
            const float gDb = sGain.tick(gainDbTarget);
            const float mx = sMix.tick(mixTarget);
            const float md = sMode.tick(modeTarget);
            const float scg = sSC.tick(scTarget);
            const float hfg = sHF.tick(hfTarget);
            const float drv = sDrive.tick(driveTarget);

            // 1) bemenő trafó + optikai osztó, oldallánc (összekapcsolt sztereó)
            float y[kMaxCh];
            float sc = 0;
            for (int c = 0; c < ch; ++c) {
                float x = inX[c].tick(in[c][i], drv);
                y[c] = x * gainNow;
                float s = std::fabs(y[c] + hfg * scHP[c].highpass(y[c]));
                sc = std::max(sc, s);
            }
            updateCell(sc * scg, md);

            // automatikus gain-kompenzáció (EKIAnalog.h)
            float inPw = 0;
            for (int c = 0; c < ch; ++c) inPw = std::max(inPw, in[c][i] * in[c][i]);
            autoMk.track(inPw, gainNow);
            const float autoDb = std::min(autoMk.tickDb(autoOn > 0.5f), 30.0f);
            const float g = dbToGain(gDb + autoDb);

            // 2) GAIN → cső (2x) → DC → kimenő trafó → MIX
            float rect = 0;
            for (int c = 0; c < ch; ++c) {
                float u0, u1;
                os[c].up(y[c] * g, u0, u1);
                float t = os[c].down(tube.tick(u0), tube.tick(u1));
                t = outX[c].tick(dc[c].tick(t), drv);
                const float d = dry[c].tick(in[c][i]);   // fázishelyes száraz jel
                float o = d + (t - d) * mx;
                out[c][i] = o;
                rect = std::max(rect, std::fabs(o));
            }

            // 3) VU-mutató
            needle.tick(p.meterMode == 0 ? gainNow : rect * 1.1107f * vuRef);
        }
        lastMeterMode = p.meterMode;
    }

    /// pillanatnyi hangerő-csökkentés dB-ben (≤ 0)
    float gainReductionDb() const { return gainToDb(gainNow); }
    /// az automatikus kompenzáció pillanatnyi értéke dB-ben
    float autoMakeupDb() const { return autoMk.value(); }
    /// VU-mutató állása dB-ben (GR módban a csökkentés, kimeneti módban a VU-szint)
    float vuDb() const { return clampf(gainToDb(std::max(needle.pos, 1e-6f)), -40.0f, 6.0f); }

private:
    inline void updateCell(float drive, float limitMix) {
        // EL-panel: gyors csúcskövető
        el = drive > el ? drive + (el - drive) * elAtt : drive + (el - drive) * elRel;
        // fény → vezetés (el = 1 ≈ 1 dB GR küszöb)
        const float e2 = el * el;
        const float gC = 0.122f * e2;
        const float e7 = e2 * e2 * e2 * el;
        const float gL = 0.122f * e7;
        float Gt = std::min(gC + (gL - gC) * limitMix, 1.0e4f);
        // gyors rész
        Gf += (Gt - Gf) * (Gt > Gf ? kAttF : kRelF);
        // fénymemória és lassú rész
        const float lit = Gt / (1.0f + Gt);
        mem += (lit - mem) * (lit > mem ? kMemUp : kMemDown);
        const float relS = 1.0f / ((0.35f + 3.2f * mem) * sr);
        Gs += (Gt - Gs) * (Gt > Gs ? kAttS : relS);
        const float G = 0.65f * Gf + 0.35f * Gs;
        gainNow = 1.0f / (1.0f + G);
    }

    float sr = 48000;
    Transformer inX[kMaxCh], outX[kMaxCh];
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    DryAligner dry[kMaxCh];
    OnePole scHP[kMaxCh];
    TubeStage tube;
    Smooth sGain, sMix, sMode, sSC, sHF, sDrive;
    VUNeedle needle;
    float el = 0, Gf = 0, Gs = 0, mem = 0, gainNow = 1;
    AutoMakeup autoMk;
    float elAtt = 0, elRel = 0, kAttF = 0, kRelF = 0, kAttS = 0, kMemUp = 0, kMemDown = 0;
    int lastMeterMode = 0;
    bool primed = false;
};

} // namespace eki
