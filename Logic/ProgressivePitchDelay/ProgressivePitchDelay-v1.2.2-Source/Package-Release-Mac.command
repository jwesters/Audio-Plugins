#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.2.2"
PROJECT="ProgressivePitchDelay"
PRODUCT="Progressive Pitch Delay"
ARTEFACTS="$ROOT/build/ProgressivePitchDelay_artefacts/Release"
AU_SOURCE="$ARTEFACTS/AU/$PRODUCT.component"
VST3_SOURCE="$ARTEFACTS/VST3/$PRODUCT.vst3"
DIST="$ROOT/dist"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ppd-release.XXXXXX")"
SIGNING_IDENTITY="${SIGNING_IDENTITY:--}"

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() {
    echo "Error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "Required command not found: $1"
}

for command_name in ditto rsync shasum unzip codesign; do
    require_command "$command_name"
done

[[ -d "$AU_SOURCE" ]] || fail "Release AU not found. Run Build-Mac.command first."
[[ -d "$VST3_SOURCE" ]] || fail "Release VST3 not found. Run Build-Mac.command first."
[[ -f "$ROOT/LICENSE" ]] || fail "LICENSE is missing."
[[ -f "$ROOT/INSTALL.md" ]] || fail "INSTALL.md is missing."
[[ -f "$ROOT/RELEASE_NOTES.md" ]] || fail "RELEASE_NOTES.md is missing."
[[ -f "$ROOT/SETUP.txt" ]] || fail "SETUP.txt is missing."
[[ -x "$ROOT/Install-Unsigned-Mac.command" ]] || fail "Install-Unsigned-Mac.command is missing or not executable."

check_bundle_version() {
    local bundle="$1"
    local plist="$bundle/Contents/Info.plist"
    [[ -f "$plist" ]] || fail "Info.plist is missing from $bundle"

    local found_version
    found_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist" 2>/dev/null || true)"
    [[ "$found_version" == "$VERSION" ]] || \
        fail "Version mismatch in $(basename "$bundle"): expected $VERSION, found ${found_version:-unknown}"
}

check_bundle_version "$AU_SOURCE"
check_bundle_version "$VST3_SOURCE"

rm -rf "$DIST"
mkdir -p "$DIST"

AU_PACKAGE="$WORK/${PROJECT}-v${VERSION}-macOS-AU"
VST3_PACKAGE="$WORK/${PROJECT}-v${VERSION}-macOS-VST3"
SOURCE_PACKAGE="$WORK/${PROJECT}-v${VERSION}-Source"

mkdir -p "$AU_PACKAGE" "$VST3_PACKAGE" "$SOURCE_PACKAGE"
ditto "$AU_SOURCE" "$AU_PACKAGE/$PRODUCT.component"
ditto "$VST3_SOURCE" "$VST3_PACKAGE/$PRODUCT.vst3"
cp "$ROOT/LICENSE" "$ROOT/INSTALL.md" "$ROOT/SETUP.txt" "$ROOT/RELEASE_NOTES.md" "$ROOT/Install-Unsigned-Mac.command" "$AU_PACKAGE/"
cp "$ROOT/LICENSE" "$ROOT/INSTALL.md" "$ROOT/SETUP.txt" "$ROOT/RELEASE_NOTES.md" "$ROOT/Install-Unsigned-Mac.command" "$VST3_PACKAGE/"
chmod +x "$AU_PACKAGE/Install-Unsigned-Mac.command" "$VST3_PACKAGE/Install-Unsigned-Mac.command"

sign_bundle() {
    local bundle="$1"

    if [[ "$SIGNING_IDENTITY" == "-" ]]; then
        echo "Ad-hoc signing $(basename "$bundle")"
        codesign --force --deep --sign - "$bundle"
    else
        echo "Developer ID signing $(basename "$bundle")"
        codesign --force --deep --options runtime --timestamp \
            --sign "$SIGNING_IDENTITY" "$bundle"
    fi

    codesign --verify --deep --strict --verbose=2 "$bundle"
}

sign_bundle "$AU_PACKAGE/$PRODUCT.component"
sign_bundle "$VST3_PACKAGE/$PRODUCT.vst3"

rsync -a \
    --exclude '/build/' \
    --exclude '/dist/' \
    --exclude '/.git/' \
    --exclude '.DS_Store' \
    "$ROOT/" "$SOURCE_PACKAGE/"

AU_ZIP="$DIST/${PROJECT}-v${VERSION}-macOS-AU.zip"
VST3_ZIP="$DIST/${PROJECT}-v${VERSION}-macOS-VST3.zip"
SOURCE_ZIP="$DIST/${PROJECT}-v${VERSION}-Source.zip"

ditto -c -k --sequesterRsrc --keepParent "$AU_PACKAGE" "$AU_ZIP"
ditto -c -k --sequesterRsrc --keepParent "$VST3_PACKAGE" "$VST3_ZIP"
ditto -c -k --sequesterRsrc --keepParent "$SOURCE_PACKAGE" "$SOURCE_ZIP"

for archive in "$AU_ZIP" "$VST3_ZIP" "$SOURCE_ZIP"; do
    unzip -tq "$archive" >/dev/null || fail "ZIP integrity test failed: $archive"
done

(
    cd "$DIST"
    shasum -a 256 \
        "$(basename "$AU_ZIP")" \
        "$(basename "$VST3_ZIP")" \
        "$(basename "$SOURCE_ZIP")" \
        > SHA256SUMS.txt
)

cp "$ROOT/RELEASE_NOTES.md" "$DIST/"
cp "$ROOT/CHANGELOG.md" "$DIST/"

cat <<SUMMARY

Release package complete.

Created:
  $AU_ZIP
  $VST3_ZIP
  $SOURCE_ZIP
  $DIST/SHA256SUMS.txt
  $DIST/RELEASE_NOTES.md
  $DIST/CHANGELOG.md

Signing identity:
  $SIGNING_IDENTITY

Use SIGNING_IDENTITY="Developer ID Application: Your Name (TEAMID)" before this
command to create Developer ID-signed bundles. The default '-' identity creates an ad-hoc signature only. It is not a
Developer ID signature and is not notarized. Recipient setup instructions are
in SETUP.txt and INSTALL.md.
SUMMARY
