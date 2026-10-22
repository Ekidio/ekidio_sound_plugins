// DYNEKI-6 — hatsávos karakteres csatorna-EQ (BRIT / PUNCH), M/S kettős EQ és egygombos, ultra tiszta automata COMP (aufx), EKIDIO SOUND
// 2026-10-23 (a user nullázása): a SAT megszűnt (az azonosítója rejtve megmarad), minden gain 0, AIR +5 dB 20 kHz-en,
// COMP ≈ 1–2 dB GR egy átlagos mixen, ANALOG alapból KI; az AUTO GAIN alapból BE marad (a user döntése).
#include "EKIAUBase.h"
#include "DynekiDSP.h"
#include <array>
#include <cstring>

using namespace eki;

// az EQ-készlet paraméterei (L/R vagy MID), utánuk a közösek, majd a SIDE készlet
enum : UInt32 {
    kHpf, kLowG, kLowF, kLmG, kLmF, kHmG, kHmF, kHighG, kHighF, kAirG, kAirF,
    kVoice, kMode, kSat, kComp, kOutput, kAutoGain, kAnalog, kLmQ, kHmQ, kMix,
    kSHpf, kSLowG, kSLowF, kSLmG, kSLmF, kSHmG, kSHmF, kSHighG, kSHighF, kSAirG, kSAirF,
    kEdit, kVU, kGR, kAutoMakeup, kNumParams
};

namespace {

constexpr float COMP_DEFAULT = 3;   // ≈ 1–2 dB GR egy átlagos mixen (mérve)

/// main = a fő (L/R vagy MID) készlet: minden gain 0, csak az AIR +5 dB (20 kHz); a SIDE készlet sima
std::vector<ParamDef> eqSetParams(const char* pfx, bool main) {
    // a nevek a plugin élettartamáig élnek (ParamDef csak mutatót tárol)
    auto n = [&](const char* s) -> const char* { return strdup((std::string(pfx) + s).c_str()); };
    return {
        {n("HPF"), 0, 4, 0, kAudioUnitParameterUnit_Indexed, {"Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz"}},
        {n("LOW GAIN"), -16, 16, 0, kAudioUnitParameterUnit_Decibels},
        {n("LOW FREQ"), 0, 4, 2, kAudioUnitParameterUnit_Indexed, {"15 Hz", "35 Hz", "60 Hz", "110 Hz", "220 Hz"}},
        {n("LOW MID GAIN"), -16, 16, 0, kAudioUnitParameterUnit_Decibels},
        {n("LOW MID FREQ"), 0, 4, 1, kAudioUnitParameterUnit_Indexed, {"200 Hz", "400 Hz", "800 Hz", "1.5 kHz", "3 kHz"}},
        {n("HIGH MID GAIN"), -16, 16, 0, kAudioUnitParameterUnit_Decibels},
        {n("HIGH MID FREQ"), 0, 4, 2, kAudioUnitParameterUnit_Indexed, {"1.5 kHz", "3 kHz", "5 kHz", "7 kHz", "10 kHz"}},
        {n("HIGH GAIN"), -16, 16, 0, kAudioUnitParameterUnit_Decibels},
        {n("HIGH FREQ"), 0, 3, 2, kAudioUnitParameterUnit_Indexed, {"8 kHz", "10 kHz", "12 kHz", "16 kHz"}},
        {n("AIR GAIN"), 0, 15, main ? 5.0f : 0.0f, kAudioUnitParameterUnit_Decibels},
        {n("AIR FREQ"), 0, 4, 3, kAudioUnitParameterUnit_Indexed, {"2.5 kHz", "5 kHz", "10 kHz", "20 kHz", "40 kHz"}},
    };
}

std::vector<ParamDef> allParams() {
    std::vector<ParamDef> v = eqSetParams("", true);
    std::vector<ParamDef> common = {
        {"VOICE", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Brit", "Punch"}},
        {"MODE", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"L/R", "M/S"}},
        {"SAT", 0, 10, 0, kAudioUnitParameterUnit_Generic, {}, false, nullptr, false, true},   // megszűnt (rejtett)
        {"COMP", 0, 10, COMP_DEFAULT, kAudioUnitParameterUnit_Generic},
        {"OUTPUT", -12, 12, 0, kAudioUnitParameterUnit_Decibels},
        {"AUTO GAIN", 0, 1, 1, kAudioUnitParameterUnit_Indexed, {"Off", "On"}},
        {"ANALOG", 0, 100, 0, kAudioUnitParameterUnit_Percent},
        {"LOW MID Q", 0, 4, 0, kAudioUnitParameterUnit_Indexed, {"Auto", "0.5", "1.0", "2.0", "4.0"}},
        {"HIGH MID Q", 0, 4, 0, kAudioUnitParameterUnit_Indexed, {"Auto", "0.5", "1.0", "2.0", "4.0"}},
        {"MIX", 0, 100, 100, kAudioUnitParameterUnit_Percent},
    };
    v.insert(v.end(), common.begin(), common.end());
    auto side = eqSetParams("SIDE ", false);
    v.insert(v.end(), side.begin(), side.end());
    v.push_back({"EDIT", 0, 1, 0, kAudioUnitParameterUnit_Indexed, {"Mid", "Side"}, false, nullptr, true});
    v.push_back({"VU", -20, 3, 0, kAudioUnitParameterUnit_Decibels, {}, true});
    v.push_back({"GAIN REDUCTION", -40, 0, 0, kAudioUnitParameterUnit_Decibels, {}, true});
    v.push_back({"AUTO MAKEUP", -40, 40, 0, kAudioUnitParameterUnit_Decibels, {}, true});
    return v;
}

/// preset: EQ-készlet = {HPF, LOW G, LOW F, LM G, LM F, HM G, HM F, HIGH G, HIGH F, AIR G, AIR F}
using Set = std::array<float, 11>;
constexpr Set kFlat = {0, 0, 2, 0, 1, 0, 2, 0, 2, 0, 3};
PresetDef preset(const char* name, Set a, float voice, float mode, float sat, float comp, Set b = kFlat,
                 float analog = 60, float mix = 100, float autoGain = 1) {
    std::vector<float> v(a.begin(), a.end());
    //                VOICE  MODE  SAT  COMP OUT AUTO ANALOG LMQ HMQ MIX
    for (float x : {voice, mode, sat, comp, 0.0f, autoGain, analog, 0.0f, 0.0f, mix}) v.push_back(x);
    v.insert(v.end(), b.begin(), b.end());
    return {name, v};
}

std::vector<PresetDef> allPresets() {
    //               HPF  LOW G F    LM G  F    HM G  F    HIGH G F   AIR G F
    return {
        preset("Default",         {0,   0,   2,   0,   1,   0,    2,   0,   2,   5,   3}, 1, 0, 0, COMP_DEFAULT, kFlat, 0, 100, 1),   // a user nullázása (2026-10-23)
        preset("EKI Classic",     {0,   6,   2,   0,   1,   0,    2,   2,   0,   5,   3}, 1, 0, 0, 5),   // a korábbi alapbeállítás (2026-10-19)
        preset("Vocal Presence",  {2,   0,   2,   -2,  1,   2.5f, 1,   0,   2,   4,   3}, 0, 0, 3, 4),
        preset("Drum Bus Punch",  {0,   3,   2,   -2.5f, 1, 2,    2,   1.5f, 1,  0,   3}, 1, 0, 4, 5),
        preset("Mix Bus Air",     {0,   1,   1,   0,   1,   0,    2,   0,   2,   3,   4}, 0, 0, 2, 3),
        preset("Bass Weight",     {1,   4,   3,   -2,  1,   2,    0,   0,   2,   0,   3}, 0, 0, 5, 4),
        preset("Acoustic Sparkle",{2,   -1.5f, 3, -2,  0,   2,    2,   0,   2,   3,   2}, 1, 0, 2, 3),
        preset("M/S Master Width",{0,   1,   2,   0,   1,   -0.5f, 1,  0,   2,   1,   3}, 0, 1, 1.5f, 2,
               {3,   0,   2,   0,   1,   0,    2,   2,   2,   3,   3}),
        preset("Lo-Fi Dirt",      {4,   0,   2,   3,   2,   0,    2,   -6,  0,   0,   3}, 1, 0, 9, 7),
    };
}

} // namespace

class Dyneki final : public AudioUnitBase {
public:
    Dyneki(AudioComponentInstance inst, const AudioComponentDescription& desc)
        : AudioUnitBase(inst, desc, allParams(), allPresets(), {{1, 1}, {2, 2}}) {
        lockWhileOn(kOutput, kAutoGain);   // AUTO GAIN alatt a kimenet 0 dB-en zárva
    }

    void prepare(double sampleRate, UInt32, UInt32) override { dsp.prepare(float(sampleRate)); }
    void reset() override { dsp.reset(); setMeter(kVU, 0); setMeter(kGR, 0); setMeter(kAutoMakeup, 0); }

    void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) override {
        DynekiParams p;
        readSet(p.a, kHpf);
        readSet(p.b, kSHpf);
        p.voice = idx(kVoice);
        p.ms = idx(kMode);
        p.comp = param(kComp);
        p.outputDb = param(kOutput);
        p.autoGain = param(kAutoGain);
        p.analog = param(kAnalog);
        p.lmQ = idx(kLmQ);
        p.hmQ = idx(kHmQ);
        p.mix = param(kMix);
        dsp.process(in, out, int(channels), int(frames), p);
        setMeter(kVU, clampf(dsp.vuDb(), -20, 3));
        setMeter(kGR, clampf(dsp.gainReductionDb(), -40, 0));
        setMeter(kAutoMakeup, clampf(dsp.autoMakeupDb(), -40, 40));
    }

private:
    DynekiDSP dsp;

    int idx(UInt32 id) { return int(param(id) + 0.5f); }
    void readSet(EqSet& s, UInt32 base) {
        s.hpf = idx(base + 0);
        s.lowG = param(base + 1); s.lowF = idx(base + 2);
        s.lmG = param(base + 3); s.lmF = idx(base + 4);
        s.hmG = param(base + 5); s.hmF = idx(base + 6);
        s.highG = param(base + 7); s.highF = idx(base + 8);
        s.airG = param(base + 9); s.airF = idx(base + 10);
    }
};

extern "C" __attribute__((visibility("default"))) void* DynekiFactory(const AudioComponentDescription* desc) {
    return makeFactory<Dyneki>(desc);
}
