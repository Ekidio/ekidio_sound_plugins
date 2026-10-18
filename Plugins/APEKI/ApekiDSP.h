// ApekiDSP.h — punchos VCA busz-kompresszor (a klasszikus 2500-as busz-kompresszorok hangulatában), AU-független.
//
// Jelút:  VCA → OUTPUT (+AUTO) → diszkrét op-amp + kimenő trafó (2x OS, ANALOG) → DC → MIX (fázishelyes)
// Oldallánc:
//   TYPE NEW = előrecsatolt (a bemenetet nézi: feszes, pontos), OLD = visszacsatolt (a kimenetet nézi: simább, "régi")
//   THRUST: NORM = lapos, MED = enyhe mélyvágás, LOUD = erős mélyvágás + kis magas-kiemelés a detektorban →
//           a mély kevésbé húzza le a gaint, a lábdob/basszus átüt (punch)
//   L/R LINK: 0% független csatornák … 100% teljesen összekapcsolt
//   ~1 ms RMS-detektor → KNEE (HARD 0 / MED 6 / SOFT 12 dB) → RATIO → felfutás/elengedés a dB-tartományban
#pragma once
#include "EKIAnalog.h"

namespace eki {

struct ApekiParams {
    float thresholdDb = -16;   // −40…0 dBFS
    int ratio = 3;             // 0…6: 1.5, 2, 3, 4, 6, 10, ∞
    int attack = 5;            // 0…6: 0,03 / 0,1 / 0,3 / 1 / 3 / 10 / 30 ms
    int release = 2;           // 0…5: 0,05 / 0,1 / 0,2 / 0,5 / 1 / 2 s
    int knee = 1;              // 0 HARD, 1 MED, 2 SOFT
    int thrust = 1;            // 0 NORM, 1 MED, 2 LOUD
    int type = 0;              // 0 NEW (előrecsatolt), 1 OLD (visszacsatolt)
    float outputDb = 0;        // −24…+24 dB
    float autoGain = 0;
    float link = 100;          // 0…100 %
    float analog = 50;         // 0…100 %
    float mix = 100;           // 0…100 %
    int meterMode = 0;
};

class ApekiDSP {
public:
    static constexpr int kMaxCh = 2;

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            outX[c].prepare(sr, 40000.0f, 70.0f);
            dc[c].set(sr, 5.0f);
            dry[c].prepare(sr, 40000.0f, 70.0f, 40000.0f, 70.0f, 5.0f, false, true);
            hpMed[c].set(90.0f, sr);
            hpLoud[c].set(220.0f, sr);
            hiShelf[c].set(3000.0f, sr);
        }
        for (Smooth* s : {&sThr, &sSlope, &sKnee, &sOut, &sMix, &sLink, &sThrust}) s->setTime(0.02f, sr);
        kRms = 1.0f - onePoleCoef(0.001f, sr);
        autoMk.prepare(sr);
        meter.prepare(sr);
        lastAtk = lastRel = -1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            outX[c].reset(); os[c].reset(); dc[c].reset(); dry[c].reset();
            hpMed[c].reset(); hpLoud[c].reset(); hiShelf[c].reset();
            rms[c] = 0; gr[c] = 0; gain[c] = 1; lastOut[c] = 0;
        }
        autoMk.reset();
        meter.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const ApekiParams& p) {
        ch = std::min(ch, kMaxCh);
        static const float kRatios[7] = {1.5f, 2, 3, 4, 6, 10, 1000};
        static const float kKnees[3] = {0, 6, 12};
        configure(std::clamp(p.attack, 0, 6), std::clamp(p.release, 0, 5));
        const float ratio = kRatios[std::clamp(p.ratio, 0, 6)];
        const bool feedback = p.type == 1;
        // visszacsatolásnál a meredekség a kimenetre vonatkozik: arány = 1 + meredekség (stabilitás miatt korlátozva)
        const float slopeT = feedback ? std::min(ratio - 1.0f, 14.0f) : 1.0f - 1.0f / ratio;
        const float thrT = clampf(p.thresholdDb, -40, 0);
        const float kneeT = kKnees[std::clamp(p.knee, 0, 2)];
        const float outT = clampf(p.outputDb, -24, 24);
        const float mixT = clampf(p.mix, 0, 100) * 0.01f;
        const float linkT = clampf(p.link, 0, 100) * 0.01f;
        const float thrustT = float(std::clamp(p.thrust, 0, 2));
        const float a01 = clampf(p.analog, 0, 100) * 0.01f;
        const bool autoOn = p.autoGain >= 0.5f;
        opamp.set(a01 * 0.85f, 0.08f, 16.0f);          // diszkrét op-amp: kevés páros, feszes páratlan
        meter.mode = p.meterMode;
        if (!primed) {
            sThr.value = thrT; sSlope.value = slopeT; sKnee.value = kneeT; sOut.value = outT;
            sMix.value = mixT; sLink.value = linkT; sThrust.value = thrustT;
            primed = true;
        }

        for (int i = 0; i < n; ++i) {
            const float thr = sThr.tick(thrT), slope = sSlope.tick(slopeT), knee = sKnee.tick(kneeT);
            const float outDb = sOut.tick(outT), mx = sMix.tick(mixT), link = sLink.tick(linkT), th = sThrust.tick(thrustT);
            const float drv = 0.05f + 0.5f * a01;

            // oldallánc: NEW = bemenet, OLD = az előző kimenet (visszacsatolás); THRUST szűrés; ~1 ms RMS
            float pw[kMaxCh], mx2 = 0, rawPw = 0;
            for (int c = 0; c < ch; ++c) {
                float s = feedback ? lastOut[c] : in[c][i];
                const float m = hpMed[c].highpass(s), l = hpLoud[c].highpass(s), h = hiShelf[c].highpass(s);
                // NORM → lapos, MED → félig mélyvágott, LOUD → erősen mélyvágott + magas-kiemelés (folytonos átmenet)
                const float medMix = std::min(th, 1.0f), loudMix = std::max(th - 1.0f, 0.0f);
                float sc = s + (0.5f * (m - s)) * medMix;
                sc = sc + ((l + 0.4f * h) - sc) * loudMix;
                rms[c] += (sc * sc - rms[c]) * kRms;
                pw[c] = rms[c];
                mx2 = std::max(mx2, rms[c]);
                rawPw = std::max(rawPw, in[c][i] * in[c][i]);
            }
            float y[kMaxCh], prePw = 0, grMin = 0;
            for (int c = 0; c < ch; ++c) {
                const float d = pw[c] + (mx2 - pw[c]) * link;
                const float lvlDb = 10.0f * std::log10(std::max(d * 2.0f, 1e-12f));
                const float target = std::max(softKneeGR(lvlDb - thr, slope, knee), -40.0f);
                gr[c] += (target - gr[c]) * (target < gr[c] ? kAtt : kRel);
                gain[c] = dbToGain(gr[c]);
                grMin = std::min(grMin, gr[c]);
                y[c] = in[c][i] * gain[c];
                prePw = std::max(prePw, y[c] * y[c]);
            }
            autoMk.trackEnergy(rawPw, prePw);
            const float mk = dbToGain(outDb + autoMk.tickEnergyDb(autoOn));

            float rect = 0;
            for (int c = 0; c < ch; ++c) {
                lastOut[c] = y[c];                        // visszacsatolt detektor a VCA utáni jelet látja
                float u0, u1;
                os[c].up(y[c] * mk, u0, u1);
                float t = dc[c].tick(os[c].down(opamp.tick(u0), opamp.tick(u1)));
                t = outX[c].tick(t, drv);
                const float dryAligned = dry[c].tick(in[c][i]);
                const float o = dryAligned + (t - dryAligned) * mx;
                out[c][i] = o;
                rect = std::max(rect, std::fabs(o));
            }
            meter.tick(dbToGain(grMin), rect);
        }
    }

    float gainReductionDb() const { return std::min(gr[0], gr[1]); }
    float autoMakeupDb() const { return autoMk.value(); }
    float vuDb() const { return meter.db(); }

private:
    void configure(int atk, int rel) {
        if (atk == lastAtk && rel == lastRel) return;
        static const float A[7] = {0.00003f, 0.0001f, 0.0003f, 0.001f, 0.003f, 0.010f, 0.030f};
        static const float R[6] = {0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f};
        kAtt = 1.0f - onePoleCoef(A[atk], sr);
        kRel = 1.0f - onePoleCoef(R[rel] / 2.3f, sr);   // a megadott idő alatt ~90%-ot enged vissza
        lastAtk = atk; lastRel = rel;
    }

    float sr = 48000;
    Transformer outX[kMaxCh];
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    DryAligner dry[kMaxCh];
    OnePole hpMed[kMaxCh], hpLoud[kMaxCh], hiShelf[kMaxCh];
    float rms[kMaxCh] = {}, gr[kMaxCh] = {}, gain[kMaxCh] = {1, 1}, lastOut[kMaxCh] = {};
    float kRms = 0, kAtt = 0, kRel = 0;
    int lastAtk = -1, lastRel = -1;
    TubeStage opamp;
    AutoMakeup autoMk;
    VUMeter meter;
    Smooth sThr, sSlope, sKnee, sOut, sMix, sLink, sThrust;
    bool primed = false;
};

} // namespace eki
