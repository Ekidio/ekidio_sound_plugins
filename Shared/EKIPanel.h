// EKIPanel.h — közös AUv2 Cocoa panel-motor az EKIDIO SOUND pluginokhoz (csak .mm fájlból, ARC-cal).
//
// A plugin egy PanelController-t épít (vezérlők + előlap-rajzoló) egy kompakt ALAPMÉRETRE, az
// EKI_DEFINE_PANEL_VIEW makró pedig legyártja a buildenként egyedi nevű NSView + AUCocoaUIBase
// osztályokat. A panel a jobb alsó sarokból húzva méretezhető (75…200%, dupla katt = 100%), a méretet
// megjegyzi (NSUserDefaults). Minden rajz vektoros (CoreGraphics/AppKit), a nézet "flipped" (y lefelé).
#pragma once
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#import <CoreText/CoreText.h>
#include "EKIUpdate.h"
#include <dlfcn.h>
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <cmath>

namespace eki::ui {

// MARK: - Téma, színek, segédek

inline NSColor* rgb(int hex, CGFloat a = 1) {
    return [NSColor colorWithSRGBRed:((hex >> 16) & 255) / 255.0 green:((hex >> 8) & 255) / 255.0 blue:(hex & 255) / 255.0 alpha:a];
}

enum KnobStyle { Bakelite, SilverSkirt, Console, ChromeRing, Premium, Machined, ConsoleCap, Fluted, Pointer };

/// Plugin-téma (bundle-önként egy példány; a panel build-függvénye állítja be)
struct Theme {
    int silk = 0xE9DFC6;                              // szitanyomás
    int accent = 0xF0B04A;                            // értékkijelzés
    int led = 0xF0B04A;                               // LED-ek, világító gombok
    int skirtTop = 0x34343A, skirtBot = 0x060606;     // gomb szoknya
    int capTop = 0x48484E, capBot = 0x111114;         // gomb sapka
    int pointer = 0xE9DFC6;                           // mutatóvonal
    int buttonTop = 0x4A4B50, buttonBot = 0x1A1B1E;   // kikapcsolt nyomógomb
    int buttonText = -1;                              // nyomógomb felirat (-1 = silk)
    KnobStyle knob = Bakelite;
    NSString* fontRegular = @"Futura-Medium";       // feliratok betűtípusa
    NSString* fontBold = @"Futura-Bold";
    int tick = -1;                                    // skálavonások / számok színe (-1 = silk)
    int minorTicks = 0;                               // finom osztások két főosztás között (nagy gomboknál)
    int ledStyle = 0;                                 // 0: szögletes LED-gomb, 1: kerek fém gomb világító gyűrűvel
    bool valueGlow = false;                           // az értékkijelzés halványan izzik
    bool coloredLine = false;                         // esztergált gomb: izzó pont helyett színes mutatóvonal + vékony színes kupakperem
    bool dotTicks = false;                            // skála: vonások helyett pöttyök (vintage hardver-előlap)
    int arc = -1;                                     // esztergált gomb: világító érték-ív a peremen (-1 = nincs), pl. neonkék
};
inline Theme& theme() { static Theme t; return t; }

inline NSColor* silk() { return rgb(theme().silk); }
inline NSColor* silkDim() { return rgb(theme().silk, 0.5); }
inline NSColor* accent() { return rgb(theme().accent); }
inline NSColor* tickColor() { return theme().tick >= 0 ? rgb(theme().tick) : silk(); }

/// tetszőleges betűtípussal (pl. talpas cím)
inline void textFont(NSString* s, NSString* fontName, CGPoint center, CGFloat size, NSColor* col, CGFloat kern) {
    NSFont* f = [NSFont fontWithName:fontName size:size] ?: [NSFont systemFontOfSize:size];
    NSDictionary* a = @{NSFontAttributeName: f, NSForegroundColorAttributeName: col, NSKernAttributeName: @(kern)};
    NSSize sz = [s sizeWithAttributes:a];
    [s drawAtPoint:NSMakePoint(center.x - sz.width / 2 + kern / 2, center.y - sz.height / 2) withAttributes:a];
}

inline CGFloat deg(CGFloat d) { return d * M_PI / 180.0; }
/// pont a középponttól `d` távolságra, `a` szögben (0 = fel, óramutató szerint) — flipped koordinátákban
inline CGPoint polar(CGPoint c, CGFloat a, CGFloat d) { return CGPointMake(c.x + std::sin(a) * d, c.y - std::cos(a) * d); }
inline NSRect circleRect(CGPoint c, CGFloat r) { return NSMakeRect(c.x - r, c.y - r, 2 * r, 2 * r); }
inline CGContextRef cg() { return NSGraphicsContext.currentContext.CGContext; }

inline NSFont* font(CGFloat size, bool bold = false) {
    NSFont* f = [NSFont fontWithName:(bold ? theme().fontBold : theme().fontRegular) size:size];
    return f ?: [NSFont systemFontOfSize:size weight:(bold ? NSFontWeightBold : NSFontWeightMedium)];
}

inline void text(NSString* s, CGPoint center, CGFloat size, NSColor* col, bool bold = false, CGFloat kern = 0.6) {
    if (!s) return;
    NSDictionary* a = @{NSFontAttributeName: font(size, bold), NSForegroundColorAttributeName: col, NSKernAttributeName: @(kern)};
    NSSize sz = [s sizeWithAttributes:a];
    [s drawAtPoint:NSMakePoint(center.x - sz.width / 2 + kern / 2, center.y - sz.height / 2) withAttributes:a];
}

inline void line(CGPoint a, CGPoint b, CGFloat w, NSColor* col) {
    NSBezierPath* p = [NSBezierPath bezierPath];
    [p moveToPoint:a]; [p lineToPoint:b];
    p.lineWidth = w; p.lineCapStyle = NSLineCapStyleRound;
    [col setStroke]; [p stroke];
}

inline void fillOval(CGPoint c, CGFloat r, NSColor* top, NSColor* bottom, CGFloat angle = -90) {
    [[[NSGradient alloc] initWithStartingColor:top endingColor:bottom] drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] angle:angle];
}

inline void shadowed(CGFloat dy, CGFloat blur, CGFloat alpha, void (^draw)(void)) {
    CGContextSaveGState(cg());
    CGContextSetShadowWithColor(cg(), CGSizeMake(0, -dy), blur, rgb(0, alpha).CGColor);
    draw();
    CGContextRestoreGState(cg());
}

// MARK: - Gomb

inline CGFloat tickLen(CGFloat r) { return std::max(3.0, r * 0.16); }
inline CGFloat tickFont(CGFloat r) { return r >= 30 ? 9.5 : 7.5; }

/// Forgatógomb skálával. norm 0…1 → −sweep…+sweep fok. tickLabels: skálafeliratok (nil = csak vonások)
inline void drawKnob(CGPoint c, CGFloat r, float norm, NSArray<NSString*>* tickLabels, CGFloat sweep = 135, int capColor = -1, int ticks = 11, int capTint = -1, int rimColor = -1, bool bipolar = false) {
    const Theme& t = theme();
    const int n = tickLabels ? int(tickLabels.count) : ticks;
    const CGFloat tl = tickLen(r);
    for (int i = 0; i < n && n > 1; ++i) {
        CGFloat a = deg(-sweep + 2 * sweep * i / (n - 1));
        if (t.dotTicks) { [tickColor() setFill]; [[NSBezierPath bezierPathWithOvalInRect:circleRect(polar(c, a, r + 3 + tl * 0.5), r >= 30 ? 1.5 : 1.15)] fill]; }
        else line(polar(c, a, r + 3), polar(c, a, r + 3 + tl), r >= 30 ? 1.4 : 1.0, tickColor());
        if (tickLabels) text(tickLabels[i], polar(c, a, r + 3 + tl + tickFont(r) * 0.95), tickFont(r), tickColor(), false, 0.2);
        if (r >= 30 && i < n - 1)
            for (int m = 1; m <= t.minorTicks; ++m) {
                CGFloat am = deg(-sweep + 2 * sweep * (i + m / CGFloat(t.minorTicks + 1)) / (n - 1));
                line(polar(c, am, r + 3), polar(c, am, r + 3 + tl * 0.5), 0.8, [tickColor() colorWithAlphaComponent:0.7]);
            }
    }
    if (t.knob == Pointer) {   // "kakasfej" mutatós gomb (vintage hardver): kerek agy + elnyújtott, lekerekített mutató, festett műanyag
        const int col = capColor >= 0 ? capColor : t.capTop;
        const CGFloat a = deg(-sweep + 2 * sweep * std::clamp(norm, 0.0f, 1.0f)), ca = std::cos(a), sa = std::sin(a);
        auto P = [&](CGFloat x, CGFloat y) { return NSMakePoint(c.x + x * ca - y * sa, c.y + x * sa + y * ca); };   // helyi (fel = −y) → nézet
        // egybefüggő, lapos test: hegyes orr, széles váll, lekerekített farok (a forgástengely a váll közepén)
        NSBezierPath* body = [NSBezierPath bezierPath];
        [body moveToPoint:P(0, -r * 1.04)];
        [body curveToPoint:P(r * 0.50, r * 0.02) controlPoint1:P(r * 0.10, -r * 0.92) controlPoint2:P(r * 0.50, -r * 0.40)];
        [body curveToPoint:P(0, r * 0.66) controlPoint1:P(r * 0.50, r * 0.42) controlPoint2:P(r * 0.30, r * 0.66)];
        [body curveToPoint:P(-r * 0.50, r * 0.02) controlPoint1:P(-r * 0.30, r * 0.66) controlPoint2:P(-r * 0.50, r * 0.42)];
        [body curveToPoint:P(0, -r * 1.04) controlPoint1:P(-r * 0.50, -r * 0.40) controlPoint2:P(-r * 0.10, -r * 0.92)];
        [body closePath];
        shadowed(r * 0.14, r * 0.35, 0.75, ^{ [rgb(col) setFill]; [body fill]; });
        [[[NSGradient alloc] initWithColorsAndLocations:[rgb(col) blendedColorWithFraction:0.30 ofColor:NSColor.whiteColor], 0.0, rgb(col), 0.5,
                                                        [rgb(col) blendedColorWithFraction:0.45 ofColor:NSColor.blackColor], 1.0, nil]
            drawInBezierPath:body relativeCenterPosition:NSMakePoint(-0.3, 0.35)];
        // a test teteje: lapos gerinc (fényes él a hossztengely mentén) és finom domborulat
        CGContextSaveGState(cg()); [body addClip];
        NSBezierPath* ridge = [NSBezierPath bezierPath];
        [ridge moveToPoint:P(-r * 0.16, -r * 0.70)]; [ridge lineToPoint:P(r * 0.16, -r * 0.70)]; [ridge lineToPoint:P(r * 0.26, r * 0.40)]; [ridge lineToPoint:P(-r * 0.26, r * 0.40)]; [ridge closePath];
        [rgb(0xFFFFFF, 0.10) setFill]; [ridge fill];
        [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.30) endingColor:rgb(0xFFFFFF, 0.0)]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - r * 0.55, c.y - r * 0.75, r * 0.8, r * 0.6)] angle:-90];
        CGContextRestoreGState(cg());
        [rgb(0x000000, 0.5) setStroke]; body.lineWidth = 0.8; [body stroke];
        // mutató: fehér háromszög a csúcs felé (sötét gombon), krém vonal (színes gombon)
        const bool dark = ((col >> 16 & 255) + (col >> 8 & 255) + (col & 255)) < 240;
        if (dark) {
            NSBezierPath* tri = [NSBezierPath bezierPath];
            [tri moveToPoint:P(0, -r * 0.94)]; [tri lineToPoint:P(r * 0.14, -r * 0.52)]; [tri lineToPoint:P(-r * 0.14, -r * 0.52)]; [tri closePath];
            [rgb(0xF4F1EA) setFill]; [tri fill];
        } else line(P(0, -r * 0.48), P(0, -r * 0.92), std::max(1.4, r * 0.07), rgb(0xFFF6E8, 0.95));
        return;
    }
    if (t.knob == ConsoleCap) {   // konzolgomb: sima világosszürke test, fényes színes kupak, fehér mutatóvonal
        shadowed(r * 0.16, r * 0.45, 0.8, ^{ [rgb(0x101112) setFill]; [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] fill]; });
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xE6E9ED), 0.0, rgb(0xA9ADB3), 0.45, rgb(0x5E6268), 0.85, rgb(0x3A3D42), 1.0, nil]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] relativeCenterPosition:NSMakePoint(-0.35, 0.45)];
        NSBezierPath* ridge = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 0.9)];
        ridge.lineWidth = std::max(0.8, r * 0.03); [rgb(0x000000, 0.25) setStroke]; [ridge stroke];
        const CGFloat capR = r * 0.68;
        const int cc = capColor >= 0 ? capColor : t.capTop;
        shadowed(r * 0.04, r * 0.08, 0.6, ^{
            [[[NSGradient alloc] initWithStartingColor:[rgb(cc) blendedColorWithFraction:0.3 ofColor:NSColor.whiteColor]
                                           endingColor:[rgb(cc) blendedColorWithFraction:0.35 ofColor:NSColor.blackColor]]
                drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, capR)] relativeCenterPosition:NSMakePoint(-0.3, 0.4)];
        });
        [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.45) endingColor:rgb(0xFFFFFF, 0.0)]   // fényes csillanás a kupakon
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - capR * 0.7, c.y - capR * 0.85, capR * 0.95, capR * 0.6)] angle:-90];
        [rgb(0x000000, 0.45) setStroke];
        NSBezierPath* rim = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, capR)]; rim.lineWidth = 0.8; [rim stroke];
        CGFloat a = deg(-sweep + 2 * sweep * std::clamp(norm, 0.0f, 1.0f));
        line(polar(c, a, capR * 0.15), polar(c, a, r * 0.94), std::max(1.6, r * 0.065), rgb(0xFFFFFF));
        return;
    }
    if (t.knob == Fluted) {   // nagy, hullámos szélű ("csillag") fekete gomb fényes kupolával és fehér háromszög-jelölővel (vintage rack)
        const CGFloat a = deg(-sweep + 2 * sweep * std::clamp(norm, 0.0f, 1.0f));
        // skálagyűrű: két vékony körív a vonások két szélén
        for (CGFloat rr : {r + 3, r + 3 + tl}) {
            NSBezierPath* arc = [NSBezierPath bezierPath];
            [arc appendBezierPathWithArcWithCenter:c radius:rr startAngle:(-sweep - 90) endAngle:(sweep - 90) clockwise:NO];
            arc.lineWidth = 0.7; [[tickColor() colorWithAlphaComponent:0.55] setStroke]; [arc stroke];
        }
        // hullámos test (8 karéj), a gombbal együtt forog
        constexpr int kLobes = 8;
        NSBezierPath* body = [NSBezierPath bezierPath];
        for (int i = 0; i <= 240; ++i) {
            const CGFloat th = 2 * M_PI * i / 240;
            const CGFloat rad = r * (0.90 + 0.10 * std::pow(std::fabs(std::cos(kLobes * (th - a) / 2)), 0.55));
            const CGPoint q = polar(c, th, rad);
            if (i == 0) [body moveToPoint:q]; else [body lineToPoint:q];
        }
        [body closePath];
        shadowed(r * 0.16, r * 0.42, 0.85, ^{ [rgb(0x050505) setFill]; [body fill]; });
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x3C3D42), 0.0, rgb(0x16171A), 0.5, rgb(0x050506), 1.0, nil]
            drawInBezierPath:body relativeCenterPosition:NSMakePoint(-0.3, 0.45)];
        body.lineWidth = 1; [rgb(0x000000, 0.9) setStroke]; [body stroke];
        // karéjok fénye (bal-fent világosabb)
        for (int k = 0; k < kLobes; ++k) {
            const CGFloat la = a + 2 * M_PI * k / kLobes;
            const CGFloat lit = 0.5 + 0.5 * std::cos(la + deg(45));
            line(polar(c, la, r * 0.80), polar(c, la, r * 0.97), std::max(1.0, r * 0.05), rgb(0xFFFFFF, 0.04 + 0.12 * lit));
        }
        // kúpos váll + fényes kupola
        NSBezierPath* shoulder = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 0.76)];
        [[[NSGradient alloc] initWithStartingColor:rgb(0x2A2B2F) endingColor:rgb(0x060607)] drawInBezierPath:shoulder angle:-90];
        NSBezierPath* dome = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 0.6)];
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x4A4B52), 0.0, rgb(0x1B1C1F), 0.55, rgb(0x0A0A0B), 1.0, nil]
            drawInBezierPath:dome relativeCenterPosition:NSMakePoint(-0.3, 0.45)];
        CGContextSaveGState(cg());
        [dome addClip];
        [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.28) endingColor:rgb(0xFFFFFF, 0.0)]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(polar(c, deg(-40), r * 0.3), r * 0.34)] relativeCenterPosition:NSZeroPoint];
        CGContextRestoreGState(cg());
        [rgb(0xFFFFFF, 0.10) setStroke]; dome.lineWidth = 0.8; [dome stroke];
        // fehér háromszög-jelölő a vállon
        NSBezierPath* tri = [NSBezierPath bezierPath];
        [tri moveToPoint:polar(c, a, r * 0.86)];
        [tri lineToPoint:polar(c, a - 0.13, r * 0.66)];
        [tri lineToPoint:polar(c, a + 0.13, r * 0.66)];
        [tri closePath];
        [rgb(0xF4F2EC) setFill]; [tri fill];
        return;
    }
    if (t.knob == Machined) {   // esztergált sötét fém: finom recézés, "pörgetett" sapka csokornyakkendő-csillanással, arany mutató
        shadowed(r * 0.18, r * 0.5, 0.9, ^{ [rgb(0x050505) setFill]; [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] fill]; });
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x2C2D31), 0.0, rgb(0x121214), 0.6, rgb(0x060607), 1.0, nil]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] relativeCenterPosition:NSMakePoint(-0.25, 0.4)];
        if (rimColor >= 0)   // színezett (eloxált) karima: a recézett külső gyűrű a megadott színben
            [[[NSGradient alloc] initWithColorsAndLocations:[rgb(rimColor) blendedColorWithFraction:0.22 ofColor:NSColor.whiteColor], 0.0, rgb(rimColor), 0.45,
                                                            [rgb(rimColor) blendedColorWithFraction:0.55 ofColor:NSColor.blackColor], 1.0, nil]
                drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] relativeCenterPosition:NSMakePoint(-0.25, 0.4)];
        const int knurls = r >= 30 ? 110 : 60;
        for (int i = 0; i < knurls; ++i) {
            CGFloat a = 2 * M_PI * i / knurls;
            const CGFloat lit = 0.5 + 0.5 * std::cos(a + deg(45));
            line(polar(c, a, r * 0.8), polar(c, a, r * 0.995), std::max(0.6, r * 0.016), rgb(0xFFFFFF, (i & 1) ? 0.03 : 0.05 + 0.16 * lit));
        }
        NSBezierPath* bevel = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 0.8)];
        [[[NSGradient alloc] initWithStartingColor:rgb(0x55565C) endingColor:rgb(0x0A0A0B)] drawInBezierPath:bevel angle:-90];
        const CGFloat capR = r * 0.74;
        NSBezierPath* top = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, capR)];
        if (capTint >= 0)   // színezett (eloxált) sapka
            [[[NSGradient alloc] initWithStartingColor:[rgb(capTint) blendedColorWithFraction:0.15 ofColor:NSColor.whiteColor]
                                           endingColor:[rgb(capTint) blendedColorWithFraction:0.45 ofColor:NSColor.blackColor]]
                drawInBezierPath:top relativeCenterPosition:NSMakePoint(-0.3, 0.4)];
        else { [rgb(0x26272B) setFill]; [top fill]; }
        CGContextSaveGState(cg());
        [top addClip];
        for (int i = 0; i < 144; ++i) {          // anizotróp fény: két átellenes csillanás
            const CGFloat a0 = 2 * M_PI * i / 144, a1 = 2 * M_PI * (i + 1) / 144;
            const CGFloat k = std::pow(std::fabs(std::cos(a0 + deg(45))), 6.0);
            NSBezierPath* w = [NSBezierPath bezierPath];
            [w moveToPoint:c]; [w lineToPoint:polar(c, a0, capR + 1)]; [w lineToPoint:polar(c, a1, capR + 1)]; [w closePath];
            [rgb(0xFFFFFF, 0.02 + 0.20 * k) setFill]; [w fill];
        }
        for (CGFloat rr = 2; rr < capR; rr += 1.6)   // koncentrikus esztergálás
            { NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, rr)]; ring.lineWidth = 0.5; [rgb(int(rr * 10) % 2 ? 0xFFFFFF : 0x000000, 0.035) setStroke]; [ring stroke]; }
        CGContextRestoreGState(cg());
        top.lineWidth = 1.0; [rgb(0xFFFFFF, 0.16) setStroke]; [top stroke];   // sapkaperem
        if (t.coloredLine && capColor >= 0) {   // vékony színes gyűrű a sapka szélén
            NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, capR - std::max(1.2, r * 0.035))];
            ring.lineWidth = std::max(1.2, r * 0.045); [rgb(capColor, 0.9) setStroke]; [ring stroke];
        }
        [rgb(0x000000, 0.9) setStroke];
        NSBezierPath* rim = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r - 0.4)]; rim.lineWidth = 1; [rim stroke];
        CGFloat a = deg(-sweep + 2 * sweep * std::clamp(norm, 0.0f, 1.0f));
        if (t.arc >= 0) {   // világító érték-ív a peremen: halvány pálya + fényes ív (bipoláris gombnál középről)
            const CGFloat ar = r * 0.88, lw = std::max(2.6, r * 0.11);
            auto arcPath = [&](CGFloat a0, CGFloat a1) {   // a szög a "fel" iránytól, óramutató szerint (flipped nézet)
                NSBezierPath* p = [NSBezierPath bezierPath];
                const int n = std::max(2, int(std::fabs(a1 - a0) / 0.05));
                for (int i = 0; i <= n; ++i) { const CGPoint q = polar(c, a0 + (a1 - a0) * i / n, ar); if (i == 0) [p moveToPoint:q]; else [p lineToPoint:q]; }
                p.lineWidth = lw; p.lineCapStyle = NSLineCapStyleRound; p.lineJoinStyle = NSLineJoinStyleRound; return p;
            };
            [rgb(0x000000, 0.45) setStroke]; [arcPath(deg(-sweep), deg(sweep)) stroke];
            const CGFloat a0 = bipolar ? 0 : deg(-sweep);
            if (std::fabs(a - a0) > 0.01) {
                CGContextSaveGState(cg());
                CGContextSetShadowWithColor(cg(), CGSizeZero, lw * 2.2, rgb(t.arc, 0.85).CGColor);
                [rgb(t.arc) setStroke]; [arcPath(a0, a) stroke];
                CGContextRestoreGState(cg());
            }
        }
        if (t.coloredLine && capColor >= 0) {   // színes mutatóvonal
            line(polar(c, a, capR * 0.22), polar(c, a, capR * 0.9), std::max(2.0, r * 0.06), rgb(capColor));
        } else if (capColor >= 0 && r >= 30) {   // izzó jelzőpont (mint egy beépített LED)
            const CGPoint d = polar(c, a, capR * 0.8);
            const CGFloat dr = std::max(2.5, r * 0.065);
            [[[NSGradient alloc] initWithStartingColor:rgb(capColor, 0.45) endingColor:rgb(capColor, 0.0)]
                drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(d, dr * 2.4)] relativeCenterPosition:NSZeroPoint];
            [[[NSGradient alloc] initWithStartingColor:[rgb(capColor) blendedColorWithFraction:0.55 ofColor:NSColor.whiteColor] endingColor:rgb(capColor)]
                drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(d, dr)] relativeCenterPosition:NSMakePoint(-0.2, 0.3)];
        } else {
            line(polar(c, a, capR * 0.5), polar(c, a, capR * 0.93), std::max(1.8, r * 0.05), rgb(t.pointer));
        }
        return;
    }
    if (t.knob == Premium) {   // prémium bakelit: mély árnyék, recézett perem, kúpos oldal, fényes sapka csillanással
        shadowed(r * 0.16, r * 0.45, 0.85, ^{ [rgb(0x050505) setFill]; [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] fill]; });
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x3A3A40), 0.0, rgb(0x141416), 0.55, rgb(0x050505), 1.0, nil]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] relativeCenterPosition:NSMakePoint(-0.25, 0.4)];
        const int knurls = r >= 30 ? 72 : 40;
        for (int i = 0; i < knurls; ++i) {
            CGFloat a = 2 * M_PI * i / knurls;
            const CGFloat lit = 0.5 + 0.5 * std::cos(a + deg(45));     // fény balról-fentről
            line(polar(c, a, r * 0.87), polar(c, a, r * 0.995), std::max(0.8, r * 0.025), rgb(0xFFFFFF, 0.04 + 0.13 * lit));
        }
        NSBezierPath* side = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 0.86)];
        [[[NSGradient alloc] initWithStartingColor:rgb(0x2E2E33) endingColor:rgb(0x070708)] drawInBezierPath:side angle:-90];
        NSBezierPath* top = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 0.72)];
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x46464D), 0.0, rgb(0x1E1E22), 0.6, rgb(0x0E0E10), 1.0, nil]
            drawInBezierPath:top relativeCenterPosition:NSMakePoint(-0.3, 0.45)];
        [rgb(0xFFFFFF, 0.12) setStroke]; top.lineWidth = 0.8; [top stroke];
        CGContextSaveGState(cg());
        [top addClip];
        const CGPoint spot = polar(c, deg(-40), r * 0.38);   // lágy csillanás bal-fent
        [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.20) endingColor:rgb(0xFFFFFF, 0.0)]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(spot, r * 0.42)] relativeCenterPosition:NSZeroPoint];
        CGContextRestoreGState(cg());
        [rgb(0x000000, 0.9) setStroke];
        NSBezierPath* rim = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r - 0.4)]; rim.lineWidth = 1; [rim stroke];
        CGFloat a = deg(-sweep + 2 * sweep * std::clamp(norm, 0.0f, 1.0f));
        const CGFloat w = std::max(2.0, r * 0.07);
        line(polar(c, a, r * 0.18), polar(c, a, r * 0.68), w, rgb(t.pointer));
        line(polar(c, a, r * 0.88), polar(c, a, r * 0.99), w * 0.9, rgb(t.pointer));   // jel a peremen is
        return;
    }
    // árnyék + test
    shadowed(r * 0.1, r * 0.25, 0.75, ^{ [rgb(0x0A0A0A) setFill]; [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] fill]; });
    NSBezierPath* skirt = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)];
    CGFloat capR = r * 0.7;
    switch (t.knob) {
        case SilverSkirt:   // fekete gomb, csiszolt ezüst szoknya
            [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xF2F2F4), 0.0, rgb(0xA9AAB0), 0.5, rgb(0xE6E6EA), 0.75, rgb(0x7D7E84), 1.0, nil]
                drawInBezierPath:skirt angle:-60];
            capR = r * 0.74;
            break;
        case Console:       // szürke, enyhén kúpos szoknya
            [[[NSGradient alloc] initWithStartingColor:rgb(t.skirtTop) endingColor:rgb(t.skirtBot)] drawInBezierPath:skirt angle:-90];
            capR = r * 0.62;
            break;
        case ChromeRing:    // fekete bakelit krómgyűrűvel
        case Bakelite:
        default:
            [[[NSGradient alloc] initWithStartingColor:rgb(t.skirtTop) endingColor:rgb(t.skirtBot)] drawInBezierPath:skirt relativeCenterPosition:NSMakePoint(-0.35, 0.45)];
            break;
    }
    if (t.knob == Bakelite || t.knob == ChromeRing) {
        const int knurls = r >= 30 ? 56 : 28;
        for (int i = 0; i < knurls; ++i) {
            CGFloat a = 2 * M_PI * i / knurls;
            line(polar(c, a, r * 0.84), polar(c, a, r * 0.985), 0.9, rgb(0xFFFFFF, 0.07));
        }
    }
    if (t.knob == ChromeRing) {
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 0.8)];
        ring.lineWidth = std::max(1.5, r * 0.07);
        [rgb(0xD8DADD) setStroke]; [ring stroke];
        [rgb(0xFFFFFF, 0.5) setStroke]; ring.lineWidth = 0.6; [ring stroke];
    }
    NSBezierPath* cap = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, capR)];
    NSColor* capA = capColor >= 0 ? [rgb(capColor) blendedColorWithFraction:0.25 ofColor:NSColor.whiteColor] : rgb(t.capTop);
    NSColor* capB = capColor >= 0 ? [rgb(capColor) blendedColorWithFraction:0.45 ofColor:NSColor.blackColor] : rgb(t.capBot);
    [[[NSGradient alloc] initWithStartingColor:capA endingColor:capB] drawInBezierPath:cap angle:-90];
    [rgb(0xFFFFFF, 0.14) setStroke]; cap.lineWidth = 0.8; [cap stroke];
    [rgb(0x000000, 0.8) setStroke];
    NSBezierPath* rim = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, r - 0.4)]; rim.lineWidth = 0.8; [rim stroke];

    CGFloat a = deg(-sweep + 2 * sweep * std::clamp(norm, 0.0f, 1.0f));
    const CGFloat w = std::max(1.6, r * 0.075);
    if (t.knob == SilverSkirt) {   // mutató a szoknyán is (mint a gravírozott vonal)
        line(polar(c, a, capR * 1.02), polar(c, a, r * 0.97), w * 0.8, rgb(0x1A1A1A));
    }
    line(polar(c, a, r * 0.2), polar(c, a, capR * 0.95), w, rgb(t.pointer));
}

/// Csavarhúzós trimmer (süllyesztett, réses)
inline void drawTrimPot(CGPoint c, CGFloat r, float norm) {
    for (int i = 0; i < 11; ++i) {
        CGFloat a = deg(-135 + 27.0 * i);
        line(polar(c, a, r + 3), polar(c, a, r + 5.5), 0.9, silk());
    }
    fillOval(c, r + 1.5, rgb(0x000000, 0.45), rgb(0xFFFFFF, 0.25));
    [[[NSGradient alloc] initWithStartingColor:rgb(0xE8E8EA) endingColor:rgb(0x77787E)] drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] relativeCenterPosition:NSMakePoint(-0.3, 0.3)];
    CGFloat a = deg(-135 + 270.0 * std::clamp(norm, 0.0f, 1.0f));
    line(polar(c, a, r * 0.85), polar(c, a + M_PI, r * 0.85), r * 0.28, rgb(0x2A2A2E));
}

/// Fém karos kapcsoló: on = lefelé. s: méretszorzó
inline void drawToggle(CGPoint c, bool on, NSString* offLabel, NSString* onLabel, CGFloat s) {
    const CGFloat fs = 8.5 * std::max(0.85, s);
    text(offLabel, CGPointMake(c.x, c.y - 30 * s), fs, on ? silkDim() : silk(), !on, 0.4);
    text(onLabel, CGPointMake(c.x, c.y + 30 * s), fs, on ? silk() : silkDim(), on, 0.4);
    fillOval(c, 10 * s, rgb(0xDADADC), rgb(0x55565A));
    [[rgb(0x000000, 0.6) colorWithAlphaComponent:0.6] setStroke];
    [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, 10 * s)] stroke];
    [rgb(0x151515) setFill]; [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, 5 * s)] fill];
    CGPoint tip = CGPointMake(c.x, c.y + (on ? 18 : -18) * s);
    shadowed(3 * s, 4 * s, 0.7, ^{
        line(c, tip, 5 * s, rgb(0xC9C9CC));
        [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF) endingColor:rgb(0x8A8A90)]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(tip, 4.5 * s)] relativeCenterPosition:NSMakePoint(-0.4, 0.4)];
    });
}

/// Világító nyomógomb (LED-del), felirat alatta
inline void drawLedButton(CGPoint c, bool on, NSString* label, CGFloat s) {
    if (theme().ledStyle == 2) {   // szögletes, belülről világító nyomógomb (vintage hardver): kikapcsolva matt fehér, bekapcsolva kékesfehér fény
        const int L = theme().led;
        const CGFloat h = 15 * s;
        const NSRect bez = NSMakeRect(c.x - h - 3 * s, c.y - h - 3 * s, 2 * h + 6 * s, 2 * h + 6 * s), cap = NSMakeRect(c.x - h, c.y - h, 2 * h, 2 * h);
        shadowed(2 * s, 4 * s, 0.7, ^{ [[[NSGradient alloc] initWithStartingColor:rgb(0x2A2C30) endingColor:rgb(0x08090A)] drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:bez xRadius:3 * s yRadius:3 * s] angle:-90]; });
        NSBezierPath* face = [NSBezierPath bezierPathWithRoundedRect:cap xRadius:2 * s yRadius:2 * s];
        CGContextSaveGState(cg());
        if (on) CGContextSetShadowWithColor(cg(), CGSizeZero, 16 * s, rgb(L, 0.95).CGColor);
        [[[NSGradient alloc] initWithStartingColor:(on ? [rgb(L) blendedColorWithFraction:0.75 ofColor:NSColor.whiteColor] : rgb(0xBFC0BE))
                                       endingColor:(on ? [rgb(L) blendedColorWithFraction:0.25 ofColor:NSColor.whiteColor] : rgb(0x8E908E))] drawInBezierPath:face angle:-90];
        CGContextRestoreGState(cg());
        if (on) [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.85) endingColor:rgb(0xFFFFFF, 0.0)]
                    drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(cap, h * 0.25, h * 0.25)] relativeCenterPosition:NSZeroPoint];
        [rgb(0x000000, 0.5) setStroke]; face.lineWidth = 0.8; [face stroke];
        text(label, CGPointMake(c.x, c.y + h + 13 * s), 8.5 * std::max(0.9, s), silk(), true, 0.5);
        return;
    }
    if (theme().ledStyle == 1) {   // kerek fém nyomógomb világító gyűrűvel
        const int L = theme().led;
        const CGFloat R = 17 * s;
        shadowed(3 * s, 7 * s, 0.7, ^{ fillOval(c, R + 3 * s, rgb(0x3A3B40), rgb(0x0E0F11)); });
        fillOval(c, R + 1.5 * s, rgb(0x0A0A0B), rgb(0x2A2B2E));
        CGContextSaveGState(cg());
        if (on) CGContextSetShadowWithColor(cg(), CGSizeZero, 12 * s, rgb(L, 0.95).CGColor);
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:circleRect(c, R - 1.2 * s)];
        ring.lineWidth = 3.2 * s;
        [(on ? [rgb(L) blendedColorWithFraction:0.25 ofColor:NSColor.whiteColor] : [rgb(L) blendedColorWithFraction:0.8 ofColor:NSColor.blackColor]) setStroke];
        [ring stroke];
        CGContextRestoreGState(cg());
        const CGFloat capR = R * 0.74;
        fillOval(c, capR, rgb(0x5A5C62), rgb(0x1A1B1E));
        CGContextSaveGState(cg());
        [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, capR)] addClip];
        for (int i = 0; i < 72; ++i) {
            const CGFloat a0 = 2 * M_PI * i / 72, a1 = 2 * M_PI * (i + 1) / 72;
            const CGFloat k = std::pow(std::fabs(std::cos(a0 + deg(45))), 6.0);
            NSBezierPath* w = [NSBezierPath bezierPath];
            [w moveToPoint:c]; [w lineToPoint:polar(c, a0, capR + 1)]; [w lineToPoint:polar(c, a1, capR + 1)]; [w closePath];
            [rgb(0xFFFFFF, 0.03 + 0.22 * k) setFill]; [w fill];
        }
        CGContextRestoreGState(cg());
        [rgb(0x000000, 0.7) setStroke]; [[NSBezierPath bezierPathWithOvalInRect:circleRect(c, capR)] stroke];
        text(label, CGPointMake(c.x, c.y + R + 14 * s), 9 * std::max(0.9, s), on ? silk() : silkDim(), true, 0.8);
        return;
    }
    NSRect r = NSMakeRect(c.x - 16 * s, c.y - 10 * s, 32 * s, 20 * s);
    NSBezierPath* body = [NSBezierPath bezierPathWithRoundedRect:r xRadius:3 * s yRadius:3 * s];
    [[[NSGradient alloc] initWithStartingColor:rgb(theme().buttonTop) endingColor:rgb(theme().buttonBot)] drawInBezierPath:body angle:-90];
    [rgb(0x000000, 0.8) setStroke]; [body stroke];
    const int L = theme().led;
    if (on) [[[NSGradient alloc] initWithStartingColor:rgb(L, 0.55) endingColor:rgb(L, 0)]
                drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, 12 * s)] relativeCenterPosition:NSZeroPoint];
    [[[NSGradient alloc] initWithStartingColor:(on ? [rgb(L) blendedColorWithFraction:0.6 ofColor:NSColor.whiteColor] : [rgb(L) blendedColorWithFraction:0.7 ofColor:NSColor.blackColor])
                                   endingColor:(on ? rgb(L) : [rgb(L) blendedColorWithFraction:0.85 ofColor:NSColor.blackColor])]
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, 3.8 * s)] relativeCenterPosition:NSMakePoint(-0.3, 0.3)];
    text(label, CGPointMake(c.x, c.y + 20 * s), 8 * std::max(0.9, s), on ? silk() : silkDim(), true, 0.6);
}

/// Gombsor (egy választható), vízszintes vagy függőleges. lightAll: az utolsó ("ALL") mindet felgyújtja
inline CGPoint radioPos(CGPoint c, int i, int n, bool vertical, CGFloat s, CGFloat widen = 1) {
    const CGFloat gap = (vertical ? 24 : 34 * widen) * s;
    const CGFloat o = (i - (n - 1) / 2.0) * gap;
    return vertical ? CGPointMake(c.x, c.y + o) : CGPointMake(c.x + o, c.y);
}
inline void drawRadioRow(CGPoint c, int index, NSArray<NSString*>* labels, bool lightAll, bool vertical, CGFloat s, CGFloat widen = 1) {
    const int n = int(labels.count);
    const CGFloat w = 28 * s * widen, h = 18 * s;
    const int L = theme().led;
    for (int i = 0; i < n; ++i) {
        CGPoint bc = radioPos(c, i, n, vertical, s, widen);
        const bool lit = i == index || (lightAll && index == n - 1);
        NSBezierPath* body = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(bc.x - w / 2, bc.y - h / 2, w, h) xRadius:2.5 * s yRadius:2.5 * s];
        shadowed(lit ? 1 : 2.5, lit ? 2 : 3.5, 0.9, ^{
            [[[NSGradient alloc] initWithStartingColor:(lit ? [rgb(L) blendedColorWithFraction:0.4 ofColor:NSColor.whiteColor] : rgb(theme().buttonTop))
                                           endingColor:(lit ? [rgb(L) blendedColorWithFraction:0.3 ofColor:NSColor.blackColor] : rgb(theme().buttonBot))]
                drawInBezierPath:body angle:-90];
        });
        [rgb(0x000000, 0.7) setStroke]; [body stroke];
        if (lit) [[[NSGradient alloc] initWithStartingColor:rgb(L, 0.3) endingColor:rgb(L, 0)]
                     drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(bc, 20 * s)] relativeCenterPosition:NSZeroPoint];
        NSColor* tc = lit ? rgb(0x101010) : (theme().buttonText >= 0 ? rgb(theme().buttonText) : silk());
        text(labels[i], bc, 8.5 * std::max(0.9, s), tc, true, 0.2);
    }
}

/// Kis forgókapcsoló (pálcás mutató) N állással, a feliratok körben
inline void drawSelector(CGPoint c, CGFloat r, int index, NSArray<NSString*>* labels, CGFloat spread) {
    const int n = int(labels.count);
    const CGFloat fs = r >= 14 ? 8.5 : 7.5;
    for (int i = 0; i < n; ++i) {
        CGFloat a = deg(-spread + 2 * spread * i / std::max(1, n - 1));
        line(polar(c, a, r + 3), polar(c, a, r + 6), 1.1, silk());
        text(labels[i], polar(c, a, r + 7 + fs), fs, i == index ? silk() : silkDim(), i == index, 0.2);
    }
    CGFloat a = deg(-spread + 2 * spread * index / std::max(1, n - 1));
    NSBezierPath* bar = [NSBezierPath bezierPath];
    CGPoint tip = polar(c, a, r), back = polar(c, a + M_PI, r * 0.55);
    CGPoint s1 = polar(c, a + M_PI / 2, r * 0.24), s2 = polar(c, a - M_PI / 2, r * 0.24);
    [bar moveToPoint:tip]; [bar lineToPoint:s1]; [bar lineToPoint:back]; [bar lineToPoint:s2]; [bar closePath];
    shadowed(3, 6, 0.9, ^{
        fillOval(c, r * 0.62, rgb(0x55565C), rgb(0x0C0C0E));
        [[[NSGradient alloc] initWithStartingColor:rgb(0x4A4B51) endingColor:rgb(0x0E0E10)] drawInBezierPath:bar angle:-90];
    });
    // fényes bakelit: csillanás a pálca tetején, világos perem
    CGContextSaveGState(cg());
    [bar addClip];
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.28) endingColor:rgb(0xFFFFFF, 0.0)]
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(polar(c, deg(-45), r * 0.3), r * 0.55)] relativeCenterPosition:NSZeroPoint];
    CGContextRestoreGState(cg());
    [rgb(0xFFFFFF, 0.32) setStroke]; bar.lineWidth = 0.9; [bar stroke];
    line(polar(c, a, r * 0.25), polar(c, a, r * 0.95), 1.8, rgb(0xF2F2F2));
}

// MARK: - Beágyazott fontok

/// a bundle Contents/Resources mappájában lévő .otf/.ttf fontok regisztrálása (csak ebbe a folyamatba, egyszer)
inline void registerBundleFonts() {
    static bool done = false;
    if (done) return;
    done = true;
    Dl_info info{};
    if (!dladdr((const void*)&registerBundleFonts, &info) || !info.dli_fname) return;
    NSURL* exe = [NSURL fileURLWithPath:[NSString stringWithUTF8String:info.dli_fname]];
    NSURL* res = [[[exe URLByDeletingLastPathComponent] URLByDeletingLastPathComponent] URLByAppendingPathComponent:@"Resources"];
    for (NSURL* u in [[NSFileManager defaultManager] contentsOfDirectoryAtURL:res includingPropertiesForKeys:nil options:0 error:nil]) {
        NSString* ext = u.pathExtension.lowercaseString;
        if ([ext isEqualToString:@"otf"] || [ext isEqualToString:@"ttf"])
            CTFontManagerRegisterFontsForURL((__bridge CFURLRef)u, kCTFontManagerScopeProcess, nullptr);
    }
}

// MARK: - Mérőműszer

struct VUStyle {
    int bezel = 0;        // 0: fekete, 1: króm, 2: acél-fekete szögletes, 3: keret nélküli, borostyán háttérfénnyel
    int face = 0;         // 0: krém/borostyán, 1: fehér, 2: halvány zöldes, 3: meleg, belülről világító, 4: kék, belülről világító
    bool grScale = false; // true: GR-skála (0 jobbra, 20 balra, dB-ben lineáris)
    NSString* legend = @"VU";
    NSString* legendFont = nil;   // a számlap feliratának betűtípusa (nil = a téma félkövére)
    CGFloat legendSize = 14;      // 156 px magas műszerre vonatkoztatva
    CGFloat legendKern = 1.2;
    NSString* subLegend = nil;    // kis második sor a felirat alatt (pl. "VU")
    bool legendBox = false;       // a felirat vékony keretben (mint egy gyári logó a számlapon)
    int needleTop = -1, needleBottom = -1;   // kétszínű mutató: felső rész / alsó (tő) rész színe (-1 = alapértelmezett)
};

/// "Pajzs" forma: felül lekerekített sarkok, alul ferdén levágott sarkok (flipped koordináták)
inline NSBezierPath* shieldPath(NSRect r, CGFloat rad, CGFloat cx, CGFloat cy) {
    NSBezierPath* p = [NSBezierPath bezierPath];
    const CGFloat x0 = NSMinX(r), x1 = NSMaxX(r), y0 = NSMinY(r), y1 = NSMaxY(r), j = rad * 0.45;
    [p moveToPoint:NSMakePoint(x0 + rad, y0)];
    [p appendBezierPathWithArcFromPoint:NSMakePoint(x1, y0) toPoint:NSMakePoint(x1, y0 + rad) radius:rad];
    [p appendBezierPathWithArcFromPoint:NSMakePoint(x1, y1 - cy) toPoint:NSMakePoint(x1 - cx, y1) radius:j];
    [p appendBezierPathWithArcFromPoint:NSMakePoint(x1 - cx, y1) toPoint:NSMakePoint(x0 + cx, y1) radius:j];
    [p appendBezierPathWithArcFromPoint:NSMakePoint(x0 + cx, y1) toPoint:NSMakePoint(x0, y1 - cy) radius:j];
    [p appendBezierPathWithArcFromPoint:NSMakePoint(x0, y1 - cy) toPoint:NSMakePoint(x0, y0) radius:j];
    [p appendBezierPathWithArcFromPoint:NSMakePoint(x0, y0) toPoint:NSMakePoint(x0 + rad, y0) radius:rad];
    [p closePath];
    return p;
}

/// Analóg műszer. db: VU-skálán −20…+3, GR-skálán −20…0
inline void drawVU(NSRect rc, float db, const VUStyle& st) {
    CGContextRef ctx = cg();
    const CGFloat k = rc.size.height / 156.0;
    if (st.bezel == 3) {   // süllyesztett ablak, körülötte meleg fény
        NSBezierPath* win = [NSBezierPath bezierPathWithRoundedRect:rc xRadius:16 * k yRadius:16 * k];
        CGContextSaveGState(ctx);
        CGContextSetShadowWithColor(ctx, CGSizeZero, 26 * k, rgb(0xFFB04A, 0.45).CGColor);
        [rgb(0xE9B866) setFill]; [win fill];
        CGContextRestoreGState(ctx);
    }
    const bool shield = st.bezel == 4;
    NSBezierPath* bezel = shield ? shieldPath(rc, 16 * k, rc.size.width * 0.16, rc.size.height * 0.34)
                                 : [NSBezierPath bezierPathWithRoundedRect:rc xRadius:(st.bezel == 2 ? 3 : 8) * k yRadius:(st.bezel == 2 ? 3 : 8) * k];
    if (shield) {   // vastag sötét fém keret, felül fénycsík
        shadowed(5 * k, 12 * k, 0.75, ^{
            [[[NSGradient alloc] initWithColorsAndLocations:rgb(0x5C5F65), 0.0, rgb(0x2A2C30), 0.35, rgb(0x141518), 0.8, rgb(0x2E3034), 1.0, nil] drawInBezierPath:bezel angle:-90];
        });
        [rgb(0xFFFFFF, 0.28) setStroke]; bezel.lineWidth = 1 * k; [bezel stroke];
    }
    if (st.bezel != 3 && !shield) shadowed(3 * k, 7 * k, 0.6, ^{
        if (st.bezel == 1)
            [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xF4F4F6), 0.0, rgb(0x9A9BA0), 0.45, rgb(0xE1E2E5), 0.7, rgb(0x6E6F74), 1.0, nil] drawInBezierPath:bezel angle:-90];
        else
            [[[NSGradient alloc] initWithStartingColor:rgb(st.bezel == 2 ? 0x2A2B2E : 0x1C1D20) endingColor:rgb(0x0B0B0D)] drawInBezierPath:bezel angle:-90];
    });
    if (st.bezel != 3 && !shield) { [rgb(0xFFFFFF, st.bezel == 1 ? 0.5 : 0.08) setStroke]; [bezel stroke]; }
    const CGFloat ins = st.bezel == 3 ? 0 : shield ? 10 * k : (st.bezel == 1 ? 7 : 8) * k;
    NSRect face = NSInsetRect(rc, ins, ins);
    const CGFloat fr = st.bezel == 3 ? 16 * k : shield ? 10 * k : 4 * k;
    NSBezierPath* facePath = shield ? shieldPath(face, fr, face.size.width * 0.14, face.size.height * 0.32)
                                    : [NSBezierPath bezierPathWithRoundedRect:face xRadius:fr yRadius:fr];
    if (st.face == 4) {   // kék, belülről világító számlap + halvány kék fény a keret körül
        CGContextSaveGState(ctx);
        CGContextSetShadowWithColor(ctx, CGSizeZero, 18 * k, rgb(0x22C3F0, 0.55).CGColor);
        [rgb(0x1B9FD6) setFill]; [facePath fill];
        CGContextRestoreGState(ctx);
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xD8F7FF), 0.0, rgb(0x8FE3FB), 0.35, rgb(0x3FC1EE), 0.7, rgb(0x1586C0), 1.0, nil] drawInBezierPath:facePath relativeCenterPosition:NSMakePoint(0, 0.25)];
    } else if (st.face == 3)
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xFFF3D2), 0.0, rgb(0xF6D592), 0.55, rgb(0xD9A453), 0.85, rgb(0xA8732E), 1.0, nil] drawInBezierPath:facePath relativeCenterPosition:NSMakePoint(0, 0.25)];
    else if (st.face == 1)
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xFFFFFF), 0.0, rgb(0xF1F1EE), 0.6, rgb(0xD6D6D2), 1.0, nil] drawInBezierPath:facePath relativeCenterPosition:NSMakePoint(0, 0.35)];
    else if (st.face == 2)
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xF4F6E8), 0.0, rgb(0xE1E6CC), 0.6, rgb(0xB9C1A0), 1.0, nil] drawInBezierPath:facePath relativeCenterPosition:NSMakePoint(0, 0.35)];
    else
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xFBEFC9), 0.0, rgb(0xEBD39A), 0.6, rgb(0xC9A867), 1.0, nil] drawInBezierPath:facePath relativeCenterPosition:NSMakePoint(0, 0.35)];

    CGContextSaveGState(ctx);
    [facePath addClip];
    const CGPoint piv = CGPointMake(NSMidX(face), NSMaxY(face) + face.size.height * 0.32);
    const CGFloat R = face.size.height * 1.02;
    const CGFloat span = 39;
    const int ink = st.face == 4 ? 0x06283A : 0x1A1408, red = 0xB02A20;
    auto ang = [&](float d) {
        float pos;
        if (st.grScale) pos = 1.0f + std::clamp(d, -21.0f, 0.5f) / 20.0f;
        else pos = (std::pow(10.0f, d / 20.0f) - 0.1f) / (std::pow(10.0f, 3.0f / 20.0f) - 0.1f);
        return deg(-span + 2 * span * std::clamp(pos, -0.03f, 1.04f));
    };
    NSBezierPath* arc = [NSBezierPath bezierPath];
    const float lo = -20, hi = st.grScale ? 0 : 0;
    for (int i = 0; i <= 60; ++i) { float d = lo + (hi - lo) * i / 60; CGPoint p = polar(piv, ang(d), R); i ? [arc lineToPoint:p] : [arc moveToPoint:p]; }
    arc.lineWidth = 1.3 * k; [rgb(ink) setStroke]; [arc stroke];
    if (!st.grScale) {
        NSBezierPath* arcR = [NSBezierPath bezierPath];
        for (int i = 0; i <= 20; ++i) { float d = 3.0f * i / 20; CGPoint p = polar(piv, ang(d), R + 2 * k); i ? [arcR lineToPoint:p] : [arcR moveToPoint:p]; }
        arcR.lineWidth = 5 * k; [rgb(0xC0352A) setStroke]; [arcR stroke];
        const float marks[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
        for (float m : marks) {
            CGFloat a = ang(m);
            NSColor* col = m > 0 ? rgb(red) : rgb(ink);
            line(polar(piv, a, R), polar(piv, a, R + (m == 0 || m == -20 || m == 3 ? 11 : 8) * k), 1.3 * k, col);
            NSString* s = m > 0 ? [NSString stringWithFormat:@"+%d", int(m)] : [NSString stringWithFormat:@"%d", int(std::fabs(m))];
            text(s, polar(piv, a, R + 19 * k), 10 * k, col, m == 0, 0.2);
        }
        const CGFloat sy = shield ? NSMaxY(face) - face.size.height * 0.40 : NSMaxY(face) - 15 * k;
        text(@"−", CGPointMake(NSMinX(face) + 14 * k, sy), 13 * k, rgb(ink));
        text(@"+", CGPointMake(NSMaxX(face) - 14 * k, sy), 13 * k, rgb(red));
    } else {
        const float marks[] = {-20, -14, -10, -8, -6, -4, -2, 0};
        for (float m : marks) {
            CGFloat a = ang(m);
            line(polar(piv, a, R), polar(piv, a, R + (m == 0 || m == -20 ? 11 : 8) * k), 1.3 * k, rgb(ink));
            text([NSString stringWithFormat:@"%d", int(std::fabs(m))], polar(piv, a, R + 19 * k), 10 * k, rgb(ink), m == 0, 0.2);
        }
        for (int m = -19; m < 0; ++m) {
            CGFloat a = ang(float(m));
            line(polar(piv, a, R), polar(piv, a, R + 4 * k), 0.8 * k, rgb(ink, 0.7));
        }
    }
    const CGPoint lp = CGPointMake(piv.x, NSMaxY(face) - face.size.height * (st.subLegend ? 0.34 : 0.3));
    if (st.legendFont) textFont(st.legend, st.legendFont, lp, st.legendSize * k, rgb(ink), st.legendKern * k);
    else text(st.legend, lp, st.legendSize * k, rgb(ink), true, st.legendKern * k);
    if (st.legendBox) {
        NSFont* lf = [NSFont fontWithName:(st.legendFont ?: @"Futura-Bold") size:st.legendSize * k] ?: [NSFont systemFontOfSize:st.legendSize * k];
        const NSSize ts = [st.legend sizeWithAttributes:@{NSFontAttributeName: lf, NSKernAttributeName: @(st.legendKern * k)}];
        const NSRect br = NSMakeRect(lp.x - ts.width / 2 - 6 * k, lp.y - ts.height / 2 - 2 * k, ts.width + 12 * k, ts.height + 4 * k);
        NSBezierPath* bp = [NSBezierPath bezierPathWithRect:br]; bp.lineWidth = 1.0 * k; [rgb(ink) setStroke]; [bp stroke];
        NSBezierPath* bp2 = [NSBezierPath bezierPathWithRect:NSInsetRect(br, 2 * k, 2 * k)]; bp2.lineWidth = 0.5 * k; [bp2 stroke];
    }
    if (st.subLegend) text(st.subLegend, CGPointMake(piv.x, lp.y + st.legendSize * (st.legendBox ? 1.35 : (st.subLegend.length > 4 ? 1.08 : 0.85)) * k), 7.5 * k, rgb(ink, 0.7), true, 2 * k);
    CGFloat a = ang(db);
    CGContextSetShadowWithColor(ctx, CGSizeMake(2 * k, -3 * k), 2.5 * k, rgb(0, 0.35).CGColor);
    if (st.needleTop >= 0 || st.needleBottom >= 0) {   // kétszínű mutató: fent (a skálán) az egyik, lent (a tőnél) a másik szín
        const int top = st.needleTop >= 0 ? st.needleTop : 0x111111, bot = st.needleBottom >= 0 ? st.needleBottom : 0xB8261C;
        line(polar(piv, a, R * 0.62), polar(piv, a, R + 14 * k), 2.6 * k, rgb(0x000000, 0.55));   // vékony sötét kontúr a világos számlapon
        line(polar(piv, a, R * 0.62), polar(piv, a, R + 14 * k), 1.6 * k, rgb(top));
        line(polar(piv, a, R * 0.33), polar(piv, a, R * 0.64), 2.4 * k, rgb(bot));
    } else {
        line(polar(piv, a, R * 0.35), polar(piv, a, R + 14 * k), 1.5 * k, rgb(0x111111));
        if (shield) line(polar(piv, a, R * 0.33), polar(piv, a, R * 0.62), 2.2 * k, rgb(0xB8261C));   // piros mutatótő
    }
    CGContextRestoreGState(ctx);
    // üveg csillanás
    CGContextSaveGState(ctx);
    [facePath addClip];
    NSBezierPath* glare = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMinX(face) - 30 * k, NSMinY(face) - face.size.height * 0.9,
                                                                             face.size.width + 60 * k, face.size.height * 1.35)];
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.22) endingColor:rgb(0xFFFFFF, 0.0)] drawInBezierPath:glare angle:90];
    CGContextRestoreGState(ctx);
    if (shield) {   // belső árnyék, üveg-csillanás jobbra fent, vékony perem
        CGContextSaveGState(ctx);
        [facePath addClip];
        for (int i = 0; i < 7; ++i) {
            NSBezierPath* e = shieldPath(NSInsetRect(face, i * 1.1 * k, i * 1.1 * k), std::max(1.0, fr - i * k), face.size.width * 0.14, face.size.height * 0.32);
            e.lineWidth = 2.2 * k; [rgb(st.face == 4 ? 0x03202E : 0x2A1A06, 0.26 - i * 0.035) setStroke]; [e stroke];
        }
        NSBezierPath* g = [NSBezierPath bezierPath];
        [g moveToPoint:NSMakePoint(NSMaxX(face) - face.size.width * 0.34, NSMinY(face))];
        [g lineToPoint:NSMakePoint(NSMaxX(face), NSMinY(face))];
        [g lineToPoint:NSMakePoint(NSMaxX(face), NSMinY(face) + face.size.height * 0.42)];
        [g closePath];
        [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.30) endingColor:rgb(0xFFFFFF, 0.0)] drawInBezierPath:g angle:-135];
        CGContextRestoreGState(ctx);
        facePath.lineWidth = 1.2 * k; [rgb(0x000000, 0.75) setStroke]; [facePath stroke];
    } else if (st.bezel == 3) {   // belső árnyék a peremen + vékony aranyos szegély
        CGContextSaveGState(ctx);
        [facePath addClip];
        for (int i = 0; i < 6; ++i) {
            NSBezierPath* e = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(face, i * 1.2 * k, i * 1.2 * k) xRadius:fr - i * k yRadius:fr - i * k];
            e.lineWidth = 2.4 * k; [rgb(0x2A1A06, 0.22 - i * 0.035) setStroke]; [e stroke];
        }
        CGContextRestoreGState(ctx);
        facePath.lineWidth = 1.4 * k; [rgb(0x6B4F22) setStroke]; [facePath stroke];
        NSBezierPath* outer = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(face, -2 * k, -2 * k) xRadius:fr + 2 * k yRadius:fr + 2 * k];
        outer.lineWidth = 1.2 * k; [rgb(0x000000, 0.8) setStroke]; [outer stroke];
    } else {
        [rgb(0x000000, 0.5) setStroke]; facePath.lineWidth = 1.2 * k; [facePath stroke];
    }
}

// MARK: - Előlap-elemek

inline void drawScrew(CGPoint c, CGFloat r = 6) {
    [[[NSGradient alloc] initWithStartingColor:rgb(0xE0E0E2) endingColor:rgb(0x606066)]
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] relativeCenterPosition:NSMakePoint(-0.4, 0.4)];
    line(CGPointMake(c.x - r * 0.7, c.y - r * 0.2), CGPointMake(c.x + r * 0.7, c.y + r * 0.2), r * 0.24, rgb(0x2A2A2E));
}

/// Rack-előlap: háromszínű színátmenet, finom szemcse, rack-fülek (earW szélesek) csavarokkal
inline void drawRackFace(CGFloat W, CGFloat H, int top, int mid, int bottom, int ears, CGFloat earW = 22, CGFloat grain = 1.0) {
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(top), 0.0, rgb(mid), 0.45, rgb(bottom), 1.0, nil] drawInRect:NSMakeRect(0, 0, W, H) angle:-90];
    uint32_t seed = 0x2545F491;
    for (int i = 0; i < int(W * H / 330 * grain); ++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        CGFloat x = (seed % uint32_t(W * 10)) / 10.0, y = ((seed >> 13) % uint32_t(H * 10)) / 10.0;
        [rgb((seed & 1) ? 0xFFFFFF : 0x000000, (seed & 1) ? 0.02 : 0.06) setFill];
        NSRectFillUsingOperation(NSMakeRect(x, y, 1, 1), NSCompositingOperationSourceOver);
    }
    for (CGFloat x : {CGFloat(0), W - earW}) {
        NSRect ear = NSMakeRect(x, 0, earW, H);
        [[[NSGradient alloc] initWithStartingColor:rgb(ears) endingColor:[rgb(ears) blendedColorWithFraction:0.4 ofColor:NSColor.blackColor]]
            drawInRect:ear angle:(x == 0 ? 0 : 180)];
        [rgb(0x000000, 0.45) setFill];
        NSRectFill(NSMakeRect(x == 0 ? earW - 1 : x, 0, 1, H));
        drawScrew(CGPointMake(x + earW / 2, 26), earW * 0.25);
        drawScrew(CGPointMake(x + earW / 2, H - 26), earW * 0.25);
    }
}

/// Fejléc: bal oldalt a gyártó, jobb oldalt a típusleírás
inline void drawHeader(CGFloat W, CGFloat y, CGFloat margin, NSString* left, NSString* right, CGFloat size = 8.5) {
    text(left, CGPointMake(margin + 70, y), size, silk(), true, 2.6);
    text(right, CGPointMake(W - margin - 90, y), size * 0.9, silk(), false, 1.6);
}

/// Jelzőlámpa (ékkő): glow 0…1
inline void drawJewel(CGPoint c, CGFloat glow, int color, NSString* label, CGFloat r = 7) {
    glow = std::clamp(glow, CGFloat(0), CGFloat(1));
    if (glow > 0.01)
        [[[NSGradient alloc] initWithStartingColor:rgb(color, 0.6 * glow) endingColor:rgb(color, 0)]
            drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 2.4)] relativeCenterPosition:NSZeroPoint];
    fillOval(c, r, rgb(0xD0D0D4), rgb(0x505056));
    NSColor* base = rgb(color);
    NSColor* hot = [[base blendedColorWithFraction:0.75 ofColor:NSColor.blackColor] blendedColorWithFraction:glow ofColor:[base blendedColorWithFraction:0.35 ofColor:NSColor.whiteColor]];
    NSColor* core = [[base blendedColorWithFraction:0.88 ofColor:NSColor.blackColor] blendedColorWithFraction:glow ofColor:base];
    [[[NSGradient alloc] initWithStartingColor:hot endingColor:core]
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r * 0.72)] relativeCenterPosition:NSMakePoint(-0.3, 0.35)];
    if (label) text(label, CGPointMake(c.x, c.y + r + 8), 7, silkDim(), false, 1.2);
}

/// LED-jelző: süllyesztett fém foglalat, színes lencse csillanással; világításkor a lencse magja kifehéredik (fényudvar nélkül).
/// glow 0…1 (pl. a hangerő-csökkentéssel arányos)
inline void drawLed(CGPoint c, CGFloat glow, int color, NSString* label, CGFloat r = 5.5) {
    glow = std::clamp(glow, CGFloat(0), CGFloat(1));
    // foglalat
    fillOval(c, r + 3.2, rgb(0x2A2B2E), rgb(0xD8DADD));
    fillOval(c, r + 1.6, rgb(0x0B0B0C), rgb(0x3A3B3F));
    // lencse: kialudva sötét színes üveg, világítva fehéres mag
    NSColor* base = rgb(color);
    NSColor* off = [base blendedColorWithFraction:0.78 ofColor:NSColor.blackColor];
    NSColor* core = [off blendedColorWithFraction:glow ofColor:[base blendedColorWithFraction:0.6 ofColor:NSColor.whiteColor]];
    NSColor* edge = [off blendedColorWithFraction:glow ofColor:base];
    [[[NSGradient alloc] initWithStartingColor:core endingColor:edge]   // csak a lencse világít, nincs fényudvar
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:circleRect(c, r)] relativeCenterPosition:NSZeroPoint];
    // csillanás a lencsén
    [[[NSGradient alloc] initWithStartingColor:rgb(0xFFFFFF, 0.75) endingColor:rgb(0xFFFFFF, 0.0)]
        drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - r * 0.6, c.y - r * 0.75, r * 0.8, r * 0.55)] angle:-90];
    if (label) text(label, CGPointMake(c.x, c.y + r + 13), 8, silk(), true, 1.6);
}

/// Gravírozott névtábla (sötét lemez világos betűkkel, vagy fordítva)
inline void drawBadge(NSRect r, NSString* name, int plate, int ink, CGFloat size, CGFloat kern = 4) {
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect:r xRadius:2 yRadius:2];
    shadowed(1, 2, 0.5, ^{ [[[NSGradient alloc] initWithStartingColor:[rgb(plate) blendedColorWithFraction:0.15 ofColor:NSColor.whiteColor] endingColor:rgb(plate)] drawInBezierPath:p angle:-90]; });
    [rgb(0x000000, 0.5) setStroke]; p.lineWidth = 0.6; [p stroke];
    text(name, CGPointMake(NSMidX(r), NSMidY(r)), size, rgb(ink), true, kern);
}

/// Mérőfelirat: mérőmód · GR · (AUTO makeup, ha be van kapcsolva)
inline NSString* meterCaption(NSString* mode, float grDb, bool autoOn, float autoDb) {
    NSString* s = [NSString stringWithFormat:@"%@  ·  GR %.1f", mode, std::fabs(grDb) < 0.05f ? 0.0f : grDb];
    if (autoOn) s = [s stringByAppendingFormat:@"  ·  AUTO %+.1f", std::fabs(autoDb) < 0.05f ? 0.0f : autoDb];
    return s;
}

inline NSArray<NSString*>* ticks(int from, int step, int n) {
    NSMutableArray* a = [NSMutableArray array];
    for (int i = 0; i < n; ++i) [a addObject:[NSString stringWithFormat:@"%d", from + i * step]];
    return a;
}

// MARK: - Vezérlők és vezérlő-logika

class PanelController;

struct Control {
    enum Kind { Knob, SteppedKnob, Toggle, LedButton, Selector, RadioRow, TrimPot } kind;
    AudioUnitParameterID pid;
    CGPoint c;
    CGFloat r;                           // sugár (Knob/Selector/TrimPot), méretszorzó (Toggle/LedButton/RadioRow, 0 = 1)
    NSString* label;                     // név
    NSArray<NSString*>* options;         // Knob: skálafeliratok, SteppedKnob/Selector/RadioRow: állások, Toggle: [ki, be]
    float minV, maxV, defV;
    NSString* (^format)(float) = nil;    // értékkijelzés (nil = nincs)
    float value = 0;
    CGFloat spread = 0;                  // Knob/SteppedKnob: félszög (0 = 135°), Selector: szélső állások szöge (0 = 55°)
    int cap = -1;                        // sapkaszín / jelzőpont színe (-1 = téma)
    int tint = -1;                       // esztergált gomb: színezett (eloxált) sapka (-1 = nincs)
    int rim = -1;                        // esztergált gomb: színezett (eloxált) karima / recézett külső gyűrű (-1 = nincs)
    bool bipolar = false;                // érték-ív középről (± gombok)
    std::vector<float> taper = {};       // egyedi skála: az egyenletesen elosztott skálajelekhez tartozó értékek (szakaszonként lineáris)
    bool logTaper = false;               // logaritmikus skála (idők, arány)
    bool vertical = false;               // RadioRow: függőleges
    CGFloat labelDy = 0;                 // a felirat távolsága a középponttól (0 = automatikus)
    bool drawer = false;                 // az EKI fiókban van (csak nyitott fióknál látszik)
    CGFloat labelSize = 0;               // feliratméret (0 = automatikus)
    int knobStyle = -1;                  // saját gombforma (KnobStyle; -1 = a téma szerinti)
    CGFloat rowWiden = 1;                // RadioRow: a gombok szélesség-szorzója (hosszabb feliratokhoz)
    bool upIsOn = false;                 // Toggle: a felső állás jelenti a bekapcsolt (1) értéket (mint a vason)
    int ink = -1;                        // saját feliratszín (skálaszámok, név, érték; pl. sötét szöveg világos mezőn), -1 = téma
    std::function<bool(const PanelController&)> visibleIf = nullptr;   // feltételes megjelenés (pl. M/S: a MID vagy a SIDE készlet gombjai)
    int lockedBy = -1;                   // zárolva (0-n, halványítva), amíg ez a kapcsoló-paraméter be van kapcsolva (AUTO GAIN)

    CGFloat size() const { return r > 0 ? r : 1; }
    CGFloat sweep() const { return spread > 0 ? spread : (kind == Selector ? 55 : 135); }
    float toNorm(float v) const {
        if (taper.size() >= 2) {
            const int n = int(taper.size()) - 1;
            if (v <= taper[0]) return 0;
            for (int i = 0; i < n; ++i)
                if (v <= taper[i + 1]) return (i + (v - taper[i]) / std::max(1e-6f, taper[i + 1] - taper[i])) / n;
            return 1;
        }
        if (logTaper && minV > 0) return std::log(v / minV) / std::log(maxV / minV);
        return (v - minV) / std::max(1e-6f, maxV - minV);
    }
    float fromNorm(float n) const {
        n = std::clamp(n, 0.0f, 1.0f);
        if (taper.size() >= 2) {
            const int segs = int(taper.size()) - 1;
            const float x = n * segs;
            const int i = std::min(int(x), segs - 1);
            return taper[i] + (taper[i + 1] - taper[i]) * (x - i);
        }
        if (logTaper && minV > 0) return minV * std::pow(maxV / minV, n);
        return minV + n * (maxV - minV);
    }
    bool isDiscrete() const { return kind != Knob && kind != TrimPot; }
};

class PanelController {
public:
    AudioUnit au = nullptr;
    CGSize size{};                                                 // alapméret (100%)
    CGFloat scale = 1;
    std::string prefsKey;                                           // a méret megjegyzéséhez
    std::vector<Control> controls;
    std::vector<std::pair<AudioUnitParameterID, float>> watched;   // csak olvasott paraméterek (mérők)
    std::function<void(PanelController&)> drawFace;                // előlap (a vezérlők alatt)
    std::function<void(PanelController&)> drawOverlay;             // a vezérlők fölött
    std::function<void()> onResize;                                // a nézet állítja be
    std::function<void(PanelController&)> drawStatic;              // statikus előlap (bitképbe gyorsítótárazva)
    std::function<bool(PanelController&)> animate;                 // folyamatos animáció (pl. forgó orsók): true = újra kell rajzolni
    CGFloat drawerHeight = 0;                                       // EKI fiók magassága (0 = nincs fiók)
    bool drawerOpen = false;
    struct HotSpot { CGRect r; std::function<void(PanelController&, CGPoint)> action; };
    std::vector<HotSpot> hotspots;                                  // kattintható területek (fejléc gombjai)
    __weak NSView* view = nil;
    CGFloat backing = 2;
    NSBitmapImageRep* staticCache = nil;
    CGFloat cacheScale = 0, cacheBacking = 0; bool cacheDrawer = false;
    int updateGen = -1;                                             // frissítés-jelzés: utoljára rajzolt állapot
    // POWER LED: a plugin dolgozik-e (a DAW nem kerülte meg / nem állította le) — kEKIProperty_Activity életjelből
    UInt32 lastRenderCount = 0; CFAbsoluteTime lastRenderTime = 0; bool processing = true;

    PanelController(AudioUnit unit, CGSize sz, const char* key) : au(unit), size(sz), prefsKey(key) {
        registerBundleFonts();
        NSString* k = [NSString stringWithFormat:@"EKIDIO.%s.uiScale", key];
        double s = [[NSUserDefaults standardUserDefaults] doubleForKey:k];
        if (s >= 0.75 && s <= 2.0) scale = s;
        update::check();                                             // háttérben: van-e újabb verzió
    }

    CGFloat fullHeight() const { return size.height + (drawerOpen ? drawerHeight : 0); }
    CGSize viewSize() const { return CGSizeMake(std::round(size.width * scale), std::round(fullHeight() * scale)); }
    void invalidateStatic() { staticCache = nil; }

    // ---- EKI fiók ----
    std::string key(const char* what) const { return "EKIDIO." + prefsKey + "." + what; }
    void loadDrawerState() {
        drawerOpen = [[NSUserDefaults standardUserDefaults] boolForKey:[NSString stringWithUTF8String:key("drawerOpen").c_str()]];
    }
    void toggleDrawer() {
        drawerOpen = !drawerOpen;
        [[NSUserDefaults standardUserDefaults] setBool:drawerOpen forKey:[NSString stringWithUTF8String:key("drawerOpen").c_str()]];
        invalidateStatic();
        if (onResize) onResize();
    }

    // ---- presetek (a plugin gyári presetjei) ----
    std::vector<std::pair<SInt32, NSString*>> factoryPresets() const {
        std::vector<std::pair<SInt32, NSString*>> out;
        CFArrayRef arr = nullptr; UInt32 sz = sizeof(arr);
        if (AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &arr, &sz) == noErr && arr) {
            for (CFIndex i = 0; i < CFArrayGetCount(arr); ++i) {
                auto p = (const AUPreset*)CFArrayGetValueAtIndex(arr, i);
                out.push_back({p->presetNumber, [(__bridge NSString*)p->presetName copy]});
            }
            CFRelease(arr);
        }
        return out;
    }
    AUPreset currentPreset() const {
        AUPreset p{-1, nullptr}; UInt32 sz = sizeof(p);
        AudioUnitGetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, &sz);
        return p;   // a presetName-et a hívó engedi el
    }
    NSString* currentPresetName() const {
        AUPreset p = currentPreset();
        if (!p.presetName) return @"—";
        NSString* s = [(__bridge NSString*)p.presetName copy];
        CFRelease(p.presetName);
        return s;
    }
    void selectPreset(int index) {
        auto list = factoryPresets();
        if (list.empty()) return;
        index = (index % int(list.size()) + int(list.size())) % int(list.size());
        AUPreset p{list[index].first, (__bridge CFStringRef)list[index].second};
        AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, sizeof(p));
        poll();
    }
    int currentPresetIndex() const {
        AUPreset p = currentPreset();
        int idx = -1;
        if (p.presetName) CFRelease(p.presetName);
        auto list = factoryPresets();
        for (int i = 0; i < int(list.size()); ++i) if (list[i].first == p.presetNumber) idx = i;
        return idx;
    }
    /// legördülő preset-lista a megadott (alap-koordinátás) téglalapnál
    void popupPresetMenu(CGRect r) {
        NSView* v = view;
        if (!v) return;
        auto list = factoryPresets();
        NSPopUpButtonCell* cell = [[NSPopUpButtonCell alloc] initTextCell:@"" pullsDown:NO];
        for (auto& it : list) [cell addItemWithTitle:it.second];
        const int cur = currentPresetIndex();
        if (cur >= 0) [cell selectItemAtIndex:cur];
        NSRect fr = NSMakeRect(r.origin.x * scale, r.origin.y * scale, r.size.width * scale, r.size.height * scale);
        [cell performClickWithFrame:fr inView:v];
        const NSInteger sel = cell.indexOfSelectedItem;
        if (sel >= 0 && sel != cur) selectPreset(int(sel));
    }

    /// a `target` gomb zárolása, amíg a `sw` kapcsoló be van kapcsolva (az AU 0-ra állítja és nem engedi állítani)
    void lock(AudioUnitParameterID target, AudioUnitParameterID sw) {
        for (auto& c : controls) if (c.pid == target) c.lockedBy = int(sw);
    }
    /// paraméter állítása egy egyedi kattintható területről (pl. csőablak): a hozzá tartozó (akár rejtett) vezérlőn át,
    /// automáció-gesztussal együtt
    void setParamFromUI(AudioUnitParameterID pid, float v) {
        for (auto& c : controls) if (c.pid == pid) { gesture(c, true); set(c, v); gesture(c, false); return; }
    }
    bool shown(const Control& c) const { return !c.visibleIf || c.visibleIf(*this); }
    bool isLocked(const Control& c) const { return c.lockedBy >= 0 && get(AudioUnitParameterID(c.lockedBy)) >= 0.5f; }

    float get(AudioUnitParameterID pid) const {
        for (auto& w : watched) if (w.first == pid) return w.second;
        for (auto& c : controls) if (c.pid == pid) return c.value;
        AudioUnitParameterValue v = 0;
        AudioUnitGetParameter(au, pid, kAudioUnitScope_Global, 0, &v);
        return v;
    }

    /// paraméterek beolvasása; true, ha újra kell rajzolni
    bool poll() {
        bool changed = false;
        if (const int g = update::generation(); g != updateGen) { updateGen = g; changed = true; }
        {   // életjel: ha a feldolgozás leáll (a DAW kikapcsolta a plugint) vagy bypass, a POWER LED kialszik
            UInt32 act[2] = {0, 0}; UInt32 sz = sizeof(act);
            bool now = true;
            if (au && AudioUnitGetProperty(au, 64000 /* kEKIProperty_Activity */, kAudioUnitScope_Global, 0, act, &sz) == noErr) {
                const CFAbsoluteTime t = CFAbsoluteTimeGetCurrent();
                if (act[0] != lastRenderCount) { lastRenderCount = act[0]; lastRenderTime = t; }
                now = act[1] == 0 && t - lastRenderTime < 0.6;
            }
            if (now != processing) { processing = now; changed = true; }
        }
        for (auto& c : controls) {
            AudioUnitParameterValue v = 0;
            if (AudioUnitGetParameter(au, c.pid, kAudioUnitScope_Global, 0, &v) == noErr && v != c.value) { c.value = v; changed = true; }
        }
        for (auto& w : watched) {
            AudioUnitParameterValue v = 0;
            if (AudioUnitGetParameter(au, w.first, kAudioUnitScope_Global, 0, &v) == noErr && std::fabs(v - w.second) > 0.01f) { w.second = v; changed = true; }
        }
        if (animate && animate(*this)) changed = true;
        return changed;
    }

    void draw() {
        CGContextSaveGState(cg());
        CGContextScaleCTM(cg(), scale, scale);
        if (drawStatic) {
            if (!staticCache || cacheScale != scale || cacheBacking != backing || cacheDrawer != drawerOpen) renderStatic();
            [staticCache drawInRect:NSMakeRect(0, 0, size.width, fullHeight()) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver
                           fraction:1 respectFlipped:YES hints:nil];
        }
        if (drawFace) drawFace(*this);
        for (auto& c : controls) if ((!c.drawer || drawerOpen) && shown(c)) {
            if (c.ink >= 0 || c.knobStyle >= 0) {            // saját feliratszín / gombforma: a téma erre a vezérlőre átállítva
                const Theme saved = theme();
                if (c.ink >= 0) theme().silk = theme().tick = theme().accent = c.ink;
                if (c.knobStyle >= 0) theme().knob = KnobStyle(c.knobStyle);
                drawControl(c);
                theme() = saved;
            } else drawControl(c);
            if (isLocked(c)) drawLockShade(c);
        }
        if (drawOverlay) drawOverlay(*this);
        drawGrip();
        CGContextRestoreGState(cg());
    }

    // ---- egér (nézet-koordinátákban) ----
    void mouseDown(CGPoint vp, int clicks, bool shift) {
        CGPoint p = base(vp);
        int hs = -1;
        for (int i = 0; i < int(hotspots.size()); ++i) if (CGRectContainsPoint(hotspots[i].r, p)) hs = i;
        if (hs >= 0) { hotspots[hs].action(*this, p); return; }
        if (p.x > size.width - 18 && p.y > fullHeight() - 18) {   // méretfogó
            if (clicks >= 2) { setScale(1.0); saveScale(); return; }
            resizing = true; startPx = vp.x; startPy = vp.y; startScale = scale;
            return;
        }
        active = hit(p);
        moved = false;
        if (active < 0) return;
        Control& c = controls[active];
        switch (c.kind) {
            case Control::Knob:
            case Control::TrimPot:
            case Control::SteppedKnob:
                gesture(c, true);
                if (clicks >= 2) set(c, c.defV);
                startY = p.y; startNorm = c.toNorm(c.value); startV = c.value;
                break;
            case Control::Toggle:
            case Control::LedButton:
                gesture(c, true); set(c, c.value >= 0.5f * (c.minV + c.maxV) ? c.minV : c.maxV); gesture(c, false);
                active = -1;
                break;
            case Control::Selector: {
                const int n = int(c.options.count);
                int idx = int(std::lround(c.value - c.minV));
                CGFloat dx = p.x - c.c.x, dy = p.y - c.c.y;
                if (std::hypot(dx, dy) > c.r) {   // feliratra kattintás: közvetlen választás
                    CGFloat a = std::atan2(dx, -dy) * 180 / M_PI;
                    idx = int(std::lround((a + c.sweep()) / (2 * c.sweep() / std::max(1, n - 1))));
                } else {
                    idx = (idx + 1) % n;
                }
                gesture(c, true); set(c, c.minV + std::clamp(idx, 0, n - 1)); gesture(c, false);
                active = -1;
                break;
            }
            case Control::RadioRow: {
                const int n = int(c.options.count);
                int best = 0; CGFloat bd = 1e9;
                for (int i = 0; i < n; ++i) {
                    CGPoint q = radioPos(c.c, i, n, c.vertical, c.size(), c.rowWiden);
                    CGFloat d = std::hypot(p.x - q.x, p.y - q.y);
                    if (d < bd) { bd = d; best = i; }
                }
                gesture(c, true); set(c, c.minV + best); gesture(c, false);
                active = -1;
                break;
            }
        }
    }

    void mouseDragged(CGPoint vp, bool shift) {
        if (resizing) {
            const CGFloat dxw = vp.x - startPx, dyw = (vp.y - startPy) * size.width / size.height;
            const CGFloat dw = std::fabs(dxw) >= std::fabs(dyw) ? dxw : dyw;   // a nagyobb elmozdulás számít
            setScale(startScale + dw / size.width);
            return;
        }
        if (active < 0) return;
        CGPoint p = base(vp);
        Control& c = controls[active];
        if (std::fabs(p.y - startY) > 2) moved = true;
        if (c.kind == Control::SteppedKnob) {
            set(c, startV + std::round(float(startY - p.y) / 16.0f));
        } else {
            const float px = shift ? 1600.0f : 180.0f;
            set(c, c.fromNorm(startNorm + float(startY - p.y) / px));
        }
    }

    void mouseUp() {
        if (resizing) { resizing = false; saveScale(); return; }
        if (active >= 0) {
            Control& c = controls[active];
            if (c.kind == Control::SteppedKnob && !moved) {   // kattintás: következő állás
                float v = c.value + 1; if (v > c.maxV + 0.01f) v = c.minV;
                set(c, v);
            }
            gesture(c, false);
        }
        active = -1;
    }

    void scroll(CGPoint vp, CGFloat dy, bool shift) {
        int i = hit(base(vp));
        if (i < 0) return;
        Control& c = controls[i];
        if (c.kind == Control::Knob || c.kind == Control::TrimPot)
            set(c, c.fromNorm(c.toNorm(c.value) + float(dy) / (shift ? 1500.0f : 150.0f)));
        else if (c.kind == Control::SteppedKnob && std::fabs(dy) >= 1)
            set(c, c.value + (dy > 0 ? 1 : -1));
    }

private:
    int active = -1;
    bool moved = false, resizing = false;
    CGFloat startY = 0, startPx = 0, startPy = 0, startScale = 1;
    float startNorm = 0, startV = 0;

    CGPoint base(CGPoint vp) const { return CGPointMake(vp.x / scale, vp.y / scale); }

    void setScale(CGFloat s) {
        s = std::clamp(s, CGFloat(0.75), CGFloat(2.0));
        if (std::fabs(s - scale) < 0.004) return;
        scale = s;
        if (onResize) onResize();
    }
    void saveScale() {
        [[NSUserDefaults standardUserDefaults] setDouble:scale forKey:[NSString stringWithFormat:@"EKIDIO.%s.uiScale", prefsKey.c_str()]];
    }

    void drawGrip() {
        const CGFloat h = fullHeight();
        for (int i = 1; i <= 3; ++i) {
            CGFloat o = 4 * i + 1;
            line(CGPointMake(size.width - 3, h - o), CGPointMake(size.width - o, h - 3), 1.0, rgb(drawerOpen ? 0xFFFFFF : theme().silk, 0.35));
        }
    }

    /// a statikus előlap kirajzolása bitképbe (a tényleges pixelméreten, retinán is élesen)
    void renderStatic() {
        const CGFloat k = scale * backing;
        const NSInteger pw = NSInteger(std::ceil(size.width * k)), ph = NSInteger(std::ceil(fullHeight() * k));
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:pw pixelsHigh:ph bitsPerSample:8
                                    samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        NSGraphicsContext* g = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        CGContextRef c = g.CGContext;
        CGContextTranslateCTM(c, 0, ph);
        CGContextScaleCTM(c, k, -k);
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:c flipped:YES]];
        drawStatic(*this);
        [NSGraphicsContext restoreGraphicsState];
        rep.size = NSMakeSize(size.width, fullHeight());
        staticCache = rep;
        cacheScale = scale; cacheBacking = backing; cacheDrawer = drawerOpen;
    }

    CGFloat labelFont(const Control& c) const { return c.labelSize > 0 ? c.labelSize : (c.r >= 30 ? 11 : 8.5); }

    /// zárolt gomb: sötétített, a sapka fölé halvány árnyék
    void drawLockShade(const Control& c) {
        CGContextRef g = cg();
        CGContextSaveGState(g);
        CGContextSetRGBFillColor(g, 0, 0, 0, 0.5);
        CGContextFillEllipseInRect(g, CGRectMake(c.c.x - c.r - 3, c.c.y - c.r - 3, 2 * c.r + 6, 2 * c.r + 6));
        CGContextRestoreGState(g);
    }

    void drawControl(const Control& c) {
        const int idx = int(std::lround(c.value - c.minV));
        switch (c.kind) {
            case Control::Knob: {
                drawKnob(c.c, c.r, c.toNorm(c.value), c.options, c.sweep(), c.cap, 11, c.tint, c.rim, c.bipolar);
                const CGFloat lf = labelFont(c);
                CGFloat dy = c.labelDy;
                if (dy <= 0) {
                    const CGFloat outer = c.r + 3 + tickLen(c.r) + (c.options ? tickFont(c.r) * 1.6 : 0);
                    dy = std::max(c.r + 6, outer * std::cos(deg(180 - c.sweep()))) + lf * 0.9;
                }
                text(c.label, CGPointMake(c.c.x, c.c.y + dy), lf, silk(), true, c.r >= 30 ? 1.6 : 0.8);
                if (c.format) {
                    CGContextSaveGState(cg());
                    if (theme().valueGlow) CGContextSetShadowWithColor(cg(), CGSizeZero, 6, rgb(theme().accent, 0.75).CGColor);
                    text(c.format(c.value), CGPointMake(c.c.x, c.c.y + dy + lf + 1), lf * 0.85, accent(), false, 0.3);
                    CGContextRestoreGState(cg());
                }
                break;
            }
            case Control::SteppedKnob: {
                const int n = int(c.options.count);
                drawKnob(c.c, c.r, n > 1 ? float(idx) / (n - 1) : 0, c.options, c.sweep(), c.cap);
                const CGFloat lf = labelFont(c);
                const CGFloat dy = c.labelDy > 0 ? c.labelDy : c.r + 8 + lf * 0.9;
                text(c.label, CGPointMake(c.c.x, c.c.y + dy), lf, silk(), true, 0.8);
                if (c.format) text(c.format(c.value), CGPointMake(c.c.x, c.c.y + dy + lf + 1), lf * 0.85, accent(), false, 0.3);   // pontos érték (sokállású gomboknál)
                break;
            }
            case Control::TrimPot:
                drawTrimPot(c.c, c.r, c.toNorm(c.value));
                text(c.label, CGPointMake(c.c.x, c.c.y + (c.labelDy > 0 ? c.labelDy : c.r + 13)), 7.5, silk(), true, 0.6);
                if (c.format) text(c.format(c.value), CGPointMake(c.c.x, c.c.y + (c.labelDy > 0 ? c.labelDy : c.r + 13) + 9), 7, accent(), false, 0.3);
                break;
            case Control::Toggle:
                drawToggle(c.c, c.upIsOn ? idx == 0 : idx != 0, c.options[0], c.options[1], c.size());
                break;
            case Control::LedButton:
                drawLedButton(c.c, idx != 0, c.label, c.size());
                break;
            case Control::Selector:
                drawSelector(c.c, c.r, idx, c.options, c.sweep());
                text(c.label, CGPointMake(c.c.x, c.c.y + (c.labelDy > 0 ? c.labelDy : c.r + 12)), 8.5, silk(), true, 0.8);
                break;
            case Control::RadioRow: {
                drawRadioRow(c.c, idx, c.options, [c.options.lastObject isEqualToString:@"ALL"], c.vertical, c.size(), c.rowWiden);
                const int n = int(c.options.count);
                CGPoint last = radioPos(c.c, n - 1, n, c.vertical, c.size(), c.rowWiden);
                CGPoint lp = c.vertical ? CGPointMake(c.c.x, last.y + 18 * c.size()) : CGPointMake(c.c.x, c.c.y + 19 * c.size());
                if (c.labelDy > 0) lp = CGPointMake(c.c.x, c.c.y + c.labelDy);
                text(c.label, lp, 8.5, silk(), true, 0.8);
                break;
            }
        }
    }

    int hit(CGPoint p) const {
        for (int i = 0; i < int(controls.size()); ++i) {
            const Control& c = controls[i];
            if (c.drawer && !drawerOpen) continue;
            if (!shown(c)) continue;
            if (isLocked(c)) continue;                        // zárolt gomb: nem tekerhető
            CGFloat dx = p.x - c.c.x, dy = p.y - c.c.y;
            const CGFloat s = c.size();
            switch (c.kind) {
                case Control::Knob: case Control::SteppedKnob:
                    if (std::hypot(dx, dy) <= c.r + 6) return i; break;
                case Control::TrimPot:
                    if (std::hypot(dx, dy) <= c.r + 8) return i; break;
                case Control::Toggle:
                    if (std::fabs(dx) <= 20 * s && std::fabs(dy) <= 36 * s) return i; break;
                case Control::LedButton:
                    if (std::fabs(dx) <= 20 * s && dy >= -13 * s && dy <= 26 * s) return i; break;
                case Control::Selector:
                    if (std::hypot(dx, dy) <= c.r + 22 && dy <= c.r + 4) return i; break;
                case Control::RadioRow: {
                    const int n = int(c.options.count);
                    for (int k = 0; k < n; ++k) {
                        CGPoint q = radioPos(c.c, k, n, c.vertical, s, c.rowWiden);
                        if (std::fabs(p.x - q.x) <= 15 * s * c.rowWiden && std::fabs(p.y - q.y) <= 11 * s) return i;
                    }
                    break;
                }
            }
        }
        return -1;
    }

    AudioUnitParameter param(const Control& c) const {
        AudioUnitParameter p{};
        p.mAudioUnit = au; p.mParameterID = c.pid; p.mScope = kAudioUnitScope_Global; p.mElement = 0;
        return p;
    }

    void set(Control& c, float v) {
        v = std::clamp(v, c.minV, c.maxV);
        if (c.isDiscrete()) v = std::round(v);
        if (v == c.value) return;
        c.value = v;
        AudioUnitParameter p = param(c);
        AUParameterSet(nullptr, nullptr, &p, v, 0);   // beállít + értesíti a hostot (automáció)
    }

    void gesture(const Control& c, bool begin) {
        AudioUnitEvent ev{};
        ev.mEventType = begin ? kAudioUnitEvent_BeginParameterChangeGesture : kAudioUnitEvent_EndParameterChangeGesture;
        ev.mArgument.mParameter = param(c);
        AUEventListenerNotify(nullptr, nullptr, &ev);
    }
};

} // namespace eki::ui

// MARK: - Cocoa osztályok (buildenként egyedi név: EKI_UI_FACTORY a build.sh-ból)

#define EKI_UI_CAT2(a, b) a##b
#define EKI_UI_CAT(a, b) EKI_UI_CAT2(a, b)
#define EKI_UI_VIEW EKI_UI_CAT(EKI_UI_FACTORY, _View)

/// BUILD_FN: std::unique_ptr<eki::ui::PanelController> (*)(AudioUnit)
#define EKI_DEFINE_PANEL_VIEW(BUILD_FN)                                                                   \
@interface EKI_UI_VIEW : NSView                                                                            \
- (instancetype)initWithAU:(AudioUnit)au;                                                                  \
@end                                                                                                       \
@implementation EKI_UI_VIEW {                                                                              \
    std::unique_ptr<eki::ui::PanelController> _pc;                                                         \
    NSTimer* _timer;                                                                                       \
}                                                                                                          \
- (instancetype)initWithAU:(AudioUnit)au {                                                                 \
    auto pc = BUILD_FN(au);                                                                                \
    CGSize vs = pc->viewSize();                                                                            \
    if ((self = [super initWithFrame:NSMakeRect(0, 0, vs.width, vs.height)])) {                            \
        _pc = std::move(pc);                                                                               \
        _pc->poll();                                                                                       \
        _pc->view = self;                                                                                  \
        __weak EKI_UI_VIEW* weakSelf = self;                                                               \
        _pc->onResize = ^{                                                                                 \
            EKI_UI_VIEW* s = weakSelf; if (!s) return;                                                     \
            CGSize v = [s controller]->viewSize();                                                         \
            [s setFrameSize:NSMakeSize(v.width, v.height)];                                                \
            [s setNeedsDisplay:YES];                                                                       \
        };                                                                                                 \
    }                                                                                                      \
    return self;                                                                                           \
}                                                                                                          \
- (BOOL)isFlipped { return YES; }                                                                          \
- (BOOL)isOpaque { return YES; }                                                                           \
- (BOOL)mouseDownCanMoveWindow { return NO; }   /* a fejléc-sáv gombjai ne ablakmozgatásnak számítsanak */ \
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }                                                      \
- (eki::ui::PanelController*)controller { return _pc.get(); }                                              \
- (void)drawRect:(NSRect)r { _pc->draw(); }                                                                \
- (void)viewDidChangeBackingProperties {                                                                    \
    [super viewDidChangeBackingProperties];                                                                \
    if (self.window) { _pc->backing = self.window.backingScaleFactor; _pc->invalidateStatic(); }           \
    [self setNeedsDisplay:YES];                                                                            \
}                                                                                                          \
- (void)viewDidMoveToWindow {                                                                              \
    [super viewDidMoveToWindow];                                                                           \
    if (self.window) _pc->backing = self.window.backingScaleFactor;                                        \
    [_timer invalidate]; _timer = nil;                                                                     \
    if (!self.window) return;                                                                              \
    __weak EKI_UI_VIEW* weakSelf = self;                                                                   \
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 repeats:YES block:^(NSTimer*) {                     \
        EKI_UI_VIEW* s = weakSelf;                                                                         \
        if (s && [s controller]->poll()) [s setNeedsDisplay:YES];                                          \
    }];                                                                                                    \
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];                                \
}                                                                                                          \
- (void)dealloc { [_timer invalidate]; }                                                                   \
- (NSPoint)local:(NSEvent*)e { return [self convertPoint:e.locationInWindow fromView:nil]; }              \
- (void)mouseDown:(NSEvent*)e {                                                                            \
    _pc->mouseDown([self local:e], int(e.clickCount), e.modifierFlags & NSEventModifierFlagShift);         \
    [self setNeedsDisplay:YES];                                                                            \
}                                                                                                          \
- (void)mouseDragged:(NSEvent*)e {                                                                         \
    _pc->mouseDragged([self local:e], e.modifierFlags & NSEventModifierFlagShift);                         \
    [self setNeedsDisplay:YES];                                                                            \
}                                                                                                          \
- (void)mouseUp:(NSEvent*)e { _pc->mouseUp(); [self setNeedsDisplay:YES]; }                                \
- (void)scrollWheel:(NSEvent*)e {                                                                          \
    _pc->scroll([self local:e], e.scrollingDeltaY * (e.hasPreciseScrollingDeltas ? 0.1 : 1.0),            \
                e.modifierFlags & NSEventModifierFlagShift);                                               \
    [self setNeedsDisplay:YES];                                                                            \
}                                                                                                          \
@end                                                                                                       \
@interface EKI_UI_FACTORY : NSObject <AUCocoaUIBase>                                                       \
@end                                                                                                       \
@implementation EKI_UI_FACTORY                                                                             \
- (unsigned)interfaceVersion { return 0; }                                                                 \
- (NSString*)description { return @"EKIDIO SOUND"; }                                                       \
- (NSView*)uiViewForAudioUnit:(AudioUnit)au withSize:(NSSize)size {                                        \
    return [[EKI_UI_VIEW alloc] initWithAU:au];                                                            \
}                                                                                                          \
@end
