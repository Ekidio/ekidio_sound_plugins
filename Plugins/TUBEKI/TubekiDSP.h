// TubekiDSP.h — opto-csöves, krémes kompresszor (a klasszikus CL 1B-típusú gépek hangulatában), AU-független.
//
// Jelút:  bemenő trafó → optikai VCA-szakasz (előrecsatolt, lágy térd) → GAIN (+AUTO) → csöves erősítőfokozat (2x OS) →
//         DC → kimenő trafó → MIX (fázishelyes)
// Detektor: ~5 ms RMS (a fénycella "látja" az energiát) → küszöb, arány (2…10:1), 8 dB-es lágy térd
// MODE:
//   FIXED          — az optikai cella saját időzítése: ~10 ms felfutás, kétlépcsős, programfüggő elengedés (60 ms … ~3 s)
//   MANUAL         — ATTACK (0,5…300 ms) és RELEASE (0,05…10 s) gombokkal
//   FIXED + MANUAL — kézi felfutás, az elengedés a kézi gyors rész + a cella lassú, programfüggő farka
// A GAIN a csöves fokozatba hajt: minél többet erősítesz, annál több a meleg (páros) felharmonikus.
#pragma once
#include "EKIAnalog.h"

namespace eki {

struct TubekiParams {
    float thresholdDb = -20;   // −40…0 dBFS
    float ratio = 3;           // 2…10
    float attackMs = 30;       // 0,5…300 ms
    float releaseS = 0.8f;     // 0,05…10 s
    int mode = 0;              // 0 FIXED, 1 MANUAL, 2 FIXED + MANUAL
    float gainDb = 0;          // −10…+30 dB
    float autoGain = 0;
    float analog = 50;         // 0…100 %
    float mix = 100;           // 0…100 %
    int meterMode = 0;
};

class TubekiDSP {
public:
    static constexpr int kMaxCh = 2;

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            inX[c].prepare(sr, 40000.0f, 80.0f);
            outX[c].prepare(sr, 40000.0f, 70.0f);
            dc[c].set(sr, 5.0f);
            dry[c].prepare(sr, 40000.0f, 80.0f, 40000.0f, 70.0f, 5.0f, true, true);
        }
        for (Smooth* s : {&sThr, &sSlope, &sGain, &sMix, &sDrive}) s->setTime(0.02f, sr);
        kRms = 1.0f - onePoleCoef(0.005f, sr);
        // FIXED (optikai cella) időállandók
        kOptAtt = 1.0f - onePoleCoef(0.010f, sr);
        kOptFast = 1.0f - onePoleCoef(0.075f, sr);              // ~50–60 ms-ig az első fele
        kOptSlowCharge = 1.0f - onePoleCoef(0.5f, sr);
        kMemUp = 1.0f - onePoleCoef(2.0f, sr);
        kMemDown = 1.0f - onePoleCoef(6.0f, sr);
        autoMk.prepare(sr);
        meter.prepare(sr);
        lastAtk = lastRel = -1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) { inX[c].reset(); outX[c].reset(); os[c].reset(); dc[c].reset(); dry[c].reset(); }
        rms = 0; grFast = grSlow = grMan = 0; mem = 0; grNow = 0;
        autoMk.reset();
        meter.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const TubekiParams& p) {
        ch = std::min(ch, kMaxCh);
        configure(clampf(p.attackMs, 0.5f, 300) * 0.001f, clampf(p.releaseS, 0.05f, 10));
        const int mode = std::clamp(p.mode, 0, 2);
        const float thrT = clampf(p.thresholdDb, -40, 0);
        const float slopeT = 1.0f - 1.0f / clampf(p.ratio, 2, 10);
        const float gainT = clampf(p.gainDb, -10, 30);
        const float mixT = clampf(p.mix, 0, 100) * 0.01f;
        const float a01 = clampf(p.analog, 0, 100) * 0.01f;
        const float driveT = 0.06f + 0.5f * a01;
        const bool autoOn = p.autoGain >= 0.5f;
        tube.set(a01, 0.16f, 14.0f);                           // csöves fokozat: meleg, páros felharmonikusok
        meter.mode = p.meterMode;
        if (!primed) { sThr.value = thrT; sSlope.value = slopeT; sGain.value = gainT; sMix.value = mixT; sDrive.value = driveT; primed = true; }

        for (int i = 0; i < n; ++i) {
            const float thr = sThr.tick(thrT), slope = sSlope.tick(slopeT);
            const float gDb = sGain.tick(gainT), mx = sMix.tick(mixT), drv = sDrive.tick(driveT);

            float x[kMaxCh], pw = 0, rawPw = 0;
            for (int c = 0; c < ch; ++c) {
                x[c] = inX[c].tick(in[c][i], drv);
                pw = std::max(pw, x[c] * x[c]);
                rawPw = std::max(rawPw, in[c][i] * in[c][i]);
            }
            rms += (pw - rms) * kRms;
            const float lvlDb = 10.0f * std::log10(std::max(rms * 2.0f, 1e-12f));
            const float target = std::max(softKneeGR(lvlDb - thr, slope, 8.0f), -40.0f);

            // optikai cella: gyors (60 ms) és lassan töltődő, "fénymemóriás" ág (0,4…3 s)
            const float lit = clampf(-target / 12.0f, 0.0f, 1.0f);
            mem += (lit - mem) * (lit > mem ? kMemUp : kMemDown);
            const float kSlowRel = 1.0f / ((0.4f + 2.6f * mem) / 2.3f * sr);
            float gr;
            if (mode == 1) {                                    // MANUAL
                grMan += (target - grMan) * (target < grMan ? kManAtt : kManRel);
                gr = grMan;
            } else if (mode == 0) {                             // FIXED
                grFast += (target - grFast) * (target < grFast ? kOptAtt : kOptFast);
                grSlow += (target - grSlow) * (target < grSlow ? kOptSlowCharge : kSlowRel);
                gr = std::min(grFast, 0.6f * grFast + 0.4f * grSlow);
            } else {                                            // FIXED + MANUAL
                grMan += (target - grMan) * (target < grMan ? kManAtt : kManRel);
                grSlow += (target - grSlow) * (target < grSlow ? kOptSlowCharge : kSlowRel);
                gr = std::min(grMan, 0.6f * grMan + 0.4f * grSlow);
            }
            grNow = gr;
            const float g = dbToGain(gr);

            float prePw = 0;
            for (int c = 0; c < ch; ++c) prePw = std::max(prePw, x[c] * x[c] * g * g);
            autoMk.trackEnergy(rawPw, prePw);
            const float mk = dbToGain(gDb + autoMk.tickEnergyDb(autoOn));

            float rect = 0;
            for (int c = 0; c < ch; ++c) {
                float u0, u1;
                os[c].up(x[c] * g * mk, u0, u1);
                float t = dc[c].tick(os[c].down(tube.tick(u0), tube.tick(u1)));
                t = outX[c].tick(t, drv);
                const float d = dry[c].tick(in[c][i]);
                const float o = d + (t - d) * mx;
                out[c][i] = o;
                rect = std::max(rect, std::fabs(o));
            }
            meter.tick(g, rect);
        }
    }

    float gainReductionDb() const { return grNow; }
    float autoMakeupDb() const { return autoMk.value(); }
    float vuDb() const { return meter.db(); }

private:
    void configure(float atk, float rel) {
        if (atk == lastAtk && rel == lastRel) return;
        kManAtt = 1.0f - onePoleCoef(atk, sr);
        kManRel = 1.0f - onePoleCoef(rel / 2.3f, sr);   // a megadott idő alatt ~90%-ot enged vissza
        lastAtk = atk; lastRel = rel;
    }

    float sr = 48000;
    Transformer inX[kMaxCh], outX[kMaxCh];
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    DryAligner dry[kMaxCh];
    TubeStage tube;
    AutoMakeup autoMk;
    VUMeter meter;
    Smooth sThr, sSlope, sGain, sMix, sDrive;
    float rms = 0, kRms = 0, grFast = 0, grSlow = 0, grMan = 0, mem = 0, grNow = 0;
    float kOptAtt = 0, kOptFast = 0, kOptSlowCharge = 0, kMemUp = 0, kMemDown = 0, kManAtt = 0, kManRel = 0;
    float lastAtk = -1, lastRel = -1;
    bool primed = false;
};

} // namespace eki
