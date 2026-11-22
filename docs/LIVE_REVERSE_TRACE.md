# 实机逆向溯源记录（PID 39116 / PC 1.03）

本文记录在 **停用 `DivaExtraModuleSets`** 后，对运行中的 `Hatsune Miku: Project DIVA Mega Mix+`（PC 1.03，`TimeDateStamp=1666920875`，`SizeOfImage=602353664`）使用只读 Frida 探针、`ReadProcessMemory` 内存对比及 `dumpbin /DISASM` 完成的实机逆向分析结果。

## 1. 实机运行环境与第三方 Mod Hook 拓扑（PID `39116`）

通过 `slot_trace_probe.js`（`inspectTargets`）与实机进程只读字节对比（`DivaMegaMix.exe` 内存 vs 磁盘镜像）确认：

- **`.bss` 代码节**（`VA 0x1000..0xB24A00`）权限为 `r-x`；**`.code` 代码节**（`VA 0xCD1D000..0x23D742B5`）及 `GetSoundConfigTarget`（`0x14E5B6340`）、`GetModuleId`（`0x1401D94F0`）、`SetModuleId`（`0x1401D9510`）在运行期权限为 `rwx`。
- Frida `Interceptor.attach` 对包括 `.code` 内 `GetSoundConfigTarget`（`0x14E5B6340`）在内的全部 16 个目标函数均顺利挂载并正常拦截返回，未触发 Denuvo / anti-tamper 崩溃或校验异常。
- 磁盘 vs 实机内存差异解析出以下 19 处已有 Mod Hook / 补丁（均在我们 Mod 之前存在）：

| RVA 地址 | 实机修改内容 / 跳转目标 | 归属模块 / 用途 |
|---|---|---|
| `0x1401D5C90` / `0x1401D5CB0` / `0x1401D5DD0` | `jmp -> dinput8.dll+0x34c2c / 0x34c6a / 0x34ca8` | DML（ExPatch / SaveData 扩展） |
| `0x1401D7FB0` (`LoadSaveData` / `CommitCustomizeEquip`) | `jmp 0x13fff04a8 -> NewClassics.dll+0x24d20` | New Classics |
| `0x1401D8280` (`SaveSaveData` / `ResetCustomizeEquip`) | `jmp 0x13fff04c0 -> NewClassics.dll+0x251f0` | New Classics |
| `0x1401D9DF0` | `jmp 0x13fff04d8 -> NewClassics.dll+0x4a6f0` | New Classics |
| `0x1401FCB60` / `0x1402033C0` / `0x14067EA00` | `jmp -> FutureToneCustomization.dll+0x2c80 / 0x2990 / 0x20f0` | Future Tone Customization |
| `0x1402AC970` (`GetInputState`) | `jmp 0x100270178 -> DivaInGameSearch.dll+0xedc0` | DivaInGameSearch |
| `0x14066B2D0` / `0x1406910B0` | `jmp -> DivaInGameSearch.dll+0x1ef20 / 0x15c50` | DivaInGameSearch |
| `0x140684A00` / `0x140687D10` / `0x140687D70` / `0x140687DE0` / `0x14069B610` | `jmp -> NewClassics.dll+0x41040 / 0x40a60 / 0x40ba0 / 0x40df0 / 0x40e90` | New Classics |
| `0x1406EDC40` (`PvSelApplyUiScope`) | `jmp 0x13fff0552 -> NewClassics.dll+0x467e0` | New Classics |
| `0x1406EE7B0` / `0x1406F3100` | `jmp -> NewClassics.dll+0x46ab0 / 0x46290` | New Classics |
| `0x1406DE563`, `0x1406DEA21`, `0x1406DEA28`, `0x1406DF6F4`, `0x1406DF748` | 单字节/NOP 补丁（移除 `int16` 符号扩展） | `PvSelSetPanelPv`（`0x1406DE9F0`）及周边被扩为 `int32` PV ID |

> 环境备注：当前代理 Shell 位于 `WinSta0\exebox-...` 隔离桌面，无法通过 `EnumWindows`/`diva_key.ahk` 直接枚举并按键操作位于 `WinSta0\Default` 的游戏窗口（除非桌面端预先启动 `desktop_bridge.ps1`）。

## 2. 实机抓取的调用链证据（`attract_events.json` + 反汇编）

1. **Attract PV / 选曲刷新链**（2.5 秒共采集 2,320 条调用事件）：
   - `0x1402C9B00` -> `PvSelApplyUiScope`（`0x1406EDC40`，已被 `NewClassics.dll` Hook）
   - `PvSelTask`（`0x1406DECD0`）-> `ApplySongOutfit(work, pvId=2, overrideTable=0)`（`0x1401EAD50`）
   - 在 `ApplySongOutfit` 内部依次调用 6 次 `[GetSlotConfigConst(save=0x14114c664, pvId=2, slot=0..5) @ +0x1EADAA -> GetModuleId(equipPtr) @ +0x1EADB5]`，随后在 `+0x1EAE3E` 调用 `GetSoundConfig(save, pvId=2, useGlobalDefault=0)`（`0x1401D5F40 -> jmp 0x14E5B6340`）。
2. **选曲菜单 F4 切槽链**（`0x1406F06DE..0x1406F0740`）：
   - 读取 `GetSaveData()+0x169410`（`0..3`），加 1 回环写回 `save+0x169410`
   - 调用 `PvSelApplyOutfit(selector, pvId)`（`0x1406F4360 -> jmp 0x1401EAD50 ApplySongOutfit`）
   - 获取面板后调用 `PvSelSetScopeUi(panel, scope)`（`0x1406DF6D0`）和 `PvSelSetPanelPv(panel, pvId)`（`0x1406DE9F0`）。
3. **自定义菜单 F4 切槽与换装写入链**：
   - `CycleCustomizeScope`（`0x140681A90`）共有 5 个调用点：`0x1406887D2`、`0x14068C7A2`、`0x140691292`、`0x140697488`、`0x14069A209`。
   - 换装预览 / 保存通过 `0x140681CE0`（`GetCustomSlotWrapper`）在 `0x140681D2A` 调用 `GetSlotConfig`（`0x1401D5E30`）取回 `ModuleEquip*`，并在 `0x140681E13` 调用 `SetModuleId`（`0x1401D9510 -> jmp 0x14E810860`）写入模块 ID 与道具位。

## 3. 闪退根因实锤

### 根因一（选曲界面按 F4 / 切换槽位立即闪退）：非递归 `std::mutex` 同线程重入自死锁
- 旧代码 `HookPvSelSetScopeUi` 在持有 `std::lock_guard lock(st.configMutex)`（非递归互斥量）的情况下，直接调用 `originalPvSelSetScopeUi(panel, scope)`。
- 反汇编 `0x1406DF6D0`（`PvSelSetScopeUi`）证实：当 `scope != [rcx+0xA8C]` 时，`0x1406DF6DE jne 0x1406DF720`（`PvSelSetScopeUiSlow`）在 `0x1406DF756` 调用 `0x1406DF6F0`，并通过 `0x1406DF711 jmp 0x1401EAD50` 同步进入 `ApplySongOutfit`。
- `HookApplySongOutfit`（以及内部触发的 `HookGetSlotConfigConst`、`HookGetSoundConfig`）在同一线程上再次对同一个非递归 `st.configMutex` 加锁 -> **立即触发 MSVC `_Mtx_lock` 未定义死锁 / `abort()` 闪退**。
- 同类锁未提前释放隐患同时存在于 `HookGetSlotConfig`、`HookGetSlotConfigConst`、`HookGetSoundConfig` 与 `HookCycleCustomizeScope`。

### 根因二（自定义界面切换槽位 / 切换模型闪退）：MSVC LTCG 跨调用易失寄存器（`r9` 等）破坏
- 反汇编自定义页面 F4 回调 `0x14069A1FD..0x14069A215` 实锤：
  ```asm
  000000014069A1FD: mov  r9, qword ptr [rax+968h]
  000000014069A204: test r9, r9
  000000014069A207: je   000000014069A230
  000000014069A209: call 0000000140681A90   ; CycleCustomizeScope（原函数从未使用 r9！）
  000000014069A20E: test al, al
  000000014069A210: je   000000014069A230
  000000014069A212: mov  rcx, r9            ; LTCG 假定 r9 未被 CycleCustomizeScope 破坏！
  000000014069A215: call 000000014067DA40
  ```
- 原生函数 `CycleCustomizeScope`（`0x140681A90`）、`GetSlotConfig`（`0x1401D5E30`）、`GetSlotConfigConst`（`0x1401D5EC0`）、`GetSoundConfigTarget`（`0x14E5B6340`）、`ApplySongOutfit`（`0x1401EAD50`）、`PvSelSetScopeUi`（`0x1406DF6D0`）均处于 MSVC `/GL` 全程序优化（LTCG）调用图中。纯 C++ Detour Hook 会覆盖 `r9/r10/r11/xmm0..xmm5`（以及快速路径上的 `rcx/rdx/r8`），返回后调用方解引用被污染的 `r9` 直接触发 Access Violation 崩溃。
- **修复要求**：与 `DivaModLoader`（`SaveDataImp.asm` / `StrArrayImp.asm`）保持一致，必须为所有 LTCG 敏感函数外挂 MASM x64 寄存器保护跳板（保存并恢复 `rcx, rdx, r8, r9, r10, r11, xmm0..xmm5`，仅保留 `rax` 返回值）。

### 次要结构性发现
1. **`PvSelSetPanelPv`（`0x1406DE9F0`）参数宽度**：DML/ExPatch 将原本 `movsx edx, ax` 补丁为 `int32_t`（`0x1406DEA21`/`0x1406DEA28`），因此 `SafeRefreshPvSelector` 应使用 `int32_t` 传参而非截断为 `int16_t`。
2. **`ModuleEquip::flags` 默认值**：`attract_events.json` 中真实默认记录字节为 `flags = 0x1F`（低 5 位对应 5 个自定义饰品槽位启用标志），`ApplySongOutfit` 在 `0x1401EADD4..0x1401EAE0D` 逐位解析 `[r8+18h]` 的 bit 0..4；新建空白槽位记录应以 `0x1F` 为默认标志。
