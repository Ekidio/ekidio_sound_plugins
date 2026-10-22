// DbekiDSP.h — "dob-ütő" VCA kompresszor valódi RMS-detektorral (a klasszikus 160-as kompresszorok hangulatában), AU-független.
//
// Jelút:  VCA → OUTPUT GAIN (+AUTO) → VCA/op-amp szín (2x OS, ANALOG) → DC → MIX (fázishelyes)
// Oldallánc (összekapcsolt sztereó):
//   valódi RMS-szint (~4 ms) → küszöb, arány (1:1…∞), kemény térd
//   programfüggő felfutás: minél nagyobb a túllépés, annál gyorsabb (≈15 ms 10 dB-nél, 5 ms 20 dB-nél, 3 ms 30 dB-nél)
//   állandó sebességű elengedés: ≈120 dB/s — ettől "csattan" és lélegzik a dob
//   OVEREASY = a LÁGY mód (2026-10-23, a user: "kopogós, lágyságot várok tőle"; alapból BE): 20 dB-es széles térd (a küszöb
//   alatt fokozatosan indul), kerekebb RMS (12 ms), négyszer puhább felfutás (legalább 3 ms) és exponenciális, lágyan
//   lecsengő elengedés (≈150 ms) a gyors, egyenes 120 dB/s helyett
#pragma once
#include "EKIAnalog.h"

namespace eki {

struct DbekiParams {
    float thresholdDb = -20;   // −40…0 dBFS
    float ratio = 4;           // 1…60 (60 = ∞)
    float outputDb = 0;        // −20…+20 dB
    float overEasy = 0;        // 0 = kemény térd, 1 = OVEREASY
    float autoGain = 0;
    float analog = 40;         // 0…100 %
    float mix = 100;           // 0…100 %
    int meterMode = 0;         // 0 = GAIN CHANGE, 1 = OUTPUT, 2 = INPUT
};

class DbekiDSP {
public:
    static constexpr int kMaxCh = 2;

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            dc[c].set(sr, 5.0f);
            dry[c].prepare(sr, 40000.0f, 90.0f, 40000.0f, 90.0f, 5.0f, false, false);
        }
        for (Smooth* s : {&sThr, &sSlope, &sKnee, &sOut, &sMix}) s->setTime(0.02f, sr);
        kRms = 1.0f - onePoleCoef(0.004f, sr);
        kRmsSoft = 1.0f - onePoleCoef(0.012f, sr);
        kRelSoft = 1.0f - onePoleCoef(0.150f, sr);
        relPerSample = 120.0f / sr;                       // 120 dB/s
        autoMk.prepare(sr);
        meter.prepare(sr);
        inMeter.prepare(sr); inMeter.mode = 1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) { os[c].reset(); dc[c].reset(); dry[c].reset(); }
        rms = 0; grDb = 0; overDb = -40;
        autoMk.reset();
        meter.reset(); inMeter.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const DbekiParams& p) {
        ch = std::min(ch, kMaxCh);
        const float ratio = clampf(p.ratio, 1, 60);
        const float slopeT = ratio >= 59.5f ? 1.0f : 1.0f - 1.0f / ratio;
        const float thrT = clampf(p.thresholdDb, -40, 0);
        const bool soft = p.overEasy >= 0.5f;
        const float kneeT = soft ? 20.0f : 0.0f;
        const float outT = clampf(p.outputDb, -20, 20);
        const float mixT = clampf(p.mix, 0, 100) * 0.01f;
        const float a01 = clampf(p.analog, 0, 100) * 0.01f;
        const bool autoOn = p.autoGain >= 0.5f;
        color.set(a01 * 0.8f, 0.04f, 18.0f);               // VCA / op-amp: főleg páratlan, feszes
        meter.mode = p.meterMode == 1 ? 1 : 0;
        const bool inputMeter = p.meterMode == 2;
        if (!primed) { sThr.value = thrT; sSlope.value = slopeT; sKnee.value = kneeT; sOut.value = outT; sMix.value = mixT; primed = true; }

        for (int i = 0; i < n; ++i) {
            const float thr = sThr.tick(thrT), slope = sSlope.tick(slopeT), knee = sKnee.tick(kneeT);
            const float outDb = sOut.tick(outT), mx = sMix.tick(mixT);

            // valódi RMS (összekapcsolt sztereó: a hangosabb csatorna)
            float pw = 0, inPk = 0;
            for (int c = 0; c < ch; ++c) { pw = std::max(pw, in[c][i] * in[c][i]); inPk = std::max(inPk, std::fabs(in[c][i])); }
            rms += (pw - rms) * (soft ? kRmsSoft : kRms);
            const float lvlDb = 10.0f * std::log10(std::max(rms * 2.0f, 1e-12f));
            overDb = lvlDb - thr;
            const float target = std::max(softKneeGR(overDb, slope, knee), -50.0f);

            // programfüggő felfutás: az időállandó a hibával csökken; állandó sebességű (dB/s) elengedés
            const float err = target - grDb;
            if (err < 0) {
                float tau = clampf(0.004f * std::pow(10.0f / std::max(-err, 0.5f), 1.6f), 0.0005f, 0.040f);
                if (soft) tau = clampf(tau * 4.0f, 0.003f, 0.080f);   // lágy mód: puhább felfutás
                grDb += err * (1.0f - std::exp(-1.0f / (tau * sr)));
            } else {
                grDb += soft ? err * kRelSoft : std::min(err, relPerSample);   // lágy mód: exponenciális elengedés
            }
            const float g = dbToGain(grDb);

            float prePw = 0;
            for (int c = 0; c < ch; ++c) prePw = std::max(prePw, in[c][i] * in[c][i] * g * g);
            autoMk.trackEnergy(pw, prePw);
            const float mk = dbToGain(outDb + autoMk.tickEnergyDb(autoOn));

            float rect = 0;
            for (int c = 0; c < ch; ++c) {
                float u0, u1;
                os[c].up(in[c][i] * g * mk, u0, u1);
                const float t = dc[c].tick(os[c].down(color.tick(u0), color.tick(u1)));
                const float d = dry[c].tick(in[c][i]);
                const float o = d + (t - d) * mx;
                out[c][i] = o;
                rect = std::max(rect, std::fabs(o));
            }
            meter.tick(g, rect);
            inMeter.tick(1.0f, inPk);
            useInput = inputMeter;
        }
    }

    float gainReductionDb() const { return grDb; }
    float autoMakeupDb() const { return autoMk.value(); }
    float vuDb() const { return useInput ? inMeter.db() : meter.db(); }
    /// a detektált szint a küszöbhöz képest (dB): < 0 = BELOW, > 0 = ABOVE
    float overThresholdDb() const { return overDb; }

private:
    float sr = 48000;
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    DryAligner dry[kMaxCh];
    TubeStage color;
    AutoMakeup autoMk;
    VUMeter meter, inMeter;
    Smooth sThr, sSlope, sKnee, sOut, sMix;
    float rms = 0, kRms = 0, kRmsSoft = 0, kRelSoft = 0, grDb = 0, overDb = -40, relPerSample = 0;
    bool useInput = false;
    bool primed = false;
};

} // namespace eki
