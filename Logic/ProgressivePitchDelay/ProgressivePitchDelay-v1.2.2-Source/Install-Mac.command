#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
AU_SOURCE="$ROOT/build/ProgressivePitchDelay_artefacts/Release/AU/Progressive Pitch Delay.component"
VST3_SOURCE="$ROOT/build/ProgressivePitchDelay_artefacts/Release/VST3/Progressive Pitch Delay.vst3"
AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"

if [[ ! -d "$AU_SOURCE" || ! -d "$VST3_SOURCE" ]]; then
    echo "The Release AU/VST3 builds were not found. Run Build-Mac.command first."
    exit 1
fi

codesign --force --deep --sign - "$AU_SOURCE"
codesign --force --deep --sign - "$VST3_SOURCE"

mkdir -p "$AU_DEST" "$VST3_DEST"
rm -rf "$AU_DEST/Progressive Pitch Delay.component"
rm -rf "$VST3_DEST/Progressive Pitch Delay.vst3"
ditto "$AU_SOURCE" "$AU_DEST/Progressive Pitch Delay.component"
ditto "$VST3_SOURCE" "$VST3_DEST/Progressive Pitch Delay.vst3"

killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "Installed Progressive Pitch Delay."
echo "AU:   $AU_DEST/Progressive Pitch Delay.component"
echo "VST3: $VST3_DEST/Progressive Pitch Delay.vst3"
echo "Restart Logic Pro so it scans the Audio Unit."
