#include "native_hooks.h"
#include "native_layout.h"
#include "utf8_util.h"
#include <detours.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>

namespace diva {
using namespace native;

namespace {
using InputFn=void*(__fastcall*)(int);
using GetSaveDataFn=uintptr_t(__fastcall*)();
using GetSlotConfigFn=void*(__fastcall*)(uintptr_t,int32_t,uint32_t);
using GetSlotConfigConstFn=void*(__fastcall*)(uintptr_t,int32_t,int32_t);
using GetSoundConfigFn=void*(__fastcall*)(uintptr_t,int32_t,char);
using LoadSaveDataFn=void(__fastcall*)(uintptr_t);
using SaveSaveDataFn=void(__fastcall*)(uintptr_t);
using CycleCustomizeScopeFn=uint8_t(__fastcall*)();
using ApplySongOutfitFn=void(__fastcall*)(uintptr_t,int32_t,uintptr_t);
using PvSelSwitchCtrlFn=uint8_t(__fastcall*)(uintptr_t);
using PvSelSetScopeUiFn=void(__fastcall*)(uintptr_t,int32_t);
using PvSelApplyOutfitFn=void(__fastcall*)(uintptr_t,int32_t);
using PvSelGetPanelFn=uintptr_t(__fastcall*)(uintptr_t);
using PvSelSetPanelPvFn=void(__fastcall*)(uintptr_t,int32_t);
using IsPlaylistModeFn=uint8_t(__fastcall*)();
using GetLanguageDirFn=const char*(__fastcall*)();
using GetDefaultModuleIdFn=int32_t(__fastcall*)(int32_t);
using GetPvDbFn=uintptr_t(__fastcall*)();
using GetPvEntryFn=uintptr_t(__fastcall*)(uintptr_t,int32_t);
using GetPerformerCountFn=uint64_t(__fastcall*)(uintptr_t);
using GetPerformerCharaFn=int32_t(__fastcall*)(uintptr_t,int32_t);

extern "C" {
void* __fastcall ThunkGetSlotConfig(uintptr_t save,int32_t pvId,uint32_t slot);
void* __fastcall ThunkGetSlotConfigConst(uintptr_t save,int32_t pvId,int32_t slot);
void* __fastcall ThunkGetSoundConfig(uintptr_t save,int32_t pvId,char useGlobalDefault);
uint8_t __fastcall ThunkCycleCustomizeScope();
void __fastcall ThunkApplySongOutfit(uintptr_t work,int32_t pvId,uintptr_t overrideTable);
void __fastcall ThunkPvSelSetScopeUi(uintptr_t panel,int32_t scope);

void* __fastcall HookGetSlotConfigImpl(uintptr_t save,int32_t pvId,uint32_t slot);
void* __fastcall HookGetSlotConfigConstImpl(uintptr_t save,int32_t pvId,int32_t slot);
void* __fastcall HookGetSoundConfigImpl(uintptr_t save,int32_t pvId,char useGlobalDefault);
uint8_t __fastcall HookCycleCustomizeScopeImpl();
void __fastcall HookApplySongOutfitImpl(uintptr_t work,int32_t pvId,uintptr_t overrideTable);
void __fastcall HookPvSelSetScopeUiImpl(uintptr_t panel,int32_t scope);
}

InputFn originalInput=nullptr;
GetSlotConfigFn originalGetSlotConfig=nullptr;
GetSlotConfigConstFn originalGetSlotConfigConst=nullptr;
GetSoundConfigFn originalGetSoundConfig=nullptr;
LoadSaveDataFn originalLoadSaveData=nullptr;
SaveSaveDataFn originalSaveSaveData=nullptr;
CycleCustomizeScopeFn originalCycleScope=nullptr;
ApplySongOutfitFn originalApplySongOutfit=nullptr;
PvSelSwitchCtrlFn originalPvSelCtrl=nullptr;
PvSelSetScopeUiFn originalPvSelSetScopeUi=nullptr;

struct BridgeState {
    uintptr_t base=0;
    bool isGameHost=false;
    bool supported=false;
    bool inputReady=false;
    std::string diagnostic;

    std::mutex configMutex;
    ModSettings settings{};
    ActiveSlot activeSlot{SlotKind::Shared,0};
    int32_t expectedNativeScope=0;
    uint64_t slotGeneration=1;
    uint64_t lastUiSlotGeneration=0;
    std::optional<ActiveSlot> pendingJumpSlot;
    bool pendingSongSelectRefresh=false;

    SlotStore store;
    bool saveLoaded=false;
    bool loadedFromDisk=false;
    fs::path saveFilePath;
    bool hasPendingSaveResult=false;
    bool lastSaveOk=true;

    std::mutex workerMutex;
    std::condition_variable workerCv;
    bool workerStop=false;
    uint64_t dirtyEpoch=0;
    std::thread saveWorker;

    std::array<ModuleEquip,CustomizeSet::kPerformerSlots> sharedFallbackModules{};
    std::array<ModuleEquip,CustomizeSet::kPerformerSlots> songFallbackModules{};
    std::map<int32_t,SlotStore::DefaultModules> songDefaultsCache;
    bool needSongDefaultsWarmup=false;
    std::array<uint8_t,CustomizeSet::kSoundBytes> fallbackSound{};
};

BridgeState& State() {
    static BridgeState* s=new BridgeState;
    return *s;
}

void EmitLog(const std::string& message) {
    if(Bridge().logCallback) Bridge().logCallback(message);
}

bool IsDemoModeActive() {
    uint8_t flag=0;
    return State().base && ReadAt(State().base+rva::kSpecialDemoModeFlag,flag) && flag!=0;
}

bool CallIsPlaylistMode() {
    const auto base=State().base;
    if(!base || !IsExecutable(base+rva::kIsPlaylistMode)) return false;
    __try {
        return reinterpret_cast<IsPlaylistModeFn>(base+rva::kIsPlaylistMode)()!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t CallGetSaveData() {
    const auto base=State().base;
    if(!base || !IsExecutable(base+rva::kGetSaveData)) return 0;
    __try {
        return reinterpret_cast<GetSaveDataFn>(base+rva::kGetSaveData)();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::string ReadGameSaveDirectory() {
    const auto base=State().base;
    if(!base) return {};
    const uintptr_t strAddr=base+rva::kSaveDirString;
    uint64_t len=0,cap=0;
    if(!ReadAt(strAddr+16,len) || !ReadAt(strAddr+24,cap) || len==0 || len>=512 || cap<len) return {};
    uintptr_t bufAddr=strAddr;
    if(cap>=16) {
        if(!ReadAt(strAddr,bufAddr) || !bufAddr) return {};
    }
    std::string dir(static_cast<size_t>(len),'\0');
    SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(bufAddr),dir.data(),static_cast<SIZE_T>(len),&got) || got!=len) return {};
    while(!dir.empty() && dir.back()=='\0') dir.pop_back();
    return dir;
}

fs::path ResolveSaveFilePath() {
    if(!State().saveFilePath.empty()) return State().saveFilePath;
    std::string dir=ReadGameSaveDirectory();
    if(!dir.empty()) {
        fs::path p=fs::path(Wide(dir))/SlotStore::kDefaultFileName;
        State().saveFilePath=p;
        return p;
    }
    // 备选回退：仅当 %APPDATA%\SEGA\Project DIVA MEGA39's\Steam 下存在唯一账户目录时使用。
    wchar_t* appdata=nullptr;
    size_t appdataLen=0;
    if(_wdupenv_s(&appdata,&appdataLen,L"APPDATA")==0 && appdata) {
        const fs::path steamRoot=fs::path(appdata)/L"SEGA"/L"Project DIVA MEGA39's"/L"Steam";
        free(appdata);
        std::error_code ec;
        if(fs::exists(steamRoot,ec)) {
            std::vector<fs::path> dirs;
            for(const auto& entry:fs::directory_iterator(steamRoot,ec)) {
                if(entry.is_directory(ec)) dirs.push_back(entry.path());
            }
            if(dirs.size()==1) {
                State().saveFilePath=dirs.front()/SlotStore::kDefaultFileName;
                return State().saveFilePath;
            }
        }
    }
    return {};
}

void SeedDefaultSoundFromSave(uintptr_t save) {
    if(!save) return;
    std::array<uint8_t,CustomizeSet::kSoundBytes> sound{};
    SIZE_T got=0;
    if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(save+save_offsets::kDefaultSound),sound.data(),sound.size(),&got) && got==sound.size()) {
        State().store.SetDefaultSoundTemplate(sound.data());
        std::memcpy(State().fallbackSound.data(),sound.data(),sound.size());
    }
}

void EnsureLoadedLocked(uintptr_t save) {
    if(State().saveLoaded) return;
    const fs::path filePath=ResolveSaveFilePath();
    if(filePath.empty()) return;
    if(save) SeedDefaultSoundFromSave(save);
    const bool loaded=State().store.LoadFromFile(filePath);
    State().saveLoaded=true;
    State().loadedFromDisk=loaded;
    if(loaded) {
        State().needSongDefaultsWarmup=true;
        State().activeSlot=ClampActiveSlot(State().store.GetActiveSlot(),State().settings.sharedSlots,State().settings.songSlots);
        State().store.SetActiveSlot(State().activeSlot);
        if(State().settings.enabled && save) {
            State().expectedNativeScope=NativeScopeFor(State().activeSlot);
            WriteAt(save+save_offsets::kScope,State().expectedNativeScope);
            ++State().slotGeneration;
        }
        EmitLog("已从云存档文件读取扩展槽位："+Utf8(filePath.wstring()));
    } else {
        EmitLog("未找到既有扩展存档，将在首次修改或游戏存档时创建："+Utf8(filePath.wstring()));
    }
}

// 必须在未持有 configMutex 时调用，禁止持锁调用游戏原生 PV/模型数据库函数。
std::optional<SlotStore::DefaultModules> ResolveSongDefaultModulesNative(int32_t pvId) {
    const auto& st=State();
    if(!st.supported || !st.base || pvId<=0) return std::nullopt;
    const uintptr_t base=st.base;
    for(const uintptr_t rvaAddr:{rva::kGetPvDb,rva::kGetPvEntry,rva::kGetPerformerCount,
                                 rva::kGetPerformerChara,rva::kGetDefaultModuleId}) {
        if(!IsExecutable(base+rvaAddr)) return std::nullopt;
    }
    __try {
        const uintptr_t pvDb=reinterpret_cast<GetPvDbFn>(base+rva::kGetPvDb)();
        if(!pvDb) return std::nullopt;
        const uintptr_t entry=reinterpret_cast<GetPvEntryFn>(base+rva::kGetPvEntry)(pvDb,pvId);
        if(!entry) return std::nullopt;
        const uint64_t count=std::min<uint64_t>(
            reinterpret_cast<GetPerformerCountFn>(base+rva::kGetPerformerCount)(entry),
            CustomizeSet::kPerformerSlots);
        SlotStore::DefaultModules defaults{};
        for(size_t i=0;i<count;++i) {
            const int32_t chara=reinterpret_cast<GetPerformerCharaFn>(base+rva::kGetPerformerChara)(entry,static_cast<int32_t>(i));
            const int32_t modId=reinterpret_cast<GetDefaultModuleIdFn>(base+rva::kGetDefaultModuleId)(chara);
            defaults[i]=modId>=0?modId:0;
        }
        return defaults;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return std::nullopt;
    }
}

// 必须在未持有 configMutex 时调用。
SlotStore::DefaultModules EnsureSongDefaultsResolved(int32_t pvId) {
    SlotStore::DefaultModules fallback{};
    if(pvId<=0) return fallback;
    auto& st=State();
    {
        std::lock_guard lock(st.configMutex);
        const auto it=st.songDefaultsCache.find(pvId);
        if(it!=st.songDefaultsCache.end()) return it->second;
    }
    const auto resolved=ResolveSongDefaultModulesNative(pvId);
    if(!resolved.has_value()) return fallback;
    {
        std::lock_guard lock(st.configMutex);
        st.songDefaultsCache[pvId]=*resolved;
        st.store.SetSongDefaultModules(pvId,*resolved);
    }
    return *resolved;
}

// 必须在未持有 configMutex 时调用。
void WarmupStoredSongDefaults() {
    auto& st=State();
    std::vector<int32_t> pvIds;
    {
        std::lock_guard lock(st.configMutex);
        if(!st.needSongDefaultsWarmup) return;
        st.needSongDefaultsWarmup=false;
        pvIds=st.store.ListStoredSongPvIds();
    }
    for(const int32_t pvId:pvIds) {
        EnsureSongDefaultsResolved(pvId);
    }
}

void ScheduleBackgroundSave() {
    auto& st=State();
    {
        std::lock_guard lock(st.workerMutex);
        ++st.dirtyEpoch;
    }
    st.workerCv.notify_one();
}

void SyncSaveScopeAndNotify(uintptr_t save,ActiveSlot nextSlot) {
    auto& st=State();
    st.activeSlot=nextSlot;
    st.expectedNativeScope=NativeScopeFor(nextSlot);
    if(save) WriteAt(save+save_offsets::kScope,st.expectedNativeScope);
    ++st.slotGeneration;
    st.store.SetActiveSlot(nextSlot);
    ScheduleBackgroundSave();
}

void SaveWorkerProc() {
    auto& st=State();
    std::unique_lock lock(st.workerMutex);
    uint64_t savedEpoch=0;
    while(!st.workerStop) {
        st.workerCv.wait(lock,[&]{ return st.workerStop || st.dirtyEpoch!=savedEpoch; });
        if(st.workerStop) break;
        const uint64_t targetEpoch=st.dirtyEpoch;
        // 1.2s 防抖：连续按 F4 切槽时合并为一次原子写盘。
        st.workerCv.wait_for(lock,std::chrono::milliseconds(1200),[&]{ return st.workerStop; });
        if(st.workerStop) break;
        if(st.dirtyEpoch!=targetEpoch) continue;
        savedEpoch=targetEpoch;
        lock.unlock();
        fs::path path;
        bool shouldWrite=false;
        {
            std::lock_guard cfgLock(st.configMutex);
            path=ResolveSaveFilePath();
            shouldWrite=!path.empty() && st.saveLoaded && st.store.IsDirty();
        }
        if(shouldWrite) {
            const bool ok=st.store.SaveAtomic(path);
            std::lock_guard cfgLock(st.configMutex);
            st.hasPendingSaveResult=true;
            st.lastSaveOk=ok;
            if(!ok) EmitLog("后台原子写入扩展存档失败："+Utf8(path.wstring()));
        }
        lock.lock();
    }
}

void* __fastcall HookInput(int player) {
    if(Bridge().captures.load(std::memory_order_relaxed)) return originalInput(1);
    return originalInput(player);
}
} // 暂闭匿名命名空间以导出 extern "C" 实现函数给 MASM 跳板调用。

extern "C" void* __fastcall HookGetSlotConfigImpl(uintptr_t save,int32_t pvId,uint32_t slot) {
    if(!save || IsDemoModeActive()) return originalGetSlotConfig(save,pvId,slot);
    void* customResult=nullptr;
    auto& st=State();
    for(int pass=0;pass<2;++pass) {
        bool needWarmup=false;
        bool needSongDefaults=false;
        {
            std::lock_guard lock(st.configMutex);
            const ActiveSlot active=st.activeSlot;
            if(!(st.settings.enabled && slot<CustomizeSet::kPerformerSlots && active.IsExtended()
                && pvId!=700 && pvId!=701 && (active.kind==SlotKind::Shared || pvId>0))) {
                break;
            }
            EnsureLoadedLocked(save);
            needWarmup=st.needSongDefaultsWarmup;
            const SlotStore::DefaultModules* cachedDefaults=nullptr;
            if(active.kind==SlotKind::Song) {
                const auto it=st.songDefaultsCache.find(pvId);
                if(it!=st.songDefaultsCache.end()) {
                    cachedDefaults=&it->second;
                } else if(pass==0) {
                    needSongDefaults=true;
                }
            }
            if(!needWarmup && !needSongDefaults) {
                ModuleEquip* equip=(active.kind==SlotKind::Shared)
                    ? st.store.GetOrCreateSharedModule(static_cast<uint32_t>(active.index),slot)
                    : st.store.GetOrCreateSongModule(pvId,static_cast<uint32_t>(active.index),slot);
                if(equip && equip->moduleId<0) {
                    equip->moduleId=(cachedDefaults && (*cachedDefaults)[slot]>=0)?(*cachedDefaults)[slot]:0;
                }
                customResult=equip;
                ScheduleBackgroundSave();
                break;
            }
        }
        if(needWarmup) WarmupStoredSongDefaults();
        if(needSongDefaults) EnsureSongDefaultsResolved(pvId);
    }
    if(customResult) return customResult;
    return originalGetSlotConfig(save,pvId,slot);
}

extern "C" void* __fastcall HookGetSlotConfigConstImpl(uintptr_t save,int32_t pvId,int32_t slot) {
    if(!save || IsDemoModeActive()) return originalGetSlotConfigConst(save,pvId,slot);
    void* customResult=nullptr;
    auto& st=State();
    for(int pass=0;pass<2;++pass) {
        bool needWarmup=false;
        bool needSongDefaults=false;
        {
            std::lock_guard lock(st.configMutex);
            const ActiveSlot active=st.activeSlot;
            if(!(st.settings.enabled && slot>=0 && slot<static_cast<int32_t>(CustomizeSet::kPerformerSlots)
                && active.IsExtended() && pvId!=700 && pvId!=701 && (active.kind==SlotKind::Shared || pvId>0))) {
                break;
            }
            const uint32_t performer=static_cast<uint32_t>(slot);
            EnsureLoadedLocked(save);
            needWarmup=st.needSongDefaultsWarmup;
            const SlotStore::DefaultModules* cachedDefaults=nullptr;
            if(active.kind==SlotKind::Song) {
                const auto it=st.songDefaultsCache.find(pvId);
                if(it!=st.songDefaultsCache.end()) {
                    cachedDefaults=&it->second;
                } else if(pass==0) {
                    needSongDefaults=true;
                }
            }
            if(!needWarmup && !needSongDefaults) {
                if(active.kind==SlotKind::Shared) {
                    if(const ModuleEquip* existing=st.store.FindSharedModule(static_cast<uint32_t>(active.index),performer)) {
                        if(existing->moduleId>=0) {
                            customResult=const_cast<ModuleEquip*>(existing);
                        } else {
                            st.sharedFallbackModules[performer]=*existing;
                            st.sharedFallbackModules[performer].moduleId=0;
                            customResult=&st.sharedFallbackModules[performer];
                        }
                    } else {
                        st.sharedFallbackModules[performer]=ModuleEquip{};
                        customResult=&st.sharedFallbackModules[performer];
                    }
                } else {
                    const int32_t defMod=(cachedDefaults && (*cachedDefaults)[performer]>=0)?(*cachedDefaults)[performer]:0;
                    // 只读查询绝不插入空白歌曲记录，防止浏览曲库时膨胀 .dat。
                    if(const ModuleEquip* existing=st.store.FindSongModule(pvId,static_cast<uint32_t>(active.index),performer)) {
                        if(existing->moduleId>=0) {
                            customResult=const_cast<ModuleEquip*>(existing);
                        } else {
                            st.songFallbackModules[performer]=*existing;
                            st.songFallbackModules[performer].moduleId=defMod;
                            customResult=&st.songFallbackModules[performer];
                        }
                    } else {
                        st.songFallbackModules[performer]=ModuleEquip{defMod,{-1,-1,-1,-1,-1},0x1Fu};
                        customResult=&st.songFallbackModules[performer];
                    }
                }
                break;
            }
        }
        if(needWarmup) WarmupStoredSongDefaults();
        if(needSongDefaults) EnsureSongDefaultsResolved(pvId);
    }
    if(customResult) return customResult;
    return originalGetSlotConfigConst(save,pvId,slot);
}

extern "C" void* __fastcall HookGetSoundConfigImpl(uintptr_t save,int32_t pvId,char useGlobalDefault) {
    if(!save || useGlobalDefault!='\0' || pvId==700 || pvId==701 || IsDemoModeActive()) {
        return originalGetSoundConfig(save,pvId,useGlobalDefault);
    }
    void* customResult=nullptr;
    bool needWarmup=false;
    {
        auto& st=State();
        std::lock_guard lock(st.configMutex);
        const ActiveSlot active=st.activeSlot;
        if(st.settings.enabled && active.IsExtended() && (active.kind==SlotKind::Shared || pvId>0)) {
            EnsureLoadedLocked(save);
            needWarmup=st.needSongDefaultsWarmup;
            customResult=(active.kind==SlotKind::Shared)
                ? static_cast<void*>(st.store.GetOrCreateSharedSound(static_cast<uint32_t>(active.index)))
                : static_cast<void*>(st.store.GetOrCreateSongSound(pvId,static_cast<uint32_t>(active.index)));
            ScheduleBackgroundSave();
        }
    }
    if(needWarmup) WarmupStoredSongDefaults();
    if(customResult) return customResult;
    return originalGetSoundConfig(save,pvId,useGlobalDefault);
}

namespace {
void __fastcall HookLoadSaveData(uintptr_t save) {
    originalLoadSaveData(save);
    if(!save) return;
    bool needWarmup=false;
    {
        auto& st=State();
        std::lock_guard lock(st.configMutex);
        SeedDefaultSoundFromSave(save);
        st.saveLoaded=false;
        EnsureLoadedLocked(save);
        needWarmup=st.needSongDefaultsWarmup;
        if(!st.loadedFromDisk) {
            int32_t scope=0;
            if(ReadAt(save+save_offsets::kScope,scope)) {
                st.activeSlot=FromNativeScope(scope);
                st.expectedNativeScope=scope;
                st.store.SetActiveSlot(st.activeSlot);
            }
        } else if(st.settings.enabled) {
            st.expectedNativeScope=NativeScopeFor(st.activeSlot);
            WriteAt(save+save_offsets::kScope,st.expectedNativeScope);
        }
    }
    if(needWarmup) WarmupStoredSongDefaults();
}

void __fastcall HookSaveSaveData(uintptr_t save) {
    {
        auto& st=State();
        std::lock_guard lock(st.configMutex);
        if(save && st.settings.enabled) {
            // 保证写入原生存档的 scope 值始终合法（0..3）。
            WriteAt(save+save_offsets::kScope,NativeScopeFor(st.activeSlot));
        }
    }
    originalSaveSaveData(save);
    auto& st=State();
    std::lock_guard lock(st.configMutex);
    const fs::path path=ResolveSaveFilePath();
    if(!path.empty() && st.saveLoaded) {
        std::error_code ec;
        if(st.store.IsDirty() || fs::exists(path,ec)) {
            const bool ok=st.store.SaveAtomic(path);
            st.hasPendingSaveResult=true;
            st.lastSaveOk=ok;
            EmitLog(ok?"随游戏存档同步写入扩展存档。":"随游戏存档同步写入扩展存档失败。");
        }
    }
}
} // 结束匿名命名空间，导出后三枚 MASM 跳板目标实现。

extern "C" uint8_t __fastcall HookCycleCustomizeScopeImpl() {
    if(CallIsPlaylistMode()) return 0;
    const uintptr_t save=CallGetSaveData();
    bool delegateVanilla=false;
    bool needWarmup=false;
    uint8_t result=0;
    {
        auto& st=State();
        std::lock_guard lock(st.configMutex);
        if(!st.settings.enabled) {
            delegateVanilla=true;
        } else {
            EnsureLoadedLocked(save);
            needWarmup=st.needSongDefaultsWarmup;
            const int total=st.settings.sharedSlots+st.settings.songSlots;
            if(total>1) {
                const bool reverse=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
                const ActiveSlot prev=st.activeSlot;
                const ActiveSlot next=CycleSlot(prev,reverse?-1:1,st.settings.sharedSlots,st.settings.songSlots);
                if(next!=prev) {
                    SyncSaveScopeAndNotify(save,next);
                    st.pendingSongSelectRefresh=true;
                    result=1;
                }
            }
        }
    }
    if(needWarmup) WarmupStoredSongDefaults();
    if(!delegateVanilla) return result;

    const uint8_t changed=originalCycleScope();
    auto& st=State();
    std::lock_guard lock(st.configMutex);
    int32_t scope=0;
    if(save && ReadAt(save+save_offsets::kScope,scope)) {
        st.activeSlot=FromNativeScope(scope);
        st.expectedNativeScope=scope;
        ++st.slotGeneration;
    }
    return changed;
}

extern "C" void __fastcall HookApplySongOutfitImpl(uintptr_t work,int32_t pvId,uintptr_t overrideTable) {
    bool needWarmup=false;
    if(overrideTable==0) {
        const uintptr_t save=CallGetSaveData();
        auto& st=State();
        std::lock_guard lock(st.configMutex);
        if(st.settings.enabled && save) {
            EnsureLoadedLocked(save);
            needWarmup=st.needSongDefaultsWarmup;
            int32_t actualScope=0;
            if(ReadAt(save+save_offsets::kScope,actualScope) && actualScope!=st.expectedNativeScope) {
                const bool reverse=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
                const ActiveSlot next=CycleSlot(st.activeSlot,reverse?-1:1,st.settings.sharedSlots,st.settings.songSlots);
                SyncSaveScopeAndNotify(save,next);
            }
        }
    }
    if(needWarmup) WarmupStoredSongDefaults();
    originalApplySongOutfit(work,pvId,overrideTable);
}

extern "C" void __fastcall HookPvSelSetScopeUiImpl(uintptr_t panel,int32_t scope) {
    bool shouldInvalidate=false;
    {
        auto& st=State();
        std::lock_guard lock(st.configMutex);
        if(panel && st.settings.enabled && st.lastUiSlotGeneration!=st.slotGeneration) {
            // 先更新代数标志并解锁，再在锁外刷新面板与调用原生函数，避免 PvSelSetScopeUiSlow -> ApplySongOutfit 重入自死锁。
            st.lastUiSlotGeneration=st.slotGeneration;
            shouldInvalidate=true;
        }
    }
    if(shouldInvalidate) {
        WriteAt(panel+pv_selector::kPanelLastScope,int32_t{-1});
    }
    originalPvSelSetScopeUi(panel,scope);
}

namespace {
void SafeRefreshPvSelector(uintptr_t base,uintptr_t selector,int32_t pvId,int32_t nativeScope) {
    __try {
        reinterpret_cast<PvSelApplyOutfitFn>(base+rva::kPvSelApplyOutfit)(selector,pvId);
        const uintptr_t panel=reinterpret_cast<PvSelGetPanelFn>(base+rva::kPvSelGetPanel)(selector+pv_selector::kPanelWrapper);
        if(panel) {
            WriteAt(panel+pv_selector::kPanelLastScope,int32_t{-1});
            HookPvSelSetScopeUiImpl(panel,nativeScope);
            reinterpret_cast<PvSelSetPanelPvFn>(base+rva::kPvSelSetPanelPv)(panel,pvId);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

uint8_t __fastcall HookPvSelCtrl(uintptr_t selector) {
    const uint8_t ret=originalPvSelCtrl(selector);
    bool doRefresh=false;
    uintptr_t base=0;
    int32_t nativeScope=0;
    {
        auto& st=State();
        std::lock_guard lock(st.configMutex);
        if(st.pendingJumpSlot && st.settings.enabled) {
            const ActiveSlot jump=*st.pendingJumpSlot;
            st.pendingJumpSlot.reset();
            const uintptr_t save=CallGetSaveData();
            EnsureLoadedLocked(save);
            const ActiveSlot clamped=ClampActiveSlot(jump,st.settings.sharedSlots,st.settings.songSlots);
            SyncSaveScopeAndNotify(save,clamped);
            nativeScope=st.expectedNativeScope;
            base=st.base;
            doRefresh=true;
            st.pendingSongSelectRefresh=false;
        } else if(st.pendingSongSelectRefresh && st.settings.enabled) {
            uintptr_t controller=0;
            if(st.base) ReadAt(st.base+rva::kCustomizeTask+customize_task::kController,controller);
            if(controller==0) {
                st.pendingSongSelectRefresh=false;
                nativeScope=st.expectedNativeScope;
                base=st.base;
                doRefresh=true;
            }
        }
    }
    if(doRefresh && selector && base) {
        int32_t state=0,pvId=0;
        if(ReadAt(selector+pv_selector::kState,state) && state==pv_selector::kStateIdle
            && ReadAt(selector+pv_selector::kPvId,pvId) && pvId>0) {
            SafeRefreshPvSelector(base,selector,pvId,nativeScope);
        }
    }
    return ret;
}

bool ReadLanguageDirectory(uintptr_t image,char* output) {
    __try {
        const unsigned char expected[]={0xE8,0xF6,0xF4,0x08,0x00};
        if(std::memcmp(reinterpret_cast<const void*>(image+rva::kGetLanguageDirectoryCall),expected,sizeof(expected))) return false;
        const char* directory=reinterpret_cast<GetLanguageDirFn>(image+rva::kGetLanguageDirectory)();
        if(!directory) return false;
        for(size_t n=0;n<32;++n) { output[n]=directory[n]; if(!output[n]) return true; }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return false;
}
}

NativeBridge& Bridge() {
    static NativeBridge* b=new NativeBridge;
    return *b;
}

void NativeBridge::Log(const std::string& message) const {
    if(logCallback) logCallback(message);
}

void NativeBridge::Initialize() {
    auto& st=State();
    st.supported=false;
    st.inputReady=false;
    st.base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr,path,32768);
    if(fs::path(path).filename()!=L"DivaMegaMix.exe") {
        st.isGameHost=false;
        st.inputReady=true;
        st.diagnostic="当前为独立预览或验证宿主，已禁用游戏内存 Hook。";
        return;
    }
    st.isGameHost=true;
    if(!VerifyGameBuild(st.base)) {
        st.diagnostic="游戏构建与已核查 PC 1.03（TimeDateStamp=1666920875, SizeOfImage=602353664）不符，已安全停用全部原生 Hook。";
        Log(st.diagnostic);
        return;
    }

    // 校验 GetInputState 尾部签名（保留前序 Mod 的 Detours 跳板）。
    const unsigned char inputSig[]={0x4C,0x8B,0x05,0,0,0,0,0x4D,0x85,0xC0,0x74,0x35,0x4C,0x63,0xC9,0x48,0xB8,0,0,0,0,0,0,0,0,0x49,0x8B,0x48,0x08,0x49,0x2B,0x08,0x48,0xF7,0xE9};
    unsigned char inputTail[sizeof(inputSig)]{};
    SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(st.base+rva::kGetInputState),inputTail,sizeof(inputTail),&got)
        || got!=sizeof(inputTail) || std::memcmp(inputTail+25,inputSig+25,10)!=0) {
        st.diagnostic="输入函数签名校验失败，已停用原生 Hook。";
        Log(st.diagnostic);
        return;
    }

    // 校验 0x1D5F40 jmp 跳转桩指向 0xE5B6340。
    uint8_t jmpOpcode=0;
    int32_t jmpRel=0;
    if(!ReadAt(st.base+rva::kGetSoundConfigThunk,jmpOpcode) || jmpOpcode!=0xE9 || !ReadAt(st.base+rva::kGetSoundConfigThunk+1,jmpRel)
        || (rva::kGetSoundConfigThunk+5+static_cast<intptr_t>(jmpRel))!=static_cast<intptr_t>(rva::kGetSoundConfigTarget)) {
        st.diagnostic="音效配置获取器跳转桩校验失败，已停用原生 Hook。";
        Log(st.diagnostic);
        return;
    }

    for(auto rvaAddr:{rva::kGetSlotConfig,rva::kGetSlotConfigConst,rva::kGetSoundConfigTarget,
                      rva::kLoadSaveData,rva::kSaveSaveData,rva::kCycleCustomizeScope,
                      rva::kApplySongOutfit,rva::kPvSelectorSwitchCtrl,rva::kPvSelSetScopeUi}) {
        if(!IsExecutable(st.base+rvaAddr)) {
            st.diagnostic="目标 Hook 入口内存不可执行，已停用原生 Hook。";
            Log(st.diagnostic);
            return;
        }
    }

    originalInput=reinterpret_cast<InputFn>(st.base+rva::kGetInputState);
    originalGetSlotConfig=reinterpret_cast<GetSlotConfigFn>(st.base+rva::kGetSlotConfig);
    originalGetSlotConfigConst=reinterpret_cast<GetSlotConfigConstFn>(st.base+rva::kGetSlotConfigConst);
    originalGetSoundConfig=reinterpret_cast<GetSoundConfigFn>(st.base+rva::kGetSoundConfigTarget);
    originalLoadSaveData=reinterpret_cast<LoadSaveDataFn>(st.base+rva::kLoadSaveData);
    originalSaveSaveData=reinterpret_cast<SaveSaveDataFn>(st.base+rva::kSaveSaveData);
    originalCycleScope=reinterpret_cast<CycleCustomizeScopeFn>(st.base+rva::kCycleCustomizeScope);
    originalApplySongOutfit=reinterpret_cast<ApplySongOutfitFn>(st.base+rva::kApplySongOutfit);
    originalPvSelCtrl=reinterpret_cast<PvSelSwitchCtrlFn>(st.base+rva::kPvSelectorSwitchCtrl);
    originalPvSelSetScopeUi=reinterpret_cast<PvSelSetScopeUiFn>(st.base+rva::kPvSelSetScopeUi);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&originalInput),HookInput);
    DetourAttach(reinterpret_cast<PVOID*>(&originalGetSlotConfig),reinterpret_cast<PVOID>(ThunkGetSlotConfig));
    DetourAttach(reinterpret_cast<PVOID*>(&originalGetSlotConfigConst),reinterpret_cast<PVOID>(ThunkGetSlotConfigConst));
    DetourAttach(reinterpret_cast<PVOID*>(&originalGetSoundConfig),reinterpret_cast<PVOID>(ThunkGetSoundConfig));
    DetourAttach(reinterpret_cast<PVOID*>(&originalLoadSaveData),HookLoadSaveData);
    DetourAttach(reinterpret_cast<PVOID*>(&originalSaveSaveData),HookSaveSaveData);
    DetourAttach(reinterpret_cast<PVOID*>(&originalCycleScope),reinterpret_cast<PVOID>(ThunkCycleCustomizeScope));
    DetourAttach(reinterpret_cast<PVOID*>(&originalApplySongOutfit),reinterpret_cast<PVOID>(ThunkApplySongOutfit));
    DetourAttach(reinterpret_cast<PVOID*>(&originalPvSelCtrl),HookPvSelCtrl);
    DetourAttach(reinterpret_cast<PVOID*>(&originalPvSelSetScopeUi),reinterpret_cast<PVOID>(ThunkPvSelSetScopeUi));
    const LONG commit=DetourTransactionCommit();
    if(commit!=NO_ERROR) {
        originalInput=nullptr;
        st.diagnostic="Detours Hook 事务提交失败，错误码="+std::to_string(commit);
        Log(st.diagnostic);
        return;
    }

    {
        std::lock_guard lock(st.workerMutex);
        st.workerStop=false;
        st.saveWorker=std::thread(SaveWorkerProc);
    }

    st.inputReady=true;
    st.supported=true;
    st.diagnostic="已核对 PC 1.03 构建并挂载全部 10 个链式 Hook（含输入隔离与独立存档）。";
    Log(st.diagnostic);
}

void NativeBridge::Shutdown() {
    auto& st=State();
    {
        std::lock_guard lock(st.workerMutex);
        st.workerStop=true;
    }
    st.workerCv.notify_all();
    if(st.saveWorker.joinable()) st.saveWorker.join();

    std::lock_guard cfgLock(st.configMutex);
    const fs::path path=ResolveSaveFilePath();
    if(!path.empty() && st.saveLoaded && st.store.IsDirty()) {
        st.store.SaveAtomic(path);
    }
}

std::string NativeBridge::CurrentLanguage() const {
    const auto& st=State();
    char dir[32]{};
    if(!st.supported || !st.base || !ReadLanguageDirectory(st.base,dir)) return {};
    const std::string path(dir);
    const auto slash=path.find_last_of('/');
    if(slash==std::string::npos) return {};
    const auto lang=path.substr(slash+1);
    for(const auto* valid:{"jp","en","cn","tw","kr","fr","ge","it","sp"}) {
        if(lang==valid) return lang;
    }
    return {};
}

void NativeBridge::ApplySettings(const ModSettings& settings) {
    auto& st=State();
    std::lock_guard lock(st.configMutex);
    const bool wasEnabled=st.settings.enabled;
    st.settings=settings;
    st.activeSlot=ClampActiveSlot(st.activeSlot,settings.sharedSlots,settings.songSlots);
    st.store.SetActiveSlot(st.activeSlot);
    const uintptr_t save=st.supported?CallGetSaveData():0;
    if(save) EnsureLoadedLocked(save);
    if(settings.enabled) {
        st.expectedNativeScope=NativeScopeFor(st.activeSlot);
        if(save) WriteAt(save+save_offsets::kScope,st.expectedNativeScope);
        ++st.slotGeneration;
        if(wasEnabled!=settings.enabled) {
            st.pendingJumpSlot=st.activeSlot;
        }
    } else if(wasEnabled && save) {
        // 关闭拓展：立即回退到当前对应的原生有效槽位，不改动后台扩展数据。
        const int32_t vanillaScope=NativeScopeFor(st.activeSlot);
        st.expectedNativeScope=vanillaScope;
        st.activeSlot=FromNativeScope(vanillaScope);
        WriteAt(save+save_offsets::kScope,vanillaScope);
        ++st.slotGeneration;
    }
}

RuntimeSnapshot NativeBridge::ReadSnapshot() const {
    auto& st=State();
    RuntimeSnapshot snap;
    snap.isGameHost=st.isGameHost;
    snap.supported=st.supported;
    snap.inputReady=st.inputReady;
    {
        std::lock_guard lock(st.configMutex);
        snap.enabled=st.settings.enabled;
        snap.activeSlot=st.activeSlot;
        snap.visibleSharedSlots=st.settings.sharedSlots;
        snap.visibleSongSlots=st.settings.songSlots;
        const auto stats=st.store.GetStats(st.settings.sharedSlots,st.settings.songSlots);
        snap.storedSharedCount=stats.storedSharedCount;
        snap.hiddenSharedCount=stats.hiddenSharedCount;
        snap.storedSongCount=stats.storedSongCount;
        snap.hiddenSongCount=stats.hiddenSongCount;
        snap.saveDirty=st.store.IsDirty();
        snap.hasPendingSaveResult=st.hasPendingSaveResult;
        snap.lastSaveOk=st.lastSaveOk;
        snap.saveFilePath=Utf8(st.saveFilePath.wstring());
        snap.diagnostic=st.diagnostic;
    }
    if(st.supported && st.base) {
        uintptr_t waitVtbl=0;
        int32_t waitDispState=0;
        const bool waitScreenActive=
            ReadAt(st.base+rva::kTaskWaitScreen,waitVtbl)
            && waitVtbl==st.base+rva::kTaskWaitScreenVtable
            && ReadAt(st.base+rva::kTaskWaitScreen+task_wait_screen::kDispState,waitDispState)
            && waitDispState!=0;

        int32_t mCur=0,mProc=0,sCur=0,sNext=0,sProc=0;
        uint8_t mShift=0,sShift=0;
        ReadAt(st.base+mode_info::kCurrent,mCur);
        ReadAt(st.base+mode_info::kProc,mProc);
        ReadAt(st.base+mode_info::kShift,mShift);
        ReadAt(st.base+mode_info::kSubCurrent,sCur);
        ReadAt(st.base+mode_info::kSubNext,sNext);
        ReadAt(st.base+mode_info::kSubProc,sProc);
        ReadAt(st.base+mode_info::kSubShift,sShift);
        const bool outerIdle=(mProc==ModeProc::Ctrl && sProc==ModeProc::Ctrl && mShift==0 && sShift==0);

        uintptr_t controller=0;
        ReadAt(st.base+rva::kCustomizeTask+customize_task::kController,controller);

        uintptr_t pvSel=0;
        int32_t pvState=0,pvId=0;
        ReadAt(st.base+rva::kPvSelectorSwitch,pvSel);
        if(pvSel) {
            ReadAt(pvSel+pv_selector::kState,pvState);
            ReadAt(pvSel+pv_selector::kPvId,pvId);
        }
        snap.currentPvId=pvId;

        if(controller!=0) {
            uintptr_t overrideTable=0;
            ReadAt(controller+customize_controller::kOverrideTable,overrideTable);
            snap.isPlaylist=(overrideTable!=0 || CallIsPlaylistMode());

            uintptr_t header=0,manager=0,mgrVtbl=0,head=0,node=0;
            uint8_t headerVisible=0;
            int32_t sceneKey=-1;
            const bool headerReady=ReadAt(controller+customize_controller::kHeader,header)
                && header!=0 && ReadAt(header,headerVisible) && headerVisible!=0;
            const bool sceneValid=ReadAt(controller+customize_controller::kSceneManager,manager)
                && manager!=0 && ReadAt(manager,mgrVtbl) && mgrVtbl==st.base+rva::kSceneManagerVtable
                && ReadAt(manager+scene_manager::kTreeHead,head)
                && ReadAt(manager+scene_manager::kCurrentNode,node) && node!=0 && node!=head
                && ReadAt(node+scene_manager::kNodeKey,sceneKey)
                && (sceneKey==SceneKey::TopMenu || sceneKey==SceneKey::Module
                    || sceneKey==SceneKey::Hairstyle || sceneKey==SceneKey::Item
                    || sceneKey==SceneKey::Sound);
            snap.inCustomization=(!waitScreenActive && outerIdle && !snap.isPlaylist && headerReady && sceneValid);
            snap.inSongSelect=false;
        } else {
            snap.inCustomization=false;
            snap.isPlaylist=false;

            int32_t visiblePvid=-1;
            int32_t member=0;
            if(pvSel && ReadAt(st.base+rva::kVisiblePvidMember,member)
                && member>=pv_selector::kVisiblePvidBias && member<=pv_selector::kVisiblePvidMemberMax) {
                int32_t vp=-1;
                if(ReadAt(pvSel+static_cast<intptr_t>(member)-pv_selector::kVisiblePvidBias,vp)
                    && vp>0 && vp<10000000) {
                    visiblePvid=vp;
                }
            }
            const bool normalSongSelect=(sCur==ModeSub::PvSel && sNext==ModeSub::PlaylistSel);
            const bool playlistSongPicker=(sCur==ModeSub::PlaylistSel && sNext==ModeSub::Game && visiblePvid>0 && visiblePvid==pvId);
            snap.inSongSelect=(!waitScreenActive && outerIdle && mCur==Mode::Game && pvSel!=0
                && pvState==pv_selector::kStateIdle && pvId>0 && (normalSongSelect || playlistSongPicker));
        }

        snap.gameLanguage=CurrentLanguage();
        const uintptr_t save=CallGetSaveData();
        if(save) ReadAt(save+save_offsets::kScope,snap.nativeScope);
    }
    return snap;
}

bool NativeBridge::HasStoredShared(uint32_t slotIndex) const {
    return State().store.HasStoredSharedData(slotIndex);
}

bool NativeBridge::HasStoredSong(int32_t pvId,uint32_t slotIndex) const {
    return State().store.HasStoredSongData(pvId,slotIndex);
}

void NativeBridge::RequestSelectSlot(ActiveSlot slot) {
    auto& st=State();
    std::lock_guard lock(st.configMutex);
    const ActiveSlot clamped=ClampActiveSlot(slot,st.settings.sharedSlots,st.settings.songSlots);
    const uintptr_t save=st.supported?CallGetSaveData():0;
    if(save) EnsureLoadedLocked(save);
    SyncSaveScopeAndNotify(save,clamped);
    st.pendingJumpSlot=clamped;
}

bool NativeBridge::TriggerManualSave() {
    auto& st=State();
    std::lock_guard lock(st.configMutex);
    const uintptr_t save=st.supported?CallGetSaveData():0;
    if(save) EnsureLoadedLocked(save);
    const fs::path path=ResolveSaveFilePath();
    if(path.empty()) return false;
    const bool ok=st.store.SaveAtomic(path);
    st.hasPendingSaveResult=true;
    st.lastSaveOk=ok;
    EmitLog(ok?"手动写入扩展存档成功："+Utf8(path.wstring()):"手动写入扩展存档失败："+Utf8(path.wstring()));
    return ok;
}

namespace detail {
HookTestPointers GetThunkEntryPointsForTest() {
    return HookTestPointers{
        reinterpret_cast<void*>(ThunkGetSlotConfig),
        reinterpret_cast<void*>(ThunkGetSlotConfigConst),
        reinterpret_cast<void*>(ThunkGetSoundConfig),
        reinterpret_cast<void*>(ThunkCycleCustomizeScope),
        reinterpret_cast<void*>(ThunkApplySongOutfit),
        reinterpret_cast<void*>(ThunkPvSelSetScopeUi),
    };
}

void SetTestOriginalsForUnitTests(const HookTestPointers& originals) {
    originalGetSlotConfig=reinterpret_cast<GetSlotConfigFn>(originals.getSlotConfig);
    originalGetSlotConfigConst=reinterpret_cast<GetSlotConfigConstFn>(originals.getSlotConfigConst);
    originalGetSoundConfig=reinterpret_cast<GetSoundConfigFn>(originals.getSoundConfig);
    originalCycleScope=reinterpret_cast<CycleCustomizeScopeFn>(originals.cycleScope);
    originalApplySongOutfit=reinterpret_cast<ApplySongOutfitFn>(originals.applySongOutfit);
    originalPvSelSetScopeUi=reinterpret_cast<PvSelSetScopeUiFn>(originals.pvSelSetScopeUi);
}

void SetTestStateForUnitTests(const ModSettings& settings,ActiveSlot slot) {
    auto& st=State();
    std::lock_guard lock(st.configMutex);
    st.settings=settings;
    st.activeSlot=slot;
    st.expectedNativeScope=NativeScopeFor(slot);
    st.saveLoaded=true;
    ++st.slotGeneration;
}
}
}
