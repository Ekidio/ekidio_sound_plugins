// Presetenként a szükséges makeup mérése: AUTO GAIN bekapcsolva, tipikus lüktető jel (csúcs −6 dBFS), 8 s után
// az AUTO MAKEUP mérő értéke — ezt írjuk a preset kimeneti erősítésébe, ha az AUTO GAIN alapból ki van kapcsolva.
//   clang++ -std=c++20 Tools/preset_makeup.cpp -framework AudioToolbox -framework CoreFoundation -o build/preset_makeup
#include <AudioToolbox/AudioToolbox.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
static double ph = 0, t = 0;
static OSStatus input(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n, AudioBufferList* io) {
    for (UInt32 i = 0; i < n; ++i) {
        double env = 0.15 + 0.85 * std::pow(0.5 + 0.5 * std::sin(2 * M_PI * 4 * t), 3.0);
        float v = float(0.5 * env * std::sin(ph)); ph += 2 * M_PI * 220 / 48000; t += 1.0 / 48000;
        for (UInt32 c = 0; c < io->mNumberBuffers; ++c) ((float*)io->mBuffers[c].mData)[i] = v;
    }
    return noErr;
}
struct P { const char* name; OSType sub; int autoId, makeupMeterId, gainId; };
int main(int argc, char** argv) {
    std::string only = argc > 1 ? argv[1] : "";
    P plugs[] = {{"OPTEKI", 'Opek', 9, 10, 1}, {"VARIEKI", 'Vrek', 10, 16, 9}, {"FETEKI", 'Ftek', 5, 11, 1}, {"BUSEKI", 'Bsek', 6, 12, 1}, {"APEKI", 'Apek', 8, 15, 7}, {"DBEKI", 'Dbek', 4, 10, 2}, {"TUBEKI", 'Tbek', 6, 12, 5}, {"FAIREKI", 'Frek', 6, 12, 5}};
    for (auto& pl : plugs) {
        if (!only.empty() && only != pl.name) continue;
        AudioComponentDescription d{kAudioUnitType_Effect, pl.sub, 'Ekid', 0, 0};
        AudioUnit au; AudioComponentInstanceNew(AudioComponentFindNext(nullptr, &d), &au);
        AudioStreamBasicDescription f{48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        AURenderCallbackStruct cb{input, nullptr};
        AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        AudioUnitInitialize(au);
        CFArrayRef arr; UInt32 sz = sizeof(arr);
        AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &arr, &sz);
        printf("%s\n", pl.name);
        for (CFIndex k = 0; k < CFArrayGetCount(arr); ++k) {
            AUPreset p = *(const AUPreset*)CFArrayGetValueAtIndex(arr, k);
            AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, sizeof p);
            AudioUnitReset(au, kAudioUnitScope_Global, 0);
            AudioUnitSetParameter(au, pl.autoId, kAudioUnitScope_Global, 0, 1, 0);
            float g0; AudioUnitGetParameter(au, pl.gainId, kAudioUnitScope_Global, 0, &g0);
            std::vector<float> L(512), R(512); struct { AudioBufferList abl; AudioBuffer x; } io;
            AudioTimeStamp ts{}; ts.mFlags = kAudioTimeStampSampleTimeValid; t = 0; ph = 0;
            for (int b = 0; b < 48000 * 8 / 512; ++b) {
                io.abl.mNumberBuffers = 2; io.abl.mBuffers[0] = {1, 2048, L.data()}; io.abl.mBuffers[1] = {1, 2048, R.data()};
                AudioUnitRenderActionFlags fl = 0; AudioUnitRender(au, &fl, &ts, 0, 512, &io.abl); ts.mSampleTime += 512;
            }
            float mk; AudioUnitGetParameter(au, pl.makeupMeterId, kAudioUnitScope_Global, 0, &mk);
            char nm[64]; CFStringGetCString(p.presetName, nm, 64, kCFStringEncodingUTF8);
            printf("  %-16s preset-gain %+5.1f  auto %+5.1f  → új gain %+5.1f\n", nm, g0, mk, std::round((g0 + mk) * 2) / 2);
        }
        CFRelease(arr);
        AudioUnitUninitialize(au); AudioComponentInstanceDispose(au);
    }
}
