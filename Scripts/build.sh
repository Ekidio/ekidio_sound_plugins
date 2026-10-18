#!/bin/zsh
# EKIDIO SOUND — közös build / aláírás / telepítés / auval
#
#   Scripts/build.sh OPTEKI            # build + ad-hoc aláírás + telepítés + auval
#   Scripts/build.sh OPTEKI --no-install
#   Scripts/build.sh OPTEKI --no-auval
#   Scripts/build.sh all
set -euo pipefail

ROOT=${0:A:h:h}
source "$ROOT/Scripts/eki.env"

INSTALL=1
AUVAL=1
TARGETS=()
for arg in "$@"; do
  case $arg in
    --no-install) INSTALL=0; AUVAL=0 ;;
    --no-auval)   AUVAL=0 ;;
    all)          TARGETS+=(${(f)"$(ls "$ROOT/Plugins")"}) ;;
    -*)           echo "Ismeretlen kapcsoló: $arg"; exit 2 ;;
    *)            TARGETS+=("$arg") ;;
  esac
done
if (( ${#TARGETS} == 0 )); then
  echo "Használat: Scripts/build.sh <PLUGIN|all> [--no-install] [--no-auval]"
  echo "Pluginok: $(ls "$ROOT/Plugins" | tr '\n' ' ')"
  exit 2
fi

COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"

build_one() {
  local P=$1
  local PDIR="$ROOT/Plugins/$P"
  [[ -f "$PDIR/plugin.env" ]] || { echo "Nincs ilyen plugin: $P"; exit 1; }
  local NAME DESCRIPTION TYPE SUBTYPE FACTORY VERSION TAGS
  source "$PDIR/plugin.env"

  local BUNDLE="$ROOT/build/$NAME.component"
  echo "==> $NAME ($TYPE $SUBTYPE $MANUFACTURER) fordítása"
  rm -rf "$BUNDLE"
  mkdir -p "$BUNDLE/Contents/MacOS"

  # saját panel (.mm) esetén buildenként egyedi Objective-C osztálynév, hogy két verzió se ütközzön egy hostban
  local SOURCES=("$PDIR"/*.cpp) UIFLAGS=()
  local MM=("$PDIR"/*.mm(N))
  if (( ${#MM} )); then
    SOURCES+=("${MM[@]}")
    UIFLAGS=(-fobjc-arc -DEKI_UI_FACTORY=${NAME}_ViewFactory_$(date +%Y%m%d%H%M%S) -framework AppKit)
  fi

  clang++ -std=c++20 -O3 -fno-math-errno -arch arm64 -arch x86_64 -mmacosx-version-min=$MIN_MACOS \
    -bundle -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter \
    -I"$ROOT/Shared" -I"$PDIR" "${SOURCES[@]}" "${UIFLAGS[@]}" \
    -framework AudioToolbox -framework CoreAudio -framework CoreFoundation \
    -o "$BUNDLE/Contents/MacOS/$NAME"

  local MAJ MIN PAT
  IFS=. read -r MAJ MIN PAT <<< "$VERSION"
  local VERINT=$(( (MAJ << 16) | (MIN << 8) | PAT ))
  local TAGXML=""
  for t in ${=TAGS}; do TAGXML+="<string>$t</string>"; done

  cat > "$BUNDLE/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleExecutable</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_PREFIX.${NAME:l}</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleSignature</key><string>????</string>
  <key>LSMinimumSystemVersion</key><string>$MIN_MACOS</string>
  <key>AudioComponents</key>
  <array>
    <dict>
      <key>type</key><string>$TYPE</string>
      <key>subtype</key><string>$SUBTYPE</string>
      <key>manufacturer</key><string>$MANUFACTURER</string>
      <key>name</key><string>$MANUFACTURER_NAME: $NAME</string>
      <key>description</key><string>$DESCRIPTION</string>
      <key>version</key><integer>$VERINT</integer>
      <key>factoryFunction</key><string>$FACTORY</string>
      <key>sandboxSafe</key><true/>
      <key>tags</key><array>$TAGXML</array>
    </dict>
  </array>
</dict>
</plist>
EOF
  plutil -lint -s "$BUNDLE/Contents/Info.plist"

  # saját panelhez: a közös fontok beágyazása (a panel futáskor, csak a saját folyamatába regisztrálja)
  if (( ${#MM} )) && [[ -d "$ROOT/Shared/Fonts" ]]; then
    mkdir -p "$BUNDLE/Contents/Resources"
    cp "$ROOT"/Shared/Fonts/*.(otf|ttf)(N) "$BUNDLE/Contents/Resources/"
  fi

  echo "==> ad-hoc aláírás"
  codesign --force --sign - --timestamp=none "$BUNDLE"

  if (( INSTALL )); then
    echo "==> telepítés: $COMPONENTS/$NAME.component"
    mkdir -p "$COMPONENTS"
    rm -rf "$COMPONENTS/$NAME.component"
    cp -R "$BUNDLE" "$COMPONENTS/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
  fi

  if (( AUVAL )); then
    echo "==> auval"
    local LOG="$ROOT/build/$NAME-auval.log"
    if auval -strict -v "$TYPE" "$SUBTYPE" "$MANUFACTURER" > "$LOG" 2>&1; then
      echo "    auval: PASS  (napló: build/$NAME-auval.log)"
    else
      tail -40 "$LOG"
      echo "    auval: FAIL  (napló: build/$NAME-auval.log)"
      return 1
    fi
  fi
}

for t in "${TARGETS[@]}"; do build_one "$t"; done
