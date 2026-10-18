// FairekiDSP.h — visszacsatolt, változó meredekségű (vari-mu) csöves kompresszor
// (a klasszikus kétcsatornás 670-es gépek hangulatában), AU-független.
//
// Jelút:  INPUT → bemenő trafó → [LAT/VERT: közép/oldal kódolás] → vari-mu fokozat (visszacsatolt vezérlőfeszültség) →
//         [dekódolás] → OUTPUT (+AUTO) → cső (2x OS) → DC → kimenő trafó → DRY/WET (fázishelyes)
// A távoli levágású cső erősítése a vezérlőfeszültséggel (Vc) exponenciálisan csökken: a csökkentés dB-ben ~ Vc, a Vc
// a kimenet lineáris túllépéséből jön (visszacsatolás) → lágyan indul, a szinttel egyre meredekebb.
// A csökkentéssel nő a cső görbülete → több felharmonikus (ez a vari-mu "ragasztó" színe).
// TIME CONSTANT 1–6 (felfutás / elengedés): 1: 0,2 ms/0,3 s  2: 0,2 ms/0,8 s  3: 0,4 ms/2 s  4: 0,8 ms/5 s
//                                         5: 0,2 ms/2 s egyedi, 10 s tartós  6: 0,4 ms/0,3 s egyedi, 10 s tartós
// AGC: L/R = összekapcsolt sztereó, LAT/VERT = közép és oldal külön szabályozva. AGC FILTER: oldallánc-felüláteresztő.
#pragma once
#include "EKIAnalog.h"

namespace eki {

struct FairekiParams {
    float inputDb = 0;          // −10…+20 dB
    float thresholdDb = -20;    // −40…0 dBFS
    int timeConstant = 1;       // 0…5 (TC1…TC6)
    int latVert = 0;            // 0 = L/R (összekapcsolt), 1 = LAT/VERT (M/S)
    int scFilter = 0;           // 0 ki, 1: 90 Hz, 2: 180 Hz, 3: MID (300 Hz)
    float outputDb = 0;         // −20…+20 dB
    float autoGain = 0;
    float analog = 60;          // 0…100 %
    float mix = 100;            // 0…100 %
    int meterMode = 0;          // 0 = GR, 1 = OUTPUT
};

class FairekiDSP {
public:
    static constexpr int kMaxCh = 2;

    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            inX[c].prepare(sr, 35000.0f, 80.0f);
            outX[c].prepare(sr, 28000.0f, 70.0f);
            dc[c].set(sr, 5.0f);
            dry[c].prepare(sr, 35000.0f, 80.0f, 28000.0f, 70.0f, 5.0f, true, true);
            needle[c].prepare(sr);
        }
        for (Smooth* s : {&sIn, &sOut, &sThr, &sMix, &sDrive}) s->setTime(0.02f, sr);
        autoMk.prepare(sr);
        lastTC = lastFilt = -1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            inX[c].reset(); outX[c].reset(); os[c].reset(); dc[c].reset(); dry[c].reset(); vc[c].reset(); needle[c].reset();
            hp[c][0].reset(); hp[c][1].reset();
            gain[c] = 1; vcNow[c] = 0;
        }
        autoMk.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const FairekiParams& p) {
        ch = std::min(ch, kMaxCh);
        configure(std::clamp(p.timeConstant, 0, 5), std::clamp(p.scFilter, 0, 3));
        const float inDbT = clampf(p.inputDb, -10, 20);
        const float outDbT = clampf(p.outputDb, -20, 20);
        const float thrT = dbToGain(clampf(p.thresholdDb, -40, 0));
        const float mixT = clampf(p.mix, 0, 100) * 0.01f;
        const float a01 = clampf(p.analog, 0, 100) * 0.01f;
        const float driveT = 0.1f + 0.6f * a01;
        const bool ms = p.latVert == 1 && ch == 2;
        const bool autoOn = p.autoGain >= 0.5f;
        tube.set(a01 * 0.9f, 0.07f, 12.0f);                // ellenütemű csőfokozat
        for (auto& m : needle) m.mode = p.meterMode == 1 ? 1 : 0;
        if (!primed) { sIn.value = inDbT; sOut.value = outDbT; sThr.value = thrT; sMix.value = mixT; sDrive.value = driveT; primed = true; }

        for (int i = 0; i < n; ++i) {
            const float inG = dbToGain(sIn.tick(inDbT));
            const float outDb = sOut.tick(outDbT), thr = sThr.tick(thrT), mx = sMix.tick(mixT), drv = sDrive.tick(driveT);

            float x[kMaxCh] = {0, 0}, rawPw = 0;
            for (int c = 0; c < ch; ++c) {
                x[c] = inX[c].tick(in[c][i] * inG, drv);
                rawPw = std::max(rawPw, in[c][i] * in[c][i]);
            }
            float a[kMaxCh] = {x[0], ch > 1 ? x[1] : 0};
            if (ms) { a[0] = 0.5f * (x[0] + x[1]); a[1] = 0.5f * (x[0] - x[1]); }

            // vari-mu fokozat; a vezérlés a kimenetet nézi (visszacsatolás), AGC FILTER-rel
            float y[kMaxCh];
            float lvl[kMaxCh] = {0, 0};
            for (int c = 0; c < ch; ++c) {
                y[c] = a[c] * gain[c];
                float s = y[c];
                if (filtOn) s = hp[c][1].highpass(hp[c][0].highpass(s));
                lvl[c] = std::fabs(s);
            }
            if (ms) {
                for (int c = 0; c < 2; ++c) control(c, lvl[c], thr);
            } else {
                const float l = ch > 1 ? std::max(lvl[0], lvl[1]) : lvl[0];
                control(0, l, thr);
                if (ch > 1) { gain[1] = gain[0]; vcNow[1] = vcNow[0]; }
            }
            // a csökkentéssel nő a cső görbülete
            for (int c = 0; c < ch; ++c) {
                const float h = 0.16f * a01 * std::min(vcNow[c], 3.0f);
                y[c] += h * y[c] * y[c] - 0.35f * h * y[c] * y[c] * y[c];
            }
            if (ms) { const float l = y[0] + y[1], r = y[0] - y[1]; y[0] = l; y[1] = r; }

            float prePw = 0;
            for (int c = 0; c < ch; ++c) prePw = std::max(prePw, y[c] * y[c]);
            autoMk.trackEnergy(rawPw, prePw);
            const float mk = dbToGain(outDb + autoMk.tickEnergyDb(autoOn));

            for (int c = 0; c < ch; ++c) {
                float u0, u1;
                os[c].up(y[c] * mk, u0, u1);
                float t = dc[c].tick(os[c].down(tube.tick(u0), tube.tick(u1)));
                t = outX[c].tick(t, drv);
                const float d = dry[c].tick(in[c][i]);
                const float o = d + (t - d) * mx;
                out[c][i] = o;
                needle[c].tick(gain[c], std::fabs(o));
            }
            if (ch == 1) needle[1].tick(gain[0], std::fabs(out[0][i]));
        }
    }

    float gainReductionDb() const { return gainToDb(std::min(gain[0], gain[1])); }
    float autoMakeupDb() const { return autoMk.value(); }
    /// a két műszer: 0 = LEFT/LAT, 1 = RIGHT/VERT (GR módban csökkentés, OUTPUT módban kimeneti VU)
    float needleDb(int c) const { return needle[std::clamp(c, 0, 1)].db(); }

private:
    inline void control(int c, float lvl, float thr) {
        const float target = -6.0f * std::max(0.0f, lvl / thr - 1.0f);    // dB, ≤ 0
        const float grDb = std::max(vc[c].tick(target), -30.0f);
        vcNow[c] = -grDb / 6.0f;
        gain[c] = dbToGain(grDb);
    }

    void configure(int tc, int filt) {
        if (tc != lastTC) {
            //                    felfutás  gyors el.  lassú töltés  lassú el.
            static const float T[6][4] = {
                {0.0002f, 0.3f, 1e9f, 0.3f}, {0.0002f, 0.8f, 1e9f, 0.8f}, {0.0004f, 2.0f, 1e9f, 2.0f},
                {0.0008f, 5.0f, 1e9f, 5.0f}, {0.0002f, 2.0f, 1.5f, 10.0f}, {0.0004f, 0.3f, 1.0f, 10.0f},
            };
            // a megadott elengedési idő alatt ~90%-ot enged vissza (τ = idő / 2,3)
            for (int c = 0; c < kMaxCh; ++c) vc[c].set(sr, T[tc][0], T[tc][1] / 2.3f, T[tc][2], T[tc][3] / 2.3f);
            lastTC = tc;
        }
        if (filt != lastFilt) {
            static const float F[4] = {0, 90, 180, 300};
            filtOn = filt > 0;
            for (int c = 0; c < kMaxCh; ++c) for (auto& f : hp[c]) if (filtOn) f.set(F[filt], sr);
            lastFilt = filt;
        }
    }

    float sr = 48000;
    Transformer inX[kMaxCh], outX[kMaxCh];
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    DryAligner dry[kMaxCh];
    DualRelease vc[kMaxCh];
    OnePole hp[kMaxCh][2];
    bool filtOn = false;
    float gain[kMaxCh] = {1, 1}, vcNow[kMaxCh] = {0, 0};
    TubeStage tube;
    AutoMakeup autoMk;
    VUMeter needle[kMaxCh];
    Smooth sIn, sOut, sThr, sMix, sDrive;
    int lastTC = -1, lastFilt = -1;
    bool primed = false;
};

} // namespace eki
