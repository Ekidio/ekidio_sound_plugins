// MASTEQUI — háromsávos mastering EQ két polccal (aufx), EKIDIO SOUND. A referencia-mérésekből illesztett görbék.
// Alapállás: minden 0 (semleges), AUTO GAIN van, de alapban KI (a user döntése, 2026-10-22).
#include "EKIAUBase.h"
#include "MastequiDSP.h"

using namespace eki;

enum : UInt32 { kLowShelf, kLowGain, kLowFreq, kLowQ, kMidGain, kMidFreq, kMidQ, kHighGain, kHighFreq, kHighQ, kHighShelf,
                kLowShift, kRange, kOutput, kAutoGain, kAutoMakeup, kNumParams };

class Mastequi final : public AudioUnitBase {
public:
    Mastequi(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"LOW SHELF", -12, 12, 0, kAudioUnitParameterUnit_Generic},
                {"LOW GAIN", -12, 12, 0, kAudioUnitParameterUnit_Generic},
                {"LOW FREQ", 0, 23, 12, kAudioUnitParameterUnit_Indexed, {"11 Hz", "13 Hz", "15 Hz", "18 Hz", "20 Hz", "25 Hz", "30 Hz", "36 Hz", "42 Hz", "50 Hz", "60 Hz", "71 Hz",
                                                                          "85 Hz", "100 Hz", "120 Hz", "140 Hz", "170 Hz", "200 Hz", "240 Hz", "280 Hz", "340 Hz", "400 Hz", "450 Hz", "570 Hz"}},
                {"LOW Q", 0, 4, 1, kAudioUnitParameterUnit_Indexed, {"1", "2", "3", "4", "5"}},
                {"MID GAIN", -12, 12, 0, kAudioUnitParameterUnit_Generic},
                {"MID FREQ", 0, 23, 12, kAudioUnitParameterUnit_Indexed, {"125 Hz", "150 Hz", "180 Hz", "210 Hz", "250 Hz", "300 Hz", "350 Hz", "420 Hz", "500 Hz", "600 Hz", "700 Hz", "840 Hz",
                                                                          "1 kHz", "1.2 kHz", "1.4 kHz", "1.7 kHz", "2 kHz", "2.4 kHz", "2.8 kHz", "3.4 kHz", "4 kHz", "4.5 kHz", "5.6 kHz", "6.2 kHz"}},
                {"MID Q", 0, 4, 1, kAudioUnitParameterUnit_Indexed, {"1", "2", "3", "4", "5"}},
                {"HIGH GAIN", -12, 12, 0, kAudioUnitParameterUnit_Generic},
                {"HIGH FREQ", 0, 23, 12, kAudioUnitParameterUnit_Indexed, {"3.4 kHz", "3.7 kHz", "4 kHz", "4.4 kHz", "4.8 kHz", "5.2 kHz", "5.7 kHz", "6.2 kHz", "6.8 kHz", "7.4 kHz", "8.1 kHz", "8.8 kHz",
                                                                           "9.5 kHz", "10 kHz", "11 kHz", "12 kHz", "13 kHz", "14 kHz", "16 kHz", "17 kHz", "19 kHz", "21 kHz", "23 kHz", "25 kHz"}},
                {"HIGH Q", 0, 4, 1, kAudioUnitParameterUnit_Indexed, {"1", "2", "3", "4", "5"}},
                {"HIGH SHELF", -12, 12, 0, kAudioUnitParameterUnit_Generic},
                {"LOW SHIFT", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"RANGE", 0, 2, 2, kAudioUnitParameterUnit_Indexed, {"6 dB", "9 dB", "12 dB"}},
                {"OUTPUT", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"AUTO GAIN", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                // alapállás: minden 0, AUTO GAIN ki (a user döntése)
                //                    LSH  LG LF LQ  MG MF MQ  HG HF HQ  HSH SHIFT RNG OUT AUTO
                {"Default",          {0,   0, 12, 1,  0, 12, 1,  0, 12, 1,  0,  0,    2,  0,  0}},
                {"Air & Weight",     {2,   0, 12, 1,  0, 12, 1,  0, 12, 1,  3,  0,    2,  0,  0}},
                {"Silk Top",         {0,   0, 12, 1,  0, 12, 1,  3, 18, 0,  0,  0,    2,  0,  0}},
                {"Vocal Presence",   {0,   0, 12, 1,  2, 19, 1,  0, 12, 1,  0,  0,    2,  0,  0}},
                {"Low Mid Clean",    {0,   0, 12, 1, -2,  5, 2,  0, 12, 1,  0,  0,    2,  0,  0}},
                {"Sub Tighten",      {0,  -2,  6, 2,  0, 12, 1,  0, 12, 1,  0,  0,    2,  0,  0}},
                {"Warm Master",      {2,  -1, 18, 1,  0, 12, 1,  0, 12, 1,  1,  1,    2,  0,  0}},
                {"Master Polish",    {1,   0, 12, 1, -1,  8, 1,  2, 18, 1,  1,  0,    2,  0,  0}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kOutput, kAutoGain);   // AUTO GAIN alatt a kimenet 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    double latencySeconds() const override { return dsp.latencySeconds(); }
    void reset() override { dsp.reset(); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        MastequiParams p;
        p.lowShelf = param(kLowShelf); p.highShelf = param(kHighShelf);
        const UInt32 g[3] = {kLowGain, kMidGain, kHighGain}, f[3] = {kLowFreq, kMidFreq, kHighFreq}, q[3] = {kLowQ, kMidQ, kHighQ};
        for (int b = 0; b < 3; ++b) { p.gain[b] = param(g[b]); p.freq[b] = int(param(f[b]) + 0.5f); p.q[b] = int(param(q[b]) + 0.5f); }
        p.lowShift = param(kLowShift);
        p.range = int(param(kRange) + 0.5f);
        p.outDb = param(kOutput);
        p.autoGain = param(kAutoGain);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
    }

private:
    MastequiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* MastequiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Mastequi>(desc);
}
