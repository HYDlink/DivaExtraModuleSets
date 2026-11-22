# 依赖与数据来源

本项目代码以 GNU AGPL-3.0-or-later 发布，许可证见 LICENSE。发行同时附带可重建源码与第三方许可声明。

| 内容 | 固定版本或提交 | 来源与许可证 |
| --- | --- | --- |
| Dear ImGui 核心、Win32、DX11 后端 | v1.88，提交 9aae45eb4a05a5a1f96be1ef37eb503a12ceb889（含 FramebufferScale 补丁） | https://github.com/ocornut/imgui ，MIT |
| Microsoft Detours | v4.0.1，e4bfd6b03e50de46b47abfbd1e46b384f0c5f833 | https://github.com/microsoft/Detours ，MIT |
| toml++ | v3.1.0 | https://github.com/marzer/tomlplusplus ，MIT |
| DIVA Mod Loader 接口与 SaveData 机制参考 | 0abc2c89229c4b307bfa1555656c54c2e5cff480 | https://github.com/blueskythlikesclouds/DivaModLoader ，MIT |
| QuantumSwitch / NewClassics / ReDIVA / MikuMikuSwitchPlugin | 见 `third_party/vendor/licenses/` | PC 1.03 菜单与选曲状态布局参考，各附原许可证文本 |

中文字体与日文字体在运行时只读使用 Windows 系统字体目录（`%WINDIR%\Fonts`），不重新分发字体文件。
