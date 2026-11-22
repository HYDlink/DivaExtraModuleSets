# DivaExtraModuleSets 架构原理与实机踩坑总结

本文档面向希望了解本插件实现原理、以及同样基于《初音未来 Project DIVA Mega Mix+》（PC 1.03）开发 DivaModLoader（DML）插件的开发者，整理了本项目的核心架构、已核查原生内存布局与实机调试踩坑经验。

---

## 1. 总体架构与核心设计

原版游戏在存档结构体中将套装槽位硬编码为 **3 套全曲设定（A/B/C）+ 1 套曲目单独设定**。直接扩容原版存档结构体会导致云存档不兼容、玩家退坑卸载坏档，且极易与其他 Mod 冲突。因此本项目采用 **“槽位虚拟化 + 独立云存档 + 原位 DX11 视觉融合”** 方案：

### 1.1 槽位虚拟化与非破坏式隐藏

- **原生槽位零改动**：当玩家处于全曲 `A/B/C`（`index 0..2`）或单曲 `1`（`index 0`）时，Hook 直接透传给游戏原函数，继续读写原版游戏存档，与其他 Mod 与原生逻辑完全一致。
- **范围值始终合法**：无论当前切换到全曲 `D–Z` 还是单曲 `2–10`，写回原版存档 `SaveData + 0x169410` 的 `nativeScope` 永远限制在 `0..3`（见 [slot_store.cpp](../src/slot_store.cpp) 的 `NativeScopeFor`）。即使游戏突然存盘或玩家卸载本插件，原版存档也不会残留越界值。
- **非破坏式隐藏**：玩家在 `settings.ini` 或 `F8` 面板调小 `shared_slots` / `song_slots` 时，底层 [SlotStore](../src/slot_store.h) 与存档文件保留所有高编号槽位记录，仅收窄切换循环列表与显示上限；日后重新调大数字即可立即恢复。

### 1.2 独立 Steam 云存档（`DivaExtraModuleSets.dat`）

- 游戏保存目录由 `SaveDirString`（`0xDA2000`）动态读取（即 `%APPDATA%\SEGA\Project DIVA MEGA39's\Steam\<SteamID>\`）。
- 扩展数据以自定义二进制格式 `DEMS` v1 写入同目录下的 `DivaExtraModuleSets.dat`，自带：
  - **Payload CRC32 校验**（防坏档）；
  - **1.2 秒后台防抖 + `MoveFileExW` 原子替换**（连续按 `F4` 切槽合并写入，随游戏存盘及退出兜底同步，帧回调零阻塞）；
  - **只读与可写分离**：`GetSlotConfigConst` 浏览预览绝不向 `SlotStore` 插入空白歌曲记录，仅当实际修改套装且结果不同于默认配置时才落盘。

### 1.3 未设置槽位的默认角色解析

- **全曲设定 `D–Z`**：与原版 `A/B/C` 初始化逻辑（`0x1401DA6F0 → 0x1401D94C0`）完全一致，各歌者默认 `moduleId = 0`（初音未来默认模型）、饰品 `items = {-1, -1, -1, -1, -1}`、`flags = 0x1F`。
- **曲目单独设定 `2–10`**：按 `pvId` 在**锁外**依次调用游戏原生 PVDB 与角色默认模型接口（`GetPvDb → GetPvEntry → GetPerformerCount → GetPerformerChara → GetDefaultModuleId`），动态解析每位歌者的原生默认角色（如初音 `0`、镜音铃 `40`、镜音连 `41`、巡音流歌 `42`、KAITO `267`、MEIKO `363`）。只浏览不修改时同样视为默认态不膨胀存档；若旧版 `.dat` 曾存入 `moduleId < 0`，反序列化时自动清洗为合法默认值。

### 1.4 16:9 居中原位角标覆盖与精准门禁

游戏底部的 `全曲设定 / 曲目单独设定` 标签实质上是预烘烤贴图。本项目通过 DX11 + ImGui 在 `OnFrame` 阶段进行**居中 16:9 画布等比映射**（自动兼容超宽屏/带黑边窗口）：

- 用原版银灰底色 `#C5C5C5` 覆盖文字内框，完整保留白色外框与右侧 `[F4]` 键帽；
- 以 SEGA 原生初音青 `#19B7BD` 多向平滑描边 + 右下微投影 + 纯白本体绘制当前槽位名称；
- 自动适配 PC 1.03 官方 **9 种游戏语言**；
- 严格通过 `TaskWaitScreen`、`outerIdle`、`SceneManager`、`CustomizeHeader` 与 `visiblePvid` 门禁，确保绝不泄漏到 `NOW LOADING` 加载画面或无 `[F4]` 栏的子界面。

---

## 2. PC 1.03 已核查地址与内存布局速查表

> 所有 RVA 仅针对 Steam `DivaMegaMix.exe` PC 1.03（`TimeDateStamp = 1666920875`，`SizeOfImage = 602353664`）。初始化阶段在 [native_layout.h](../src/native_layout.h) 执行 PE 头严格校验，构建不符时安全停用全部原生 Hook。追踪细节另见 [LIVE_REVERSE_TRACE.md](LIVE_REVERSE_TRACE.md)。

| 类别 | 名称 / 偏移 | PC 1.03 RVA / 偏移 | 用途说明 |
| --- | --- | --- | --- |
| 存档存取 | `kGetSaveData` | `0x1D6510` | 获取运行时 `SaveData` 指针 |
| 存档存取 | `kGetSlotConfig` | `0x1D5E30` | 可写服装/饰品槽位记录获取器 `void*(save, int32_t pvId, uint32_t slot)` |
| 存档存取 | `kGetSlotConfigConst` | `0x1D5EC0` | 只读服装/饰品槽位记录获取器 `void*(save, int32_t pvId, int32_t slot)` |
| 存档存取 | `kGetSoundConfigThunk` / `Target` | `0x1D5F40` → `0xE5B6340` | 按键音 `0x188` 字节获取器（原入口为 5 字节 `E9` 跳桩，Hook `Target`） |
| 存档存取 | `kLoadSaveData` / `kSaveSaveData` | `0x1D7FB0` / `0x1D8280` | 随游戏读档/存档同步挂载并刷盘独立 `.dat` |
| 存档存取 | `kSaveDirString` / `kSpecialDemoModeFlag` | `0xDA2000` / `0xCC0FE3E` | 云存档目录 `std::string` / 试玩模式临时数组标志 |
| 存档偏移 | `kCommonSets` / `kScope` / `kDefaultSound` | `save + 0x168D68` / `0x169410` / `0x16941C` | 共通 A/B/C（`3 × 0x238`）、原生范围（`0..3`）、默认按键音（`0x188`） |
| 结构体尺寸 | `ModuleEquip` / `CustomizeSet` | `0x1C` / `0x238` 字节 | `moduleId(i32) + items[5](i32) + flags(u32)`；6 歌者 + 8B 保留 + 按键音 |
| 切换与预览 | `kCycleCustomizeScope` / `kApplySongOutfit` | `0x681A90` / `0x1EAD50` | 自定义页 `F4` 范围切换 / 进曲及选曲套用服装 |
| 选曲刷新 | `kPvSelApplyOutfit` / `kPvSelSetScopeUi` | `0x6F4360` / `0x6DF6D0` | 选曲切换槽位后刷新预览服装与右下角面板（`panel + 0xA8C` 为范围缓存） |
| 选曲面板 | `kPvSelGetPanel` / `kPvSelSetPanelPv` | `0x6DE400` / `0x6DE9F0` | 获取选曲底部面板及更新预览 PV（DML/ExPatch 下为 `int32_t`） |
| PVDB 默认 | `kGetPvDb` / `kGetPvEntry` | `0x4BBA20` / `0x4BB800` | 获取 PV 数据库及按 `pvId` 查找曲目条目 |
| PVDB 默认 | `kGetPerformerCount` / `Chara` / `kGetDefaultModuleId` | `0x4BCC80` / `0x4BCAB0` / `0x1D4F50` | 读取曲目歌者数量、角色编号及原生默认模型 ID |
| 场景门禁 | `kModeInfo` / `kPvSelectorSwitch` | `0xCC61078` / `0xCC5EF18` | 外层状态机（含 `shift` 切屏标志）与选曲任务对象 |
| 场景门禁 | `kVisiblePvidMember` | `0x6EFE8C` | 动态成员偏移立即数，用于区分播放列表左侧挑歌（`>0`）与右侧列表（`-1`） |
| 场景门禁 | `kCustomizeTask` / `controller` | `0xCC6F100` / `+0x78` | 自定义控制器（`+0x10` 为播放列表单曲覆盖表，`+0x968` 为右上角 Header，`+0x998` 为 `SceneManager`） |
| 场景门禁 | `kTaskWaitScreen` / `Vtable` / `dispState` | `0xCC0B530` / `0xCC2268` / `+0x70` | `NOW LOADING` 过渡幕布绘制状态（非 `0` 时禁止绘制角标） |
| 输入与语言 | `kGetInputState` / `kGetLanguageDirectory` | `0x2AC970` / `0x2C8D30` | 面板输入隔离 Hook 入口 / 当前游戏语言目录（`cn/tw/jp/en/kr/fr/ge/it/sp`） |

---

## 3. 六大实机踩坑与工程解法

1. **`Init()` 太早挂载破坏其他 Mod 的 `SIG_SCAN`（与 `DivaKaraoke.dll` 冲突）**
   - **现象**：在 `Init()` Hook `GetInputState` 会把函数头改写为 `E9` 跳转，导致按加载顺序排在后面的 `DivaKaraoke.dll` 执行字节码特征扫描失败并断言崩溃（`GImGui != 0`）。
   - **解法**：把所有 Detours Hook 挂载延迟到 DML 的 `PostInit()` 阶段，确保前序/同批插件已完成特征扫描，并完整保留已有跳板链（[mod.cpp](../src/mod.cpp)）。

2. **持锁调用游戏原函数导致同步回调自死锁**
   - **现象**：选曲页面按 `F4` 立即卡死崩溃。原因是 `HookPvSelSetScopeUi` 持有非递归 `std::mutex` 调用了 `originalPvSelSetScopeUi`，而后者内部会同步执行 `PvSelSetScopeUiSlow → ApplySongOutfit → HookApplySongOutfit`，试图再次加同一把锁导致死锁。
   - **解法**：确立“**调用任何 `original*()` 或游戏原生数据库前必须释放 `configMutex`**”的严格锁边界，必要时采用短锁检查 + 锁外解析的两段式循环（[native_hooks.cpp](../src/native_hooks.cpp)）。

3. **MSVC LTCG 跨内部调用保留易失寄存器 `r9`**
   - **现象**：自定义页面按 `F4` 随机访问违规崩溃。反汇编显示编译器在全程序优化（LTCG）下直接在调用点 `0x14069A1FD..0x14069A215` 跨过 `call CycleCustomizeScope (0x681A90)` 保留了易失寄存器 `r9` 中的对象指针；普通 C++ Hook 按标准 ABI 覆盖了 `r9` 导致返回即崩。
   - **解法**：编写极简 x64 MASM 寄存器保护跳板 [native_thunks.asm](../src/native_thunks.asm)（保存/恢复 `rcx/rdx/r8/r9/r10/r11` 与 16 字节栈对齐），并以汇编回归测试 [thunk_regression_tests.cpp](../tests/thunk_regression_tests.cpp) 防止编译器破坏契约。

4. **DX11 字体图集超过 `16384` 纹理高度上限导致闪退**
   - **现象**：新增 `36px` 高清角标字体后启动游戏在显卡纹理创建阶段崩溃（`E_INVALIDARG 0x80070057`，`pTexture = NULL`）。原因是全量繁简字库（2 万+字）与大字号辅助字体共用同一张 ImGui 纹理图集，高度超过 DirectX 11 的硬上限 `16384` 像素。
   - **解法**：主面板改用常用简体区 + 日文区 + 活跃文案字符；原位大号角标字体仅打包 9 种语言术语所需的约 246 个字形（整张图集回落至 `2048×2048`～`2048×4096`），并加入跑真实 DX11 GPU 设备创建的 [font_atlas_tests.cpp](../tests/font_atlas_tests.cpp) 自动校验。

5. **以 `moduleId = -1` 充当未设置默认值导致预览卡死与自定义空模**
   - **现象**：切到未设置的 `全曲设定 D` 或 `曲目单独设定 2` 时，选曲预览停留在上一套服装，进自定义则角色直接隐身、右下角模型名为空。
   - **解法**：实机核查证明 `-1` 在 `0x1404421A0` 查询中直接返回 `false`；将共通默认修正为 `0`，单曲默认按 `PvDb` 解析歌者默认 ID，并在反序列化旧版 `.dat` 时清洗残留 `-1`（[slot_store_tests.cpp](../tests/slot_store_tests.cpp)）。

6. **`NOW LOADING` 幕布残留与 PV 播放列表左侧挑歌误屏蔽**
   - **现象**：仅判 `CustomizeTask+0x78 != 0` 会使角标在切换过场时贴在 `NOW LOADING` 画面上；而仅按 `IsPlaylistMode()` 屏蔽会导致播放列表「添加新曲目」页失去原生 `[F4]` 位置覆盖。
   - **解法**：引入 `TaskWaitScreen + 0x70`、`outerIdle`、`SceneManager` 节点键、`CustomizeHeader[0]` 与 `visiblePvid` 五重校验，实现进入/退出过渡零闪烁、播放列表添加侧精准显示、退回列表后自动隐藏。

---

## 4. 官方 9 语言术语与 1080p 原位坐标参数

### 4.1 官方 9 语言套装术语表

| 游戏目录代码 | 语种 | 全曲共用（`A–Z`） | 单曲独立（单槽/默认） | 单曲扩展（`2–10`） |
| --- | --- | --- | --- | --- |
| `cn` | 简体中文 | `全曲设定 {letter}` | `曲目单独设定` | `曲目单独设定 {index}` |
| `tw` | 繁体中文 | `全曲設定 {letter}` | `曲目單獨設定` | `曲目單獨設定 {index}` |
| `jp` | 日文 | `共通設定 {letter}` | `曲別設定` | `曲別設定 {index}` |
| `en` | 英文 | `Shared Set {letter}` | `Song Specific` | `Song Specific {index}` |
| `kr` | 韩文 | `공통 설정 {letter}` | `곡별 설정` | `곡별 설정 {index}` |
| `fr` | 法文 | `Set partagé {letter}` | `Par chanson` | `Par chanson {index}` |
| `ge` | 德文 | `Allg. Set {letter}` | `Songspezifisch` | `Songspezifisch {index}` |
| `it` | 意大利文 | `Set comune {letter}` | `Per brano` | `Per brano {index}` |
| `sp` | 西班牙文 | `Set común {letter}` | `Por canción` | `Por canción {index}` |

### 4.2 原位银灰框 1920×1080 基准参数

| 场景 | 上底 `[tlX..trX, yTop]` | 下底 `[blX..brX, yBot]` | 基准字号 | 盖底色 / 描边色 |
| --- | --- | --- | --- | --- |
| 选曲 / 播放列表添加曲目（右下角） | `[1631..1836, 867]` | `[1597..1836, 901]` | `24.5f` | `#C5C5C5` / `#19B7BD` |
| 自定义（右上角） | `[1574..1847, 163]` | `[1528.5..1801.5, 208.5]` | `32.0f` | `#C5C5C5` / `#19B7BD` |
