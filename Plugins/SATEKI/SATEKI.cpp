// SATEKI — MULTI TUBE SATURATION: három lágy levegő-egység (LOW 7k / MID 10k / HIGH 14k), aufx, EKIDIO SOUND
#include "EKIAUBase.h"
#include "SatekiDSP.h"

using namespace eki;

enum : UInt32 { kLowDrive, kMidDrive, kHighDrive, kLowFreq, kMidFreq, kHighFreq, kLevelIn, kLevelOut, kMix, kAutoGain,
                kSatVU, kAutoMakeup, kNumParams };

class Sateki final : public AudioUnitBase {
public:
    Sateki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"LOW DRIVE", 0, 10, 5, kAudioUnitParameterUnit_Generic},
                {"MID DRIVE", 0, 10, 5, kAudioUnitParameterUnit_Generic},
                {"HIGH DRIVE", 0, 10, 5, kAudioUnitParameterUnit_Generic},
                {"LOW FREQ", 0, 3, 1, kAudioUnitParameterUnit_Indexed, {"5 kHz", "6 kHz", "7 kHz", "8 kHz"}},
                {"MID FREQ", 0, 3, 1, kAudioUnitParameterUnit_Indexed, {"9 kHz", "10 kHz", "11 kHz", "12 kHz"}},
                {"HIGH FREQ", 0, 3, 3, kAudioUnitParameterUnit_Indexed, {"13 kHz", "14 kHz", "15 kHz", "16 kHz"}},
                {"LEVEL IN", -12, 12, 12, kAudioUnitParameterUnit_Decibels},
                {"LEVEL OUT", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"MIX", 0, 100, 60, kAudioUnitParameterUnit_Percent},
                {"AUTO GAIN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"SATURATION", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // AUTO GAIN mindenhol be (EKI-szabály)
                //                    LOW MID HIGH  LF MF HF  IN OUT MIX AUTO
                {"Default",          {5,   5,  5,    1, 1, 3, 12, 0, 60,  1}},   // a user jóváhagyott beállítása (2026-10-19)
                {"Silk Air",         {0,   3,  4,    2, 1, 1,  0, 0, 100, 1}},
                {"Vocal Presence",   {4,   3,  2,    2, 1, 1,  0, 0, 100, 1}},
                {"Upper Sheen",      {6,   4,  0,    1, 1, 1,  0, 0, 100, 1}},
                {"Hats & Air",       {0,   4,  7,    2, 2, 1,  0, 0, 100, 1}},
                {"Full Air",         {5,   6,  6,    2, 1, 1,  0, 0, 100, 1}},
                {"Gentle Top",       {0,   2,  2,    2, 1, 2,  0, 0, 100, 1}},
                {"Air Max",          {8,   8,  8,    1, 1, 1,  0, 0, 100, 1}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kLevelOut, kAutoGain);   // AUTO GAIN alatt a kimenet 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kSatVU, -20); setMeter(kAutoMakeup, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        SatekiParams p;
        for (int k = 0; k < 3; ++k) { p.drive[k] = param(kLowDrive + k); p.freq[k] = int(param(kLowFreq + k) + 0.5f); }
        p.levelInDb = param(kLevelIn);
        p.levelOutDb = param(kLevelOut);
        p.mix = param(kMix);
        p.autoGain = param(kAutoGain);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kSatVU, dsp.vuDb());
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
    }

private:
    SatekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* SatekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Sateki>(desc);
}
