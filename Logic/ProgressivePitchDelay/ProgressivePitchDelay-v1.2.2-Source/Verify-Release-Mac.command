#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DIST="$ROOT/dist"

[[ -d "$DIST" ]] || { echo "No dist folder found. Run Package-Release-Mac.command first."; exit 1; }
[[ -f "$DIST/SHA256SUMS.txt" ]] || { echo "SHA256SUMS.txt is missing."; exit 1; }

cd "$DIST"
shasum -a 256 -c SHA256SUMS.txt

for archive in ./*.zip; do
    unzip -tq "$archive" >/dev/null
    echo "ZIP OK: $(basename "$archive")"
done

for archive in ./*-macOS-AU.zip ./*-macOS-VST3.zip; do
    [[ -f "$archive" ]] || continue
    listing="$(unzip -Z1 "$archive")"
    grep -q '/SETUP.txt$' <<<"$listing" || { echo "SETUP.txt missing from $(basename "$archive")"; exit 1; }
    grep -q '/INSTALL.md$' <<<"$listing" || { echo "INSTALL.md missing from $(basename "$archive")"; exit 1; }
    grep -q '/Install-Unsigned-Mac.command$' <<<"$listing" || { echo "Installer missing from $(basename "$archive")"; exit 1; }
done

echo
printf 'Release verification passed.\n'
