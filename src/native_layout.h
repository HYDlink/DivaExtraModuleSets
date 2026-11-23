#pragma once
#include <Windows.h>
#include <cstdint>

// 已核查 PC 构建（DivaMegaMix.exe 1.03）的原生地址与内存布局。
// 所有地址均来自 DivaInGameSearch/src/native_layout.h 及本项目的静态核查；
// 换构建必须重新核查，构建不符时禁止安装任何原生 Hook。
namespace diva::native {

namespace build {
constexpr uint32_t kTimestamp=1666920875;
constexpr uint32_t kImageSize=602353664;
}

namespace Mode {
enum Value : int32_t {
    Startup=0,Advertise=1,Game=2,DataTest=3,TestMode=4,AppError=5,
    CsMenu=6,Customize=7,Gallery=8,MenuSwitch=9,GameSwitch=10,TshirtEdit=11,
};
}

namespace ModeSub {
enum Value : int32_t {
    PvSel=5,PlaylistSel=6,Game=7,
    CsCustomizeSel=36,CsUnk37=37,
};
}

namespace ModeProc {
enum Value : int32_t { Init=0,Ctrl=1,Dest=2 };
}

namespace SceneKey {
enum Value : int32_t {
    Loading=0,TopMenu=1,Module=2,Hairstyle=3,Item=4,Tshirt=5,Sound=6,Config=7,Finish=8
};
}

namespace rva {
// ---- 外层模式与界面 ----
constexpr uintptr_t kModeInfo=0xCC61078;             // [ReDIVA] ModeInfo 实例
constexpr uintptr_t kPvSelectorSwitch=0xCC5EF18;     // [QS] 指向 PVSelectorSwitch 的指针
constexpr uintptr_t kPvSelectorSwitchCtrl=0x6EDC40;  // [NC] 选曲控制帧
constexpr uintptr_t kVisiblePvidMember=0x6EFE8C;     // [QS] 选曲可见 PVID 成员偏移立即数地址
constexpr uintptr_t kPvSelApplyOutfit=0x6F4360;      // [自拟] void(pvSelector, int pvId)，选曲切换槽位后刷新预览服装
constexpr uintptr_t kPvSelSetScopeUi=0x6DF6D0;       // [自拟] void(panel, int scope)，Switch 选曲右上角套装图标与文字刷新
constexpr uintptr_t kPvSelGetPanel=0x6DE400;         // [自拟] uintptr_t(pvSelector + 0x1A50)
constexpr uintptr_t kPvSelSetPanelPv=0x6DE9F0;       // [自拟] void(panel, int16 pvId)
constexpr uintptr_t kCustomizeTask=0xCC6F100;        // [自拟] 自定义任务静态本体，+0x78 非空代表处于自定义菜单
constexpr uintptr_t kCustomizeControllerCtrl=0x67F2B0; // [自拟] uint8_t(controller)，自定义控制器每帧回调
constexpr uintptr_t kSceneTopMenuChangeScope=0x69A1F0;   // [自拟] void(scene)，TopMenu 切套装范围
constexpr uintptr_t kSceneModuleChangeScope=0x691270;    // [自拟] void(scene)，Module 切套装范围
constexpr uintptr_t kSceneHairstyleChangeScope=0x6887B0; // [自拟] void(scene)，Hairstyle 切套装范围
constexpr uintptr_t kSceneItemChangeScope=0x68C780;      // [自拟] void(scene)，Item 切套装范围
constexpr uintptr_t kSceneSoundChangeScope=0x697420;     // [自拟] void(scene)，Sound 切套装范围
constexpr uintptr_t kSceneManagerVtable=0xCC4E38;    // [RTTI] CustomizeSelectorSceneManager 虚表
constexpr uintptr_t kTaskWaitScreen=0xCC0B530;       // [RTTI] TaskWaitScreen（NOW LOADING 幕布）静态实例
constexpr uintptr_t kTaskWaitScreenVtable=0xCC2268;  // [RTTI] TaskWaitScreen 虚表

// ---- 存档与套装读写 ----
constexpr uintptr_t kGetSaveData=0x1D6510;           // [NC] uintptr_t()
constexpr uintptr_t kGetSlotConfig=0x1D5E30;         // [自拟] void*(save, int pvId, uint32_t slot) 可写获取器
constexpr uintptr_t kGetSlotConfigConst=0x1D5EC0;    // [自拟] void*(save, int pvId, int32_t slot) 只读获取器
constexpr uintptr_t kGetSoundConfigThunk=0x1D5F40;   // [自拟] jmp 0xE5B6340 跳转桩
constexpr uintptr_t kGetSoundConfigTarget=0xE5B6340; // [自拟] void*(save, int pvId, char useGlobalDefault)
constexpr uintptr_t kLoadSaveData=0x1D7FB0;          // [DML] void(save)
constexpr uintptr_t kSaveSaveData=0x1D8280;          // [DML] void(save)
constexpr uintptr_t kSaveDirString=0xDA2000;         // [自拟] MSVC std::string，保存当前 Steam 存档目录
constexpr uintptr_t kSpecialDemoModeFlag=0xCC0FE3E;  // [自拟] 非零时使用临时试玩数组
constexpr uintptr_t kGetDefaultModuleId=0x1D4F50;    // [自拟] int32_t(int32_t chara) 角色默认模型 ID
constexpr uintptr_t kGetModuleTable=0x441B60;        // [自拟] uintptr_t() 模型表对象（+0 begin, +8 end, 步长 0xC0）
constexpr uintptr_t kGetPvDb=0x4BBA20;               // [自拟] uintptr_t() PV 数据库指针
constexpr uintptr_t kGetPvEntry=0x4BB800;            // [自拟] uintptr_t(pvDb, int32_t pvId)
constexpr uintptr_t kGetPerformerCount=0x4BCC80;     // [自拟] uint64_t(pvEntry)
constexpr uintptr_t kGetPerformerChara=0x4BCAB0;     // [自拟] int32_t(pvEntry, int32_t slot)
constexpr uintptr_t kGetPerformerCostume=0x4BCEE0;   // [自拟] int32_t(pvEntry, int32_t slot) 曲目推荐/默认服装 COS 编号

// ---- 范围切换与游玩应用 ----
constexpr uintptr_t kCycleCustomizeScope=0x681A90;   // [自拟] uint8_t()，所有自定义子页 F4 共同调用
constexpr uintptr_t kApplySongOutfit=0x1EAD50;       // [自拟] void(work, int pvId, uintptr_t overrideTable)
constexpr uintptr_t kIsPlaylistMode=0x1F1990;        // [自拟] uint8_t()

// ---- 输入与语言 ----
constexpr uintptr_t kGetInputState=0x2AC970;         // [NC] void*(int player)
constexpr uintptr_t kGetLanguageDirectory=0x2C8D30;  // [自拟] const char*()
constexpr uintptr_t kGetLanguageDirectoryCall=0x239835; // [自拟] call 0x2C8D30 校验指令
}

namespace save_offsets {
constexpr uintptr_t kCommonSets=0x168D68;    // [自拟] 共通套装 A/B/C 数组起点（3 * 0x238）
constexpr uintptr_t kScope=0x169410;         // [自拟] int32_t 当前范围（0..2 共通，3 单曲）
constexpr uintptr_t kDefaultSound=0x16941C;  // [自拟] 0x188 字节默认按键音效配置
constexpr size_t kCustomizeSetSize=0x238;
constexpr size_t kSlotRecordSize=0x1C;
}

namespace pv_selector {
constexpr uintptr_t kState=0x6C;
constexpr int32_t kStateIdle=6;
constexpr uintptr_t kPanelWrapper=0x1A50;
constexpr uintptr_t kPvId=0x262A4;
constexpr uintptr_t kPanelLastScope=0xA8C;
constexpr int32_t kVisiblePvidBias=0x9C8;
constexpr int32_t kVisiblePvidMemberMax=0x100000;
}

namespace mode_info {
constexpr uintptr_t kCurrent=rva::kModeInfo+0x00;
constexpr uintptr_t kProc=rva::kModeInfo+0x0C;
constexpr uintptr_t kShift=rva::kModeInfo+0x10;
constexpr uintptr_t kSubCurrent=rva::kModeInfo+0x1C;
constexpr uintptr_t kSubNext=rva::kModeInfo+0x20;
constexpr uintptr_t kSubProc=rva::kModeInfo+0x28;
constexpr uintptr_t kSubShift=rva::kModeInfo+0x2C;
}

namespace customize_task {
constexpr uintptr_t kController=0x78;
}

namespace customize_controller {
constexpr uintptr_t kSourcePvId=0x08;
constexpr uintptr_t kOverrideTable=0x10;
constexpr uintptr_t kHeader=0x968;
constexpr uintptr_t kSceneManager=0x998;
}

namespace scene_manager {
constexpr uintptr_t kTreeHead=0x08;
constexpr uintptr_t kCurrentNode=0x18;
constexpr uintptr_t kTargetNode=0x20;
constexpr uintptr_t kTransition=0x28;
constexpr int32_t kTransitionIdle=1;
constexpr uintptr_t kNodeKey=0x20;
constexpr uintptr_t kNodeScene=0x28;
}

namespace customize_scene {
constexpr uintptr_t kController=0x08;
constexpr uintptr_t kTopMenuState=0x178;
constexpr uintptr_t kModulePopup=0x60;
constexpr uintptr_t kModuleState=0x330;
constexpr uintptr_t kHairstyleState=0x3A0;
constexpr uintptr_t kItemState=0x300;
constexpr uintptr_t kSoundState=0x1A8;
constexpr uintptr_t kStateInternal=0x18;
}

namespace task_wait_screen {
constexpr uintptr_t kDispState=0x70;
}

template<class T> inline bool ReadAt(uintptr_t address,T& value) {
    SIZE_T count=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}

template<class T> inline bool WriteAt(uintptr_t address,T value) {
    MEMORY_BASIC_INFORMATION info{};SIZE_T count=0;
    return address && VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))
        && (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
        && WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}

inline bool IsExecutable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))
        && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}

inline bool VerifyGameBuild(uintptr_t base) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if(!ReadAt(base,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE) return false;
    if(!ReadAt(base+dos.e_lfanew,nt) || nt.Signature!=IMAGE_NT_SIGNATURE) return false;
    return nt.FileHeader.TimeDateStamp==build::kTimestamp && nt.OptionalHeader.SizeOfImage==build::kImageSize;
}
}
