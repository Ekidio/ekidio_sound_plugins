// PULTEKI — passzív csöves EQ-sor (aufx), EKIDIO SOUND: PROGRAM EQ + MID-RANGE EQ + FILTER egy pluginban, extra tiszta motorral.
// A felül kapcsolható oldalak (PAGE) csak a nézetet váltják; a jel mindig mindhárom modulon átmegy (mindegyiknek saját IN-je van).
// Alapállás: minden gomb 0 / OFF (semleges, bitre átlátszó), AUTO GAIN van, de alapban KI, ANALOG 0 (extra tiszta).
#include "EKIAUBase.h"
#include "PultekiDSP.h"

using namespace eki;

enum : UInt32 { kLowBoost, kLowAtten, kLowFreq, kHighBoost, kBandwidth, kHighFreq, kHighAtten, kAttenSel, kEqpIn,
                kMidLowG, kMidLowF, kDipG, kDipF, kMidHighG, kMidHighF, kMeqIn,
                kLowCut, kHighCut, kHlfIn,
                kOutput, kAutoGain, kAnalog, kPage, kVuColour, kAutoMakeup, kVU, kNumParams };

class Pulteki final : public AudioUnitBase {
public:
    Pulteki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"LOW BOOST", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"LOW ATTEN", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"LOW FREQ", 0, 3, 1, kAudioUnitParameterUnit_Indexed, {"20 Hz", "30 Hz", "60 Hz", "100 Hz"}},
                {"HIGH BOOST", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"BANDWIDTH", 0, 10, 5, kAudioUnitParameterUnit_Generic},
                {"HIGH FREQ", 0, 6, 4, kAudioUnitParameterUnit_Indexed, {"3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz"}},
                {"HIGH ATTEN", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"ATTEN SEL", 0, 2, 1, kAudioUnitParameterUnit_Indexed, {"5 kHz", "10 kHz", "20 kHz"}},
                {"PROGRAM EQ IN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Out", "In"}},
                {"MID PEAK LOW", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"MID LOW FREQ", 0, 4, 2, kAudioUnitParameterUnit_Indexed, {"200 Hz", "300 Hz", "500 Hz", "700 Hz", "1 kHz"}},
                {"MID DIP", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"DIP FREQ", 0, 10, 4, kAudioUnitParameterUnit_Indexed,
                    {"200 Hz", "300 Hz", "500 Hz", "700 Hz", "1 kHz", "1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz", "7 kHz"}},
                {"MID PEAK HIGH", 0, 10, 0, kAudioUnitParameterUnit_Generic},
                {"MID HIGH FREQ", 0, 4, 2, kAudioUnitParameterUnit_Indexed, {"1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz"}},
                {"MID-RANGE EQ IN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Out", "In"}},
                {"LOW CUT", 0, 10, 0, kAudioUnitParameterUnit_Indexed,
                    {"Off", "50 Hz", "80 Hz", "100 Hz", "150 Hz", "250 Hz", "500 Hz", "750 Hz", "1 kHz", "1.5 kHz", "2 kHz"}},
                {"HIGH CUT", 0, 10, 10, kAudioUnitParameterUnit_Indexed,
                    {"1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz", "6 kHz", "8 kHz", "10 kHz", "12 kHz", "15 kHz", "Off"}},
                {"FILTER IN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Out", "In"}},
                {"OUTPUT", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
                {"AUTO GAIN", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"ANALOG", 0, 100, 0, kAudioUnitParameterUnit_Percent},
                {"PAGE", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Program EQ", "Mid-Range EQ", "Filter"}, false, nullptr, true},
                {"VU COLOUR", 0, 2, 1, kAudioUnitParameterUnit_Indexed, {"Black", "Red", "Blue"}, false, nullptr, true},   // alap: piros
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                //                    LB  LA LF  HB  BW HF  HA AS IN  ML MLF DIP DF  MH MHF IN  LC HC IN  OUT AUTO ANLG
                {"Default",          {0,  0, 1,  0,  5, 4,  0, 1, 1,  0, 2,  0,  4,  0, 2,  1,  0, 10, 1, 0,  0,   0}},
                {"Kick Trick",       {6,  4, 2,  0,  5, 4,  0, 1, 1,  0, 2,  0,  4,  0, 2,  1,  0, 10, 1, 0,  0,   0}},
                {"Bass Weight",      {5,  2, 1,  0,  5, 4,  0, 1, 1,  0, 2,  2,  1,  0, 2,  1,  0, 10, 1, 0,  0,   0}},
                {"Air Lift",         {0,  0, 1,  4,  8, 6,  1, 2, 1,  0, 2,  0,  4,  0, 2,  1,  0, 10, 1, 0,  0,   0}},
                {"Vocal Presence",   {0,  0, 1,  2,  6, 4,  0, 1, 1,  0, 2,  2,  1,  3, 2,  1,  1, 10, 1, 0,  0,   0}},
                {"Master Sheen",     {3,  1, 1,  2,  9, 5,  1, 2, 1,  0, 2,  0,  4,  0, 2,  1,  0, 10, 1, 0,  0,   0}},
                {"Mid Scoop",        {0,  0, 1,  0,  5, 4,  0, 1, 1,  0, 2,  4,  2,  2, 2,  1,  0, 10, 1, 0,  0,   0}},
                {"Telephone",        {0,  0, 1,  0,  5, 4,  0, 1, 1,  4, 4,  0,  4,  0, 2,  1,  6,  2, 1, 0,  0,   0}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kOutput, kAutoGain);   // AUTO GAIN alatt a kimenet 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    double latencySeconds() const override { return dsp.latencySeconds(); }
    void reset() override { dsp.reset(); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        PultekiParams p;
        p.lowBoost = param(kLowBoost); p.lowAtten = param(kLowAtten); p.lowFreq = idx(kLowFreq);
        p.highBoost = param(kHighBoost); p.bandwidth = param(kBandwidth); p.highFreq = idx(kHighFreq);
        p.highAtten = param(kHighAtten); p.attenSel = idx(kAttenSel); p.eqpIn = param(kEqpIn);
        p.midLowG = param(kMidLowG); p.midLowF = idx(kMidLowF); p.dipG = param(kDipG); p.dipF = idx(kDipF);
        p.midHighG = param(kMidHighG); p.midHighF = idx(kMidHighF); p.meqIn = param(kMeqIn);
        p.lowCut = idx(kLowCut); p.highCut = idx(kHighCut); p.hlfIn = param(kHlfIn);
        p.outDb = param(kOutput); p.autoGain = param(kAutoGain); p.analog = param(kAnalog);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
        setMeter(kVU, dsp.vuDb());
    }

private:
    int idx(UInt32 id) { return int(param(id) + 0.5f); }
    PultekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* PultekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Pulteki>(desc);
}
