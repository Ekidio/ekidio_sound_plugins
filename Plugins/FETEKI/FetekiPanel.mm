// FETEKI saját panel — EKIDIO SOUND rack-stílus, fekete előlap (a klasszikus FET-limiterek hangulatában):
//   bal oldalt INPUT és OUTPUT, középen ATTACK/RELEASE és függőleges RATIO-gombsor, jobbra a pajzs-VU
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kInput, kOutput, kAttack, kRelease, kRatio, kAutoGain, kAnalog, kMix, kMeterMode,
                              kVU, kGR, kAutoMakeup };

constexpr int INK = 0xE8E4DA, INK_DIM = 0x9C988E;
const Look LOOK = {
    0x232428, 0x18191C, 0x0F1012, 1.0,
    {0xC9CCD1, 0x6A6D72, 0x2E3034, 0x4A4D52, 0x9A9DA2},
    0x2C2E32, 0x26282C, 0x1F2124, 1.2,
    INK, INK_DIM, 0.10, 0.35,
};

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, @"FET PEAK LIMITER", {{430, @"FET & class-A output color"}, {630, @"parallel blend with the dry signal"}});
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    drawVU(NSMakeRect(642, 54, 264, 172), pc.get(kVU), dialStyle(@"FETEKI"));
    drawLed(CGPointMake(575, 290), std::lround(pc.get(kRatio)) == 4 ? 1.0f : 0.0f, 0xFF3A22, nil, 4.5);   // ALL jelző
}

std::unique_ptr<PanelController> buildFetekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xCFCBC2; t.accent = 0xFFB547; t.led = 0xFF5A2A;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = true;
    t.pointer = 0xF4F1EA;
    t.buttonTop = 0x2E2F34; t.buttonBot = 0x0E0F11; t.buttonText = 0xE8E4DA;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined;
    theme() = t;
    auto pc = makePanel(au, "feteki");
    pc->watched = {{kVU, -20}, {kGR, 0}, {kAutoMakeup, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    pc->controls.push_back({Control::LedButton, kAutoGain, {88, 196}, 1.2, @"AUTO GAIN", nil, 0, 1, 0});
    pc->controls.push_back({Control::Knob, kInput, {215, 190}, 50, @"INPUT", ticks(-10, 5, 11), -10, 40, 0, dB});
    pc->controls.back().labelSize = 12; pc->controls.back().cap = 0xFFB23F;
    pc->controls.push_back({Control::Knob, kOutput, {385, 190}, 50, @"OUTPUT", ticks(-20, 4, 11), -20, 20, 0, dB});
    pc->controls.back().labelSize = 12; pc->controls.back().cap = 0xFFB23F;
    pc->controls.push_back({Control::Knob, kAttack, {500, 128}, 19, @"ATTACK", ticks(1, 1, 7), 1, 7, 3,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f µs", 800.0 * std::pow(20.0 / 800.0, (v - 1) / 6.0)]; }});
    pc->controls.back().labelSize = 8.5;
    pc->controls.push_back({Control::Knob, kRelease, {500, 246}, 19, @"RELEASE", ticks(1, 1, 7), 1, 7, 5,
        ^NSString*(float v) { double s = 1.1 * std::pow(0.05 / 1.1, (v - 1) / 6.0); return s < 1 ? [NSString stringWithFormat:@"%.0f ms", s * 1000] : [NSString stringWithFormat:@"%.2f s", s]; }});
    pc->controls.back().labelSize = 8.5;
    pc->controls.push_back({Control::RadioRow, kRatio, {575, 168}, 1.05, @"RATIO", @[@"4", @"8", @"12", @"20", @"ALL"], 0, 4, 0});
    pc->controls.back().vertical = true;
    pc->controls.push_back({Control::SteppedKnob, kMeterMode, {774, 282}, 14, @"METER", @[@"GR", @"+4", @"+8"], 0, 2, 0});
    pc->controls.back().spread = 50; pc->controls.back().labelDy = 27; pc->controls.back().labelSize = 8.5;

    // EKI fiók
    const CGFloat dy = H + 58;
    pc->controls.push_back({Control::Knob, kAnalog, {430, dy}, 24, @"ANALOG", nil, 0, 100, 50, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::Knob, kMix, {630, dy}, 24, @"MIX", nil, 0, 100, 100, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    pc->lock(kOutput, kAutoGain);                          // AUTO GAIN alatt a gain 0-n, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildFetekiPanel)
