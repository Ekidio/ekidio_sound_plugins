// FETEKI — FET-es csúcskompresszor (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "FetekiDSP.h"

using namespace eki;

enum : UInt32 { kInput, kOutput, kAttack, kRelease, kRatio, kAutoGain, kAnalog, kMix, kMeterMode,
                kVU, kGR, kAutoMakeup, kNumParams };

class Feteki final : public AudioUnitBase {
public:
    Feteki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"INPUT", -10, 40, 0, kAudioUnitParameterUnit_Decibels},
                {"OUTPUT", -20, 20, 0, kAudioUnitParameterUnit_Decibels},
                {"ATTACK", 1, 7, 3, kAudioUnitParameterUnit_Generic},
                {"RELEASE", 1, 7, 5, kAudioUnitParameterUnit_Generic},
                {"RATIO", 0, 4, 0, kAudioUnitParameterUnit_Indexed, {"4:1", "8:1", "12:1", "20:1", "ALL"}},
                {"AUTO GAIN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"ANALOG", 0, 100, 50, kAudioUnitParameterUnit_Percent},
                {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"METER", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Gain Reduction", "Output +4", "Output +8"}, false, nullptr, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GAIN REDUCTION", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                //                INPUT OUT ATK REL RATIO AUTO ANLG MIX METER
                {"Default",       {0, 0, 3, 5, 0, 1, 50, 100, 0}},
                {"Vocal Grab",    {12, 5.5f, 3, 6, 0, 0, 50, 100, 0}},
                {"Drum Smash",    {25, 14, 6, 7, 4, 0, 70, 40, 0}},
                {"Bass Control",  {10, 7.5f, 2, 4, 1, 0, 50, 100, 0}},
                {"Room Crush",    {30, 10, 7, 7, 4, 0, 80, 100, 0}},
                {"Gentle Peak",   {5, 8, 2, 4, 0, 0, 40, 100, 0}},
                {"Guitar Bite",   {15, 5, 4, 6, 2, 0, 60, 100, 0}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kOutput, kAutoGain);   // AUTO GAIN alatt a kimeneti gain 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        FetekiParams p;
        p.inputDb = param(kInput);
        p.outputDb = param(kOutput);
        p.attack = param(kAttack);
        p.release = param(kRelease);
        p.ratio = int(param(kRatio) + 0.5f);
        p.autoGain = param(kAutoGain);
        p.analog = param(kAnalog);
        p.mix = param(kMix);
        p.meterMode = int(param(kMeterMode) + 0.5f);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kVU, clampf(dsp.vuDb(), -20, 3));
        setMeter(kGR, clampf(dsp.gainReductionDb(), -40, 0));
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
    }

private:
    FetekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* FetekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Feteki>(desc);
}
