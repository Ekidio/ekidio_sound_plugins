// DBEKI saját panel — EKIDIO SOUND rack-stílus a klasszikus 160-as kompresszorok hangulatában:
//   fekete előlap, felül-alul ezüst díszléc, két oldalt diófa oldallap, ezüst esztergált gombok,
//   BELOW/ABOVE küszöb-LED-ek, METER gombsor, jobbra a családi pajzs-VU
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kThreshold, kRatio, kOutput, kOverEasy, kAutoGain, kAnalog, kMix, kMeterMode, kVU, kGR, kAutoMakeup, kOver };

constexpr int INK = 0xEDEEF0, INK_DIM = 0xA3A6AB, SILVER = 0xC9CBCF;
const Look LOOK = {
    0x232427, 0x1B1C1E, 0x131416, 0.6,
    {0xC9CCD1, 0x6A6D72, 0x2E3034, 0x4A4D52, 0x9A9DA2},
    0x2A2C30, 0x25272A, 0x1E2023, 1.1,
    INK, INK_DIM, 0.08, 0.30,
};
constexpr CGFloat WOOD = 32;   // diófa oldallap szélessége

/// diófa oldallap: meleg barna alap, hullámzó évgyűrű-erezet (determinisztikus), élárnyékok
void woodCheek(NSRect r, uint32_t seed) {
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x8C5A33), 0.0, rgb(0x744624), 0.5, rgb(0x5E3719), 1.0, nil] drawInRect:r angle:-90];
    CGContextSaveGState(cg());
    NSRectClip(r);
    for (int k = 0; k < 26; ++k) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        const CGFloat x0 = NSMinX(r) + (seed % 1000) / 1000.0 * r.size.width;
        const CGFloat amp = 1.0 + (seed >> 10) % 30 / 10.0, freq = 0.02 + ((seed >> 16) % 20) / 1000.0, ph = (seed >> 4) % 628 / 100.0;
        NSBezierPath* g = [NSBezierPath bezierPath];
        for (CGFloat y = NSMinY(r); y <= NSMaxY(r); y += 3) {
            const NSPoint p = NSMakePoint(x0 + amp * std::sin(y * freq + ph), y);
            y == NSMinY(r) ? [g moveToPoint:p] : [g lineToPoint:p];
        }
        g.lineWidth = 0.6 + (seed % 3) * 0.4;
        [rgb((seed & 1) ? 0x3A2010 : 0xB07A4A, (seed & 1) ? 0.35 : 0.18) setStroke];
        [g stroke];
    }
    CGContextRestoreGState(cg());
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.10) endingColor:rgb(0x000000, 0.25)] drawInRect:r angle:0];
}

/// csiszolt alumínium díszléc
void silverTrim(NSRect r) {
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xF0F1F3), 0.0, rgb(0xB9BCC0), 0.35, rgb(0xE2E4E7), 0.55, rgb(0x8E9196), 1.0, nil] drawInRect:r angle:-90];
    line(CGPointMake(NSMinX(r), NSMaxY(r) - 0.5), CGPointMake(NSMaxX(r), NSMaxY(r) - 0.5), 1, rgb(0x000000, 0.6));
    line(CGPointMake(NSMinX(r), NSMinY(r) + 0.5), CGPointMake(NSMaxX(r), NSMinY(r) + 0.5), 1, rgb(0xFFFFFF, 0.6));
}

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{480, @"VCA & op-amp color"}});
    NSRect plateR = NSInsetRect(NSMakeRect(8, HEAD + 8, W - 16, H - HEAD - 16), 5, 5);
    CGContextSaveGState(cg());
    [[NSBezierPath bezierPathWithRoundedRect:plateR xRadius:15 yRadius:15] addClip];
    silverTrim(NSMakeRect(NSMinX(plateR), NSMinY(plateR), plateR.size.width, 11));
    silverTrim(NSMakeRect(NSMinX(plateR), NSMaxY(plateR) - 11, plateR.size.width, 11));
    woodCheek(NSMakeRect(NSMinX(plateR), NSMinY(plateR), WOOD, plateR.size.height), 0x1234567u);
    woodCheek(NSMakeRect(NSMaxX(plateR) - WOOD, NSMinY(plateR), WOOD, plateR.size.height), 0x7654321u);
    CGContextRestoreGState(cg());

    // gravírozott címek a gombok fölött
    for (auto t : std::vector<std::pair<CGFloat, NSString*>>{{175, @"THRESHOLD"}, {352, @"COMPRESSION"}, {528, @"OUTPUT GAIN"}})
        textFont(t.second, @"AvenirNext-DemiBold", CGPointMake(t.first, 76), 11, rgb(INK), 2.5);
    textFont(@"BELOW", @"AvenirNext-DemiBold", CGPointMake(108, 96), 7.5, rgb(INK_DIM), 1.5);
    textFont(@"ABOVE", @"AvenirNext-DemiBold", CGPointMake(244, 96), 7.5, rgb(INK_DIM), 1.5);
    textFont(@"COMPRESSOR / LIMITER", @"AvenirNext-DemiBold", CGPointMake(764, 231), 8.5, rgb(INK), 2.5);
}

void drawDynamic(PanelController& pc) {
    drawHeaderDynamic(pc);
    const bool gr = std::lround(pc.get(kMeterMode)) == 0;
    drawVU(NSMakeRect(632, 60, 264, 162), pc.get(kVU), dialStyle(@"DBEKI", gr));
    // küszöb-LED-ek: sárga = a jel a küszöb alatt, piros = felette
    const float over = pc.get(kOver);
    drawLed(CGPointMake(140, 96), over <= 0 ? 1.0f : 0.0f, 0xFFC21F, nil, 5);
    drawLed(CGPointMake(210, 96), over > 0 ? 1.0f : 0.0f, 0xFF3A22, nil, 5);
}

std::unique_ptr<PanelController> buildDbekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xE4E5E8; t.accent = 0xFFC46B; t.led = 0xFF5A2A;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false;
    t.pointer = 0x1A1A1A;                                  // sötét mutató az ezüst sapkán
    t.buttonTop = 0x3A3C41; t.buttonBot = 0x141518; t.buttonText = 0xE8EAEE;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined;
    theme() = t;
    auto pc = makePanel(au, "dbeki");
    pc->watched = {{kVU, -20}, {kGR, 0}, {kAutoMakeup, 0}, {kOver, -30}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawDynamic;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    // a három nagy ezüst gomb (cím fölöttük, érték alattuk)
    pc->controls.push_back({Control::Knob, kThreshold, {175, 170}, 38, nil, ticks(-40, 10, 5), -40, 0, -16,
        ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f dB", v]; }});
    pc->controls.back().tint = SILVER; pc->controls.back().labelSize = 11;
    pc->controls.push_back({Control::Knob, kRatio, {352, 170}, 42, nil, @[@"1", @"1.5", @"2", @"3", @"4", @"6", @"10", @"20", @"∞"], 1, 60, 4,
        ^NSString*(float v) { return v >= 59.5f ? @"∞ : 1" : [NSString stringWithFormat:@"%.1f : 1", v]; }});
    pc->controls.back().tint = SILVER; pc->controls.back().labelSize = 11;
    pc->controls.back().taper = {1, 1.5f, 2, 3, 4, 6, 10, 20, 60};
    pc->controls.push_back({Control::Knob, kOutput, {528, 170}, 38, nil, ticks(-20, 10, 5), -20, 20, 0, dB});
    pc->controls.back().tint = SILVER; pc->controls.back().labelSize = 11;

    // alsó sor: OVEREASY, METER gombsor
    pc->controls.push_back({Control::LedButton, kOverEasy, {175, 266}, 0.8, @"OVEREASY", nil, 0, 1, 1});
    pc->controls.push_back({Control::RadioRow, kMeterMode, {440, 266}, 1.2, @"METER", @[@"GAIN", @"OUT", @"IN"], 0, 2, 0});

    // VU alatt: AUTO GAIN, MIX
    pc->controls.push_back({Control::LedButton, kAutoGain, {700, 272}, 0.8, @"AUTO GAIN", nil, 0, 1, 0});
    pc->controls.push_back({Control::Knob, kMix, {830, 260}, 14, @"MIX", nil, 0, 100, 100, pct});
    pc->controls.back().tint = SILVER; pc->controls.back().labelSize = 8.5;

    // EKI fiók
    pc->controls.push_back({Control::Knob, kAnalog, {480, H + 58}, 24, @"ANALOG", nil, 0, 100, 40, pct});
    pc->controls.back().drawer = true; pc->controls.back().labelSize = 9; pc->controls.back().tint = SILVER;
    pc->lock(kOutput, kAutoGain);                          // AUTO GAIN alatt a gain 0-n, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildDbekiPanel)
