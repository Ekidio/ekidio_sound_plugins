// A telepített AU saját panelját PNG-be rendereli (mintha egy host nyitná meg).
//   clang++ -std=c++20 -fobjc-arc Tools/ui_snapshot.mm -framework AppKit -framework AudioToolbox -o build/ui_snapshot
//   build/ui_snapshot aufx Opek Ekid build/OPTEKI-panel.png [paraméter=érték ...]
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#include <cmath>
#include <vector>
#include <string>

static double ph = 0;
static OSStatus input(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n, AudioBufferList* io) {
    for (UInt32 i = 0; i < n; ++i) { float v = 0.5f * sin(ph); ph += 2 * M_PI * 220 / 48000; for (UInt32 c = 0; c < io->mNumberBuffers; ++c) ((float*)io->mBuffers[c].mData)[i] = v; }
    return noErr;
}
static OSType fourcc(const char* s) { return OSType(s[0]) << 24 | OSType(s[1]) << 16 | OSType(s[2]) << 8 | OSType(s[3]); }

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 5) { printf("használat: type subtype manu out.png [id=érték ...]\n"); return 2; }
        [NSApplication sharedApplication];
        AudioComponentDescription d{fourcc(argv[1]), fourcc(argv[2]), fourcc(argv[3]), 0, 0};
        AudioComponent comp = AudioComponentFindNext(nullptr, &d);
        AudioUnit au; if (!comp || AudioComponentInstanceNew(comp, &au)) { printf("AU nem nyitható\n"); return 1; }
        AudioStreamBasicDescription f{48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        AURenderCallbackStruct cb{input, nullptr};
        AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        AudioUnitInitialize(au);
        for (int i = 5; i < argc; ++i) { int id; float v; if (sscanf(argv[i], "%d=%f", &id, &v) == 2) AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, v, 0); }
        std::vector<float> L(512), R(512);
        struct { AudioBufferList abl; AudioBuffer extra; } io;
        AudioTimeStamp ts{}; ts.mFlags = kAudioTimeStampSampleTimeValid;
        for (int b = 0; b < 300; ++b) {
            io.abl.mNumberBuffers = 2; io.abl.mBuffers[0] = {1, 2048, L.data()}; io.abl.mBuffers[1] = {1, 2048, R.data()};
            AudioUnitRenderActionFlags fl = 0; AudioUnitRender(au, &fl, &ts, 0, 512, &io.abl); ts.mSampleTime += 512;
        }
        AudioUnitCocoaViewInfo* info = (AudioUnitCocoaViewInfo*)malloc(sizeof(AudioUnitCocoaViewInfo));
        UInt32 sz = sizeof(AudioUnitCocoaViewInfo);
        OSStatus e = AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &sz);
        if (e) { printf("nincs CocoaUI (%d)\n", (int)e); return 1; }
        NSURL* url = (__bridge_transfer NSURL*)info->mCocoaAUViewBundleLocation;
        NSString* cls = (__bridge_transfer NSString*)info->mCocoaAUViewClass[0];
        printf("Bundle: %s\nFactory: %s\n", url.path.UTF8String, cls.UTF8String);
        NSBundle* bundle = [NSBundle bundleWithURL:url];
        Class fc = [bundle classNamed:cls];
        if (!fc) { printf("factory osztály nem található\n"); return 1; }
        id<AUCocoaUIBase> factory = [[fc alloc] init];
        NSView* view = [factory uiViewForAudioUnit:au withSize:NSZeroSize];
        printf("Nézet mérete: %.0f x %.0f\n", view.frame.size.width, view.frame.size.height);
        NSWindow* win = [[NSWindow alloc] initWithContentRect:view.frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        [win setContentView:view];
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
        if (getenv("EKI_UI_INTERACT")) {
            // valódi egéresemények a panel ablakának: (x, y) panel-koordináta, y lefelé
            auto send = [&](NSEventType t, CGFloat x, CGFloat y, NSInteger clicks) {
                NSPoint wp = NSMakePoint(x, view.frame.size.height - y);
                NSEvent* ev = [NSEvent mouseEventWithType:t location:wp modifierFlags:0 timestamp:0 windowNumber:win.windowNumber
                                                  context:nil eventNumber:0 clickCount:clicks pressure:1];
                if (t == NSEventTypeLeftMouseDown) [view mouseDown:ev];
                else if (t == NSEventTypeLeftMouseDragged) [view mouseDragged:ev];
                else [view mouseUp:ev];
            };
            auto get = [&](int id) { AudioUnitParameterValue v; AudioUnitGetParameter(au, id, kAudioUnitScope_Global, 0, &v); return v; };
            printf("előtte: PR %.1f  GAIN %.1f  AUTO %.0f  MODE %.0f  METER %.0f\n", get(0), get(1), get(9), get(2), get(6));
            send(NSEventTypeLeftMouseDown, 615, 150, 1);                 // PEAK REDUCTION: 55 px fel → +25
            for (int i = 1; i <= 11; ++i) send(NSEventTypeLeftMouseDragged, 615, 150 - 5 * i, 1);
            send(NSEventTypeLeftMouseUp, 615, 95, 1);
            send(NSEventTypeLeftMouseDown, 165, 150, 1);                 // GAIN: 44 px fel → +10 dB
            for (int i = 1; i <= 4; ++i) send(NSEventTypeLeftMouseDragged, 165, 150 - 11 * i, 1);
            send(NSEventTypeLeftMouseUp, 165, 106, 1);
            send(NSEventTypeLeftMouseDown, 100, 318, 1); send(NSEventTypeLeftMouseUp, 100, 318, 1);   // AUTO GAIN ki
            send(NSEventTypeLeftMouseDown, 682, 320, 1); send(NSEventTypeLeftMouseUp, 682, 320, 1);   // LIMIT
            send(NSEventTypeLeftMouseDown, 478, 290, 1); send(NSEventTypeLeftMouseUp, 478, 290, 1);   // METER "+10" felirat
            printf("utána:  PR %.1f  GAIN %.1f  AUTO %.0f  MODE %.0f  METER %.0f\n", get(0), get(1), get(9), get(2), get(6));
            send(NSEventTypeLeftMouseDown, 165, 150, 2); send(NSEventTypeLeftMouseUp, 165, 150, 2);   // dupla katt: alapérték
            printf("GAIN dupla katt után: %.1f\n", get(1));
            [view display];
        }
        if (const char* clicks = getenv("EKI_UI_CLICKS")) {
            // "x,y;x,y;..." kattintások, utána az összes paraméter kiírása
            std::string str(clicks);
            size_t pos = 0;
            while (pos < str.size()) {
                size_t end = str.find(';', pos); if (end == std::string::npos) end = str.size();
                float x, y; if (sscanf(str.substr(pos, end - pos).c_str(), "%f,%f", &x, &y) == 2) {
                    NSPoint wp = NSMakePoint(x, view.frame.size.height - y);
                    for (NSEventType t : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
                        NSEvent* ev = [NSEvent mouseEventWithType:t location:wp modifierFlags:0 timestamp:0 windowNumber:win.windowNumber
                                                          context:nil eventNumber:0 clickCount:1 pressure:1];
                        if (t == NSEventTypeLeftMouseDown) [view mouseDown:ev]; else [view mouseUp:ev];
                    }
                }
                pos = end + 1;
            }
            UInt32 sz2 = 0; AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz2, nullptr);
            for (UInt32 id = 0; id < sz2 / sizeof(AudioUnitParameterID); ++id) {
                AudioUnitParameterValue v; AudioUnitGetParameter(au, id, kAudioUnitScope_Global, 0, &v); printf("p%u=%.1f ", id, v);
            }
            printf("\n");
            [view display];
        }
        if (const char* rs = getenv("EKI_UI_RESIZE")) {
            // a jobb alsó sarok megfogása és húzása dx képponttal (nézet-koordinátában)
            const float dx = atof(rs);
            NSSize s0 = view.frame.size;
            auto ev = [&](NSEventType t, CGFloat x, CGFloat y, NSInteger clicks) {
                return [NSEvent mouseEventWithType:t location:NSMakePoint(x, view.frame.size.height - y) modifierFlags:0 timestamp:0
                                      windowNumber:win.windowNumber context:nil eventNumber:0 clickCount:clicks pressure:1];
            };
            [view mouseDown:ev(NSEventTypeLeftMouseDown, s0.width - 5, s0.height - 5, 1)];
            [view mouseDragged:ev(NSEventTypeLeftMouseDragged, s0.width - 5 + dx, s0.height - 5, 1)];
            [view mouseUp:ev(NSEventTypeLeftMouseUp, s0.width - 5 + dx, s0.height - 5, 1)];
            printf("Méretezés: %.0fx%.0f → %.0fx%.0f\n", s0.width, s0.height, view.frame.size.width, view.frame.size.height);
            [win setContentSize:view.frame.size];
            [view display];
        }
        NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:argv[4]] atomically:YES];
        printf("Mentve: %s\n", argv[4]);
        [win setContentView:nil];
        AudioUnitUninitialize(au); AudioComponentInstanceDispose(au);
    }
    return 0;
}
