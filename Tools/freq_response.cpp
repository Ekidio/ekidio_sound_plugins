// Frekvencia-átvitel mérése kompresszió nélkül (kis szintű impulzus → lineáris tartomány), MIX 100/60/30
//   clang++ -std=c++20 -O2 -IShared -IPlugins/OPTEKI -IPlugins/VARIEKI -IPlugins/FETEKI -IPlugins/BUSEKI Tools/freq_response.cpp -o build/freq_response
#include "OptekiDSP.h"
#include "VariekiDSP.h"
#include "FetekiDSP.h"
#include "BusekiDSP.h"
#include "ApekiDSP.h"
#include "DbekiDSP.h"
#include "TubekiDSP.h"
#include "FairekiDSP.h"
#include <cstdio>
#include <vector>
#include <complex>
#include <functional>
using namespace eki;

template <class DSP, class P>
std::vector<float> impulse(float sr, P p) {
    DSP d; d.prepare(sr);
    const int N = 16384; std::vector<float> in(N, 0.0f), out(N), o2(N);
    in[0] = 0.001f;
    const float* ip[2] = {in.data(), in.data()}; float* op[2] = {out.data(), o2.data()};
    for (int i = 0; i < N; i += 256) {
        const float* ii[2] = {ip[0] + i, ip[1] + i}; float* oo[2] = {op[0] + i, op[1] + i};
        d.process(ii, oo, 2, 256, p);
    }
    for (auto& v : out) v /= 0.001f;
    return out;
}
double magDb(const std::vector<float>& h, double f, double sr) {
    std::complex<double> s = 0;
    for (size_t n = 0; n < h.size(); ++n) s += double(h[n]) * std::polar(1.0, -2 * M_PI * f * n / sr);
    return 20 * log10(std::abs(s) + 1e-12);
}
template <class DSP, class P>
void report(const char* name, float sr, std::function<P(float)> mk) {
    const double fs[] = {30, 100, 1000, 3000, 6000, 8000, 10000, 12000, 15000, 18000, 20000};
    printf("%-8s %5.1fk  ", name, sr / 1000);
    for (double f : fs) printf("%7.0f", f);
    printf("  Hz\n");
    for (float mix : {100.f, 60.f, 30.f}) {
        auto h = impulse<DSP, P>(sr, mk(mix));
        printf("   MIX %3.0f%%      ", mix);
        for (double f : fs) printf("%7.2f", magDb(h, f, sr));
        printf("  dB\n");
    }
}
int main() {
    for (float sr : {44100.f, 48000.f, 96000.f}) {
        report<OptekiDSP, OptekiParams>("OPTEKI", sr, [](float m) { OptekiParams p; p.peakReduction = 0; p.autoGain = 0; p.analog = 50; p.mix = m; return p; });
        report<FetekiDSP, FetekiParams>("FETEKI", sr, [](float m) { FetekiParams p; p.inputDb = -10; p.autoGain = 0; p.analog = 50; p.mix = m; return p; });
        report<VariekiDSP, VariekiParams>("VARIEKI", sr, [](float m) { VariekiParams p; p.thresholdDb = 0; p.autoGain = 0; p.analog = 35; p.mix = m; return p; });
        report<FairekiDSP, FairekiParams>("FAIREKI", sr, [](float m) { FairekiParams p; p.thresholdDb = 0; p.autoGain = 0; p.analog = 60; p.mix = m; return p; });
        report<TubekiDSP, TubekiParams>("TUBEKI", sr, [](float m) { TubekiParams p; p.thresholdDb = 0; p.autoGain = 0; p.analog = 50; p.mix = m; return p; });
        report<DbekiDSP, DbekiParams>("DBEKI", sr, [](float m) { DbekiParams p; p.thresholdDb = 0; p.autoGain = 0; p.analog = 40; p.mix = m; return p; });
        report<ApekiDSP, ApekiParams>("APEKI", sr, [](float m) { ApekiParams p; p.thresholdDb = 0; p.autoGain = 0; p.analog = 50; p.mix = m; return p; });
        report<BusekiDSP, BusekiParams>("BUSEKI", sr, [](float m) { BusekiParams p; p.thresholdDb = 0; p.autoGain = 0; p.analog = 45; p.mix = m; return p; });
        printf("\n");
    }
}
