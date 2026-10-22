// LIMEKI — DENSITY & CLEAN LIMITER (aufx), EKIDIO SOUND. Két kikapcsolható fokozat, együtt balról jobbra.
// AUTO GAIN nincs (a user döntése: a limiter célja a hangerő emelése — az egyetlen kivétel a családban).
// 2026-10-23: a CLEAN motorja az EK1 VOLUME-limitere (Shared/EKILoudLimiter.h), PUSH 0–15 dB; a RELEASE, TRUE PEAK, CLEAN LINK és
// LOOK-AHEAD megszűnt (a motor maga variálja az elengedést, mindig true-peak és csatolt) — az azonosítójuk rejtve megmarad.
// Alapállás: THRESHOLD 0, PUSH 0 (a user "nullázása").
#include "EKIAUBase.h"
#include "LimekiDSP.h"

using namespace eki;

enum : UInt32 { kThreshold, kDenCeiling, kDenRelease, kLink, kDenMode, kDenOn,
                kPush, kCeiling, kRelease, kTruePeak, kCleanOn, kCleanLink, kLookahead, kDenMix, kAutoLufs,
                kDenGR, kCleanGR, kOutL, kOutR, kPeakL, kPeakR, kLufs, kPushApplied, kNumParams };

class Limeki final : public AudioUnitBase {
public:
    Limeki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"THRESHOLD", 0, 30, 0, kAudioUnitParameterUnit_Decibels},                     // lehúzás (a kijelzőn negatív)
                {"DENSITY CEILING", -12, 0, 0, kAudioUnitParameterUnit_Decibels},
                {"DENSITY RELEASE", 0.5f, 2, 1, kAudioUnitParameterUnit_Generic},              // az időállandó szorzója
                {"LINK", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Stereo", "Dual"}},
                {"DENSITY MODE", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Slow", "Fast"}},
                {"DENSITY ON", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"PUSH", 0, 15, 0, kAudioUnitParameterUnit_Decibels},
                {"CEILING", -3, 0, -1, kAudioUnitParameterUnit_Decibels},
                {"RELEASE", 0, 5, 0, kAudioUnitParameterUnit_Indexed, {"Auto", "10 ms", "30 ms", "100 ms", "300 ms", "1 s"}, false, nullptr, false, true},
                {"TRUE PEAK", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}, false, nullptr, false, true},
                {"CLEAN ON", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"CLEAN LINK", 0, 100, 0, kAudioUnitParameterUnit_Percent, {}, false, nullptr, false, true},
                {"LOOK-AHEAD", 0, 2, 1, kAudioUnitParameterUnit_Indexed, {"1 ms", "3 ms", "5 ms"}, false, nullptr, false, true},
                {"DENSITY MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"AUTO -14 LUFS", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"DENSITY GR", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"CLEAN GR", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"OUT L", -60, 6, -60, kAudioUnitParameterUnit_Decibels, {}, true},
                {"OUT R", -60, 6, -60, kAudioUnitParameterUnit_Decibels, {}, true},
                {"PEAK L", -60, 6, -60, kAudioUnitParameterUnit_Decibels, {}, true},
                {"PEAK R", -60, 6, -60, kAudioUnitParameterUnit_Decibels, {}, true},
                {"LUFS", -70, 0, -70, kAudioUnitParameterUnit_Generic, {}, true},
                {"PUSH APPLIED", 0, 15, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                //                    THR CEIL REL LINK MODE DON PUSH CEIL REL TP CON CLINK LA MIX AUTO
                {"Default",          {0,  0,   1,  0,   0,   1,  0,   -1,  0,  1, 1, 0,  1, 100, 0}},
                {"Dense Bus",        {24, 0,   1,  0,   0,   1,  0,   -1,  0,  1, 1, 0,  1, 100, 0}},
                {"Heavy Press",      {30, 0,   1,  0,   0,   1,  0,   -1,  0,  1, 1, 0,  1, 100, 0}},
                {"Punchy Drums",     {12, 0,   1,  0,   1,   1,  0,   -1,  0,  1, 1, 0,  1, 100, 0}},
                {"Clean Master",     {0,  0,   1,  0,   0,   0,  4,   -1,  0,  1, 1, 0,  2, 100, 0}},
                {"Dense + Clean",    {18, -3,  1,  0,   0,   1,  3,   -1,  0,  1, 1, 0,  1, 100, 0}},
                {"Streaming -14",    {12, -3,  1,  0,   0,   1,  0,   -1,  0,  1, 1, 0,  1, 100, 1}},
                {"Parallel Density", {24, 0,   1,  0,   0,   1,  0,   -1,  0,  1, 1, 0,  1, 50,  0}},
            },
            {{1, 1}, {2, 2}}) {}

    void prepare(double sampleRate, UInt32, UInt32) override { sr = sampleRate; dsp.prepare(float(sampleRate)); }
    double sampleRateValue() const { return sr; }
    double latencySeconds() const override { return dsp.latencySeconds(); }
    void reset() override { dsp.reset(); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        LimekiParams p;
        p.thresholdDb = -param(kThreshold);
        p.denCeilDb = param(kDenCeiling);
        p.denRelTrim = param(kDenRelease);
        p.link = int(param(kLink) + 0.5f);
        p.denMode = int(param(kDenMode) + 0.5f);
        p.denOn = param(kDenOn);
        p.pushDb = param(kPush);
        p.ceilDb = param(kCeiling);
        p.cleanOn = param(kCleanOn);
        p.denMix = param(kDenMix);
        p.autoLufs = param(kAutoLufs);
        dsp.process(in, out, int(channels), int(frames), p);
        // a GR-mutató VU-szerűen esik vissza (≈ 0,3 s), hogy olvasható legyen
        const float gr = dsp.takeDenGr(), kb = 1.0f - std::exp(-float(frames) / (0.3f * float(std::max(1.0, sampleRateValue()))));
        denGrDisp = gr < denGrDisp ? gr : denGrDisp + (gr - denGrDisp) * kb;
        setMeter(kDenGR, clampf(denGrDisp, -40, 0));
        setMeter(kCleanGR, clampf(dsp.takeCleanGr(), -40, 0));
        setMeter(kOutL, clampf(dsp.takePeakDb(0), -60, 6));
        setMeter(kOutR, clampf(dsp.takePeakDb(channels > 1 ? 1 : 0), -60, 6));
        setMeter(kPeakL, clampf(dsp.peakHoldDb(0), -60, 6));
        setMeter(kPeakR, clampf(dsp.peakHoldDb(channels > 1 ? 1 : 0), -60, 6));
        setMeter(kLufs, clampf(dsp.lufs(), -70, 0));
        setMeter(kPushApplied, dsp.pushAppliedDb());
    }

private:
    LimekiDSP dsp;
    float denGrDisp = 0;
    double sr = 48000;
};

extern "C" __attribute__((visibility("default"))) void* LimekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Limeki>(desc);
}
