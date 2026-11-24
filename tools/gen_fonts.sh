#!/usr/bin/env bash
# Génère les polices LVGL avec les caractères accentués français (Latin-1)
# et les symboles LVGL. Dépendance : npm i -g lv_font_conv
set -euo pipefail
OUT="${1:-firmware/platform/stm32h7/fonts}"
mkdir -p "$OUT"
TTF="${MONTSERRAT_TTF:-Montserrat-Medium.ttf}"
SYM="${FA_TTF:-FontAwesome5-Solid+Brands+Regular.woff}"
SYMBOLS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,\
61502,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,\
61559,61560,61561,61563,61587,61589,61636,61637,61639,61671,61674,61683,61724,61732,61787,\
61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"
for size in 20 28 48; do
    lv_font_conv --no-compress --no-prefilter --bpp 4 --size "$size" --format lvgl \
        --font "$TTF" -r 0x20-0x7F,0xA0-0xFF,0x2019,0x2026 \
        --font "$SYM" -r "$SYMBOLS" \
        --lv-font-name "ui_font_${size}" -o "$OUT/ui_font_${size}.c"
done
echo "Polices générées dans $OUT"
