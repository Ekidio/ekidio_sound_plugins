// TUBEKI saját panel — EKIDIO SOUND rack-stílus a klasszikus opto-csöves (CL 1B-típusú) kompresszorok hangulatában:
//   királykék szálcsiszolt előlap, fekete esztergált gombok fehér mutatóval, két sorban (GAIN · RATIO · THRESHOLD,
//   ATTACK · RELEASE · ATTACK/RELEASE SELECT), METER kapcsoló, jobbra fent a pajzs-VU, alatta a cső-lámpa és a PARALLEL (MIX)
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kThreshold, kRatio, kAttack, kRelease, kMode, kGain, kAutoGain, kAnalog, kMix, kMeterMode, kVU, kGR, kAutoMakeup };

constexpr int INK = 0xF2F4F8, INK_DIM = 0xB9C6DE;
const Look LOOK = {
    0x2459A8, 0x1D4C93, 0x173E7A, 0.65,
    {0xE6E8EB, 0x8E9196, 0x4E5156, 0x74777C, 0xC4C7CB},
    0x1B4686, 0x1F4C90, 0x183D74, 0.8,
    INK, INK_DIM, 0.22, 0.26,
};

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, @"OPTO TUBE COMPRESSOR", {{430, @"tube & transformer color"}});
    textFont(@"sidechain: optical cell", @"AvenirNext-Regular", CGPointMake(150, 90), 7.5, rgb(INK_DIM), 0.8);
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    drawVU(NSMakeRect(662, 54, 254, 164), pc.get(kVU), dialStyle(@"TUBEKI"));
    // cső-lámpa: a fűtés mindig izzik, a kompresszióval erősödik
    drawLed(CGPointMake(700, 262), 0.55f - pc.get(kGR) / 12.0f, 0xFF3A22, @"TUBE", 6);
}

std::unique_ptr<PanelController> buildTubekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xE6EAF2; t.accent = 0xFFD27A; t.led = 0xFF5A2A;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false;
    t.pointer = 0xF6F6F6;                                  // fehér mutatóvonal a fekete gombokon
    t.buttonTop = 0x2E2F34; t.buttonBot = 0x0E0F11; t.buttonText = 0xE8EAEE;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined;
    theme() = t;
    auto pc = makePanel(au, "tubeki");
    pc->watched = {{kVU, -20}, {kGR, 0}, {kAutoMakeup, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    // felső sor: GAIN · RATIO · THRESHOLD · METER
    pc->controls.push_back({Control::Knob, kGain, {258, 128}, 30, @"GAIN", ticks(-10, 10, 5), -10, 30, 7, dB});
    pc->controls.back().labelSize = 10;
    pc->controls.push_back({Control::Knob, kRatio, {386, 128}, 28, @"RATIO", ticks(2, 2, 5), 2, 10, 3,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f : 1", v]; }});
    pc->controls.back().labelSize = 10;
    pc->controls.push_back({Control::Knob, kThreshold, {514, 128}, 30, @"THRESHOLD", ticks(-40, 10, 5), -40, 0, -20,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f dB", v]; }});
    pc->controls.back().labelSize = 10;
    pc->controls.push_back({Control::SteppedKnob, kMeterMode, {614, 132}, 14, @"METER", @[@"COMP", @"+4", @"+10"], 0, 2, 0});
    pc->controls.back().spread = 55; pc->controls.back().labelSize = 8.5;

    // alsó sor: ATTACK · RELEASE · ATTACK/RELEASE SELECT
    pc->controls.push_back({Control::Knob, kAttack, {258, 250}, 26, @"ATTACK", nil, 0.5f, 300, 30,
        ^NSString*(float v) { return v < 10 ? [NSString stringWithFormat:@"%.1f ms", v] : [NSString stringWithFormat:@"%.0f ms", v]; }});
    pc->controls.back().logTaper = true; pc->controls.back().labelSize = 10;
    pc->controls.push_back({Control::Knob, kRelease, {386, 250}, 26, @"RELEASE", nil, 0.05f, 10, 0.8f,
        ^NSString*(float v) { return v < 1 ? [NSString stringWithFormat:@"%.0f ms", v * 1000] : [NSString stringWithFormat:@"%.1f s", v]; }});
    pc->controls.back().logTaper = true; pc->controls.back().labelSize = 10;
    pc->controls.push_back({Control::SteppedKnob, kMode, {514, 252}, 17, @"ATTACK/RELEASE", @[@"FIX", @"MAN", @"F+M"], 0, 2, 0});
    pc->controls.back().spread = 55; pc->controls.back().labelSize = 8.5;

    // bal oldalt: AUTO GAIN; jobbra lent: PARALLEL (MIX)
    pc->controls.push_back({Control::LedButton, kAutoGain, {130, 250}, 1.0, @"AUTO GAIN", nil, 0, 1, 0});
    pc->controls.push_back({Control::Knob, kMix, {836, 256}, 20, @"PARALLEL", nil, 0, 100, 100, pct});
    pc->controls.back().labelSize = 9;

    // EKI fiók
    pc->controls.push_back({Control::Knob, kAnalog, {430, H + 58}, 24, @"ANALOG", nil, 0, 100, 50, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildTubekiPanel)
