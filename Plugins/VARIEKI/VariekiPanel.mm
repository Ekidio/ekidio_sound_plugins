// VARIEKI saját panel — EKIDIO SOUND rack-stílus a klasszikus sztereó vari-mu stúdiókompresszorok hangulatában:
//   pala-kék szálcsiszolt előlap, két oldalt a családi pajzs-VU (bal: csökkentés, jobb: kimenet),
//   középen fekete vezérlőpanel csavarokkal; a precíziós finomhangolás a fiókban
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kThreshold, kCrest, kRatio, kKnee, kAttack, kRelPeak, kRelRms, kStereo, kScCut, kMakeup,
                              kAutoGain, kAnalog, kMix, kMeterMode, kVU, kGR, kAutoMakeup, kGRNeedle, kOutVU };

// pala-kék (szürkés indigó) előlap, fehér gravírozás
constexpr int INK = 0xE9ECF4, INK_DIM = 0xAEB5CC;
const Look LOOK = {
    0x414864, 0x373D57, 0x2E334A, 0.8,
    {0xE6E8EB, 0x8E9196, 0x4E5156, 0x74777C, 0xC4C7CB},
    0x30364E, 0x353B55, 0x2B3046, 0.9,
    INK, INK_DIM, 0.22, 0.26,
};
const NSRect kLeftWin = NSMakeRect(28, 62, 246, 160), kRightWin = NSMakeRect(686, 62, 246, 160);
const NSRect kCenter = NSMakeRect(286, 74, 388, 238);

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {});
    textFont(@"STEREO VARIABLE-MU DYNAMICS", @"AvenirNext-DemiBold", CGPointMake(480, 63), 8.5, rgb(INK), 3);
    // középső fekete vezérlőpanel csavarokkal
    NSBezierPath* cp = [NSBezierPath bezierPathWithRoundedRect:kCenter xRadius:8 yRadius:8];
    shadowed(2, 6, 0.6, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x1E1F22) endingColor:rgb(0x111213)] drawInBezierPath:cp angle:-90]; });
    [rgb(0xFFFFFF, 0.10) setStroke]; cp.lineWidth = 0.8; [cp stroke];
    for (CGPoint s : {CGPointMake(NSMinX(kCenter) + 11, NSMinY(kCenter) + 11), CGPointMake(NSMaxX(kCenter) - 11, NSMinY(kCenter) + 11),
                      CGPointMake(NSMinX(kCenter) + 11, NSMaxY(kCenter) - 11), CGPointMake(NSMaxX(kCenter) - 11, NSMaxY(kCenter) - 11)})
        drawScrew(s, 3.4);
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    // a családi pajzs-VU két oldalt: bal = csökkentés (GR-skála), jobb = kimeneti VU
    VUStyle gr = dialStyle(@"VARIEKI", true);
    gr.legendSize = 13; gr.subLegend = @"REDUCTION dB";
    drawVU(kLeftWin, pc.get(kGRNeedle), gr);
    VUStyle vu = dialStyle(@"VARIEKI");
    vu.legendSize = 13; vu.subLegend = @"OUTPUT VU";
    drawVU(kRightWin, pc.get(kOutVU), vu);
    // TUBE LED a jobb ablak alatt: a cső-szín mértékével és a csökkentéssel izzik
    const float color = pc.get(kAnalog) / 100.0f;
    drawLed(CGPointMake(NSMidX(kRightWin), 258), color > 0 ? 0.25f + 0.45f * color - pc.get(kGR) / 15.0f : 0.0f, 0xFF9A2E, nil, 5);
    textFont(@"TUBE", @"AvenirNext-DemiBold", CGPointMake(NSMidX(kRightWin), 276), 7.5, rgb(INK_DIM), 2);
}

std::unique_ptr<PanelController> buildVariekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xDADDE8; t.accent = 0xFFD27A; t.led = 0xFF5A2A;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false;
    t.pointer = 0xF4F1EA;
    t.buttonTop = 0x2E2F34; t.buttonBot = 0x0E0F11; t.buttonText = 0xE8E4DA;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined;
    theme() = t;
    auto pc = makePanel(au, "varieki");
    pc->watched = {{kVU, -20}, {kGR, 0}, {kAutoMakeup, 0}, {kGRNeedle, 0}, {kOutVU, -20}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };
    auto ms = ^NSString*(float v) { return v < 10 ? [NSString stringWithFormat:@"%.1f ms", v] : [NSString stringWithFormat:@"%.0f ms", v]; };

    // középső fekete panel: 2 × 3 gomb (felül RECOVERY · OUTPUT · RATIO, alul THRESHOLD · ATTACK · ANALOG)
    const CGFloat c1 = 351, c2 = 480, c3 = 609, r1 = 132, r2 = 248, R = 26;
    auto knob = [&](AudioUnitParameterID id, CGPoint c, NSString* name, float lo, float hi, float def, NSString* (^fmt)(float), bool log, int dot) {
        pc->controls.push_back({Control::Knob, id, c, R, name, nil, lo, hi, def, fmt});
        pc->controls.back().labelSize = 10; pc->controls.back().logTaper = log; pc->controls.back().cap = dot;
    };
    knob(kRelRms, {c1, r1}, @"RECOVERY", 20, 2000, 600, ms, true, -1);
    knob(kMakeup, {c2, r1}, @"OUTPUT", -12, 24, 0, dB, false, -1);
    knob(kRatio, {c3, r1}, @"RATIO", 1, 20, 2.5f, ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f : 1", v]; }, true, -1);
    knob(kThreshold, {c1, r2}, @"THRESHOLD", -40, 0, -24, ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f dBFS", v]; }, false, -1);
    knob(kAttack, {c2, r2}, @"ATTACK", 0.1f, 100, 20, ms, true, -1);
    knob(kAnalog, {c3, r2}, @"ANALOG", 0, 100, 85, pct, false, -1);

    // bal ablak alatt: AUTO GAIN
    pc->controls.push_back({Control::LedButton, kAutoGain, {NSMidX(kLeftWin), 258}, 0.85, @"AUTO GAIN", nil, 0, 1, 1});

    // EKI fiók: a precíziós motor finomhangolása
    const CGFloat dy = H + 60, x0 = 250, dx = 110;
    struct D { AudioUnitParameterID id; NSString* name; float lo, hi, def; bool log; NSString* (^fmt)(float); };
    std::vector<D> ds = {
        {kCrest, @"PEAK CREST", 0, 20, 6, false, dB}, {kKnee, @"SOFT KNEE", 0, 24, 12, false, dB},
        {kRelPeak, @"REL PEAK", 5, 500, 80, true, ms}, {kStereo, @"STEREO SENS", 0, 100, 0, false, pct},
    };
    for (size_t i = 0; i < ds.size(); ++i) {
        pc->controls.push_back({Control::Knob, ds[i].id, {x0 + dx * i, dy}, 19, ds[i].name, nil, ds[i].lo, ds[i].hi, ds[i].def, ds[i].fmt});
        pc->controls.back().drawer = true; pc->controls.back().labelSize = 8.5; pc->controls.back().logTaper = ds[i].log;
    }
    pc->controls.push_back({Control::SteppedKnob, kScCut, {x0 + dx * 4, dy}, 17, @"SC LOW CUT", @[@"OFF", @"40", @"80", @"120", @"200"], 0, 4, 1});
    pc->controls.back().drawer = true; pc->controls.back().spread = 110; pc->controls.back().labelSize = 8.5;
    pc->controls.push_back({Control::Knob, kMix, {x0 + dx * 5, dy}, 19, @"MIX", nil, 0, 100, 100, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 8.5;
    pc->lock(kMakeup, kAutoGain);                          // AUTO GAIN alatt a gain 0-n, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildVariekiPanel)
