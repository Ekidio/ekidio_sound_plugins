// EKIAUBase.h — függőség nélküli AUv2 keret (AudioComponentPlugInInterface) az EKIDIO SOUND pluginokhoz.
// Effekt (aufx) és hangszer (aumu) is. Csak nem-interleaved Float32 formátum, 1 bemeneti és 1 kimeneti busz.
// A plugin osztály ebből származik, és a prepare/reset/process (+ handleMIDI) függvényeket valósítja meg.
#pragma once
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <vector>
#include <cstring>
#include <string>
#include <cmath>
#include <algorithm>
#include <dlfcn.h>
#include <dispatch/dispatch.h>
#include "EKIDSP.h"

// EKI_UI_FACTORY: a build.sh adja meg, ha a pluginnak van saját (Cocoa) panelje
/// saját AU-tulajdonság (a panelnek): { UInt32 renderszámláló, UInt32 bypass } — a POWER LED ebből tudja, hogy a plugin dolgozik-e
constexpr AudioUnitPropertyID kEKIProperty_Activity = 64000;

#define EKI_STR2(x) #x
#define EKI_STR(x) EKI_STR2(x)

namespace eki {

struct ParamDef {
    const char* name;
    float minValue, maxValue, defaultValue;
    AudioUnitParameterUnit unit;
    std::vector<const char*> valueStrings = {};   // Indexed paraméterhez
    bool meter = false;                            // csak olvasható mérő (VU, GR)
    const char* unitName = nullptr;                // CustomUnit felirat
    bool display = false;                          // kijelzési beállítás (pl. METER): a presetek nem írják át
    bool hidden = false;                           // megszűnt kezelő: az azonosító (és a mentett érték) megmarad, de a
                                                   // paraméterlistában nem szerepel — a régi projektek így nem csúsznak el
};

struct PresetDef {
    const char* name;
    std::vector<float> values;   // paraméter-ID sorrendben (a mérők kimaradhatnak a végéről)
};

struct MIDIEvent { UInt32 offset; UInt8 status, d1, d2; };

class AudioUnitBase {
public:
    AudioUnitBase(AudioComponentInstance inst, const AudioComponentDescription& desc,
                  std::vector<ParamDef> params, std::vector<PresetDef> presets,
                  std::vector<AUChannelInfo> channelInfos)
        : mInstance(inst), mDesc(desc), mParams(std::move(params)), mPresetDefs(std::move(presets)),
          mChannelInfos(std::move(channelInfos)), mValues(mParams.size()) {
        mIsInstrument = (desc.componentType == kAudioUnitType_MusicDevice);
        for (size_t i = 0; i < mParams.size(); ++i) {
            mValues[i].store(mParams[i].defaultValue);
            mParamNames.push_back(CFStringCreateWithCString(nullptr, mParams[i].name, kCFStringEncodingUTF8));
            mUnitNames.push_back(mParams[i].unitName
                ? CFStringCreateWithCString(nullptr, mParams[i].unitName, kCFStringEncodingUTF8) : nullptr);
        }
        for (size_t i = 0; i < mPresetDefs.size(); ++i) {
            AUPreset p; p.presetNumber = SInt32(i);
            p.presetName = CFStringCreateWithCString(nullptr, mPresetDefs[i].name, kCFStringEncodingUTF8);
            mPresets.push_back(p);
        }
        mPresentPreset.presetNumber = mPresets.empty() ? -1 : 0;
        mPresentPreset.presetName = mPresets.empty() ? CFSTR("Untitled") : mPresets[0].presetName;
        CFRetain(mPresentPreset.presetName);
        UInt32 ch = 2;
        mFormat[0] = mFormat[1] = canonical(44100.0, ch);
        mInputABL = (AudioBufferList*)calloc(1, sizeof(AudioBufferList) + 8 * sizeof(AudioBuffer));
    }

    virtual ~AudioUnitBase() {
        for (auto s : mParamNames) if (s) CFRelease(s);
        for (auto s : mUnitNames) if (s) CFRelease(s);
        for (auto& p : mPresets) CFRelease(p.presetName);
        CFRelease(mPresentPreset.presetName);
        free(mInputABL);
    }

    // ---- a plugin által megvalósítandó ----
    virtual void prepare(double sampleRate, UInt32 maxFrames, UInt32 channels) = 0;
    virtual void reset() = 0;
    /// in: effektnél a bemenet (csatornánként), hangszernél nullptr. in[c] == out[c] lehet (in-place):
    /// mintánként előbb olvass a bemenetből, csak utána írj a kimenetbe.
    virtual void process(const float* const* in, float* const* out, UInt32 channels, UInt32 frames) = 0;
    /// hangszereknél: a blokkon belüli MIDI események (offset szerint rendezve)
    virtual void handleMIDI(const MIDIEvent&) {}
    virtual double tailTime() const { return 0.0; }
    virtual double latencySeconds() const { return 0.0; }

    // ---- segédek a pluginnak ----
    float param(UInt32 id) const { return mValues[id].load(std::memory_order_relaxed); }
    void setMeter(UInt32 id, float v) { mValues[id].store(v, std::memory_order_relaxed); }
    double sampleRate() const { return mFormat[1].mSampleRate; }
    bool bypassed() const { return mBypass.load(std::memory_order_relaxed); }

    // ================= AU dispatch =================

    OSStatus initialize() {
        UInt32 outCh = mFormat[1].mChannelsPerFrame;
        UInt32 inCh = mIsInstrument ? 0 : mFormat[0].mChannelsPerFrame;
        if (!mIsInstrument && mFormat[0].mSampleRate != mFormat[1].mSampleRate) return kAudioUnitErr_FormatNotSupported;
        bool ok = false;
        for (auto& ci : mChannelInfos) {
            bool inOk = ci.inChannels < 0 || UInt32(ci.inChannels) == inCh;
            bool outOk = ci.outChannels < 0 || UInt32(ci.outChannels) == outCh;
            if (ci.inChannels == -1 && ci.outChannels == -1) { inOk = true; outOk = inCh == outCh || mIsInstrument; }
            if (inOk && outOk) { ok = true; break; }
        }
        if (!ok) return kAudioUnitErr_FormatNotSupported;
        UInt32 n = std::max(inCh, outCh);
        mInBuf.assign(size_t(n) * mMaxFrames, 0.0f);
        mOutBuf.assign(size_t(n) * mMaxFrames, 0.0f);
        prepare(mFormat[1].mSampleRate, mMaxFrames, outCh);
        reset();
        mInitialized = true;
        return noErr;
    }

    OSStatus uninitialize() { mInitialized = false; return noErr; }

    OSStatus resetAU(AudioUnitScope, AudioUnitElement) {
        if (mInitialized) reset();
        mMIDICount = 0;
        return noErr;
    }

    OSStatus getPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem,
                             UInt32* outSize, Boolean* outWritable) {
        UInt32 size = 0; Boolean writable = false;
        OSStatus err = propertyInfo(id, scope, elem, size, writable);
        if (err) return err;
        if (outSize) *outSize = size;
        if (outWritable) *outWritable = writable;
        return noErr;
    }

    OSStatus getProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem,
                         void* outData, UInt32* ioSize) {
        UInt32 size = 0; Boolean writable = false;
        OSStatus err = propertyInfo(id, scope, elem, size, writable);
        if (err) return err;
        if (!outData || !ioSize) return kAudio_ParamError;
        if (*ioSize < size) return kAudioUnitErr_InvalidPropertyValue;
        *ioSize = size;
        switch (id) {
            case kAudioUnitProperty_ClassInfo:
                *(CFPropertyListRef*)outData = copyClassInfo(); return noErr;
            case kAudioUnitProperty_SampleRate:
                *(Float64*)outData = mFormat[scope == kAudioUnitScope_Input ? 0 : 1].mSampleRate; return noErr;
            case kAudioUnitProperty_StreamFormat:
                *(AudioStreamBasicDescription*)outData = mFormat[scope == kAudioUnitScope_Input ? 0 : 1]; return noErr;
            case kAudioUnitProperty_ParameterList: {
                auto ids = (AudioUnitParameterID*)outData;
                UInt32 k = 0;
                for (UInt32 i = 0; i < mParams.size() && k < size / sizeof(AudioUnitParameterID); ++i)
                    if (!mParams[i].hidden) ids[k++] = i;
                return noErr;
            }
            case kAudioUnitProperty_ParameterInfo:
                fillParamInfo(elem, *(AudioUnitParameterInfo*)outData); return noErr;
            case kAudioUnitProperty_ParameterValueStrings: {
                auto& vs = mParams[elem].valueStrings;
                CFMutableArrayRef arr = CFArrayCreateMutable(nullptr, CFIndex(vs.size()), &kCFTypeArrayCallBacks);
                for (auto s : vs) {
                    CFStringRef cs = CFStringCreateWithCString(nullptr, s, kCFStringEncodingUTF8);
                    CFArrayAppendValue(arr, cs); CFRelease(cs);
                }
                *(CFArrayRef*)outData = arr; return noErr;
            }
            case kAudioUnitProperty_ElementCount:
                *(UInt32*)outData = elementCount(scope); return noErr;
            case kAudioUnitProperty_Latency:
                *(Float64*)outData = latencySeconds(); return noErr;
            case kAudioUnitProperty_TailTime:
                *(Float64*)outData = tailTime(); return noErr;
            case kAudioUnitProperty_SupportedNumChannels:
                memcpy(outData, mChannelInfos.data(), size); return noErr;
            case kAudioUnitProperty_MaximumFramesPerSlice:
                *(UInt32*)outData = mMaxFrames; return noErr;
            case kAudioUnitProperty_LastRenderError:
                *(OSStatus*)outData = noErr; return noErr;
            case kAudioUnitProperty_BypassEffect:
                *(UInt32*)outData = mBypass.load() ? 1 : 0; return noErr;
            case kEKIProperty_Activity: {
                auto o = (UInt32*)outData;
                o[0] = mRenderCount.load(std::memory_order_relaxed); o[1] = mBypass.load() ? 1 : 0;
                return noErr;
            }
            case kAudioUnitProperty_InPlaceProcessing:
                *(UInt32*)outData = mInPlace; return noErr;
            case kAudioUnitProperty_ShouldAllocateBuffer:
                *(UInt32*)outData = 1; return noErr;
            case kAudioUnitProperty_OfflineRender:
                *(UInt32*)outData = mOffline; return noErr;
            case kAudioUnitProperty_FactoryPresets: {
                CFMutableArrayRef arr = CFArrayCreateMutable(nullptr, CFIndex(mPresets.size()), nullptr);
                for (auto& p : mPresets) CFArrayAppendValue(arr, &p);
                *(CFArrayRef*)outData = arr; return noErr;
            }
            case kAudioUnitProperty_PresentPreset: {
                AUPreset p = mPresentPreset; CFRetain(p.presetName);
                *(AUPreset*)outData = p; return noErr;
            }
            case kAudioUnitProperty_HostCallbacks:
                memcpy(outData, &mHostCallbacks, size); return noErr;
            case kAudioUnitProperty_PresentationLatency:
                *(Float64*)outData = mPresentationLatency; return noErr;
#ifdef EKI_UI_FACTORY
            case kAudioUnitProperty_CocoaUI: {
                CFURLRef url = copyBundleURL();
                if (!url) return kAudioUnitErr_InvalidProperty;
                auto info = (AudioUnitCocoaViewInfo*)outData;   // a host engedi el mindkettőt
                info->mCocoaAUViewBundleLocation = url;
                info->mCocoaAUViewClass[0] = CFStringCreateWithCString(nullptr, EKI_STR(EKI_UI_FACTORY), kCFStringEncodingUTF8);
                return noErr;
            }
#endif
            default: return kAudioUnitErr_InvalidProperty;
        }
    }

    OSStatus setProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem,
                         const void* inData, UInt32 inSize) {
        UInt32 size = 0; Boolean writable = false;
        OSStatus err = propertyInfo(id, scope, elem, size, writable);
        if (err) return err;
        if (!inData) return kAudio_ParamError;
        switch (id) {
            case kAudioUnitProperty_ClassInfo:
                if (inSize < sizeof(CFPropertyListRef)) return kAudioUnitErr_InvalidPropertyValue;
                return restoreClassInfo(*(const CFPropertyListRef*)inData);
            case kAudioUnitProperty_MakeConnection: {
                if (inSize < sizeof(AudioUnitConnection)) return kAudioUnitErr_InvalidPropertyValue;
                mConnection = *(const AudioUnitConnection*)inData;
                if (mConnection.sourceAudioUnit) mRenderCallback.inputProc = nullptr;
                return noErr;
            }
            case kAudioUnitProperty_SetRenderCallback: {
                if (inSize < sizeof(AURenderCallbackStruct)) return kAudioUnitErr_InvalidPropertyValue;
                mRenderCallback = *(const AURenderCallbackStruct*)inData;
                if (mRenderCallback.inputProc) mConnection.sourceAudioUnit = nullptr;
                return noErr;
            }
            case kAudioUnitProperty_SampleRate: {
                if (inSize < sizeof(Float64)) return kAudioUnitErr_InvalidPropertyValue;
                AudioStreamBasicDescription f = mFormat[scope == kAudioUnitScope_Input ? 0 : 1];
                f.mSampleRate = *(const Float64*)inData;
                return setFormat(scope, f);
            }
            case kAudioUnitProperty_StreamFormat:
                if (inSize < sizeof(AudioStreamBasicDescription)) return kAudioUnitErr_InvalidPropertyValue;
                return setFormat(scope, *(const AudioStreamBasicDescription*)inData);
            case kAudioUnitProperty_MaximumFramesPerSlice: {
                if (inSize < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
                UInt32 v = *(const UInt32*)inData;
                if (v == mMaxFrames) return noErr;
                if (mInitialized) return kAudioUnitErr_Initialized;
                if (v == 0 || v > 65536) return kAudioUnitErr_InvalidPropertyValue;
                mMaxFrames = v;
                notify(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0);
                return noErr;
            }
            case kAudioUnitProperty_BypassEffect: {
                if (inSize < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
                bool b = *(const UInt32*)inData != 0;
                if (b != mBypass.load()) { if (!b && mInitialized) reset(); mBypass.store(b); }
                notify(kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0);
                return noErr;
            }
            case kAudioUnitProperty_InPlaceProcessing:
                if (inSize < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
                mInPlace = *(const UInt32*)inData ? 1 : 0; return noErr;
            case kAudioUnitProperty_ShouldAllocateBuffer:
                return noErr;
            case kAudioUnitProperty_OfflineRender:
                if (inSize < sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
                mOffline = *(const UInt32*)inData ? 1 : 0; return noErr;
            case kAudioUnitProperty_PresentPreset: {
                if (inSize < sizeof(AUPreset)) return kAudioUnitErr_InvalidPropertyValue;
                const AUPreset& p = *(const AUPreset*)inData;
                if (p.presetNumber >= 0) {
                    if (size_t(p.presetNumber) >= mPresets.size()) return kAudioUnitErr_InvalidPropertyValue;
                    applyPreset(size_t(p.presetNumber));
                    setPresentPreset(mPresets[size_t(p.presetNumber)]);
                } else {
                    if (!p.presetName) return kAudioUnitErr_InvalidPropertyValue;
                    setPresentPreset(p);
                }
                notify(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
                return noErr;
            }
            case kAudioUnitProperty_HostCallbacks:
                memset(&mHostCallbacks, 0, sizeof(mHostCallbacks));
                memcpy(&mHostCallbacks, inData, std::min<size_t>(inSize, sizeof(mHostCallbacks)));
                return noErr;
            case kAudioUnitProperty_PresentationLatency:
                if (inSize < sizeof(Float64)) return kAudioUnitErr_InvalidPropertyValue;
                mPresentationLatency = *(const Float64*)inData; return noErr;
            default: return kAudioUnitErr_PropertyNotWritable;
        }
    }

    OSStatus addListener(AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, void* user) {
        mListeners.push_back({id, proc, user});
        return noErr;
    }

    OSStatus removeListener(AudioUnitPropertyID id, AudioUnitPropertyListenerProc proc, void* user, bool matchUser) {
        mListeners.erase(std::remove_if(mListeners.begin(), mListeners.end(), [&](const Listener& l) {
            return l.id == id && l.proc == proc && (!matchUser || l.user == user);
        }), mListeners.end());
        return noErr;
    }

    OSStatus addRenderNotify(AURenderCallback proc, void* user) {
        for (int i = 0; i < kMaxNotify; ++i)
            if (!mNotify[i].proc) { mNotify[i].user = user; mNotify[i].proc = proc; return noErr; }
        return kAudio_MemFullError;
    }

    OSStatus removeRenderNotify(AURenderCallback proc, void* user) {
        for (int i = 0; i < kMaxNotify; ++i)
            if (mNotify[i].proc == proc && mNotify[i].user == user) { mNotify[i].proc = nullptr; mNotify[i].user = nullptr; }
        return noErr;
    }

    OSStatus getParameter(AudioUnitParameterID id, AudioUnitScope scope, AudioUnitElement, AudioUnitParameterValue* out) {
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        if (id >= mParams.size()) return kAudioUnitErr_InvalidParameter;
        if (!out) return kAudio_ParamError;
        *out = mValues[id].load(std::memory_order_relaxed);
        return noErr;
    }

    OSStatus setParameter(AudioUnitParameterID id, AudioUnitScope scope, AudioUnitElement, AudioUnitParameterValue v, UInt32) {
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        if (id >= mParams.size()) return kAudioUnitErr_InvalidParameter;
        if (mParams[id].meter) return noErr;
        const ParamDef& d = mParams[id];
        if (!std::isfinite(v)) v = d.defaultValue;
        if (id == mLockTarget && locked()) v = 0;                  // AUTO GAIN alatt a kimeneti gain 0 dB-en zárva
        mValues[id].store(clampf(v, d.minValue, d.maxValue), std::memory_order_relaxed);
        if (id == mLockSwitch && enforceLock()) notifyParameterAsync(mLockTarget);
        return noErr;
    }

    /// zár: amíg a `sw` kapcsoló be van kapcsolva, a `target` paraméter 0 és nem állítható (AUTO GAIN → kimeneti gain)
    void lockWhileOn(AudioUnitParameterID target, AudioUnitParameterID sw) { mLockTarget = target; mLockSwitch = sw; }

    OSStatus scheduleParameters(const AudioUnitParameterEvent* events, UInt32 n) {
        for (UInt32 i = 0; i < n; ++i) {
            const auto& e = events[i];
            float v = e.eventType == kParameterEvent_Immediate ? e.eventValues.immediate.value : e.eventValues.ramp.endValue;
            OSStatus err = setParameter(e.parameter, e.scope, e.element, v, 0);
            if (err) return err;
        }
        return noErr;
    }

    OSStatus midiEvent(UInt32 status, UInt32 d1, UInt32 d2, UInt32 offset) {
        if (!mIsInstrument) return kAudioUnitErr_InvalidProperty;
        if (mMIDICount < kMaxMIDI) mMIDI[mMIDICount++] = {offset, UInt8(status), UInt8(d1 & 0x7F), UInt8(d2 & 0x7F)};
        return noErr;
    }

    OSStatus render(AudioUnitRenderActionFlags* ioFlags, const AudioTimeStamp* ts, UInt32 bus,
                    UInt32 frames, AudioBufferList* ioData) {
        if (!mInitialized) return kAudioUnitErr_Uninitialized;
        if (bus != 0) return kAudioUnitErr_InvalidElement;
        if (frames > mMaxFrames) return kAudioUnitErr_TooManyFramesToProcess;
        if (!ioData || !ts) return kAudio_ParamError;
        const UInt32 outCh = mFormat[1].mChannelsPerFrame;
        if (ioData->mNumberBuffers != outCh) return kAudioUnitErr_FormatNotSupported;

        AudioUnitRenderActionFlags localFlags = ioFlags ? *ioFlags : 0;
        callNotify(localFlags | kAudioUnitRenderAction_PreRender, ts, bus, frames, ioData);

        DenormalGuard guard;
        float* outPtr[8] = {};
        auto bindOutput = [&](UInt32 c, float* fallback) {
            if (!ioData->mBuffers[c].mData) ioData->mBuffers[c].mData = fallback;
            ioData->mBuffers[c].mDataByteSize = frames * sizeof(float);
            ioData->mBuffers[c].mNumberChannels = 1;
            outPtr[c] = (float*)ioData->mBuffers[c].mData;
        };

        OSStatus err = noErr;
        if (!mIsInstrument) {
            const UInt32 inCh = mFormat[0].mChannelsPerFrame;
            mInputABL->mNumberBuffers = inCh;
            for (UInt32 c = 0; c < inCh; ++c) {
                mInputABL->mBuffers[c].mNumberChannels = 1;
                mInputABL->mBuffers[c].mDataByteSize = frames * sizeof(float);
                mInputABL->mBuffers[c].mData = &mInBuf[size_t(c) * mMaxFrames];
            }
            AudioUnitRenderActionFlags pullFlags = 0;
            if (mRenderCallback.inputProc)
                err = mRenderCallback.inputProc(mRenderCallback.inputProcRefCon, &pullFlags, ts, 0, frames, mInputABL);
            else if (mConnection.sourceAudioUnit)
                err = AudioUnitRender(mConnection.sourceAudioUnit, &pullFlags, ts, mConnection.sourceOutputNumber, frames, mInputABL);
            else
                err = kAudioUnitErr_NoConnection;
            if (err == noErr) {
                const float* inPtr[8] = {};
                for (UInt32 c = 0; c < inCh; ++c) inPtr[c] = (const float*)mInputABL->mBuffers[c].mData;
                // a process() in-place biztos (mintánként előbb olvas, aztán ír), így a kimenet lehet a bemenő puffer
                for (UInt32 c = 0; c < outCh; ++c) {
                    float* src = (float*)inPtr[std::min(c, inCh - 1)];
                    bindOutput(c, mInPlace ? src : &mOutBuf[size_t(c) * mMaxFrames]);
                }
                if (mBypass.load(std::memory_order_relaxed)) {
                    for (UInt32 c = 0; c < outCh; ++c)
                        if (outPtr[c] != inPtr[std::min(c, inCh - 1)]) memmove(outPtr[c], inPtr[std::min(c, inCh - 1)], frames * sizeof(float));
                } else {
                    process(inPtr, outPtr, outCh, frames);
                    mRenderCount.fetch_add(1, std::memory_order_relaxed);   // életjel a panelnek
                }
            } else {
                for (UInt32 c = 0; c < outCh; ++c) bindOutput(c, &mOutBuf[size_t(c) * mMaxFrames]);
            }
        } else {
            for (UInt32 c = 0; c < outCh; ++c) bindOutput(c, &mOutBuf[size_t(c) * mMaxFrames]);
            renderInstrument(outPtr, outCh, frames);
        }

        if (err == noErr) {
            callNotify(localFlags | kAudioUnitRenderAction_PostRender, ts, bus, frames, ioData);
        } else {
            callNotify(localFlags | kAudioUnitRenderAction_PostRender | kAudioUnitRenderAction_PostRenderError, ts, bus, frames, ioData);
        }
        return err;
    }

    // ================= belső =================
protected:
    AudioComponentInstance mInstance;
    AudioComponentDescription mDesc;
    std::vector<ParamDef> mParams;
    std::vector<PresetDef> mPresetDefs;

private:
    struct Listener { AudioUnitPropertyID id; AudioUnitPropertyListenerProc proc; void* user; };
    struct Notify { AURenderCallback proc = nullptr; void* user = nullptr; };
    static constexpr int kMaxNotify = 16;
    static constexpr UInt32 kMaxMIDI = 512;
    static constexpr UInt32 kStateMagic = 0x454B4970;   // "EKIp"

    std::vector<AUChannelInfo> mChannelInfos;
    std::vector<std::atomic<float>> mValues;
    std::vector<CFStringRef> mParamNames, mUnitNames;
    std::vector<AUPreset> mPresets;
    AUPreset mPresentPreset{};
    AudioStreamBasicDescription mFormat[2]{};   // [0] input, [1] output
    UInt32 mMaxFrames = 4096;
    bool mIsInstrument = false;
    bool mInitialized = false;
    std::atomic<bool> mBypass{false};
    std::atomic<uint32_t> mRenderCount{0};
    UInt32 mInPlace = 1, mOffline = 0;
    Float64 mPresentationLatency = 0;
    HostCallbackInfo mHostCallbacks{};
    AudioUnitConnection mConnection{};
    AURenderCallbackStruct mRenderCallback{};
    AudioBufferList* mInputABL = nullptr;
    std::vector<float> mInBuf, mOutBuf;
    std::vector<Listener> mListeners;
    Notify mNotify[kMaxNotify];
    AudioUnitParameterID mLockTarget = UINT32_MAX, mLockSwitch = UINT32_MAX;
    MIDIEvent mMIDI[kMaxMIDI];
    UInt32 mMIDICount = 0;

    static void bundleAnchor() {}

    /// a .component bundle URL-je (a futó bináris helyéből: …/X.component/Contents/MacOS/X)
    static CFURLRef copyBundleURL() {
        Dl_info info{};
        if (!dladdr((const void*)&bundleAnchor, &info) || !info.dli_fname) return nullptr;
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)info.dli_fname, CFIndex(strlen(info.dli_fname)), false);
        for (int i = 0; i < 3 && url; ++i) {
            CFURLRef up = CFURLCreateCopyDeletingLastPathComponent(nullptr, url);
            CFRelease(url); url = up;
        }
        return url;
    }

    static AudioStreamBasicDescription canonical(double sr, UInt32 ch) {
        AudioStreamBasicDescription f{};
        f.mSampleRate = sr;
        f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = UInt32(kAudioFormatFlagsNativeFloatPacked) | UInt32(kAudioFormatFlagIsNonInterleaved);
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4;
        f.mChannelsPerFrame = ch; f.mBitsPerChannel = 32;
        return f;
    }

    UInt32 elementCount(AudioUnitScope scope) const {
        switch (scope) {
            case kAudioUnitScope_Global: return 1;
            case kAudioUnitScope_Input: return mIsInstrument ? 0 : 1;
            case kAudioUnitScope_Output: return 1;
            default: return 0;
        }
    }

    OSStatus checkIO(AudioUnitScope scope, AudioUnitElement elem) const {
        if (scope != kAudioUnitScope_Input && scope != kAudioUnitScope_Output) return kAudioUnitErr_InvalidScope;
        if (elem >= elementCount(scope)) return kAudioUnitErr_InvalidElement;
        return noErr;
    }

    OSStatus propertyInfo(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem, UInt32& size, Boolean& writable) {
        writable = false;
        auto global = [&]() -> OSStatus { return scope == kAudioUnitScope_Global ? OSStatus(noErr) : OSStatus(kAudioUnitErr_InvalidScope); };
        switch (id) {
            case kAudioUnitProperty_ClassInfo:
                size = sizeof(CFPropertyListRef); writable = true; return global();
            case kAudioUnitProperty_MakeConnection:
                if (mIsInstrument || scope != kAudioUnitScope_Input) return kAudioUnitErr_InvalidScope;
                if (elem != 0) return kAudioUnitErr_InvalidElement;
                size = sizeof(AudioUnitConnection); writable = true; return noErr;
            case kAudioUnitProperty_SetRenderCallback:
                if (mIsInstrument || scope != kAudioUnitScope_Input) return kAudioUnitErr_InvalidScope;
                if (elem != 0) return kAudioUnitErr_InvalidElement;
                size = sizeof(AURenderCallbackStruct); writable = true; return noErr;
            case kAudioUnitProperty_SampleRate:
            case kAudioUnitProperty_StreamFormat: {
                OSStatus e = checkIO(scope, elem); if (e) return e;
                size = id == kAudioUnitProperty_SampleRate ? sizeof(Float64) : sizeof(AudioStreamBasicDescription);
                writable = !mInitialized; return noErr;
            }
            case kAudioUnitProperty_ParameterList:
                size = scope == kAudioUnitScope_Global
                     ? UInt32(std::count_if(mParams.begin(), mParams.end(), [](const ParamDef& d) { return !d.hidden; }) * sizeof(AudioUnitParameterID)) : 0;
                return noErr;
            case kAudioUnitProperty_ParameterInfo:
                if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
                if (elem >= mParams.size()) return kAudioUnitErr_InvalidParameter;
                size = sizeof(AudioUnitParameterInfo); return noErr;
            case kAudioUnitProperty_ParameterValueStrings:
                if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
                if (elem >= mParams.size() || mParams[elem].valueStrings.empty()) return kAudioUnitErr_InvalidProperty;
                size = sizeof(CFArrayRef); return noErr;
            case kAudioUnitProperty_ElementCount:
                size = sizeof(UInt32); return noErr;
            case kAudioUnitProperty_Latency:
            case kAudioUnitProperty_TailTime:
                size = sizeof(Float64); return global();
            case kAudioUnitProperty_SupportedNumChannels:
                size = UInt32(mChannelInfos.size() * sizeof(AUChannelInfo)); return global();
            case kAudioUnitProperty_MaximumFramesPerSlice:
                size = sizeof(UInt32); writable = true; return global();
            case kAudioUnitProperty_LastRenderError:
                size = sizeof(OSStatus); return global();
            case kEKIProperty_Activity:
                size = 2 * sizeof(UInt32); return global();
            case kAudioUnitProperty_BypassEffect:
                if (mIsInstrument) return kAudioUnitErr_InvalidProperty;
                size = sizeof(UInt32); writable = true; return global();
            case kAudioUnitProperty_InPlaceProcessing:
                if (mIsInstrument) return kAudioUnitErr_InvalidProperty;
                size = sizeof(UInt32); writable = true; return global();
            case kAudioUnitProperty_ShouldAllocateBuffer: {
                OSStatus e = checkIO(scope, elem); if (e) return e;
                size = sizeof(UInt32); writable = true; return noErr;
            }
            case kAudioUnitProperty_OfflineRender:
                size = sizeof(UInt32); writable = true; return global();
            case kAudioUnitProperty_FactoryPresets:
                if (mPresets.empty()) return kAudioUnitErr_InvalidProperty;
                size = sizeof(CFArrayRef); return global();
            case kAudioUnitProperty_PresentPreset:
                size = sizeof(AUPreset); writable = true; return global();
            case kAudioUnitProperty_HostCallbacks:
                size = sizeof(HostCallbackInfo); writable = true; return global();
            case kAudioUnitProperty_PresentationLatency: {
                OSStatus e = checkIO(scope, elem); if (e) return e;
                size = sizeof(Float64); writable = true; return noErr;
            }
#ifdef EKI_UI_FACTORY
            case kAudioUnitProperty_CocoaUI:
                size = sizeof(AudioUnitCocoaViewInfo); return global();
#endif
            default: return kAudioUnitErr_InvalidProperty;
        }
    }

    OSStatus setFormat(AudioUnitScope scope, const AudioStreamBasicDescription& f) {
        const int idx = scope == kAudioUnitScope_Input ? 0 : 1;
        const AudioStreamBasicDescription& cur = mFormat[idx];
        if (f.mFormatID != kAudioFormatLinearPCM || f.mBitsPerChannel != 32 || f.mBytesPerFrame != 4 ||
            f.mFramesPerPacket != 1 || !(f.mFormatFlags & kAudioFormatFlagIsFloat) ||
            !(f.mFormatFlags & kAudioFormatFlagIsNonInterleaved) || f.mChannelsPerFrame < 1 ||
            f.mChannelsPerFrame > 8 || f.mSampleRate < 8000 || f.mSampleRate > 768000)
            return kAudioUnitErr_FormatNotSupported;
        bool chOk = false;
        for (auto& ci : mChannelInfos) {
            SInt16 want = idx == 0 ? ci.inChannels : ci.outChannels;
            if (want < 0 || UInt32(want) == f.mChannelsPerFrame) chOk = true;
        }
        if (!chOk) return kAudioUnitErr_FormatNotSupported;
        if (cur.mSampleRate == f.mSampleRate && cur.mChannelsPerFrame == f.mChannelsPerFrame) return noErr;
        if (mInitialized) return kAudioUnitErr_Initialized;
        mFormat[idx] = canonical(f.mSampleRate, f.mChannelsPerFrame);
        notify(kAudioUnitProperty_StreamFormat, scope, 0);
        notify(kAudioUnitProperty_SampleRate, scope, 0);
        return noErr;
    }

    void fillParamInfo(UInt32 id, AudioUnitParameterInfo& info) {
        const ParamDef& d = mParams[id];
        memset(&info, 0, sizeof(info));
        strlcpy(info.name, d.name, sizeof(info.name));
        info.cfNameString = mParamNames[id];
        info.unit = d.unit;
        info.unitName = mUnitNames[id];
        info.minValue = d.minValue;
        info.maxValue = d.maxValue;
        info.defaultValue = d.defaultValue;
        info.flags = kAudioUnitParameterFlag_HasCFNameString | kAudioUnitParameterFlag_IsReadable;
        if (d.meter) info.flags |= kAudioUnitParameterFlag_MeterReadOnly;
        else info.flags |= kAudioUnitParameterFlag_IsWritable;
        if (id == mLockSwitch) info.flags |= kAudioUnitParameterFlag_IsGlobalMeta;   // más paramétert is átállít (AUTO GAIN → gain 0)
    }

    void notify(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem) {
        auto copy = mListeners;
        for (auto& l : copy) if (l.id == id) l.proc(l.user, mInstance, id, scope, elem);
    }

    bool locked() const {
        return mLockSwitch < mParams.size() && mLockTarget < mParams.size() && mValues[mLockSwitch].load(std::memory_order_relaxed) >= 0.5f;
    }
    /// ha a zár él, a cél 0 lesz; true, ha változott
    bool enforceLock() {
        if (!locked() || mValues[mLockTarget].load(std::memory_order_relaxed) == 0.0f) return false;
        mValues[mLockTarget].store(0.0f, std::memory_order_relaxed);
        return true;
    }
    /// a hostok/panelek értesítése a főszálon (a setParameter jöhet a render-szálról is)
    void notifyParameterAsync(AudioUnitParameterID id) {
        auto* p = new AudioUnitParameter{mInstance, id, kAudioUnitScope_Global, 0};
        dispatch_async_f(dispatch_get_main_queue(), p, [](void* ctx) {
            auto* q = static_cast<AudioUnitParameter*>(ctx);
            AUParameterListenerNotify(nullptr, nullptr, q);
            delete q;
        });
    }

    void notifyAllParameters() {
        AudioUnitParameter p{};
        p.mAudioUnit = mInstance;
        p.mParameterID = kAUParameterListener_AnyParameter;
        p.mScope = kAudioUnitScope_Global;
        p.mElement = 0;
        AUParameterListenerNotify(nullptr, nullptr, &p);
    }

    void setPresentPreset(const AUPreset& p) {
        CFStringRef old = mPresentPreset.presetName;
        mPresentPreset.presetNumber = p.presetNumber;
        mPresentPreset.presetName = p.presetName;
        CFRetain(mPresentPreset.presetName);
        CFRelease(old);
    }

    void applyPreset(size_t idx) {
        const auto& v = mPresetDefs[idx].values;
        for (size_t i = 0; i < v.size() && i < mParams.size(); ++i)
            if (!mParams[i].meter && !mParams[i].display) mValues[i].store(clampf(v[i], mParams[i].minValue, mParams[i].maxValue));
        enforceLock();
        notifyAllParameters();
    }

    void callNotify(AudioUnitRenderActionFlags flags, const AudioTimeStamp* ts, UInt32 bus, UInt32 frames, AudioBufferList* io) {
        for (int i = 0; i < kMaxNotify; ++i) {
            AURenderCallback p = mNotify[i].proc;
            if (p) { AudioUnitRenderActionFlags f = flags; p(mNotify[i].user, &f, ts, bus, frames, io); }
        }
    }

    void renderInstrument(float* const* out, UInt32 ch, UInt32 frames) {
        // MIDI események offset szerint rendezve, a blokk felosztva az eseményeknél
        std::stable_sort(mMIDI, mMIDI + mMIDICount, [](const MIDIEvent& a, const MIDIEvent& b) { return a.offset < b.offset; });
        UInt32 pos = 0, ev = 0;
        float* sub[8];
        while (pos < frames) {
            while (ev < mMIDICount && mMIDI[ev].offset <= pos) handleMIDI(mMIDI[ev++]);
            UInt32 end = ev < mMIDICount ? std::min(frames, mMIDI[ev].offset) : frames;
            if (end <= pos) end = pos + 1;
            for (UInt32 c = 0; c < ch; ++c) sub[c] = out[c] + pos;
            process(nullptr, sub, ch, end - pos);
            pos = end;
        }
        while (ev < mMIDICount) handleMIDI(mMIDI[ev++]);
        mMIDICount = 0;
    }

    // ---- ClassInfo (állapot mentés/visszatöltés) ----
    static void dictSetInt(CFMutableDictionaryRef d, CFStringRef k, SInt32 v) {
        CFNumberRef n = CFNumberCreate(nullptr, kCFNumberSInt32Type, &v);
        CFDictionarySetValue(d, k, n); CFRelease(n);
    }
    static bool dictGetInt(CFDictionaryRef d, CFStringRef k, SInt32& v) {
        auto n = (CFNumberRef)CFDictionaryGetValue(d, k);
        return n && CFGetTypeID(n) == CFNumberGetTypeID() && CFNumberGetValue(n, kCFNumberSInt32Type, &v);
    }

    CFPropertyListRef copyClassInfo() {
        CFMutableDictionaryRef d = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        dictSetInt(d, CFSTR(kAUPresetVersionKey), 0);
        dictSetInt(d, CFSTR(kAUPresetTypeKey), SInt32(mDesc.componentType));
        dictSetInt(d, CFSTR(kAUPresetSubtypeKey), SInt32(mDesc.componentSubType));
        dictSetInt(d, CFSTR(kAUPresetManufacturerKey), SInt32(mDesc.componentManufacturer));
        // data: kStateMagic + (UInt32 id, Float32 érték) párok, big-endian
        std::vector<UInt8> bytes;
        auto put32 = [&](UInt32 v) { for (int s = 24; s >= 0; s -= 8) bytes.push_back(UInt8(v >> s)); };
        put32(kStateMagic);
        for (UInt32 i = 0; i < mParams.size(); ++i) {
            if (mParams[i].meter) continue;
            float f = mValues[i].load(); UInt32 bits; memcpy(&bits, &f, 4);
            put32(i); put32(bits);
        }
        CFDataRef data = CFDataCreate(nullptr, bytes.data(), CFIndex(bytes.size()));
        CFDictionarySetValue(d, CFSTR(kAUPresetDataKey), data); CFRelease(data);
        CFDictionarySetValue(d, CFSTR(kAUPresetNameKey), mPresentPreset.presetName);
        return d;
    }

    OSStatus restoreClassInfo(CFPropertyListRef pl) {
        if (!pl || CFGetTypeID(pl) != CFDictionaryGetTypeID()) return kAudioUnitErr_InvalidPropertyValue;
        auto d = (CFDictionaryRef)pl;
        SInt32 ver = -1, type = 0, sub = 0, manu = 0;
        if (!dictGetInt(d, CFSTR(kAUPresetVersionKey), ver) || ver != 0) return kAudioUnitErr_InvalidPropertyValue;
        if (!dictGetInt(d, CFSTR(kAUPresetTypeKey), type) || !dictGetInt(d, CFSTR(kAUPresetSubtypeKey), sub) ||
            !dictGetInt(d, CFSTR(kAUPresetManufacturerKey), manu)) return kAudioUnitErr_InvalidPropertyValue;
        if (UInt32(type) != mDesc.componentType || UInt32(sub) != mDesc.componentSubType ||
            UInt32(manu) != mDesc.componentManufacturer) return kAudioUnitErr_InvalidPropertyValue;
        auto data = (CFDataRef)CFDictionaryGetValue(d, CFSTR(kAUPresetDataKey));
        if (data && CFGetTypeID(data) == CFDataGetTypeID()) {
            const UInt8* p = CFDataGetBytePtr(data);
            CFIndex n = CFDataGetLength(data);
            auto get32 = [&](CFIndex o) { return UInt32(p[o]) << 24 | UInt32(p[o + 1]) << 16 | UInt32(p[o + 2]) << 8 | UInt32(p[o + 3]); };
            if (n >= 4 && get32(0) == kStateMagic) {
                for (CFIndex o = 4; o + 8 <= n; o += 8) {
                    UInt32 id = get32(o), bits = get32(o + 4);
                    float f; memcpy(&f, &bits, 4);
                    if (id < mParams.size() && !mParams[id].meter && std::isfinite(f))
                        mValues[id].store(clampf(f, mParams[id].minValue, mParams[id].maxValue));
                }
            }
        }
        enforceLock();
        auto name = (CFStringRef)CFDictionaryGetValue(d, CFSTR(kAUPresetNameKey));
        if (name && CFGetTypeID(name) == CFStringGetTypeID()) {
            AUPreset p{-1, name};
            setPresentPreset(p);
        }
        notifyAllParameters();
        notify(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
        return noErr;
    }
};

// ================= AudioComponentPlugInInterface glue =================

struct PlugInInstance {
    AudioComponentPlugInInterface iface;
    AudioUnitBase* unit;
    AudioComponentDescription desc;
};

template <class T>
struct Dispatch {
    static AudioUnitBase* U(void* self) { return ((PlugInInstance*)self)->unit; }

    static OSStatus Open(void* self, AudioComponentInstance inst) {
        auto pi = (PlugInInstance*)self;
        try { pi->unit = new T(inst, pi->desc); } catch (...) { return kAudio_MemFullError; }
        return noErr;
    }
    static OSStatus Close(void* self) {
        auto pi = (PlugInInstance*)self;
        delete pi->unit;
        delete pi;
        return noErr;
    }

    static OSStatus Initialize(void* s) { return U(s)->initialize(); }
    static OSStatus Uninitialize(void* s) { return U(s)->uninitialize(); }
    static OSStatus GetPropertyInfo(void* s, AudioUnitPropertyID id, AudioUnitScope sc, AudioUnitElement el, UInt32* sz, Boolean* w) {
        return U(s)->getPropertyInfo(id, sc, el, sz, w);
    }
    static OSStatus GetProperty(void* s, AudioUnitPropertyID id, AudioUnitScope sc, AudioUnitElement el, void* d, UInt32* sz) {
        return U(s)->getProperty(id, sc, el, d, sz);
    }
    static OSStatus SetProperty(void* s, AudioUnitPropertyID id, AudioUnitScope sc, AudioUnitElement el, const void* d, UInt32 sz) {
        return U(s)->setProperty(id, sc, el, d, sz);
    }
    static OSStatus AddPropertyListener(void* s, AudioUnitPropertyID id, AudioUnitPropertyListenerProc p, void* u) {
        return U(s)->addListener(id, p, u);
    }
    static OSStatus RemovePropertyListener(void* s, AudioUnitPropertyID id, AudioUnitPropertyListenerProc p) {
        return U(s)->removeListener(id, p, nullptr, false);
    }
    static OSStatus RemovePropertyListenerWithUserData(void* s, AudioUnitPropertyID id, AudioUnitPropertyListenerProc p, void* u) {
        return U(s)->removeListener(id, p, u, true);
    }
    static OSStatus AddRenderNotify(void* s, AURenderCallback p, void* u) { return U(s)->addRenderNotify(p, u); }
    static OSStatus RemoveRenderNotify(void* s, AURenderCallback p, void* u) { return U(s)->removeRenderNotify(p, u); }
    static OSStatus GetParameter(void* s, AudioUnitParameterID id, AudioUnitScope sc, AudioUnitElement el, AudioUnitParameterValue* v) {
        return U(s)->getParameter(id, sc, el, v);
    }
    static OSStatus SetParameter(void* s, AudioUnitParameterID id, AudioUnitScope sc, AudioUnitElement el, AudioUnitParameterValue v, UInt32 o) {
        return U(s)->setParameter(id, sc, el, v, o);
    }
    static OSStatus ScheduleParameters(void* s, const AudioUnitParameterEvent* e, UInt32 n) { return U(s)->scheduleParameters(e, n); }
    static OSStatus Render(void* s, AudioUnitRenderActionFlags* f, const AudioTimeStamp* ts, UInt32 bus, UInt32 n, AudioBufferList* io) {
        return U(s)->render(f, ts, bus, n, io);
    }
    static OSStatus Reset(void* s, AudioUnitScope sc, AudioUnitElement el) { return U(s)->resetAU(sc, el); }
    static OSStatus MIDIEvent(void* s, UInt32 st, UInt32 d1, UInt32 d2, UInt32 o) { return U(s)->midiEvent(st, d1, d2, o); }
    static OSStatus SysEx(void*, const UInt8*, UInt32) { return noErr; }

    static AudioComponentMethod Lookup(SInt16 sel) {
        switch (sel) {
            case kAudioUnitInitializeSelect: return (AudioComponentMethod)Initialize;
            case kAudioUnitUninitializeSelect: return (AudioComponentMethod)Uninitialize;
            case kAudioUnitGetPropertyInfoSelect: return (AudioComponentMethod)GetPropertyInfo;
            case kAudioUnitGetPropertySelect: return (AudioComponentMethod)GetProperty;
            case kAudioUnitSetPropertySelect: return (AudioComponentMethod)SetProperty;
            case kAudioUnitAddPropertyListenerSelect: return (AudioComponentMethod)AddPropertyListener;
            case kAudioUnitRemovePropertyListenerSelect: return (AudioComponentMethod)RemovePropertyListener;
            case kAudioUnitRemovePropertyListenerWithUserDataSelect: return (AudioComponentMethod)RemovePropertyListenerWithUserData;
            case kAudioUnitAddRenderNotifySelect: return (AudioComponentMethod)AddRenderNotify;
            case kAudioUnitRemoveRenderNotifySelect: return (AudioComponentMethod)RemoveRenderNotify;
            case kAudioUnitGetParameterSelect: return (AudioComponentMethod)GetParameter;
            case kAudioUnitSetParameterSelect: return (AudioComponentMethod)SetParameter;
            case kAudioUnitScheduleParametersSelect: return (AudioComponentMethod)ScheduleParameters;
            case kAudioUnitRenderSelect: return (AudioComponentMethod)Render;
            case kAudioUnitResetSelect: return (AudioComponentMethod)Reset;
            case kMusicDeviceMIDIEventSelect: return (AudioComponentMethod)MIDIEvent;
            case kMusicDeviceSysExSelect: return (AudioComponentMethod)SysEx;
            default: return nullptr;
        }
    }
};

template <class T>
void* makeFactory(const AudioComponentDescription* desc) {
    auto pi = new PlugInInstance{};
    pi->iface.Open = Dispatch<T>::Open;
    pi->iface.Close = Dispatch<T>::Close;
    pi->iface.Lookup = Dispatch<T>::Lookup;
    pi->iface.reserved = nullptr;
    pi->desc = *desc;
    return pi;
}

} // namespace eki
