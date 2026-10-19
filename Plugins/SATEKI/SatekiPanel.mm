// SATEKI saját panel — a jóváhagyott GUI-irány (Tools/sateki_mockup.mm) a 2. koncepcióra igazítva:
//   mély brit-zöld szálcsiszolt előlap, krém felirat, sárgaréz jelölések; balra pajzs-VU (a telítés mértéke) és alatta MIX,
//   alatta MIX; középen három cső (az eredeti, első cső-ikon) (LOW / MID / HIGH egység), mindegyik alatt a saját hullámos DRIVE gombja — a cső a saját
//   DRIVE-jával izzik; jobbra a név, AUTO GAIN és POWER; fiókban a három FREQ (sávkezdet), LEVEL IN, LEVEL OUT.
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kLowDrive, kMidDrive, kHighDrive, kLowFreq, kMidFreq, kHighFreq, kLevelIn, kLevelOut, kMix, kAutoGain,
                              kSatVU, kAutoMakeup };

constexpr int INK = 0xF1EAD6, INK_DIM = 0xBFC9B4, COPPER = 0xE2B04A;
const Look LOOK = {
    0x2E4A3D, 0x253D32, 0x1C3027, 0.9,
    {0xE6E8EB, 0x8E9196, 0x4C5055, 0x72767B, 0xC2C5C9},
    0x223A2F, 0x274034, 0x1E342A, 1.0,
    INK, INK_DIM, 0.14, 0.32,
};
const char* kUnits[] = {"LOW", "MID", "HIGH"};
const int kUnitHz[3][4] = {{5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16}};
constexpr CGFloat TUBE_X0 = 330, TUBE_DX = 160, TUBE_W = 56, TUBE_Y = 58, TUBE_H = 94;
const NSRect kVUWin = NSMakeRect(26, 56, 192, 124);
constexpr CGFloat NAME_X = 862;
NSRect tubeRect(int i) { return NSMakeRect(TUBE_X0 + TUBE_DX * i - TUBE_W / 2, TUBE_Y, TUBE_W, TUBE_H); }

void drawTubeKind(NSRect r, int kind, float glow) {
    const int glowCol[4] = {0xFF9A3A, 0xFFC84A, 0xFF5A2A, 0xFF8A3A};
    const int hazeCol[4] = {-1, -1, 0x5A8CFF, 0xB070FF};
    NSBezierPath* win = [NSBezierPath bezierPathWithRoundedRect:r xRadius:6 yRadius:6];
    shadowed(1, 4, 0.6, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x14100C) endingColor:rgb(0x070504)] drawInBezierPath:win angle:-90]; });
    CGContextSaveGState(cg());
    [win addClip];
    const CGFloat cx = NSMidX(r);
    // háttér-izzás a cső színében
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(glowCol[kind], 0.75 * glow), 0.0, rgb(glowCol[kind], 0.25 * glow), 0.45,
                                                    rgb(0x000000, 0.0), 1.0, nil]
        drawInBezierPath:[NSBezierPath bezierPathWithRect:r] relativeCenterPosition:NSMakePoint(0, 0.05)];
    // üvegbúra (típusonként más forma)
    NSBezierPath* glass = [NSBezierPath bezierPath];
    CGFloat gTop, gBot, gw;
    if (kind <= 1) {            // előfok-cső: karcsú, rövidebb
        gw = NSWidth(r) * 0.52; gTop = NSMinY(r) + 14; gBot = NSMaxY(r) - 18;
        glass = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(cx - gw / 2, gTop, gw, gBot - gTop) xRadius:gw / 2 yRadius:gw / 2.2];
    } else if (kind == 2) {     // EL34: magas, egyenes henger
        gw = NSWidth(r) * 0.64; gTop = NSMinY(r) + 6; gBot = NSMaxY(r) - 14;
        glass = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(cx - gw / 2, gTop, gw, gBot - gTop) xRadius:gw / 2 yRadius:gw / 3];
    } else {                    // 6L6: "kólásüveg" (öblös alj, szűkülő nyak)
        gw = NSWidth(r) * 0.74; gTop = NSMinY(r) + 6; gBot = NSMaxY(r) - 14;
        const CGFloat neck = gw * 0.55, shoulder = gTop + (gBot - gTop) * 0.42;
        [glass moveToPoint:NSMakePoint(cx - neck / 2, gTop + neck / 2)];
        [glass appendBezierPathWithArcWithCenter:NSMakePoint(cx, gTop + neck / 2) radius:neck / 2 startAngle:180 endAngle:0 clockwise:NO];
        [glass curveToPoint:NSMakePoint(cx + gw / 2, shoulder + 14) controlPoint1:NSMakePoint(cx + neck / 2, shoulder - 6) controlPoint2:NSMakePoint(cx + gw / 2, shoulder)];
        [glass lineToPoint:NSMakePoint(cx + gw / 2, gBot - 6)];
        [glass curveToPoint:NSMakePoint(cx - gw / 2, gBot - 6) controlPoint1:NSMakePoint(cx + gw / 2, gBot + 2) controlPoint2:NSMakePoint(cx - gw / 2, gBot + 2)];
        [glass lineToPoint:NSMakePoint(cx - gw / 2, shoulder + 14)];
        [glass curveToPoint:NSMakePoint(cx - neck / 2, gTop + neck / 2) controlPoint1:NSMakePoint(cx - gw / 2, shoulder) controlPoint2:NSMakePoint(cx - neck / 2, shoulder - 6)];
        [glass closePath];
    }
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.08) endingColor:rgb(0xFFFFFF, 0.02)] drawInBezierPath:glass angle:0];
    // getter (ezüst tükör a tetején); a 12AT7-nél gyűrű
    if (kind == 1) {
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cx - gw * 0.34, gTop + 3, gw * 0.68, 6)];
        ring.lineWidth = 1.4; [rgb(0xD8DCE2, 0.85) setStroke]; [ring stroke];
    } else {
        [[[NSGradient alloc] initWithStartingColor:rgb(0xE2E6EC, 0.8) endingColor:rgb(0x6C7076, 0.5)]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cx - gw * 0.3, gTop + 3, gw * 0.6, kind >= 2 ? 7 : 5)] angle:-90];
    }
    // belső szerkezet
    const CGFloat y0 = gTop + (gBot - gTop) * (kind <= 1 ? 0.30 : 0.36), y1 = gBot - (gBot - gTop) * 0.16;
    auto heater = [&](NSRect fil, CGFloat bloom) {
        [[rgb(0x5A1C06) blendedColorWithFraction:glow ofColor:[rgb(glowCol[kind]) blendedColorWithFraction:0.5 ofColor:NSColor.whiteColor]] setFill];
        [[NSBezierPath bezierPathWithRect:fil] fill];
        [[[NSGradient alloc] initWithStartingColor:rgb(glowCol[kind], 0.7 * glow) endingColor:rgb(glowCol[kind], 0.0)]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(fil, -bloom, -3)] relativeCenterPosition:NSZeroPoint];
    };
    if (kind <= 1) {            // két kis anódpár (kettős trióda)
        for (int k = 0; k < 2; ++k) {
            const NSRect pl = NSMakeRect(cx - gw * 0.36 + k * gw * 0.4, y0, gw * 0.32, y1 - y0);
            [[[NSGradient alloc] initWithStartingColor:rgb(0x8A8E94) endingColor:rgb(0x45484D)] drawInRect:pl angle:0];
            heater(NSMakeRect(NSMidX(pl) - 0.8, NSMinY(pl) + 3, 1.6, NSHeight(pl) - 6), 5);
        }
        if (kind == 1) line(CGPointMake(cx - gw * 0.44, (y0 + y1) / 2), CGPointMake(cx + gw * 0.44, (y0 + y1) / 2), 1.0, rgb(0xCFC6B8, 0.7));
    } else {                    // végcső: egy nagy, bordázott anód, mögötte izzó katód
        const NSRect pl = NSMakeRect(cx - gw * 0.34, y0, gw * 0.68, y1 - y0);
        [[[NSGradient alloc] initWithStartingColor:rgb(0x7E8288) endingColor:rgb(0x3A3D42)] drawInRect:pl angle:0];
        for (int k = 1; k < 4; ++k) line(CGPointMake(NSMinX(pl) + NSWidth(pl) * k / 4, NSMinY(pl) + 2), CGPointMake(NSMinX(pl) + NSWidth(pl) * k / 4, NSMaxY(pl) - 2), 0.8, rgb(0x2A2C30, 0.7));
        heater(NSMakeRect(cx - 1.2, NSMinY(pl) - 4, 2.4, 6), 7);
        heater(NSMakeRect(cx - 1.2, NSMaxY(pl) - 2, 2.4, 6), 7);
        // jellegzetes ködfény a lemez szélén (EL34: kék, 6L6: lilás)
        if (hazeCol[kind] >= 0)
            for (CGFloat ex : {NSMinX(pl) - 1, NSMaxX(pl) + 1})
                [[[NSGradient alloc] initWithStartingColor:rgb(hazeCol[kind], 0.55 * glow) endingColor:rgb(hazeCol[kind], 0.0)]
                    drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(ex - 5, NSMinY(pl), 10, NSHeight(pl))] relativeCenterPosition:NSZeroPoint];
    }
    // üvegkontúr + csillanás + foglalat
    [rgb(0xFFE6C0, 0.40) setStroke]; glass.lineWidth = 0.9; [glass stroke];
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.25) endingColor:rgb(0xFFFFFF, 0.0)]
        drawInRect:NSMakeRect(cx - gw / 2 + 3, gTop + 10, 3.5, gBot - gTop - 20) angle:0];
    [[[NSGradient alloc] initWithStartingColor:rgb(0x2A2B2E) endingColor:rgb(0x0E0E10)] drawInRect:NSMakeRect(cx - gw / 2 - 1, gBot - 1, gw + 2, 9) angle:-90];
    CGContextRestoreGState(cg());
    [rgb(0x000000, 0.7) setStroke]; win.lineWidth = 1; [win stroke];
}

/// szekció-zárójel a feliratok alatt (mint a vason: ┗━ LOW BAND ━┛)
void bracket(CGFloat x0, CGFloat x1, CGFloat y, NSString* name) {
    line(CGPointMake(x0, y - 5), CGPointMake(x0, y), 1, rgb(INK, 0.7));
    line(CGPointMake(x1, y - 5), CGPointMake(x1, y), 1, rgb(INK, 0.7));
    NSFont* f = [NSFont fontWithName:@"AvenirNext-DemiBold" size:8];
    const CGFloat tw = [name sizeWithAttributes:@{NSFontAttributeName: f, NSKernAttributeName: @2}].width + 14;
    const CGFloat m = (x0 + x1) / 2;
    line(CGPointMake(x0, y), CGPointMake(m - tw / 2, y), 1, rgb(INK, 0.7));
    line(CGPointMake(m + tw / 2, y), CGPointMake(x1, y), 1, rgb(INK, 0.7));
    textFont(name, @"AvenirNext-DemiBold", CGPointMake(m, y), 8, rgb(INK), 2);
}

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{330, @"where each tube's band starts"}, {610, @"input drive into the tubes"}, {730, @"final level (AUTO GAIN off)"}});
    // név + alcím jobbra, díszvonallal
    textFont(@"SATEKI", @"Futura-Bold", CGPointMake(NAME_X, 84), 23, rgb(INK), 3);
    textFont(@"MULTI TUBE", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 106), 6.5, rgb(INK_DIM), 1.6);
    textFont(@"SATURATION", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 116), 6.5, rgb(INK_DIM), 1.6);
    line(CGPointMake(NAME_X - 62, 128), CGPointMake(NAME_X + 62, 128), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X - 62, 128), CGPointMake(NAME_X - 62, 123), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X + 62, 128), CGPointMake(NAME_X + 62, 123), 1, rgb(INK, 0.7));
    // csősor-keret: sötét betét, mint egy nyitott ház
    NSRect bank = NSMakeRect(TUBE_X0 - 74, TUBE_Y - 8, TUBE_DX * 2 + 148, TUBE_H + 34);
    NSBezierPath* b = [NSBezierPath bezierPathWithRoundedRect:bank xRadius:8 yRadius:8];
    shadowed(1, 5, 0.6, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x1C1612) endingColor:rgb(0x0E0B09)] drawInBezierPath:b angle:-90]; });
    [rgb(0xFFFFFF, 0.08) setStroke]; b.lineWidth = 0.8; [b stroke];
    bracket(TUBE_X0 - 74, TUBE_X0 + TUBE_DX * 2 + 74, 300, @"SOFT AIR  ·  DRIVE");
}

void drawFace(PanelController& pc) {
    drawHeaderDynamic(pc);
    for (int i = 0; i < 3; ++i) {
        const NSRect r = tubeRect(i);
        const float drive = pc.get(kLowDrive + i) / 10.0f;
        drawTubeKind(r, 0, 0.08f + 0.92f * drive);   // mindhárom az eredeti, első cső-ikon (karcsú, narancs izzás); a saját DRIVE-jával izzik
        const int f = std::clamp(int(std::lround(pc.get(kLowFreq + i))), 0, 3);
        textFont([NSString stringWithFormat:@"%s  %d kHz", kUnits[i], kUnitHz[i][f]], @"AvenirNext-DemiBold",
                 CGPointMake(NSMidX(r), TUBE_Y + TUBE_H + 13), 8.5, rgb(drive > 0.001f ? COPPER : INK_DIM), 1.4);
    }
    VUStyle v = dialStyle(@"SATEKI"); v.legendSize = 14; v.subLegend = @"AIR";
    drawVU(kVUWin, pc.get(kSatVU), v);
    drawLed(CGPointMake(NAME_X, 290), pc.processing ? 0.9f : 0.0f, 0x46D35A, nil, 4.5);   // a plugin be/ki állapotát követi
    textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 305), 6.5, rgb(INK_DIM), 2);
}

std::unique_ptr<PanelController> buildSatekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = 0xE6E0CC; t.accent = 0xFFD27A; t.led = 0xFFB040;
    t.minorTicks = 4; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF6F0E6;
    t.buttonTop = 0x2C3A33; t.buttonBot = 0x0F1612; t.buttonText = 0xF0EAD8;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Machined; t.coloredLine = true;
    theme() = t;
    auto pc = makePanel(au, "sateki");
    pc->watched = {{kSatVU, -20}, {kAutoMakeup, 0}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawFace;
    auto& cs = pc->controls;
    auto dB = ^NSString*(float v) { return [NSString stringWithFormat:@"%+.1f dB", v]; };
    auto pct = ^NSString*(float v) { return [NSString stringWithFormat:@"%.0f %%", v]; };

    // három DRIVE: hullámos gomb a saját cső alatt
    const float defDrive[3] = {5, 5, 5};
    for (int i = 0; i < 3; ++i) {
        cs.push_back({Control::Knob, AudioUnitParameterID(kLowDrive + i), {TUBE_X0 + TUBE_DX * i, 238}, 29,
                      [NSString stringWithFormat:@"%s DRIVE", kUnits[i]], ticks(0, 1, 11), 0, 10, defDrive[i]});
        cs.back().knobStyle = Fluted; cs.back().labelSize = 9.5;
    }
    // MIX a VU alatt
    cs.push_back({Control::Knob, kMix, {122, 238}, 24, @"MIX", ticks(0, 10, 11), 0, 100, 60, pct});
    cs.back().cap = COPPER; cs.back().labelSize = 8.5;
    // AUTO GAIN
    cs.push_back({Control::LedButton, kAutoGain, {NAME_X, 222}, 0.8, @"AUTO GAIN", nil, 0, 1, 1});

    // EKI fiók: LOW / MID / HIGH FREQ (sávkezdet), LEVEL IN, LEVEL OUT
    const CGFloat dy = H + 58;
    const int defFreq[3] = {1, 1, 3};
    for (int i = 0; i < 3; ++i) {
        NSMutableArray* names = [NSMutableArray array];
        for (int k = 0; k < 4; ++k) [names addObject:[NSString stringWithFormat:@"%dk", kUnitHz[i][k]]];
        cs.push_back({Control::SteppedKnob, AudioUnitParameterID(kLowFreq + i), {CGFloat(230 + 100 * i), dy}, 20,
                      [NSString stringWithFormat:@"%s FREQ", kUnits[i]], names, 0, 3, float(defFreq[i])});
        cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = COPPER; cs.back().spread = 100;
    }
    cs.push_back({Control::Knob, kLevelIn, {610, dy}, 22, @"LEVEL IN", ticks(-12, 12, 3), -12, 12, 12, dB});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = COPPER;
    cs.push_back({Control::Knob, kLevelOut, {730, dy}, 22, @"LEVEL OUT", ticks(-12, 12, 3), -12, 12, 0, dB});
    cs.back().drawer = true; cs.back().labelSize = 9; cs.back().cap = COPPER;

    pc->lock(kLevelOut, kAutoGain);   // AUTO GAIN alatt a kimenet 0 dB-en, nem tekerhető
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildSatekiPanel)
