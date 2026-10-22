// DBEKI — "dob-ütő" VCA kompresszor valódi RMS-detektorral (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "DbekiDSP.h"

using namespace eki;

enum : UInt32 { kThreshold, kRatio, kOutput, kOverEasy, kAutoGain, kAnalog, kMix, kMeterMode, kVU, kGR, kAutoMakeup, kOver, kNumParams };

class Dbeki final : public AudioUnitBase {
public:
    Dbeki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"THRESHOLD", -40, 0, -16, kAudioUnitParameterUnit_Decibels},   // lágy módban ≈ 3–4 dB GR egy átlagos mixen
                {"COMPRESSION", 1, 60, 4, kAudioUnitParameterUnit_Ratio},
                {"OUTPUT GAIN", -20, 20, 0, kAudioUnitParameterUnit_Decibels},
                {"OVEREASY", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},   // lágy mód, alapból BE (2026-10-23)
                {"AUTO GAIN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"ANALOG", 0, 100, 40, kAudioUnitParameterUnit_Percent},
                {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"METER", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Gain Change", "Output", "Input"}, false, nullptr, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GAIN REDUCTION", -50, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"ABOVE THRESHOLD", -30, 30, -30, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // Default: AUTO GAIN be (a gain 0-n zárva); a többi preset AUTO GAIN nélkül szól, a presetek OUTPUT GAIN értéke a mért makeup (tipikus jelen)
                //                 THR  RATIO OUT OE AUTO ANLG MIX METER
                {"Default",        {-16, 4, 0, 1, 1, 40, 100, 0}},
                {"Kick Snap",      {-22, 4, 7, 0, 0, 45, 100, 0}},
                {"Snare Crack",    {-24, 6, 9, 0, 0, 50, 100, 0}},
                {"Bass Even",      {-22, 3, 6, 1, 0, 40, 100, 0}},
                {"Vocal Level",    {-26, 2.5f, 7.5f, 1, 0, 35, 100, 0}},
                {"Drum Smash",     {-30, 60, 17, 0, 0, 60, 50, 0}},
                {"Guitar Thick",   {-20, 3, 5, 1, 0, 65, 100, 0}},
                {"Master Touch",   {-12, 1.5f, 0.5f, 1, 0, 25, 100, 0}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kOutput, kAutoGain);   // AUTO GAIN alatt a kimeneti gain 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); setMeter(kOver, -30); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        DbekiParams p;
        p.thresholdDb = param(kThreshold);
        p.ratio = param(kRatio);
        p.outputDb = param(kOutput);
        p.overEasy = param(kOverEasy);
        p.autoGain = param(kAutoGain);
        p.analog = param(kAnalog);
        p.mix = param(kMix);
        p.meterMode = int(param(kMeterMode) + 0.5f);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kVU, clampf(dsp.vuDb(), -20, 3));
        setMeter(kGR, clampf(dsp.gainReductionDb(), -50, 0));
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
        setMeter(kOver, clampf(dsp.overThresholdDb(), -30, 30));
    }

private:
    DbekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* DbekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Dbeki>(desc);
}
