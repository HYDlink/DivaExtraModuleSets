# DIVA 额外套装槽位拓展（DivaExtraModuleSets）

[English](README.en.md) · 简体中文

《初音未来 Project DIVA Mega Mix+》（PC Steam 1.03 / DivaModLoader）非破坏式服装套装拓展 Mod：将全曲共用扩展至 **A–Z（最多 26 套）**、单曲独立扩展至 **1–10（最多 10 套）**，支持在游戏原生选曲/播放列表编辑/自定义页面按 **`F4` / `Shift+F4`** 无缝切换，并按 **`F8`** 打开游戏内设置面板。

## 功能特性

- **双维度槽位拓展**：支持同时拓展**全曲设定（Shared Set `A–Z`，1–26 可调，原生 3）**与**曲目单独设定（Song Specific `1–10`，1–10 可调，原生 1）**。
- **游戏原生原位融合角标**：直接在选曲右下角、播放列表「添加新曲目」右下角及自定义右上角的原生银灰套装框内覆盖绘制当前槽位名称（自动随游戏语言适配官方 **9 种语言**），并精准跟随 `NOW LOADING` 幕布与无栏位子页自动隐藏，无多余悬浮遮挡。
- **未设置槽位自动匹配原生默认角色**：未自定义的全曲扩展槽默认呈现初音未来（`moduleId = 0`）；未自定义的曲目单独扩展槽会从游戏 PV 数据库自动读取该曲每位歌者的原生默认角色模型。
- **非破坏式隐藏**：在面板或 `settings.ini` 调小可见槽位数时，**不会删除或覆盖高编号槽位已保存的服装与按键音配置**；之后随时调大即可完整恢复读取。
- **独立 Steam 云存档存储**：原生 `A/B/C` 与单曲 `1` 仍走原版游戏存档；扩展槽位独立以 CRC32 校验 + 原子写入保存在原生存档目录的 `DivaExtraModuleSets.dat`，自动支持 Steam 云同步，卸载本 Mod 对原生存档零影响。
- **游戏内控制面板（`F8`）**：支持 Pearl（珍珠白，默认）与 Classic 双主题、中/英/日三语自动跟随、鼠标/键盘快速点击直达任意槽位、手动立即存盘、以及随时一键开关拓展功能并回退原生槽位。
- **Luma HDR10 / scRGB 自适应**：自动探测 HDR 后缓冲格式并匹配 ReShade `[Luma]` 白亮度，在 SDR 与 HDR 下保持一致观感。

## 安装与更新

1. 预先安装并配置好 [DivaModLoader](https://github.com/blueskythlikesclouds/DivaModLoader)。
2. 完全退出游戏，从 [Releases](https://github.com/HYDlink/DivaExtraModuleSets/releases/latest) 下载压缩包，将里面的 `DivaExtraModuleSets` 文件夹解压放入游戏目录的 `mods/` 文件夹。
3. 在 Mod 管理器中启用 `DivaExtraModuleSets`（推荐放在加载顺序末尾或靠后位置；已兼容 `DivaKaraoke.dll`、`EdenProject_Core` 等常用插件）。

- **更新**：退出游戏后用新版覆盖 `mods/DivaExtraModuleSets` 目录；若想保留自定义热键与槽位数量设置，保留原有的 `settings.ini` 即可。
- **卸载**：直接删除 `mods/DivaExtraModuleSets` 文件夹即可，原版游戏存档不受任何影响；若希望连同扩展存档一起清理，可删除 Steam 云存档目录下的 `DivaExtraModuleSets.dat`。

## 快捷键与操作

| 按键 / 操作 | 作用 |
| --- | --- |
| `F4` | 在选曲、播放列表添加曲目页、自定义换装页顺序循环切换套装槽位（仅进入播放列表独立锁定自定义页时按原生规则禁用） |
| `Shift + F4` | 反向循环切换套装槽位 |
| `F8` | 打开 / 关闭游戏内设置面板（可在 `settings.ini` 修改 `hotkey` 支持 `Ctrl+` / `Shift+` / `Alt+` 组合） |

## 主要配置项（`settings.ini`）

| 键名 | 默认值 | 说明 |
| --- | --- | --- |
| `enabled` | `true` | 是否启用拓展（设为 `false` 立即无缝回退原生 3+1 槽位，保留扩展存档不删） |
| `shared_slots` | `6` | 可见全曲设定槽位数（`1–26`，对应 `A–Z`） |
| `song_slots` | `2` | 可见曲目单独设定槽位数（`1–10`） |
| `hud_mode` | `always` | 原位套装角标显示策略：`always` / `extended_only` / `off` |
| `ui_theme` | `pearl` | 设置面板主题：`pearl`（珍珠白）/ `classic` |
| `language` | `auto` | 设置面板语言：`auto`（自动跟随游戏/系统）/ `zh-CN` / `en` / `ja` |
| `hotkey` | `F8` | 面板热键（`F1–F12` 及组合修饰键） |
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

## 技术文档与逆向笔记

- [架构原理与实机踩坑总结](docs/ARCHITECTURE_AND_LESSONS.md) — 槽位虚拟化原理、PC 1.03 地址速查表、6 个真实排障案例（LTCG 寄存器破坏 / 同步回调死锁 / DX11 字体图集上限 / 默认角色解析等）与 9 语言术语表。
- [实机逆向追踪记录](docs/LIVE_REVERSE_TRACE.md) — `PvSelectorSwitch` 与 `CustomizeSelectorSceneManager` 等实时追踪原始数据。

## 许可证与致谢

本项目以 **GNU AGPL-3.0-or-later** 开源，详见 [LICENSE](LICENSE) 与第三方依赖及逆向参考致谢 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
