#!/usr/bin/env bash
# Desktop simulator: compiles the real SkyPoint UI + sky engine with LVGL for the host,
# renders the main screens at 410x502 and writes PNGs to docs/screenshots/.
# Usage: tools/simulator/build_and_run.sh [Arduino libraries dir] [unix time]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
LIBS="${1:-$HOME/Arduino/libraries}"
LV="$LIBS/lvgl"; SGP="$LIBS/Sgp4/src"
OUT="$HERE/build"; mkdir -p "$OUT/obj" "$HERE/shots" "$REPO/docs/screenshots"
cp "$HERE/lv_conf.h" "$OUT/lv_conf.h"
echo "Compiling LVGL (cached)..."
find "$LV/src" -name '*.c' | while read -r f; do
  o="$OUT/obj/$(echo "$f" | md5sum | cut -c1-12).o"; [ -f "$o" ] || echo "$f $o"
done | xargs -r -P "$(nproc)" -n 2 sh -c 'gcc -c -O2 -w -DLV_CONF_INCLUDE_SIMPLE -I'"$OUT"' -I'"$LV"' -I'"$LV"'/src "$0" -o "$1"'
for f in "$REPO"/SkyPoint/f_*.c; do gcc -c -O2 -w -DLV_CONF_INCLUDE_SIMPLE -I"$OUT" -I"$LV" "$f" -o "$OUT/$(basename "$f").o"; done
g++ -std=c++17 -O2 -w -Uunix -DLV_CONF_INCLUDE_SIMPLE -I"$OUT" -I"$LV" -I"$LV/src" -I"$SGP" -I"$REPO/test/stub" -I"$REPO/SkyPoint" \
  "$HERE/sim_main.cpp" "$REPO"/SkyPoint/{ui,skyview,sky,astro,sats,orient,bg,sound}.cpp "$SGP"/*.cpp "$OUT"/f_*.o "$OUT"/obj/*.o -lm -o "$OUT/sim"
(cd "$HERE" && rm -f shots/*.raw && "$OUT/sim" ${2:-})
python3 "$HERE/topng.py" "$HERE/shots" "$REPO/docs/screenshots"
