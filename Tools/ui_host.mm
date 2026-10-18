// Valódi ablakos mini host: a telepített AU panelját igazi NSWindow-ba teszi, és az AppKit eseménysorán
// át (NSApp postEvent) kattint — így ugyanazon az úton megy, mint egy DAW-ban.
//   clang++ -std=c++20 -fobjc-arc Tools/ui_host.mm -framework AppKit -framework AudioToolbox -o build/ui_host
//   build/ui_host aufx Opek Ekid "x,y;x,y"
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#include <string>
#include <vector>
static OSType fourcc(const char* s) { return OSType(s[0]) << 24 | OSType(s[1]) << 16 | OSType(s[2]) << 8 | OSType(s[3]); }

int main(int argc, char** argv) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AudioComponentDescription d{fourcc(argv[1]), fourcc(argv[2]), fourcc(argv[3]), 0, 0};
        AudioComponent comp = AudioComponentFindNext(nullptr, &d);
        AudioUnit au; AudioComponentInstanceNew(comp, &au);
        AudioUnitInitialize(au);
        AudioUnitCocoaViewInfo info; UInt32 sz = sizeof(info);
        AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &sz);
        NSBundle* b = [NSBundle bundleWithURL:(__bridge_transfer NSURL*)info.mCocoaAUViewBundleLocation];
        id<AUCocoaUIBase> f = [[[b classNamed:(__bridge_transfer NSString*)info.mCocoaAUViewClass[0]] alloc] init];
        NSView* view = [f uiViewForAudioUnit:au withSize:NSZeroSize];
        const bool daw = getenv("EKI_DAW_WINDOW") != nullptr;   // DAW-szerű: a tartalom a címsor alá nyúlik, háttérrel mozgatható
        NSWindow* win = [[NSWindow alloc] initWithContentRect:view.frame
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | (daw ? NSWindowStyleMaskFullSizeContentView : 0)
            backing:NSBackingStoreBuffered defer:NO];
        if (daw) { win.titlebarAppearsTransparent = YES; win.titleVisibility = NSWindowTitleHidden; win.movableByWindowBackground = YES; }
        // mint egy DAW: a nézet egy tárolóba kerül, és az ablak követi a nézet méretét
        NSView* container = [[NSView alloc] initWithFrame:view.frame];
        [container addSubview:view];
        win.contentView = container;
        view.postsFrameChangedNotifications = YES;
        [[NSNotificationCenter defaultCenter] addObserverForName:NSViewFrameDidChangeNotification object:view queue:nil usingBlock:^(NSNotification*) {
            [win setContentSize:view.frame.size];
            [view setFrameOrigin:NSZeroPoint];
            printf("  [host] a nézet új mérete: %.0fx%.0f\n", view.frame.size.width, view.frame.size.height);
        }];
        [win makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        std::string clicks = argc > 4 ? argv[4] : "";
        auto preset = [&]() { AUPreset p{}; UInt32 s2 = sizeof(p); AudioUnitGetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, &s2);
                              std::string n = p.presetName ? [(__bridge NSString*)p.presetName UTF8String] : "?"; if (p.presetName) CFRelease(p.presetName); return n; };
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 600 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            printf("kiinduló preset: %s, nézet: %.0fx%.0f\n", preset().c_str(), view.frame.size.width, view.frame.size.height);
            size_t pos = 0; int delay = 0;
            while (pos < clicks.size()) {
                size_t end = clicks.find(';', pos); if (end == std::string::npos) end = clicks.size();
                float x, y; sscanf(clicks.substr(pos, end - pos).c_str(), "%f,%f", &x, &y);
                pos = end + 1;
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (delay += 400) * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    NSPoint lp = [view convertPoint:NSMakePoint(x, y) toView:nil];   // a nézet (flipped) pontja → ablak-koordináta
                    for (NSEventType t : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
                        NSEvent* ev = [NSEvent mouseEventWithType:t location:lp modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime
                                                     windowNumber:win.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1];
                        [NSApp sendEvent:ev];
                    }
                    printf("kattintás (%.0f,%.0f) → preset: %s, nézet: %.0fx%.0f, ablak: %.0fx%.0f\n", x, y, preset().c_str(),
                           view.frame.size.width, view.frame.size.height, win.contentView.frame.size.width, win.contentView.frame.size.height);
                });
            }
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (delay + 800) * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
        });
        [app run];
    }
}
