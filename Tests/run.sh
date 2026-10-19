#!/bin/sh
# Builds and runs the geometry regression tests: ./Tests/run.sh, or ./Tests/run.sh --update to rewrite the corpus
# baseline after an intended change. libtess2, nanosvg and stb are checked out beside this repo, as the Xcode project
# expects; set DEPS to look elsewhere. libtess2 keeps its assertions here, unlike the app's build, so tests also catch
# broken tessellator invariants.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
DEPS=${DEPS:-$(dirname "$ROOT")}
SRC="$ROOT/Unit Space/Unit Space"
OUT="$HERE/build"
mkdir -p "$OUT"
for c in "$DEPS"/libtess2/Source/*.c; do
    o="$OUT/$(basename "$c" .c).o"
    [ "$o" -nt "$c" ] || clang -O2 -w -I"$DEPS/libtess2/Include" -c "$c" -o "$o"
done
clang++ -std=gnu++17 -fobjc-arc -O2 -w -I"$SRC/src" -I"$SRC/metal" -I"$DEPS/libtess2/Include" -I"$DEPS/nanosvg/src" -I"$DEPS/stb" \
    "$HERE/RegressionTests.mm" "$OUT"/*.o -framework Foundation -o "$OUT/RegressionTests"
cd "$ROOT"
exec "$OUT/RegressionTests" "$@"
