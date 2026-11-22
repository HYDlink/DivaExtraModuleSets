#include "host.h"
#include "i18n.h"
#include "native_hooks.h"
#include "utf8_util.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <fstream>
#include <mutex>

namespace {
void Log(const std::string& text) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char prefix[48]{};
    std::snprintf(prefix,sizeof(prefix),"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                  time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    std::ofstream out(diva::App().view.home/"extra-slots-diagnostics.log",std::ios::app);
    out<<prefix<<text<<"\n";
    out.flush();
    if(!out.good()) OutputDebugStringW(diva::Wide("DivaExtraModuleSets 诊断写入失败："+text+"\n").c_str());
}

void SyncGameLanguageOnce() {
    static bool synced=false;
    if(synced) return;
    auto& host=diva::App();
    const std::string gameLang=diva::Bridge().CurrentLanguage();
    if(gameLang.empty()) return;
    synced=true;
    if(diva::ApplyGameLanguage(host.view.home,gameLang)) {
        for(const auto& line:diva::LocaleDiagnostics()) Log(line);
        host.view.fontDirty=true;
    }
}
}

extern "C" {
__declspec(dllexport) void Init() {
    HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Init),&self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self,path,32768);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr,executable,32768);

    const auto modHome=diva::fs::path(path).parent_path();
    const auto gameDir=diva::fs::path(executable).parent_path();

    diva::InitLocale(modHome);
    auto& host=diva::App();
    host.Init(modHome,gameDir);
    host.diagnostic=Log;
    for(const auto& line:diva::LocaleDiagnostics()) Log(line);

    host.view.onSettingsChanged=[] {
        diva::Bridge().ApplySettings(diva::App().view.settings);
    };
    host.view.onSaveSettings=[] {
        return diva::SaveSettings(diva::App().view.home/"settings.ini",diva::App().view.settings);
    };
    host.view.onManualSave=[] {
        return diva::Bridge().TriggerManualSave();
    };
    host.view.onSelectSlot=[](diva::ActiveSlot slot) {
        diva::Bridge().RequestSelectSlot(slot);
    };
    host.view.hasStoredShared=[](uint32_t idx) {
        return diva::Bridge().HasStoredShared(idx);
    };
    host.view.hasStoredSong=[](int32_t pvId,uint32_t idx) {
        return diva::Bridge().HasStoredSong(pvId,idx);
    };
    host.view.onFirstOpen=[] {
        SyncGameLanguageOnce();
    };
    host.view.onOpen=[] {
        diva::App().view.statusBanner.clear();
        diva::App().view.statusIsError=false;
    };
}

__declspec(dllexport) void PostInit() {
    auto& bridge=diva::Bridge();
    bridge.logCallback=Log;
    bridge.Initialize();
    bridge.ApplySettings(diva::App().view.settings);
}

__declspec(dllexport) void D3DInit(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context) {
    diva::App().Attach(chain,device,context);
}

__declspec(dllexport) void OnFrame(IDXGISwapChain* chain) {
    auto& host=diva::App();
    const auto snap=host.view.CurrentSnapshot();
    if(snap.inSongSelect || snap.inCustomization) {
        SyncGameLanguageOnce();
    }
    host.PumpInputMessages();
    host.Frame(chain);
}

__declspec(dllexport) void OnResize(IDXGISwapChain* chain) {
    (void)chain;
}
}
