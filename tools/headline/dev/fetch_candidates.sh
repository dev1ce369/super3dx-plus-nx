#!/bin/bash
# The candidate fonts the headline style was chosen from (all open source), into $HEADLINE_FONTS (default ~/headline_fonts), for
# compare_fonts.py. Libre Franklin Bold was chosen; the rest are only for comparing again.
OUT="${HEADLINE_FONTS:-$HOME/headline_fonts}"
mkdir -p "$OUT"
B=https://raw.githubusercontent.com/google/fonts/main/ofl
get() { curl -sSL -f -o "$OUT/$2" "$1" && echo "ok $2" || echo "FAILED $2"; }
get "$B/librefranklin/LibreFranklin%5Bwght%5D.ttf" LibreFranklin-wght.ttf
get "$B/publicsans/PublicSans%5Bwght%5D.ttf" PublicSans-wght.ttf
get "$B/mplusrounded1c/MPLUSRounded1c-ExtraBold.ttf" MPLUSRounded1c-ExtraBold.ttf
get "$B/mplusrounded1c/MPLUSRounded1c-Bold.ttf" MPLUSRounded1c-Bold.ttf
get "$B/nunito/Nunito%5Bwght%5D.ttf" Nunito-wght.ttf
get "$B/arimo/Arimo%5Bwght%5D.ttf" Arimo-wght.ttf
get "$B/rubik/Rubik%5Bwght%5D.ttf" Rubik-wght.ttf
get "$B/poppins/Poppins-Bold.ttf" Poppins-Bold.ttf
get "$B/outfit/Outfit%5Bwght%5D.ttf" Outfit-wght.ttf
# DejaVu Sans Bold, Liberation Sans Bold and Nimbus Sans Bold come from Debian packages (unpacked, not installed)
T=$(mktemp -d)
( cd "$T" && apt-get download fonts-liberation fonts-urw-base35 >/dev/null 2>&1; for f in *.deb; do dpkg -x "$f" x; done
  cp x/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf "$OUT/" 2>/dev/null
  cp x/usr/share/fonts/opentype/urw-base35/NimbusSans-Bold.otf "$OUT/" 2>/dev/null )
cp /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf "$OUT/" 2>/dev/null
ls "$OUT"
