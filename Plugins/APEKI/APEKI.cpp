// APEKI — punchos VCA busz-kompresszor (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "ApekiDSP.h"

using namespace eki;

enum : UInt32 { kThreshold, kRatio, kAttack, kRelease, kKnee, kThrust, kType, kOutput, kAutoGain, kLink, kAnalog, kMix, kMeterMode,
                kVU, kGR, kAutoMakeup, kNumParams };

class Apeki final : public AudioUnitBase {
public:
    Apeki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"THRESHOLD", -40, 0, -22, kAudioUnitParameterUnit_Decibels},
                {"RATIO", 0, 6, 3, kAudioUnitParameterUnit_Indexed, {"1.5:1", "2:1", "3:1", "4:1", "6:1", "10:1", "Inf:1"}},
                {"ATTACK", 0, 6, 5, kAudioUnitParameterUnit_Indexed, {"0.03 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms"}},
                {"RELEASE", 0, 5, 2, kAudioUnitParameterUnit_Indexed, {"0.05 s", "0.1 s", "0.2 s", "0.5 s", "1 s", "2 s"}},
                {"KNEE", 0, 2, 1, kAudioUnitParameterUnit_Indexed, {"Hard", "Med", "Soft"}},
                {"THRUST", 0, 2, 1, kAudioUnitParameterUnit_Indexed, {"Norm", "Med", "Loud"}},
                {"TYPE", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"New (feed-forward)", "Old (feedback)"}},
                {"OUTPUT", -24, 24, 5, kAudioUnitParameterUnit_Decibels},
                {"AUTO GAIN", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"L/R LINK", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"ANALOG", 0, 100, 55, kAudioUnitParameterUnit_Percent},
                {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"METER", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Gain Reduction", "Output +4", "Output +10"}, false, nullptr, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GAIN REDUCTION", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // AUTO GAIN alapból ki: a presetek OUTPUT értéke a mért makeup (tipikus jelen)
                //                  THR  RATIO ATK REL KNEE THR TYPE OUT AUTO LINK ANLG MIX METER
                {"Default",         {-22, 3, 5, 2, 1, 1, 0, 5, 0, 100, 55, 100, 0}},
                {"Drum Bus Punch",  {-22, 3, 5, 1, 0, 2, 0, 7, 0, 100, 60, 100, 0}},
                {"Mix Bus Glue",    {-14, 1, 6, 2, 2, 1, 1, 2.5f, 0, 100, 50, 100, 0}},
                {"Snare Snap",      {-24, 4, 4, 0, 0, 2, 0, 9.5f, 0, 100, 60, 100, 0}},
                {"Vocal Forward",   {-22, 2, 3, 2, 1, 0, 1, 9.5f, 0, 100, 45, 100, 0}},
                {"Bass Tight",      {-20, 3, 5, 2, 1, 1, 0, 7.5f, 0, 100, 55, 100, 0}},
                {"Room Slam",       {-30, 6, 0, 0, 0, 2, 0, 17.5f, 0, 100, 70, 50, 0}},
                {"Gentle Master",   {-10, 0, 6, 3, 2, 1, 1, 1, 0, 100, 30, 100, 0}},
            },
            {{1, 1}, {2, 2}}) {}

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        ApekiParams p;
        p.thresholdDb = param(kThreshold);
        p.ratio = int(param(kRatio) + 0.5f);
        p.attack = int(param(kAttack) + 0.5f);
        p.release = int(param(kRelease) + 0.5f);
        p.knee = int(param(kKnee) + 0.5f);
        p.thrust = int(param(kThrust) + 0.5f);
        p.type = int(param(kType) + 0.5f);
        p.outputDb = param(kOutput);
        p.autoGain = param(kAutoGain);
        p.link = param(kLink);
        p.analog = param(kAnalog);
        p.mix = param(kMix);
        p.meterMode = int(param(kMeterMode) + 0.5f);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kVU, clampf(dsp.vuDb(), -20, 3));
        setMeter(kGR, clampf(dsp.gainReductionDb(), -40, 0));
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
    }

private:
    ApekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* ApekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Apeki>(desc);
}
