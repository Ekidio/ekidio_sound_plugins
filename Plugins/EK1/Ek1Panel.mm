// EK1 saját panel — EKI-stílus, a user színkérésével (2026-10-22): világos, fehéres-szürke szálcsiszolt lemez, sötét felirat,
//   ezüst karimás sötét gombok neonkék érték-ívvel (a ± gombokon középről); az OUTPUT pereme piros.
//   Oszlopok: TONE (AIR, FOCUS, BASS PUNCH — ±12 dB, a MASTEQUI görbéi) · LOUDNESS (PUSH = DENSITY, nagy VOLUME) ·
//   SHAPE (TRANSIENT, WIDTH, OUTPUT) · METER (név, EKI pajzs-VU: kék, belülről világító számlap, "EKIDIO SOUND", felül fehér /
//   alul piros mutató; POWER). EKI fiók (2026-10-22): FOCUS FREQ, TRANSIENT ATTACK / RELEASE, RELEASE MAX (a VOLUME lassú
//   fokozatának elengedése teljes VOLUME-nál), AUTO GAIN (alapban KI — az előlapról ide került, a user kérésére).
//   Oszlopsorrend: TONE · LOUDNESS · SHAPE · METER (a user választása, 2026-10-22: "határozottan VU jobbra"); a függőleges vonalak
//   az oszlopok közé esnek, így a VU nem lóg beléjük.
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kAir, kFocus, kPunch, kPush, kVolume, kWidth, kTransient, kOutput, kAutoGain, kRelMax, kTrAttack, kTrRelease, kFocusFreq,
                              kPushGR, kLimGR, kAutoMakeup, kVU };

constexpr int INK = 0x2B2F36, INK_DIM = 0x6C737D, CYAN = 0x22C3F0, CYAN_TXT = 0x1592C2, SILVER = 0xCDD2D8, RED_RIM = 0xC4222C;
const Look LOOK = {
    0xF2F3F5, 0xE2E5E9, 0xCED2D8, 0.55,   // fehéres-szürke, finoman szálcsiszolt
    {0xE6E8EB, 0x8E9196, 0x4C5055, 0x72767B, 0xC2C5C9},
    0xDDE0E4, 0xD3D7DC, 0xC6CAD0, 0.55,   // a fiók is világos szálcsiszolt (a sötét feliratok olvashatók)
    INK, INK_DIM, 0.55, 0.16,
};
constexpr CGFloat ROW1 = 112, ROW2 = 236;

// ---- oszlopok ----
enum Col { TONE, LOUD, SHAPE, METER };
constexpr CGFloat kColW[4] = {230, 258, 194, 172}, kGap = 16, kStart = 26;
const Col* order() { static const Col o[4] = {TONE, LOUD, SHAPE, METER}; return o; }
/// az oszlop bal széle
CGFloat colX(Col c) {
    CGFloat x = kStart;
    for (int i = 0; i < 4; ++i) { if (order()[i] == c) return x; x += kColW[order()[i]] + kGap; }
    return x;
}
CGFloat meterX() { return colX(METER) + kColW[METER] / 2; }
NSRect vuRect() { return NSMakeRect(meterX() - 80, 128, 160, 114); }

void bracket(CGFloat x0, CGFloat x1, CGFloat y, NSString* name, int ink) {
    line(CGPointMake(x0, y - 5), CGPointMake(x0, y), 1, rgb(ink, 0.7));
    line(CGPointMake(x1, y - 5), CGPointMake(x1, y), 1, rgb(ink, 0.7));
    NSFont* f = [NSFont fontWithName:@"AvenirNext-DemiBold" size:8];
    const CGFloat tw = [name sizeWithAttributes:@{NSFontAttributeName: f, NSKernAttributeName: @2}].width + 14;
    const CGFloat m = (x0 + x1) / 2;
    line(CGPointMake(x0, y), CGPointMake(m - tw / 2, y), 1, rgb(ink, 0.7));
    line(CGPointMake(m + tw / 2, y), CGPointMake(x1, y), 1, rgb(ink, 0.7));
    textFont(name, @"AvenirNext-DemiBold", CGPointMake(m, y), 8, rgb(ink), 2);
}

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{290, @"FOCUS centre"}, {485, @"TRANSIENT window / decay"}, {680, @"VOLUME release at full volume"},
                                {830, @"output locked at 0 dB"}});
    const CGFloat mx = meterX();
    textFont(@"EK1", @"Futura-Bold", CGPointMake(mx, 84), 30, rgb(INK), 4);
    textFont(@"MASTERING  CHAIN", @"AvenirNext-DemiBold", CGPointMake(mx, 110), 6.5, rgb(INK_DIM), 1.6);
    line(CGPointMake(mx - 56, 122), CGPointMake(mx + 56, 122), 1, rgb(INK, 0.7));
    line(CGPointMake(mx - 56, 122), CGPointMake(mx - 56, 117), 1, rgb(INK, 0.7));
    line(CGPointMake(mx + 56, 122), CGPointMake(mx + 56, 117), 1, rgb(INK, 0.7));
    const std::pair<Col, NSString*> names[3] = {{TONE, @"TONE"}, {LOUD, @"LOUDNESS"}, {SHAPE, @"SHAPE"}};
    for (auto& [c, n] : names) bracket(colX(c) + 4, colX(c) + kColW[c] - 4, 302, n, INK);
    // függőleges vonalak az oszlopok között
    CGFloat x = kStart;
    for (int i = 0; i < 3; ++i) {
        x += kColW[order()[i]];
        const CGFloat lx = x + kGap / 2;
        line(CGPointMake(lx, 66), CGPointMake(lx, 286), 1, rgb(0x000000, 0.28)); line(CGPointMake(lx + 1, 66), CGPointMake(lx + 1, 286), 1, rgb(0xFFFFFF, 0.05));
        x += kGap;
    }
}

void drawFace(PanelController& pc) {
    drawHeaderDynamic(pc);
    VUStyle v = dialStyle(@"EKIDIO SOUND", false, 4);
    v.legendSize = 10; v.legendKern = 1.8; v.subLegend = nil; v.needleTop = 0xFFFFFF; v.needleBottom = 0xC4222C;
    drawVU(vuRect(), pc.processing ? pc.get(kVU) : -20.0f, v);
    const CGFloat mx = meterX();
    drawLed(CGPointMake(mx, 268), pc.processing ? 0.9f : 0.0f, 0x46D35A, nil, 4.5);
    textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(mx, 286), 6.5, rgb(INK_DIM), 2);
    if (pc.drawerOpen && pc.get(kAutoGain) >= 0.5f)   // AUTO GAIN (fiók): a ténylegesen beállt kiegyenlítés
        textFont([NSString stringWithFormat:@"%+.1f dB", pc.get(kAutoMakeup)], @"AvenirNext-DemiBold", CGPointMake(830, H + 92), 8, rgb(CYAN_TXT), 1);
}

std::unique_ptr<PanelController> buildEk1Panel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0x5A616B; t.accent = CYAN_TXT; t.led = CYAN;
    t.minorTicks = 1; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xE8EDF2;
    t.buttonTop = 0xF2F3F5; t.buttonBot = 0xC9CDD3; t.buttonText = INK;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined; t.coloredLine = false; t.arc = CYAN;
    theme() = t;
    auto pc = makePanel(au, "ek1");
    pc->watched = {{kAutoMakeup, 0}, {kVU, -20}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawFace;
    auto& cs = pc->controls;
    auto v1 = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f", v]; };
    auto u1 = ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f", v]; };
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    NSArray* tl = @[@"-12", @"", @"-6", @"", @"0", @"", @"+6", @"", @"+12"];
    const CGFloat tx = colX(TONE), lx = colX(LOUD), sx = colX(SHAPE), mx = meterX();
    auto toneKnob = [&](AudioUnitParameterID id, CGFloat x, CGFloat y, NSString* name) {
        cs.push_back({Control::Knob, id, {x, y}, 24, name, tl, -12, 12, 0, dB});
        cs.back().labelSize = 9; cs.back().bipolar = true;
    };
    toneKnob(kAir, tx + 56, ROW1, @"AIR");
    toneKnob(kFocus, tx + 174, ROW1, @"FOCUS");
    toneKnob(kPunch, tx + 115, ROW2, @"BASS PUNCH");

    cs.push_back({Control::Knob, kPush, {lx + 48, 176}, 34, @"PUSH", @[@"0", @"10", @"20", @"30"], 0, 30, 0, u1}); cs.back().labelSize = 10.5;
    cs.push_back({Control::Knob, kVolume, {lx + 188, 168}, 56, @"VOLUME", ticks(0, 3, 6), 0, 15, 0, u1});
    cs.back().labelSize = 12;

    cs.push_back({Control::Knob, kTransient, {sx + 46, ROW1}, 22, @"TRANSIENT", @[@"-10", @"0", @"+10"], -10, 10, 0, v1}); cs.back().labelSize = 8.5; cs.back().bipolar = true;
    cs.push_back({Control::Knob, kWidth, {sx + 148, ROW1}, 22, @"WIDTH", @[@"-5", @"0", @"+5"], -5, 5, 0, v1}); cs.back().labelSize = 8.5; cs.back().bipolar = true;
    cs.push_back({Control::Knob, kOutput, {sx + 97, ROW2}, 22, @"OUTPUT", @[@"-12", @"0", @"+12"], -12, 12, 0, dB}); cs.back().labelSize = 8.5; cs.back().bipolar = true;

    // ---- EKI fiók ----
    const CGFloat dy = H + 58;
    auto ms = ^NSString*(float v) { return v >= 1000 ? @"1 s" : [NSString stringWithFormat:v < 10 ? @"%.1f ms" : @"%.0f ms", v]; };
    auto hz = ^NSString*(float v) { return v >= 1000 ? [NSString stringWithFormat:@"%.2f kHz", v / 1000] : [NSString stringWithFormat:@"%.0f Hz", v]; };
    auto drawerKnob = [&](AudioUnitParameterID id, CGFloat x, NSString* name, NSArray* labels, float lo, float hi, float def,
                          NSString* (^fmt)(float), std::vector<float> taper) {
        cs.push_back({Control::Knob, id, {x, dy}, 20, name, labels, lo, hi, def, fmt});
        cs.back().drawer = true; cs.back().labelSize = 8.5; cs.back().logTaper = true; cs.back().taper = taper;
    };
    drawerKnob(kFocusFreq, 290, @"FOCUS FREQ", @[@"300", @"600", @"1k", @"2k", @"5k"], 300, 5000, 1000, hz, {300, 600, 1000, 2000, 5000});
    drawerKnob(kTrAttack, 430, @"TR ATTACK", @[@"1", @"3", @"8", @"15", @"30"], 1, 30, 8, ms, {1, 3, 8, 15, 30});
    drawerKnob(kTrRelease, 540, @"TR RELEASE", @[@"5", @"20", @"60", @"150", @"300"], 5, 300, 20, ms, {5, 20, 60, 150, 300});
    drawerKnob(kRelMax, 680, @"RELEASE MAX", @[@"50", @"100", @"200", @"500", @"1k"], 50, 1000, 500, ms, {50, 100, 200, 500, 1000});
    cs.push_back({Control::LedButton, kAutoGain, {830, dy - 8}, 0.75, @"AUTO GAIN", nil, 0, 1, 0}); cs.back().drawer = true;

    for (auto& c : cs) if (c.kind == Control::Knob) c.rim = c.pid == kOutput ? RED_RIM : SILVER;   // ezüst karima, az OUTPUT-on piros
    pc->lock(kOutput, kAutoGain);
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildEk1Panel)
