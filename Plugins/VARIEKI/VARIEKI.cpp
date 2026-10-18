// VARIEKI — precíz, kettős detektoros kompresszor opcionális vari-mu színnel (aufx), EKIDIO SOUND
#include "EKIAUBase.h"
#include "VariekiDSP.h"

using namespace eki;

enum : UInt32 { kThreshold, kCrest, kRatio, kKnee, kAttack, kRelPeak, kRelRms, kStereo, kScCut, kMakeup,
                kAutoGain, kAnalog, kMix, kMeterMode, kVU, kGR, kAutoMakeup, kGRNeedle, kOutVU, kNumParams };

class Varieki final : public AudioUnitBase {
public:
    Varieki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc,
            {
                {"THRESHOLD", -40, 0, -30, kAudioUnitParameterUnit_Decibels},
                {"PEAK CREST", 0, 20, 6, kAudioUnitParameterUnit_Decibels},
                {"RATIO", 1, 20, 1.6f, kAudioUnitParameterUnit_Ratio},
                {"SOFT KNEE", 0, 24, 12, kAudioUnitParameterUnit_Decibels},
                {"ATTACK", 0.1f, 100, 50, kAudioUnitParameterUnit_Milliseconds},
                {"RELEASE PEAK", 5, 500, 80, kAudioUnitParameterUnit_Milliseconds},
                {"RELEASE RMS", 20, 2000, 1000, kAudioUnitParameterUnit_Milliseconds},
                {"STEREO SENS", 0, 100, 0, kAudioUnitParameterUnit_Percent},
                {"SC LOW CUT", 0, 4, 1, kAudioUnitParameterUnit_Indexed, {"Off", "40 Hz", "80 Hz", "120 Hz", "200 Hz"}},
                {"MAKEUP", -12, 24, 0, kAudioUnitParameterUnit_Decibels},
                {"AUTO GAIN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
                {"ANALOG", 0, 100, 85, kAudioUnitParameterUnit_Percent},
                {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
                {"METER", 0, 2, 0, kAudioUnitParameterUnit_Indexed, {"Gain Reduction", "Output +4", "Output +10"}, false, nullptr, true},
                {"VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GAIN REDUCTION", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"GR METER", -20, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true},
                {"OUTPUT VU", -20, 3, -20, kAudioUnitParameterUnit_Decibels, {}, true},
            },
            {
                //                  THR  CREST RATIO KNEE ATK  RELP RELR  ST  CUT MK AUTO ANLG MIX METER
                {"Default",         {-30, 6, 1.6f, 12, 50, 80, 1000, 0, 1, 0, 1, 85, 100, 0}},
                {"Velvet Bus",      {-20, 6, 2, 10, 15, 60, 350, 15, 2, 4.5f, 0, 45, 100, 0}},
                {"Vocal Silk",      {-26, 9, 3, 6, 5, 40, 150, 0, 2, 9.5f, 0, 40, 100, 0}},
                {"Thick Drums",     {-28, 3, 4, 6, 3, 30, 120, 0, 1, 13, 0, 70, 70, 0}},
                {"Bass Weight",     {-22, 6, 3, 8, 20, 80, 300, 0, 0, 7, 0, 60, 100, 0}},
                {"Vari Glow",       {-24, 6, 2.5f, 12, 20, 80, 600, 0, 1, 8, 0, 85, 100, 1}},
                {"Breathing Room",  {-30, 4, 6, 6, 1, 25, 90, 0, 2, 15.5f, 0, 55, 50, 0}},
                {"Clean Master",    {-14, 8, 1.5f, 12, 20, 60, 400, 0, 1, 1.5f, 0, 0, 100, 0}},
            },
            {{1, 1}, {2, 2}}) {
        lockWhileOn(kMakeup, kAutoGain);   // AUTO GAIN alatt a kimeneti gain 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, -20); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); setMeter(kGRNeedle, 0); setMeter(kOutVU, -20); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        VariekiParams p;
        p.thresholdDb = param(kThreshold);
        p.crestDb = param(kCrest);
        p.ratio = param(kRatio);
        p.kneeDb = param(kKnee);
        p.attackMs = param(kAttack);
        p.relPeakMs = param(kRelPeak);
        p.relRmsMs = param(kRelRms);
        p.stereoSens = param(kStereo);
        p.scLowCut = int(param(kScCut) + 0.5f);
        p.makeupDb = param(kMakeup);
        p.autoGain = param(kAutoGain);
        p.analog = param(kAnalog);
        p.mix = param(kMix);
        p.meterMode = int(param(kMeterMode) + 0.5f);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kVU, clampf(dsp.vuDb(), -20, 3));
        setMeter(kGR, clampf(dsp.gainReductionDb(), -40, 0));
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
        setMeter(kGRNeedle, clampf(dsp.grNeedleDb(), -20, 0));
        setMeter(kOutVU, clampf(dsp.outVuDb(), -20, 3));
    }

private:
    VariekiDSP dsp;
};

extern "C" __attribute__((visibility("default"))) void* VariekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Varieki>(desc);
}
