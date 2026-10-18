// FetekiDSP.h — FET-es csúcskompresszor modell, AU-független.
//
// Jelút:  INPUT → bemenő trafó → FET-osztó (log-tartományú, előrecsatolt vezérlés) →
//         OUTPUT (+AUTO) → class-A kimenőfokozat (2x OS) → DC → kimenő trafó → MIX
//
// Rögzített küszöb, az INPUT-tal hajtjuk bele. ATTACK 1–7: 800 µs … 20 µs, RELEASE 1–7: 1,1 s … 50 ms
// (7 = leggyorsabb). RATIO: 4, 8, 12, 20 (nagyobb aránynál kissé magasabb küszöb, keményebb térd),
// ALL: minden gomb benyomva — mélyebb küszöb, túlkompresszió (a kimenet a küszöb felett vissza is
// eshet), gyorsabb elengedés, több torzítás. A FET mint feszültségvezérelt ellenállás a csökkentéssel
// arányosan páros felharmonikust ad; gyors elengedésnél a vezérlés hullámossága basszus-torzítást okoz.
#pragma once
#include "EKIAnalog.h"

namespace eki {

struct FetekiParams {
    float inputDb = 0;      // −10…+40 dB
    float outputDb = 0;     // −20…+20 dB
    float attack = 3;       // 1…7 (7 = leggyorsabb)
    float release = 5;      // 1…7 (7 = leggyorsabb)
    int ratio = 0;          // 0: 4, 1: 8, 2: 12, 3: 20, 4: ALL
    float autoGain = 1;
    float analog = 50;      // 0…100 %
    float mix = 100;        // 0…100 %
    int meterMode = 0;      // 0 = GR, 1 = +4, 2 = +8
};

class FetekiDSP {
public:
    static constexpr int kMaxCh = 2;

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            inX[c].prepare(sr, 40000.0f, 60.0f);
            outX[c].prepare(sr, 32000.0f, 60.0f);
            dc[c].set(sr, 5.0f);
            dry[c].prepare(sr, 40000.0f, 60.0f, 32000.0f, 60.0f, 5.0f, true, true);
        }
        for (Smooth* s : {&sIn, &sOut, &sMix, &sThr, &sSlope, &sKnee, &sDrive}) s->setTime(0.02f, sr);
        autoMk.prepare(sr);
        meter.prepare(sr, -18.0f, -10.0f);
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) { inX[c].reset(); outX[c].reset(); os[c].reset(); dc[c].reset(); dry[c].reset(); }
        grDb = 0;
        autoMk.reset();
        meter.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const FetekiParams& p) {
        ch = std::min(ch, kMaxCh);
        const int r = std::clamp(p.ratio, 0, 4);
        static const float kRatio[5] = {4, 8, 12, 20, 20};
        static const float kThr[5] = {-22, -20, -18, -16, -26};
        static const float kKnee[5] = {6, 4, 3, 2, 10};
        const bool all = r == 4;
        const float slopeT = all ? 1.12f : 1.0f - 1.0f / kRatio[r];    // ALL: túlkompresszió
        const float thrT = kThr[r], kneeT = kKnee[r];
        const float inDbT = clampf(p.inputDb, -10, 40);
        const float outDbT = clampf(p.outputDb, -20, 20);
        const float mixT = clampf(p.mix, 0, 100) * 0.01f;
        const float a01 = clampf(p.analog, 0, 100) * 0.01f;
        const float driveT = 0.1f + 0.45f * a01;
        const float atkSec = 800e-6f * std::pow(20.0f / 800.0f, (clampf(p.attack, 1, 7) - 1) / 6.0f);
        // a megadott elengedési idő alatt ~90%-ot enged vissza (τ = idő / 2,3)
        const float relSec = 1.1f * std::pow(0.05f / 1.1f, (clampf(p.release, 1, 7) - 1) / 6.0f) * (all ? 0.7f : 1.0f) / 2.3f;
        const float kAtt = 1.0f - onePoleCoef(atkSec, sr);
        const float kRel = 1.0f - onePoleCoef(relSec, sr);
        const float fetH2 = (0.03f + 0.12f * a01) * (all ? 2.0f : 1.0f);
        const bool autoOn = p.autoGain >= 0.5f;
        outStage.set(a01, 0.1f, 12.0f);
        meter.mode = p.meterMode;
        if (!primed) {
            sIn.value = inDbT; sOut.value = outDbT; sMix.value = mixT; sThr.value = thrT;
            sSlope.value = slopeT; sKnee.value = kneeT; sDrive.value = driveT;
            primed = true;
        }

        for (int i = 0; i < n; ++i) {
            const float inDb = sIn.tick(inDbT);
            const float inG = dbToGain(inDb);
            const float outDb = sOut.tick(outDbT);
            const float mx = sMix.tick(mixT);
            const float thr = sThr.tick(thrT), slope = sSlope.tick(slopeT), knee = sKnee.tick(kneeT);
            const float drv = sDrive.tick(driveT);

            float x[kMaxCh], pk = 0;
            for (int c = 0; c < ch; ++c) {
                x[c] = inX[c].tick(in[c][i] * inG, drv);
                pk = std::max(pk, std::fabs(x[c]));
            }
            // csúcsdetektor → statikus görbe → felfutás/elengedés a dB-tartományban
            const float target = std::max(softKneeGR(gainToDb(pk) - thr, slope, knee), -40.0f);
            grDb += (target - grDb) * (target < grDb ? kAtt : kRel);
            const float g = dbToGain(grDb);

            float y[kMaxCh], rawPw = 0, prePw = 0;
            for (int c = 0; c < ch; ++c) {
                y[c] = x[c] * g;
                y[c] += fetH2 * (1.0f - g) * y[c] * y[c] * 2.0f;   // FET: a csökkentéssel nő a (páros) torzítás
                rawPw = std::max(rawPw, in[c][i] * in[c][i]);
                prePw = std::max(prePw, y[c] * y[c]);
            }
            autoMk.trackEnergy(rawPw, prePw);
            const float mk = dbToGain(outDb + autoMk.tickEnergyDb(autoOn));

            float rect = 0;
            for (int c = 0; c < ch; ++c) {
                float u0, u1;
                os[c].up(y[c] * mk, u0, u1);
                float t = os[c].down(outStage.tick(u0), outStage.tick(u1));
                t = outX[c].tick(dc[c].tick(t), drv);
                const float d = dry[c].tick(in[c][i]);   // fázishelyes száraz jel
                const float o = d + (t - d) * mx;
                out[c][i] = o;
                rect = std::max(rect, std::fabs(o));
            }
            meter.tick(g, rect);
        }
    }

    float gainReductionDb() const { return grDb; }
    float autoMakeupDb() const { return autoMk.value(); }
    float vuDb() const { return meter.db(); }

private:
    float sr = 48000;
    Transformer inX[kMaxCh], outX[kMaxCh];
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    DryAligner dry[kMaxCh];
    TubeStage outStage;
    AutoMakeup autoMk;
    VUMeter meter;
    Smooth sIn, sOut, sMix, sThr, sSlope, sKnee, sDrive;
    float grDb = 0;
    bool primed = false;
};

} // namespace eki
