#!/usr/bin/env bash
# Génère les fragments vocaux de la centrale à partir des textes de
# firmware/core/voice.c, par synthèse vocale française.
#   - WAV 16 kHz 16 bits mono → carte microSD, dossier /VOIX/
#   - AMR 8 kHz               → système de fichiers du module 4G (appels)
#
# Dépendances : piper (voix fr_FR-siwis-medium) OU espeak-ng, et ffmpeg.
# Usage : tools/gen_voix.sh [dossier_sortie]
set -euo pipefail
OUT="${1:-voix}"
mkdir -p "$OUT/VOIX" "$OUT/AMR"
SRC="$(dirname "$0")/../firmware/core/voice.c"

synth() {   # $1 = texte, $2 = fichier wav de sortie
    local tmp="$OUT/tmp.wav"
    if command -v piper >/dev/null 2>&1 && [ -n "${PIPER_MODEL:-}" ]; then
        echo "$1" | piper --model "$PIPER_MODEL" --output_file "$tmp" >/dev/null 2>&1
    else
        espeak-ng -v fr -s 150 -w "$tmp" "$1"
    fi
    ffmpeg -loglevel error -y -i "$tmp" -ar 16000 -ac 1 -sample_fmt s16 "$2"
    ffmpeg -loglevel error -y -i "$tmp" -ar 8000 -ac 1 -c:a libopencore_amrnb -b:a 12.2k \
        "$OUT/AMR/$(basename "${2%.WAV}").AMR" 2>/dev/null || true
    rm -f "$tmp"
}

# Messages : lignes   [VM_XXX] = "texte",   dans l'ordre de l'énumération.
# VM_NONE (texte vide) n'est pas renvoyé par grep : on commence donc à 1.
i=1
grep -oP '\[VM_[A-Z_]+\]\s*=\s*"\K[^"]*' "$SRC" | while IFS= read -r txt; do
    if [ -n "$txt" ]; then
        synth "$txt" "$(printf '%s/VOIX/M%03d.WAV' "$OUT" "$i")"
        echo "M$(printf '%03d' "$i") : $txt"
    fi
    i=$((i + 1))
done

# Nombres 1 à 32
NOMBRES=(zéro un deux trois quatre cinq six sept huit neuf dix onze douze treize quatorze \
quinze seize dix-sept dix-huit dix-neuf vingt vingt-et-un vingt-deux vingt-trois vingt-quatre \
vingt-cinq vingt-six vingt-sept vingt-huit vingt-neuf trente trente-et-un trente-deux)
for n in $(seq 1 32); do
    synth "${NOMBRES[$n]}" "$(printf '%s/VOIX/N%03d.WAV' "$OUT" "$n")"
done
echo "Fragments générés dans $OUT/VOIX (microSD) et $OUT/AMR (module 4G)."
