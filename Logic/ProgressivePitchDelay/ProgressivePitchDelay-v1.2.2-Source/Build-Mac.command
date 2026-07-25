#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
JUCE_PATH="${1:-${JUCE_PATH:-}}"

if [[ -z "$JUCE_PATH" ]]; then
    for candidate in "$HOME/JUCE" "$HOME/Developer/JUCE" "/Applications/JUCE"; do
        if [[ -f "$candidate/CMakeLists.txt" ]]; then
            JUCE_PATH="$candidate"
            break
        fi
    done
fi

if [[ -z "$JUCE_PATH" || ! -f "$JUCE_PATH/CMakeLists.txt" ]]; then
    echo "JUCE was not found."
    echo "Run this script with your JUCE folder as the first argument, for example:"
    echo "  ./Build-Mac.command /Users/yourname/JUCE"
    exit 1
fi

cmake -S "$ROOT" -B "$ROOT/build" \
    -DJUCE_PATH="$JUCE_PATH" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$ROOT/build" --config Release --parallel

echo
echo "Build complete."
echo "AU:   $ROOT/build/ProgressivePitchDelay_artefacts/Release/AU/Progressive Pitch Delay.component"
echo "VST3: $ROOT/build/ProgressivePitchDelay_artefacts/Release/VST3/Progressive Pitch Delay.vst3"
