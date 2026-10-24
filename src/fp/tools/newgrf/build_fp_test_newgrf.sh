#!/bin/bash
#
# Build the test NewGRF of the first-person mode into <output dir>/fp_test_houses.grf.
# Needs NML: pip install nml.

set -eu
OUT=$(realpath -m "${1:?Usage: $0 <output dir>}")
SRC=$(dirname "$(realpath "$0")")
mkdir -p "$OUT"
cd "$SRC"
python3 fp_make_test_graphics.py "$OUT/fp_test_houses.png" "$OUT/fp_test_houses_32bpp.png"
cp fp_test_houses.nml "$OUT/"
cp -r lang "$OUT/"
cd "$OUT"
nmlc --quiet -c --grf fp_test_houses.grf fp_test_houses.nml
echo "$OUT/fp_test_houses.grf"
