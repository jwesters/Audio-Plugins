# Building Progressive Pitch Delay

Progressive Pitch Delay is a JUCE-based Audio Unit and VST3 effect for macOS.
JUCE is not included in this source distribution and must be obtained
separately.

## Tested build environment

- macOS 11 or newer
- JUCE 9.0.0
- CMake 3.22 or newer
- Xcode and the Xcode Command Line Tools
- A C++17-compatible compiler

By default, the project builds for the architecture of the Mac performing the
build.

## Build with the included script

Open Terminal, change to the source folder, and run:

```bash
./Build-Mac.command "/absolute/path/to/JUCE-9.0.0"
```

Example:

```bash
./Build-Mac.command "/Users/yourname/Developer/JUCE-9.0.0"
```

The script creates a Release build and produces:

```text
build/ProgressivePitchDelay_artefacts/Release/AU/Progressive Pitch Delay.component
build/ProgressivePitchDelay_artefacts/Release/VST3/Progressive Pitch Delay.vst3
```

## Build manually with CMake

From the source folder, run:

```bash
cmake -S . -B build \
  -DJUCE_PATH="/absolute/path/to/JUCE-9.0.0" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release --parallel
```

## Install the local build

After a successful build, run:

```bash
./Install-Mac.command
```

This installs the plug-ins for the current user at:

```text
~/Library/Audio/Plug-Ins/Components/Progressive Pitch Delay.component
~/Library/Audio/Plug-Ins/VST3/Progressive Pitch Delay.vst3
```

The install script applies ad-hoc signing for local testing. The plug-ins are
not Developer ID signed or notarized.

## Validate the Audio Unit

Run:

```bash
./Validate-Mac.command
```

A successful validation ends with:

```text
AU VALIDATION SUCCEEDED
```

## Create release archives

After building and validating, run:

```bash
./Package-Release-Mac.command
./Verify-Release-Mac.command
```

The completed release files are written to the `dist` folder.

## JUCE licensing

Anyone compiling, modifying, or redistributing Progressive Pitch Delay must
obtain JUCE separately and comply with the JUCE licence applicable to their
use. See `THIRD-PARTY-NOTICES.md` for more information.
