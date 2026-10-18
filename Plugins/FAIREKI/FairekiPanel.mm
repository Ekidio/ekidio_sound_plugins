// FAIREKI saját panel — EKIDIO SOUND rack-stílus a klasszikus kétcsatornás 670-es vari-mu gépek hangulatában:
//   fekete szálcsiszolt előlap, bal oldalt egy nagy pajzs-VU, piros kézírásos modell-logó, "csirkefej" kapcsolók
//   (METER, AGC, TIME CONSTANT, AGC FILTER), esztergált gombok piros peremgyűrűvel, piros LAT/VERT akcentusok
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kInput, kThreshold, kTimeConstant, kAgc, kAgcFilter, kOutput, kAutoGain, kAnalog, kMix, kMeterMode,
                              kVU, kGR, kAutoMakeup, kNeedleL, kNeedleR };

constexpr int INK = 0xEDEDED, INK_DIM = 0x9C9C9C, RED = 0xE8392C;
const Look LOOK = {
    0x202123, 0x18191B, 0x111214, 0.85,
    {0xC9CCD1, 0x6A6D72, 0x2E3034, 0x4A4D52, 0x9A9DA2},
    0x26282B, 0x222427, 0x1B1D20, 1.1,
    INK, INK_DIM, 0.08, 0.32,
};

/// fehér/piros kétszínű gravírozott felirat (pl. "LEFT-" fehér + "LAT" piros)
void twoTone(NSString* white, NSString* red, CGPoint c, CGFloat size) {
    NSFont* f = [NSFont fontWithName:@"AvenirNext-DemiBold" size:size];
    NSDictionary* aw = @{NSFontAttributeName: f, NSForegroundColorAttributeName: rgb(INK), NSKernAttributeName: @1.2};
    NSDictionary* ar = @{NSFontAttributeName: f, NSForegroundColorAttributeName: rgb(RED), NSKernAttributeName: @1.2};
    const CGFloat ww = [white sizeWithAttributes:aw].width, rw = [red sizeWithAttributes:ar].width;
    const CGFloat h = [white sizeWithAttributes:aw].height;
    [white drawAtPoint:NSMakePoint(c.x - (ww + rw) / 2, c.y - h / 2) withAttributes:aw];
    [red drawAtPoint:NSMakePoint(c.x - (ww + rw) / 2 + ww, c.y - h / 2) withAttributes:ar];
}

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{480, @"tube & transformer color"}});
    // piros kézírásos modell-logó + "MODEL 670" sor (a klasszikus előlapok piros logója)
    textFont(@"Faireki", @"SnellRoundhand-Black", CGPointMake(545, 68), 25, rgb(RED), 0.5);
    textFont(@"VARIABLE-MU LIMITER", @"AvenirNext-DemiBold", CGPointMake(410, 70), 7, rgb(INK_DIM), 1.8);
    textFont(@"MODEL 670", @"AvenirNext-DemiBold", CGPointMake(675, 70), 8.5, rgb(INK), 2);
    // kétszínű címek a kapcsolók fölött
    twoTone(@"LEFT-", @"LAT", CGPointMake(345, 205), 7.5);
    twoTone(@"RIGHT-", @"VERT", CGPointMake(345, 216), 7.5);
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    const bool gr = std::lround(pc.get(kMeterMode)) == 0;
    drawVU(NSMakeRect(34, 64, 258, 176), pc.get(kVU), dialStyle(@"FAIREKI", gr));
    drawLed(CGPointMake(236, 280), 0.85f, 0xFF3A22, @"POWER", 6);
}

std::unique_ptr<PanelController> buildFairekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xE2E2E2; t.accent = 0xFF6A5A; t.led = 0xFF5A2A;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false;
    t.pointer = 0xF6F6F6;
    t.buttonTop = 0x2E2F34; t.buttonBot = 0x0E0F11; t.buttonText = 0xE8EAEE;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined; t.coloredLine = true;              // esztergált gombok piros peremgyűrűvel és mutatóval
    theme() = t;
    auto pc = makePanel(au, "faireki");
    pc->watched = {{kVU, -20}, {kGR, 0}, {kAutoMakeup, 0}, {kNeedleL, 0}, {kNeedleR, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    // "csirkefej" kapcsolók: METER, AGC
    pc->controls.push_back({Control::Selector, kMeterMode, {345, 140}, 24, @"METERING", @[@"GR", @"OUT"], 0, 1, 0});
    pc->controls.back().spread = 40; pc->controls.back().labelDy = 34;
    pc->controls.push_back({Control::Selector, kAgc, {345, 262}, 24, @"AGC", @[@"L/R", @"LAT/VERT"], 0, 1, 0});
    pc->controls.back().spread = 45; pc->controls.back().labelDy = 34;

    // a nagy gombok (piros peremgyűrű)
    pc->controls.push_back({Control::Knob, kInput, {480, 152}, 40, @"INPUT GAIN", ticks(-10, 5, 7), -10, 20, 0, dB});
    pc->controls.back().labelSize = 10; pc->controls.back().cap = RED;
    pc->controls.push_back({Control::Knob, kThreshold, {632, 152}, 40, @"THRESHOLD", ticks(-40, 8, 6), -40, 0, -20,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f dB", v]; }});
    pc->controls.back().labelSize = 10; pc->controls.back().cap = RED;
    pc->controls.push_back({Control::Knob, kOutput, {480, 264}, 22, @"OUTPUT", nil, -20, 20, 0, dB});
    pc->controls.back().labelSize = 9.5; pc->controls.back().cap = RED;
    pc->controls.push_back({Control::Knob, kMix, {632, 264}, 22, @"DRY/WET", nil, 0, 100, 100, pct});
    pc->controls.back().labelSize = 9.5; pc->controls.back().cap = RED;

    // "csirkefej" kapcsolók: TIME CONSTANT, AGC FILTER
    pc->controls.push_back({Control::Selector, kTimeConstant, {786, 140}, 26, @"TIME CONSTANT", @[@"1", @"2", @"3", @"4", @"5", @"6"], 0, 5, 1});
    pc->controls.back().spread = 75; pc->controls.back().labelDy = 36;
    pc->controls.push_back({Control::Selector, kAgcFilter, {786, 262}, 24, @"AGC FILTER", @[@"OFF", @"90", @"180", @"MID"], 0, 3, 0});
    pc->controls.back().spread = 70; pc->controls.back().labelDy = 34;

    // AUTO GAIN a VU alatt
    pc->controls.push_back({Control::LedButton, kAutoGain, {100, 276}, 0.85, @"AUTO GAIN", nil, 0, 1, 0});

    // EKI fiók
    pc->controls.push_back({Control::Knob, kAnalog, {480, H + 58}, 24, @"ANALOG", nil, 0, 100, 60, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9; pc->controls.back().cap = RED;
    pc->lock(kOutput, kAutoGain);                          // AUTO GAIN alatt a gain 0-n, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildFairekiPanel)
