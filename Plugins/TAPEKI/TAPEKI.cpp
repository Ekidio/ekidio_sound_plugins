// TAPEKI — ANALOG TAPE MACHINE: STUDIO / MASTER / CASSETTE, szalagtelítés, head bump, wow & flutter, zaj (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "TapekiDSP.h"

using namespace eki;

enum : UInt32 { kRecLevel, kSpeed, kMachine, kWow, kNoise, kBias, kBump, kCal, kReproLf, kReproHf, kMix, kLevelOut, kAutoGain,
                kVU, kAutoMakeup, kNumParams };

class Tapeki final : public AudioUnitBase {
public:
    Tapeki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"REC LEVEL", 0, 10, 4.7f, kAudioUnitParameterUnit_Generic},
                {"SPEED", 0, 2, 2, kAudioUnitParameterUnit_Indexed, {"7.5 ips", "15 ips", "30 ips"}},
                {"MACHINE", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Studio", "Master", "Cassette"}},
                {"WOW & FLUTTER", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"NOISE", 0, 10, 1, kAudioUnitParameterUnit_Generic},
                {"BIAS", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Under", "Norm", "Over"}},
                {"HEAD BUMP", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"CAL", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"+3", "+6", "+9"}},
                {"REPRO LF", -4, 4, 0, kAudioUnitParameterUnit_Decibels},
                {"REPRO HF", -8, 8, 0, kAudioUnitParameterUnit_Decibels},
                {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"LEVEL OUT", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"AUTO GAIN", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"TAPE LEVEL", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // AUTO GAIN: a Default presetben és alapértékként KI (a user döntése, 2026-10-23); a többi preset bekapcsolva hagyja
                // alap: a user beállítása: STUDIO, 30 ips, REC 4,7, W&F 0, NOISE 1, BIAS UNDER, HEAD BUMP 0, CAL +3, REPRO LF 0, REPRO HF 0 (= +6 dB)
                //                    REC   SPD MACH WOW NOISE BIAS BUMP CAL RLF   RHF   MIX  OUT AUTO
                {"Default",           {4.7f, 2,  0,   0,  1,    0,   0,   0,  0,    0,    100, 0,  0}},   // a user beállítása (képről)
                {"Warm Drums",        {6.5f, 1,  0,   1,  1,    0,   6,   1,  1,    -4,   100, 0,  1}},
                {"Mix Bus Glue",      {5,    2,  0,   1,  1,    1,   3,   0,  0,    -3.5f, 100, 0, 1}},
                {"Mastering Gloss",   {4.5f, 2,  1,   0,  0,    2,   2,   0,  0,    -3,   100, 0,  1}},
                {"Vocal Warmth",      {5.5f, 2,  0,   1,  1,    1,   2,   0,  -0.5f, -3.5f, 100, 0, 1}},
                {"Bass Thump",        {6,    1,  0,   1,  1,    1,   9,   1,  1.5f, -4,   100, 0,  1}},
                {"Lo-Fi Cassette",    {7,    0,  2,   7,  6,    0,   4,   2,  0,    -4,   100, 0,  1}},
                {"Hot Tape Smash",    {9,    1,  0,   2,  2,    0,   6,   2,  0,    -4,   80,  0,  1}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kLevelOut, kAutoGain);   // AUTO GAIN alatt a kimenet 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    double latencySeconds() const override { return dsp.latencySeconds(); }   // a wow & flutter állandó késleltetése
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kAutoMakeup, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        TapekiParams p;
        p.recLevel = param(kRecLevel);
        p.speed = int(param(kSpeed) + 0.5f);
        p.machine = int(param(kMachine) + 0.5f);
        p.wow = param(kWow);
        p.noise = param(kNoise);
        p.bias = int(param(kBias) + 0.5f);
        p.bump = param(kBump);
        p.cal = int(param(kCal) + 0.5f);
        p.reproLf = param(kReproLf);
        p.reproHf = param(kReproHf);
        p.mix = param(kMix);
        p.levelOutDb = param(kLevelOut);
        p.autoGain = param(kAutoGain);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kVU, dsp.vuDb());
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
    }

private:
    TapekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* TapekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Tapeki>(desc);
}
