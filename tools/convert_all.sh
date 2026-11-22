#!/bin/sh
# Kept for habit: the work is done by tools/pipeline.py (the same on every system).
#   tools/convert_all.sh <arcade root> [--no-media] [--only <step>] [--keep-going]
cd "$(dirname "$0")/.." && exec python3 tools/pipeline.py convert "$@"
