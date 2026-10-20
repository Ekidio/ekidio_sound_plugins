// TAPEKI GUI-terv — "ANALOG TAPE MACHINE", EKIDIO SOUND rack-stílus, a közös panel-motorral (AU nélkül).
//   clang++ -std=c++20 -fobjc-arc -DEKI_UI_FACTORY=TapekiMock -IShared -IPlugins/TAPEKI Tools/tapeki_mockup.mm -framework AppKit -framework AudioToolbox -o build/tapeki_mockup
//   build/tapeki_mockup out.png [drawer]
// Előlap: füstüveg orsó-ablak két orsóval, vezetőgörgőkkel és fejblokkal · MACHINE gombsor · VU (REC LEVEL) · WOW & FLUTTER,
//         NOISE · nagy REC LEVEL gomb piros REC LED-del · SPEED gombsor · név + alcím · AUTO GAIN + POWER;
//         fiók: BIAS, HEAD BUMP, MIX, LEVEL OUT.
#include "TapekiArt.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {
enum : AudioUnitParameterID { kRecLevel, kSpeed, kMachine, kWow, kNoise, kBias, kBump, kMix, kLevelOut, kAutoGain };
using namespace tapeki_art;

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{330, @"tape formulation / bias"}, {450, @"low-end head bump"}, {570, @"parallel blend"}, {690, @"final level (AUTO GAIN off)"}});
    textFont(@"TAPEKI", @"Futura-Bold", CGPointMake(NAME_X, 84), 23, rgb(INK), 3);
    textFont(@"ANALOG TAPE", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 106), 6.5, rgb(INK_DIM), 1.6);
    textFont(@"MACHINE", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 116), 6.5, rgb(INK_DIM), 1.6);
    line(CGPointMake(NAME_X - 62, 128), CGPointMake(NAME_X + 62, 128), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X - 62, 128), CGPointMake(NAME_X - 62, 123), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X + 62, 128), CGPointMake(NAME_X + 62, 123), 1, rgb(INK, 0.7));
    bracket(NSMinX(kWin), NSMaxX(kWin), 302, @"MACHINE");
    bracket(NSMinX(kVU), NSMaxX(kVU), 302, @"TAPE");
    bracket(REC_X - 64, REC_X + 64, 302, @"SPEED  ·  IPS");
}

void drawFace(PanelController& pc) {
    textFont(@"Default", @"AvenirNext-DemiBold", CGPointMake(CGRectGetMidX(kPreset), 18), 10.5, rgb(0xEDE3CF), 1.2);
    const float sp = getenv("SPIN") ? float(atof(getenv("SPIN"))) : 18.0f;
    drawWindow(sp, sp * 1.45f + 40, 0.75, int(std::lround(pc.get(kMachine))));
    NSString* const vuCodes[3] = {@"UT-1562", @"BL-43T", @"MX-C90"};   // a VU-n a gép szalagkódja
    VUStyle v = dialStyle(vuCodes[std::clamp(int(std::lround(pc.get(kMachine))), 0, 2)], false, 1); v.legendSize = 12; v.subLegend = @"REC LEVEL";   // fehér számlap (a user kérése)
    drawVU(kVU, -2.0f, v);
    drawLed(CGPointMake(NAME_X, 290), 0.9f, 0x46D35A, nil, 4.5);
    textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 305), 6.5, rgb(INK_DIM), 2);
}

}

int main(int argc, char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        const bool drawer = argc > 2 && strcmp(argv[2], "drawer") == 0;
        Theme t;
        t.silk = INK; t.tick = INK; t.accent = VALUE; t.led = RED;
        t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF4F1EA;
        t.buttonTop = 0x3A3B40; t.buttonBot = 0x111214; t.buttonText = 0xEDEDED;
        t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
        t.knob = Machined;
        theme() = t;

        PanelController pc(nullptr, CGSizeMake(W, H), "tapeki-mockup");
        pc.drawerHeight = DRAWER; pc.drawerOpen = drawer;
        pc.drawStatic = drawStatic; pc.drawFace = drawFace;
        auto add = [&](Control c, float v) { c.value = v; pc.controls.push_back(c); return &pc.controls.back(); };
        Control* c;
        auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
        auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

        // MACHINE gombsor az orsó-ablak alatt
        c = add({Control::RadioRow, kMachine, {NSMidX(kWin), 278}, 1.1, nil, @[@"STUDIO", @"MASTER", @"CASSETTE"], 0, 2, 0}, getenv("MACHINE") ? float(atoi(getenv("MACHINE"))) : 0.0f);
        c->rowWiden = 1.9;
        // WOW & FLUTTER, NOISE a VU alatt
        c = add({Control::Knob, kWow, {NSMinX(kVU) + 50, 236}, 20, @"WOW & FLUTTER", ticks(0, 2, 6), 0, 10, 2}, 2); c->labelSize = 8.5; c->cap = RED;
        c = add({Control::Knob, kNoise, {NSMaxX(kVU) - 50, 236}, 20, @"NOISE", ticks(0, 2, 6), 0, 10, 2}, 2); c->labelSize = 8.5; c->cap = RED;
        // REC LEVEL: nagy gomb
        c = add({Control::Knob, kRecLevel, {REC_X, 152}, 48, @"REC LEVEL", ticks(0, 1, 11), 0, 10, 5}, 5);
        c->knobStyle = Fluted; c->labelSize = 11;
        // SPEED gombsor
        c = add({Control::RadioRow, kSpeed, {REC_X, 278}, 1.1, nil, @[@"7.5", @"15", @"30"], 0, 2, 1}, 1);
        c->rowWiden = 1.2;
        // AUTO GAIN
        add({Control::LedButton, kAutoGain, {NAME_X, 222}, 0.8, @"AUTO GAIN", nil, 0, 1, 1}, 1);

        // EKI fiók: BIAS, HEAD BUMP, MIX, LEVEL OUT
        const CGFloat dy = H + 58;
        c = add({Control::SteppedKnob, kBias, {330, dy}, 20, @"BIAS", @[@"UNDER", @"NORM", @"OVER"], 0, 2, 1}, 1); c->drawer = true; c->labelSize = 9; c->cap = RED; c->spread = 70;
        c = add({Control::Knob, kBump, {450, dy}, 22, @"HEAD BUMP", ticks(0, 2, 6), 0, 10, 5}, 5); c->drawer = true; c->labelSize = 9; c->cap = RED;
        c = add({Control::Knob, kMix, {570, dy}, 22, @"MIX", nil, 0, 100, 100, pct}, 100); c->drawer = true; c->labelSize = 9; c->cap = RED;
        c = add({Control::Knob, kLevelOut, {690, dy}, 22, @"LEVEL OUT", ticks(-12, 12, 3), -12, 12, 0, dB}, 0); c->drawer = true; c->labelSize = 9; c->cap = RED;

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
