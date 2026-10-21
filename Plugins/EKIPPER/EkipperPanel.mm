// EKIPPER saját panel — EKI-stílus: szénszürke, szálcsiszolt lemez, parázsszínű kiemelés. A user döntése (2026-10-21):
// csak lágy (WARM) vágás, az előlapon nincs algoritmus-felirat, CEILING / MIX / AUTO GAIN nincs.
//   bal: nagy CLIP-műszer (mennyit vág, dB) · közép: nagy DRIVE, OUTPUT, CLIP-lámpa · jobb: üvegablakos LED-mérő,
//   név, világító CLIP-kijelző, POWER · fiók: OVERSAMPLING (2× / 4× / 8×)
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kDrive, kOutput, kOversample, kClip, kOutL, kOutR, kPeakL, kPeakR };

constexpr int INK = 0xF1E6D6, INK_DIM = 0xB9AE9F, EMBER = 0xFF6A2B, AMBER = 0xFFB347;
const Look LOOK = {
    0x2D2E33, 0x24252A, 0x18191C, 0.9,
    {0xE6E8EB, 0x8E9196, 0x4C5055, 0x72767B, 0xC2C5C9},
    0x2E3034, 0x34373B, 0x26282C, 1.0,
    INK, 0xB9BDC4, 0.14, 0.34,
};
const NSRect kVU = NSMakeRect(34, 70, 214, 140);
const NSRect kMeter = NSMakeRect(672, 62, 60, 212);
constexpr CGFloat DRV_X = 405, OUT_X = 562, NAME_X = 845;

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

/// üvegablakos LED-mérő (mint a LIMEKI-n), parázsszínű tetővel és a plafon jelölőjével
void drawLedMeter(NSRect r, float dbL, float dbR, float pkL, float pkR, float ceilDb) {
    NSBezierPath* bez = [NSBezierPath bezierPathWithRoundedRect:r xRadius:7 yRadius:7];
    shadowed(1, 5, 0.6, ^{ [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xD5D7DA), 0.0, rgb(0x6C6F75), 0.5, rgb(0xB9BCC1), 1.0, nil] drawInBezierPath:bez angle:-90]; });
    const NSRect win = NSInsetRect(r, 3, 3);
    NSBezierPath* glass = [NSBezierPath bezierPathWithRoundedRect:win xRadius:5 yRadius:5];
    [[[NSGradient alloc] initWithStartingColor:rgb(0x15100D) endingColor:rgb(0x060404)] drawInBezierPath:glass angle:-90];
    CGContextSaveGState(cg()); [glass addClip];
    constexpr int N = 44;
    const CGFloat top = NSMinY(win) + 12, bot = NSMaxY(win) - 18, segH = (bot - top) / N;
    auto yOf = [&](float db) { const float t = std::clamp((db + 30.0f) / 30.0f, 0.0f, 1.0f); return bot - t * (bot - top); };
    auto colOf = [](float db) { return db > -1.0f ? 0xFF3B2A : db > -4.0f ? EMBER : db > -12.0f ? 0xFFD9A0 : 0xF2A33A; };
    const float lv[2] = {dbL, dbR}, pk[2] = {pkL, pkR};
    const CGFloat bw = 7, x0 = NSMinX(win) + 9;
    for (int ch = 0; ch < 2; ++ch) {
        const CGFloat x = x0 + ch * (bw + 5);
        for (int k = 0; k < N; ++k) {
            const float db = -30.0f + 30.0f * (k + 0.5f) / N;
            const bool on = db <= lv[ch];
            [rgb(colOf(db), on ? 0.95 : 0.07) setFill];
            NSRectFill(NSMakeRect(x, bot - (k + 1) * segH + 0.6, bw, segH - 1.2));
        }
        const CGFloat yl = yOf(lv[ch]);
        [[[NSGradient alloc] initWithStartingColor:rgb(0xFF9A50, 0.16) endingColor:rgb(0xFF9A50, 0.0)] drawInRect:NSMakeRect(x - 3, yl, bw + 6, bot - yl) angle:-90];
        const CGFloat yp = yOf(pk[ch]);
        [rgb(colOf(pk[ch])) setFill]; NSRectFill(NSMakeRect(x, yp - 0.8, bw, 1.6));
    }
    for (float db : {0.f, -6.f, -12.f, -20.f, -30.f}) {
        const CGFloat y = yOf(db), xs = x0 + 2 * bw + 9;
        line(CGPointMake(xs, y), CGPointMake(xs + 3, y), 0.6, rgb(INK_DIM, 0.8));
        textFont([NSString stringWithFormat:@"%g", db], @"AvenirNext-Medium", CGPointMake(xs + 12, y), 6, rgb(INK_DIM), 0.3);
    }
    const CGFloat yc = yOf(ceilDb);
    line(CGPointMake(x0 - 3, yc), CGPointMake(x0 + 2 * bw + 8, yc), 1.0, rgb(AMBER, 0.95));
    NSBezierPath* tri = [NSBezierPath bezierPath];
    [tri moveToPoint:NSMakePoint(x0 - 7, yc - 3.5)]; [tri lineToPoint:NSMakePoint(x0 - 2, yc)]; [tri lineToPoint:NSMakePoint(x0 - 7, yc + 3.5)]; [tri closePath];
    [rgb(AMBER) setFill]; [tri fill];
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.10) endingColor:rgb(0xFFFFFF, 0.0)] drawInRect:NSMakeRect(NSMinX(win), NSMinY(win), NSWidth(win), NSHeight(win) * 0.35) angle:-90];
    CGContextRestoreGState(cg());
    textFont(@"L", @"AvenirNext-DemiBold", CGPointMake(x0 + bw / 2, NSMaxY(win) - 8), 6.5, rgb(INK_DIM), 0.5);
    textFont(@"R", @"AvenirNext-DemiBold", CGPointMake(x0 + bw * 1.5 + 5, NSMaxY(win) - 8), 6.5, rgb(INK_DIM), 0.5);
}

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{330, @"internal oversampling (higher = smoother, more CPU)"}});
    textFont(@"EKIPPER", @"Futura-Bold", CGPointMake(NAME_X, 84), 24, rgb(INK), 3);
    textFont(@"SOFT CLIPPER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 110), 6.5, rgb(INK_DIM), 1.6);
    line(CGPointMake(NAME_X - 56, 128), CGPointMake(NAME_X + 56, 128), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X - 56, 128), CGPointMake(NAME_X - 56, 123), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X + 56, 128), CGPointMake(NAME_X + 56, 123), 1, rgb(INK, 0.7));
    bracket(26, OUT_X + 48, 302, @"CLIPPER", INK);
    textFont(@"OUTPUT", @"AvenirNext-DemiBold", CGPointMake(NSMidX(kMeter), NSMaxY(kMeter) + 10), 7, rgb(INK_DIM), 1.6);
}

void drawFace(PanelController& pc) {
    drawHeaderDynamic(pc);
    VUStyle v = dialStyle(@"CLIP", true, 3); v.subLegend = @"CLIPPED dB"; v.legendSize = 14;
    drawVU(kVU, pc.get(kClip), v);
    drawLedMeter(kMeter, pc.get(kOutL), pc.get(kOutR), pc.get(kPeakL), pc.get(kPeakR), 0.0f);
    // CLIP-lámpa: annál erősebben izzik, minél többet vág
    const float clip = -pc.get(kClip);
    drawLed(CGPointMake(OUT_X, 262), pc.processing ? std::clamp(clip / 3.0f, 0.0f, 1.0f) : 0.0f, EMBER, nil, 6.5);
    textFont(@"CLIP", @"AvenirNext-DemiBold", CGPointMake(OUT_X, 280), 7, rgb(INK), 2);
    // világító CLIP-kijelző (mennyit vág most, dB)
    const NSRect box = NSMakeRect(NAME_X - 52, 178, 104, 30);
    shadowed(1, 3, 0.5, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x0A0A0B) endingColor:rgb(0x1C1D20)] drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:box xRadius:4 yRadius:4] angle:-90]; });
    NSString* txt = pc.processing ? [NSString stringWithFormat:@"%5.1f", -clip] : @" --.-";
    textFont(@"-88.8", @"Menlo-Bold", CGPointMake(NSMidX(box), NSMidY(box)), 16, rgb(0xFF8A40, 0.07), 1.5);
    NSShadow* glow = [NSShadow new]; glow.shadowColor = rgb(0xFF6A2B, 0.95); glow.shadowBlurRadius = 7; glow.shadowOffset = NSZeroSize;
    [NSGraphicsContext saveGraphicsState]; [glow set];
    textFont(txt, @"Menlo-Bold", CGPointMake(NSMidX(box), NSMidY(box)), 16, rgb(0xFFA060), 1.5);
    [NSGraphicsContext restoreGraphicsState];
    textFont(@"CLIP  ·  dB", @"AvenirNext-DemiBold", CGPointMake(NAME_X, NSMaxY(box) + 12), 6.5, rgb(INK_DIM), 1.6);
    drawLed(CGPointMake(NAME_X, 290), pc.processing ? 0.9f : 0.0f, 0x46D35A, nil, 4.5);
    textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 305), 6.5, rgb(INK_DIM), 2);
}

std::unique_ptr<PanelController> buildEkipperPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xE8DCCB; t.accent = EMBER; t.led = EMBER;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF6F0E6;
    t.buttonTop = 0x34353A; t.buttonBot = 0x111214; t.buttonText = 0xF0E6D8;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined; t.coloredLine = true;
    theme() = t;
    auto pc = makePanel(au, "ekipper");
    pc->watched = {{kClip, 0}, {kOutL, -60}, {kOutR, -60}, {kPeakL, -60}, {kPeakR, -60}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawFace;
    auto& cs = pc->controls;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };

    cs.push_back({Control::Knob, kDrive, {DRV_X, 158}, 58, @"DRIVE", ticks(0, 4, 6), 0, 20, 0, dB});
    cs.back().knobStyle = Fluted; cs.back().labelSize = 11;
    cs.push_back({Control::Knob, kOutput, {OUT_X, 140}, 26, @"OUTPUT", ticks(-12, 6, 5), -12, 12, 0, dB}); cs.back().labelSize = 9;

    const CGFloat dy = H + 58;
    cs.push_back({Control::SteppedKnob, kOversample, {330, dy}, 20, @"OVERSAMPLING", @[@"2x", @"4x", @"8x"], 0, 2, 1});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = AMBER; cs.back().spread = 70;

    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildEkipperPanel)
