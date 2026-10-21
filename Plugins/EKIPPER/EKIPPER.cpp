// EKIPPER — SOFT CLIPPER (aufx), EKIDIO SOUND. Csak lágy (WARM) vágás, a referencia-mérésből illesztett görbével.
// AUTO GAIN nincs (a user döntése, 2026-10-21) — a LIMEKI mellett a második kivétel a családban.
#include "EKIAUBase.h"
#include "EkipperDSP.h"

using namespace eki;

enum : UInt32 { kDrive, kOutput, kOversample, kClip, kOutL, kOutR, kPeakL, kPeakR, kNumParams };

class Ekipper final : public AudioUnitBase {
public:
    Ekipper(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"DRIVE", 0, 20, 0, kAudioUnitParameterUnit_Decibels},
                {"OUTPUT", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"OVERSAMPLING", 0, 2, 1, kAudioUnitParameterUnit_Indexed, {"2x", "4x", "8x"}},
                {"CLIP", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"OUT L", -60, 6, -60, kAudioUnitParameterUnit_Decibels, {}, true},
                {"OUT R", -60, 6, -60, kAudioUnitParameterUnit_Decibels, {}, true},
                {"PEAK L", -60, 6, -60, kAudioUnitParameterUnit_Decibels, {}, true},
                {"PEAK R", -60, 6, -60, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                //                    DRV  OUT  OS
                {"Default",          {0,   0,   1}},
                {"Gentle Shave",     {3,   0,   1}},
                {"Warm Push",        {9,   0,   1}},
                {"Loud & Soft",      {12,  0,   1}},
                {"Drum Smash",       {16,  0,   1}},
                {"Full Saturation",  {20,  0,   1}},
                {"Master Peaks",     {2,   0,   2}},
                {"Low Latency CPU",  {6,   0,   0}},
            },
            {{1, 1}, {2, 2}}) {}

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    double latencySeconds() const override { return dsp.latencySeconds(); }
    void reset() override { dsp.reset(); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        EkipperParams p;
        p.driveDb = param(kDrive);
        p.outDb = param(kOutput);
        p.oversample = int(param(kOversample) + 0.5f);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kClip, clampf(dsp.clipDb(), -40, 0));
        setMeter(kOutL, clampf(dsp.takePeakDb(0), -60, 6));
        setMeter(kOutR, clampf(dsp.takePeakDb(1), -60, 6));
        setMeter(kPeakL, clampf(dsp.holdDb(0), -60, 6));
        setMeter(kPeakR, clampf(dsp.holdDb(1), -60, 6));
    }

private:
    EkipperDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* EkipperFactory(const AudioComponentDescription* desc) {
    return makeFactory<Ekipper>(desc);
}
