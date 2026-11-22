# Usage & Configuration

English · [简体中文](USAGE.md) · [Back to README](../README.en.md)

## Feature Details

- **Dual-Scope Slot Expansion**: Independently expand **Shared Sets (`A–Z`, configurable 1–26, vanilla 3)** and **Song Specific slots (`1–10`, configurable 1–10, vanilla 1)**.
- **Seamless In-Place Native Banner Overlay**: Renders right inside the game's native silver Set banner in Song Select (bottom-right), PV Playlist Edit's song picker (bottom-right), and Customization (top-right) using pixel-matched 16:9 mapping, supporting **all 9 official game languages** and automatically hiding during `NOW LOADING` screens and subpages without a `[F4]` banner.
- **Authentic Default Characters for Uncustomized Slots**: Untouched Shared slots default to Hatsune Miku Default (`moduleId = 0`), while untouched Song Specific slots automatically resolve each song's default performer modules from the game's PV database (`PvDb`).
- **Non-Destructive Hiding**: Lowering `shared_slots` or `song_slots` only hides higher slots from the UI and cycle rotation — saved outfits and button sounds are never deleted and immediately reappear when you increase the counts later.
- **Separate Steam Cloud-Compatible Save (`DivaExtraModuleSets.dat`)**: Vanilla `A/B/C` and `Song Specific 1` continue using the game's native save unmodified. Extended slots are persisted atomically with CRC32 verification to `DivaExtraModuleSets.dat` inside your Steam save folder, syncing with Steam Cloud.
- **In-Game Control Panel (`F8`)**: Features Pearl (default) and Classic themes, automatic `en` / `zh-CN` / `ja` localization, live one-click slot quick-jump and slot-count sliders across both Song Select and Customization menus, manual save trigger, and a live enable/disable toggle with immediate model/slot refresh.
- **Luma HDR10 / scRGB Adaptation**: Automatically detects 10-bit / FP16 swapchains and matches ReShade `[Luma]` paper white levels.

## Updating & Uninstalling

- **Load Order & Compatibility**: Placing `DivaExtraModuleSets` near the end of your load order is recommended; tested compatible with `DivaKaraoke.dll`, `EdenProject_Core`, etc.
- **Updating**: Exit the game completely and overwrite `mods/DivaExtraModuleSets` with the new release while keeping your existing `settings.ini`.
- **Uninstalling**: Remove the `mods/DivaExtraModuleSets` folder. Your vanilla save data remains untouched (optionally delete `DivaExtraModuleSets.dat` in your Steam save directory if you also want to remove extended slot data).

## Configuration (`settings.ini`)

| Key | Default | Description |
| --- | --- | --- |
| `enabled` | `true` | Enable extended slots (`false` immediately reverts to vanilla 3+1 without deleting stored extended data) |
| `shared_slots` | `6` | Visible Shared Sets (`1–26`, `A–Z`) |
| `song_slots` | `2` | Visible Song Specific slots (`1–10`) |
| `hud_mode` | `always` | In-place banner overlay mode: `always` / `extended_only` / `off` |
| `ui_theme` | `pearl` | Settings panel theme: `pearl` / `classic` |
| `language` | `auto` | Panel language: `auto` / `en` / `zh-CN` / `ja` |
| `hotkey` | `F8` | Settings panel toggle hotkey (`F1–F12` with optional `Ctrl+` / `Shift+` / `Alt+`) |
| `ui_color_output` | `auto` | HDR/SDR output mode: `auto` / `sdr` / `hdr10` / `scrgb` |

## Building & Testing from Source

Requires Windows x64, Visual Studio 2022 (MSVC x64 + MASM `ml64` + Windows SDK), CMake 3.24+, and PowerShell 7:

```powershell
# Build Release DLL, offline preview executable, and run all 6 CTest suites
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1

# Package player-ready release zip + SHA-256
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
```

Test suites include `SlotStoreTests`, `I18nCheck`, `HardcodedZhCheck`, `DllCheck`, `ThunkRegressionTests` (x64 LTCG register preservation & re-entrancy checks), and `FontAtlasTests` (real DX11 GPU font atlas texture validation).
