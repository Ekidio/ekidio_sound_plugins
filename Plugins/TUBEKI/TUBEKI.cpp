// TUBEKI — opto-csöves, krémes kompresszor (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "TubekiDSP.h"

using namespace eki;

enum : UInt32 { kThreshold, kRatio, kAttack, kRelease, kMode, kGain, kAutoGain, kAnalog, kMix, kMeterMode, kVU, kGR, kAutoMakeup, kNumParams };

class Tubeki final : public AudioUnitBase {
public:
    Tubeki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"THRESHOLD", -40, 0, -20, kAudioUnitParameterUnit_Decibels},
                {"RATIO", 2, 10, 3, kAudioUnitParameterUnit_Ratio},
                {"ATTACK", 0.5f, 300, 30, kAudioUnitParameterUnit_Milliseconds},
                {"RELEASE", 0.05f, 10, 0.8f, kAudioUnitParameterUnit_Seconds},
                {"ATTACK/RELEASE", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Fixed", "Manual", "Fixed + Manual"}},
                {"GAIN", -10, 30, 0, kAudioUnitParameterUnit_Decibels},
                {"AUTO GAIN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"ANALOG", 0, 100, 50, kAudioUnitParameterUnit_Percent},
                {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"METER", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Compression", "Output +4", "Output +10"}, false, nullptr, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GAIN REDUCTION", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // Default: AUTO GAIN be (a gain 0-n zárva); a többi preset AUTO GAIN nélkül szól, a presetek GAIN értéke a mért makeup (tipikus jelen)
                //                  THR  RATIO ATK  REL   MODE GAIN AUTO ANLG MIX METER
                {"Default",         {-20, 3, 30, 0.8f, 0, 0, 1, 50, 100, 0}},
                {"Creamy Vocal",    {-24, 3, 20, 0.6f, 0, 9.5f, 0, 55, 100, 0}},
                {"Vocal Upfront",   {-28, 4, 10, 0.3f, 2, 14, 0, 60, 100, 0}},
                {"Bass Smooth",     {-22, 4, 30, 0.8f, 0, 9, 0, 60, 100, 0}},
                {"Acoustic Gentle", {-18, 2, 40, 1.0f, 0, 4.5f, 0, 45, 100, 0}},
                {"Drum Room Tube",  {-30, 8, 5, 0.15f, 1, 17.5f, 0, 75, 60, 0}},
                {"Mix Bus Warm",    {-14, 2, 50, 1.5f, 2, 3, 0, 50, 100, 0}},
                {"Hot Tube Push",   {-20, 3, 30, 0.8f, 0, 7, 0, 100, 100, 0}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kGain, kAutoGain);   // AUTO GAIN alatt a kimeneti gain 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        TubekiParams p;
        p.thresholdDb = param(kThreshold);
        p.ratio = param(kRatio);
        p.attackMs = param(kAttack);
        p.releaseS = param(kRelease);
        p.mode = int(param(kMode) + 0.5f);
        p.gainDb = param(kGain);
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
    TubekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* TubekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Tubeki>(desc);
}
