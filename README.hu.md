# EKI PLUGINS — EKIDIO SOUND (magyar fejlesztői leírás)

Weboldal: https://ekidio.github.io/ekidio_sound_plugins/hu/ · Letöltés: https://github.com/Ekidio/ekidio_sound_plugins/releases/latest

AUv2 (.component) Audio Unit pluginok macOS-re, közös DSP-alapra és build-scriptre építve.

| Plugin | Típus | Kód | Állapot |
|---|---|---|---|
| OPTEKI — optikai szintszabályzó | aufx | `Opek` / `Ekid` | hang + saját panel kész, auval PASS |
| VARIEKI — kettős detektoros kompresszor vari-mu színnel | aufx | `Vrek` / `Ekid` | hang + saját panel kész, auval PASS |
| FETEKI — FET csúcskompresszor | aufx | `Ftek` / `Ekid` | hang + saját panel kész, auval PASS |
| BUSEKI — VCA buszkompresszor | aufx | `Bsek` / `Ekid` | hang + saját panel kész, auval PASS |
| APEKI — punchos busz-kompresszor | aufx | `Apek` / `Ekid` | hang + saját panel kész, auval PASS |
| DBEKI — RMS VCA kompresszor / limiter | aufx | `Dbek` / `Ekid` | hang + saját panel kész, auval PASS |
| TUBEKI — opto-csöves kompresszor | aufx | `Tbek` / `Ekid` | hang + saját panel kész, auval PASS |
| FAIREKI — vari-mu csöves limiter (LAT/VERT) | aufx | `Frek` / `Ekid` | hang + saját panel kész, auval PASS |
| DYNEKI-6 — hatsávos karakteres EQ + SAT + DYN, M/S | aufx | `Dyek` / `Ekid` | hang + saját panel kész, auval PASS |
| SATEKI — lágy, csöves levegő-emelő (3 cső, 5–16 kHz) | aufx | `Satk` / `Ekid` | hang + saját panel kész, auval PASS |
| TAPEKI — analóg szalagos magnó (STUDIO / MASTER / CASSETTE, hiszterézis) | aufx | `Tpek` / `Ekid` | hang + saját panel kész, auval PASS |
| LIMEKI — density és clean limiter (DENSITY + true-peak CLEAN, AUTO −14 LUFS; önálló telepítő) | aufx | `Lmek` / `Ekid` | hang + saját panel kész, auval PASS |
| EK1 — mastering-lánc (AIR / FOCUS / BASS PUNCH, PUSH, kétfokozatú VOLUME-limiter, TRANSIENT, WIDTH; önálló telepítő) | aufx | `Ek1m` / `Ekid` | hang + saját panel kész, auval PASS |
| MASTEQUI — háromsávos mastering EQ két polccal, 2× lineáris fázisú túlmintavételezés (önálló telepítő) | aufx | `Mstq` / `Ekid` | hang + saját panel kész, auval PASS |
| EKIPPER — soft clipper (DRIVE, OUTPUT, 2× / 4× / 8× túlmintavételezés; önálló telepítő) | aufx | `Ekcl` / `Ekid` | hang + saját panel kész, auval PASS |
| (szinti) — analóg mono szinti | aumu | — | tervezve |

## Build

```bash
Scripts/build.sh OPTEKI          # fordítás (arm64+x86_64) + ad-hoc aláírás + telepítés + auval -strict
Scripts/build.sh OPTEKI --no-install
Scripts/build.sh all
```

Telepítés helye: `~/Library/Audio/Plug-Ins/Components`. Az auval naplója: `build/<NÉV>-auval.log`.

## Felépítés

- `Shared/EKIDSP.h`: közös DSP (PolyBLEP, SVF, ZDF ladder, ADSR, simító, 2x IIR oversampler, VU-ballisztika, denormal-védelem). A GROOVEKI DSPCore C++ portja.
- `Shared/EKIAUBase.h`: függőség nélküli AUv2 keret (effekt + hangszer, paraméterek, presetek, ClassInfo, mérők, CocoaUI).
- `Shared/EKIAnalog.h`: közös analóg modellek (trafó, cső/kimenőfokozat), AUTO GAIN (GR-átlagos és energia-alapú), VU-mérő módok, lágy térd, kettős (programfüggő) elengedés.
- `Shared/EKIPanel.h`: közös Cocoa panel-motor (gombok, kapcsolók, VU, egér/görgő, automáció-gesztusok). Ha a pluginban van `.mm`, a build.sh buildenként egyedi Objective-C osztálynevet ad.
- `Plugins/<NÉV>/`: `plugin.env` (AU metaadatok), `<NÉV>.cpp` (AU osztály), DSP header.
- `Scripts/eki.env`: gyártókód és -név.
- `Tools/`: offline mérések (`opteki_measure.cpp`, `family_measure.cpp [vari|fet|bus]`), mini host (`au_smoketest.cpp`), panel-pillanatkép PNG-be (`ui_snapshot.mm`; `EKI_UI_CLICKS="x,y;x,y"` kattintásteszt).

Panel kezelése: húzás fel/le, Shift = finom, dupla kattintás = alapérték, görgő; kapcsolók kattintásra; forgókapcsolók feliratára kattintva közvetlenül választ; lépcsős gombok kattintásra léptetnek, húzva/görgővel váltanak.

Méret: a jobb alsó sarokban lévő fogóval 75–200% között húzható, dupla kattintás = 100%; a méretet pluginonként megjegyzi.

## OPTEKI paraméterek

PEAK REDUCTION (0–100), GAIN (−20…+30 dB; AUTO GAIN mellett finom korrekció), AUTO GAIN (be/ki: az energiával súlyozott átlagos GR ≈1,2 s alatt visszaemelve, így az átlagszint ≈ a bemeneti), COMPRESS/LIMIT, HF EMPHASIS (oldallánc-kiemelés), ANALOG (cső + trafó), MIX, METER (GR / Output +4 / Output +10), valamint két csak olvasható mérő: VU és GAIN REDUCTION.

## VARIEKI paraméterek

Precíz, sima motor (csúcs- + RMS-detektor) opcionális, hallható vari-mu csőszínnel:
THRESHOLD, PEAK CREST (mennyivel a küszöb felett kezdi kezelni a tranzienseket), RATIO, SOFT KNEE, ATTACK,
RELEASE PEAK, RELEASE RMS (kettős: tartós kompresszió után lassabb farok), STEREO SENS (0% összekapcsolt …
100% független), SC LOW CUT, MAKEUP, AUTO GAIN, ANALOG (vari-mu szín; 0% = teljesen tiszta út), MIX, METER.

## FETEKI paraméterek

INPUT (−10…+40 dB, ezzel hajtjuk a rögzített küszöbbe), OUTPUT, ATTACK 1–7 (800 µs … 20 µs), RELEASE 1–7 (1,1 s … 50 ms), RATIO 4 / 8 / 12 / 20 / ALL (túlkompresszió), AUTO GAIN, ANALOG, MIX, METER (GR / +4 / +8).

## BUSEKI paraméterek

THRESHOLD (−40…0 dBFS), MAKEUP, RATIO 2 / 4 / 10, ATTACK 0,1–30 ms, RELEASE 0,1–1,2 s / AUTO (programfüggő), SC HPF (ki / 60 / 90 / 150 / 250 Hz), AUTO GAIN, ANALOG (konzol-színezés), MIX, METER.

Az AUTO GAIN mindegyiknél úgy kompenzál, hogy a kimenet átlagszintje ≈ a bemenetié legyen (az INPUT emelését is beleértve); mérve ±0,1 dB-en belül.

## APEKI paraméterek

THRESHOLD, RATIO 1.5…10 / ∞, ATTACK 0,03…30 ms, RELEASE 0,05…2 s, KNEE (HARD / MED / SOFT),
THRUST (NORM / MED / LOUD — oldallánc-mélyvágás: a basszus nem húzza le a gaint, punchos marad),
TYPE (NEW = előrecsatolt, OLD = visszacsatolt), OUTPUT ±24 dB (piros gomb), AUTO GAIN, ANALOG, fiókban L/R LINK és MIX.

## DBEKI paraméterek

THRESHOLD, COMPRESSION 1:1…∞ (folytonos), OUTPUT GAIN ±20 dB, OVEREASY (lágy térd), MIX, AUTO GAIN, METER (GAIN / OUT / IN),
fiókban ANALOG. Valódi RMS-detektor, programfüggő felfutás (≈15 / 7 / 4 ms 10 / 20 / 30 dB túllépésnél), állandó 120 dB/s elengedés.
BELOW (sárga) / ABOVE (piros) küszöb-LED.

## TUBEKI paraméterek

THRESHOLD, RATIO 2…10:1, ATTACK 0,5…300 ms, RELEASE 0,05…10 s, ATTACK/RELEASE (FIX = optikai cella programfüggő
időzítése, MAN = kézi, F+M = kézi + a cella lassú farka), GAIN (a csöves fokozatba hajt), METER, PARALLEL (MIX), AUTO GAIN,
fiókban ANALOG.

## FAIREKI paraméterek

INPUT GAIN, THRESHOLD, TIME CONSTANT 1–6 (5–6 programfüggő), AGC (L/R összekapcsolt vagy LAT/VERT = közép/oldal külön),
AGC FILTER (OFF / 90 / 180 Hz / MID), OUTPUT, DRY/WET, AUTO GAIN, METER (GR / OUT, két műszer: LEFT·LAT és RIGHT·VERT),
fiókban ANALOG. Visszacsatolt vari-mu: a szinttel egyre meredekebb, a csökkentéssel nő a csőszín.
