// Ek1DSP.h — EK1: egygombos-szekciós mastering-lánc, AU-független.
//
// A user Logic-próbája után (2026-10-22) átalakítva — "tiszta legyen minden, sőt még attól is tisztább":
//   BASS PUNCH (±12 dB): a MASTEQUI 41 Hz-es elsőrendű mély polca
//   FOCUS (±12 dB): 1 kHz-es harang, amely a gombbal egyre szélesebb (Q 1,2 → 0,48), és egyre erősebben hozza a 2,5 kHz-es
//        kiterjesztést is → kis állásnál fókuszált 1 kHz, felhúzva széles 1–3 kHz-es "jelenlét" (a user kérése, 2026-10-22)
//   AIR (±12 dB): a MASTEQUI 11,6 kHz-es elsőrendű magas polca          (közös görbék: Shared/EKIMasterCurves.h)
//   TRANSIENT (±10): tranziens-formáló, a felfutást erősebben emeli ("pattogósabb"); egy csúcskövetőből számolt gyors / lassú
//        burkoló aránya → állandó hangon nem modulál, nem torzít
//   PUSH (0–30 dB): a LIMEKI DENSITY fokozata (STEREO, SLOW), kétszeres elengedéssel (RELEASE ×2)
//   WIDTH (±5 dB): oldaljel (S) erősítése, csak 150 Hz felett (alatta a sztereókép érintetlen)
//   VOLUME (0–15 dB): erősítés egy feszes, nem lüktető true-peak limiterbe (a user: "feszes, fixen hangos, ne lüktessen"):
//        1,5 ms előretekintés, a jel fél periódusáig tartás (a mély így sem torzul), utána a VOLUME-mal együtt növő elengedés
//        (30 ms → RELEASE MAX, az EKI fiókban állítható, alap 200 ms), plafon −0,1 dBTP
//   OUTPUT; AUTO −14 LUFS (EKI fiók, alapban KI): a VOLUME magától áll be. Az ANALOG, a BODY, a referencia "ragasztója" és a +0,37 dB-es eltolás kikerült:
//   semleges állásban a lánc átlátszó (PUSH 0-nál a DENSITY ki van kerülve); csak a −0,1 dBTP fölötti csúcsokat fogja a limiter.
// Jelút: [2× túlmintavételezve: BASS PUNCH → FOCUS → AIR → TRANSIENT] → PUSH (DENSITY) → WIDTH → VOLUME → limiter → OUTPUT
#pragma once
#include "EKIAnalog.h"
#include "EKIFilters.h"
#include "EKIOversample.h"
#include "EKIMasterCurves.h"
#include "EKILoudLimiter.h"
#include "../LIMEKI/LimekiDSP.h"
#include <cstring>

namespace eki {

struct Ek1Params {
    float air = 0, focus = 0, punch = 0;     // ±12 dB
    float push = 0;                          // 0…30 dB (DENSITY THRESHOLD)
    float volume = 0;                        // 0…15 dB
    float width = 0, transient = 0;          // ±5 / ±10
    float outDb = 0;
    float autoLufs = 0;                      // AUTO −14 LUFS (alapban KI; 2026-10-23 óta az AUTO GAIN helyett, mint a LIMEKI-ben)
    float relMaxMs = 500;                    // EKI fiók: a lassú fokozat elengedése teljes VOLUME-nál (az indulás fixen 30 ms)
    float trAttackMs = 8, trReleaseMs = 20;  // EKI fiók: a TRANSIENT ablaka (a lassú burok ráfutása) és elengedése
    float focusHz = 1000;                    // EKI fiók: a FOCUS középfrekvenciája (300 Hz…5 kHz; a kiterjesztés 2,5x felette)
};

class Ek1DSP {
public:
    static constexpr int kMaxCh = 2, kSub = 32, kOsLat = FirOversamplerHQ::K;

    void prepare(float sampleRate) {
        sr = sampleRate; srOS = 2 * sampleRate;
        for (auto& o : os) o.design(2);
        for (int c = 0; c < kMaxCh; ++c) {
            kwOut[c][0].designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr, 1681.0);
            kwOut[c][1].designMatched(Analog2::highPass(38.0, 0.5), sr, 38.0);
        }
        smoothK = 1.0f - onePoleCoef(0.03f, sr / kSub);
        pkRel = onePoleCoef(0.060f, srOS);   // lassú visszatérés: 100 Hz-en sem hullámzik (tiszta)
        trFastA = 1.0f - onePoleCoef(0.0001f, srOS); trSlowA = 1.0f - onePoleCoef(0.008f, srOS); trRel = 1.0f - onePoleCoef(0.020f, srOS);
        density.prepare(sr);
        lim.prepare(sr, 0.003f, 0.005f, 1.6f);    // gyors fokozat: csak a csúcsok (2 x 3 ms simított ív, 5 ms, periódusnyi tartás)
        leveler.prepare(sr);
        for (int c = 0; c < kMaxCh; ++c) sideLp[c].designLP2(150.0, sr);
        totalLat = kOsLat + int(std::lround(density.latencySeconds() * sr)) + lim.lat;
        lufsK = 1.0f - onePoleCoef(1.0f, sr);   // ≈ rövid távú (3 s-os ablaknak megfelelő simítás)
        needle.set(sr);
        reset();
    }

    void reset() {
        needle.reset();
        for (int c = 0; c < kMaxCh; ++c) {
            os[c].reset();
            for (auto& b : eq[c]) b.reset();
            lo[c].reset(); hi[c].reset();
            for (auto& k : kwOut[c]) k.reset();
        }
        density.reset(); lim.reset(); leveler.reset(); for (auto& b : sideLp) b.reset();
        trPk = 0; trFast = trSlow = 1e-6f; oPos = 0; sTone = 0; lufsMs = 1e-6;
        for (auto& d : osDly) std::fill(std::begin(d), std::end(d), 0.0f);
        primed = false;
        for (auto& k : key) k = -1e9f;
    }

    double latencySeconds() const { return double(totalLat) / sr; }

    void process(const float* const* in, float* const* out, int ch, int n, const Ek1Params& p) {
        ch = std::min(ch, kMaxCh);
        // AUTO −14 LUFS: a VOLUME lassan (≈ 0,6 dB/s) magától a −14 LUFS-os rövid távú kimeneti hangossághoz áll
        const bool autoL = p.autoLufs >= 0.5f;
        if (!autoL) autoVolDb = clampf(p.volume, 0, 15);   // bekapcsoláskor a kézi állásból indul
        const float t[7] = {clampf(p.air, -12, 12), clampf(p.focus, -12, 12), clampf(p.punch, -12, 12), clampf(p.push, 0, 30),
                            autoL ? autoVolDb : clampf(p.volume, 0, 15), clampf(p.width, -5, 5), clampf(p.transient, -10, 10)};
        const float outT = clampf(p.outDb, -12, 12);
        if (!primed) {
            for (int k = 0; k < 7; ++k) s[k] = t[k];
            sFocusHz = clampf(p.focusHz, 300, 5000);
            sOut = outT; sTone = (std::fabs(t[0]) + std::fabs(t[1]) + std::fabs(t[2]) + std::fabs(t[6]) > 1e-4f) ? 1.0f : 0.0f; primed = true;
        }

        for (int i0 = 0; i0 < n; i0 += kSub) {
            const int m = std::min(kSub, n - i0);
            for (int k = 0; k < 7; ++k) s[k] += (t[k] - s[k]) * smoothK;
            sOut += (outT - sOut) * smoothK;
            designEq();
            LimekiParams lp;   // PUSH = a LIMEKI DENSITY fokozata, 2x elengedéssel; a CLEAN ki
            lp.thresholdDb = -s[3]; lp.denCeilDb = 0; lp.denRelTrim = 2.0f; lp.link = 0; lp.denMode = 0; lp.denMix = 100;
            lp.denOn = t[3] > 0.05f ? 1.0f : 0.0f;   // PUSH 0: a DENSITY kikerülve (a késleltetett eredeti megy tovább — bitre átlátszó)
            lp.cleanOn = 0; lp.pushDb = 0; lp.autoLufs = 0;
            const float trK = s[6] >= 0 ? s[6] * 0.085f : s[6] * 0.05f;   // a felfutás emelése erősebb ("pattogósabb")
            trSlowA = 1.0f - onePoleCoef(clampf(p.trAttackMs, 1, 30) * 0.001f, srOS);
            trRel = 1.0f - onePoleCoef(clampf(p.trReleaseMs, 5, 300) * 0.001f, srOS);
            sFocusHz += (clampf(p.focusHz, 300, 5000) - sFocusHz) * smoothK;
            // ha a hangszín-gombok és a TRANSIENT mind 0-n: a túlmintavételezett út helyett a késleltetett eredeti (bitre átlátszó)
            const bool toneOn = std::fabs(t[0]) + std::fabs(t[1]) + std::fabs(t[2]) + std::fabs(t[6]) > 1e-4f;
            sTone += ((toneOn ? 1.0f : 0.0f) - sTone) * smoothK;
            if (!toneOn && sTone < 1e-4f) sTone = 0;
            // 1) hangszín + tranziens, 2x túlmintavételezve
            for (int i = i0; i < i0 + m; ++i) {
                float u[kMaxCh][2];
                for (int c = 0; c < ch; ++c) os[c].up(in[c][i], u[c]);
                for (int ph = 0; ph < 2; ++ph) {
                    float x[kMaxCh], a = 0;
                    for (int c = 0; c < ch; ++c) { float v = lo[c].tick(u[c][ph]); v = eq[c][1].tick(eq[c][0].tick(v)); v = hi[c].tick(v); x[c] = v; a = std::max(a, std::fabs(v)); }
                    trPk = a > trPk ? a : a + (trPk - a) * pkRel;
                    trFast += (trPk - trFast) * (trPk > trFast ? trFastA : trRel);
                    trSlow += (trPk - trSlow) * (trPk > trSlow ? trSlowA : trRel);
                    if (std::fabs(trK) > 1e-4f) {
                        const float g = dbToGain(clampf(trK * gainToDb(std::max(trFast, 1e-6f) / std::max(trSlow, 1e-6f)), -12.0f, 12.0f));
                        for (int c = 0; c < ch; ++c) x[c] *= g;
                    }
                    for (int c = 0; c < ch; ++c) u[c][ph] = x[c];
                }
                for (int c = 0; c < ch; ++c) {
                    const float wet = os[c].down(u[c]);
                    const float dry = osDly[c][size_t(oPos)]; osDly[c][size_t(oPos)] = in[c][i];   // ugyanannyi késleltetés, szűrés nélkül
                    tmp[c][i - i0] = dry + (wet - dry) * sTone;
                }
                oPos = (oPos + 1) % kOsLat;
            }
            // 2) PUSH: LIMEKI DENSITY (blokkban)
            const float* tin[kMaxCh] = {tmp[0], tmp[1]};
            float* tout[kMaxCh] = {tmp2[0], tmp2[1]};
            density.process(tin, tout, ch, m, lp);
            denGr = std::min(denGr, density.takeDenGr());
            // 3) WIDTH → VOLUME (kézi vagy AUTO −14 LUFS) → tiszta limiter → OUTPUT
            const float sideG = dbToGain(s[5]), volG = dbToGain(s[4]);
            // változó elengedés (a user ötlete): a lassú fokozaté a VOLUME-mal együtt nő — 30 ms-ról a RELEASE MAX-ig (EKI fiók)
            const float relMax = clampf(p.relMaxMs, 50, 1000) * 0.001f;
            leveler.rel = 1.0f - onePoleCoef(0.030f + (relMax - 0.030f) * std::pow(s[4] / 15.0f, 1.5f), sr);
            for (int i = 0; i < m; ++i) {
                float x[kMaxCh];
                for (int c = 0; c < ch; ++c) x[c] = tout[c][i];
                if (ch == 2) {   // WIDTH: az oldaljel 150 Hz alatti része érintetlen (a mély középen marad)
                    const float M = 0.5f * (x[0] + x[1]), S = 0.5f * (x[0] - x[1]);
                    const float sl = sideLp[0].tick(S);
                    if (std::fabs(s[5]) > 1e-3f) { const float So = sl + (S - sl) * sideG; x[0] = M + So; x[1] = M - So; }
                }
                for (int c = 0; c < ch; ++c) x[c] *= volG;
                leveler.tick(x, ch, -0.1f);   // lassú: az átlagszint (a test nem lüktet)
                lim.tick(x, ch, -0.1f);       // gyors: a csúcsok (a tranziens feszes marad)
                limGr = std::min(limGr, gainToDb(lim.g * leveler.g));
                const float g = dbToGain(sOut);
                float av = 0; double e = 0;
                for (int c = 0; c < ch; ++c) {
                    out[c][i0 + i] = x[c] * g; av += std::fabs(out[c][i0 + i]);
                    const float k = kwOut[c][1].tick(kwOut[c][0].tick(out[c][i0 + i])); e += double(k) * k;   // K-súlyozott kimenet
                }
                lufsMs += (e - lufsMs) * lufsK;
                if (autoL) {
                    const double l = -0.691 + 10.0 * std::log10(std::max(lufsMs, 1e-12));
                    if (l > -60.0) autoVolDb = clampf(autoVolDb + float((-14.0 - l) * 0.6 / sr), 0.0f, 15.0f);
                }
                needle.tick(av / float(ch));   // kimeneti VU: egyenirányított átlag, VU-ballisztika
            }
        }
    }

    /// AUTO −14 LUFS: a ténylegesen beállt VOLUME (dB)
    float autoVolumeDb() const { return autoVolDb; }
    /// kimeneti VU (dB): 0 VU = −12 dBFS RMS (szinusz: az egyenirányított átlag 0,9 × RMS)
    float vuDb() const { return clampf(gainToDb(std::max(needle.pos, 1e-6f) / 0.9f) + 12.0f, -20.0f, 3.0f); }
    float takePushGr() { const float v = denGr; denGr = 0; return v; }
    float takeLimGr() { const float v = limGr; limGr = 0; return v; }

private:
    void designEq() {
        const float nk[4] = {s[0], s[1], s[2], sFocusHz * 0.001f};
        bool same = true; for (int k = 0; k < 4; ++k) if (std::fabs(nk[k] - key[k]) > 0.002f) same = false;
        if (same) return;
        for (int k = 0; k < 4; ++k) key[k] = nk[k];
        // FOCUS: harang a választott frekvencián (alap 1 kHz) egyre szélesebb Q-val + egyre erősebb kiterjesztés 2,5x felette
        // (vágásnál tükrözve)
        const double fc = sFocusHz, fe = std::min(2.5 * fc, 0.4 * srOS);
        Biquad bell, ext;
        const float g0 = s[1], k = std::fabs(g0) / 12.0f;
        if (std::fabs(g0) < 1e-3f) { bell.setIdentity(); ext.setIdentity(); }
        else {
            const float q = 1.2f * (1.0f - 0.6f * k);
            const float ge = g0 * 0.6f * std::pow(k, 0.7f);
            bell.designMatched(Analog2::bell(fc, g0, q), srOS, fc);
            if (std::fabs(ge) < 1e-3f) ext.setIdentity(); else ext.designMatched(Analog2::bell(fe, ge, 0.8), srOS, fe);
        }
        Shelf1 l, h;
        l.design(true, 41.4, s[2], 0.015, srOS);    // BASS PUNCH: 41 Hz mély polc
        h.design(false, 11624.0, s[0], 0.0, srOS);  // AIR: 11,6 kHz magas polc
        for (int c = 0; c < kMaxCh; ++c) {
            for (int j = 0; j < 2; ++j) { const double z1 = eq[c][j].z1, z2 = eq[c][j].z2; eq[c][j] = j ? ext : bell; eq[c][j].z1 = z1; eq[c][j].z2 = z2; }
            const float zl = lo[c].z1, zh = hi[c].z1; lo[c] = l; lo[c].z1 = zl; hi[c] = h; hi[c].z1 = zh;
        }
    }

    float sr = 48000, srOS = 96000, smoothK = 0.1f;
    FirOversamplerHQ os[kMaxCh];
    Biquad eq[kMaxCh][2];
    Shelf1 lo[kMaxCh], hi[kMaxCh];
    Biquad kwOut[kMaxCh][2];   // K-súlyozás az AUTO −14 LUFS méréséhez
    LimekiDSP density;
    EkTightLimiter lim;
    EkLeveler leveler;
    Biquad sideLp[kMaxCh];
    VUNeedle needle;
    float tmp[kMaxCh][kSub] = {}, tmp2[kMaxCh][kSub] = {}, osDly[kMaxCh][kOsLat] = {}, sTone = 0;
    int oPos = 0;
    int totalLat = 1;
    double lufsMs = 1e-6; float lufsK = 0, autoVolDb = 0;
    float pkRel = 0.99f, trFastA = 0.1f, trSlowA = 0.01f, trRel = 0.01f;
    float trPk = 0, trFast = 1e-6f, trSlow = 1e-6f, denGr = 0, limGr = 0;
    float s[7] = {}, sOut = 0, key[4], sFocusHz = 1000;
    bool primed = false;
};

} // namespace eki
