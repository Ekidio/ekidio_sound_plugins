// Mini host: a telepített OPTEKI-t AudioComponent API-n át nyitja meg és rendereli
//   clang++ -std=c++20 Tools/au_smoketest.cpp -framework AudioToolbox -framework CoreFoundation -o build/au_smoketest && build/au_smoketest
#include <AudioToolbox/AudioToolbox.h>
#include <cstdio>
#include <cmath>
#include <vector>
static double ph = 0; static float amp = 0.5f;
static OSStatus input(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n, AudioBufferList* io) {
    for (UInt32 i = 0; i < n; ++i) { float v = amp * sin(ph); ph += 2 * M_PI * 220 / 48000; for (UInt32 c = 0; c < io->mNumberBuffers; ++c) ((float*)io->mBuffers[c].mData)[i] = v; }
    return noErr;
}
int main() {
    AudioComponentDescription d{kAudioUnitType_Effect, 'Opek', 'Ekid', 0, 0};
    AudioComponent comp = AudioComponentFindNext(nullptr, &d);
    if (!comp) { printf("nem található\n"); return 1; }
    CFStringRef name; AudioComponentCopyName(comp, &name); char buf[128]; CFStringGetCString(name, buf, 128, kCFStringEncodingUTF8); printf("Név: %s\n", buf);
    AudioUnit au; if (AudioComponentInstanceNew(comp, &au)) return 1;
    AudioStreamBasicDescription f{48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
    AURenderCallbackStruct cb{input, nullptr};
    AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    printf("Initialize: %d\n", (int)AudioUnitInitialize(au));
    CFArrayRef presets; UInt32 sz = sizeof presets;
    AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &sz);
    printf("Presetek (%ld):", CFArrayGetCount(presets));
    for (CFIndex i = 0; i < CFArrayGetCount(presets); ++i) { auto p = (const AUPreset*)CFArrayGetValueAtIndex(presets, i); CFStringGetCString(p->presetName, buf, 128, kCFStringEncodingUTF8); printf(" [%s]", buf); }
    printf("\n");
    AUPreset vocal = *(const AUPreset*)CFArrayGetValueAtIndex(presets, 2);
    AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &vocal, sizeof vocal);
    CFRelease(presets);
    float pr; AudioUnitGetParameter(au, 0, kAudioUnitScope_Global, 0, &pr); printf("'Vocal Limit' után PEAK REDUCTION = %.0f\n", pr);
    std::vector<float> L(512), R(512);
    struct { AudioBufferList abl; AudioBuffer extra; } io;
    AudioTimeStamp ts{}; ts.mFlags = kAudioTimeStampSampleTimeValid;
    for (int blk = 0; blk < 200; ++blk) {
        io.abl.mNumberBuffers = 2; io.abl.mBuffers[0] = {1, 2048, L.data()}; io.abl.mBuffers[1] = {1, 2048, R.data()};
        AudioUnitRenderActionFlags fl = 0;
        OSStatus e = AudioUnitRender(au, &fl, &ts, 0, 512, &io.abl); if (e) { printf("render hiba %d\n", (int)e); return 1; }
        ts.mSampleTime += 512;
    }
    float pk = 0; for (float v : L) pk = std::max(pk, std::fabs(v));
    float vu, gr; AudioUnitGetParameter(au, 7, kAudioUnitScope_Global, 0, &vu); AudioUnitGetParameter(au, 8, kAudioUnitScope_Global, 0, &gr);
    printf("Bemenet −6 dBFS → kimenet %.1f dBFS, GR mérő %.1f dB, VU %.1f dB\n", 20 * log10(pk), gr, vu);
    CFPropertyListRef state; sz = sizeof state;
    AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &sz);
    AudioUnitSetParameter(au, 0, kAudioUnitScope_Global, 0, 5, 0);
    AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state);
    AudioUnitGetParameter(au, 0, kAudioUnitScope_Global, 0, &pr); printf("Állapot visszatöltve: PEAK REDUCTION = %.0f\n", pr);
    CFRelease(state);
    AudioUnitUninitialize(au); AudioComponentInstanceDispose(au);
    printf("OK\n");
}
