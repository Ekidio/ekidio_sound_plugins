// FAIREKI — visszacsatolt vari-mu csöves kompresszor LAT/VERT móddal (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "FairekiDSP.h"

using namespace eki;

enum : UInt32 { kInput, kThreshold, kTimeConstant, kAgc, kAgcFilter, kOutput, kAutoGain, kAnalog, kMix, kMeterMode,
                kVU, kGR, kAutoMakeup, kNeedleL, kNeedleR, kNumParams };

class Faireki final : public AudioUnitBase {
public:
    Faireki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"INPUT GAIN", -10, 20, 0, kAudioUnitParameterUnit_Decibels},
                {"THRESHOLD", -40, 0, -8, kAudioUnitParameterUnit_Decibels},   // alap −8 (a user döntése, 2026-10-23)
                {"TIME CONSTANT", 0, 5, 1, kAudioUnitParameterUnit_Indexed,
                    {"1 (0.3 s)", "2 (0.8 s)", "3 (2 s)", "4 (5 s)", "5 (auto 2/10 s)", "6 (auto 0.3/10 s)"}},
                {"AGC", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Left/Right", "Lat/Vert"}},   // alap LAT/VERT
                {"AGC FILTER", 0, 3, 0, kAudioUnitParameterUnit_Indexed, {"Off", "90 Hz", "180 Hz", "Mid Range"}},
                {"OUTPUT", -20, 20, 0, kAudioUnitParameterUnit_Decibels},
                {"AUTO GAIN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"ANALOG", 0, 100, 60, kAudioUnitParameterUnit_Percent},
                {"DRY/WET", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"METER", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Gain Reduction", "Output"}, false, nullptr, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GAIN REDUCTION", -30, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"METER LEFT/LAT", -20, 3, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"METER RIGHT/VERT", -20, 3, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // Default: AUTO GAIN be (a gain 0-n zárva); a többi preset AUTO GAIN nélkül szól, a presetek OUTPUT értéke a mért makeup (tipikus jelen)
                //                     IN  THR  TC AGC FILT OUT AUTO ANLG MIX METER
                {"Default",            {0, -8, 1, 1, 0, 0, 1, 60, 100, 0}},   // LAT/VERT, THR −8 (2026-10-23)
                {"Mix Glue",           {0, -16, 4, 0, 1, 4, 0, 55, 100, 0}},
                {"Vocal Gold",         {2, -24, 1, 0, 0, 8, 0, 65, 100, 0}},
                {"Drum Bus Thick",     {3, -26, 0, 0, 1, 7, 0, 75, 100, 0}},
                {"Mastering Lat/Vert", {0, -12, 5, 1, 1, 1.5f, 0, 45, 100, 0}},
                {"Bass Tube",          {2, -22, 2, 0, 0, 7, 0, 70, 100, 0}},
                {"Slow Glue",          {0, -18, 3, 0, 2, 3.5f, 0, 60, 100, 0}},
                {"Tube Drive",         {10, -6, 1, 0, 0, -5.5f, 0, 100, 100, 0}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kOutput, kAutoGain);   // AUTO GAIN alatt a kimeneti gain 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); setMeter(kNeedleL, 0); setMeter(kNeedleR, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        FairekiParams p;
        p.inputDb = param(kInput);
        p.thresholdDb = param(kThreshold);
        p.timeConstant = int(param(kTimeConstant) + 0.5f);
        p.latVert = int(param(kAgc) + 0.5f);
        p.scFilter = int(param(kAgcFilter) + 0.5f);
        p.outputDb = param(kOutput);
        p.autoGain = param(kAutoGain);
        p.analog = param(kAnalog);
        p.mix = param(kMix);
        p.meterMode = int(param(kMeterMode) + 0.5f);
        dsp.process(in, out, int(channels), int(frames), p);
        // egy műszer: GR módban a mélyebb csökkentés, OUTPUT módban a hangosabb csatorna (LAT/VERT-ben is)
        const float n0 = dsp.needleDb(0), n1 = channels > 1 ? dsp.needleDb(1) : n0;
        setMeter(kVU, clampf(p.meterMode == 0 ? std::min(n0, n1) : std::max(n0, n1), -20, 3));
        setMeter(kGR, clampf(dsp.gainReductionDb(), -30, 0));
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
        setMeter(kNeedleL, clampf(dsp.needleDb(0), -20, 3));
        setMeter(kNeedleR, clampf(dsp.needleDb(1), -20, 3));
    }

private:
    FairekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* FairekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Faireki>(desc);
}
