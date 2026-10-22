// PULTEKI saját panel — a user választása (2026-10-23): kékesszürke lemez, krém gravírozás, bordázott gain-gombok, mutatós
//   frekvencia-kapcsolók, JOBB oldalon kicsi pajzs-VU (kimenet) minden oldalon, balra a modul IN kapcsolója, a kezelők az IN és a VU
//   között szimmetrikusan, középen (2026-10-23); a számlap alapból
//   piros, a fiókban választható (fekete / piros / éjkék, kijelzési beállítás). Felül a kapcsolósor (PAGE) választja, melyik modult látod:
//   PROGRAM EQ · MID-RANGE EQ · FILTER — a fülek melletti LED a modul IN állását mutatja. Jobbra állandó: név, OUTPUT, POWER
//   (piros ékkő-lámpa, csak a lencse világít). Fiók: ANALOG (csőszín, alapban 0), AUTO GAIN (alapban KI).
#include "EKIRack.h"

using namespace eki::ui;
using namespace eki::ui::rack;

namespace {

enum : AudioUnitParameterID { kLowBoost, kLowAtten, kLowFreq, kHighBoost, kBandwidth, kHighFreq, kHighAtten, kAttenSel, kEqpIn,
                              kMidLowG, kMidLowF, kDipG, kDipF, kMidHighG, kMidHighF, kMeqIn,
                              kLowCut, kHighCut, kHlfIn,
                              kOutput, kAutoGain, kAnalog, kPage, kVuColour, kAutoMakeup, kVU };

constexpr int INK = 0xEEE7D6, INK_DIM = 0xBFC6CF, ACCENT = 0xF0C060, JEWEL = 0xE0302A;
const Look LOOK = {
    0x56677C, 0x4C5C70, 0x415063, 0.55,   // kékesszürke (a user választása)
    {0xE6E8EB, 0x8E9196, 0x4C5055, 0x72767B, 0xC2C5C9},
    0x2E3034, 0x34373B, 0x26282C, 1.0,
    INK, INK_DIM, 0.16, 0.30,
};
const NSRect kVuRect = NSMakeRect(626, 116, 140, 100);   // kicsi pajzs-VU, JOBBRA (minden oldalon)
constexpr CGFloat MID = 345;    // PROGRAM EQ: a kezelők tengelye (az IN és a VU közötti tér közepe)
constexpr CGFloat MID1 = 320;   // MID-RANGE EQ: a bal szél és a VU közötti tér közepe (az IN a VU alatt)
constexpr CGFloat MID2 = 410;   // FILTER: a VU a két kapcsoló között, a teljes lemezrész közepén
constexpr CGFloat SPLIT = 790, NAME_X = 868, TAB_Y = 74;
constexpr CGFloat kTabX[3] = {256, 345, 433};   // a kapcsolósor gombjainak közepe (RadioRow, rowWiden 2.6, közép = MID)

int page(const PanelController& pc) { return std::clamp(int(pc.get(kPage) + 0.5f), 0, 2); }

void bracket(CGFloat x0, CGFloat x1, CGFloat y, NSString* name, int ink) {
    line(CGPointMake(x0, y - 5), CGPointMake(x0, y), 1, rgb(ink, 0.7));
    line(CGPointMake(x1, y - 5), CGPointMake(x1, y), 1, rgb(ink, 0.7));
    NSFont* f = [NSFont fontWithName:@"AvenirNext-DemiBold" size:8];
    const CGFloat tw = [name sizeWithAttributes:@{NSFontAttributeName: f, NSKernAttributeName: @2}].width + 14;
    const CGFloat m = (x0 + x1) / 2;
    line(CGPointMake(x0, y), CGPointMake(m - tw / 2, y), 1, rgb(ink, 0.7));
    line(CGPointMake(m + tw / 2, y), CGPointMake(x1, y), 1, rgb(ink, 0.7));
    textFont(name, @"AvenirNext-DemiBold", CGPointMake(m, y), 8, rgb(ink), 2);
}

void drawStatic(PanelController& pc) {
    drawChrome(pc, LOOK, nil, {{300, @"tube colour (0 = extra clean)"}, {500, @"output stays at the input level"}, {720, @"meter face"}});
    line(CGPointMake(SPLIT, 66), CGPointMake(SPLIT, 300), 1, rgb(0x000000, 0.28));
    line(CGPointMake(SPLIT + 1, 66), CGPointMake(SPLIT + 1, 300), 1, rgb(0xFFFFFF, 0.06));
    line(CGPointMake(30, 96), CGPointMake(SPLIT - 16, 96), 1, rgb(0x000000, 0.22));
    line(CGPointMake(30, 97), CGPointMake(SPLIT - 16, 97), 1, rgb(0xFFFFFF, 0.06));
    textFont(@"PULTEKI", @"Futura-Bold", CGPointMake(NAME_X, 84), 21, rgb(INK), 3);
    textFont(@"PASSIVE TUBE", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 106), 6.5, rgb(INK_DIM), 1.6);
    textFont(@"EQUALIZER SET", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 116), 6.5, rgb(INK_DIM), 1.6);
    line(CGPointMake(NAME_X - 54, 128), CGPointMake(NAME_X + 54, 128), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X - 54, 128), CGPointMake(NAME_X - 54, 123), 1, rgb(INK, 0.7));
    line(CGPointMake(NAME_X + 54, 128), CGPointMake(NAME_X + 54, 123), 1, rgb(INK, 0.7));
}

void drawFace(PanelController& pc) {
    drawHeaderDynamic(pc);
    const int pg = page(pc);
    static NSString* const names[3] = {@"PROGRAM EQUALIZER", @"MID-RANGE EQUALIZER", @"FILTER"};
    bracket(28, SPLIT - 14, 304, names[pg], INK);
    auto title = [pg](CGFloat x, NSString* n) { textFont(n, @"AvenirNext-DemiBold", CGPointMake(x, pg == 2 ? 140 : pg == 1 ? 130 : 112), 9, rgb(INK), 3); };   // a user rajza szerint (2026-10-23)
    if (pg == 0) { title(MID - 138, @"LOW"); title(MID + 138, @"HIGH"); }
    if (pg == 1) { title(MID1 - 180, @"PEAK"); title(MID1, @"DIP"); title(MID1 + 180, @"PEAK"); }
    if (pg == 2) { title(MID2 - 220, @"LOW CUT-OFF"); title(MID2 + 220, @"HIGH CUT-OFF"); }
    // kimeneti VU (pajzs), a számlap színe a fiókban: fekete / piros / éjkék
    static const int faces[3] = {5, 6, 7};
    VUStyle v = dialStyle(@"PULTEKI", false, faces[std::clamp(int(pc.get(kVuColour) + 0.5f), 0, 2)]);
    v.legendSize = 11; v.legendKern = 2.2;
    // PROGRAM EQ és MID-RANGE EQ: jobbra fent (alatta az ATTEN SEL, ill. az IN); FILTER: középen, a két nagy kapcsoló között
    drawVU(pg == 2 ? NSMakeRect(MID2 - 63, 124, 126, 90) : kVuRect,
           pc.processing ? pc.get(kVU) : -20.0f, v);
    // a fülek melletti LED-ek: a modul IN állásban van-e
    const AudioUnitParameterID ins[3] = {kEqpIn, kMeqIn, kHlfIn};
    for (int i = 0; i < 3; ++i) drawLed(CGPointMake(kTabX[i] + 44, TAB_Y - 10), pc.get(ins[i]) >= 0.5f ? 0.9f : 0.0f, JEWEL, nil, 2.6);
    drawJewel(CGPointMake(NAME_X, 278), pc.processing ? 1.0f : 0.0f, JEWEL, nil, 8);   // POWER: csak a lencse világít
    textFont(@"POWER", @"AvenirNext-DemiBold", CGPointMake(NAME_X, 300), 6.5, rgb(INK_DIM), 2);
    if (pc.drawerOpen && pc.get(kAutoGain) >= 0.5f)   // AUTO GAIN (fiók): a ténylegesen beállt kiegyenlítés
        textFont([NSString stringWithFormat:@"%+.1f dB", pc.get(kAutoMakeup)], @"AvenirNext-DemiBold", CGPointMake(500, H + 92), 8, rgb(ACCENT), 1);
}

std::unique_ptr<PanelController> buildPultekiPanel(AudioUnit au) {
    Theme t;
    t.silk = INK; t.tick = INK; t.accent = ACCENT; t.led = 0xFF5A40;
    t.minorTicks = 1; t.ledStyle = 1; t.valueGlow = false; t.pointer = 0xF2EEE6;
    t.buttonTop = 0x2E3946; t.buttonBot = 0x10151B; t.buttonText = 0xE6ECF2;
    t.fontRegular = @"AvenirNext-Medium"; t.fontBold = @"AvenirNext-DemiBold";
    t.knob = Fluted; t.coloredLine = false;
    theme() = t;
    auto pc = makePanel(au, "pulteki");
    pc->watched = {{kAutoMakeup, 0}, {kVU, -20}, {kPage, 0}, {kVuColour, 0}, {kEqpIn, 1}, {kMeqIn, 1}, {kHlfIn, 1}};
    pc->drawStatic = drawStatic;
    pc->drawFace = drawFace;
    auto& cs = pc->controls;
    NSArray* zeroTen = ticks(0, 1, 11);
    auto onPage = [](int pg) { return std::function<bool(const PanelController&)>([pg](const PanelController& p) { return page(p) == pg; }); };
    auto gain = [&](AudioUnitParameterID id, CGFloat x, CGFloat y, NSString* name, int pg, CGFloat r = 31) {
        cs.push_back({Control::Knob, id, {x, y}, r, name, zeroTen, 0, 10, 0}); cs.back().labelSize = 9.5; cs.back().knobStyle = Fluted;
        cs.back().visibleIf = onPage(pg);
    };
    // frekvencia-kapcsolók: mutatós, lépcsős (spread = félszög)
    auto sel = [&](AudioUnitParameterID id, CGFloat x, CGFloat y, NSString* name, NSArray* opts, float def, CGFloat spread, int pg, CGFloat r = 18) {
        cs.push_back({Control::SteppedKnob, id, {x, y}, r, name, opts, 0, float(opts.count - 1), def});
        cs.back().labelSize = 8.5; cs.back().spread = spread; cs.back().knobStyle = Pointer; cs.back().visibleIf = onPage(pg);
    };
    auto inSwitch = [&](AudioUnitParameterID id, CGFloat x, CGFloat y, int pg) {
        cs.push_back({Control::Toggle, id, {x, y}, 1.0, @"IN", @[@"OUT", @"IN"], 0, 1, 1}); cs.back().visibleIf = onPage(pg);
    };

    // ---- felső kapcsolósor: melyik modult látod ----
    cs.push_back({Control::RadioRow, kPage, {MID, TAB_Y}, 1.0, nil, @[@"PROGRAM EQ", @"MID-RANGE EQ", @"FILTER"], 0, 2, 0});
    cs.back().rowWiden = 2.6;

    // ---- PROGRAM EQ ---- (a frekvencia-kapcsolók a BOOST / ATTEN párok alatt középen, a BANDWIDTH a kettő között — a user kérése)
    // két pár a MID tengely körül (párok közepe ±138, a páron belül ±58); alul a kapcsolók a párok alatt, a BANDWIDTH középen
    gain(kLowBoost, MID - 196, 166, @"BOOST", 0);
    gain(kLowAtten, MID - 80, 166, @"ATTEN", 0);
    gain(kHighBoost, MID + 80, 166, @"BOOST", 0);
    gain(kHighAtten, MID + 196, 166, @"ATTEN", 0);
    sel(kAttenSel, NSMidX(kVuRect), 262, @"ATTEN SEL kHz", @[@"5", @"10", @"20"], 1, 55, 0, 17); cs.back().labelDy = 26;
    inSwitch(kEqpIn, 46, 256, 0);
    sel(kLowFreq, MID - 138, 254, @"LOW FREQUENCY Hz", @[@"20", @"30", @"60", @"100"], 1, 75, 0, 22);
    cs.push_back({Control::Knob, kBandwidth, {MID, 252}, 22, @"BANDWIDTH", @[@"SHARP", @"", @"", @"", @"", @"BROAD"], 0, 10, 5});
    cs.back().labelSize = 8.5; cs.back().knobStyle = Fluted; cs.back().visibleIf = onPage(0);
    sel(kHighFreq, MID + 138, 254, @"HIGH FREQUENCY kHz", @[@"3", @"4", @"5", @"8", @"10", @"12", @"16"], 4, 120, 0, 22);

    // ---- MID-RANGE EQ: három (kapcsoló + gomb) pár a MID tengely körül, 180-as osztással ----
    inSwitch(kMeqIn, NSMidX(kVuRect), 257, 1);   // a VU alatt (mint a PROGRAM EQ oldalon az ATTEN SEL)
    sel(kMidLowF, MID1 - 224, 198, @"FREQ Hz", @[@"200", @"300", @"500", @"700", @"1k"], 2, 100, 1, 18);
    gain(kMidLowG, MID1 - 136, 194, @"PEAK", 1, 30);
    sel(kDipF, MID1 - 44, 198, @"FREQ kHz", @[@".2", @"", @".5", @"", @"1", @"", @"2", @"", @"4", @"", @"7"], 4, 140, 1, 18);
    gain(kDipG, MID1 + 44, 194, @"DIP", 1, 30);
    sel(kMidHighF, MID1 + 136, 198, @"FREQ kHz", @[@"1.5", @"2", @"3", @"4", @"5"], 2, 100, 1, 18);
    gain(kMidHighG, MID1 + 224, 194, @"PEAK", 1, 30);

    // ---- FILTER: két kapcsoló a MID tengely körül ----
    inSwitch(kHlfIn, MID2, 250, 2);   // a VU alatt, középen (mint a MID-RANGE oldalon)
    sel(kLowCut, MID2 - 220, 206, @"Hz", @[@"OFF", @"50", @"80", @"100", @"150", @"250", @"500", @"750", @"1k", @"1.5k", @"2k"], 0, 140, 2, 34);
    sel(kHighCut, MID2 + 220, 206, @"kHz", @[@"1.5", @"2", @"3", @"4", @"5", @"6", @"8", @"10", @"12", @"15", @"OFF"], 10, 140, 2, 34);

    // ---- kimenet (minden oldalon) ----
    cs.push_back({Control::Knob, kOutput, {NAME_X, 194}, 28, @"OUTPUT", @[@"-12", @"-6", @"0", @"+6", @"+12"], -12, 12, 0,
                  ^NSString*(float x) { return [NSString stringWithFormat:@"%+.1f dB", x]; }});
    cs.back().labelSize = 8.5; cs.back().rim = 0xC4222C; cs.back().knobStyle = Machined;

    // ---- EKI fiók ----
    const CGFloat dy = H + 58;
    cs.push_back({Control::Knob, kAnalog, {300, dy}, 22, @"ANALOG", nil, 0, 100, 0,
                  ^NSString*(float x) { return [NSString stringWithFormat:@"%.0f %%", x]; }});
    cs.back().drawer = true; cs.back().labelSize = 9;
    cs.push_back({Control::LedButton, kAutoGain, {500, dy - 8}, 0.8, @"AUTO GAIN", nil, 0, 1, 0}); cs.back().drawer = true;
    cs.push_back({Control::RadioRow, kVuColour, {720, dy - 8}, 0.9, nil, @[@"BLACK", @"RED", @"BLUE"], 0, 2, 1}); cs.back().drawer = true; cs.back().rowWiden = 1.5;

    pc->lock(kOutput, kAutoGain);
    return pc;
}

} // namespace

EKI_DEFINE_PANEL_VIEW(buildPultekiPanel)
