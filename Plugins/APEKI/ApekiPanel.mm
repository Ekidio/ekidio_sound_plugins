// APEKI saját panel — EKIDIO SOUND rack-stílus a klasszikus punchos busz-kompresszorok hangulatában:
//   sötétszürke fő panel COMPRESSOR és TONE keretekkel, jobb oldalt kék panel a nagy piros OUTPUT gombbal
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kThreshold, kRatio, kAttack, kRelease, kKnee, kThrust, kType, kOutput, kAutoGain, kLink, kAnalog, kMix,
                              kMeterMode, kVU, kGR, kAutoMakeup };

constexpr int INK = 0xEDEFF2, INK_DIM = 0x9EA3A9, BLUE_INK = 0x6FB6FF;
constexpr CGFloat BLUE_X = 712;   // a kék panel bal széle
const Look LOOK = {
    0x35373B, 0x2B2D30, 0x232528, 0.9,
    {0xC9CCD1, 0x6A6D72, 0x2E3034, 0x4A4D52, 0x9A9DA2},
    0x2C2E32, 0x26282C, 0x1F2124, 1.2,
    INK, INK_DIM, 0.10, 0.32,
};

/// gravírozott, lekerekített keret címmel (mint a csoportosító dobozok az eredeti előlapon)
void groupBox(NSRect r, NSString* title) {
    NSBezierPath* b = [NSBezierPath bezierPathWithRoundedRect:r xRadius:10 yRadius:10];
    b.lineWidth = 1.2; [rgb(0x000000, 0.45) setStroke]; [b stroke];
    NSBezierPath* hi = [NSBezierPath bezierPathWithRoundedRect:NSOffsetRect(r, 0, 1) xRadius:10 yRadius:10];
    hi.lineWidth = 0.6; [rgb(0xFFFFFF, 0.10) setStroke]; [hi stroke];
    textFont(title, @"AvenirNext-DemiBold", CGPointMake(NSMidX(r), NSMinY(r) + 12), 9.5, rgb(INK), 3);
}

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{430, @"stereo link: 0 % independent … 100 % linked"}, {630, @"parallel blend with the dry signal"}});

    // ---- kék oldalpanel (a lemez lekerekített formájába vágva) ----
    NSRect plateR = NSInsetRect(NSMakeRect(8, HEAD + 8, W - 16, H - HEAD - 16), 5, 5);
    NSBezierPath* plate = [NSBezierPath bezierPathWithRoundedRect:plateR xRadius:15 yRadius:15];
    NSRect blueR = NSMakeRect(BLUE_X, NSMinY(plateR), NSMaxX(plateR) - BLUE_X, plateR.size.height);
    CGContextSaveGState(cg());
    [plate addClip];
    brushedPlate([NSBezierPath bezierPathWithRect:blueR], blueR, 0x2A62B8, 0x1F4E97, 0x173C78, 0x7F4A7C15u, 0.8);
    [[[NSGradient alloc] initWithStartingColor:rgb(0x000000, 0.55) endingColor:rgb(0x000000, 0.0)]
        drawInRect:NSMakeRect(BLUE_X, NSMinY(plateR), 10, plateR.size.height) angle:0];
    line(CGPointMake(BLUE_X - 0.5, NSMinY(plateR)), CGPointMake(BLUE_X - 0.5, NSMaxY(plateR)), 1, rgb(0x000000, 0.8));
    CGContextRestoreGState(cg());
    textFont(@"PUNCH BUS COMPRESSOR", @"AvenirNext-DemiBold", CGPointMake(830, 70), 7.5, rgb(0xDCE8FF), 2);

    // ---- csoportosító keretek ----
    groupBox(NSMakeRect(312, 52, 388, 162), @"COMPRESSOR");
    groupBox(NSMakeRect(312, 222, 388, 90), @"TONE");
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    drawVU(NSMakeRect(30, 54, 264, 172), pc.get(kVU), dialStyle(@"APEKI"));
    drawLed(CGPointMake(420, 86), -pc.get(kGR) / 8.0f, 0xFF3A22, nil, 4.2);   // a küszöb-LED: csökkentéskor izzik
}

std::unique_ptr<PanelController> buildApekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xD8DCE0; t.accent = BLUE_INK; t.led = 0xFF5A2A;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false;
    t.pointer = 0xF4F1EA;
    t.buttonTop = 0x3A3C41; t.buttonBot = 0x141518; t.buttonText = 0xE8EAEE;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined;
    theme() = t;
    auto pc = makePanel(au, "apeki");
    pc->watched = {{kVU, -20}, {kGR, 0}, {kAutoMakeup, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    // COMPRESSOR
    pc->controls.push_back({Control::Knob, kThreshold, {372, 134}, 36, @"THRESHOLD", ticks(-40, 8, 6), -40, 0, -22,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f dB", v]; }});
    pc->controls.back().labelSize = 10; pc->controls.back().cap = 0xFF3B2A;
    pc->controls.push_back({Control::SteppedKnob, kAttack, {470, 130}, 19, @"ATTACK ms", @[@".03", @".1", @".3", @"1", @"3", @"10", @"30"], 0, 6, 5});
    pc->controls.back().spread = 125; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::SteppedKnob, kRatio, {558, 130}, 19, @"RATIO", @[@"1.5", @"2", @"3", @"4", @"6", @"10", @"∞"], 0, 6, 3});
    pc->controls.back().spread = 125; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::SteppedKnob, kRelease, {646, 130}, 19, @"RELEASE s", @[@".05", @".1", @".2", @".5", @"1", @"2"], 0, 5, 2});
    pc->controls.back().spread = 120; pc->controls.back().labelSize = 9;

    // TONE: a karakter 1-1 kattintással
    pc->controls.push_back({Control::RadioRow, kKnee, {392, 268}, 0.95, @"KNEE", @[@"HARD", @"MED", @"SOFT"], 0, 2, 1});
    pc->controls.push_back({Control::RadioRow, kThrust, {528, 268}, 0.95, @"THRUST", @[@"NORM", @"MED", @"LOUD"], 0, 2, 1});
    pc->controls.push_back({Control::RadioRow, kType, {648, 268}, 0.95, @"TYPE", @[@"NEW", @"OLD"], 0, 1, 0});

    // VU alatt
    pc->controls.push_back({Control::SteppedKnob, kMeterMode, {162, 282}, 14, @"METER", @[@"GR", @"+4", @"+10"], 0, 2, 0});
    pc->controls.back().spread = 50; pc->controls.back().labelDy = 27; pc->controls.back().labelSize = 8.5;

    // kék panel: a nagy piros OUTPUT
    pc->controls.push_back({Control::Knob, kOutput, {830, 168}, 46, @"OUTPUT", ticks(-24, 8, 7), -24, 24, 5,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; }});
    pc->controls.back().labelSize = 12; pc->controls.back().tint = 0xC8281E;
    pc->controls.push_back({Control::LedButton, kAutoGain, {778, 268}, 0.85, @"AUTO GAIN", nil, 0, 1, 0});
    pc->controls.push_back({Control::Knob, kAnalog, {886, 264}, 15, @"ANALOG", nil, 0, 100, 55, pct});
    pc->controls.back().labelSize = 8.5;

    // EKI fiók
    const CGFloat dy = H + 58;
    pc->controls.push_back({Control::Knob, kLink, {430, dy}, 24, @"L/R LINK", nil, 0, 100, 100, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::Knob, kMix, {630, dy}, 24, @"MIX", nil, 0, 100, 100, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    pc->lock(kOutput, kAutoGain);                          // AUTO GAIN alatt a gain 0-n, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildApekiPanel)
