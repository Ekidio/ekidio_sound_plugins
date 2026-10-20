// LimekiDSP.h — LIMEKI: DENSITY & CLEAN LIMITER, AU-független.
//
// Két fokozat, mindkettő kikapcsolható; együtt a jel balról jobbra halad:
//   DENSITY (a user kedvenc dúsító limiterének mért viselkedése — EKIMETER-renderekből):
//     THRESHOLD = belső szintpótlás (−THRESHOLD dB) → előretekintő (3,3 ms) csúcslimiter 0 dBFS-es plafonnal, azonnali felfutással,
//     exponenciális elengedéssel (SLOW τ ≈ 190 ms, FAST τ ≈ 46 ms; rövid löket után ≈ 25 %-kal gyorsabb) → lágy vágás (2x túlmintavételezve;
//     a teljes szinten ≈ 1,5 % 3. felhang, a csúcs ≈ 0,2 dB-lel a plafon alatt — ez a "dúsítás") → CEILING (kimeneti szint) → MIX
//     STEREO: a két csatorna közös erősítéssel (teljes csatolás), DUAL: külön
//   CLEAN (tiszta csúcsfogó): PUSH → előretekintő (1 / 3 / 5 ms) true peak limiter (4x interpolált csúcsbecslés), simított erősítés
//     (nincs hullámformán belüli torzítás), AUTO elengedés (rövid csúcsra gyors, tartósra lassú) vagy fix idő, csatolás %, CEILING dBTP
//     AUTO −14 LUFS: a PUSH lassan magától a −14 LUFS-os (rövid távú, K-súlyozott) kimenethez áll; a CEILING közben tart
// A késleltetés minden állásban állandó (a kikapcsolt fokozat is ugyanannyit késleltet) — a DAW-nak jelzett latencia nem változik.
// AUTO GAIN nincs (a user döntése: a limiter célja a hangerő emelése).
#pragma once
#include "EKIAnalog.h"
#include "EKIFilters.h"
#include <deque>
#include <vector>

namespace eki {

struct LimekiParams {
    // DENSITY
    float thresholdDb = -6;   // 0 … −30 (a THRESHOLD gomb), = belső szintpótlás
    float denCeilDb = 0;      // −12 … 0 (kimeneti szint)
    float denRelTrim = 1;     // 0,5 … 2 (az elengedés finomhangolása)
    int link = 0;             // 0 = STEREO (csatolt), 1 = DUAL
    int denMode = 0;          // 0 = SLOW, 1 = FAST
    float denOn = 1;
    float denMix = 100;       // %
    // CLEAN
    float pushDb = 0;         // 0 … +12
    float ceilDb = -1;        // −3 … 0 dBTP
    int release = 0;          // 0 = AUTO, 1..5 = 10 / 30 / 100 / 300 / 1000 ms
    float truePeak = 1;
    float cleanOn = 1;
    float cleanLink = 0;      // % (a referencia csatornái függetlenek)
    int lookahead = 1;        // 0 = 1 ms, 1 = 3 ms, 2 = 5 ms
    float autoLufs = 0;       // AUTO −14 LUFS
};

/// csúszó minimum (monoton sor) — az előretekintő limiter "a következő W mintában mennyi erősítés kell" számításához
struct SlidingMin {
    std::deque<std::pair<long, float>> q; long n = 0; long win = 1;
    void reset(long w) { q.clear(); n = 0; win = std::max<long>(1, w); }
    float push(float v) {
        while (!q.empty() && q.back().second >= v) q.pop_back();
        q.push_back({n, v});
        while (q.front().first <= n - win) q.pop_front();
        ++n;
        return q.front().second;
    }
};

/// mozgóátlag (W hossz) — a minimum-tartott erősítés simítása, hogy a csúcsra pontosan elérje a kellő értéket, torzítás nélkül
struct BoxAvg {
    std::vector<float> buf; size_t pos = 0; double sum = 0;
    void reset(size_t w, float v) { buf.assign(std::max<size_t>(1, w), v); pos = 0; sum = double(v) * buf.size(); }
    float push(float v) { sum += v - buf[pos]; buf[pos] = v; pos = (pos + 1) % buf.size(); return float(sum / buf.size()); }
};

/// egyszerű késleltető
struct Delay {
    std::vector<float> b; size_t pos = 0;
    void reset(size_t n) { b.assign(std::max<size_t>(1, n), 0.0f); pos = 0; }
    float tick(float x) { const float y = b[pos]; b[pos] = x; pos = (pos + 1) % b.size(); return y; }
};

class LimekiDSP {
public:
    static constexpr int kMaxCh = 2;

    void prepare(float sampleRate) {
        sr = sampleRate;
        denLa = std::max(1, int(std::lround(0.0033 * sr)));              // DENSITY előretekintés (mért: 3,3 ms)
        cleanMax = std::max(1, int(std::lround(0.005 * sr)));            // CLEAN legnagyobb előretekintés (5 ms) — ennyi a késleltetése mindig
        for (int c = 0; c < kMaxCh; ++c) {
            denDelay[c].reset(size_t(denLa)); cleanDelay[c].reset(size_t(cleanMax)); dryDelay[c].reset(size_t(denLa));
            for (auto& k : kw[c]) k.reset();
            kw[c][0].designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr, 1681.0);
            kw[c][1].designMatched(Analog2::highPass(38.0, 0.5), sr, 38.0);
        }
        smoothK = 1.0f - onePoleCoef(0.02f, sr);
        lufsK = 1.0f - onePoleCoef(1.0f, sr);                            // ≈ rövid távú (3 s-os ablaknak megfelelő simítás)
        meterK = onePoleCoef(1.5f, sr);                                  // csúcstartó esése
        lastLa = -1;
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            denDelay[c].reset(size_t(denLa)); cleanDelay[c].reset(size_t(cleanMax)); dryDelay[c].reset(size_t(denLa));
            denDet[c] = -120;
            os[c].reset(); osDry[c].reset(); denMin[c].reset(denLa + 1); denBox[c].reset(size_t(denLa), 0.0f); denEnv[c] = 0; denHold[c] = 0;
            cleanEnv[c] = 0; cleanHold[c] = 0; zcCount[c] = 0; zcAvg[c] = 0; zcSign[c] = true; for (auto& k : kw[c]) k.reset();
            hist[c][0] = hist[c][1] = hist[c][2] = 0;
        }
        lastLa = -1; ms = 1e-6; autoPushDb = 0; primed = false;
        grDen = grClean = 0; pk[0] = pk[1] = pkHold[0] = pkHold[1] = 0;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const LimekiParams& p) {
        ch = std::min(ch, kMaxCh);
        const int la = std::clamp(p.lookahead, 0, 2) == 0 ? std::max(1, int(std::lround(0.001 * sr)))
                     : p.lookahead == 1 ? std::max(1, int(std::lround(0.003 * sr))) : cleanMax;
        if (la != lastLa) { for (int c = 0; c < kMaxCh; ++c) { cleanMin[c].reset(la + 1); cleanBox[c].reset(size_t(la), 0.0f); gainPad[c].reset(size_t(std::max(1, cleanMax - la))); } padOn = cleanMax > la; lastLa = la; }
        // célértékek (simítva)
        const float thrT = clampf(-p.thresholdDb, 0, 30), dCeilT = clampf(p.denCeilDb, -12, 0);
        const float denOnT = p.denOn >= 0.5f ? 1.0f : 0.0f, cleanOnT = p.cleanOn >= 0.5f ? 1.0f : 0.0f;
        const float mixT = clampf(p.denMix, 0, 100) * 0.01f, linkT = clampf(p.cleanLink, 0, 100) * 0.01f;
        const float ceilT = clampf(p.ceilDb, -3, 0);
        if (!primed) { sThr = thrT; sDCeil = dCeilT; sDenOn = denOnT; sCleanOn = cleanOnT; sMix = mixT; sLink = linkT; sCeil = ceilT; sPush = p.pushDb; primed = true; }
        // DENSITY elengedés (mért időállandók, a finomhangolóval)
        const float tauDen = (p.denMode == 1 ? 0.06f : 0.228f) * clampf(p.denRelTrim, 0.5f, 2.0f);
        const float kDenRel = 1.0f - onePoleCoef(tauDen, sr), kDenRelShort = 1.0f - onePoleCoef(tauDen * 0.9f, sr);
        const int holdShort = int(0.05f * sr);
        const float kDenAtt = 1.0f - onePoleCoef(0.0002f, sr);
        // CLEAN elengedés: AUTO (rövid csúcsra 15 ms, tartós limitálásra 350 ms felé) vagy fix
        const float relMs[6] = {0, 10, 30, 100, 300, 1000};
        const bool relAuto = p.release <= 0;
        // a tiszta csúcsfogó (a referencia mérése szerint): a halkítás ≈ 25 ms-ig kitart (több mély periódust átfog — a mélyben sem
        // hullámzik, nem torzít), utána gyorsan enged el (AUTO ≈ 40 ms) — a tranziensek közt visszaáll, ettől hangos, mégis tiszta
        const float kClFast = 1.0f - onePoleCoef(relAuto ? 0.006f : relMs[std::clamp(p.release, 1, 5)] * 0.001f, sr);
        // a tartás a jelhez igazodik: a nullátmenetek közti időből (≈ fél periódus) — mély hangon hosszú (nem torzít),
        // sűrű tranziensen rövid (gyorsan visszaáll, hangosabb)
        const int holdMin = std::max(1, int(0.0006f * sr)), holdMax = int(0.025f * sr);
        const float kZc = 1.0f - onePoleCoef(0.02f, sr);
        const bool tp = p.truePeak >= 0.5f, autoL = p.autoLufs >= 0.5f;

        for (int i = 0; i < n; ++i) {
            sThr += (thrT - sThr) * smoothK; sDCeil += (dCeilT - sDCeil) * smoothK; sDenOn += (denOnT - sDenOn) * smoothK;
            sCleanOn += (cleanOnT - sCleanOn) * smoothK; sMix += (mixT - sMix) * smoothK; sLink += (linkT - sLink) * smoothK; sCeil += (ceilT - sCeil) * smoothK;
            const float pushT = autoL ? autoPushDb : clampf(p.pushDb, 0, 12);
            sPush += (pushT - sPush) * smoothK;

            // ---------------- DENSITY ----------------
            const float gIn = dbToGain(sThr);
            float x[kMaxCh], xd[kMaxCh];
            for (int c = 0; c < ch; ++c) x[c] = in[c][i] * gIn;
            // érzékelés: csúcsérzékelő rövid (≈ 0,8 ms-os) felfutással — tartós hangon (a mélyben is) pontosan a csúcsra áll, a nagyon
            // rövid tranzienseket nem tölti fel teljesen: azok átjutnak, és a lágy vágás kerekíti őket (ettől sűrűbb, hangosabb — mint a
            // referencia). Az elengedés az érzékelőé (mért időállandók; rövid löket után gyorsabb).
            float req[kMaxCh];
            for (int c = 0; c < ch; ++c) {
                // decibelben dolgozik: a lehúzás a 0 dBFS-es plafon felé exponenciálisan cseng le (mint a referencián: 90 % ≈ 2,3 × 63 %)
                const float aDb = gainToDb(std::fabs(x[c]) + 1e-12f);
                if (aDb > denDet[c]) { denDet[c] += (aDb - denDet[c]) * kDenAtt; denHold[c] = 0; }
                else { ++denHold[c]; denDet[c] += (std::max(aDb, 0.0f) - denDet[c]) * (denHold[c] < holdShort ? kDenRelShort : kDenRel); }
                req[c] = std::min(0.0f, -denDet[c]);
            }
            if (p.link == 0 && ch == 2) req[0] = req[1] = std::min(req[0], req[1]);
            float gDen[kMaxCh];
            for (int c = 0; c < ch; ++c) {
                gDen[c] = denBox[c].push(req[c]);                           // simítás az előretekintés hosszán (a 3,3 ms-os késleltetéssel)
                xd[c] = denDelay[c].tick(x[c]);
            }
            float yDen[kMaxCh];
            float grD = 0;
            for (int c = 0; c < ch; ++c) {
                const float lim = xd[c] * dbToGain(gDen[c]);
                // lágy vágás 2x túlmintavételezve (a "dúsítás")
                float u0, u1; os[c].up(lim, u0, u1);
                // a lemintázás utáni túllövést is lágyan kerekíti (a referencia csúcsai ≈ +1…+1,5 dBFS-nél megállnak)
                const float s = softTop(os[c].down(softClip3(u0), softClip3(u1)));
                // a száraz ág ugyanazon az úton (fázishelyes MIX), csak a vágás nélkül
                float d0, d1; osDry[c].up(xd[c], d0, d1);
                const float dry = osDry[c].down(d0, d1);
                const float wet = dry + (s - dry) * sMix;
                const float dOut = wet * dbToGain(sDCeil);
                // kikapcsolva: a késleltetett eredeti (azonos késleltetés)
                const float bypass = dryDelay[c].tick(in[c][i]);
                yDen[c] = bypass + (dOut - bypass) * sDenOn;
                grD = std::min(grD, gDen[c]);
            }
            grDen = std::min(grDen, grD * sDenOn);

            // ---------------- CLEAN ----------------
            const float gPush = dbToGain(sPush * sCleanOn);
            const float ceil = dbToGain(sCeil);
            float y[kMaxCh], peakEst[kMaxCh];
            for (int c = 0; c < ch; ++c) {
                y[c] = yDen[c] * gPush;
                // true peak becslés: a mintapontok közötti csúcs (4x, köbös Hermite-interpoláció a legutóbbi pontokon)
                float pk4 = std::fabs(y[c]);
                if (tp) {
                    const float a = hist[c][0], b = hist[c][1], cc = hist[c][2], d = y[c];
                    for (float t : {0.25f, 0.5f, 0.75f}) {
                        const float c1 = 0.5f * (cc - a), c2 = a - 2.5f * b + 2.0f * cc - 0.5f * d, c3 = 0.5f * (d - a) + 1.5f * (b - cc);
                        pk4 = std::max(pk4, std::fabs(((c3 * t + c2) * t + c1) * t + b));
                    }
                }
                hist[c][0] = hist[c][1]; hist[c][1] = hist[c][2]; hist[c][2] = y[c];
                peakEst[c] = pk4;
            }
            float reqC[kMaxCh];
            for (int c = 0; c < ch; ++c) reqC[c] = std::min(0.0f, gainToDb(ceil) - gainToDb(peakEst[c] + 1e-12f));
            if (ch == 2) { const float mn = std::min(reqC[0], reqC[1]); for (int c = 0; c < 2; ++c) reqC[c] += (mn - reqC[c]) * sLink; }
            float grC = 0;
            for (int c = 0; c < ch; ++c) {
                // fél periódus becslése: a legutóbbi nullátmenetek közti idő (simítva)
                const bool pos = y[c] >= 0.0f;
                if (pos != zcSign[c]) { zcAvg[c] += (float(zcCount[c]) - zcAvg[c]) * std::min(1.0f, kZc * float(zcCount[c] + 1)); zcCount[c] = 0; zcSign[c] = pos; }
                else if (zcCount[c] < holdMax) ++zcCount[c];
                const int holdN = std::clamp(int(1.25f * std::max(zcAvg[c], float(zcCount[c]))), holdMin, holdMax);
                const float m = cleanMin[c].push(reqC[c]);
                if (m <= cleanEnv[c]) { cleanEnv[c] = m; cleanHold[c] = holdN; }      // azonnal (az előretekintés miatt simán)
                else if (cleanHold[c] > 0) --cleanHold[c];                            // tartás
                else cleanEnv[c] += (m - cleanEnv[c]) * kClFast;                     // elengedés
                // rövidebb előretekintésnél az erősítést a különbséggel késleltetjük, hogy a csúcs a simítás végére essen
                const float g = padOn ? gainPad[c].tick(cleanBox[c].push(cleanEnv[c])) : cleanBox[c].push(cleanEnv[c]);
                const float yd = cleanDelay[c].tick(y[c]);
                // az előretekintés hossza a teljes késleltetésen belül: a hiányzó részt a fenti késleltető pótolja
                float o = yd * dbToGain(g * sCleanOn);
                // biztonsági háló: a plafon fölötti maradékot (ritka, köbös becslés hibája) lágyan levágja
                if (sCleanOn > 0.5f && std::fabs(o) > ceil) o = std::copysign(ceil, o);
                out[c][i] = o;
                grC = std::min(grC, g * sCleanOn);
            }
            grClean = std::min(grClean, grC);

            // ---------------- mérők + AUTO −14 LUFS ----------------
            double e = 0;
            for (int c = 0; c < ch; ++c) {
                const float k = kw[c][1].tick(kw[c][0].tick(out[c][i])); e += double(k) * k;
                const float a = std::fabs(out[c][i]);
                pk[c] = std::max(pk[c], a);
            }
            ms += (e - ms) * lufsK;
            if (autoL && cleanOnT > 0.5f) {
                const double l = -0.691 + 10.0 * std::log10(std::max(ms, 1e-12));
                if (l > -60.0) autoPushDb = clampf(autoPushDb + float((-14.0 - l) * 0.6 / sr), 0.0f, 18.0f);   // ≈ 0,6 dB/s
            }
        }
        for (int c = 0; c < kMaxCh; ++c) { pkHold[c] = std::max(pk[c], pkHold[c] * std::pow(meterK, float(n))); }
    }

    // ---- mérők (a panelnek) ----
    float takeDenGr() { const float g = grDen; grDen = 0; return g; }
    float takeCleanGr() { const float g = grClean; grClean = 0; return g; }
    float takePeakDb(int c) { const float v = gainToDb(std::max(pk[c], 1e-6f)); pk[c] = 0; return v; }
    float peakHoldDb(int c) const { return gainToDb(std::max(pkHold[c], 1e-6f)); }
    float lufs() const { return float(-0.691 + 10.0 * std::log10(std::max(ms, 1e-12))); }
    float pushAppliedDb() const { return autoPushDb; }
    double latencySeconds() const { return double(denLa + cleanMax) / sr; }

private:
    /// lágy vágás: −1,4 dBFS-ig egyenes, fölötte éles, sima térd ≈ −0,3 dBFS felé (a teljes szinten ≈ 1,9 % 3. felhang)
    static inline float softTop(float x) {
        const float a = std::fabs(x);
        if (a <= 1.0f) return x;
        return std::copysign(1.0f + 0.16f * std::tanh((a - 1.0f) / 0.16f), x);   // ≈ +1,3 dBFS felé hajlik
    }
    static inline float softClip3(float x) {
        // éles, de sima térd a plafon közelében: −1 dBFS-en még szinte tiszta (0,05 %), a teljes szinten ≈ 1,9 % 3. felhang
        const float a = std::fabs(x);
        if (a < 0.875f) return x;
        return std::copysign(0.875f + 0.11f * std::tanh((a - 0.875f) / 0.11f), x);
    }

    float sr = 48000, smoothK = 0.01f, lufsK = 0, meterK = 0.999f;
    int denLa = 158, cleanMax = 240, lastLa = -1;
    Delay denDelay[kMaxCh], cleanDelay[kMaxCh], dryDelay[kMaxCh], gainPad[kMaxCh];
    bool padOn = false;
    Oversampler2x os[kMaxCh], osDry[kMaxCh];
    SlidingMin denMin[kMaxCh], cleanMin[kMaxCh];
    BoxAvg denBox[kMaxCh], cleanBox[kMaxCh];
    float denDet[kMaxCh] = {-120, -120};   // érzékelő (dBFS)
    float denEnv[kMaxCh] = {0, 0}, cleanEnv[kMaxCh] = {0, 0};
    int denHold[kMaxCh] = {0, 0}, cleanHold[kMaxCh] = {0, 0}, zcCount[kMaxCh] = {0, 0};
    float zcAvg[kMaxCh] = {0, 0};
    bool zcSign[kMaxCh] = {true, true};
    float hist[kMaxCh][3] = {};
    Biquad kw[kMaxCh][2];
    double ms = 1e-6;
    float autoPushDb = 0;
    float grDen = 0, grClean = 0, pk[kMaxCh] = {0, 0}, pkHold[kMaxCh] = {0, 0};
    float sThr = 6, sDCeil = 0, sDenOn = 1, sCleanOn = 1, sMix = 1, sLink = 1, sCeil = -1, sPush = 0;
    bool primed = false;
};

} // namespace eki
