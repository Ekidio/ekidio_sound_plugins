# TERVEK

## OPTEKI (kompresszor)
- [x] Hang: T4-szerű optikai cella (~10 ms felfutás, kétlépcsős, programfüggő elengedés), lágy térd, COMPRESS/LIMIT, HF EMPHASIS, cső + trafó (2x OS), VU-ballisztika, presetek
- [x] auval -strict PASS, telepítve
- [x] AUTO GAIN: automatikus gain-kompenzáció (mérve: RMS-eltérés ±0,2 dB minden PR/mód állásban)
- [ ] Meghallgatás DAW-ban, finomhangolás fül alapján
- [x] Saját panel (Cocoa view): GAIN + PEAK REDUCTION nagy gombok, VU-műszer (GR / +4 / +10), CELL lámpa, AUTO GAIN, ANALOG, HF EMPHASIS, METER, MIX, COMPRESS/LIMIT
- [ ] Panel kipróbálása DAW-ban (automáció-írás, méretezés), finomítás

## VARIEKI / FETEKI / BUSEKI
- [x] Hang (mérve: görbék, időállandók, torzítás, AUTO GAIN ±0,1 dB), auval -strict PASS, telepítve
- [x] Saját panelek (közös motor, saját téma: zöldesszürke / fekete-ezüst / konzolszürke)
- [ ] Meghallgatás DAW-ban, finomhangolás fül alapján

## Kompakt, "utalásos" panelek (2026-10-18)
- [x] Méretezhető panel-motor (75–200%, megjegyzi), új gomb-stílusok, lépcsős gomb, trimmer, függőleges gombsor, VU-stílusok
- [x] OPTEKI: világos ezüst előlap · VARIEKI: hadihajó-szürke, krómkeretes műszer · FETEKI: fekete, ezüstszoknyás gombok, jobb oldali VU · BUSEKI: konzolmodul, színes sapkák, GR-műszer
- [x] VARIEKI újratervezve a sztereó vari-mu előlap mintájára: pala-kék lemez, két oldalt műszer (bal GR, jobb OUTPUT VU), középen fekete vezérlőpanel — ellenőrzésre vár
- [x] VARIEKI (korábbi indigókék változat) / BUSEKI (grafit konzol, esztergált gombok piros/kék jelöléssel — jóváhagyva)
- [x] VARIEKI: kettős detektoros (csúcs + RMS) motor, alapból hallható vari-mu szín (ANALOG 35%)
- [ ] Meghallgatás / kipróbálás DAW-ban

## Új panel-nyelv: nagy "modern hardver" rack-előlap (2026-10-18, a Relic-szerű méret/kinézet EKI-változata)
- [x] Motor: fejléc-sáv (EKIDIO SOUND, preset ◀ lista ▶, EKI fiók gomb), EKI fiók (ritka paraméterek), statikus előlap bitkép-gyorsítótár, prémium bakelit gomb
- [x] OPTEKI pilot (960 px, 3U): elöl GAIN, PEAK REDUCTION, AUTO GAIN, METER, COMPRESS/LIMIT, CELL lámpa; fiókban HF EMPHASIS, ANALOG, MIX
- [x] Stílus-szabály: prémium kidolgozás (fém keret, szálcsiszolt lemez, esztergált gombok, világító VU), de a SZÍNVILÁG az eredeti eszközre utal (OPTEKI fehéres-ezüst, FETEKI fekete, VARIEKI hadihajó-szürke, BUSEKI kékesszürke konzol…); a modellnév a VU-számlapon (gyári műszer-felirat), a márka csak a fejlécben
- [x] OPTEKI jóváhagyva: EZ AZ EKIDIO SOUND VIZUÁL (pajzs-VU a modellnévvel, izzó pontos esztergált gombok, világító gyűrűs gomb, LED-jelzők) — a többi is erre épül
- [x] Fontok: csak macOS rendszerfontok (Futura, Avenir Next, DIN…) — nem kell terjeszteni, eladáshoz is rendben. A fizetős Vermin Vibes kikerült.
- [x] FETEKI (fekete), VARIEKI (hadihajó-szürke), BUSEKI (kékesszürke konzol) az OPTEKI stílusában — közös keret: Shared/EKIRack.h
- [x] AUTO GAIN alapból KI mind a 4-ben (a user a kompresszor valódi hangját hallja); a presetek a mért makeup-pal szólnak, a Default 0 dB
- [x] APEKI (2500-ihletésű punch busz-komp): hang mérve (arányok, időzítések, THRUST, FF/FB, AUTO GAIN, MIX lapos), panel: sötétszürke + kék oldalpanel + piros OUTPUT, auval PASS
- [x] DBEKI (160-ihletésű): hang mérve, panel: fekete + ezüst díszlécek + diófa oldallapok + ezüst gombok + BELOW/ABOVE LED, auval PASS
- [x] TUBEKI (CL 1B-ihletésű): opto + cső, FIX/MAN/F+M, királykék panel, auval PASS
- [x] FAIREKI (670-ihletésű): visszacsatolt vari-mu, TC1–6, LAT/VERT, AGC FILTER; fekete panel két egymás alatti VU-val, auval PASS — kevés gomb, 1–2 mozdulatos használat

## Javítások a Logic-próba után (2026-10-18)
- [x] Fejléc-gombok (preset, EKI fiók) Logicban: a panel átlátszatlan és nem mozgat ablakot
- [x] MIX < 100% fésűszűrő (−10…−15 dB bevágások) mind a 4 pluginban: fázishelyes száraz jel (DryAligner) → lapos ±0,02 dB
- [x] 44,1/48 kHz-en a trafó HF-vágója tompított (−2 dB 18 kHz, −6,7 dB 20 kHz): csak 96 kHz-től él
- [x] Presetek nem írják át a METER-t (kijelzési paraméter); VU alatti GR/AUTO felirat ki; CELL LED fényudvar nélkül

## Kiadás (2026-10-18)
- [x] GitHub: Ekidio/ekidio_sound_plugins, Pages (EN/HU, banner, montázs), v1.0.0 release (pkg + DMG, ad-hoc aláírás)
- VST3: most nem tervezzük (Macen szinte minden DAW kezeli az AU-t; kivétel a Cubase/Nuendo). Az oldalon: „AU only”.

## EQ COLLECTION (ötletlista, 2026-10-18 — még nem indult)
A kompresszorcsalád mintájára: hang első, AUTO GAIN alapból ki, közös rack-panel, eredeti színvilág, márkanév sehol a UI-n.
Javasolt sorrend: PULTEKI → BRITEKI → APEQ → CONSEKI → MASSEKI → AIREKI (a nevek munkanevek).
- [ ] PULTEKI — passzív csöves program-EQ + középsáv (EQP-1A + MEQ-5 ihletés): mély emelés/vágás trükk, magas emelés sávszélességgel, magas vágás; középsáv az EKI fiókban; műszer helyett LED
- [ ] BRITEKI — brit tekercses csatorna-EQ (1073 ihletés): lépcsős frekvenciák, HPF, trafószín
- [ ] APEQ — arányos Q-jú, lépcsős 3–4 sávos EQ (550A ihletés), az APEKI párja
- [ ] CONSEKI — konzol csatorna-EQ BROWN/BLACK karakterváltóval (E/G ihletés), a BUSEKI párja
- [ ] MASSEKI — mastering passzív EQ széles, zenei sávokkal (Massive Passive ihletés), a VARIEKI párja
- [ ] AIREKI — modern „levegő” sáv egy gombbal (EQ4 Air ihletés)
- [ ] ABBEKI — vintage brit pult-EQ (TG12345 ihletés) — opcionális
- [ ] TILTEKI — Baxandall / tilt mastering EQ — opcionális
- Technika: analóg prototípusokból illesztett görbék, a magas sávoknál túlmintavétel (44,1 kHz-en se gyűrődjön), közös Transformer/TubeStage szín
- Kiadás: külön „EQ Collection” telepítő és aloldal, a kompresszoros oldallal azonos stílusban

## Szinti (aumu)
- [ ] 3 oszcillátor + zaj, túlvezérelt 24 dB-es ladder, gyors burkolók, analóg drift, legato/glide, túlmintavételes drive, presetek
- [ ] Saját panel
