#!/bin/zsh
# EKIDIO SOUND — telepítő: minden plugin buildje (ad-hoc aláírva) → .pkg (/Library/Audio/Plug-Ins/Components) → DMG
#
#   Scripts/make-installer.sh            # build/EKIDIO-SOUND-Compressors-<verzió>.pkg és .dmg
#
# A .pkg a rendszerszintű Components mappába telepít, utána frissíti az AU-listát (AudioComponentRegistrar).
# A pkg-vel telepített pluginok nem kerülnek "karanténba", így a DAW gond nélkül betölti őket.
set -euo pipefail

ROOT=${0:A:h:h}
source "$ROOT/Scripts/eki.env"
VERSION="1.0.2"
NAME="EKIDIO-SOUND-Compressors-$VERSION"
OUT="$ROOT/build"
STAGE="$OUT/pkg-stage"
SCRIPTS="$OUT/pkg-scripts"
RES="$OUT/pkg-resources"

echo "==> minden plugin fordítása (telepítés nélkül)"
"$ROOT/Scripts/build.sh" all --no-install

rm -rf "$STAGE" "$SCRIPTS" "$RES"
mkdir -p "$STAGE" "$SCRIPTS" "$RES"
for p in "$ROOT"/Plugins/*(/); do
    cp -R "$OUT/${p:t}.component" "$STAGE/"          # a mappanév = a plugin neve
done
echo "==> a csomagba kerül: $(ls "$STAGE" | tr '\n' ' ')"

# telepítés előtti szkript: a saját pluginjaink régi példányai (rendszer- és felhasználói mappa) törlődnek,
# így a frissítés felülír, és nem lesz duplikált plugin (csak a csomagban lévő nevek!)
NAMES=""
for p in "$ROOT"/Plugins/*(/); do NAMES+="${p:t} "; done
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
    --identifier "$BUNDLE_PREFIX.compressors" --version "$VERSION" \
    --scripts "$SCRIPTS" "$OUT/components.pkg"
rm -f "$OUT/components.plist"

# telepítő szövegek (angol, a csomagban megjelenő üdvözlés és licenc)
cat > "$RES/welcome.html" <<EOF
<html><head><meta charset="utf-8"></head><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>EKIDIO SOUND &mdash; Compressor Collection $VERSION</h2>
<p>Eight analog-inspired compressors as Audio Unit (AU) plugins for macOS:</p>
<p><b>OPTEKI</b> optical leveler &middot; <b>FETEKI</b> FET peak limiter &middot; <b>VARIEKI</b> variable-mu dynamics &middot;
<b>BUSEKI</b> stereo bus compressor &middot; <b>APEKI</b> punch bus compressor &middot; <b>DBEKI</b> RMS compressor/limiter &middot;
<b>TUBEKI</b> opto tube compressor &middot; <b>FAIREKI</b> variable-mu limiter.</p>
<p>They are installed to <code>/Library/Audio/Plug-Ins/Components</code>. Restart your DAW afterwards.</p>
</body></html>
EOF
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
    <title>EKIDIO SOUND Compressors $VERSION</title>
    <welcome file="welcome.html"/>
    <license file="license.html"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <volume-check><allowed-os-versions><os-version min="$MIN_MACOS"/></allowed-os-versions></volume-check>
    <choices-outline><line choice="default"><line choice="components"/></line></choices-outline>
    <choice id="default"/>
    <choice id="components" visible="false"><pkg-ref id="$BUNDLE_PREFIX.compressors"/></choice>
    <pkg-ref id="$BUNDLE_PREFIX.compressors" version="$VERSION" onConclusion="none">components.pkg</pkg-ref>
</installer-gui-script>
EOF

echo "==> telepítő (.pkg)"
productbuild --distribution "$OUT/distribution.xml" --resources "$RES" --package-path "$OUT" "$OUT/$NAME.pkg"
rm -f "$OUT/components.pkg"

echo "==> DMG"
DSTAGE="$OUT/dmg-stage"
rm -rf "$DSTAGE" "$OUT/$NAME.dmg"
mkdir -p "$DSTAGE"
cp "$OUT/$NAME.pkg" "$DSTAGE/Install EKIDIO SOUND Compressors.pkg"
cat > "$DSTAGE/Read Me.txt" <<EOF
EKIDIO SOUND — Compressor Collection $VERSION

1. Double-click "Install EKIDIO SOUND Compressors.pkg".
   If macOS says it cannot verify the developer: click Done, then open
   System Settings → Privacy & Security and click "Open Anyway".
2. Restart your DAW (Logic Pro: Plug-in Manager → Reset & Rescan if needed).
3. The plugins appear under the manufacturer "EKIDIO SOUND".

Uninstall: delete the *EKI.component files from /Library/Audio/Plug-Ins/Components.

https://ekidio.github.io/ekidio_sound_plugins/
EOF
hdiutil create -srcfolder "$DSTAGE" -volname "EKIDIO SOUND Compressors" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$OUT/$NAME.dmg" -quiet
rm -rf "$DSTAGE" "$STAGE" "$SCRIPTS" "$RES" "$OUT/distribution.xml"

# a pluginok frissítés-figyelője ezt olvassa (a GitHub Pages-ről): commit + push a kiadással együtt
printf '{\n  "version": "%s",\n  "url": "https://ekidio.github.io/ekidio_sound_plugins/"\n}\n' "$VERSION" > "$ROOT/docs/version.json"
# a weboldal letöltési linkje az aktuális DMG-re mutasson
sed -i '' -E "s/EKIDIO-SOUND-Compressors-[0-9.]+[0-9]\.dmg/$NAME.dmg/g" "$ROOT/docs/index.html" "$ROOT/docs/hu/index.html"

echo "Kész:"
ls -lh "$OUT/$NAME.pkg" "$OUT/$NAME.dmg" | awk '{print "  " $5 "  " $9}'
