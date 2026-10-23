#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -f "$HERE/third_party/JUCE/CMakeLists.txt" ]]; then
    echo "JUCE submodule is missing; initialising..."
    git -C "$HERE" submodule update --init --recursive
fi
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null 2>&1; then
    if [[ -x "$HERE/tools/cmake/bin/cmake" ]]; then
        CMAKE="$HERE/tools/cmake/bin/cmake"
    else
        echo "CMake not found. Install CMake >= 3.22." >&2
        exit 1
    fi
fi
"$CMAKE" -S "$HERE" -B "$HERE/build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build "$HERE/build" --target Corda_VST3 corda_render corda_selftest -j "$(nproc)"
echo
echo "VST3: $HERE/build/Corda_artefacts/Release/VST3/Corda Grand.vst3"
echo "Install: $HERE/install.sh"
