// EKIAnalog.h — közös analóg modellek és dinamika-segédek a kompresszor-családhoz
// (trafó, cső/kimenőfokozat, automatikus gain-kompenzáció, VU-mérő módok, lágy térdű görbe).
#pragma once
#include "EKIDSP.h"

namespace eki {

/// Trafó: alacsony frekvenciás vasmag-telítés + csatolási felüláteresztő + finom HF-levágás
struct Transformer {
    OnePole lf, hf;
    DCBlocker dc;
    bool hfOn = false;   // a HF-levágás csak akkor él, ha jóval a Nyquist alatt van (pl. 96 kHz-en); 44,1/48 kHz-en nem tompít
    void prepare(float sr, float hfHz, float lfHz = 90.0f) {
        lf.set(lfHz, sr);
        hfOn = hfHz < sr * 0.4f;
        if (hfOn) hf.set(hfHz, sr);
        dc.set(sr, 7.0f);
    }
    inline float tick(float x, float drive) {
        float l = lf.lowpass(x);
        float sat = softClip(l * drive) / drive;
        float y = dc.tick(x + (sat - l));
        return hfOn ? hf.lowpass(y) : y;
    }
    /// ugyanaz a lineáris szűrőút telítés nélkül (a száraz jel fázis-igazításához)
    inline float tickLinear(float x) {
        float y = dc.tick(x);
        return hfOn ? hf.lowpass(y) : y;
    }
    void reset() { lf.reset(); hf.reset(); dc.reset(); }
};

/// A száraz (dry) jel fázis-igazítása párhuzamos keveréshez: ugyanazokon a lineáris szűrőkön megy át, mint a
/// feldolgozott jel (trafó lineáris része, 2x túlmintavételezés, DC-blokk), így a MIX keverés nem fésűsödik.
struct DryAligner {
    Transformer in, out;
    Oversampler2x os;
    DCBlocker dc;
    bool useIn = true, useOut = true;
    void prepare(float sr, float inHf, float inLf, float outHf, float outLf, float dcHz, bool withIn, bool withOut) {
        in.prepare(sr, inHf, inLf); out.prepare(sr, outHf, outLf); dc.set(sr, dcHz);
        useIn = withIn; useOut = withOut;
    }
    inline float tick(float x) {
        if (useIn) x = in.tickLinear(x);
        float u0, u1;
        os.up(x, u0, u1);
        x = dc.tick(os.down(u0, u1));
        return useOut ? out.tickLinear(x) : x;
    }
    void reset() { in.reset(); out.reset(); os.reset(); dc.reset(); }
};

/// Telítő fokozat (cső / class-A / konzol): aszimmetrikus lágy telítés, kis jelre egységnyi erősítés.
/// bias: aszimmetria (0 = csak páratlan, nagyobb = több páros felharmonikus)
struct TubeStage {
    float headroom = 3, bias = 0.15f, offset = 0, invSlope = 1;
    void set(float amount01, float biasValue = 0.15f, float cleanHeadroom = 16.0f) {
        bias = biasValue;
        headroom = cleanHeadroom * std::pow(1.0f / cleanHeadroom, clampf(amount01, 0, 1));   // tiszta … koszos (1)
        offset = softClip(bias);
        float d = (softClip(bias + 1e-3f) - softClip(bias - 1e-3f)) / 2e-3f;
        invSlope = 1.0f / d;
    }
    inline float tick(float x) const {
        float v = x / headroom;
        return (softClip(v + bias) - offset) * invSlope * headroom;
    }
};

/// Automatikus gain-kompenzáció: a hangerő-csökkentés lassú (≈1,2 s), jelenergiával súlyozott átlagát
/// emeli vissza (a hangos részek számítanak, mint a hangerő-érzetben). −50 dBFS alatt nem frissül,
/// így a szünetek nem húzzák le. Az `extraDb` pl. a bemeneti erősítés ellentettje.
struct AutoMakeup {
    float kAvg = 0, kGateRel = 0, gateEnv = 0, num = 0, den = 0, avgDb = 0, corr = 1.034f;
    Smooth s;
    void prepare(float sr, float avgSeconds = 1.2f) {
        kAvg = 1.0f - onePoleCoef(avgSeconds, sr);
        kGateRel = onePoleCoef(0.3f, sr);
        s.setTime(0.08f, sr);
        reset();
    }
    void reset() { gateEnv = num = den = avgDb = 0; s.value = 0; }
    /// inPw: a (bemeneti erősítés utáni) jel pillanatnyi teljesítménye (max. csatorna), gain: GR lineárisan
    inline void track(float inPw, float gain) {
        const float pk = std::sqrt(inPw);
        gateEnv = pk > gateEnv ? pk : pk + (gateEnv - pk) * kGateRel;
        if (gateEnv > 0.00316f) {
            const float x2 = inPw * gain * gain;
            num += (x2 * gainToDb(gain) - num) * kAvg;
            den += (x2 - den) * kAvg;
            if (den > 1e-12f) avgDb = num / den;
        }
    }
    /// a lineáris kompenzáció (simítva)
    inline float tickDb(bool on, float extraDb = 0) {
        return s.tick(on ? clampf(-corr * avgDb + extraDb, -40.0f, 40.0f) : 0.0f);
    }
    float value() const { return s.value; }

    // --- közvetlen energia-összevetés (gyors, hullámos vezérlésű kompresszorokhoz) ---
    // inPw: a nyers bemenet teljesítménye, prePw: a kompresszált jel teljesítménye a makeup előtt.
    // A kompenzáció = a két lassú átlagteljesítmény aránya, így bármilyen görbére (és a bemeneti
    // erősítésre is) pontos.
    inline void trackEnergy(float inPw, float prePw) {
        const float pk = std::sqrt(inPw);
        gateEnv = pk > gateEnv ? pk : pk + (gateEnv - pk) * kGateRel;
        if (gateEnv > 0.00316f) {
            num += (prePw - num) * kAvg;
            den += (inPw - den) * kAvg;
            if (num > 1e-12f && den > 1e-12f) avgDb = 10.0f * std::log10(num / den);
        }
    }
    inline float tickEnergyDb(bool on) { return s.tick(on ? clampf(-avgDb, -40.0f, 40.0f) : 0.0f); }
};

/// VU-mérő módokkal: 0 = GR, 1 = kimenet (0 VU = refA dBFS), 2 = kimenet (0 VU = refB dBFS)
struct VUMeter {
    VUNeedle needle;
    float refA = 1, refB = 1;
    int mode = 0;
    void prepare(float sr, float refADbfs = -18.0f, float refBDbfs = -8.0f) {
        needle.set(sr); refA = dbToGain(-refADbfs); refB = dbToGain(-refBDbfs);
    }
    void reset() { needle.reset(); }
    inline void tick(float grGain, float outPeak) {
        needle.tick(mode == 0 ? grGain : outPeak * 1.1107f * (mode == 2 ? refB : refA));
    }
    float db() const { return clampf(gainToDb(std::max(needle.pos, 1e-6f)), -40.0f, 6.0f); }
};

/// Lágy térdű statikus görbe (dB-ben): a csökkentés (≤ 0) adott túllépésnél.
/// slope = 1 − 1/arány (> 1 is lehet: túlkompresszió), knee = térdszélesség dB-ben
inline float softKneeGR(float overDb, float slope, float knee) {
    if (knee > 0.0f) {
        if (2.0f * overDb < -knee) return 0.0f;
        if (2.0f * overDb <= knee) { const float t = overDb + knee * 0.5f; return -slope * t * t / (2.0f * knee); }
    } else if (overDb <= 0.0f) {
        return 0.0f;
    }
    return -slope * overDb;
}

/// Kettős (programfüggő) elengedés: egy gyors és egy lassan töltődő ág, a mélyebb csökkentés érvényes.
/// Rövid tranziens után gyorsan enged el, tartós kompresszió után lassan. Értékek dB-ben (≤ 0).
struct DualRelease {
    float fast = 0, slow = 0;
    float kAtt = 0, kRelFast = 0, kChargeSlow = 0, kRelSlow = 0;
    void set(float sr, float attack, float relFast, float chargeSlow, float relSlow) {
        kAtt = 1.0f - onePoleCoef(attack, sr);
        kRelFast = 1.0f - onePoleCoef(relFast, sr);
        kChargeSlow = 1.0f - onePoleCoef(chargeSlow, sr);
        kRelSlow = 1.0f - onePoleCoef(relSlow, sr);
    }
    inline float tick(float target) {
        fast += (target - fast) * (target < fast ? kAtt : kRelFast);
        slow += (target - slow) * (target < slow ? kChargeSlow : kRelSlow);
        return std::min(fast, slow);
    }
    void reset() { fast = slow = 0; }
};

} // namespace eki
