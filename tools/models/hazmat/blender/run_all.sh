#!/bin/sh
# Builds the Blender hazmat suit end to end. Usage: run_all.sh <SVM install root> [blender.exe]
set -e
cd "$(dirname "$0")/.."
INSTALL="$1"
B="${2:-/c/Program Files/Blender Foundation/Blender 5.2/blender.exe}"
python blender/prep.py --install "$INSTALL" --out out/blender
"$B" -b --factory-startup --python blender/build.py -- out/blender > out/blender/build.log 2>&1 || { tail -30 out/blender/build.log; exit 1; }
grep -E "low poly|bake device" out/blender/build.log
python blender/finish.py --install "$INSTALL" --out out/blender --assets out/blender/assets
"$B" -b --python blender/render.py -- out/blender out/blender/assets > out/blender/render.log 2>&1 || { tail -30 out/blender/render.log; exit 1; }
echo done: out/blender/assets and out/blender/hazmat.blend
