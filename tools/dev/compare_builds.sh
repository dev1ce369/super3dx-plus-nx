#!/bin/bash
# Do two builds of the arcade game (say Japan and Export) behave the same in the recompiled physics?
#
#   tools/dev/compare_builds.sh <arcade root A> <arcade root B> [--rivals]
#
# For each build: a clean copy of this repository in $MT3DX_WORK (default /tmp/mt3dx-compare/a, /b), the tables and
# physics generated from that build's `main`, test/physics_sweep.c built and run (every car of the car select on three
# courses), and with --rivals test/story_rival_race.c (the first race of every story episode against its rivals).
# Then the outputs are compared: car names are ignored (regions name some cars differently), the rest must match.
# Rival lines differ legitimately where a region swaps a rival's car (the export build has no Gemballa): they are
# split into "same rivals, different result" (a real difference) and "different rival setup" (expected).
#
# Every stage has a time limit and a log in $MT3DX_WORK/logs; a stage that fails or times out stops the run.
set -uo pipefail
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
[ $# -ge 2 ] || { sed -n 2,14p "$0"; exit 2; }
A="$(cd "$1" && pwd)"; B="$(cd "$2" && pwd)"; shift 2
RIVALS=0; [ "${1:-}" = "--rivals" ] && RIVALS=1
WORK="${MT3DX_WORK:-/tmp/mt3dx-compare}"
[ -n "${MT3DX_VENV:-}" ] && export PATH="$MT3DX_VENV/bin:$PATH"
mkdir -p "$WORK/logs"
CFLAGS="-O1 -ffp-contract=off -DRC_FPT=double -DWMMT_EGL -Isrc -Isrc/phys"
SRCS="src/car.c src/phys/rc_runtime.c src/phys/phys_rc.c -lm"

stage() { # name, limit, dir, command...
  local name="$1" limit="$2" dir="$3"; shift 3
  printf '  %-34s' "$name"; local t0=$SECONDS
  if (cd "$dir" && timeout "$limit" "$@") > "$WORK/logs/$name.log" 2>&1; then
    echo "ok ($((SECONDS - t0)) s)"
  else
    local rc=$?; echo "FAILED (exit $rc$([ $rc = 124 ] && echo ", timed out after $limit s"))"
    tail -12 "$WORK/logs/$name.log"; exit 1
  fi
}

for side in a b; do
  root="$A"; [ $side = b ] && root="$B"
  T="$WORK/$side"
  echo "== build $side: $root"
  rm -rf "$T"; mkdir -p "$T"
  # a clean copy: no data, no build output, nothing generated from another build's arcade files
  (cd "$HERE" && tar --exclude=./data --exclude=./build --exclude=./.git --exclude='__pycache__' \
     --exclude='src/*db.h' --exclude=src/battle.h --exclude=src/carwheel.h --exclude='src/story*.h' \
     --exclude=src/sttune.h --exclude=src/steff.h --exclude=src/tenline.h --exclude=src/minicourse.h \
     --exclude=src/phys/phys_rc.c --exclude=src/phys/rc_addr.h -cf - .) | (cd "$T" && tar -xf -)
  mkdir -p "$T/data"
  stage "$side-generate" 2400 "$T" sh tools/gen_from_original.sh "$root" --phys
  stage "$side-build-sweep" 900 "$T" gcc $CFLAGS test/physics_sweep.c $SRCS -o "$WORK/sweep_$side"
  stage "$side-run-sweep" 1500 "$T" "$WORK/sweep_$side" data
  cp "$WORK/logs/$side-run-sweep.log" "$WORK/sweep_$side.txt"
  if [ $RIVALS = 1 ]; then
    stage "$side-build-rivals" 900 "$T" gcc $CFLAGS test/story_rival_race.c $SRCS -o "$WORK/rival_$side"
    mkdir -p "$T/data/carconfig"; cp "$root"/data/carconfig/ytConfig*.txt "$T/data/carconfig/"
    : > "$WORK/rivals_$side.txt"
    echo "  $side-run-rivals (100 episodes, 60 s each at most)"
    for e in $(seq 0 99); do
      out=$(cd "$T" && timeout 60 "$WORK/rival_$side" data "$e" 1200 2>&1 | grep -E "^episode|^rc:" | tr '\n' ' ')
      echo "${out:-episode $e: NO OUTPUT (crash or timeout)}" >> "$WORK/rivals_$side.txt"
    done
    echo "    no output: $(grep -c 'NO OUTPUT' "$WORK/rivals_$side.txt"), rc aborts: $(grep -c 'rc:' "$WORK/rivals_$side.txt")"
  fi
done

echo "== comparing"
strip() { sed -E 's/^[^|]*\|(t[0-9]+\|c[0-9]+)/\1/' "$1" | grep -E '^t[0-9]+\|c[0-9]' | sort; }
strip "$WORK/sweep_a.txt" > "$WORK/sweep_a.cmp"; strip "$WORK/sweep_b.txt" > "$WORK/sweep_b.cmp"
na=$(wc -l < "$WORK/sweep_a.cmp"); nb=$(wc -l < "$WORK/sweep_b.cmp")
nd=$(diff "$WORK/sweep_a.cmp" "$WORK/sweep_b.cmp" | grep -c '^[<>]')
echo "  cars: $na samples vs $nb, differing once names are ignored: $nd"
[ "$nd" = 0 ] && [ "$na" = "$nb" ] && [ "$na" != 0 ] && RES=SAME || RES=DIFFERENT
if [ $RIVALS = 1 ]; then
  for s in a b; do
    sed -E 's/^(episode [0-9]+) series ([0-9-]+) \(([0-9]+) rivals, types([^)]*)\): (.*)$/\1|\2|\3|\4|\5/' "$WORK/rivals_$s.txt" > "$WORK/rivals_$s.split"
  done
  paste -d'#' "$WORK/rivals_a.split" "$WORK/rivals_b.split" | awk -F'#' '
    { split($1,x,"|"); split($2,y,"|");
      if (x[2]==y[2] && x[3]==y[3] && x[4]==y[4]) { if (x[5]==y[5]) same++; else bad++ } else swap++ }
    END { printf "  story races: %d identical, %d different rival setup (expected where a region swaps a car), %d SAME RIVALS BUT DIFFERENT RESULT\n", same, swap, bad;
          exit bad > 0 }' || RES=DIFFERENT
fi
echo "RESULT: $RES   (outputs in $WORK)"
[ "$RES" = SAME ]
