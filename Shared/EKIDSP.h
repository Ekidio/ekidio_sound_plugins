// EKIDSP.h — közös, valós idejű DSP építőkockák az EKIDIO SOUND pluginokhoz.
// A GROOVEKI DSPCore.swift C++ portja + oversampler, egyszerű szűrők, denormal-védelem.
// Minden allokációmentes és render-szál biztos.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace eki {

constexpr float kPi = 3.14159265358979323846f;

// MARK: - Segédfüggvények

inline float clampf(float x, float lo, float hi) { return std::min(hi, std::max(lo, x)); }
inline float mtof(float n) { return 440.0f * std::exp2((n - 69.0f) / 12.0f); }
inline float semis(float s) { return std::exp2(s / 12.0f); }
inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1e-9f)); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

/// Lágy szaturáció (racionális tanh-közelítés, a vágási pontokban pontos)
inline float softClip(float x) {
    float c = clampf(x, -3.0f, 3.0f);
    float c2 = c * c;
    return c * (27.0f + c2) / (27.0f + 9.0f * c2);
}

/// Egypólusú együttható: `t` másodperces időállandó (63%)
inline float onePoleCoef(float seconds, float sr) {
    return std::exp(-1.0f / (std::max(1e-6f, seconds) * sr));
}

/// Denormal-védelem a render blokk idejére (FTZ/DAZ), RAII
struct DenormalGuard {
#if defined(__aarch64__)
    uint64_t saved;
    DenormalGuard() { saved = __builtin_arm_rsr64("fpcr"); __builtin_arm_wsr64("fpcr", saved | (1ull << 24)); }
    ~DenormalGuard() { __builtin_arm_wsr64("fpcr", saved); }
#elif defined(__x86_64__)
    unsigned int saved;
    DenormalGuard() { saved = _mm_getcsr(); _mm_setcsr(saved | 0x8040); }
    ~DenormalGuard() { _mm_setcsr(saved); }
#else
    DenormalGuard() {}
#endif
};

/// Gyors xorshift fehérzaj -1...1
struct Noise {
    uint32_t state = 0x9E3779B9u;
    explicit Noise(uint32_t seed = 0x9E3779B9u) : state(seed == 0 ? 1 : seed) {}
    inline float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(int32_t(state)) / float(INT32_MAX);
    }
};

// MARK: - Sávkorlátozott oszcillátor (PolyBLEP)

struct BLOsc {
    float phase = 0, inc = 0;

    inline void setFreq(float f, float sr) { inc = clampf(f / sr, 0.0f, 0.45f); }

    static inline float blep(float t, float dt) {
        if (t < dt) { float x = t / dt; return x + x - x * x - 1.0f; }
        if (t > 1.0f - dt) { float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
        return 0.0f;
    }

    inline void advance() { phase += inc; if (phase >= 1.0f) phase -= 1.0f; }

    inline float saw() { float v = 2.0f * phase - 1.0f - blep(phase, inc); advance(); return v; }

    inline float pulse(float pw) {
        float v = phase < pw ? 1.0f : -1.0f;
        v += blep(phase, inc);
        float t2 = phase + 1.0f - pw;
        if (t2 >= 1.0f) t2 -= 1.0f;
        v -= blep(t2, inc);
        advance();
        return v;
    }

    inline float triangle() { float v = 1.0f - 4.0f * std::fabs(phase - 0.5f); advance(); return v; }
    inline float sine() { float v = std::sin(2.0f * kPi * phase); advance(); return v; }

    /// 0 saw, 1 square, 2 pulse 25%, 3 triangle, 4 sine
    inline float wave(int type, float pw = 0.5f) {
        switch (type) {
            case 0: return saw();
            case 1: return pulse(pw);
            case 2: return pulse(0.25f);
            case 3: return triangle();
            default: return sine();
        }
    }
};

// MARK: - TPT állapotváltozós szűrő (Zavalishin / Cytomic)

struct SVF {
    float ic1 = 0, ic2 = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float lp = 0, bp = 0, hp = 0;

    inline void set(float cutoff, float q, float sr) {
        float fc = clampf(cutoff, 16.0f, sr * 0.45f);
        float g = std::tan(kPi * fc / sr);
        k = 1.0f / std::max(0.5f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void tick(float v0) {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }

    inline float lowpass(float x) { tick(x); return lp; }
    inline float highpass(float x) { tick(x); return hp; }
    void reset() { ic1 = ic2 = 0; }
};

// MARK: - Zero-delay-feedback 4 pólusú ladder, szaturált visszacsatolással

struct Ladder {
    float s1 = 0, s2 = 0, s3 = 0, s4 = 0, G = 0, k = 0;

    /// reso 0...1 (1 = önrezgés határa)
    inline void set(float cutoff, float reso, float sr) {
        float fc = clampf(cutoff, 16.0f, sr * 0.42f);
        float g = std::tan(kPi * fc / sr);
        G = g / (1.0f + g);
        k = clampf(reso, 0.0f, 1.05f) * 3.95f;
    }

    inline float tick(float x) {
        float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        float S = G3 * (1 - G) * s1 + G2 * (1 - G) * s2 + G * (1 - G) * s3 + (1 - G) * s4;
        float y4est = (G4 * x + S) / (1 + k * G4);
        float u = softClip(x * (1 + 0.45f * k) - k * y4est);
        float v = (u - s1) * G; float y1 = v + s1; s1 = y1 + v;
        v = (y1 - s2) * G; float y2 = v + s2; s2 = y2 + v;
        v = (y2 - s3) * G; float y3 = v + s3; s3 = y3 + v;
        v = (y3 - s4) * G; float y4 = v + s4; s4 = y4 + v;
        return y4;
    }

    void reset() { s1 = s2 = s3 = s4 = 0; }
};

// MARK: - ADSR (lineáris attack, exponenciális decay/release)

struct ADSR {
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float value = 0, attackInc = 0.01f, decayCoef = 0.999f, sustain = 1, releaseCoef = 0.999f;

    /// -60 dB-ig `t` másodperc alatt
    static inline float coef(float t, float sr) { return std::exp(-6.9077554f / (std::max(0.0005f, t) * sr)); }

    inline void set(float a, float d, float s, float r, float sr) {
        attackInc = 1.0f / (std::max(0.0005f, a) * sr);
        decayCoef = coef(d, sr);
        sustain = clampf(s, 0, 1);
        releaseCoef = coef(r, sr);
    }

    void gateOn() { stage = Attack; }
    void gateOff() { if (stage != Idle) stage = Release; }
    bool isActive() const { return stage != Idle; }
    void reset() { stage = Idle; value = 0; }

    inline float tick() {
        switch (stage) {
            case Idle: return 0;
            case Attack: value += attackInc; if (value >= 1) { value = 1; stage = Decay; } break;
            case Decay:
                value = sustain + (value - sustain) * decayCoef;
                if (std::fabs(value - sustain) < 0.0005f) { value = sustain; stage = Sustain; }
                break;
            case Sustain: value = sustain; break;
            case Release: value *= releaseCoef; if (value < 0.00005f) { value = 0; stage = Idle; } break;
        }
        return value;
    }
};

/// Egypólusú paraméter-simító
struct Smooth {
    float value = 0, coef = 0.995f;
    void setTime(float seconds, float sr) { coef = onePoleCoef(seconds, sr); }
    inline float tick(float target) { value = target + (value - target) * coef; return value; }
};

/// Egypólusú alul-/felüláteresztő (TPT)
struct OnePole {
    float s = 0, g = 0;
    void set(float cutoff, float sr) {
        float w = std::tan(kPi * clampf(cutoff, 1.0f, sr * 0.49f) / sr);
        g = w / (1.0f + w);
    }
    inline float lowpass(float x) { float v = (x - s) * g; float y = v + s; s = y + v; return y; }
    inline float highpass(float x) { return x - lowpass(x); }
    void reset() { s = 0; }
};

/// DC-blokkoló (~5 Hz)
struct DCBlocker {
    float x1 = 0, y1 = 0, r = 0.9995f;
    void set(float sr, float hz = 5.0f) { r = 1.0f - (2.0f * kPi * hz / sr); }
    inline float tick(float x) { float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

// MARK: - 2x túlmintavételezés: polifázisú IIR félsáv (két allpass-lánc)

struct Halfband {
    static constexpr int N = 6;
    // musicdsp "polyphase IIR", 12. rendű, meredek változat
    static constexpr float a[N] = {0.036681502163648017f, 0.2746317593794541f, 0.56109896978791948f,
                                   0.769741833862266f, 0.8922608180038789f, 0.962094548378084f};
    static constexpr float b[N] = {0.13654762463195771f, 0.42313861743656667f, 0.6775400499741616f,
                                   0.839889624849638f, 0.9315419599631839f, 0.9878163707328971f};
    struct Chain {
        float x1[N] = {}, y1[N] = {};
        inline float tick(float x, const float* c) {
            for (int i = 0; i < N; ++i) {
                float y = c[i] * (x - y1[i]) + x1[i];
                x1[i] = x; y1[i] = y; x = y;
            }
            return x;
        }
        void reset() { for (int i = 0; i < N; ++i) x1[i] = y1[i] = 0; }
    };
};

struct Oversampler2x {
    Halfband::Chain upA, upB, dnA, dnB;
    float dnPrevOdd = 0;

    /// egy bemenő mintából két túlmintavételezett minta
    inline void up(float x, float& o0, float& o1) {
        o0 = upA.tick(x, Halfband::a);
        o1 = upB.tick(x, Halfband::b);
    }
    /// két túlmintavételezett mintából egy kimenő minta
    inline float down(float i0, float i1) {
        float y = 0.5f * (dnA.tick(i0, Halfband::a) + dnB.tick(dnPrevOdd, Halfband::b));
        dnPrevOdd = i1;
        return y;
    }
    void reset() { upA.reset(); upB.reset(); dnA.reset(); dnB.reset(); dnPrevOdd = 0; }
};

// MARK: - VU-mutató ballisztika (másodrendű, ~300 ms, ~1,5% túllövés)

struct VUNeedle {
    float pos = 0, vel = 0, wn2 = 0, twoZetaWn = 0, dt = 0;
    void set(float sr) {
        const float wn = 13.5f, zeta = 0.81f;
        wn2 = wn * wn; twoZetaWn = 2.0f * zeta * wn; dt = 1.0f / sr;
    }
    inline float tick(float x) {
        float acc = wn2 * (x - pos) - twoZetaWn * vel;
        vel += acc * dt;
        pos += vel * dt;
        return pos;
    }
    void reset() { pos = vel = 0; }
};

} // namespace eki
