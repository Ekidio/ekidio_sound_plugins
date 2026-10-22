// EKILoudLimiter.h — a feszes, tiszta hangosító limiter (az EK1 VOLUME motorja; 2026-10-23 óta a LIMEKI CLEAN is ezt használja).
// Két fokozat: lassú szintfokozat (EkLeveler: az 50 ms-os RMS-t a plafon alatt tartja, hosszú elengedéssel — a test nem lüktet)
// → gyors csúcsfokozat (EkTightLimiter: 2 × 3 ms simított ív, periódusnyi tartás, 5 ms elengedés, true-peak becslés).
// Az EkLoudLimiter a kettőt köti össze: a lassú fokozat elengedése a hajtással együtt nő (30 ms → relMax).
#pragma once
#include "EKIAnalog.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

namespace eki {

/// feszes limiter: a szükséges erősítés csúszó minimuma (előretekintés), a jel fél periódusáig tartva (nullátmenetekből becsülve:
/// a mélyben hosszú tartás → nincs hullámformán belüli moduláció), utána gyors elengedés, az ablakon átlagolva; true-peak becsléssel
struct EkTightLimiter {
    // kétszeres doboz-átlag (háromszög alakú gain-ív, 2026-10-22): a behúzás simább, a mélyben nem recseg
    static constexpr int kMax = 2048;
    std::deque<std::pair<long, float>> q; long n = 0;
    std::vector<float> box, box2; double sum = 0, sum2 = 0; int bpos = 0;
    float buf[2][kMax] = {}, hist[2][3] = {}; int pos = 0, la = 72, lat = 144, hold = 0, zcCount = 0, holdMin = 1, holdMax = 1000;
    float env = 0, zcAvg = 0, rel = 0.01f, holdK = 0.8f, g = 1; bool zcSign = true;
    void prepare(float fs, float laSec, float relSec, float hk) {
        la = std::clamp(int(std::lround(laSec * fs)), 2, kMax / 2 - 1); lat = 2 * la;
        rel = 1.0f - onePoleCoef(relSec, fs); holdK = hk;
        holdMin = std::max(1, int(0.0006f * fs)); holdMax = int(0.025f * fs);
        reset();
    }
    void reset() { q.clear(); n = 0; box.assign(size_t(la), 0.0f); box2.assign(size_t(la), 0.0f); sum = sum2 = 0; bpos = 0;
                   std::memset(buf, 0, sizeof buf); std::memset(hist, 0, sizeof hist);
                   pos = 0; env = 0; hold = 0; zcCount = 0; zcAvg = 0; g = 1; zcSign = true; }
    inline void tick(float* x, int ch, float ceilDb) {
        float pk = 0;
        for (int c = 0; c < ch; ++c) {   // mintaközi csúcs (4x Hermite)
            const float a = hist[c][0], b = hist[c][1], cc = hist[c][2], d = x[c];
            pk = std::max({pk, std::fabs(b), std::fabs(cc), std::fabs(d)});
            for (float t : {0.25f, 0.5f, 0.75f}) {
                const float c1 = 0.5f * (cc - a), c2 = a - 2.5f * b + 2.0f * cc - 0.5f * d, c3 = 0.5f * (d - a) + 1.5f * (b - cc);
                pk = std::max(pk, std::fabs(((c3 * t + c2) * t + c1) * t + b));
            }
            hist[c][0] = b; hist[c][1] = cc; hist[c][2] = d;
        }
        const float req = std::min(0.0f, ceilDb - gainToDb(pk + 1e-12f));
        const bool zs = (ch == 2 ? x[0] + x[1] : x[0]) >= 0.0f;
        if (zs != zcSign) { zcAvg += (float(zcCount) - zcAvg) * 0.3f; zcCount = 0; zcSign = zs; } else if (zcCount < holdMax) ++zcCount;
        const int holdN = std::clamp(int(holdK * std::max(zcAvg, float(zcCount))), holdMin, holdMax);
        while (!q.empty() && q.back().second >= req) q.pop_back();
        q.push_back({n, req});
        while (q.front().first <= n - (lat + 1)) q.pop_front();
        ++n;
        const float m = q.front().second;
        if (m <= env) { env = m; hold = holdN; } else if (hold > 0) --hold; else env += (m - env) * rel;
        sum += env - box[size_t(bpos)]; box[size_t(bpos)] = env;
        const float v = float(sum / la);
        sum2 += v - box2[size_t(bpos)]; box2[size_t(bpos)] = v; bpos = (bpos + 1) % la;
        g = dbToGain(float(sum2 / la));
        for (int c = 0; c < ch; ++c) { buf[c][pos] = x[c]; x[c] = buf[c][(pos - lat + kMax) % kMax] * g; }
        pos = (pos + 1) % kMax;
    }
};

/// A VOLUME lassú fokozata (referencia-renderek elemzése alapján, 2026-10-22): az 50 ms-os RMS-szintet a ceiling alá
/// `crestDb`-vel tartja, 20 ms-os ráfutással és hosszú elengedéssel — az ütések körül alig mozdul (a test nem esik be, a
/// tranziens feszes marad); a csúcsokat utána az EkTightLimiter fogja meg rövid behúzással.
struct EkLeveler {
    std::vector<float> ring; double sum = 0; int pos = 0, w = 2400;
    float env = 0, att = 0.001f, rel = 0.0001f, g = 1, crestDb = 3.5f;
    void prepare(float fs) {
        w = std::max(1, int(std::lround(0.050f * fs))); ring.assign(size_t(w), 0.0f);
        att = 1.0f - onePoleCoef(0.020f, fs);
        reset();
    }
    void reset() { std::fill(ring.begin(), ring.end(), 0.0f); sum = 0; pos = 0; env = 0; g = 1; }
    inline void tick(float* x, int ch, float ceilDb) {
        float p = 0;
        for (int c = 0; c < ch; ++c) p += x[c] * x[c];
        p /= float(ch);
        sum += double(p) - double(ring[size_t(pos)]); ring[size_t(pos)] = p; pos = (pos + 1) % w;
        if (sum < 0) sum = 0;
        const float lvl = 10.0f * std::log10(float(sum / w) + 1e-20f);
        const float req = std::min(0.0f, ceilDb - crestDb - lvl);
        env += (req - env) * (req < env ? att : rel);
        g = dbToGain(env);
        for (int c = 0; c < ch; ++c) x[c] *= g;
    }
};

/// a kész hangosító: lassú szintfokozat + gyors csúcsfokozat; a késleltetése `latency()` minta
struct EkLoudLimiter {
    EkLeveler leveler;
    EkTightLimiter lim;
    float fs = 48000;
    void prepare(float sampleRate) { fs = sampleRate; lim.prepare(fs, 0.003f, 0.005f, 1.6f); leveler.prepare(fs); }
    void reset() { lim.reset(); leveler.reset(); }
    /// a lassú fokozat elengedése: 30 ms-ról relMax-ig nő a hajtással (drive / driveMax)^1,5 szerint
    void setRelease(float drive, float driveMax, float relMaxSec) {
        const float k = std::pow(std::clamp(drive / driveMax, 0.0f, 1.0f), 1.5f);
        leveler.rel = 1.0f - onePoleCoef(0.030f + (relMaxSec - 0.030f) * k, fs);
    }
    inline void tick(float* x, int ch, float ceilDb) { leveler.tick(x, ch, ceilDb); lim.tick(x, ch, ceilDb); }
    float gain() const { return lim.g * leveler.g; }
    int latency() const { return lim.lat; }
};

} // namespace eki
