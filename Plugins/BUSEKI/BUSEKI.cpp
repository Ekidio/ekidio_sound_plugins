// BUSEKI — VCA-s buszkompresszor (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "BusekiDSP.h"

using namespace eki;

enum : UInt32 { kThreshold, kMakeup, kRatio, kAttack, kRelease, kScHpf, kAutoGain, kAnalog, kMix, kMeterMode,
                kVU, kGR, kAutoMakeup, kNumParams };

class Buseki final : public AudioUnitBase {
public:
    Buseki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"THRESHOLD", -40, 0, -22, kAudioUnitParameterUnit_Decibels},
                {"MAKEUP", -10, 20, 0, kAudioUnitParameterUnit_Decibels},
                {"RATIO", 0, 2, 1, kAudioUnitParameterUnit_Indexed, {"2:1", "4:1", "10:1"}},
                {"ATTACK", 0, 5, 4, kAudioUnitParameterUnit_Indexed, {"0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms"}},
                {"RELEASE", 0, 4, 1, kAudioUnitParameterUnit_Indexed, {"0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto"}},
                {"SC HPF", 0, 4, 0, kAudioUnitParameterUnit_Indexed, {"Off", "60 Hz", "90 Hz", "150 Hz", "250 Hz"}},
                {"AUTO GAIN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"ANALOG", 0, 100, 45, kAudioUnitParameterUnit_Percent},
                {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"METER", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Gain Reduction", "Output +4", "Output +10"}, false, nullptr, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GAIN REDUCTION", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                //                  THR  MK RATIO ATK REL HPF AUTO ANLG MIX METER
                {"Default",         {-22, 0, 1, 4, 1, 0, 1, 45, 100, 0}},
                {"Mix Glue",        {-16, 3, 0, 4, 4, 2, 0, 50, 100, 0}},
                {"Drum Bus Punch",  {-22, 10, 1, 3, 1, 1, 0, 40, 100, 0}},
                {"Parallel Smash",  {-30, 18.5f, 2, 0, 0, 0, 0, 50, 40, 0}},
                {"Master Gentle",   {-12, 1, 0, 5, 4, 2, 0, 20, 100, 0}},
                {"Pump It",         {-28, 16.5f, 2, 2, 0, 0, 0, 60, 100, 0}},
                {"Hot Console",     {-20, 7.5f, 1, 4, 4, 1, 0, 90, 100, 0}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kMakeup, kAutoGain);   // AUTO GAIN alatt a kimeneti gain 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        BusekiParams p;
        p.thresholdDb = param(kThreshold);
        p.makeupDb = param(kMakeup);
        p.ratio = int(param(kRatio) + 0.5f);
        p.attack = int(param(kAttack) + 0.5f);
        p.release = int(param(kRelease) + 0.5f);
        p.scHpf = int(param(kScHpf) + 0.5f);
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
    BusekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* BusekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Buseki>(desc);
}
