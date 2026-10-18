// OPTEKI — optikai szintszabályzó (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "OptekiDSP.h"

using namespace eki;

enum : UInt32 { kPeakReduction, kGain, kMode, kHFEmphasis, kAnalog, kMix, kMeterMode, kVU, kGR, kAutoGain, kAutoMakeup, kNumParams };

class Opteki final : public AudioUnitBase {
public:
    Opteki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"PEAK REDUCTION", 0, 100, 60, kAudioUnitParameterUnit_Generic},
                {"GAIN", -20, 30, 4, kAudioUnitParameterUnit_Decibels},
                {"COMPRESS / LIMIT", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Compress", "Limit"}},
                {"HF EMPHASIS", 0, 100, 0, kAudioUnitParameterUnit_Percent},
                {"ANALOG", 0, 100, 50, kAudioUnitParameterUnit_Percent},
                {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"METER", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Gain Reduction", "Output +4", "Output +10"}, false, nullptr, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GAIN REDUCTION", -30, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO GAIN", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"AUTO MAKEUP", 0, 30, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // AUTO GAIN alapból ki: a presetek kimeneti erősítése a mért makeup (tipikus jelen); a VU/GR helye csak kitöltés
                //               PR   GAIN MODE HF   ANLG MIX  METER VU GR AUTO
                {"Default",       {60, 4, 0, 0, 50, 100, 0, 0, 0, 0}},
                {"Smooth Vocal",  {50, 4.5f, 0, 25, 50, 100, 0, 0, 0, 0}},
                {"Vocal Limit",   {60, 12.5f, 1, 30, 50, 100, 0, 0, 0, 0}},
                {"Bass Leveler",  {55, 5.5f, 0, 0, 65, 100, 0, 0, 0, 0}},
                {"Drum Bus Glue", {45, 7, 1, 0, 70, 60, 0, 0, 0, 0}},
                {"Gentle Mix Bus",{25, 1, 0, 0, 40, 100, 1, 0, 0, 0}},
                {"HF Tamer",      {45, 4, 0, 100, 40, 100, 0, 0, 0, 0}},
                {"Tube Warmth",   {15, 2.5f, 0, 0, 100, 100, 1, 0, 0, 0}},
            },
            {{1, 1}, {2, 2}}) {}

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        OptekiParams p;
        p.peakReduction = param(kPeakReduction);
        p.gainDb = param(kGain);
        p.limit = param(kMode);
        p.hfEmphasis = param(kHFEmphasis);
        p.analog = param(kAnalog);
        p.mix = param(kMix);
        p.meterMode = int(param(kMeterMode) + 0.5f);
        p.autoGain = param(kAutoGain);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kVU, clampf(dsp.vuDb(), -20, 3));
        setMeter(kGR, clampf(dsp.gainReductionDb(), -30, 0));
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), 0, 30));
    }

private:
    OptekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* OptekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Opteki>(desc);
}
