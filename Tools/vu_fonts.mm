// Összehasonlító lap: a VU-számlap modellnév-felirata különböző (macOS rendszer-) fontokkal
//   clang++ -std=c++20 -fobjc-arc -IShared Tools/vu_fonts.mm -framework AppKit -framework AudioToolbox -framework CoreText -o build/vu_fonts && build/vu_fonts build/vu-fonts.png OPTEKI
#include "EKIPanel.h"
using namespace eki::ui;

int main(int argc, char** argv) {
    @autoreleasepool {
        NSString* name = argc > 2 ? [NSString stringWithUTF8String:argv[2]] : @"OPTEKI";
        struct F { NSString* font; CGFloat size, kern; NSString* label; };
        std::vector<F> fonts = {
            {@"Futura-Bold", 15, 3, @"1  Futura Bold"},
            {@"Futura-Medium", 15, 4, @"2  Futura Medium"},
            {@"DINAlternate-Bold", 17, 3, @"3  DIN Alternate"},
            {@"DINCondensed-Bold", 20, 4, @"4  DIN Condensed"},
            {@"Optima-Bold", 16, 3.5, @"5  Optima Bold"},
            {@"AvenirNext-DemiBold", 15, 4, @"6  Avenir Next Demi"},
            {@"GillSans-SemiBold", 17, 4, @"7  Gill Sans"},
            {@"Didot-Bold", 17, 3, @"8  Didot"},
            {@"Copperplate-Bold", 16, 2.5, @"9  Copperplate"},
        };
        const int cols = 3, rows = int((fonts.size() + cols - 1) / cols);
        const CGFloat cw = 300, ch = 210, W = cols * cw, H = rows * ch;
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:NSInteger(W * 2) pixelsHigh:NSInteger(H * 2)
                                    bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        NSGraphicsContext* g = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        CGContextRef c = g.CGContext;
        CGContextTranslateCTM(c, 0, H * 2); CGContextScaleCTM(c, 2, -2);
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:c flipped:YES]];
        [[[NSGradient alloc] initWithStartingColor:rgb(0xDCDEE0) endingColor:rgb(0xBFC2C5)] drawInRect:NSMakeRect(0, 0, W, H) angle:-90];
        Theme t; t.silk = 0x26272A; t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold"; theme() = t;
        for (size_t i = 0; i < fonts.size(); ++i) {
            const CGFloat x = (i % cols) * cw, y = (i / cols) * ch;
            VUStyle st{3, 3, false, name};
            st.legendFont = fonts[i].font; st.legendSize = fonts[i].size; st.legendKern = fonts[i].kern; st.subLegend = @"VU";
            drawVU(NSMakeRect(x + 30, y + 20, 240, 152), -4.0f, st);
            text(fonts[i].label, CGPointMake(x + cw / 2, y + 192), 11, rgb(0x26272A), true, 0.5);
        }
        [NSGraphicsContext restoreGraphicsState];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:argv[1]] atomically:YES];
        printf("Mentve: %s\n", argv[1]);
    }
}
