# 使用与配置说明

[English](USAGE.en.md) · 简体中文 · [返回 README](../README.md)

## 功能细节

- **双维度槽位拓展**：同时拓展**全曲设定（`A–Z`，1–26 可调，原生 3）**与**曲目单独设定（`1–10`，1–10 可调，原生 1）**。
- **游戏原生原位融合角标**：直接在选曲右下角、播放列表「添加新曲目」右下角及自定义右上角的原生银灰套装框内覆盖绘制当前槽位名称（自动随游戏语言适配官方 **9 种语言**），并在 `NOW LOADING` 幕布与无栏位子页自动隐藏。
- **未设置槽位自动匹配原生默认角色**：未自定义的全曲扩展槽默认呈现初音未来（`moduleId = 0`）；未自定义的曲目单独扩展槽自动从游戏 PV 数据库读取该曲每位歌者的原生默认角色模型。
- **非破坏式隐藏**：调小可见槽位数时不会删除或覆盖高编号槽位已保存的服装与按键音配置；之后随时调大即可恢复读取。
- **独立 Steam 云存档存储**：原生 `A/B/C` 与单曲 `1` 仍走原版游戏存档；扩展槽位独立以 CRC32 校验 + 原子写入保存在原生存档目录的 `DivaExtraModuleSets.dat`，自动跟随 Steam 云同步。
- **游戏内控制面板（`F8`）**：支持 Pearl（珍珠白，默认）与 Classic 双主题、中/英/日三语自动跟随，可在选曲与自定义换装页面实时点击直达任意槽位、拖动槽位数量滑条、手动立即存盘，以及随时一键开关拓展并同步回退原生槽位。
- **Luma HDR10 / scRGB 自适应**：自动探测 HDR 后缓冲格式并匹配 ReShade `[Luma]` 白亮度。

## 更新与卸载

- **加载顺序与兼容性**：在 Mod 管理器中启用即可，推荐放在加载顺序末尾或靠后位置；已兼容 `DivaKaraoke.dll`、`EdenProject_Core` 等常用插件。
- **更新**：完全退出游戏后用新版覆盖 `mods/DivaExtraModuleSets` 目录；若想保留自定义热键与槽位数量设置，保留原有的 `settings.ini` 即可。
- **卸载**：直接删除 `mods/DivaExtraModuleSets` 文件夹即可，原版游戏存档不受任何影响；若希望连同扩展存档一起清理，可删除 Steam 云存档目录下的 `DivaExtraModuleSets.dat`。

## 配置项（`settings.ini`）

| 键名 | 默认值 | 说明 |
| --- | --- | --- |
| `enabled` | `true` | 是否启用拓展（设为 `false` 立即无缝回退原生 3+1 槽位，保留扩展存档不删） |
| `shared_slots` | `6` | 可见全曲设定槽位数（`1–26`，对应 `A–Z`） |
| `song_slots` | `2` | 可见曲目单独设定槽位数（`1–10`） |
| `hud_mode` | `always` | 原位套装角标显示策略：`always` / `extended_only` / `off` |
| `ui_theme` | `pearl` | 设置面板主题：`pearl`（珍珠白）/ `classic` |
| `language` | `auto` | 设置面板语言：`auto`（自动跟随游戏/系统）/ `zh-CN` / `en` / `ja` |
| `hotkey` | `F8` | 面板热键（`F1–F12` 及 `Ctrl+` / `Shift+` / `Alt+` 组合键） |
| `ui_color_output` | `auto` | 色彩输出：`auto` / `sdr` / `hdr10` / `scrgb` |

## 从源码构建与测试

需要 Windows x64、Visual Studio 2022（MSVC x64 + MASM `ml64` + Windows SDK）、CMake 3.24+ 与 PowerShell 7：

```powershell
# 编译 Release DLL、离线预览程序与全部 6 项 CTest 自动测试
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1

# 生成玩家分发压缩包与 SHA-256 校验值
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
```

自动验收测试覆盖：`SlotStoreTests`（槽位逻辑、非破坏隐藏、旧 `.dat` 清洗）、`I18nCheck`、`HardcodedZhCheck`、`DllCheck`、`ThunkRegressionTests`（x64 LTCG 寄存器保护汇编跳板与防重入测试）与 `FontAtlasTests`（真实 DX11 设备字体图集纹理尺寸校验）。
