#!/bin/sh
# Regenerates rep_*.h for every .rep file under examples/.
# Generated headers are checked in so a plain `xmake` build works without codegen.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
for rep in "$root"/examples/*/*.rep; do
    dir=$(dirname "$rep")
    out="$dir/rep_$(basename "$rep" .rep).h"
    echo "generating $out"
    xmake run repgen "$rep" -o "$out"
done
