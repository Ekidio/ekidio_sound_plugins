// EKIUpdate.h — frissítés-figyelő az EKIDIO SOUND panelekhez.
// A panel megnyitásakor (legfeljebb 12 óránként) a háttérben lekéri a weboldal version.json fájlját:
//   {"version": "1.0.2", "url": "https://ekidio.github.io/ekidio_sound_plugins/"}
// Ha az ott lévő verzió újabb a pluginénál (EKI_VERSION, a build.sh adja), a fejlécben megjelenik az UPDATE jelzés.
// Adatot nem küld; hiba / nincs net esetén csendben nem csinál semmit. Csak a főszálon és a hálózati szálon fut, a hangot nem érinti.
// Teszteléshez: EKI_UPDATE_URL környezeti változó (pl. file:///…/version.json) — ilyenkor nincs időkorlát.
#pragma once
#import <Cocoa/Cocoa.h>
#include <atomic>

#ifndef EKI_VERSION
#define EKI_VERSION "0.0.0"
#endif

namespace eki::ui::update {

inline NSString* const kDefaultURL = @"https://ekidio.github.io/ekidio_sound_plugins/version.json";
inline NSString* const kKeyLatest = @"EKIDIO.update.latest";
inline NSString* const kKeyPage = @"EKIDIO.update.url";
inline NSString* const kKeyChecked = @"EKIDIO.update.checkedAt";
constexpr NSTimeInterval kInterval = 12 * 3600;

/// változik, amikor új információ érkezett (a panel ebből tudja, hogy újra kell rajzolni)
inline std::atomic<int>& generation() { static std::atomic<int> g{0}; return g; }

/// "1.2.10" > "1.2.9" — számonkénti összehasonlítás
inline bool isNewer(NSString* a, NSString* b) {
    NSArray<NSString*>* x = [a componentsSeparatedByString:@"."];
    NSArray<NSString*>* y = [b componentsSeparatedByString:@"."];
    for (NSUInteger i = 0; i < MAX(x.count, y.count); ++i) {
        const NSInteger p = i < x.count ? x[i].integerValue : 0, q = i < y.count ? y[i].integerValue : 0;
        if (p != q) return p > q;
    }
    return false;
}

/// az elérhető újabb verzió (nil, ha nincs)
inline NSString* available() {
    NSString* latest = [[NSUserDefaults standardUserDefaults] stringForKey:kKeyLatest];
    return latest && isNewer(latest, @EKI_VERSION) ? latest : nil;
}

/// a letöltési oldal
inline NSURL* pageURL() {
    NSString* u = [[NSUserDefaults standardUserDefaults] stringForKey:kKeyPage];
    NSURL* url = u ? [NSURL URLWithString:u] : nil;
    return [url.scheme isEqualToString:@"https"] ? url : [NSURL URLWithString:@"https://ekidio.github.io/ekidio_sound_plugins/"];
}

inline void openPage() { [[NSWorkspace sharedWorkspace] openURL:pageURL()]; }

/// háttér-ellenőrzés (legfeljebb kInterval-onként, processzenként egyszer)
inline void check() {
    static bool started = false;
    if (started) return;
    started = true;
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    const char* env = getenv("EKI_UPDATE_URL");
    if (!env && [NSDate timeIntervalSinceReferenceDate] - [d doubleForKey:kKeyChecked] < kInterval) return;
    NSURL* url = [NSURL URLWithString:env ? @(env) : kDefaultURL];
    if (!url) return;
    NSURLRequest* req = [NSURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:15];
    [[[NSURLSession sharedSession] dataTaskWithRequest:req completionHandler:^(NSData* data, NSURLResponse*, NSError* err) {
        if (err || !data) return;
        id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if (![json isKindOfClass:[NSDictionary class]]) return;
        id v = json[@"version"], u = json[@"url"];
        if (![v isKindOfClass:[NSString class]]) return;
        dispatch_async(dispatch_get_main_queue(), ^{
            NSUserDefaults* ud = [NSUserDefaults standardUserDefaults];
            [ud setObject:v forKey:kKeyLatest];
            if ([u isKindOfClass:[NSString class]]) [ud setObject:u forKey:kKeyPage];
            [ud setDouble:[NSDate timeIntervalSinceReferenceDate] forKey:kKeyChecked];
            generation()++;
        });
    }] resume];
}

} // namespace eki::ui::update
