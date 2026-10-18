// BusekiDSP.h — VCA-s buszkompresszor ("ragasztó") modell, AU-független.
//
// Jelút:  VCA (előrecsatolt, összekapcsolt sztereó) → MAKEUP (+AUTO) → konzol-kimenőfokozat (2x OS) →
//         DC → MIX.  Transzformátor nincs: tisztább, feszesebb karakter.
// Oldallánc: SC HPF (ki / 60 / 90 / 150 / 250 Hz) → ~2 ms RMS-detektor → statikus görbe (4 dB-es térd)
//            → felfutás/elengedés a dB-tartományban.
// RATIO 2 / 4 / 10, ATTACK 0,1 / 0,3 / 1 / 3 / 10 / 30 ms, RELEASE 0,1 / 0,3 / 0,6 / 1,2 s / AUTO
// (AUTO: programfüggő, egyedi tranziens után ~0,1 s, tartós kompresszió után ~1,2 s).
#pragma once
#include "EKIAnalog.h"

namespace eki {

struct BusekiParams {
    float thresholdDb = -18;   // −40…0 dBFS
    int ratio = 1;             // 0: 2, 1: 4, 2: 10
    int attack = 5;            // 0…5: 0,1 / 0,3 / 1 / 3 / 10 / 30 ms
    int release = 4;           // 0…4: 0,1 / 0,3 / 0,6 / 1,2 s / AUTO
    int scHpf = 0;             // 0: ki, 1: 60, 2: 90, 3: 150, 4: 250 Hz
    float makeupDb = 0;        // −10…+20 dB
    float autoGain = 1;
    float analog = 45;         // 0…100 %
    float mix = 100;           // 0…100 %
    int meterMode = 0;         // 0 = GR, 1 = +4, 2 = +10
};

class BusekiDSP {
public:
    static constexpr int kMaxCh = 2;

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c) { dc[c].set(sr, 5.0f); dry[c].prepare(sr, 40000.0f, 90.0f, 40000.0f, 90.0f, 5.0f, false, false); }
        for (Smooth* s : {&sThr, &sSlope, &sMk, &sMix}) s->setTime(0.02f, sr);
        kRms = 1.0f - onePoleCoef(0.002f, sr);
        autoMk.prepare(sr);
        meter.prepare(sr);
        lastAtk = lastRel = lastHpf = -1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) { os[c].reset(); dc[c].reset(); dry[c].reset(); hpf[c][0].reset(); hpf[c][1].reset(); }
        rms = 0;
        env.reset();
        autoMk.reset();
        meter.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const BusekiParams& p) {
        ch = std::min(ch, kMaxCh);
        static const float kRatios[3] = {2, 4, 10};
        configure(std::clamp(p.attack, 0, 5), std::clamp(p.release, 0, 4), std::clamp(p.scHpf, 0, 4));
        const float thrT = clampf(p.thresholdDb, -40, 0);
        const float slopeT = 1.0f - 1.0f / kRatios[std::clamp(p.ratio, 0, 2)];
        const float mkT = clampf(p.makeupDb, -10, 20);
        const float mixT = clampf(p.mix, 0, 100) * 0.01f;
        const float a01 = clampf(p.analog, 0, 100) * 0.01f;
        const bool autoOn = p.autoGain >= 0.5f;
        console.set(a01 * 0.8f, 0.03f, 20.0f);                           // konzol: főleg páratlan, nagy tartalék
        meter.mode = p.meterMode;
        if (!primed) { sThr.value = thrT; sSlope.value = slopeT; sMk.value = mkT; sMix.value = mixT; primed = true; }

        for (int i = 0; i < n; ++i) {
            const float thr = sThr.tick(thrT), slope = sSlope.tick(slopeT);
            const float mkDb = sMk.tick(mkT), mx = sMix.tick(mixT);

            // oldallánc: HPF → RMS (összekapcsolt: a hangosabb csatorna)
            float scPw = 0, inPw = 0;
            for (int c = 0; c < ch; ++c) {
                float s = in[c][i];
                if (hpfOn) s = hpf[c][1].highpass(hpf[c][0].highpass(s));
                scPw = std::max(scPw, s * s);
                inPw = std::max(inPw, in[c][i] * in[c][i]);
            }
            rms += (scPw - rms) * kRms;
            const float lvlDb = 10.0f * std::log10(std::max(rms * 2.0f, 1e-12f));   // ×2: szinusz csúcs = 0 dB
            const float target = std::max(softKneeGR(lvlDb - thr, slope, 4.0f), -40.0f);
            const float grDb = env.tick(target);
            const float g = dbToGain(grDb);
            grNow = grDb;

            autoMk.trackEnergy(inPw, inPw * g * g);
            const float mk = dbToGain(mkDb + autoMk.tickEnergyDb(autoOn));

            float rect = 0;
            for (int c = 0; c < ch; ++c) {
                float u0, u1;
                os[c].up(in[c][i] * g * mk, u0, u1);
                float t = dc[c].tick(os[c].down(console.tick(u0), console.tick(u1)));
                const float d = dry[c].tick(in[c][i]);   // fázishelyes száraz jel
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
    void configure(int atk, int rel, int hp) {
        if (atk != lastAtk || rel != lastRel) {
            static const float A[6] = {0.0001f, 0.0003f, 0.001f, 0.003f, 0.010f, 0.030f};
            static const float R[4] = {0.1f, 0.3f, 0.6f, 1.2f};
            // a megadott elengedési idő alatt ~90%-ot enged vissza (τ = idő / 2,3)
            if (rel == 4) env.set(sr, A[atk], 0.1f / 2.3f, 0.9f, 1.2f / 2.3f);   // AUTO: programfüggő
            else env.set(sr, A[atk], R[rel] / 2.3f, 1e9f, R[rel] / 2.3f);
            lastAtk = atk; lastRel = rel;
        }
        if (hp != lastHpf) {
            static const float F[5] = {0, 60, 90, 150, 250};
            hpfOn = hp > 0;
            for (int c = 0; c < kMaxCh; ++c) for (auto& f : hpf[c]) if (hpfOn) f.set(F[hp], sr);
            lastHpf = hp;
        }
    }

    float sr = 48000;
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    DryAligner dry[kMaxCh];
    OnePole hpf[kMaxCh][2];
    bool hpfOn = false;
    DualRelease env;
    TubeStage console;
    AutoMakeup autoMk;
    VUMeter meter;
    Smooth sThr, sSlope, sMk, sMix;
    float rms = 0, kRms = 0, grNow = 0;
    int lastAtk = -1, lastRel = -1, lastHpf = -1;
    bool primed = false;
};

} // namespace eki
