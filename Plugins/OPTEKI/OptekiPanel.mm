// OPTEKI saját panel — az EKIDIO SOUND rack-stílus referenciája
//   fehéres-ezüst szálcsiszolt előlap, pajzs-VU a modellnévvel, két nagy esztergált gomb izzó ponttal,
//   AUTO GAIN világító gyűrűvel, MODE kapcsoló, CELL LED; fiókban HF EMPHASIS, ANALOG, MIX
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kPeakReduction, kGain, kMode, kHFEmphasis, kAnalog, kMix, kMeterMode, kVU, kGR, kAutoGain, kAutoMakeup };

// színek: fehéres-ezüst előlap, sötét gravírozás
constexpr int INK = 0x26272A, INK_DIM = 0x5A5C61, VALUE = 0xD2651A;
const Look LOOK = {
    0xE9EBED, 0xD6D9DC, 0xC2C5C8, 0.75,
    {0xE6E8EB, 0x8E9196, 0x5A5D62, 0x7C7F84, 0xC4C7CB},
    0xB4B7BA, 0xBFC2C5, 0xAEB1B4, 0.9,
    INK, INK_DIM, 0.55, 0.10,
};

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, @"OPTICAL LEVELING AMPLIFIER", {
        {330, @"tames sibilance in the detector"}, {530, @"tube & transformer color"}, {730, @"parallel blend with the dry signal"}});
    textFont(@"MODE", @"AvenirNext-DemiBold", CGPointMake(878, 104), 8.5, rgb(INK), 2.5);
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    drawVU(NSMakeRect(428, 54, 264, 172), pc.get(kVU), dialStyle(@"OPTEKI"));
    drawLed(CGPointMake(878, 258), -pc.get(kGR) / 8.0f, 0xFF3A22, @"CELL", 5.5);   // a csökkentéssel arányos fényerő
}

std::unique_ptr<PanelController> buildOptekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = INK; t.accent = VALUE; t.led = 0xFF5A2A;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false;   // világos lemezen éles, nem izzó értékek
    t.pointer = 0xF4F1EA;
    t.buttonTop = 0x3A3B40; t.buttonBot = 0x111214; t.buttonText = 0xEDEDED;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined;
    theme() = t;
    auto pc = makePanel(au, "opteki");
    pc->watched = {{kVU, -20}, {kGR, 0}, {kAutoMakeup, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    // előlap: a lényeg
    pc->controls.push_back({Control::Knob, kGain, {345, 196}, 54, @"GAIN", ticks(-20, 5, 11), -20, 30, 4,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; }});
    pc->controls.back().labelSize = 12; pc->controls.back().cap = 0xFFB23F;   // borostyán jelzőpont
    pc->controls.push_back({Control::Knob, kPeakReduction, {775, 196}, 54, @"PEAK REDUCTION", ticks(0, 10, 11), 0, 100, 60,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f", v]; }});
    pc->controls.back().labelSize = 12; pc->controls.back().cap = 0xFF3B2A;   // piros jelzőpont
    pc->controls.push_back({Control::LedButton, kAutoGain, {150, 188}, 1.3, @"AUTO GAIN", nil, 0, 1, 0});
    pc->controls.push_back({Control::SteppedKnob, kMeterMode, {560, 282}, 14, @"METER", @[@"GR", @"+4", @"+10"], 0, 2, 0});
    pc->controls.back().spread = 50; pc->controls.back().labelDy = 27; pc->controls.back().labelSize = 8.5;
    pc->controls.push_back({Control::Toggle, kMode, {878, 168}, 1.15, nil, @[@"COMPRESS", @"LIMIT"], 0, 1, 0});

    // EKI fiók
    const CGFloat dy = H + 58;
    pc->controls.push_back({Control::Knob, kHFEmphasis, {330, dy}, 24, @"HF EMPHASIS", nil, 0, 100, 0, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::Knob, kAnalog, {530, dy}, 24, @"ANALOG", nil, 0, 100, 50, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::Knob, kMix, {730, dy}, 24, @"MIX", nil, 0, 100, 100, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildOptekiPanel)
