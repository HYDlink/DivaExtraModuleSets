#pragma once
#include "ui.h"
#include "ui_color_output.h"
#include "ui_color_profile.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <string>

namespace diva {
struct Host {
    View view;
    void* context=nullptr;
    HWND window=nullptr;
    WNDPROC previous=nullptr;
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* deviceContext=nullptr;

    UiColorRenderer uiColorRenderer;
    std::string lastUiColorDiagnostic;
    std::future<UiColorProfile> uiColorProfilePending;
    UiColorProfile uiColorProfile;
    fs::path uiColorProfileGame;
    uint64_t nextUiColorProfileRead=0;

    std::recursive_mutex mutex;
    std::atomic<bool> captures{false};
    bool initialized=false;
    bool preview=false;
    uint64_t releaseUntil=0;

    std::array<bool,256> swallowed{};
    std::array<bool,3> swallowedMouse{};
    std::array<bool,256> releasedKeys{};
    std::array<bool,3> releasedMouse{};

    std::function<void(const std::string&)> diagnostic;

    std::string DescribeInputState() const;
    void ResetInputState(const char* reason);
    void UpdateInputCapture(uint64_t now,bool (*keyDown)(int)=nullptr);
    UiWhiteDecision SyncUiWhite(bool knownLuma);

    void Init(const fs::path& home,const fs::path& game);
    bool Attach(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context);
    void Frame(IDXGISwapChain* chain);
    LRESULT Message(HWND hwnd,UINT message,WPARAM w,LPARAM l);
    void PumpInputMessages();
    LRESULT HandleMessage(HWND hwnd,UINT message,WPARAM w,LPARAM l,bool& useDefault);
    void Shutdown();
};

Host& App();
LRESULT CALLBACK ExtraSetsWndProc(HWND hwnd,UINT message,WPARAM w,LPARAM l);
}
