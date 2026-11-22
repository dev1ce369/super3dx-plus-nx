#!/bin/bash
# Fetches the headline font (Libre Franklin, SIL Open Font License 1.1) from the google/fonts repository into tools/headline/fonts/.
# The files are kept in the repository already; this is for putting them back, or for another weight/family:
#   tools/headline/fetch_font.sh [family-folder FontFile.ttf]       (default: librefranklin "LibreFranklin[wght].ttf")
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
FAMILY="${1:-librefranklin}"
FILE="${2:-LibreFranklin[wght].ttf}"
OUT="$HERE/fonts"
BASE="https://raw.githubusercontent.com/google/fonts/main/ofl/$FAMILY"
mkdir -p "$OUT"
ENC="$(printf '%s' "$FILE" | sed 's/\[/%5B/g; s/\]/%5D/g')"
curl -sSL -f -o "$OUT/$FILE" "$BASE/$ENC"
curl -sSL -f -o "$OUT/OFL-$FAMILY.txt" "$BASE/OFL.txt"
ls -la "$OUT"
