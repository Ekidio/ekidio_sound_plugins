// EK1 — egygombos-szekciós mastering-lánc (aufx), EKIDIO SOUND. A user Logic-próbája után (2026-10-22): tiszta lánc —
// MASTEQUI-görbék (BASS PUNCH 41 Hz polc, FOCUS 1 kHz Q1, AIR 11,6 kHz polc), TRANSIENT, PUSH = LIMEKI DENSITY (2x elengedés),
// WIDTH (150 Hz felett), VOLUME → feszes, nem lüktető limiter. Alapállás: minden 0. 2026-10-23: AUTO GAIN helyett AUTO −14 LUFS
// (mint a LIMEKI-ben: a VOLUME magától áll a −14 LUFS-hoz; ugyanazon az azonosítón, alapban KI).
#include "EKIAUBase.h"
#include "Ek1DSP.h"

using namespace eki;

enum : UInt32 { kAir, kFocus, kPunch, kPush, kVolume, kWidth, kTransient, kOutput, kAutoLufs, kRelMax, kTrAttack, kTrRelease, kFocusFreq,
                kPushGR, kLimGR, kAutoVolume, kVU, kNumParams };

class Ek1 final : public AudioUnitBase {
public:
    Ek1(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"AIR", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"FOCUS", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"BASS PUNCH", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"PUSH", 0, 30, 0, kAudioUnitParameterUnit_Decibels},
                {"VOLUME", 0, 15, 0, kAudioUnitParameterUnit_Decibels},
                {"WIDTH", -5, 5, 0, kAudioUnitParameterUnit_Decibels},
                {"TRANSIENT", -10, 10, 0, kAudioUnitParameterUnit_Generic},
                {"OUTPUT", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"AUTO -14 LUFS", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"RELEASE MAX", 50, 1000, 500, kAudioUnitParameterUnit_Milliseconds},
                {"TR ATTACK", 1, 30, 8, kAudioUnitParameterUnit_Milliseconds},
                {"TR RELEASE", 5, 300, 20, kAudioUnitParameterUnit_Milliseconds},
                {"FOCUS FREQ", 300, 5000, 1000, kAudioUnitParameterUnit_Hertz},
                {"PUSH GR", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"LIMIT GR", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO VOLUME", 0, 15, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // alapállás: minden 0, AUTO −14 LUFS ki
                //                    AIR FOC PUNCH PUSH VOL WID TRANS OUT LUFS RELMAX TRATT TRREL FOCHZ
                {"Default",          {0,   0,  0,    0,   0,  0,  0,    0,  0, 500, 8, 20, 1000}},
                {"Gentle Master",    {1.5f, 0.5f, 1,  6,   3,  0,  0,    0,  0, 500, 8, 20, 1000}},
                {"Loud & Clean",     {2,   1,  1,    10,  6,  1,  0,    0,  0, 500, 8, 20, 1000}},
                {"Punchy Drums",     {1,   1,  3,    8,   3,  0,  5,    0,  0, 500, 8, 20, 1000}},
                {"Vocal Forward",    {2,   2,  0,    6,   3,  0,  0,    0,  0, 500, 8, 20, 1000}},
                {"Bright & Wide",    {4,   0,  0,    4,   3,  2,  0,    0,  0, 500, 8, 20, 1000}},
                {"Dense Bus",        {0,   0,  1,    18,  2,  0,  2,    0,  0, 500, 8, 20, 1000}},
                {"Streaming Ready",  {1,   0.5f, 1,  10,  5,  1,  1,    0,  0, 500, 8, 20, 1000}},
            },
            {{1, 1}, {2, 2}}) {
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    double latencySeconds() const override { return dsp.latencySeconds(); }
    void reset() override { dsp.reset(); grPush = grLim = 0; }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        Ek1Params p;
        p.air = param(kAir); p.focus = param(kFocus); p.punch = param(kPunch); p.push = param(kPush);
        p.volume = param(kVolume); p.width = param(kWidth); p.transient = param(kTransient);
        p.outDb = param(kOutput); p.autoLufs = param(kAutoLufs); p.relMaxMs = param(kRelMax);
        p.trAttackMs = param(kTrAttack); p.trReleaseMs = param(kTrRelease); p.focusHz = param(kFocusFreq);
        dsp.process(in, out, int(channels), int(frames), p);
        // a GR-kijelzés simítva (gyors fel, ~0,3 s vissza)
        const float k = std::exp(-float(frames) / (0.3f * float(sampleRate())));
        const float gp = dsp.takePushGr(), gl = dsp.takeLimGr();
        grPush = gp < grPush ? gp : gp + (grPush - gp) * k;
        grLim = gl < grLim ? gl : gl + (grLim - gl) * k;
        setMeter(kPushGR, clampf(grPush, -40, 0));
        setMeter(kLimGR, clampf(grLim, -40, 0));
        setMeter(kAutoVolume, clampf(dsp.autoVolumeDb(), 0, 15));
        setMeter(kVU, dsp.vuDb());
    }

private:
    Ek1DSP dsp;
    float grPush = 0, grLim = 0;
};

extern "C" __attribute__((visibility("default"))) void* Ek1Factory(const AudioComponentDescription* desc) {
    return makeFactory<Ek1>(desc);
}
