# Progressive Pitch Delay v1.2.2

Progressive Pitch Delay is a real-time pitch-shifting multi-tap delay for macOS. Each repeat can move progressively higher or lower while retaining independent delay timing and level reduction.

## Highlights

- Progressive upward or downward pitch movement per repeat
- Normal and reverse repeat modes
- Up to 12 repeats
- Free-time and tempo-synchronised delay settings
- High-quality phase-vocoder pitch processing
- Factory presets
- Save and load user presets
- User preset names retained with Logic projects
- Logic automation support
- Automatic output headroom protection
- Tooltips, About information, Reset, and preset-folder access
- Audio Unit and VST3 formats

## Compatibility

- macOS 11 or later
- Apple Silicon and Intel support depend on the architectures selected by the local Xcode/CMake build
- Logic Pro uses the Audio Unit version

## Notes

The AU/VST3 identifiers and automation parameter IDs are unchanged from previous compatible builds. Existing Logic projects should retain their plug-in settings and automation.


## Installation and macOS security

These macOS bundles are not signed with an Apple Developer ID and are not notarized. Each AU/VST3 release ZIP includes `SETUP.txt`, `INSTALL.md`, and `Install-Unsigned-Mac.command` with user-folder installation, macOS security approval, Logic rescan, and basic-use instructions.
