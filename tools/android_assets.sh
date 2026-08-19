#!/bin/sh
# Stage the APK assets: disk image, fonts/logo, sfx maps, HOL extras, and the manifest the app copies at first run.
set -e
cd "$(dirname "$0")/.."
A=android/app/src/main/assets
rm -rf "$A"; mkdir -p "$A/assets" "$A/extras/hol"
cp "${SWIV_ADF:-/home/jon/swiv-amiga-re/SWIVFIX.ADF}" "$A/SWIVFIX.ADF"
cp assets/retro_recomp_logo.png assets/DejaVuSans.ttf "$A/assets/"
[ -f sfxmap.txt ] && cp sfxmap.txt "$A/"; [ -f sfxtune.txt ] && cp sfxtune.txt "$A/"
for d in screen box disk misc; do [ -d extras/hol/$d ] && mkdir -p "$A/extras/hol/$d" && cp extras/hol/$d/*.png "$A/extras/hol/$d/" 2>/dev/null || true; done
[ -f extras/hol/cheats.txt ] && cp extras/hol/cheats.txt "$A/extras/hol/"
( cd "$A" && find . -type f ! -name manifest.txt | sed 's|^\./||' | sort > manifest.txt )
echo "staged $(wc -l < "$A/manifest.txt") asset files"
