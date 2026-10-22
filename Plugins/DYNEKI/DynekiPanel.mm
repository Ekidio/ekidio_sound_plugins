// DYNEKI-6 saját panel — EKIDIO SOUND rack-stílus (a jóváhagyott GUI-terv: Tools/dyneki_mockup.mm):
//   sötét grafit szálcsiszolt előlap, balra a hat EQ-sáv (piros/kék/szürke jelölésű esztergált gombok, arany AIR mező),
//   középen a fekete COMP modul a pajzs-VU-val (GR; 2026-10-23 óta a SAT nélkül), jobbra OUTPUT, AUTO GAIN és POWER.
//   M/S módban az EDIT kapcsoló választja, hogy a gombok a MID vagy a SIDE készletet mutassák (SIDE-nál borostyán sávcímek).
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID {
    kHpf, kLowG, kLowF, kLmG, kLmF, kHmG, kHmF, kHighG, kHighF, kAirG, kAirF,
    kVoice, kMode, kSat, kComp, kOutput, kAutoGain, kAnalog, kLmQ, kHmQ, kMix,
    kSHpf, kSLowG, kSLowF, kSLmG, kSLmF, kSHmG, kSHmF, kSHighG, kSHighF, kSAirG, kSAirF,
    kEdit, kVU, kGR, kAutoMakeup
};
constexpr AudioUnitParameterID kSideOffset = kSHpf - kHpf;

constexpr int INK = 0xECEDEF, INK_DIM = 0xA4A8AE, SIDE_INK = 0xFFB347;
const Look LOOK = {
    0x3B3D42, 0x323438, 0x292B2F, 0.9,
    {0xE4E6E9, 0x8C9095, 0x4C5055, 0x72767B, 0xC2C5C9},
    0x2F3135, 0x34363A, 0x292B2E, 1.0,
    INK, INK_DIM, 0.12, 0.32,
};
constexpr int RED = 0xE0442F, BLUE = 0x3E86E0, GREY = 0xB9C0C8, GOLD = 0xE2B04A, AIR_INK = 0x14100A;
const CGFloat COL[] = {74, 160, 246, 332, 418, 504};    // HPF, LOW, LOW MID, HIGH MID, HIGH, AIR
const NSRect kAir = NSMakeRect(461, 82, 86, 230);
const NSRect kDyn = NSMakeRect(560, 56, 290, 256);
constexpr CGFloat OUTX = 898;

bool msOn(const PanelController& pc) { return pc.get(kMode) >= 0.5f; }
bool editSide(const PanelController& pc) { return msOn(pc) && pc.get(kEdit) >= 0.5f; }

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{330, @"color amount"}, {520, @"mid bandwidth (AUTO = proportional)"}, {780, @"parallel"},
                               {110, @"signal flow:  EQ  ›  COMP  ›  OUT"}});
    // AIR: arany eloxált mező
    NSBezierPath* a = [NSBezierPath bezierPathWithRoundedRect:kAir xRadius:8 yRadius:8];
    shadowed(1, 4, 0.5, ^{ [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xD9B25C), 0.0, rgb(0xB88F3A), 0.55, rgb(0x9A752B), 1.0, nil]
                               drawInBezierPath:a angle:-90]; });
    [rgb(0xFFF2C8, 0.35) setStroke]; a.lineWidth = 0.8; [a stroke];
    // COMP modul: fekete betét csavarokkal
    NSBezierPath* d = [NSBezierPath bezierPathWithRoundedRect:kDyn xRadius:9 yRadius:9];
    shadowed(2, 6, 0.6, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x1C1D20) endingColor:rgb(0x0F1012)] drawInBezierPath:d angle:-90]; });
    [rgb(0xFFFFFF, 0.10) setStroke]; d.lineWidth = 0.8; [d stroke];
    for (CGPoint s : {CGPointMake(NSMinX(kDyn) + 11, NSMinY(kDyn) + 11), CGPointMake(NSMaxX(kDyn) - 11, NSMinY(kDyn) + 11),
                      CGPointMake(NSMinX(kDyn) + 11, NSMaxY(kDyn) - 11), CGPointMake(NSMaxX(kDyn) - 11, NSMaxY(kDyn) - 11)})
        drawScrew(s, 3.2);
    textFont(@"OUTPUT", @"AvenirNext-DemiBold", CGPointMake(OUTX, 100), 9.5, rgb(INK), 2.2);
    textFont(@"MODE", @"AvenirNext-DemiBold", CGPointMake(56, 72), 7.5, rgb(INK_DIM), 2);
    // finom elválasztók a sávok között
    for (int i = 0; i < 4; ++i) {
        const CGFloat x = (COL[i] + COL[i + 1]) / 2;
        line(CGPointMake(x, 112), CGPointMake(x, 302), 1, rgb(0x000000, 0.25));
        line(CGPointMake(x + 1, 112), CGPointMake(x + 1, 302), 1, rgb(0xFFFFFF, 0.05));
    }
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    // sávcímek: SIDE szerkesztésekor borostyán (jelzi, hogy a gombok a SIDE készletet mutatják)
    const bool side = editSide(pc);
    const char* names[] = {"HPF", "LOW", "LOW MID", "HIGH MID", "HIGH", "AIR"};
    for (int i = 0; i < 6; ++i) {
        const int col = i == 5 ? (side ? 0x6A3A00 : 0x2A1E08) : (side ? SIDE_INK : INK);
        textFont(@(names[i]), @"AvenirNext-DemiBold", CGPointMake(COL[i], 100), 9.5, rgb(col), 2.2);
    }
    textFont(@"EDIT", @"AvenirNext-DemiBold", CGPointMake(236, 72), 7.5, rgb(INK_DIM, msOn(pc) ? 1.0 : 0.35), 2);
    if (!msOn(pc)) textFont(@"—", @"AvenirNext-DemiBold", CGPointMake(314, 72), 9, rgb(INK_DIM, 0.35), 0);
    drawVU(NSMakeRect(586, 66, 238, 150), pc.get(kVU), dialStyle(@"DYNEKI-6", true));
    drawLed(CGPointMake(OUTX, 290), pc.processing ? 0.85f : 0.0f, 0xFF3A22, nil, 5.5);   // a plugin be/ki állapotát követi
    textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(OUTX, 305), 6.5, rgb(INK_DIM), 2);
}

std::unique_ptr<PanelController> buildDynekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xE4E8EC; t.accent = 0xFFD27A; t.led = 0xFF5A2A;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF6F6F6;
    t.buttonTop = 0x2E2F34; t.buttonBot = 0x0E0F11; t.buttonText = 0xE8EAEE;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined; t.coloredLine = true;
    theme() = t;
    auto pc = makePanel(au, "dyneki");
    pc->watched = {{kVU, 0}, {kGR, 0}, {kAutoMakeup, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };
    auto& cs = pc->controls;

    // ---- M/S sor ----
    cs.push_back({Control::RadioRow, kMode, {134, 72}, 1.05, nil, @[@"L/R", @"M/S"], 0, 1, 0});
    cs.push_back({Control::RadioRow, kEdit, {314, 72}, 1.05, nil, @[@"MID", @"SIDE"], 0, 1, 0});
    cs.back().visibleIf = [](const PanelController& p) { return msOn(p); };

    // ---- EQ: két készlet ugyanott (L/R vagy MID, illetve SIDE), mindig csak az egyik látszik ----
    for (int set = 0; set < 2; ++set) {
        const AudioUnitParameterID o = set ? kSideOffset : 0;
        auto vis = set ? std::function<bool(const PanelController&)>([](const PanelController& p) { return editSide(p); })
                       : std::function<bool(const PanelController&)>([](const PanelController& p) { return !editSide(p); });
        cs.push_back({Control::SteppedKnob, kHpf + o, {COL[0], 168}, 22, @"FREQ Hz", @[@"OFF", @"50", @"80", @"160", @"300"], 0, 4, 0});
        cs.back().spread = 120; cs.back().cap = GREY; cs.back().labelSize = 8.5; cs.back().visibleIf = vis;
        // gDef/fDefMain: a fő készlet alapja (a nullázás: minden 0, csak az AIR +5 dB 20 kHz-en); a SIDE készlet sima (0 dB, fDef)
        struct B { AudioUnitParameterID g, f; int col, cap; NSArray* freqs; NSString* unit; int fDef; float lo, hi; float gDef; int fDefMain; };
        const B bands[] = {
            {kLowG, kLowF, 1, BLUE, @[@"15", @"35", @"60", @"110", @"220"], @"Hz", 2, -16, 16, 0, 2},
            {kLmG, kLmF, 2, RED, @[@".2", @".4", @".8", @"1.5", @"3"], @"kHz", 1, -16, 16, 0, 1},
            {kHmG, kHmF, 3, RED, @[@"1.5", @"3", @"5", @"7", @"10"], @"kHz", 2, -16, 16, 0, 2},
            {kHighG, kHighF, 4, BLUE, @[@"8", @"10", @"12", @"16"], @"kHz", 2, -16, 16, 0, 2},
            {kAirG, kAirF, 5, GOLD, @[@"2.5", @"5", @"10", @"20", @"40"], @"kHz", 3, 0, 15, 5, 3},
        };
        for (const auto& b : bands) {
            const bool air = b.col == 5;
            cs.push_back({Control::Knob, b.g + o, {COL[b.col], 168}, 25, @"GAIN",
                          b.lo < 0 ? ticks(-16, 16, 3) : ticks(0, 5, 4), b.lo, b.hi, set ? 0.0f : b.gDef, dB});
            cs.back().cap = b.cap; cs.back().labelSize = 8.5; cs.back().visibleIf = vis;
            if (air) cs.back().ink = AIR_INK;
            cs.push_back({Control::SteppedKnob, b.f + o, {COL[b.col], 262}, 14, b.unit, b.freqs, 0, float(b.freqs.count - 1), float(set ? b.fDef : b.fDefMain)});
            cs.back().spread = 115; cs.back().cap = b.cap; cs.back().labelSize = 8; cs.back().visibleIf = vis;
            if (air) cs.back().ink = AIR_INK;
        }
    }
    // VOICE (globális) a HPF alatt
    cs.push_back({Control::Selector, kVoice, {COL[0], 262}, 20, @"VOICE", @[@"BRIT", @"PUNCH"], 0, 1, 1});
    cs.back().spread = 45; cs.back().labelDy = 30;

    // ---- COMP (értékkijelzés nélkül: amire tekered, az a jó) ----
    cs.push_back({Control::Knob, kComp, {NSMidX(kDyn), 260}, 24, @"COMP", ticks(0, 2, 6), 0, 10, 3});
    cs.back().labelSize = 9.5; cs.back().cap = RED;

    // ---- OUTPUT + AUTO GAIN ----
    cs.push_back({Control::Knob, kOutput, {OUTX, 168}, 25, @"GAIN", ticks(-12, 12, 3), -12, 12, 0, dB});
    cs.back().cap = RED; cs.back().labelSize = 8.5;
    cs.push_back({Control::LedButton, kAutoGain, {OUTX, 246}, 0.8, @"AUTO GAIN", nil, 0, 1, 1});

    // ---- EKI fiók ----
    const CGFloat dy = H + 58;
    cs.push_back({Control::Knob, kAnalog, {330, dy}, 22, @"ANALOG", nil, 0, 100, 0, pct});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = RED;
    NSArray* qs = @[@"AUTO", @".5", @"1", @"2", @"4"];
    cs.push_back({Control::SteppedKnob, kLmQ, {470, dy}, 17, @"LOW MID Q", qs, 0, 4, 0});
    cs.back().drawer = true; cs.back().labelSize = 8.5; cs.back().spread = 115;
    cs.push_back({Control::SteppedKnob, kHmQ, {570, dy}, 17, @"HIGH MID Q", qs, 0, 4, 0});
    cs.back().drawer = true; cs.back().labelSize = 8.5; cs.back().spread = 115;
    cs.push_back({Control::Knob, kMix, {780, dy}, 22, @"MIX", nil, 0, 100, 100, pct});
    cs.back().drawer = true; cs.back().labelSize = 9;

    pc->lock(kOutput, kAutoGain);   // AUTO GAIN alatt a kimenet 0 dB-en, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildDynekiPanel)
