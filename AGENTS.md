# DivaExtraModuleSets 协作规范

## 共用知识与工具引用

- UI 规范、字体与 DX11/HDR 宿主架构：`../shared/docs/UI_DESIGN_AND_FRAMEWORK.md`
- 实机自测与截图方案（AHK v2 + REA/Frida + NativeCapture / D3D11 后缓冲）：`../shared/docs/AGENT_SELF_TEST_AND_AUTOMATION.md`
- PC 1.03 逆向索引与避坑清单：`../shared/docs/REVERSE_ENGINEERING_INDEX.md`（地址与偏移事实源：`../DivaInGameSearch/src/native_layout.h` 及 `src/native_layout.h`）
- 多语言与工程约定：`../shared/docs/I18N_AND_BUILD_CONVENTIONS.md`

## 通用红线

- 未经用户明确要求，禁止修改或自增版本号。
- 玩家可见文案写入 `locales/*.toml`（仅支持 `zh-CN`、`en`、`ja`），不在代码硬编码；开发者诊断日志保持中文。
- 游戏内帧回调不得阻塞等待，不在 `DllMain` 中初始化业务；保留其他 Mod 的输入与函数 hook 链。
- 自动单测、离屏 WARP 预览、静态反汇编不能冒充实际游戏通过；实机验收须保留操作前后截图并开图核对，配合独立字段读回。
- 键盘/界面自动化统一使用 AutoHotkey v2，逐步断言、失败即停；不得强杀游戏、自动演奏或未授权改写存档/其他 Mod/加载顺序。
- 调小 `shared_slots` 或 `song_slots` 时，**严禁截断或删除**底层 `SlotStore` 与 `DivaExtraModuleSets.dat` 中已保存的高编号槽位数据；再次调大配置必须能恢复读取。
