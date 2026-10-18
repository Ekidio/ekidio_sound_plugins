// BUSEKI saját panel — EKIDIO SOUND rack-stílus, kékesszürke konzolmodul (a nagy konzolok busz-kompresszorainak hangulatában):
//   elöl THRESHOLD, MAKEUP, lépcsős RATIO/ATTACK/RELEASE, AUTO GAIN, METER; GR-skálás pajzs-műszer
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kThreshold, kMakeup, kRatio, kAttack, kRelease, kScHpf, kAutoGain, kAnalog, kMix, kMeterMode,
                              kVU, kGR, kAutoMakeup };

// grafitszürke konzolmodul, fehér feliratok (a nagy konzolok busz-kompresszorainak hangulatában)
constexpr int INK = 0xF4F5F6, INK_DIM = 0xA8ADB3;
constexpr int BLUE = 0x3B8BE6, RED = 0xE0443A;
const Look LOOK = {
    0x4A4D52, 0x3C3F44, 0x313438, 0.8,
    {0xD8DADD, 0x7A7D82, 0x3A3C40, 0x5E6166, 0xAEB1B5},
    0x34373B, 0x3A3D42, 0x2E3135, 1.0,
    INK, INK_DIM, 0.14, 0.30,
};

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, @"STEREO BUS COMPRESSOR", {
        {330, @"keeps the kick from pumping"}, {530, @"console color"}, {730, @"parallel blend with the dry signal"}});
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    const bool gr = std::lround(pc.get(kMeterMode)) == 0;   // GR módban gain-reduction skála
    drawVU(NSMakeRect(428, 54, 264, 172), pc.get(kVU), dialStyle(@"BUSEKI", gr, 1));   // fehér számlap
}

std::unique_ptr<PanelController> buildBusekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xE6E9EC; t.accent = 0x8CC4FF; t.led = 0xFF4636;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false;
    t.pointer = 0xF4F1EA;
    t.buttonTop = 0x2E2F34; t.buttonBot = 0x0E0F11; t.buttonText = 0xE8E4DA;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined; t.coloredLine = true;   // esztergált gombok piros/kék konzolos jelöléssel
    theme() = t;
    auto pc = makePanel(au, "buseki");
    pc->watched = {{kVU, -20}, {kGR, 0}, {kAutoMakeup, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    // előlap
    pc->controls.push_back({Control::SteppedKnob, kRatio, {110, 148}, 22, @"RATIO", @[@"2", @"4", @"10"], 0, 2, 1});
    pc->controls.back().cap = BLUE;
    pc->controls.back().spread = 60; pc->controls.back().labelSize = 10;
    pc->controls.push_back({Control::LedButton, kAutoGain, {110, 262}, 1.1, @"AUTO GAIN", nil, 0, 1, 0});
    pc->controls.push_back({Control::Knob, kThreshold, {345, 196}, 54, @"THRESHOLD", ticks(-40, 4, 11), -40, 0, -18,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f dB", v]; }});
    pc->controls.back().labelSize = 12; pc->controls.back().cap = RED;
    pc->controls.push_back({Control::Knob, kMakeup, {775, 196}, 54, @"MAKEUP", ticks(-10, 5, 7), -10, 20, 0,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; }});
    pc->controls.back().labelSize = 12; pc->controls.back().cap = BLUE;
    pc->controls.push_back({Control::SteppedKnob, kAttack, {892, 128}, 19, @"ATTACK ms", @[@".1", @".3", @"1", @"3", @"10", @"30"], 0, 5, 5});
    pc->controls.back().cap = BLUE;
    pc->controls.back().spread = 120; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::SteppedKnob, kRelease, {892, 256}, 19, @"RELEASE s", @[@".1", @".3", @".6", @"1.2", @"A"], 0, 4, 4});
    pc->controls.back().cap = BLUE;
    pc->controls.back().spread = 110; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::SteppedKnob, kMeterMode, {560, 282}, 14, @"METER", @[@"GR", @"+4", @"+10"], 0, 2, 0});
    pc->controls.back().cap = BLUE;
    pc->controls.back().spread = 50; pc->controls.back().labelDy = 27; pc->controls.back().labelSize = 8.5;

    // EKI fiók
    const CGFloat dy = H + 58;
    pc->controls.push_back({Control::SteppedKnob, kScHpf, {330, dy}, 22, @"SC HPF Hz", @[@"OFF", @"60", @"90", @"150", @"250"], 0, 4, 0});
    pc->controls.back().cap = BLUE; pc->controls.back().drawer = true; pc->controls.back().spread = 110; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::Knob, kAnalog, {530, dy}, 24, @"ANALOG", nil, 0, 100, 30, pct});
    pc->controls.back().cap = BLUE;
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    pc->controls.push_back({Control::Knob, kMix, {730, dy}, 24, @"MIX", nil, 0, 100, 100, pct});
    pc->controls.back().cap = BLUE;
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9;
    pc->lock(kMakeup, kAutoGain);                          // AUTO GAIN alatt a gain 0-n, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildBusekiPanel)
