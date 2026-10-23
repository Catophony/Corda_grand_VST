#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/build/Corda_artefacts/Release/VST3/Corda Grand.vst3"
DEST_ROOT="${VST3_DIR:-$HOME/.vst3}"
DEST="$DEST_ROOT/Corda Grand.vst3"

if [[ ! -d "$SRC" ]]; then
    echo "Plugin not built yet: $SRC" >&2
    echo "Run: cmake -S \"$HERE\" -B \"$HERE/build\" -DCMAKE_BUILD_TYPE=Release && cmake --build \"$HERE/build\" --target Corda_VST3 -j" >&2
    exit 1
fi

mkdir -p "$DEST_ROOT"
if [[ -n "$DEST" && "$DEST" == */Corda\ Grand.vst3 ]]; then
    rm -rf -- "$DEST"
else
    echo "Refusing to remove unexpected path: $DEST" >&2
    exit 2
fi
cp -a "$SRC" "$DEST"
echo "Installed: $DEST"
echo "In Reaper: Options > Preferences > Plug-ins > VST, add $DEST_ROOT if needed, then Rescan."
