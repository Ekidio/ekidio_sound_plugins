#!/bin/zsh
# EKIDIO SOUND — telepítő: minden plugin buildje (ad-hoc aláírva) → .pkg (/Library/Audio/Plug-Ins/Components) → DMG
#
#   Scripts/make-installer.sh            # build/EKIDIO-SOUND-Plugins-<verzió>.pkg és .dmg (a régi, egyben lévő csomag)
#   Scripts/make-installer.sh LIMEKI     # önálló telepítő egy pluginhoz: build/EKIDIO-SOUND-LIMEKI-<plugin verzió>.pkg és .dmg
#
# Kiadási elv: minden új / módosított plugin önálló telepítőt kap (saját GitHub-kiadás: <név kisbetűvel>-v<verzió>).
#
# A .pkg a rendszerszintű Components mappába telepít, utána frissíti az AU-listát (AudioComponentRegistrar).
# A pkg-vel telepített pluginok nem kerülnek "karanténba", így a DAW gond nélkül betölti őket.
set -euo pipefail

ROOT=${0:A:h:h}
source "$ROOT/Scripts/eki.env"
SINGLE=${1:-}
if [[ -n $SINGLE ]]; then
    [[ -f "$ROOT/Plugins/$SINGLE/plugin.env" ]] || { echo "Nincs ilyen plugin: $SINGLE"; exit 1; }
    VERSION=$(grep '^VERSION=' "$ROOT/Plugins/$SINGLE/plugin.env" | cut -d'"' -f2)
    DESC=$(grep '^DESCRIPTION=' "$ROOT/Plugins/$SINGLE/plugin.env" | cut -d'"' -f2)
    NAME="EKIDIO-SOUND-$SINGLE-$VERSION"
    PKGID="$BUNDLE_PREFIX.${(L)SINGLE}"
    TITLE="EKIDIO SOUND $SINGLE"
    PLUGS=("$ROOT/Plugins/$SINGLE")
else
    VERSION="1.2.0"
    NAME="EKIDIO-SOUND-Plugins-$VERSION"
    PKGID="$BUNDLE_PREFIX.compressors"
    TITLE="EKIDIO SOUND Plugins"
    PLUGS=("$ROOT"/Plugins/*(/))
fi
OUT="$ROOT/build"
STAGE="$OUT/pkg-stage"
SCRIPTS="$OUT/pkg-scripts"
RES="$OUT/pkg-resources"

echo "==> fordítás (telepítés nélkül)"
"$ROOT/Scripts/build.sh" ${SINGLE:-all} --no-install

rm -rf "$STAGE" "$SCRIPTS" "$RES"
mkdir -p "$STAGE" "$SCRIPTS" "$RES"
for p in $PLUGS; do
    cp -R "$OUT/${p:t}.component" "$STAGE/"          # a mappanév = a plugin neve
done
echo "==> a csomagba kerül: $(ls "$STAGE" | tr '\n' ' ')"

# telepítés előtti szkript: a saját pluginjaink régi példányai (rendszer- és felhasználói mappa) törlődnek,
# így a frissítés felülír, és nem lesz duplikált plugin (csak a csomagban lévő nevek!)
NAMES=""
for p in $PLUGS; do NAMES+="${p:t} "; done
cat > "$SCRIPTS/preinstall" <<PRE
#!/bin/sh
NAMES="$NAMES"
USER_NAME=\$(stat -f%Su /dev/console 2>/dev/null)
USER_HOME=""
if [ -n "\$USER_NAME" ] && [ "\$USER_NAME" != "root" ]; then
    USER_HOME=\$(dscl . -read "/Users/\$USER_NAME" NFSHomeDirectory 2>/dev/null | awk '{print \$2}')
fi
for n in \$NAMES; do
    rm -rf "/Library/Audio/Plug-Ins/Components/\$n.component"
    [ -n "\$USER_HOME" ] && rm -rf "\$USER_HOME/Library/Audio/Plug-Ins/Components/\$n.component"
done
exit 0
PRE
chmod +x "$SCRIPTS/preinstall"

# telepítés utáni szkript: a régi AU-gyorsítótár frissítése
cat > "$SCRIPTS/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x "$SCRIPTS/postinstall"

echo "==> komponens-csomag"
# a bundle-ok ne legyenek "áthelyezhetők": a telepítő mindig a /Library-be tegye őket (ne egy máshol talált régi példány helyére)
pkgbuild --analyze --root "$STAGE" "$OUT/components.plist" >/dev/null
i=0
while plutil -replace $i.BundleIsRelocatable -bool NO "$OUT/components.plist" 2>/dev/null; do i=$((i + 1)); done
pkgbuild --root "$STAGE" --component-plist "$OUT/components.plist" --install-location /Library/Audio/Plug-Ins/Components \
    --identifier "$PKGID" --version "$VERSION" \
    --scripts "$SCRIPTS" "$OUT/components.pkg"
rm -f "$OUT/components.plist"

# telepítő szövegek (angol, a csomagban megjelenő üdvözlés és licenc)
if [[ -n $SINGLE ]]; then
cat > "$RES/welcome.html" <<EOF
<html><head><meta charset="utf-8"></head><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>EKIDIO SOUND &mdash; $SINGLE $VERSION</h2>
<p><b>$SINGLE</b> &mdash; ${DESC:l} (Audio Unit, macOS).</p>
<p>It is installed to <code>/Library/Audio/Plug-Ins/Components</code>; other EKIDIO SOUND plugins are not touched. Restart your DAW afterwards.</p>
</body></html>
EOF
else
cat > "$RES/welcome.html" <<EOF
<html><head><meta charset="utf-8"></head><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>EKIDIO SOUND &mdash; Plugins $VERSION</h2>
<p>Eleven analog-inspired Audio Unit (AU) plugins for macOS.</p>
<p>Compressors: <b>OPTEKI</b> optical leveler &middot; <b>FETEKI</b> FET peak limiter &middot; <b>VARIEKI</b> variable-mu dynamics &middot;
<b>BUSEKI</b> stereo bus compressor &middot; <b>APEKI</b> punch bus compressor &middot; <b>DBEKI</b> RMS compressor/limiter &middot;
<b>TUBEKI</b> opto tube compressor &middot; <b>FAIREKI</b> variable-mu limiter.</p>
<p>EQ, color &amp; tape: <b>DYNEKI-6</b> six-band character EQ + dynamics &middot; <b>SATEKI</b> soft tube air enhancer &middot; <b>TAPEKI</b> analog tape machine.</p>
<p>They are installed to <code>/Library/Audio/Plug-Ins/Components</code>. Restart your DAW afterwards.</p>
</body></html>
EOF
fi
cat > "$RES/license.html" <<EOF
<html><head><meta charset="utf-8"></head><body style="font-family:-apple-system,Helvetica;font-size:12px">
<h3>EKIDIO SOUND &mdash; Free License</h3>
<p>These plugins are free to download and use, including in commercial music and audio productions.
You may not sell, rent or redistribute the plugins or their source code, or modify and republish them.</p>
<p>THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY ARISING FROM THE USE OF THE SOFTWARE.</p>
<p>All product names mentioned on the website are trademarks of their respective owners and are used only to describe
the sound character; EKIDIO SOUND is not affiliated with them.</p>
<p>&copy; 2026 EKIDIO SOUND. All rights reserved.</p>
</body></html>
EOF
cat > "$OUT/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$TITLE $VERSION</title>
    <welcome file="welcome.html"/>
    <license file="license.html"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <volume-check><allowed-os-versions><os-version min="$MIN_MACOS"/></allowed-os-versions></volume-check>
    <choices-outline><line choice="default"><line choice="components"/></line></choices-outline>
    <choice id="default"/>
    <choice id="components" visible="false"><pkg-ref id="$PKGID"/></choice>
    <pkg-ref id="$PKGID" version="$VERSION" onConclusion="none">components.pkg</pkg-ref>
</installer-gui-script>
EOF

echo "==> telepítő (.pkg)"
productbuild --distribution "$OUT/distribution.xml" --resources "$RES" --package-path "$OUT" "$OUT/$NAME.pkg"
rm -f "$OUT/components.pkg"

echo "==> DMG"
DSTAGE="$OUT/dmg-stage"
rm -rf "$DSTAGE" "$OUT/$NAME.dmg"
mkdir -p "$DSTAGE"
cp "$OUT/$NAME.pkg" "$DSTAGE/Install $TITLE.pkg"
cat > "$DSTAGE/Read Me.txt" <<EOF
$TITLE $VERSION

1. Double-click "Install $TITLE.pkg".
   If macOS says it cannot verify the developer: click Done, then open
   System Settings → Privacy & Security and click "Open Anyway".
2. Restart your DAW (Logic Pro: Plug-in Manager → Reset & Rescan if needed).
3. The plugins appear under the manufacturer "EKIDIO SOUND".

Uninstall: delete the ${SINGLE:-*EKI}.component file(s) from /Library/Audio/Plug-Ins/Components.

https://ekidio.github.io/ekidio_sound_plugins/
EOF
hdiutil create -srcfolder "$DSTAGE" -volname "$TITLE" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$OUT/$NAME.dmg" -quiet
rm -rf "$DSTAGE" "$STAGE" "$SCRIPTS" "$RES" "$OUT/distribution.xml"

if [[ -n $SINGLE ]]; then
    # önálló kiadás: csak ennek a pluginnak a bejegyzése változik a version.json-ban, a többi és az egyben-csomag linkje marad
    /usr/bin/python3 -I -c '
import json, sys
p, n, v = sys.argv[1:]
d = json.load(open(p)); d.setdefault("plugins", {})[n] = v
d["plugins"] = dict(sorted(d["plugins"].items()))
open(p, "w").write(json.dumps(d, indent=2) + "\n")' "$ROOT/docs/version.json" "$SINGLE" "$VERSION"
    TAG="${(L)SINGLE}-v$VERSION"
    sed -i '' -E "s#download/[a-z0-9]+-v[0-9.]+[0-9]/EKIDIO-SOUND-$SINGLE-[0-9.]+[0-9]\.dmg#download/$TAG/$NAME.dmg#g" "$ROOT/docs/index.html" "$ROOT/docs/hu/index.html"
    echo "Kész (GitHub-kiadás címkéje: $TAG):"
    ls -lh "$OUT/$NAME.pkg" "$OUT/$NAME.dmg" | awk '{print "  " $5 "  " $9}'
    exit 0
fi

# a pluginok frissítés-figyelője ezt olvassa (a GitHub Pages-ről): commit + push a kiadással együtt
# pluginonkénti verziók (Plugins/*/plugin.env VERSION) — a pluginok a saját bejegyzésüket nézik
PLIST=""
for p in "$ROOT"/Plugins/*(/); do
    PV=$(grep '^VERSION=' "$p/plugin.env" | cut -d'"' -f2)
    PLIST+="${PLIST:+,\n}    \"${p:t}\": \"$PV\""
done
printf '{\n  "version": "%s",\n  "url": "https://ekidio.github.io/ekidio_sound_plugins/",\n  "plugins": {\n'"$PLIST"'\n  }\n}\n' "$VERSION" > "$ROOT/docs/version.json"
# a weboldal letöltési linkje az aktuális DMG-re mutasson
sed -i '' -E "s#download/v[0-9.]+[0-9]/EKIDIO-SOUND-(Compressors|Plugins)-[0-9.]+[0-9]\.dmg#download/v$VERSION/$NAME.dmg#g" "$ROOT/docs/index.html" "$ROOT/docs/hu/index.html"

echo "Kész:"
ls -lh "$OUT/$NAME.pkg" "$OUT/$NAME.dmg" | awk '{print "  " $5 "  " $9}'
