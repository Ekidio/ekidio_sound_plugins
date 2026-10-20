// LIMEKI saját panel — a jóváhagyott, kettéosztott GUI-terv (Tools/limeki_mockup.mm) szerint:
//   bal fél DENSITY (borvörös): meleg GR-VU, STEREO / DUAL, SLOW / FAST, THRESHOLD, CEILING, RELEASE (szorzó), ON
//   jobb fél CLEAN (kék): PUSH, CEILING (dBTP), RELEASE (AUTO + idők), TRUE PEAK, ON, üvegablakos LED-mérő (csúcstartó, plafonjelölő),
//   világító LUFS-kijelző, POWER; fiók: CLEAN LINK, LOOK-AHEAD, DENSITY MIX, AUTO −14 LUFS (bekapcsolva a PUSH zárolva, a ténylegesen
//   beállt érték látszik). A kikapcsolt fél elhalványul. AUTO GAIN nincs (a user döntése).
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kThreshold, kDenCeiling, kDenRelease, kLink, kDenMode, kDenOn,
                              kPush, kCeiling, kRelease, kTruePeak, kCleanOn, kCleanLink, kLookahead, kDenMix, kAutoLufs,
                              kDenGR, kCleanGR, kOutL, kOutR, kPeakL, kPeakR, kLufs, kPushApplied };

// bal fél: mély borvörös, krém gravírozás · jobb fél: mély kék eloxált, világos ezüst gravírozás
constexpr int INK = 0xF3E8D8, INK_DIM = 0xD2B9AE, AMBER = 0xFFB347;
constexpr int TI_INK = 0xEAF1FA, TI_DIM = 0xA9BBD3;
const Look LOOK = {
    0x5C2329, 0x4B1C21, 0x3A1519, 0.9,
    {0xE6E8EB, 0x8E9196, 0x4C5055, 0x72767B, 0xC2C5C9},
    0x2E3034, 0x34373B, 0x26282C, 1.0,
    INK, 0xB9BDC4, 0.14, 0.34,
};
constexpr CGFloat SPLIT = 480;                       // a két lemez határa
const NSRect kVU = NSMakeRect(30, 58, 180, 112);    // bal: GR-VU
const NSRect kMeter = NSMakeRect(742, 62, 60, 212); // jobb: kimeneti LED-mérő
constexpr CGFloat NAME_X = 870;
constexpr CGFloat THR_X = 318, DEN_X = 430, PUSH_X = 572, CL_X = 680;

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

/// kimeneti LED-mérő, elegáns változat: sötét üvegablak, vékony, sűrű szegmensek halk fénnyel (jégkék → fehér → borostyán → piros),
/// csúcstartó vonal, finom skála, a plafon borostyán jelölője háromszöggel
void drawLedMeter(NSRect r, float dbL, float dbR, float pkL, float pkR, float ceilDb) {
    NSBezierPath* bez = [NSBezierPath bezierPathWithRoundedRect:r xRadius:7 yRadius:7];
    shadowed(1, 5, 0.6, ^{ [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xC9D3E0), 0.0, rgb(0x6F7C8E), 0.5, rgb(0xB7C2D0), 1.0, nil] drawInBezierPath:bez angle:-90]; });
    const NSRect win = NSInsetRect(r, 3, 3);
    NSBezierPath* glass = [NSBezierPath bezierPathWithRoundedRect:win xRadius:5 yRadius:5];
    [[[NSGradient alloc] initWithStartingColor:rgb(0x0B1220) endingColor:rgb(0x03060C)] drawInBezierPath:glass angle:-90];
    CGContextSaveGState(cg()); [glass addClip];
    constexpr int N = 44;
    const CGFloat top = NSMinY(win) + 12, bot = NSMaxY(win) - 18, segH = (bot - top) / N;
    auto yOf = [&](float db) { const float t = std::clamp((db + 30.0f) / 30.0f, 0.0f, 1.0f); return bot - t * (bot - top); };
    auto colOf = [](float db) { return db > -1.0f ? 0xFF4A3D : db > -4.0f ? 0xFFB347 : db > -12.0f ? 0xE8F6FF : 0x5CCBFF; };
    const float lv[2] = {dbL, dbR}, pk[2] = {pkL, pkR};
    const CGFloat bw = 7, x0 = NSMinX(win) + 9;
    for (int ch = 0; ch < 2; ++ch) {
        const CGFloat x = x0 + ch * (bw + 5);
        for (int k = 0; k < N; ++k) {
            const float db = -30.0f + 30.0f * (k + 0.5f) / N;
            const bool on = db <= lv[ch];
            const NSRect seg = NSMakeRect(x, bot - (k + 1) * segH + 0.6, bw, segH - 1.2);
            [rgb(colOf(db), on ? 0.95 : 0.07) setFill]; NSRectFill(seg);
        }
        const CGFloat yl = yOf(lv[ch]);   // halk fény a világító rész körül
        [[[NSGradient alloc] initWithStartingColor:rgb(0x9FDCFF, 0.18) endingColor:rgb(0x9FDCFF, 0.0)] drawInRect:NSMakeRect(x - 3, yl, bw + 6, bot - yl) angle:-90];
        const CGFloat yp = yOf(pk[ch]);   // csúcstartó
        [rgb(colOf(pk[ch])) setFill]; NSRectFill(NSMakeRect(x, yp - 0.8, bw, 1.6));
    }
    for (float db : {0.f, -6.f, -12.f, -20.f, -30.f}) {   // finom skála
        const CGFloat y = yOf(db), xs = x0 + 2 * bw + 9;
        line(CGPointMake(xs, y), CGPointMake(xs + 3, y), 0.6, rgb(0xA9BBD3, 0.8));
        textFont([NSString stringWithFormat:@"%g", db], @"AvenirNext-Medium", CGPointMake(xs + 12, y), 6, rgb(0xA9BBD3), 0.3);
    }
    const CGFloat yc = yOf(ceilDb);   // plafon: vékony borostyán vonal + háromszög
    line(CGPointMake(x0 - 3, yc), CGPointMake(x0 + 2 * bw + 8, yc), 1.0, rgb(AMBER, 0.95));
    NSBezierPath* tri = [NSBezierPath bezierPath];
    [tri moveToPoint:NSMakePoint(x0 - 7, yc - 3.5)]; [tri lineToPoint:NSMakePoint(x0 - 2, yc)]; [tri lineToPoint:NSMakePoint(x0 - 7, yc + 3.5)]; [tri closePath];
    [rgb(AMBER) setFill]; [tri fill];
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.10) endingColor:rgb(0xFFFFFF, 0.0)] drawInRect:NSMakeRect(NSMinX(win), NSMinY(win), NSWidth(win), NSHeight(win) * 0.35) angle:-90];
    CGContextRestoreGState(cg());
    textFont(@"L", @"AvenirNext-DemiBold", CGPointMake(x0 + bw / 2, NSMaxY(win) - 8), 6.5, rgb(0xA9BBD3), 0.5);
    textFont(@"R", @"AvenirNext-DemiBold", CGPointMake(x0 + bw * 1.5 + 5, NSMaxY(win) - 8), 6.5, rgb(0xA9BBD3), 0.5);
}

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{330, @"clean stereo link"}, {450, @"look-ahead"}, {570, @"parallel density"}, {700, @"PUSH sets itself to -14 LUFS"}});
    // jobb fél: titán lemez a borvörös fölé (ugyanazon a lekerekített lemezen belül)
    const NSRect plateR = NSMakeRect(13, HEAD + 13, W - 26, H - HEAD - 26);
    NSBezierPath* plate = [NSBezierPath bezierPathWithRoundedRect:plateR xRadius:15 yRadius:15];
    CGContextSaveGState(cg());
    [plate addClip];
    const NSRect right = NSMakeRect(SPLIT, NSMinY(plateR), NSMaxX(plateR) - SPLIT, NSHeight(plateR));
    brushedPlate([NSBezierPath bezierPathWithRect:right], right, 0x2F5287, 0x24426F, 0x1A3156, 0x7A11C0DEu, 0.9);
    // belső árnyék a titán lemezen is
    for (int k = 0; k < 4; ++k) { line(CGPointMake(SPLIT, NSMinY(plateR) + k), CGPointMake(NSMaxX(plateR), NSMinY(plateR) + k), 2, rgb(0x000000, 0.18 - k * 0.04)); }
    CGContextRestoreGState(cg());
    // középső rack-sín: mély horony, két csavar
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x050506), 0.0, rgb(0x2A2C30), 0.45, rgb(0x0A0A0B), 1.0, nil]
        drawInRect:NSMakeRect(SPLIT - 5, NSMinY(plateR), 10, NSHeight(plateR)) angle:0];
    for (CGFloat y : {NSMinY(plateR) + 18, NSMaxY(plateR) - 18}) { fillOval(CGPointMake(SPLIT, y), 3.4, rgb(0xE2E5E9), rgb(0x6A6F76)); line(CGPointMake(SPLIT - 2, y), CGPointMake(SPLIT + 2, y), 0.8, rgb(0x2A2C30)); }
    // bal fél: szekciónév; jobb fél: a plugin neve
    textFont(@"LIMEKI", @"Futura-Bold", CGPointMake(NAME_X, 84), 23, rgb(TI_INK), 3);
    textFont(@"DENSITY & CLEAN", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 106), 6.5, rgb(TI_DIM), 1.6);
    textFont(@"LIMITER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 116), 6.5, rgb(TI_DIM), 1.6);
    line(CGPointMake(NAME_X - 56, 128), CGPointMake(NAME_X + 56, 128), 1, rgb(TI_INK, 0.7));
    line(CGPointMake(NAME_X - 56, 128), CGPointMake(NAME_X - 56, 123), 1, rgb(TI_INK, 0.7));
    line(CGPointMake(NAME_X + 56, 128), CGPointMake(NAME_X + 56, 123), 1, rgb(TI_INK, 0.7));
    bracket(26, SPLIT - 16, 302, @"DENSITY", INK);
    bracket(SPLIT + 16, NSMaxX(kMeter) + 4, 302, @"CLEAN", TI_INK);
    textFont(@"OUTPUT", @"AvenirNext-DemiBold", CGPointMake(NSMidX(kMeter), NSMaxY(kMeter) + 10), 7, rgb(TI_DIM), 1.6);
}

void drawFace(PanelController& pc) {
    drawHeaderDynamic(pc);
    VUStyle v = dialStyle(@"DENSITY", true, 3); v.legendSize = 12;
    drawVU(kVU, pc.get(kDenGR), v);
    drawLedMeter(kMeter, pc.get(kOutL), pc.get(kOutR), pc.get(kPeakL), pc.get(kPeakR), pc.get(kCeiling));
    // világító hangosság-kijelző (rövid távú LUFS)
    const NSRect box = NSMakeRect(NAME_X - 52, 178, 104, 34);
    shadowed(1, 3, 0.5, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x0A0A0B) endingColor:rgb(0x1C1D20)] drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:box xRadius:4 yRadius:4] angle:-90]; });
    textFont(@"-88.8", @"Menlo-Bold", CGPointMake(NSMidX(box), NSMidY(box)), 18, rgb(0xFFC060, 0.07), 1.5);
    const float l = pc.get(kLufs);
    NSString* txt = (!pc.processing || l < -60.0f) ? @" --.-" : [NSString stringWithFormat:@"%5.1f", l];
    NSShadow* glow = [NSShadow new]; glow.shadowColor = rgb(0xFF9A2A, 0.95); glow.shadowBlurRadius = 7; glow.shadowOffset = NSZeroSize;
    [NSGraphicsContext saveGraphicsState]; [glow set];
    textFont(txt, @"Menlo-Bold", CGPointMake(NSMidX(box), NSMidY(box)), 18, rgb(0xFFC060), 1.5);
    [NSGraphicsContext restoreGraphicsState];
    textFont(@"LUFS  ·  SHORT TERM", @"AvenirNext-DemiBold", CGPointMake(NAME_X, NSMaxY(box) + 12), 6.5, rgb(TI_DIM), 1.6);
    drawLed(CGPointMake(NAME_X, 290), pc.processing ? 0.9f : 0.0f, 0x46D35A, nil, 4.5);   // a plugin be/ki állapotát követi
    textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 305), 6.5, rgb(TI_DIM), 2);
}

/// a vezérlők fölött: a kikapcsolt fél elhalványul; AUTO −14 LUFS alatt a PUSH ténylegesen beállt értéke
void drawOverlay(PanelController& pc) {
    const NSRect plateR = NSMakeRect(13, HEAD + 13, W - 26, H - HEAD - 26);
    if (pc.get(kDenOn) < 0.5f) {
        NSBezierPath* pth = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(NSMinX(plateR), NSMinY(plateR), SPLIT - 5 - NSMinX(plateR), NSHeight(plateR)) xRadius:15 yRadius:15];
        [rgb(0x000000, 0.38) setFill]; [pth fill];
    }
    if (pc.get(kCleanOn) < 0.5f) {
        [rgb(0x000000, 0.38) setFill]; NSRectFillUsingOperation(NSMakeRect(SPLIT + 5, NSMinY(plateR), NSMaxX(kMeter) + 10 - SPLIT, NSHeight(plateR)), NSCompositingOperationSourceOver);
    }
    if (pc.get(kAutoLufs) >= 0.5f) {
        const NSRect r = NSMakeRect(PUSH_X - 46, 218, 92, 15);
        [rgb(0x0A1426, 0.85) setFill]; [[NSBezierPath bezierPathWithRoundedRect:r xRadius:4 yRadius:4] fill];
        textFont([NSString stringWithFormat:@"AUTO %+.1f dB", pc.get(kPushApplied)], @"AvenirNext-DemiBold", CGPointMake(NSMidX(r), NSMidY(r)), 8.5, rgb(AMBER), 1);
    }
}

std::unique_ptr<PanelController> buildLimekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xE8DCCB; t.accent = AMBER; t.led = 0xFFB040;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF6F0E6;
    t.buttonTop = 0x3A1E22; t.buttonBot = 0x150A0C; t.buttonText = 0xF0E6D8;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined; t.coloredLine = true;
    theme() = t;
    auto pc = makePanel(au, "limeki");
    pc->watched = {{kDenGR, 0}, {kCleanGR, 0}, {kOutL, -60}, {kOutR, -60}, {kPeakL, -60}, {kPeakR, -60}, {kLufs, -70}, {kPushApplied, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawFace;
    pc->drawOverlay = drawOverlay;
    auto& cs = pc->controls;
    auto negDb = ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f dB", -v]; };
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto dBTP = ^NSString*(float v) { return [NSString stringWithFormat:@"%.1f dBTP", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };
    auto mult = ^NSString*(float v) { return [NSString stringWithFormat:@"%.2f×", v]; };

    // ---- bal fél: DENSITY ----
    cs.push_back({Control::RadioRow, kLink, {120, 232}, 1.0, nil, @[@"STEREO", @"DUAL"], 0, 1, 0}); cs.back().rowWiden = 1.6;
    cs.push_back({Control::RadioRow, kDenMode, {120, 270}, 1.0, nil, @[@"SLOW", @"FAST"], 0, 1, 0}); cs.back().rowWiden = 1.6;
    cs.push_back({Control::Knob, kThreshold, {THR_X, 156}, 50, @"THRESHOLD", @[@"0", @"-5", @"-10", @"-15", @"-20", @"-25", @"-30"], 0, 30, 6, negDb});
    cs.back().knobStyle = Fluted; cs.back().labelSize = 11;
    cs.push_back({Control::Knob, kDenCeiling, {DEN_X, 100}, 20, @"CEILING", @[@"-12", @"-6", @"0"], -12, 0, 0, dB}); cs.back().labelSize = 8.5; cs.back().cap = AMBER;
    cs.push_back({Control::Knob, kDenRelease, {DEN_X, 200}, 20, @"RELEASE", @[@"½×", @"1×", @"2×"], 0.5f, 2, 1, mult}); cs.back().labelSize = 8.5; cs.back().cap = AMBER;
    cs.push_back({Control::LedButton, kDenOn, {DEN_X, 266}, 0.75, @"ON", nil, 0, 1, 1});

    // ---- jobb fél: CLEAN (világos feliratok a kéken) ----
    cs.push_back({Control::Knob, kPush, {PUSH_X, 150}, 46, @"PUSH", ticks(0, 2, 7), 0, 12, 0, dB}); cs.back().labelSize = 11; cs.back().ink = TI_INK;
    cs.push_back({Control::Knob, kCeiling, {CL_X, 112}, 20, @"CEILING", @[@"-3", @"-2", @"-1", @"0"], -3, 0, -1, dBTP}); cs.back().labelSize = 8.5; cs.back().cap = AMBER; cs.back().ink = TI_INK;
    cs.push_back({Control::SteppedKnob, kRelease, {CL_X, 200}, 18, @"RELEASE ms", @[@"AUTO", @"10", @"30", @"100", @"300", @"1k"], 0, 5, 0});
    cs.back().labelSize = 8; cs.back().cap = AMBER; cs.back().spread = 120; cs.back().ink = TI_INK;
    cs.push_back({Control::LedButton, kTruePeak, {PUSH_X, 266}, 0.75, @"TRUE PEAK", nil, 0, 1, 1}); cs.back().ink = TI_INK;
    cs.push_back({Control::LedButton, kCleanOn, {CL_X, 266}, 0.75, @"ON", nil, 0, 1, 1}); cs.back().ink = TI_INK;

    // ---- EKI fiók ----
    const CGFloat dy = H + 58;
    cs.push_back({Control::Knob, kCleanLink, {330, dy}, 22, @"CLEAN LINK", nil, 0, 100, 0, pct}); cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = AMBER;
    cs.push_back({Control::SteppedKnob, kLookahead, {450, dy}, 20, @"LOOK-AHEAD ms", @[@"1", @"3", @"5"], 0, 2, 1}); cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = AMBER; cs.back().spread = 70;
    cs.push_back({Control::Knob, kDenMix, {570, dy}, 22, @"DENSITY MIX", nil, 0, 100, 100, pct}); cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = AMBER;
    cs.push_back({Control::LedButton, kAutoLufs, {700, dy - 8}, 0.85, @"AUTO -14 LUFS", nil, 0, 1, 0}); cs.back().drawer = true;

    pc->lock(kPush, kAutoLufs);   // AUTO −14 LUFS alatt a PUSH magától áll (a fedőkijelző mutatja)
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildLimekiPanel)
