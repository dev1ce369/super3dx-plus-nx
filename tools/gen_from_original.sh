#!/bin/sh
# Kept for the Android build and for habit: the work is done by tools/pipeline.py (the same on every system).
#   tools/gen_from_original.sh <arcade root> [--phys]
cd "$(dirname "$0")/.." && exec python3 tools/pipeline.py gen "$@"
