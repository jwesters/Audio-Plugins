#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
PRODUCT="Progressive Pitch Delay"
INSTALLED=0

pause_before_exit() {
    echo
    read -r -p "Press Return to close this window..." _ || true
}
trap pause_before_exit EXIT

install_bundle() {
    local source_bundle="$1"
    local destination_folder="$2"
    local destination_bundle="$destination_folder/$(basename "$source_bundle")"

    mkdir -p "$destination_folder"
    rm -rf "$destination_bundle"
    ditto "$source_bundle" "$destination_bundle"

    # Remove only the quarantine attribute attached to this downloaded bundle.
    # This does not change global macOS security settings.
    xattr -dr com.apple.quarantine "$destination_bundle" 2>/dev/null || true

    echo "Installed: $destination_bundle"
    INSTALLED=1
}

echo "Progressive Pitch Delay v1.2.2 — User Installer"
echo "------------------------------------------------"
echo "This release is not Developer ID signed or notarized."
echo "Only continue if you trust the source of this download."
echo

if [[ -d "$ROOT/$PRODUCT.component" ]]; then
    install_bundle \
        "$ROOT/$PRODUCT.component" \
        "$HOME/Library/Audio/Plug-Ins/Components"
fi

if [[ -d "$ROOT/$PRODUCT.vst3" ]]; then
    install_bundle \
        "$ROOT/$PRODUCT.vst3" \
        "$HOME/Library/Audio/Plug-Ins/VST3"
fi

if [[ "$INSTALLED" -eq 0 ]]; then
    echo "No Progressive Pitch Delay AU or VST3 bundle was found beside this installer."
    exit 1
fi

echo
echo "Installation complete."
echo "Restart your Mac before opening the audio program."
echo

if [[ -d "$ROOT/$PRODUCT.component" ]]; then
    echo "Logic Pro location:"
    echo "  Audio FX > Audio Units > jwesters > Progressive Pitch Delay > Stereo"
    echo
    echo "If it is missing:"
    echo "  Logic Pro > Settings > Plug-in Manager > Reset & Rescan Selection"
fi

if [[ -d "$ROOT/$PRODUCT.vst3" ]]; then
    echo "For VST3, reopen the compatible host and rescan plug-ins if required."
fi
