# Progressive Pitch Delay v1.2.2 — macOS Setup

## Important security information

This release is **not signed with an Apple Developer ID and is not notarized by Apple**. Install it only when it came from a source you trust. macOS may block it during the first plug-in scan.

Logic Pro uses the **Audio Unit** version. The VST3 build is for compatible VST3 hosts; Logic does not load VST3 plug-ins.

## Recommended installation

1. Quit Logic Pro and all other audio applications.
2. Unzip the AU or VST3 release.
3. Control-click `Install-Unsigned-Mac.command`, choose **Open**, and confirm.
4. Restart the Mac before opening the host.

The included script installs only to the current user’s Library, does not use `sudo`, and does not alter system-wide Gatekeeper settings. It also removes the download quarantine attribute from the installed plug-in bundle.

If macOS blocks the script, try opening it once, then open **System Settings → Privacy & Security**, scroll to **Security**, and use **Open Anyway** for the blocked item. Return to the extracted folder and run the script again.

## Manual Audio Unit installation for Logic Pro

1. Quit Logic Pro.
2. Copy `Progressive Pitch Delay.component` to:

   `~/Library/Audio/Plug-Ins/Components/`

3. In Terminal, run:

```bash
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/Components/Progressive Pitch Delay.component"
```

4. Restart the Mac and open Logic Pro.
5. Insert the effect from:

   `Audio FX → Audio Units → jwesters → Progressive Pitch Delay → Stereo`

If it does not appear, open **Logic Pro → Settings → Plug-in Manager**, select Progressive Pitch Delay, and choose **Reset & Rescan Selection**. As a later troubleshooting step, use **Full Audio Unit Reset**, quit Logic, and reopen it.

## Manual VST3 installation

1. Quit the VST3-compatible host.
2. Copy `Progressive Pitch Delay.vst3` to:

   `~/Library/Audio/Plug-Ins/VST3/`

3. In Terminal, run:

```bash
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Progressive Pitch Delay.vst3"
```

4. Restart the Mac, open the host, and rescan VST3 plug-ins when required.

## Basic use

1. Insert Progressive Pitch Delay as an audio effect.
2. Select **Normal** or **Reverse**.
3. Select **Higher** or **Lower**.
4. Choose the semitone change and number of repeats.
5. Set a free delay time, or enable Tempo Sync and select a division.
6. Balance Original, First Repeat, and Reduction / Repeat.
7. Use High quality for the cleanest result; use Draft to reduce processing.
8. Keep Auto Headroom enabled for loud or overlapping repeats.

## Presets

- Select factory presets from the preset menu.
- **Save** creates a `.ppdpreset` user preset.
- **Load** opens a saved user preset.
- **Folder** reveals the most recently used preset folder.
- **Reset** restores the default settings.
- The plug-in state and the loaded user-preset name are retained in Logic projects.

## Removal

Delete the corresponding bundle and restart the host:

- AU: `~/Library/Audio/Plug-Ins/Components/Progressive Pitch Delay.component`
- VST3: `~/Library/Audio/Plug-Ins/VST3/Progressive Pitch Delay.vst3`

See `SETUP.txt` in the release package for the full plain-text walkthrough.
