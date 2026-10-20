// TAPEKI saját panel — a jóváhagyott GUI-terv (Tools/tapeki_mockup.mm) szerint, a közös rajzokkal (TapekiArt.h):
//   meleg "pezsgő" alumínium előlap; balra az orsó-ablak (STUDIO / MASTER / CASSETTE saját látvánnyal, szalagkóddal),
//   alatta MACHINE; középen fehér VU (a gép szalagkódjával), alatta WOW & FLUTTER és NOISE; nagy REC LEVEL, alatta SPEED;
//   jobbra a név, AUTO GAIN és POWER; fiókban CAL, BIAS, HEAD BUMP, REPRO LF / HF, MIX, LEVEL OUT.
// Az orsók forognak, amíg a plugin dolgozik (kifutással állnak meg); a tempó a SPEED-et és a gépet követi, a WOW & FLUTTER
// kicsit egyenetlenné teszi; a szalag lassan átvándorol a jobb orsóra, a végén visszateker.
#include "TapekiArt.h"
#include <memory>

using namespace eki::ui;
using namespace eki::ui::rack;
using namespace tapeki_art;

namespace {

enum : AudioUnitParameterID { kRecLevel, kSpeed, kMachine, kWow, kNoise, kBias, kBump, kCal, kReproLf, kReproHf, kMix, kLevelOut, kAutoGain,
                              kTapeVU, kAutoMakeup };

/// az orsók mozgásállapota (példányonként)
struct Reels {
    double angL = 0, angR = 40, frac = 0.85, vel = 0, t = 0, lastT = 0;
    float vuShown = -20;   // a kijelzett VU: ha nem szól, a mutató visszaesik (nem ragad a pirosban)
    bool rewinding = false;

    /// true, ha újra kell rajzolni
    bool step(PanelController& pc) {
        const double now = CFAbsoluteTimeGetCurrent();
        const double dt = lastT > 0 ? std::clamp(now - lastT, 0.0, 0.1) : 0.0;
        lastT = now; t += dt;
        const int mac = std::clamp(int(std::lround(pc.get(kMachine))), 0, 2), spd = std::clamp(int(std::lround(pc.get(kSpeed))), 0, 2);
        // felfutás ~0,3 s, kifutás ~0,7 s
        const double target = pc.processing ? 1.0 : 0.0;
        vel += (target - vel) * std::min(1.0, dt * (target > vel ? 3.5 : 1.5));
        if (vel < 0.002 && target == 0) vel = 0;
        // VU: ha a plugin dolgozik, a mért értéket követi; ha leállt, a mutató a valódi VU-tempóval visszaesik a nyugalmi helyére
        const float vuT = pc.processing ? pc.get(kTapeVU) : -20.0f, vuPrev = vuShown;
        vuShown = pc.processing ? vuT : vuShown + (vuT - vuShown) * float(std::min(1.0, dt * 6.0));
        const bool vuMoved = std::fabs(vuShown - vuPrev) > 0.02f;
        if (rewinding) {   // visszatekerés: gyorsan, visszafelé
            frac = std::min(0.85, frac + dt / 3.0);
            const double w = 900 * dt;
            angL -= w * (1.2 - frac * 0.5); angR -= w * (0.7 + frac * 0.5);
            if (frac >= 0.85) rewinding = false;
            return true;
        }
        if (vel <= 0) return vuMoved;
        // a szalag sebessége pixelben (STUDIO / MASTER / CASSETTE saját tempó, a SPEED szorozza)
        const double lin[3] = {140, 110, 90}, mul[3] = {0.5, 1.0, 2.0};
        const double w = pc.get(kWow) * 0.1;
        const double v = lin[mac] * mul[spd] * vel * (1 + w * (0.06 * std::sin(2 * M_PI * 0.8 * t) + 0.02 * std::sin(2 * M_PI * 7.0 * t)));
        // tekercssugár a szalag mennyisége szerint (a kazetta agya kisebb, így gyorsabban forog)
        const bool cas = mac == 2;
        const double h2 = cas ? 144.0 : 576.0, m2 = cas ? 961.0 : 3844.0;
        const double rL = std::sqrt(h2 + frac * (m2 - h2)), rR = std::sqrt(h2 + (1 - frac) * (m2 - h2));
        angL += v / rL * dt * 180 / M_PI;
        angR += v / rR * dt * 180 / M_PI;
        frac -= v * dt / 67200.0;            // egy "tekercs" kb. 8 perc 15 ips-en
        if (frac <= 0.02) rewinding = true;
        angL = std::fmod(angL, 360.0); angR = std::fmod(angR, 360.0);
        return true;
    }
};

std::unique_ptr<PanelController> buildTapekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = INK; t.accent = VALUE; t.led = RED;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF4F1EA;
    t.buttonTop = 0x3A3B40; t.buttonBot = 0x111214; t.buttonText = 0xEDEDED;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined;
    theme() = t;
    auto pc = makePanel(au, "tapeki");
    pc->watched = {{kTapeVU, -20}, {kAutoMakeup, 0}};
    auto reels = std::make_shared<Reels>();
    pc->animate = [reels](PanelController& p) { return reels->step(p); };
    pc->drawStatic = [](PanelController& p) {
        drawChrome(p, LOOK, nil, {{250, @"tape calibration · bias"}, {500, @"head bump · repro EQ"}, {780, @"blend · final level (AUTO GAIN off)"}});
        textFont(@"TAPEKI", @"Futura-Bold", CGPointMake(NAME_X, 84), 23, rgb(INK), 3);
        textFont(@"ANALOG TAPE", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 106), 6.5, rgb(INK_DIM), 1.6);
        textFont(@"MACHINE", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 116), 6.5, rgb(INK_DIM), 1.6);
        line(CGPointMake(NAME_X - 62, 128), CGPointMake(NAME_X + 62, 128), 1, rgb(INK, 0.7));
        line(CGPointMake(NAME_X - 62, 128), CGPointMake(NAME_X - 62, 123), 1, rgb(INK, 0.7));
        line(CGPointMake(NAME_X + 62, 128), CGPointMake(NAME_X + 62, 123), 1, rgb(INK, 0.7));
        bracket(NSMinX(kWin), NSMaxX(kWin), 302, @"MACHINE");
        bracket(NSMinX(kVU), NSMaxX(kVU), 302, @"TAPE");
        bracket(REC_X - 64, REC_X + 64, 302, @"SPEED  ·  IPS");
    };
    pc->drawFace = [reels](PanelController& p) {
        drawHeaderDynamic(p);
        const int mac = std::clamp(int(std::lround(p.get(kMachine))), 0, 2);
        drawWindow(reels->angL, reels->angR, reels->frac, mac);
        NSString* const vuCodes[3] = {@"UT-1562", @"BL-43T", @"MX-C90"};   // a VU-n a gép szalagkódja
        VUStyle v = dialStyle(vuCodes[mac], false, 1); v.legendSize = 12; v.subLegend = @"REC LEVEL";   // fehér számlap
        drawVU(kVU, reels->vuShown, v);
        drawLed(CGPointMake(NAME_X, 290), p.processing ? 0.9f : 0.0f, 0x46D35A, nil, 4.5);   // a plugin be/ki állapotát követi
        textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 305), 6.5, rgb(INK_DIM), 2);
    };
    auto& cs = pc->controls;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    // MACHINE gombsor az orsó-ablak alatt
    cs.push_back({Control::RadioRow, kMachine, {NSMidX(kWin), 278}, 1.1, nil, @[@"STUDIO", @"MASTER", @"CASSETTE"], 0, 2, 0});
    cs.back().rowWiden = 1.9;
    // WOW & FLUTTER, NOISE a VU alatt
    cs.push_back({Control::Knob, kWow, {NSMinX(kVU) + 50, 236}, 20, @"WOW & FLUTTER", ticks(0, 2, 6), 0, 10, 0});
    cs.back().labelSize = 8.5; cs.back().cap = RED;
    cs.push_back({Control::Knob, kNoise, {NSMaxX(kVU) - 50, 236}, 20, @"NOISE", ticks(0, 2, 6), 0, 10, 1});
    cs.back().labelSize = 8.5; cs.back().cap = RED;
    // REC LEVEL: nagy hullámos gomb
    cs.push_back({Control::Knob, kRecLevel, {REC_X, 152}, 48, @"REC LEVEL", ticks(0, 1, 11), 0, 10, 4.7f});
    cs.back().knobStyle = Fluted; cs.back().labelSize = 11;
    // SPEED gombsor
    cs.push_back({Control::RadioRow, kSpeed, {REC_X, 278}, 1.1, nil, @[@"7.5", @"15", @"30"], 0, 2, 2});
    cs.back().rowWiden = 1.2;
    // AUTO GAIN
    cs.push_back({Control::LedButton, kAutoGain, {NAME_X, 222}, 0.8, @"AUTO GAIN", nil, 0, 1, 1});

    // EKI fiók: CAL, BIAS, HEAD BUMP, REPRO LF / HF, MIX, LEVEL OUT
    const CGFloat dy = H + 58;
    auto dB1 = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    cs.push_back({Control::SteppedKnob, kCal, {200, dy}, 20, @"CAL", @[@"+3", @"+6", @"+9"], 0, 2, 0});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = RED; cs.back().spread = 70;
    cs.push_back({Control::SteppedKnob, kBias, {300, dy}, 20, @"BIAS", @[@"UNDER", @"NORM", @"OVER"], 0, 2, 0});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = RED; cs.back().spread = 70;
    cs.push_back({Control::Knob, kBump, {410, dy}, 22, @"HEAD BUMP", ticks(0, 2, 6), 0, 10, 0});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = RED;
    cs.push_back({Control::Knob, kReproLf, {520, dy}, 20, @"REPRO LF", ticks(-4, 2, 5), -4, 4, 0, dB1});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = RED;
    cs.push_back({Control::Knob, kReproHf, {620, dy}, 20, @"REPRO HF", ticks(-8, 4, 5), -8, 8, 0, dB1});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = RED;
    cs.push_back({Control::Knob, kMix, {735, dy}, 22, @"MIX", nil, 0, 100, 100, pct});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = RED;
    cs.push_back({Control::Knob, kLevelOut, {845, dy}, 22, @"LEVEL OUT", ticks(-12, 12, 3), -12, 12, 0, dB});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = RED;

    pc->lock(kLevelOut, kAutoGain);   // AUTO GAIN alatt a kimenet 0 dB-en, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildTapekiPanel)
