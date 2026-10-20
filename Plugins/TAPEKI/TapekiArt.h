// TapekiArt.h — a TAPEKI előlap rajzai (orsó-ablak, orsók, kazetta, zárójel), közösen a panelnek és a GUI-tervnek.
// Gépenként más látvány: STUDIO ezüst orsók + barna szalag, MASTER fekete eloxált precíziós orsók + fekete szalag,
// CASSETTE füstös "metal" kazetta; borostyán szalagkód-kijelző + PLAY lámpa; gépenként más színű háttérfény.
#pragma once
#include "EKIRack.h"

namespace tapeki_art {

using namespace eki::ui;
using namespace eki::ui::rack;



// meleg "pezsgő" alumínium előlap (a 70-es évek stúdiógépei), sötét gravírozás, piros felvétel-jelzés
constexpr int INK = 0x2B241B, INK_DIM = 0x5E5444, RED = 0xD8452E, VALUE = 0x8A2A1A;
const Look LOOK = {
    0xDCD2BC, 0xCBBFA3, 0xB5A788, 1.0,
    {0xE6E8EB, 0x8E9196, 0x4C5055, 0x72767B, 0xC2C5C9},
    0xC8BB9D, 0xD0C3A6, 0xBAAC8E, 1.0,
    INK, INK_DIM, 0.32, 0.30,
};
const NSRect kWin = NSMakeRect(26, 54, 384, 200);   // orsó-ablak
const NSRect kVU = NSMakeRect(432, 56, 192, 124);
constexpr CGFloat NAME_X = 862, REC_X = 724;
constexpr CGFloat REEL_Y = 142, REEL_R = 70;
const CGPoint kReelL = {120, REEL_Y}, kReelR = {316, REEL_Y};

/// gépenként más orsó és szalag (a forgási tempó is gépenként más — lásd kSpin)
struct ReelLook {
    int fl[4];          // tárcsa színátmenet
    int holes;          // 0 = három nagy kivágás (stúdió-alumínium), 1 = hat kerek lyuk (eloxált mastering-orsó)
    int packTop, packBot, tape;
    bool nab;           // nagy, alumínium NAB-agy (mastering)
};
const ReelLook kReels[2] = {
    {{0xF2F3F5, 0xB9BCC2, 0xE4E6EA, 0x9A9EA5}, 0, 0x5A3A22, 0x2E1C10, 0x6B4426, false},   // STUDIO: ezüst orsó, barna szalag
    {{0x6A6F78, 0x2B2E33, 0x55595F, 0x1C1E21}, 1, 0x4C4744, 0x121010, 0x6A625B, true},    // MASTER: sötét eloxált orsó, fekete hátú szalag
};
/// forgási tempó gépenként (a SPEED-del szorozva): a nagy mastering-orsó lassabban, a kazetta kis agya sokkal gyorsabban forog
constexpr float kSpin[3] = {1.0f, 0.8f, 2.4f};

/// orsó: tárcsa kivágásokkal, rajta a szalagtekercs (packR), középen agy; angle: elforgatás (fok)
void drawReel(CGPoint c, CGFloat R, CGFloat packR, CGFloat angle, const ReelLook& L) {
    // szalagtekercs (a tárcsa mögött, a kivágásokon át látszik), finom fénycsíkkal
    fillOval(c, packR, rgb(L.packTop), rgb(L.packBot));
    for (CGFloat r = 24; r < packR; r += 2.2) {
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)];
        ring.lineWidth = 0.4; [rgb(0x000000, 0.18) setStroke]; [ring stroke];
    }
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.10) endingColor:rgb(0xFFFFFF, 0.0)]
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, packR)] relativeCenterPosition:NSMakePoint(-0.4, -0.5)];
    { NSBezierPath* rim = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, packR - 0.8)]; rim.lineWidth = 1.4; [rgb(0xFFFFFF, 0.28) setStroke]; [rim stroke]; }
    NSBezierPath* flange = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, R)];
    if (L.holes == 0) {
        for (int k = 0; k < 3; ++k) {
            const CGFloat a0 = angle + k * 120 - 34, a1 = angle + k * 120 + 34;
            NSBezierPath* hole = [NSBezierPath bezierPath];
            [hole appendBezierPathWithArcWithCenter:c radius:R * 0.84 startAngle:a0 endAngle:a1];
            [hole appendBezierPathWithArcWithCenter:c radius:R * 0.36 startAngle:a1 endAngle:a0 clockwise:YES];
            [hole closePath];
            [flange appendBezierPath:hole];
        }
    } else {
        for (int k = 0; k < 6; ++k)
            [flange appendBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(polar(c, deg(angle + k * 60), R * 0.66), R * 0.17)]];
    }
    flange.windingRule = NSWindingRuleEvenOdd;
    shadowed(2, 6, 0.6, ^{
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(L.fl[0]), 0.0, rgb(L.fl[1]), 0.45, rgb(L.fl[2]), 0.7, rgb(L.fl[3]), 1.0, nil]
            drawInBezierPath:flange angle:-60];
    });
    // csillanás a tárcsán
    {
    CGContextSaveGState(cg()); [flange addClip];
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xFFFFFF, 0.45), 0.0, rgb(0xFFFFFF, 0.0), 0.55, nil]
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, R)] relativeCenterPosition:NSMakePoint(-0.55, -0.6)];
    CGContextRestoreGState(cg());
    }
    [rgb(0x000000, 0.35) setStroke]; flange.lineWidth = 0.7; [flange stroke];
    if (L.holes == 0)
        for (int k = 0; k < 3; ++k) fillOval(polar(c, deg(angle + 60 + k * 120), R * 0.6), 2.2, rgb(0xD9DCE0), rgb(0x7E838A));
    // agy
    if (L.nab) {
        fillOval(c, R * 0.36, rgb(0xEEF0F3), rgb(0x8A8F96));
        for (int k = 0; k < 3; ++k) {
            const CGPoint q = polar(c, deg(angle + 30 + k * 120), R * 0.24);
            fillOval(q, R * 0.07, rgb(0x2A2C30), rgb(0x0C0D0F));
        }
        fillOval(c, R * 0.1, rgb(0x2A2C30), rgb(0x0C0D0F));
    } else {
        fillOval(c, R * 0.24, rgb(0xE8EAEE), rgb(0x8C9097));
        fillOval(c, R * 0.12, rgb(0x2A2C30), rgb(0x0C0D0F));
        for (int k = 0; k < 3; ++k) line(polar(c, deg(angle + k * 120), R * 0.12), polar(c, deg(angle + k * 120), R * 0.2), 2.2, rgb(0x2A2C30));
    }
}

/// MASTER: precíziós mastering-orsó — fekete eloxált tárcsa, polírozott (gyémántvágott) peremmel és kivágás-élekkel,
/// finom esztergált körökkel, krómozott NAB-aggyal; rajta fényes, fekete hátú szalag tükröződéssel
void drawMasterReel(CGPoint c, CGFloat R, CGFloat packR, CGFloat angle) {
    // fényes fekete szalagtekercs
    fillOval(c, packR, rgb(0x2C2A28), rgb(0x050404));
    for (CGFloat r = 26; r < packR; r += 3.0) {
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)];
        ring.lineWidth = 0.35; [rgb(0xFFFFFF, 0.05) setStroke]; [ring stroke];
    }
    { // tükröződő ív a tekercsen
        CGContextSaveGState(cg()); [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, packR)] addClip];
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xFFFFFF, 0.0), 0.0, rgb(0xFFFFFF, 0.22), 0.48, rgb(0xFFFFFF, 0.0), 0.56, nil]
            drawInRect:circleRect(c, packR) angle:-35];
        CGContextRestoreGState(cg());
        NSBezierPath* rim = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, packR - 0.6)]; rim.lineWidth = 1.0; [rgb(0xFFFFFF, 0.22) setStroke]; [rim stroke];
    }
    // tárcsa: három karcsú kivágás
    NSBezierPath* flange = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, R)];
    NSMutableArray* holes = [NSMutableArray array];
    for (int k = 0; k < 3; ++k) {
        const CGFloat a0 = angle + k * 120 - 27, a1 = angle + k * 120 + 27;
        NSBezierPath* hole = [NSBezierPath bezierPath];
        [hole appendBezierPathWithArcWithCenter:c radius:R * 0.86 startAngle:a0 endAngle:a1];
        [hole appendBezierPathWithArcWithCenter:c radius:R * 0.40 startAngle:a1 endAngle:a0 clockwise:YES];
        [hole closePath];
        [flange appendBezierPath:hole]; [holes addObject:hole];
    }
    flange.windingRule = NSWindingRuleEvenOdd;
    shadowed(3, 8, 0.75, ^{
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x4A4E55), 0.0, rgb(0x1A1C1F), 0.4, rgb(0x34373C), 0.7, rgb(0x0E0F11), 1.0, nil]
            drawInBezierPath:flange angle:-60];
    });
    CGContextSaveGState(cg()); [flange addClip];
    // esztergált körök + csillanás
    for (CGFloat r = R * 0.42; r < R; r += 1.6) {
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)];
        ring.lineWidth = 0.3; [rgb(0xFFFFFF, 0.035) setStroke]; [ring stroke];
    }
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xFFFFFF, 0.28), 0.0, rgb(0xFFFFFF, 0.0), 0.5, nil]
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, R)] relativeCenterPosition:NSMakePoint(-0.55, -0.6)];
    CGContextRestoreGState(cg());
    // gyémántvágott, polírozott élek
    for (NSBezierPath* h in holes) { h.lineWidth = 1.1; [rgb(0xDCE0E6, 0.85) setStroke]; [h stroke]; }
    NSBezierPath* edge = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, R - 1.2)];
    edge.lineWidth = 2.2;
    [rgb(0xE6E9EE, 0.9) setStroke]; [edge stroke];
    NSBezierPath* outer = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, R)]; outer.lineWidth = 0.8; [rgb(0x000000, 0.6) setStroke]; [outer stroke];
    // krómozott NAB-agy három kulccsal, rézgyűrűvel
    shadowed(1, 3, 0.6, ^{ fillOval(c, R * 0.34, rgb(0xFAFBFC), rgb(0x7E848C)); });
    NSBezierPath* brass = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, R * 0.30)]; brass.lineWidth = 1.4; [rgb(0xD9A85A) setStroke]; [brass stroke];
    for (int k = 0; k < 3; ++k) {
        const CGPoint q = polar(c, deg(angle + 30 + k * 120), R * 0.2);
        fillOval(q, R * 0.055, rgb(0x2A2C30), rgb(0x08090A));
    }
    fillOval(c, R * 0.09, rgb(0x3A3D42), rgb(0x08090A));
    fillOval(CGPointMake(c.x - R * 0.12, c.y - R * 0.14), R * 0.06, rgb(0xFFFFFF, 0.55), rgb(0xFFFFFF, 0.0));
}

/// kazetta: füstszürke ház, krém címke, középen ablak a két kis aggyal és a szalaggal; alul a fejnyílások
/// kazetta (prémium "metal" szalag): füstös, áttetsző ház látszó belső görgőkkel, matt fekete címke arany fóliázással,
/// fényes szalagtekercsek fehér, fogas agyakkal; alul a nyomófilc és a fejnyílások
void drawCassette(CGFloat angL, CGFloat angR, CGFloat frac) {
    const NSRect shell = NSMakeRect(NSMidX(kWin) - 150, 88, 300, 160);
    NSBezierPath* sh = [NSBezierPath bezierPathWithRoundedRect:shell xRadius:9 yRadius:9];
    shadowed(3, 10, 0.8, ^{ [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x3C3F46), 0.0, rgb(0x1E2024), 0.55, rgb(0x2A2C31), 1.0, nil]
                                drawInBezierPath:sh angle:-90]; });
    CGContextSaveGState(cg()); [sh addClip];
    // áttetsző ház: a belső görgők, csavarházak és tengelyek halványan átlátszanak
    for (CGPoint q : {CGPointMake(NSMinX(shell) + 46, NSMaxY(shell) - 26), CGPointMake(NSMaxX(shell) - 46, NSMaxY(shell) - 26)})
        fillOval(q, 7, rgb(0xFFFFFF, 0.18), rgb(0xFFFFFF, 0.04));
    for (CGFloat y = NSMinY(shell) + 3; y < NSMaxY(shell); y += 3) line(CGPointMake(NSMinX(shell), y), CGPointMake(NSMaxX(shell), y), 0.3, rgb(0xFFFFFF, 0.025));
    CGContextRestoreGState(cg());
    [rgb(0xFFFFFF, 0.22) setStroke]; sh.lineWidth = 0.9; [sh stroke];
    // címke: matt fekete, arany fóliázás, vékony aranykeret
    const NSRect lab = NSInsetRect(NSMakeRect(NSMinX(shell), NSMinY(shell), NSWidth(shell), 104), 14, 10);
    NSBezierPath* lb = [NSBezierPath bezierPathWithRoundedRect:lab xRadius:4 yRadius:4];
    [[[NSGradient alloc] initWithStartingColor:rgb(0x1C1D21) endingColor:rgb(0x0B0C0E)] drawInBezierPath:lb angle:-90];
    const int GOLD = 0xD9B26A;
    NSBezierPath* fr = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(lab, 3, 3) xRadius:3 yRadius:3]; fr.lineWidth = 0.7; [rgb(GOLD, 0.8) setStroke]; [fr stroke];
    {
        NSShadow* g = [NSShadow new]; g.shadowColor = rgb(GOLD, 0.6); g.shadowBlurRadius = 4; g.shadowOffset = NSZeroSize;
        [NSGraphicsContext saveGraphicsState]; [g set];
        textFont(@"TAPEKI", @"Futura-Bold", CGPointMake(NSMinX(lab) + 46, NSMinY(lab) + 14), 10, rgb(0xF0D08A), 3);
        [NSGraphicsContext restoreGraphicsState];
    }
    textFont(@"METAL  ·  90", @"AvenirNext-DemiBold", CGPointMake(NSMaxX(lab) - 46, NSMinY(lab) + 14), 7.5, rgb(GOLD), 2);
    line(CGPointMake(NSMinX(lab) + 10, NSMinY(lab) + 25), CGPointMake(NSMaxX(lab) - 10, NSMinY(lab) + 25), 0.6, rgb(GOLD, 0.7));
    // TYPE IV jelvény
    const NSRect badge = NSMakeRect(NSMaxX(lab) - 40, NSMaxY(lab) - 20, 30, 12);
    [[[NSGradient alloc] initWithStartingColor:rgb(0xF0D08A) endingColor:rgb(0xB8893E)] drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:badge xRadius:2 yRadius:2] angle:-90];
    textFont(@"IV", @"Futura-Bold", CGPointMake(NSMidX(badge), NSMidY(badge)), 7.5, rgb(0x1A1408), 1);
    // ablak az agyakkal
    const NSRect w = NSMakeRect(NSMidX(shell) - 78, NSMinY(lab) + 34, 156, 40);
    NSBezierPath* wp = [NSBezierPath bezierPathWithRoundedRect:w xRadius:7 yRadius:7];
    [[[NSGradient alloc] initWithStartingColor:rgb(0x24201C) endingColor:rgb(0x0A0908)] drawInBezierPath:wp angle:-90];
    CGContextSaveGState(cg()); [wp addClip];
    const CGPoint hl = {NSMinX(w) + 36, NSMidY(w)}, hr = {NSMaxX(w) - 36, NSMidY(w)};
    const double prL = std::sqrt(144.0 + frac * (961.0 - 144.0)), prR = std::sqrt(144.0 + (1 - frac) * (961.0 - 144.0));   // a szalag mennyisége szerint
    for (auto [h, pr] : {std::pair{hl, prL}, std::pair{hr, prR}}) {
        fillOval(h, pr, rgb(0x4A372A), rgb(0x150E09));
        for (CGFloat r = 12; r < pr; r += 2.4) { NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:circleRect(h, r)]; ring.lineWidth = 0.3; [rgb(0xFFFFFF, 0.06) setStroke]; [ring stroke]; }
        NSBezierPath* rim = [NSBezierPath bezierPathWithOvalInRect:circleRect(h, pr - 0.6)]; rim.lineWidth = 0.9; [rgb(0xFFE2B8, 0.30) setStroke]; [rim stroke];
    }
    for (auto [hb, spin] : {std::pair{hl, angL}, std::pair{hr, angR}}) {
        const CGPoint h = hb;   // a blokk nem foghat be strukturált kötést
        shadowed(1, 2, 0.6, ^{ fillOval(h, 10, rgb(0xFBFAF6), rgb(0xC9C3B5)); });
        for (int k = 0; k < 6; ++k) line(polar(h, deg(spin + k * 60), 4.6), polar(h, deg(spin + k * 60), 8.2), 2.0, rgb(0x5E584D));
        fillOval(h, 3.8, rgb(0x1A1A1C), rgb(0x050506));
    }
    // ablak üvegcsillanása
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.16) endingColor:rgb(0xFFFFFF, 0.0)] drawInRect:NSMakeRect(NSMinX(w), NSMinY(w), NSWidth(w), 14) angle:-90];
    CGContextRestoreGState(cg());
    [rgb(0xFFFFFF, 0.25) setStroke]; wp.lineWidth = 0.8; [wp stroke];
    // alsó rész: trapéz, króm fejnyílás-keretek, nyomófilc, látszó fényes szalag
    NSBezierPath* bot = [NSBezierPath bezierPath];
    [bot moveToPoint:NSMakePoint(NSMinX(shell) + 48, NSMaxY(shell))];
    [bot lineToPoint:NSMakePoint(NSMinX(shell) + 64, NSMaxY(shell) - 32)];
    [bot lineToPoint:NSMakePoint(NSMaxX(shell) - 64, NSMaxY(shell) - 32)];
    [bot lineToPoint:NSMakePoint(NSMaxX(shell) - 48, NSMaxY(shell))];
    [bot closePath];
    [[[NSGradient alloc] initWithStartingColor:rgb(0x34373D) endingColor:rgb(0x15161A)] drawInBezierPath:bot angle:-90];
    [rgb(0xFFFFFF, 0.18) setStroke]; bot.lineWidth = 0.8; [bot stroke];
    line(CGPointMake(NSMinX(shell) + 64, NSMaxY(shell) - 5), CGPointMake(NSMaxX(shell) - 64, NSMaxY(shell) - 5), 2.2, rgb(0x6A4A34));
    line(CGPointMake(NSMinX(shell) + 64, NSMaxY(shell) - 5.8), CGPointMake(NSMaxX(shell) - 64, NSMaxY(shell) - 5.8), 0.6, rgb(0xFFE2B8, 0.45));
    for (CGFloat x : {NSMidX(shell) - 62, NSMidX(shell) + 38}) {
        const NSRect o = NSMakeRect(x, NSMaxY(shell) - 22, 24, 12);
        [[[NSGradient alloc] initWithStartingColor:rgb(0x050506) endingColor:rgb(0x1E1F22)] drawInRect:o angle:-90];
        NSBezierPath* ob = [NSBezierPath bezierPathWithRect:o]; ob.lineWidth = 0.8; [rgb(0xD8DCE2, 0.7) setStroke]; [ob stroke];
    }
    // nyomófilc középen
    const NSRect pad = NSMakeRect(NSMidX(shell) - 12, NSMaxY(shell) - 22, 24, 12);
    [[[NSGradient alloc] initWithStartingColor:rgb(0x0A0A0B) endingColor:rgb(0x1E1F22)] drawInRect:pad angle:-90];
    [[[NSGradient alloc] initWithStartingColor:rgb(0xB08850) endingColor:rgb(0x7A5A2E)] drawInRect:NSMakeRect(NSMidX(pad) - 6, NSMaxY(pad) - 6, 12, 5) angle:-90];
    // króm keresztcsavarok
    for (CGPoint sc : {CGPointMake(NSMinX(shell) + 10, NSMinY(shell) + 10), CGPointMake(NSMaxX(shell) - 10, NSMinY(shell) + 10),
                       CGPointMake(NSMinX(shell) + 10, NSMaxY(shell) - 10), CGPointMake(NSMaxX(shell) - 10, NSMaxY(shell) - 10), CGPointMake(NSMidX(shell), NSMaxY(shell) - 28)}) {
        fillOval(sc, 3.0, rgb(0xF2F4F7), rgb(0x6E737A));
        line(CGPointMake(sc.x - 1.8, sc.y), CGPointMake(sc.x + 1.8, sc.y), 0.7, rgb(0x2A2C30));
        line(CGPointMake(sc.x, sc.y - 1.8), CGPointMake(sc.x, sc.y + 1.8), 0.7, rgb(0x2A2C30));
    }
    // a ház üvegszerű csillanása
    NSBezierPath* gl = [NSBezierPath bezierPath];
    [gl moveToPoint:NSMakePoint(NSMinX(shell) + 20, NSMinY(shell))]; [gl lineToPoint:NSMakePoint(NSMinX(shell) + 120, NSMinY(shell))];
    [gl lineToPoint:NSMakePoint(NSMinX(shell) + 60, NSMaxY(shell))]; [gl lineToPoint:NSMakePoint(NSMinX(shell) - 40, NSMaxY(shell))]; [gl closePath];
    CGContextSaveGState(cg()); [sh addClip];
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.09) endingColor:rgb(0xFFFFFF, 0.0)] drawInBezierPath:gl angle:0];
    CGContextRestoreGState(cg());
}

/// vezetőgörgő
void drawGuide(CGPoint c, CGFloat r) {
    shadowed(1, 3, 0.5, ^{ fillOval(c, r, rgb(0xF0F1F3), rgb(0x8F949B)); });
    fillOval(c, r * 0.35, rgb(0x3A3C40), rgb(0x111214));
}

/// angL/angR: az orsók szöge (fok), frac: a szalag hányada a bal orsón (0…1), machine: 0/1/2
void drawWindow(CGFloat angL, CGFloat angR, CGFloat frac, int machine) {
    // füstüveg ablak
    NSBezierPath* win = [NSBezierPath bezierPathWithRoundedRect:kWin xRadius:10 yRadius:10];
    shadowed(1, 6, 0.55, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x1E1A16) endingColor:rgb(0x0A0908)] drawInBezierPath:win angle:-90]; });
    CGContextSaveGState(cg());
    [win addClip];
    // gépenként más színű háttérfény a gép belsejéből (STUDIO borostyán, MASTER hideg kék, CASSETTE neon-pink)
    const int glowCol[3] = {0xFFA640, 0x4FB4FF, 0xC86BFF};
    const CGFloat glowA[3] = {0.34, 0.28, 0.24};
    for (CGPoint gc : {kReelL, kReelR, CGPointMake(NSMidX(kWin), NSMaxY(kWin) - 10)})
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(glowCol[machine], glowA[machine]), 0.0, rgb(glowCol[machine], glowA[machine] * 0.3), 0.5, rgb(0x000000, 0.0), 1.0, nil]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(gc, 110)] relativeCenterPosition:NSZeroPoint];
    // alsó fénycsík (mint egy megvilágított szalagpálya)
    [[[NSGradient alloc] initWithStartingColor:rgb(glowCol[machine], 0.0) endingColor:rgb(glowCol[machine], 0.35)]
        drawInRect:NSMakeRect(NSMinX(kWin), NSMaxY(kWin) - 6, NSWidth(kWin), 6) angle:-90];
    if (machine == 2) {   // kazetta
        drawCassette(angL, angR, frac);
    } else {
    const ReelLook& RL = kReels[machine];
    const CGFloat packL = std::sqrt(576.0 + frac * (3844.0 - 576.0)), packR = std::sqrt(576.0 + (1 - frac) * (3844.0 - 576.0));   // tekercssugár a szalag mennyisége szerint
    // fejblokk + vezetőgörgők + szalagút
    const CGPoint gL = {168, 230}, gR = {268, 230};
    const NSRect heads = NSMakeRect(188, 220, 60, 22);
    NSColor* tape = rgb(RL.tape);
    line(CGPointMake(kReelL.x - 6, REEL_Y + packL), CGPointMake(gL.x - 6, gL.y + 5), 2.2, tape);
    line(CGPointMake(gL.x, gL.y + 6), CGPointMake(gR.x, gR.y + 6), 2.2, tape);
    line(CGPointMake(gR.x + 6, gR.y + 5), CGPointMake(kReelR.x + 6, REEL_Y + packR), 2.2, tape);
    if (machine == 1) { drawMasterReel(kReelL, REEL_R, packL, angL); drawMasterReel(kReelR, REEL_R, packR, angR); }
    else { drawReel(kReelL, REEL_R, packL, angL, RL); drawReel(kReelR, REEL_R, packR, angR, RL); }
    drawGuide(gL, 8); drawGuide(gR, 8);
    // fejblokk: törlő / felvevő / lejátszó fej (króm), pajzzsal
    shadowed(1, 3, 0.5, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x55585E) endingColor:rgb(0x1E2023)]
                               drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:heads xRadius:3 yRadius:3] angle:-90]; });
    for (int k = 0; k < 3; ++k) {
        const NSRect h = NSMakeRect(NSMinX(heads) + 6 + k * 18, NSMinY(heads) + 3, 12, 10);
        [[[NSGradient alloc] initWithStartingColor:rgb(0xF4F5F7) endingColor:rgb(0x8B9097)] drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:h xRadius:4 yRadius:4] angle:-90];
        line(CGPointMake(NSMidX(h), NSMinY(h) + 1), CGPointMake(NSMidX(h), NSMaxY(h) - 1), 0.6, rgb(0x000000, 0.6));
    }
    const char* hn[] = {"E", "R", "P"};
    for (int k = 0; k < 3; ++k) textFont(@(hn[k]), @"AvenirNext-DemiBold", CGPointMake(NSMinX(heads) + 12 + k * 18, NSMaxY(heads) - 4), 5.5, rgb(0xC9C2B4), 0.5);
    }
    // üvegcsillanás
    NSBezierPath* glare = [NSBezierPath bezierPath];
    [glare moveToPoint:NSMakePoint(NSMinX(kWin), NSMinY(kWin))];
    [glare lineToPoint:NSMakePoint(NSMinX(kWin) + 150, NSMinY(kWin))];
    [glare lineToPoint:NSMakePoint(NSMinX(kWin) + 40, NSMaxY(kWin))];
    [glare lineToPoint:NSMakePoint(NSMinX(kWin), NSMaxY(kWin))];
    [glare closePath];
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.07) endingColor:rgb(0xFFFFFF, 0.0)] drawInBezierPath:glare angle:0];
    // világító szalagkód-kijelző + PLAY lámpa középen fent (gépenként saját, kitalált szalagkód)
    {
    NSString* const codes[3] = {@"UT-1562", @"BL-43T", @"MX-C90"};
    NSString* code = codes[machine];
    const NSRect cnt = NSMakeRect(NSMidX(kWin) - 52, 62, 104, 22);
    shadowed(1, 2, 0.6, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x070605) endingColor:rgb(0x1A1410)] drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:cnt xRadius:3 yRadius:3] angle:-90]; });
    {
        NSShadow* glow = [NSShadow new]; glow.shadowColor = rgb(0xFF9A2A, 0.95); glow.shadowBlurRadius = 7; glow.shadowOffset = NSZeroSize;
        [NSGraphicsContext saveGraphicsState]; [glow set];
        textFont(code, @"Menlo-Bold", CGPointMake(NSMidX(cnt), NSMidY(cnt)), 14, rgb(0xFFC060), 2);
        [NSGraphicsContext restoreGraphicsState];
        textFont([@"8888888" substringToIndex:code.length], @"Menlo-Bold", CGPointMake(NSMidX(cnt), NSMidY(cnt)), 14, rgb(0xFFC060, 0.07), 2);   // a kikapcsolt szegmensek halvány nyoma
    }
    // orsóknál a két orsó között a kód alatt; kazettánál a kód mellett (a ház fölött)
    const CGPoint lp = machine == 2 ? CGPointMake(NSMinX(cnt) - 22, NSMidY(cnt) - 4) : CGPointMake(NSMidX(cnt), NSMaxY(cnt) + 14);
    drawLed(lp, 0.95f, 0x46E05A, nil, 4.0);
    textFont(@"PLAY", @"AvenirNext-DemiBold", CGPointMake(lp.x, lp.y + 12), 5.5, rgb(0xCFC6B4), 1.4);
    }
    CGContextRestoreGState(cg());
    [rgb(0x000000, 0.6) setStroke]; win.lineWidth = 1; [win stroke];
    // keret-csavarok
    for (CGPoint s : {CGPointMake(NSMinX(kWin) + 10, NSMinY(kWin) + 10), CGPointMake(NSMaxX(kWin) - 10, NSMinY(kWin) + 10),
                      CGPointMake(NSMinX(kWin) + 10, NSMaxY(kWin) - 10), CGPointMake(NSMaxX(kWin) - 10, NSMaxY(kWin) - 10)})
        fillOval(s, 2.6, rgb(0xC9CCD1), rgb(0x5E636A));
}

/// szekció-zárójel a feliratok alatt (mint a vason: ┗━ NAME ━┛)
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

} // namespace tapeki_art
