// EKIRack.h — az EKIDIO SOUND egységes "rack" kerete (az OPTEKI-n jóváhagyott stílus):
//   sötét fejléc-sáv (EKIDIO SOUND · preset ◀ lista ▶ · EKI fiók gomb), lekerekített fém keret,
//   szálcsiszolt előlap az eredeti eszköz színeiben, lenyitható szálcsiszolt EKI fiók.
// A pluginok csak a színeket (Look), a típus-sort, a fiók-tippeket és a saját vezérlőiket adják meg.
#pragma once
#include "EKIPanel.h"

namespace eki::ui::rack {

constexpr CGFloat W = 960, HEAD = 36, H = 330, DRAWER = 132;

// fejléc-gombok helye
const CGRect kPrev = CGRectMake(318, 7, 24, 22), kPreset = CGRectMake(346, 7, 268, 22), kNext = CGRectMake(618, 7, 24, 22);
const CGRect kDrawerBtn = CGRectMake(W - 132, 7, 96, 22);

/// előlap- és fiók-színek
struct Look {
    int plateTop, plateMid, plateBot;    // előlap színátmenet
    CGFloat brush;                       // szálcsiszolás erőssége
    int frame[5];                        // keret színátmenet (felülről lefelé)
    int trayTop, trayMid, trayBot;       // fiók
    CGFloat trayBrush;
    int ink, inkDim;                     // gravírozás
    CGFloat plateEdge;                   // a lemez peremének fénye (fehér alfa)
    CGFloat innerShadow;                 // belső árnyék a keret alatt
};

/// szálcsiszolt fém (determinisztikus hajszálvonalak + finom függőleges fénysáv); strength: a vonalak erőssége
inline void brushedPlate(NSBezierPath* clip, NSRect r, int top, int mid, int bottom, uint32_t seed, CGFloat strength) {
    CGContextSaveGState(cg());
    [clip addClip];
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(top), 0.0, rgb(mid), 0.5, rgb(bottom), 1.0, nil] drawInRect:r angle:-90];
    for (CGFloat y = NSMinY(r) + 0.5; y < NSMaxY(r); y += 0.75) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        const CGFloat a = (seed % 1000) / 1000.0;
        line(CGPointMake(NSMinX(r), y), CGPointMake(NSMaxX(r), y), 0.5,
             rgb((seed & 1) ? 0xFFFFFF : 0x000000, strength * ((seed & 1) ? 0.03 + 0.07 * a : 0.03 + 0.06 * a)));
    }
    [[[NSGradient alloc] initWithColorsAndLocations:rgb(0xFFFFFF, 0.0), 0.0, rgb(0xFFFFFF, 0.06 * strength), 0.3, rgb(0xFFFFFF, 0.0), 0.5,
                                                    rgb(0xFFFFFF, 0.035 * strength), 0.78, rgb(0xFFFFFF, 0.0), 1.0, nil] drawInRect:r angle:0];
    CGContextRestoreGState(cg());
}

/// fejléc + keret + előlap + (nyitott) fiók. typeLine: kis gravírozott típus-sor balra fent;
/// hints: a fiók vezérlői alatti egysoros magyarázatok (x, szöveg)
inline void drawChrome(PanelController& pc, const Look& L, NSString* typeLine, const std::vector<std::pair<CGFloat, NSString*>>& hints) {
    // ---- fejléc-sáv (minden pluginnál azonos) ----
    [[[NSGradient alloc] initWithStartingColor:rgb(0x222326) endingColor:rgb(0x121315)] drawInRect:NSMakeRect(0, 0, W, HEAD) angle:-90];
    line(CGPointMake(0, HEAD - 0.5), CGPointMake(W, HEAD - 0.5), 1, rgb(0x000000, 0.8));
    textFont(@"EKIDIO", @"AvenirNext-DemiBold", CGPointMake(66, 18), 12, rgb(0xEDE3CF), 3.5);
    textFont(@"SOUND", @"AvenirNext-Regular", CGPointMake(134, 18), 12, rgb(0x9C9486), 3.5);
    for (CGRect r : {kPrev, kPreset, kNext, kDrawerBtn}) {
        NSBezierPath* b = [NSBezierPath bezierPathWithRoundedRect:r xRadius:5 yRadius:5];
        [[[NSGradient alloc] initWithStartingColor:rgb(0x0B0B0C) endingColor:rgb(0x17181B)] drawInBezierPath:b angle:-90];
        [rgb(0xFFFFFF, 0.09) setStroke]; b.lineWidth = 0.8; [b stroke];
    }
    text(@"◀", CGPointMake(CGRectGetMidX(kPrev), 18), 9, rgb(0xBDB4A4));
    text(@"▶", CGPointMake(CGRectGetMidX(kNext), 18), 9, rgb(0xBDB4A4));
    textFont(pc.drawerOpen ? @"EKI  ▴" : @"EKI  ▾", @"AvenirNext-DemiBold", CGPointMake(CGRectGetMidX(kDrawerBtn), 18), 10, rgb(0xF2A33A), 2.5);

    // ---- fém keret + szálcsiszolt előlap ----
    [rgb(0x0C0C0E) setFill];
    NSRectFill(NSMakeRect(0, HEAD, W, H - HEAD));
    NSRect frameR = NSMakeRect(8, HEAD + 8, W - 16, H - HEAD - 16);
    NSBezierPath* frame = [NSBezierPath bezierPathWithRoundedRect:frameR xRadius:20 yRadius:20];
    shadowed(6, 18, 0.9, ^{
        [[[NSGradient alloc] initWithColorsAndLocations:rgb(L.frame[0]), 0.0, rgb(L.frame[1]), 0.2, rgb(L.frame[2]), 0.5,
                                                        rgb(L.frame[3]), 0.85, rgb(L.frame[4]), 1.0, nil] drawInBezierPath:frame angle:-90];
    });
    [rgb(0x000000, 0.85) setStroke]; frame.lineWidth = 1; [frame stroke];
    NSRect plateR = NSInsetRect(frameR, 5, 5);
    NSBezierPath* plate = [NSBezierPath bezierPathWithRoundedRect:plateR xRadius:15 yRadius:15];
    brushedPlate(plate, plateR, L.plateTop, L.plateMid, L.plateBot, 0x2545F491u, L.brush);
    CGContextSaveGState(cg());   // belső árnyék a keret alatt
    [plate addClip];
    for (int k = 0; k < 5; ++k) {
        NSBezierPath* e = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(plateR, k, k) xRadius:15 - k yRadius:15 - k];
        e.lineWidth = 2; [rgb(0x000000, L.innerShadow - k * L.innerShadow * 0.18) setStroke]; [e stroke];
    }
    CGContextRestoreGState(cg());
    [rgb(0xFFFFFF, L.plateEdge) setStroke]; plate.lineWidth = 0.8; [plate stroke];

    if (typeLine) textFont(typeLine, @"AvenirNext-DemiBold", CGPointMake(150, 76), 7, rgb(L.inkDim), 1.8);

    // ---- EKI fiók ----
    if (pc.drawerOpen) {
        NSRect dr = NSMakeRect(0, H, W, DRAWER);
        brushedPlate([NSBezierPath bezierPathWithRect:dr], dr, L.trayTop, L.trayMid, L.trayBot, 0x51F15E3Du, L.trayBrush);
        [[[NSGradient alloc] initWithStartingColor:rgb(0x000000, 0.45) endingColor:rgb(0x000000, 0.0)] drawInRect:NSMakeRect(0, H, W, 12) angle:-90];
        textFont(@"EKI DRAWER", @"AvenirNext-DemiBold", CGPointMake(110, H + 24), 9, rgb(L.ink), 3.5);
        textFont(@"fine tuning", @"AvenirNext-Regular", CGPointMake(110, H + 39), 8.5, rgb(L.inkDim), 1);
        for (auto& h : hints) textFont(h.second, @"AvenirNext-Regular", CGPointMake(h.first, H + DRAWER - 14), 8, rgb(L.inkDim), 0.4);
    }
}

/// a fejléc dinamikus része: az aktuális preset neve
inline void drawHeaderDynamic(PanelController& pc) {
    textFont(pc.currentPresetName(), @"AvenirNext-DemiBold", CGPointMake(CGRectGetMidX(kPreset), 18), 10.5, rgb(0xEDE3CF), 1.2);
}

/// panel-váz: méret, fiók, fejléc-gombok
inline std::unique_ptr<PanelController> makePanel(AudioUnit au, const char* key) {
    auto pc = std::make_unique<PanelController>(au, CGSizeMake(W, H), key);
    pc->drawerHeight = DRAWER;
    pc->loadDrawerState();
    pc->hotspots = {
        {kPrev, [](PanelController& p, CGPoint) { p.selectPreset(p.currentPresetIndex() - 1); }},
        {kNext, [](PanelController& p, CGPoint) { p.selectPreset(p.currentPresetIndex() + 1); }},
        {kPreset, [](PanelController& p, CGPoint) { p.popupPresetMenu(kPreset); }},
        {kDrawerBtn, [](PanelController& p, CGPoint) { p.toggleDrawer(); }},
    };
    return pc;
}

/// a modellnév a pajzs-VU számlapján (Futura Bold, alatta kis "VU")
inline VUStyle dialStyle(NSString* model, bool grScale = false, int face = 3) {
    VUStyle vu{4, face, grScale, model};
    vu.legendFont = @"Futura-Bold"; vu.legendSize = 15; vu.legendKern = 3; vu.subLegend = grScale ? @"GAIN REDUCTION" : @"VU";
    return vu;
}

} // namespace eki::ui::rack
