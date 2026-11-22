# DivaExtraModuleSets

English · [简体中文](README.md)

A non-destructive outfit slot expansion mod for *Hatsune Miku: Project DIVA Mega Mix+* (PC Steam 1.03 / DivaModLoader): expands **Shared Sets up to A–Z (max 26)** and **Song Specific sets up to 1–10 (max 10)**, supports seamless **`F4` / `Shift+F4`** cycling across Song Select, PV Playlist Edit, and Customization menus, and includes an in-game **`F8`** configuration overlay.

## Features

- **Dual-Scope Slot Expansion**: Independently expand **Shared Sets (`A–Z`, configurable 1–26, vanilla 3)** and **Song Specific slots (`1–10`, configurable 1–10, vanilla 1)**.
- **Seamless In-Place Native Banner Overlay**: Renders right inside the game's native silver Set banner in Song Select (bottom-right), PV Playlist Edit's song picker (bottom-right), and Customization (top-right) using pixel-matched 16:9 mapping, supporting **all 9 official game languages** and automatically hiding during `NOW LOADING` screens and subpages without a `[F4]` banner.
- **Authentic Default Characters for Uncustomized Slots**: Untouched Shared slots default to Hatsune Miku Default (`moduleId = 0`), while untouched Song Specific slots automatically resolve each song's default performer modules from the game's PV database (`PvDb`).
- **Non-Destructive Hiding**: Lowering `shared_slots` or `song_slots` only hides higher slots from the UI and cycle rotation — **your saved outfits and button sounds are never deleted** and immediately reappear if you increase the counts later.
- **Separate Steam Cloud-Compatible Save (`DivaExtraModuleSets.dat`)**: Vanilla `A/B/C` and `Song Specific 1` continue using the game's native save unmodified. Extended slots are persisted atomically with CRC32 verification to `DivaExtraModuleSets.dat` inside your Steam save folder, syncing with Steam Cloud and leaving your vanilla save untouched if you uninstall.
- **In-Game Control Panel (`F8`)**: Features Pearl (default) and Classic themes, automatic `en` / `zh-CN` / `ja` localization, one-click jumping to any slot, manual save trigger, and a live enable/disable toggle.
- **Luma HDR10 / scRGB Adaptation**: Automatically detects 10-bit / FP16 swapchains and matches ReShade `[Luma]` paper white levels.

## Installation & Updates

1. Install [DivaModLoader](https://github.com/blueskythlikesclouds/DivaModLoader).
2. Exit the game completely, download the latest archive from [Releases](https://github.com/HYDlink/DivaExtraModuleSets/releases/latest), and extract the `DivaExtraModuleSets` folder into your game's `mods/` directory.
3. Enable `DivaExtraModuleSets` in your mod manager (compatible with `DivaKaraoke.dll`, `EdenProject_Core`, etc.).

- **Updating**: Exit the game and overwrite `mods/DivaExtraModuleSets` with the new release while keeping your existing `settings.ini`.
- **Uninstalling**: Remove the `mods/DivaExtraModuleSets` folder. Your vanilla save data remains completely intact (optionally delete `DivaExtraModuleSets.dat` in your Steam save directory if you wish to remove extended slots too).

## Controls

| Key / Action | Description |
| --- | --- |
| `F4` | Cycle forward through Shared Sets and Song Specific slots in Song Select, PV Playlist Edit song picker, and Customization |
| `Shift + F4` | Cycle backward through slots |
| `F8` | Toggle the in-game settings panel (customizable in `settings.ini`, supports `Ctrl+` / `Shift+` / `Alt+` modifiers) |

## Configuration (`settings.ini`)

| Key | Default | Description |
| --- | --- | --- |
| `enabled` | `true` | Enable extended slots (`false` immediately reverts to vanilla 3+1 without deleting stored extended data) |
| `shared_slots` | `6` | Visible Shared Sets (`1–26`, `A–Z`) |
| `song_slots` | `2` | Visible Song Specific slots (`1–10`) |
| `hud_mode` | `always` | In-place banner overlay mode: `always` / `extended_only` / `off` |
| `ui_theme` | `pearl` | Settings panel theme: `pearl` / `classic` |
| `language` | `auto` | Panel language: `auto` / `en` / `zh-CN` / `ja` |
| `hotkey` | `F8` | Settings panel toggle hotkey |
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

## Documentation & Reverse-Engineering Notes

- [Architecture & Engineering Lessons (Chinese)](docs/ARCHITECTURE_AND_LESSONS.md) — slot virtualization, PC 1.03 RVA tables, 6 real-world pitfalls & fixes (LTCG volatile registers, synchronous callback deadlocks, DX11 16384 font atlas limit, default module resolution), and official 9-language terminology.
- [Live Reverse-Engineering Trace (Chinese)](docs/LIVE_REVERSE_TRACE.md) — runtime observations from PC 1.03.

## License & Acknowledgments

Licensed under **GNU AGPL-3.0-or-later**. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party libraries and upstream reverse-engineering credits.
