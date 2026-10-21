// MASTEQUI saját panel — EKI-stílus (a user döntése, 2026-10-22: a kipróbált hardver-előlap helyett az EKI marad, de a kékeszöld
//   lemezszínnel, festékszemcse nélkül, és a GAIN-gombok piros karimát kapnak):
//   balról: LOW SHELF (+ LOW SHIFT 41 / 83 Hz) · LOW · MID · HIGH (FREQ / GAIN / Q) · HIGH SHELF · név, OUTPUT, AUTO GAIN
//   fiók: RANGE (6 / 9 / 12 dB). A gain-gombok kijelzése a RANGE szerinti valódi dB.
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kLowShelf, kLowGain, kLowFreq, kLowQ, kMidGain, kMidFreq, kMidQ, kHighGain, kHighFreq, kHighQ, kHighShelf,
                              kLowShift, kRange, kOutput, kAutoGain, kAutoMakeup };

constexpr int INK = 0xEEF3F6, INK_DIM = 0xB3C7D1, GOLD = 0xE9C46A, RED = 0xC4222C;
const Look LOOK = {
    0x3F6A7E, 0x335A6D, 0x284A5B, 0.9,   // mély kékeszöld, szálcsiszolva
    {0xE6E8EB, 0x8E9196, 0x4C5055, 0x72767B, 0xC2C5C9},
    0x2E3034, 0x34373B, 0x26282C, 1.0,
    INK, 0xB9BDC4, 0.14, 0.34,
};
constexpr CGFloat LS_X = 88, LOW_X = 236, MID_X = 386, HIGH_X = 536, HS_X = 686, NAME_X = 852;

const float kLowHz[24]  = {11, 13, 15, 18, 20, 25, 30, 36, 42, 50, 60, 71, 85, 100, 120, 140, 170, 200, 240, 280, 340, 400, 450, 570};
const float kMidHz[24]  = {125, 150, 180, 210, 250, 300, 350, 420, 500, 600, 700, 840, 1000, 1200, 1400, 1700, 2000, 2400, 2800, 3400, 4000, 4500, 5600, 6200};
const float kHighHz[24] = {3400, 3700, 4000, 4400, 4800, 5200, 5700, 6200, 6800, 7400, 8100, 8800, 9500, 10000, 11000, 12000, 13000, 14000, 16000, 17000, 19000, 21000, 23000, 25000};

NSString* hzText(float hz) { return hz >= 1000 ? [NSString stringWithFormat:@"%g kHz", hz / 1000] : [NSString stringWithFormat:@"%g Hz", hz]; }
/// 24 állás, csak néhány skálafelirattal (a többi üres)
NSArray<NSString*>* sparse(const float* hz, std::initializer_list<int> show) {
    NSMutableArray* a = [NSMutableArray array];
    for (int i = 0; i < 24; ++i) {
        NSString* s = @"";
        for (int k : show) if (k == i) s = hz[i] >= 1000 ? [NSString stringWithFormat:@"%gk", hz[i] / 1000] : [NSString stringWithFormat:@"%g", hz[i]];
        [a addObject:s];
    }
    return a;
}

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
    drawChrome(pc, LOOK, nil, {{330, @"gain knob range: ±6 / ±9 / ±12 dB"}});
    textFont(@"MASTEQUI", @"Futura-Bold", CGPointMake(NAME_X, 84), 22, rgb(INK), 3);
    textFont(@"MASTERING", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 106), 6.5, rgb(INK_DIM), 1.6);
    textFont(@"EQUALIZER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 116), 6.5, rgb(INK_DIM), 1.6);
    line(CGPointMake(NAME_X - 56, 128), CGPointMake(NAME_X + 56, 128), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X - 56, 128), CGPointMake(NAME_X - 56, 123), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X + 56, 128), CGPointMake(NAME_X + 56, 123), 1, rgb(INK, 0.7));
    // sávnevek és elválasztók
    for (auto [x, n] : std::initializer_list<std::pair<CGFloat, NSString*>>{{LS_X, @"LOW SHELF"}, {LOW_X, @"LOW"}, {MID_X, @"MID"}, {HIGH_X, @"HIGH"}, {HS_X, @"HIGH SHELF"}})
        bracket(x - 60, x + 60, 302, n, INK);
    for (CGFloat x : {(LS_X + LOW_X) / 2, (LOW_X + MID_X) / 2, (MID_X + HIGH_X) / 2, (HIGH_X + HS_X) / 2, (HS_X + NAME_X) / 2 - 10})
        line(CGPointMake(x, 66), CGPointMake(x, 286), 1, rgb(0x000000, 0.25)), line(CGPointMake(x + 1, 66), CGPointMake(x + 1, 286), 1, rgb(0xFFFFFF, 0.05));
    textFont(@"11.6 kHz", @"AvenirNext-DemiBold", CGPointMake(HS_X, 266), 8, rgb(INK_DIM), 1.2);
}

void drawFace(PanelController& pc) {
    drawHeaderDynamic(pc);
    if (pc.get(kAutoGain) >= 0.5f) {   // AUTO GAIN: a ténylegesen beállt kiegyenlítés
        textFont([NSString stringWithFormat:@"AUTO %+.1f dB", pc.get(kAutoMakeup)], @"AvenirNext-DemiBold", CGPointMake(NAME_X, 240), 8.5, rgb(GOLD), 1);
    }
    drawLed(CGPointMake(NAME_X + 62, 296), pc.processing ? 0.9f : 0.0f, 0x46D35A, nil, 3.5);
}

std::unique_ptr<PanelController> buildMastequiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xD8E0EA; t.accent = GOLD; t.led = 0x9FD3FF;
    t.minorTicks = 1; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF6F0E6;
    t.buttonTop = 0x2E3946; t.buttonBot = 0x10151B; t.buttonText = 0xE6ECF2;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined; t.coloredLine = true;
    theme() = t;
    auto pc = makePanel(au, "masteq");
    pc->watched = {{kAutoMakeup, 0}, {kRange, 2}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawFace;
    PanelController* P = pc.get();
    auto& cs = pc->controls;
    // gain-gombok: a skála a lépéseket mutatja (−12…+12), a kijelzés a RANGE szerinti valódi dB
    auto gainDb = ^NSString*(float v) { const float r = P->get(kRange) < 0.5f ? 6 : P->get(kRange) < 1.5f ? 9 : 12; return [NSString stringWithFormat:@"%+.1f dB", v * r / 12]; };
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    NSArray* gl = @[@"-12", @"-6", @"0", @"+6", @"+12"];
    NSArray* ql = @[@"1", @"2", @"3", @"4", @"5"];

    auto band = [&](CGFloat x, AudioUnitParameterID g, AudioUnitParameterID f, AudioUnitParameterID q, const float* hz, NSArray* fl) {
        const float* table = hz;
        cs.push_back({Control::SteppedKnob, f, {x, 96}, 21, @"FREQ", fl, 0, 23, 12, ^NSString*(float v) { return hzText(table[std::clamp(int(v + 0.5f), 0, 23)]); }});
        cs.back().labelSize = 8.5; cs.back().cap = GOLD; cs.back().spread = 140;
        cs.push_back({Control::Knob, g, {x - 8, 198}, 28, @"GAIN", gl, -12, 12, 0, gainDb}); cs.back().labelSize = 9.5; cs.back().rim = RED;
        cs.push_back({Control::SteppedKnob, q, {x + 50, 262}, 10, @"Q", ql, 0, 4, 1}); cs.back().labelSize = 7.5; cs.back().cap = GOLD; cs.back().spread = 120;
    };
    band(LOW_X, kLowGain, kLowFreq, kLowQ, kLowHz, sparse(kLowHz, {0, 4, 9, 12, 17, 23}));
    band(MID_X, kMidGain, kMidFreq, kMidQ, kMidHz, sparse(kMidHz, {0, 4, 8, 12, 16, 19, 23}));
    band(HIGH_X, kHighGain, kHighFreq, kHighQ, kHighHz, sparse(kHighHz, {0, 4, 8, 12, 16, 19, 23}));
    cs.push_back({Control::Knob, kLowShelf, {LS_X, 160}, 30, @"GAIN", gl, -12, 12, 0, gainDb}); cs.back().labelSize = 9.5; cs.back().rim = RED;
    cs.push_back({Control::Toggle, kLowShift, {LS_X, 262}, 0.8, @"SHIFT", @[@"41 Hz", @"83 Hz"], 0, 1, 0});
    cs.push_back({Control::Knob, kHighShelf, {HS_X, 160}, 30, @"GAIN", gl, -12, 12, 0, gainDb}); cs.back().labelSize = 9.5; cs.back().rim = RED;
    cs.push_back({Control::Knob, kOutput, {NAME_X, 180}, 24, @"OUTPUT", @[@"-12", @"-6", @"0", @"+6", @"+12"], -12, 12, 0, dB}); cs.back().labelSize = 9;
    cs.push_back({Control::LedButton, kAutoGain, {NAME_X, 268}, 0.75, @"AUTO GAIN", nil, 0, 1, 0});

    const CGFloat dy = H + 58;
    cs.push_back({Control::SteppedKnob, kRange, {330, dy}, 20, @"RANGE", @[@"6 dB", @"9 dB", @"12 dB"], 0, 2, 2});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = GOLD; cs.back().spread = 70;

    pc->lock(kOutput, kAutoGain);
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildMastequiPanel)
