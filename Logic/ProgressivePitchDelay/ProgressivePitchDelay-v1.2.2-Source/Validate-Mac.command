#!/bin/bash
set -euo pipefail

AU_PATH="$HOME/Library/Audio/Plug-Ins/Components/Progressive Pitch Delay.component"

if [[ ! -d "$AU_PATH" ]]; then
    echo "The Audio Unit is not installed at:"
    echo "  $AU_PATH"
    echo "Run Install-Mac.command first."
    exit 1
fi

killall -9 AudioComponentRegistrar 2>/dev/null || true
xcrun auval -v aufx Ppdl Jwst
