// VariekiDSP.h — precíz, átlátszó kettős detektoros kompresszor opcionális vari-mu színnel, AU-független.
//
// Motor (tiszta, előrecsatolt, dB-tartomány):
//   oldallánc: SC LOW CUT → sztereó-érzékenység (0% = összekapcsolt, 100% = független csatornák)
//   RMS-detektor (~20 ms) → küszöb, lágy térd, arány → felfutás + kettős (gyors/lassan töltődő) elengedés
//   csúcsdetektor → küszöb + PEAK CREST → ugyanaz a görbe → felfutás + csúcs-elengedés
//   a mélyebb csökkentés érvényes: a tartós szintet a sima RMS-ág kezeli, a tranzienseket a csúcság
//   (a crest szabja meg, mennyivel a küszöb felett kezd dolgozni).
// Szín (ANALOG > 0): trafók + vari-mu cső 2x túlmintavételezve, a csökkentéssel együtt növekvő
// felharmonikusokkal. ANALOG = 0: a színező ág teljesen kimarad (tiszta út, semmilyen szűrő).
#pragma once
#include "EKIAnalog.h"

namespace eki {

struct VariekiParams {
    float thresholdDb = -18;   // −40…0 dBFS
    float crestDb = 6;         // 0…20 dB
    float ratio = 2;           // 1…20
    float kneeDb = 6;          // 0…24 dB
    float attackMs = 10;       // 0,1…100 ms
    float relPeakMs = 50;      // 5…500 ms
    float relRmsMs = 250;      // 20…2000 ms
    float stereoSens = 0;      // 0…100 %
    int scLowCut = 0;          // 0: ki, 1: 40, 2: 80, 3: 120, 4: 200 Hz
    float makeupDb = 0;        // −12…+24 dB
    float autoGain = 1;
    float analog = 35;         // 0…100 % (vari-mu szín; 0 = teljesen tiszta)
    float mix = 100;           // 0…100 %
    int meterMode = 0;
};

class VariekiDSP {
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
        for (Smooth* s : {&sThr, &sCrest, &sSlope, &sKnee, &sMk, &sMix, &sSens}) s->setTime(0.02f, sr);
        sColor.setTime(0.05f, sr);
        kRms = 1.0f - onePoleCoef(0.020f, sr);
        autoMk.prepare(sr);
        meter.prepare(sr);
        grMeter.prepare(sr); grMeter.mode = 0;      // két oldali műszer: bal = csökkentés,
        outMeter.prepare(sr); outMeter.mode = 1;    // jobb = kimeneti VU (0 VU = −18 dBFS)
        lastAtk = lastRelP = lastRelR = -1; lastCut = -1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            inX[c].reset(); outX[c].reset(); os[c].reset(); dc[c].reset(); dry[c].reset();
            hp[c][0].reset(); hp[c][1].reset();
            rmsP[c] = 0; grPeak[c] = 0; rmsEnv[c].reset(); grNow[c] = 0;
        }
        autoMk.reset();
        meter.reset(); grMeter.reset(); outMeter.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const VariekiParams& p) {
        ch = std::min(ch, kMaxCh);
        configure(p);
        const float thrT = clampf(p.thresholdDb, -40, 0);
        const float crestT = clampf(p.crestDb, 0, 20);
        const float slopeT = 1.0f - 1.0f / clampf(p.ratio, 1, 20);
        const float kneeT = clampf(p.kneeDb, 0, 24);
        const float mkT = clampf(p.makeupDb, -12, 24);
        const float mixT = clampf(p.mix, 0, 100) * 0.01f;
        const float sensT = clampf(p.stereoSens, 0, 100) * 0.01f;
        const float colorT = clampf(p.analog, 0, 100) * 0.01f;
        const bool autoOn = p.autoGain >= 0.5f;
        meter.mode = p.meterMode;
        if (!primed) {
            sThr.value = thrT; sCrest.value = crestT; sSlope.value = slopeT; sKnee.value = kneeT;
            sMk.value = mkT; sMix.value = mixT; sSens.value = sensT; sColor.value = colorT;
            primed = true;
        }
        tube.set(std::min(1.0f, sColor.value) * 0.85f, 0.06f, 14.0f);

        for (int i = 0; i < n; ++i) {
            const float thr = sThr.tick(thrT), crest = sCrest.tick(crestT), slope = sSlope.tick(slopeT);
            const float knee = sKnee.tick(kneeT), mkDb = sMk.tick(mkT), mx = sMix.tick(mixT), sens = sSens.tick(sensT);
            const float color = sColor.tick(colorT);
            const bool colored = color > 1e-4f;

            // oldallánc: SC LOW CUT, majd az összekapcsolt és a saját csatorna keverése
            float pw[kMaxCh], link = 0, rawPw = 0;
            for (int c = 0; c < ch; ++c) {
                float s = in[c][i];
                if (cutOn) s = hp[c][1].highpass(hp[c][0].highpass(s));
                pw[c] = s * s;
                link = std::max(link, pw[c]);
                rawPw = std::max(rawPw, in[c][i] * in[c][i]);
            }
            float y[kMaxCh], prePw = 0, grMin = 0;
            for (int c = 0; c < ch; ++c) {
                const float d = link + (pw[c] - link) * sens;
                // RMS-ág: sima, tartós szint
                rmsP[c] += (d - rmsP[c]) * kRms;
                const float rmsDb = 10.0f * std::log10(std::max(rmsP[c] * 2.0f, 1e-12f));   // ×2: szinusz csúcs = 0 dB
                const float grR = rmsEnv[c].tick(std::max(softKneeGR(rmsDb - thr, slope, knee), -40.0f));
                // csúcság: tranziensek a küszöb + crest felett
                const float pkDb = 10.0f * std::log10(std::max(d, 1e-12f));
                const float tP = std::max(softKneeGR(pkDb - thr - crest, slope, knee), -40.0f);
                grPeak[c] += (tP - grPeak[c]) * (tP < grPeak[c] ? kAtt : kRelPeak);
                grNow[c] = std::min(grR, grPeak[c]);
                grMin = std::min(grMin, grNow[c]);
                y[c] = in[c][i] * dbToGain(grNow[c]);
            }
            if (colored) {   // vari-mu: a csökkentéssel nő a cső görbülete
                const float h = 0.14f * color * std::min(-grMin / 6.0f, 3.0f);
                for (int c = 0; c < ch; ++c) y[c] += h * y[c] * y[c] - 0.35f * h * y[c] * y[c] * y[c];
            }
            for (int c = 0; c < ch; ++c) prePw = std::max(prePw, y[c] * y[c]);
            autoMk.trackEnergy(rawPw, prePw);
            const float mk = dbToGain(mkDb + autoMk.tickEnergyDb(autoOn));

            float rect = 0;
            for (int c = 0; c < ch; ++c) {
                float t = y[c] * mk;
                if (colored) {
                    const float drv = 0.02f + 0.6f * color;
                    t = inX[c].tick(t, drv);
                    float u0, u1;
                    os[c].up(t, u0, u1);
                    t = os[c].down(tube.tick(u0), tube.tick(u1));
                    t = outX[c].tick(dc[c].tick(t), drv);
                }
                const float aligned = dry[c].tick(in[c][i]);         // mindig fut, hogy az állapota friss legyen
                const float d = colored ? aligned : in[c][i];         // tiszta ágban nincs szűrő → a nyers jel a fázishelyes
                const float o = d + (t - d) * mx;
                out[c][i] = o;
                rect = std::max(rect, std::fabs(o));
            }
            meter.tick(dbToGain(grMin), rect);
            grMeter.tick(dbToGain(grMin), rect);
            outMeter.tick(dbToGain(grMin), rect);
        }
    }

    float gainReductionDb() const { return std::min(grNow[0], grNow[1]); }
    float autoMakeupDb() const { return autoMk.value(); }
    float vuDb() const { return meter.db(); }
    float grNeedleDb() const { return grMeter.db(); }    // csökkentés VU-ballisztikával
    float outVuDb() const { return outMeter.db(); }

private:
    void configure(const VariekiParams& p) {
        const float atk = clampf(p.attackMs, 0.1f, 100) * 0.001f;
        const float rp = clampf(p.relPeakMs, 5, 500) * 0.001f;
        const float rr = clampf(p.relRmsMs, 20, 2000) * 0.001f;
        if (atk != lastAtk || rp != lastRelP || rr != lastRelR) {
            kAtt = 1.0f - onePoleCoef(atk, sr);
            kRelPeak = 1.0f - onePoleCoef(rp / 2.3f, sr);
            // RMS: gyors elengedés a megadott idővel, tartós kompresszió után lassabb (×4) farok
            for (auto& e : rmsEnv) e.set(sr, atk, rr / 2.3f, 0.6f, rr * 4.0f / 2.3f);
            lastAtk = atk; lastRelP = rp; lastRelR = rr;
        }
        const int cut = std::clamp(p.scLowCut, 0, 4);
        if (cut != lastCut) {
            static const float F[5] = {0, 40, 80, 120, 200};
            cutOn = cut > 0;
            for (int c = 0; c < kMaxCh; ++c) for (auto& f : hp[c]) if (cutOn) f.set(F[cut], sr);
            lastCut = cut;
        }
    }

    float sr = 48000;
    Transformer inX[kMaxCh], outX[kMaxCh];
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    DryAligner dry[kMaxCh];
    OnePole hp[kMaxCh][2];
    bool cutOn = false;
    float rmsP[kMaxCh] = {}, grPeak[kMaxCh] = {}, grNow[kMaxCh] = {};
    DualRelease rmsEnv[kMaxCh];
    float kRms = 0, kAtt = 0, kRelPeak = 0;
    float lastAtk = -1, lastRelP = -1, lastRelR = -1;
    int lastCut = -1;
    TubeStage tube;
    AutoMakeup autoMk;
    VUMeter meter, grMeter, outMeter;
    Smooth sThr, sCrest, sSlope, sKnee, sMk, sMix, sSens, sColor;
    bool primed = false;
};

} // namespace eki
