// DYNEKI-6 GUI-terv: a közös panel-motorral rajzolt látványterv (AU nélkül), PNG-be.
//   clang++ -std=c++20 -fobjc-arc -DEKI_UI_FACTORY=DynekiMock -IShared Tools/dyneki_mockup.mm \
//           -framework AppKit -framework AudioToolbox -o build/dyneki_mockup
//   build/dyneki_mockup out.png [drawer]
// Jelút: EQ (6 sáv, M/S kettős EQ) → SAT → DYN (DBEKI-motor, egy gomb) → OUTPUT; AUTO GAIN az egész láncra.
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kHpf, kLowG, kLowF, kLmG, kLmF, kHmG, kHmF, kHighG, kHighF, kAirG, kAirF, kVoice, kStereo, kEdit,
                              kSat, kComp, kOutput, kAutoGain, kAnalog, kLmQ, kHmQ, kMix };

// sötét grafit szálcsiszolt előlap, világos gravírozás
constexpr int INK = 0xECEDEF, INK_DIM = 0xA4A8AE;
const Look LOOK = {
    0x3B3D42, 0x323438, 0x292B2F, 0.9,
    {0xE4E6E9, 0x8C9095, 0x4C5055, 0x72767B, 0xC2C5C9},
    0x2F3135, 0x34363A, 0x292B2E, 1.0,
    INK, INK_DIM, 0.12, 0.32,
};
constexpr int RED = 0xE0442F, BLUE = 0x3E86E0, GREY = 0xB9C0C8, GOLD = 0xE2B04A;
const CGFloat COL[] = {74, 160, 246, 332, 418, 504};    // HPF, LOW, LOW MID, HIGH MID, HIGH, AIR
const NSRect kAir = NSMakeRect(461, 82, 86, 230);       // arany AIR mező
const NSRect kDyn = NSMakeRect(560, 56, 290, 256);      // fekete SAT + DYN modul a VU-val
constexpr CGFloat OUTX = 898;

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{330, @"color amount"}, {520, @"mid bandwidth (AUTO = proportional)"}, {780, @"parallel"}, {110, @"signal flow:  EQ  ›  SAT  ›  DYN  ›  OUT"}});
    // AIR: arany eloxált mező
    NSBezierPath* a = [NSBezierPath bezierPathWithRoundedRect:kAir xRadius:8 yRadius:8];
    shadowed(1, 4, 0.5, ^{ [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xD9B25C), 0.0, rgb(0xB88F3A), 0.55, rgb(0x9A752B), 1.0, nil]
                               drawInBezierPath:a angle:-90]; });
    [rgb(0xFFF2C8, 0.35) setStroke]; a.lineWidth = 0.8; [a stroke];
    // SAT + DYN modul: fekete betét csavarokkal
    NSBezierPath* d = [NSBezierPath bezierPathWithRoundedRect:kDyn xRadius:9 yRadius:9];
    shadowed(2, 6, 0.6, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x1C1D20) endingColor:rgb(0x0F1012)] drawInBezierPath:d angle:-90]; });
    [rgb(0xFFFFFF, 0.10) setStroke]; d.lineWidth = 0.8; [d stroke];
    for (CGPoint s : {CGPointMake(NSMinX(kDyn) + 11, NSMinY(kDyn) + 11), CGPointMake(NSMaxX(kDyn) - 11, NSMinY(kDyn) + 11),
                      CGPointMake(NSMinX(kDyn) + 11, NSMaxY(kDyn) - 11), CGPointMake(NSMaxX(kDyn) - 11, NSMaxY(kDyn) - 11)})
        drawScrew(s, 3.2);
    // sávcímek
    const char* names[] = {"HPF", "LOW", "LOW MID", "HIGH MID", "HIGH", "AIR"};
    for (int i = 0; i < 6; ++i)
        textFont(@(names[i]), @"AvenirNext-DemiBold", CGPointMake(COL[i], 100), 9.5, rgb(i == 5 ? 0x2A1E08 : INK), 2.2);
    textFont(@"OUTPUT", @"AvenirNext-DemiBold", CGPointMake(OUTX, 100), 9.5, rgb(INK), 2.2);
    // M/S sor feliratai
    textFont(@"MODE", @"AvenirNext-DemiBold", CGPointMake(56, 72), 7.5, rgb(INK_DIM), 2);
    textFont(@"EDIT", @"AvenirNext-DemiBold", CGPointMake(236, 72), 7.5, rgb(INK_DIM), 2);
    // finom elválasztók a sávok között
    for (int i = 0; i < 4; ++i) {
        const CGFloat x = (COL[i] + COL[i + 1]) / 2;
        line(CGPointMake(x, 112), CGPointMake(x, 302), 1, rgb(0x000000, 0.25));
        line(CGPointMake(x + 1, 112), CGPointMake(x + 1, 302), 1, rgb(0xFFFFFF, 0.05));
    }
}

void drawFace(PanelController& pc) {
    textFont(@"Default", @"AvenirNext-DemiBold", CGPointMake(CGRectGetMidX(kPreset), 18), 10.5, rgb(0xEDE3CF), 1.2);
    drawVU(NSMakeRect(586, 66, 238, 150), -4.5f, dialStyle(@"DYNEKI-6", true));
    drawLed(CGPointMake(OUTX, 290), 0.85f, 0xFF3A22, nil, 5.5);
    textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(OUTX, 305), 6.5, rgb(INK_DIM), 2);
}

}

int main(int argc, char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        const bool drawer = argc > 2;
        Theme t;
        t.silk = INK; t.tick = 0xE4E8EC; t.accent = 0xFFD27A; t.led = 0xFF5A2A;
        t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF6F6F6;
        t.buttonTop = 0x2E2F34; t.buttonBot = 0x0E0F11; t.buttonText = 0xE8EAEE;
        t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
        t.knob = Machined; t.coloredLine = true;
        theme() = t;

        PanelController pc(nullptr, CGSizeMake(W, H), "dyneki-mockup");
        pc.drawerHeight = DRAWER; pc.drawerOpen = drawer;
        pc.drawStatic = drawStatic; pc.drawFace = drawFace;
        auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
        auto add = [&](Control c, float v) { c.value = v; pc.controls.push_back(c); return &pc.controls.back(); };
        Control* c;

        // ---- M/S sor ----
        c = add({Control::RadioRow, kStereo, {134, 72}, 1.05, nil, @[@"L/R", @"M/S"], 0, 1, 0}, 1);
        c = add({Control::RadioRow, kEdit, {314, 72}, 1.05, nil, @[@"MID", @"SIDE"], 0, 1, 0}, 0);

        // ---- EQ ----
        c = add({Control::SteppedKnob, kHpf, {COL[0], 168}, 22, @"FREQ Hz", @[@"OFF", @"50", @"80", @"160", @"300"], 0, 4, 0}, 2);
        c->spread = 120; c->cap = GREY; c->labelSize = 8.5;
        c = add({Control::Selector, kVoice, {COL[0], 262}, 20, @"VOICE", @[@"BRIT", @"PUNCH"], 0, 1, 0}, 0);
        c->spread = 45; c->labelDy = 30;
        struct B { AudioUnitParameterID g, f; int col, cap; NSArray* freqs; NSString* unit; float gv, fv, lo, hi; };
        B bands[] = {
            {kLowG, kLowF, 1, BLUE, @[@"15", @"35", @"60", @"110", @"220"], @"Hz", 3.0f, 2, -16, 16},
            {kLmG, kLmF, 2, RED, @[@".2", @".4", @".8", @"1.5", @"3"], @"kHz", -2.0f, 1, -16, 16},
            {kHmG, kHmF, 3, RED, @[@"1.5", @"3", @"5", @"7", @"10"], @"kHz", 2.5f, 2, -16, 16},
            {kHighG, kHighF, 4, BLUE, @[@"8", @"10", @"12", @"16"], @"kHz", 1.5f, 2, -16, 16},
            {kAirG, kAirF, 5, GOLD, @[@"2.5", @"5", @"10", @"20", @"40"], @"kHz", 4.0f, 3, 0, 15},
        };
        for (auto& b : bands) {
            c = add({Control::Knob, b.g, {COL[b.col], 168}, 25, @"GAIN",
                     b.lo < 0 ? ticks(-16, 16, 3) : ticks(0, 5, 4), b.lo, b.hi, 0, dB}, b.gv);
            c->cap = b.cap; c->labelSize = 8.5;
            c = add({Control::SteppedKnob, b.f, {COL[b.col], 262}, 14, b.unit, b.freqs, 0, float(b.freqs.count - 1), 0}, b.fv);
            c->spread = 115; c->cap = b.cap; c->labelSize = 8;
            if (b.col == 5) { c->ink = 0x14100A; (c - 1)->ink = 0x14100A; }   // AIR: fekete feliratok az arany mezőn
        }

        // ---- SAT + DYN (értékkijelzés nélkül: amire tekered, az a jó) ----
        c = add({Control::Knob, kSat, {636, 260}, 24, @"SAT", ticks(0, 2, 6), 0, 10, 0}, 3.0f);
        c->labelSize = 9.5; c->cap = GOLD;
        c = add({Control::Knob, kComp, {774, 260}, 24, @"COMP", ticks(0, 2, 6), 0, 10, 0}, 4.5f);
        c->labelSize = 9.5; c->cap = RED;

        // ---- OUTPUT ----
        c = add({Control::Knob, kOutput, {OUTX, 168}, 25, @"GAIN", ticks(-12, 12, 3), -12, 12, 0, dB}, 0);
        c->cap = RED; c->labelSize = 8.5;
        add({Control::LedButton, kAutoGain, {OUTX, 246}, 0.8, @"AUTO GAIN", nil, 0, 1, 1}, 1);

        // ---- EKI fiók ----
        const CGFloat dy = H + 58;
        auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };
        c = add({Control::Knob, kAnalog, {330, dy}, 22, @"ANALOG", nil, 0, 100, 60, pct}, 60); c->drawer = true; c->labelSize = 9; c->cap = RED;
        c = add({Control::Knob, kLmQ, {470, dy}, 18, @"LOW MID Q", nil, 0, 100, 50, ^NSString*(float) { return @"AUTO"; }}, 50); c->drawer = true; c->labelSize = 8.5;
        c = add({Control::Knob, kHmQ, {570, dy}, 18, @"HIGH MID Q", nil, 0, 100, 50, ^NSString*(float) { return @"AUTO"; }}, 50); c->drawer = true; c->labelSize = 8.5;
        c = add({Control::Knob, kMix, {780, dy}, 22, @"MIX", nil, 0, 100, 100, pct}, 100); c->drawer = true; c->labelSize = 9;

        // ---- render ----
        pc.backing = 1.5; pc.scale = 1;
        CGSize vs = pc.viewSize();
        const NSInteger pw = NSInteger(vs.width * pc.backing), ph = NSInteger(vs.height * pc.backing);
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:pw pixelsHigh:ph bitsPerSample:8
                                    samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        NSGraphicsContext* g = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        CGContextRef cgc = g.CGContext;
        CGContextTranslateCTM(cgc, 0, ph);
        CGContextScaleCTM(cgc, pc.backing, -pc.backing);
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:cgc flipped:YES]];
        pc.draw();
        [NSGraphicsContext restoreGraphicsState];
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        [png writeToFile:@(argv[1]) atomically:YES];
        printf("%s (%ld x %ld)\n", argv[1], long(pw), long(ph));
    }
    return 0;
}
