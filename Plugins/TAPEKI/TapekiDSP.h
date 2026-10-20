// TapekiDSP.h — TAPEKI: ANALOG TAPE MACHINE, AU-független.
//
// Jelút (2x túlmintavételezve), csatornánként:
//   REC LEVEL → wow & flutter (modulált késleltetés) → felvételi kiemelés (magas polc) → MÁGNESES SZALAG: Jiles–Atherton
//   hiszterézis (adaptív al-lépéses RK2; halkan tiszta, hangosan lágyan tömörít és "ragaszt", a CAL szerint forróbb) → szalagzaj → lejátszó visszavétel (a kiemelés pontos inverze: kis jelen lapos, nagy jelen a
//   magasak hamarabb telnek) → head bump (mélyemelés + utána kis horpadás, a sebesség szerinti frekvencián) → mély levágás →
//   sebességfüggő tető (rés-veszteség) → BIAS-polc → REPRO LF / HF trim → lejátszó elektronika (trafó-szín) → MIX (fázishelyes száraz ág) → AUTO GAIN / LEVEL OUT
// Gépek: STUDIO (meleg, nagy head bump), MASTER (tisztább, nagyobb fejtartalék, szélesebb tető, kevés ingadozás),
//        CASSETTE (kis fejtartalék, lágyabb telítés, sötét tető, több zaj és ingadozás).
#pragma once
#include "EKIAnalog.h"
#include "EKIFilters.h"
#include <random>
#include <vector>

namespace eki {

struct TapekiParams {
    float recLevel = 4.7f;  // 0…10 (5 = 0 dB; lefelé −12, fölfelé +2,4 dB)
    int speed = 2;          // 0 = 7.5, 1 = 15, 2 = 30 ips
    int machine = 0;        // 0 = STUDIO, 1 = MASTER, 2 = CASSETTE
    float wow = 0;          // 0…10
    float noise = 1;        // 0…10
    int bias = 0;           // 0 = UNDER, 1 = NORM, 2 = OVER
    float bump = 0;         // 0…10 (0 = a régi 4-es emelés, 10 = a régi 14-es: több mélyemelés)
    int cal = 0;            // 0 = +3 (alap, a user kedvence), 1 = +6, 2 = +9 (a szalag "forrósága": 0 VU mennyire hajtja a szalagot)
    float reproLf = 0;      // −4…+4 dB (lejátszó mély-trim, 100 Hz polc)
    float reproHf = 0;      // −8…+8 dB (lejátszó magas-trim, széles 7 kHz-es polc; a 0 valójában +6 dB — a user kérése)
    float mix = 100;        // %
    float levelOutDb = 0;   // −12…+12
    float autoGain = 1;
};

/// gépenkénti jelleg
struct TapeMachine {
    float jaA, jaK, jaC;    // hiszterézis: anhiszterézis-alak, koercivitás (hurok-szélesség), reverzibilitás (nagyobb = tisztább)
    float driveDb;          // a szalag hajtása 0 VU-n a CAL +9-hez képest (kisebb fejtartalék = több)
    float emphDb, emphHz;   // felvételi kiemelés
    float hfScale;          // a tető a sebességhez képest
    float bumpDb, bumpScale;
    float wowPct, flutterPct;
    float airDb;            // a háttér-"fény" legnagyobb emelése (halk magas részleteken)
    float hissDb;           // zaj NOISE 10-en (dBFS, a kimeneten, az AUTO GAIN után)
};
inline constexpr TapeMachine kMachines[3] = {
    {0.50f, 0.20f, 0.90f,  7.0f, 6.0f, 3200.0f, 1.20f, 5.0f, 1.00f, 1.80f, 0.25f, 4.0f, -38.0f},   // STUDIO (modern, nagy kivezérelhetőségű szalag)
    {0.60f, 0.25f, 0.92f,  5.0f, 5.0f, 3800.0f, 1.25f, 3.0f, 1.10f, 1.00f, 0.15f, 5.0f, -44.0f},   // MASTER (tisztább, szélesebb)
    {0.45f, 0.30f, 0.82f, 10.0f, 9.0f, 2400.0f, 0.60f, 2.5f, 0.80f, 2.40f, 0.40f, 3.0f, -32.0f},   // CASSETTE (kis fejtartalék, szélesebb hurok)
};
inline constexpr float kSpeedIps[3] = {7.5f, 15.0f, 30.0f};

/// Jiles–Atherton mágneses hiszterézis (normált, Ms = 1). A szalag "emlékezete": a tranzienseket lágyan lekerekíti, a hurok
/// miatt a hang kicsit "összeragad"; halkan (nagy reverzibilitás = bias) tiszta. Adaptív al-lépéses RK2 — magas, hangos jelen is stabil.
struct Hysteresis {
    double a = 0.5, k = 0.2, c = 0.9, alpha = 0.002, T = 1.0 / 96000;
    double M = 0, Hp = 0;
    static double L(double x) { return std::fabs(x) < 1e-4 ? x / 3 : 1.0 / std::tanh(x) - 1.0 / x; }
    static double Ld(double x) { if (std::fabs(x) < 1e-4) return 1.0 / 3; const double s = std::sinh(x); return 1.0 / (x * x) - 1.0 / (s * s); }
    double dMdt(double m, double H, double dH) const {
        const double Q = (H + alpha * m) / a, man = L(Q), dman = Ld(Q) / a, diff = man - m;
        const double delta = dH >= 0 ? 1.0 : -1.0;
        const double dM = delta * diff > 0 ? 1.0 : 0.0;
        const double den1 = delta * std::max(delta * ((1 - c) * delta * k - alpha * diff), 0.25 * (1 - c) * k);   // nem mehet át nullán
        double den = 1 - c * alpha * dman;
        if (den < 1e-6) den = 1e-6;
        return dH * ((1 - c) * dM * diff / den1 + c * dman) / den;
    }
    inline double tick(double H) {
        const double dHtot = H - Hp;
        const int n = std::clamp(int(std::ceil(std::fabs(dHtot) / (0.04 * a))), 1, 16);
        const double h = T / n, dH = dHtot / T;
        for (int i = 0; i < n; ++i) {
            const double H0 = Hp + dHtot * i / n, H1 = Hp + dHtot * (i + 1) / n;
            const double k1 = h * dMdt(M, H0, dH);
            M += h * dMdt(M + 0.5 * k1, 0.5 * (H0 + H1), dH);
        }
        if (!std::isfinite(M)) M = 0;
        M = std::clamp(M, -1.2, 1.2);
        Hp = H;
        return M;
    }
    void reset() { M = Hp = 0; }
    /// kis jeles erősítés (a normáláshoz)
    double smallGain(double fs) const {
        Hysteresis h = *this; h.reset(); h.T = 1.0 / fs;
        double ei = 0, eo = 0; const int N = int(fs * 0.05);
        for (int i = 0; i < N; ++i) { const double x = 0.01 * std::sin(2 * M_PI * 1000.0 * i / fs), y = h.tick(x); if (i > N / 2) { ei += x * x; eo += y * y; } }
        return std::sqrt(eo / std::max(ei, 1e-30));
    }
};

class TapekiDSP {
public:
    static constexpr int kMaxCh = 2;
    static constexpr int kSub = 32;

    void prepare(float sampleRate) {
        sr = sampleRate; sr2 = 2 * sampleRate;
        for (int c = 0; c < kMaxCh; ++c) {
            dc[c].set(sr, 5.0f);
            kwIn[c][0].designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr2, 1681.0);
            kwIn[c][1].designMatched(Analog2::highPass(38.0, 0.5), sr2, 38.0);
            kwOut[c][0].designMatched(Analog2::highShelf(1681.0, 4.0, 0.71), sr2, 1681.0);
            kwOut[c][1].designMatched(Analog2::highPass(38.0, 0.5), sr2, 38.0);
            hissHp[c].set(300.0f, sr2);
            seLp[c].set(2500.0f, sr2);
            for (Biquad* b : {&airH1[c], &airH2[c]}) b->designHP2(9000.0, sr2);
            recMid[c].setIdentity();
            crHp[c].set(900.0f, sr2); crLp[c].set(7000.0f, sr2);
            xf[c].prepare(sr2, 30000.0f, 80.0f); xfDry[c].prepare(sr2, 30000.0f, 80.0f);
            line[c].assign(kLine, 0.0f);
        }
        smoothK = 1.0f - onePoleCoef(0.03f, sr / kSub);
        seAtt = 1.0f - onePoleCoef(0.0008f, sr2); seRel = 1.0f - onePoleCoef(0.06f, sr2);
        airAtt = 1.0f - onePoleCoef(0.0015f, sr2); airRel = 1.0f - onePoleCoef(0.08f, sr2); airFull = 1.0f - onePoleCoef(0.3f, sr2);
        crDecay = onePoleCoef(0.0004f, sr2); dropK = 1.0f - onePoleCoef(0.012f, sr2);
        kVu = 1.0f - onePoleCoef(0.3f, sr2);
        autoMk.prepare(sr2);
        needle.set(sr);
        lastSpeed = lastMachine = lastBias = -1; lastBump = -1; lastLf = lastHf = 1e9f; lastKnob = -1;
        sideHp.designHP2(200.0, sr); decHp.designHP2(200.0, sr); wK = 1.0f - onePoleCoef(0.25f, sr);
        decAp[0].designAllpass(650.0, 0.6, sr); decAp[1].designAllpass(2100.0, 0.6, sr); decAp[2].designAllpass(5800.0, 0.6, sr);
        // állandó késleltetés (a legnagyobb ingadozáshoz méretezve: kazetta, 7.5 ips) — a DAW-nak jelzett latencia, így
        // gép- vagy sebességváltáskor sem csúszik el a sáv
        const float aW = kMachines[2].wowPct * 0.01f * 1.4f * sr2 / (2.0f * kPi * 0.9f), aF = kMachines[2].flutterPct * 0.01f * 1.4f * sr2 / (2.0f * kPi * 8.0f);
        d0 = std::min(std::ceil(2.1f * (aW + aF)) + 8.0f, float(kLine - 8));
        reset();
    }

    void reset() {
        for (int c = 0; c < kMaxCh; ++c) {
            os[c].reset(); dc[c].reset();
            for (auto& k : kwIn[c]) k.reset();
            for (auto& k : kwOut[c]) k.reset();
            for (Biquad* b : {&emph[c], &deemph[c], &bumpB[c], &dipB[c], &lowCut[c], &hf1[c], &hf2[c], &biasSh[c], &reproLo[c], &reproHi[c]}) b->reset();
            tape[c].reset(); xf[c].reset(); xfDry[c].reset(); seLp[c].reset(); seEnv[c] = 0;
            hissHp[c].reset(); hissLp[c].reset(); crHp[c].reset(); crLp[c].reset(); crEnv[c] = 0;
            for (Biquad* b : {&airH1[c], &airH2[c], &midB[c], &recMid[c]}) b->reset();
            airEnv[c] = 0; airMs[c] = 1e-6f;
            std::fill(line[c].begin(), line[c].end(), 0.0f);
        }
        sideHp.reset(); decHp.reset(); wMs = wSs = 0; for (auto& b : decAp) b.reset();
        wp = 0; phW1 = phW2 = phF1 = phF2 = 0; drift = 0; vuMs = 0; drop = dropT = 1; dropLeft = 0;
        autoMk.reset(); needle.reset();
        primed = false;
    }

    void process(const float* const* in, float* const* out, int ch, int n, const TapekiParams& p) {
        ch = std::min(ch, kMaxCh);
        const int spd = std::clamp(p.speed, 0, 2), mac = std::clamp(p.machine, 0, 2), bia = std::clamp(p.bias, 0, 2);
        if (spd != lastSpeed || mac != lastMachine || bia != lastBias) { design(spd, mac, bia); lastSpeed = spd; lastMachine = mac; lastBias = bia; lastBump = -1; }
        const TapeMachine& M = kMachines[mac];
        const bool autoOn = p.autoGain >= 0.5f;
        // REC LEVEL: 5 = 0 dB; lefelé −12 dB-ig, fölfelé csak +2,4 dB-ig (a user kérése: a régi 6-os állás legyen a maximum)
        const float recRaw = clampf(p.recLevel, 0, 10) - 5.0f, recT = recRaw <= 0 ? recRaw * 2.4f : recRaw * 0.48f;
        // "csalás" a REC LEVEL-ben (a user kérése): a gombbal nő egy széles közép-emelés 2,4 kHz körül (0…+3 dB), és 5 fölött
        // a sztereókép 200 Hz felett fokozatosan szélesedik, max-on 130 %-ig
        const float knobT = clampf(p.recLevel, 0, 10);
        const float wowT = clampf(p.wow, 0, 10) * 0.1f, noiseT = clampf(p.noise, 0, 10) * 0.1f, bumpT = 0.4f + clampf(p.bump, 0, 10) * 0.1f;   // a gomb 0-ja = a régi 4-es
        const float mixT = clampf(p.mix, 0, 100) * 0.01f, outT = clampf(p.levelOutDb, -12, 12);
        const float calDb = std::clamp(p.cal, 0, 2) == 2 ? 0.0f : p.cal == 1 ? -3.0f : -6.0f;
        const float lfT = clampf(p.reproLf, -4, 4), hfT = clampf(p.reproHf, -8, 8) + 6.0f;   // a gomb 0-ja = +6 dB (a régi 4-es állás)
        if (std::fabs(lfT - lastLf) > 0.01f || std::fabs(hfT - lastHf) > 0.01f) {
            for (int c = 0; c < kMaxCh; ++c) {
                if (std::fabs(lfT) < 0.01f) reproLo[c].setIdentity(); else reproLo[c].designMatched(Analog2::lowShelf(100.0, lfT, 0.7), sr2, 100.0);
                if (std::fabs(hfT) < 0.01f) reproHi[c].setIdentity(); else reproHi[c].designMatched(Analog2::highShelf(7000.0, hfT, 0.6), sr2, 7000.0);
            }
            lastLf = lfT; lastHf = hfT;
        }
        if (!primed) { sKnob = knobT; sRec = recT; sWow = wowT; sNoise = noiseT; sBump = bumpT; sMix = mixT; sOut = outT; primed = true; }

        // ingadozás: a mélység mintában (a késleltetés-moduláció amplitúdója = eltérés · fs / (2π f))
        const float speedWow = spd == 0 ? 1.4f : spd == 1 ? 1.0f : 0.7f;   // lassabb szalag → nagyobb relatív ingadozás
        const float aWow = M.wowPct * 0.01f * speedWow * sr2 / (2.0f * kPi * 0.9f);
        const float aFl = M.flutterPct * 0.01f * speedWow * sr2 / (2.0f * kPi * 8.0f);

        for (int i0 = 0; i0 < n; i0 += kSub) {
            const int m = std::min(kSub, n - i0);
            sRec += (recT - sRec) * smoothK; sWow += (wowT - sWow) * smoothK; sNoise += (noiseT - sNoise) * smoothK;
            sKnob += (knobT - sKnob) * smoothK;
            if (std::fabs(sKnob - lastKnob) > 0.02f) {
                const double g = 0.3 * sKnob;
                for (int c = 0; c < kMaxCh; ++c) { if (g < 0.01) recMid[c].setIdentity(); else recMid[c].designMatched(Analog2::bell(2400.0, g, 0.5), sr2, 2400.0); }
                lastKnob = sKnob;
            }
            const float width = 1.0f + 0.30f * clampf((sKnob - 5.0f) / 5.0f, 0.0f, 1.0f);
            sBump += (bumpT - sBump) * smoothK; sMix += (mixT - sMix) * smoothK; sOut += (outT - sOut) * smoothK;
            if (std::fabs(sBump - lastBump) > 0.002f) { designBump(spd, mac, sBump); lastBump = sBump; }
            const float gRec = dbToGain(sRec);
            // a szalag: 0 VU (−12 dBFS csúcsú szinusz) CAL +9-en H ≈ 0,25 csúcsot ad; a CAL kisebb értéke hidegebb, a gép saját hajtása
            const float biasDrive = bia == 0 ? 1.5f : bia == 2 ? -1.5f : 0.0f;
            const double kH = dbToGain(calDb + M.driveDb + biasDrive), outNorm = 1.0 / (tapeGain * kH);
            // forróbb kalibráció: a zaj a jelhez képest halkabb
            const float hissAmp = sNoise > 1e-4f ? dbToGain(M.hissDb - calDb + 30.0f * std::log10(sNoise) + (spd == 0 ? 3.0f : spd == 2 ? -3.0f : 0.0f)) : 0.0f;
            // ingadozás: a gomb alján finom, a tetején erős szalaghúzás
            const float depth = std::pow(sWow, 1.3f);
            // "kosz": a WOW & FLUTTER felső felén pattogás és rövid kiesések (poros, gyűrött szalag)
            const float dirt = std::pow(clampf((sWow - 0.3f) / 0.7f, 0.0f, 1.0f), 1.3f);
            const float crRate = dirt * 7.0f / sr2, dropRate = dirt * 0.45f / sr2;
            const float airG = dbToGain(M.airDb) - 1.0f;
            for (int i = i0; i < i0 + m; ++i) {
                float u[kMaxCh][2];
                for (int c = 0; c < ch; ++c) os[c].up(in[c][i] * gRec, u[c][0], u[c][1]);
                for (int ph = 0; ph < 2; ++ph) {
                    // közös szalag-ingadozás (wow: két lassú szinusz + lassú sodródás; flutter: két gyors szinusz)
                    phW1 += 0.73f / sr2; phW2 += 1.13f / sr2; phF1 += 7.3f / sr2; phF2 += 11.9f / sr2;
                    for (float* ph_ : {&phW1, &phW2, &phF1, &phF2}) if (*ph_ >= 1.0f) *ph_ -= 1.0f;
                    drift += (uni(rng) - drift) * 1e-5f;   // lassú, véletlen "húzás"
                    const float mod = depth * (aWow * (0.6f * std::sin(2 * kPi * phW1) + 0.4f * std::sin(2 * kPi * phW2) + 1.0f * drift)
                                             + aFl * (0.7f * std::sin(2 * kPi * phF1) + 0.3f * std::sin(2 * kPi * phF2)));
                    const float rd = d0 + mod;
                    // kiesés (dropout): a szalag egy pillanatra elválik a fejtől — közös a két csatornán
                    if (dropLeft > 0) { if (--dropLeft == 0) dropT = 1; }
                    else if (dirt > 0 && uni01(rng) < dropRate) { dropT = 1.0f - (0.25f + 0.45f * uni01(rng)); dropLeft = int((0.04f + 0.11f * uni01(rng)) * sr2); }
                    drop += (dropT - drop) * dropK;
                    float pIn = 0, pOut = 0;
                    for (int c = 0; c < ch; ++c) {
                        auto& L = line[c];
                        L[size_t(wp)] = u[c][ph];
                        const float dry = L[size_t((wp - int(d0) + kLine) % kLine)];
                        const float w = readHermite(L, float(wp) - rd);
                        // szalag-szint a VU-hoz
                        if (c == 0) vuMs += (w * w - vuMs) * kVu;
                        // felvétel: kiemelés → telítés → zaj → lejátszó visszavétel
                        float e = emph[c].tick(w);
                        // önfelvétel-törlés (self-erasure): a hangos magas csúcsokat a szalag nem tudja felvenni — a kiemelt magas részt
                        // pillanatnyian, lágyan kerekíti (a keletkező felhangok a hallható sáv fölé esnek, a rés-veszteség levágja)
                        {
                            const float lo = seLp[c].lowpass(e), hiP = (e - lo) * float(kH);
                            e = lo + seThr * std::tanh(hiP / seThr) / float(kH);
                        }
                        const float s = float(tape[c].tick(e * kH) * outNorm);
                        float t = deemph[c].tick(s);
                        t = bumpB[c].tick(t); t = dipB[c].tick(t); t = lowCut[c].tick(t);
                        t = hf2[c].tick(hf1[c].tick(t)); t = biasSh[c].tick(t);
                        t = reproHi[c].tick(reproLo[c].tick(t));
                        t = midB[c].tick(t);                                      // lejátszó elektronika: széles, telt közép
                        t = recMid[c].tick(t);                                    // REC LEVEL "csalás": széles emelés 2,4 kHz körül
                        // "fény" (csalás a háttérben): a 9 kHz fölötti halk részleteket emeli, a hangosakat alig — selymes levegő
                        {
                            airMs[c] += (t * t - airMs[c]) * airFull;
                            const float hi = airH2[c].tick(airH1[c].tick(t)), a = std::fabs(hi);
                            airEnv[c] += (a - airEnv[c]) * (a > airEnv[c] ? airAtt : airRel);
                            const float thr = std::max(std::sqrt(airMs[c]) * 0.2f, 3e-5f);
                            const float d = hi * airG / (1.0f + std::pow(airEnv[c] / thr, 0.7f));
                            const float lim = 1.4f * airG * thr + 1e-9f;
                            t += lim * std::tanh(d / lim);
                        }
                        t *= drop;
                        t = xf[c].tick(t, 2.6f);                                  // lejátszó elektronika: finom trafó-szín a mélyben
                        const float dryA = xfDry[c].tickLinear(dry);              // a száraz ág ugyanazon a lineáris úton (MIX fésű nélkül)
                        const float o = dryA + (t - dryA) * sMix;
                        const float ki = kwIn[c][1].tick(kwIn[c][0].tick(dry)), ko = kwOut[c][1].tick(kwOut[c][0].tick(o));
                        pIn += ki * ki; pOut += ko * ko;
                        u[c][ph] = o;
                    }
                    wp = (wp + 1) % kLine;
                    autoMk.trackEnergy(pIn, pOut);
                    const float gOut = dbToGain(autoOn ? autoMk.tickEnergyDb(true) - sRec : sOut + autoMk.tickEnergyDb(false));
                    for (int c = 0; c < ch; ++c) {
                        u[c][ph] *= gOut;
                        // szalagzaj és pattogás a kimeneten (az AUTO GAIN nem veszi vissza; a MIX-szel együtt halkul)
                        float nz = 0;
                        if (hissAmp > 0) nz += hissAmp * hissLp[c].lowpass(hissHp[c].highpass(gauss(rng)));
                        if (dirt > 0) {
                            if (uni01(rng) < crRate) crEnv[c] = (uni(rng) < 0 ? -1.0f : 1.0f) * dbToGain(-40.0f + 16.0f * uni01(rng)) * (0.4f + 0.6f * dirt);
                            if (crEnv[c] != 0) { nz += crLp[c].lowpass(crHp[c].highpass(crEnv[c] * (0.6f + 0.4f * uni(rng)))); crEnv[c] *= crDecay; if (std::fabs(crEnv[c]) < 1e-6f) crEnv[c] = 0; }
                        }
                        u[c][ph] += nz * sMix;
                    }
                }
                for (int c = 0; c < ch; ++c) out[c][i] = dc[c].tick(os[c].down(u[c][0], u[c][1]));
                if (ch == 2) {   // REC LEVEL "csalás": szélesebb sztereókép 200 Hz felett (a mély közép marad)
                    const float mid = 0.5f * (out[0][i] + out[1][i]), side = 0.5f * (out[0][i] - out[1][i]);
                    // a meglévő oldaljel erősítése + monó forráson is: a középből fázistolt (allpass-láncos), 200 Hz feletti
                    // másolat az oldalra — monóban kiesik, így monókompatibilis
                    const float sHi = sideHp.tick(side);
                    const float dec = decHp.tick(decAp[2].tick(decAp[1].tick(decAp[0].tick(mid))));
                    // a fázistolt rész annyira jön be, amennyire a forrás monó (sztereó anyag a meglévő oldalt kapja szélesebbre)
                    wMs += (mid * mid - wMs) * wK; wSs += (side * side - wSs) * wK;
                    const float mono = clampf(1.0f - 2.5f * std::sqrt(wSs / std::max(wMs, 1e-12f)), 0.0f, 1.0f);
                    if (width > 1.0005f) {
                        const float sd = side + (width - 1.0f) * (sHi + 1.1f * mono * dec);
                        out[0][i] = mid + sd; out[1][i] = mid - sd;
                    }
                }
                needle.tick(std::sqrt(std::max(vuMs, 0.0f)) * dbToGain(12.0f + 3.0f));   // 0 VU = −12 dBFS csúcsú szinusz (a mutató a 0 körül mozog)
            }
        }
    }

    /// a jelzett latencia (alapmintában): a 2x tartomány d0-ja fele
    double latencySeconds() const { return double(int(d0)) / double(sr2); }
    float vuDb() const { return clampf(gainToDb(std::max(needle.pos, 1e-6f)), -20.0f, 3.0f); }
    float autoMakeupDb() const { return autoMk.value(); }

private:
    static constexpr int kLine = 8192;
    static inline float readHermite(const std::vector<float>& L, float pos) {
        while (pos < 0) pos += float(kLine);
        const int i1 = int(pos); const float f = pos - float(i1);
        const float y0 = L[size_t((i1 - 1 + kLine) % kLine)], y1 = L[size_t(i1 % kLine)], y2 = L[size_t((i1 + 1) % kLine)], y3 = L[size_t((i1 + 2) % kLine)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    /// a kiemelés pontos inverze (a számláló és a nevező felcserélve; a polcszűrő minimálfázisú, így stabil)
    static void invert(const Biquad& a, Biquad& b) {
        b.b0 = 1.0 / a.b0; b.b1 = a.a1 / a.b0; b.b2 = a.a2 / a.b0; b.a1 = a.b1 / a.b0; b.a2 = a.b2 / a.b0;
    }
    void design(int spd, int mac, int bia) {
        const TapeMachine& M = kMachines[mac];
        const float ips = kSpeedIps[spd];
        // kiemelés: lassabb szalagnál erősebb
        const float eDb = M.emphDb + (spd == 0 ? 2.0f : spd == 2 ? -1.5f : 0.0f);
        const double fHf = std::min(double(M.hfScale) * (spd == 0 ? 12000.0 : spd == 1 ? 18000.0 : 26000.0), 0.45 * sr2);
        // hiszterézis: a BIAS a reverzibilitást állítja (UNDER: szélesebb hurok, több torzítás; OVER: tisztább)
        const double cB = std::clamp(double(M.jaC) + (bia == 0 ? -0.06 : bia == 2 ? 0.04 : 0.0), 0.5, 0.97);
        for (int c = 0; c < kMaxCh; ++c) { tape[c].a = M.jaA; tape[c].k = M.jaK; tape[c].c = cB; tape[c].T = 1.0 / sr2; }
        tapeGain = tape[0].smallGain(sr2);
        const double midHz[3] = {1200, 1500, 1400}, midDb[3] = {1.6, 1.0, 2.0};
        for (int c = 0; c < kMaxCh; ++c) {
            midB[c].designMatched(Analog2::bell(midHz[mac], midDb[mac], 0.7), sr2, midHz[mac]);
            hissLp[c].set(float(std::min(fHf, 16000.0)), sr2);
            emph[c].designMatched(Analog2::highShelf(M.emphHz, eDb, 0.6), sr2, M.emphHz);
            invert(emph[c], deemph[c]);
            lowCut[c].designHP2(std::max(12.0, 18.0 * M.bumpScale * (ips / 15.0 * 0.35 + 0.65)), sr2, 0.6);   // 30 ips-en se vágja el a lábdobot
            hf1[c].designLP2(fHf, sr2, 0.62); hf2[c].designLP2(std::min(fHf * 1.6, 0.45 * double(sr2)), sr2, 0.55);
            if (bia == 1) biasSh[c].setIdentity();
            else biasSh[c].designMatched(Analog2::highShelf(6000.0, bia == 0 ? 1.5 : -2.0, 0.7), sr2, 6000.0);
        }
    }
    void designBump(int spd, int mac, float amount) {
        const TapeMachine& M = kMachines[mac];
        const double fB = M.bumpScale * (spd == 0 ? 45.0 : spd == 1 ? 62.0 : 95.0);
        const double g = M.bumpDb * amount;
        for (int c = 0; c < kMaxCh; ++c) {
            if (g < 0.01) { bumpB[c].setIdentity(); dipB[c].setIdentity(); continue; }
            bumpB[c].designMatched(Analog2::bell(fB, g, 1.2), sr2, fB);
            dipB[c].designMatched(Analog2::bell(fB * 2.3, -0.35 * g, 1.4), sr2, fB * 2.3);
        }
    }
    float gauss(std::minstd_rand& r) { return (uni(r) + uni(r) + uni(r)) * 1.0f; }
    float uni01(std::minstd_rand& r) { return float(r()) / float(std::minstd_rand::max()); }
    float uni(std::minstd_rand& r) { return float(r()) / float(std::minstd_rand::max()) * 2.0f - 1.0f; }

    float sr = 48000, sr2 = 96000, smoothK = 0.1f, kVu = 0;
    Oversampler2x os[kMaxCh];
    DCBlocker dc[kMaxCh];
    Biquad kwIn[kMaxCh][2], kwOut[kMaxCh][2];
    Biquad emph[kMaxCh], deemph[kMaxCh], bumpB[kMaxCh], dipB[kMaxCh], lowCut[kMaxCh], hf1[kMaxCh], hf2[kMaxCh], biasSh[kMaxCh];
    Biquad reproLo[kMaxCh], reproHi[kMaxCh];
    Hysteresis tape[kMaxCh];
    Transformer xf[kMaxCh], xfDry[kMaxCh];
    double tapeGain = 0.6;
    OnePole seLp[kMaxCh], hissLp[kMaxCh], crHp[kMaxCh], crLp[kMaxCh];
    Biquad airH1[kMaxCh], airH2[kMaxCh], midB[kMaxCh], recMid[kMaxCh], sideHp, decHp, decAp[3];
    float sKnob = 5, lastKnob = -1, wMs = 0, wSs = 0, wK = 0;
    float airEnv[kMaxCh] = {0, 0}, airMs[kMaxCh] = {1e-6f, 1e-6f}, airAtt = 0, airRel = 0, airFull = 0;
    float crEnv[kMaxCh] = {0, 0}, crDecay = 0, drop = 1, dropT = 1, dropK = 0;
    int dropLeft = 0;
    float seEnv[kMaxCh] = {0, 0}, seAtt = 0, seRel = 0, seThr = 0.40f;   // self-erasure küszöb H-egységben (0 VU magas ≈ 0,05–0,1)
    float lastLf = 1e9f, lastHf = 1e9f;
    OnePole hissHp[kMaxCh];
    std::vector<float> line[kMaxCh];
    int wp = 0;
    float d0 = 64;
    float phW1 = 0, phW2 = 0, phF1 = 0, phF2 = 0, drift = 0, vuMs = 0;
    std::minstd_rand rng{12345};
    AutoMakeup autoMk;
    VUNeedle needle;
    int lastSpeed = -1, lastMachine = -1, lastBias = -1;
    float lastBump = -1;
    float sRec = 0, sWow = 0, sNoise = 0, sBump = 0, sMix = 1, sOut = 0;
    bool primed = false;
};

} // namespace eki
