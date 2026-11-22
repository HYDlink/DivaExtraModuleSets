#include "host.h"
#include "ui_theme.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

namespace diva {
namespace {
struct ContextScope {
    ImGuiContext* previous;
    explicit ContextScope(void* ctx):previous(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
    }
    ~ContextScope() {
        ImGui::SetCurrentContext(previous);
    }
};

bool PhysicalKeyDown(int key) {
    return (GetAsyncKeyState(key)&0x8000)!=0;
}

bool HotkeyMatches(const ParsedHotkey& hk,WPARAM w) {
    if(!hk.valid || w!=static_cast<WPARAM>(hk.vk)) return false;
    const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
    const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;
    return ctrl==hk.ctrl && shift==hk.shift && alt==hk.alt;
}
}

Host& App() {
    static Host* h=new Host;
    return *h;
}

std::string Host::DescribeInputState() const {
    return "面板="+std::to_string(view.open)
        +"，隔离="+std::to_string(captures.load())
        +"，待释放键="+std::to_string(std::count(swallowed.begin(),swallowed.end(),true))
        +"，待释放鼠标="+std::to_string(std::count(swallowedMouse.begin(),swallowedMouse.end(),true))
        +"，释放保护="+std::to_string(GetTickCount64()<releaseUntil);
}

void Host::ResetInputState(const char* reason) {
    const bool hadState=view.open || captures || releaseUntil
        || std::any_of(swallowed.begin(),swallowed.end(),[](bool v){ return v; })
        || std::any_of(swallowedMouse.begin(),swallowedMouse.end(),[](bool v){ return v; });
    view.open=false;
    releaseUntil=0;
    swallowed.fill(false);
    swallowedMouse.fill(false);
    releasedKeys.fill(false);
    releasedMouse.fill(false);
    captures=false;
    Bridge().captures.store(false,std::memory_order_relaxed);
    if(context) {
        ContextScope scope(context);
        ImGui::GetIO().ClearInputKeys();
    }
    if(hadState && diagnostic) diagnostic(std::string("输入状态清理：")+reason+"；"+DescribeInputState());
}

void Host::UpdateInputCapture(uint64_t now,bool (*keyDown)(int)) {
    if(!view.open && now>=releaseUntil) {
        if(!keyDown) keyDown=PhysicalKeyDown;
        for(size_t i=0;i<swallowed.size();++i) {
            if(swallowed[i] && !keyDown(static_cast<int>(i))) {
                swallowed[i]=false;
                releasedKeys[i]=true;
            }
        }
        const int mouseKeys[]{VK_LBUTTON,VK_RBUTTON,VK_MBUTTON};
        for(size_t i=0;i<swallowedMouse.size();++i) {
            if(swallowedMouse[i] && !keyDown(mouseKeys[i])) {
                swallowedMouse[i]=false;
                releasedMouse[i]=true;
            }
        }
    }
    const bool capture=view.open || now<releaseUntil
        || std::any_of(swallowed.begin(),swallowed.end(),[](bool v){ return v; })
        || std::any_of(swallowedMouse.begin(),swallowedMouse.end(),[](bool v){ return v; });
    captures.store(capture,std::memory_order_relaxed);
    Bridge().captures.store(capture,std::memory_order_relaxed);
}

UiWhiteDecision Host::SyncUiWhite(bool knownLuma) {
    if(view.uiWhiteNits>0 || !knownLuma) return ResolveUiWhite(view.uiWhiteNits,{});
    if(uiColorProfileGame!=view.game) { uiColorProfile={}; nextUiColorProfileRead=0; }
    if(uiColorProfilePending.valid() && uiColorProfilePending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            auto profile=uiColorProfilePending.get();
            if(uiColorProfileGame==view.game) uiColorProfile=std::move(profile);
        } catch(...) { uiColorProfile={}; }
    }
    const auto now=GetTickCount64();
    if(!uiColorProfilePending.valid() && now>=nextUiColorProfileRead) {
        uiColorProfileGame=view.game;
        nextUiColorProfileRead=now+1000;
        const auto file=view.game/"ReShade.ini";
        const auto gameWindow=window;
        try {
            uiColorProfilePending=std::async(std::launch::async,[file,gameWindow]{ return ReadUiColorProfile(file,gameWindow); });
        } catch(...) {}
    }
    return ResolveUiWhite(0,uiColorProfile);
}

void Host::Init(const fs::path& home,const fs::path& game) {
    std::lock_guard lock(mutex);
    if(context) return;
    view.home=home;
    view.game=game;
    view.ReloadSettings();
    auto* old=ImGui::GetCurrentContext();
    context=ImGui::CreateContext();
    SetupStyle(view.fontSize);
    view.fontDirty=false;
    ImGui::SetCurrentContext(old);
}

bool Host::Attach(IDXGISwapChain* chain,ID3D11Device* newDevice,ID3D11DeviceContext* newContext) {
    std::lock_guard lock(mutex);
    if(!context || !chain) return false;
    ContextScope scope(context);
    DXGI_SWAP_CHAIN_DESC desc{};
    if(FAILED(chain->GetDesc(&desc))) return false;
    if(initialized) {
        if(device==newDevice && window==desc.OutputWindow) return true;
        ResetInputState("图形设备或窗口重绑");
        uiColorRenderer.Reset();
        lastUiColorDiagnostic.clear();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        initialized=false;
        if(previous && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC))==ExtraSetsWndProc) {
            SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
            previous=nullptr;
        }
    }
    if(window!=desc.OutputWindow) previous=nullptr;
    window=desc.OutputWindow;
    device=newDevice;
    deviceContext=newContext;
    if(!ImGui_ImplWin32_Init(window)) return false;
    if(!ImGui_ImplDX11_Init(device,deviceContext)) { ImGui_ImplWin32_Shutdown(); return false; }
    initialized=true;
    return true;
}

void Host::PumpInputMessages() {
    if(!view.open || !window) return;
    MSG message{};
    for(int count=0;count<64;++count) {
        if(!PeekMessageW(&message,window,WM_KEYFIRST,WM_IME_COMPOSITION,PM_REMOVE)
            && !PeekMessageW(&message,window,WM_IME_SETCONTEXT,WM_IME_KEYUP,PM_REMOVE)) return;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LRESULT CALLBACK ExtraSetsWndProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    return App().Message(hwnd,message,w,l);
}

LRESULT Host::Message(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    bool useDefault=false;
    LRESULT result=0;
    {
        std::lock_guard lock(mutex);
        result=HandleMessage(hwnd,message,w,l,useDefault);
    }
    if(!useDefault) return result;
    return DefWindowProcW(hwnd,message,w,l);
}

LRESULT Host::HandleMessage(HWND hwnd,UINT message,WPARAM w,LPARAM l,bool& useDefault) {
    const bool key=(message==WM_KEYDOWN || message==WM_SYSKEYDOWN || message==WM_KEYUP || message==WM_SYSKEYUP);
    const bool down=(message==WM_KEYDOWN || message==WM_SYSKEYDOWN);
    if(!context || !initialized) {
        return previous?CallWindowProcW(previous,hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);
    }
    ContextScope scope(context);

    if(key && down) releasedKeys[w&255]=false;
    if(message==WM_LBUTTONDOWN) releasedMouse[0]=false;
    if(message==WM_RBUTTONDOWN) releasedMouse[1]=false;
    if(message==WM_MBUTTONDOWN) releasedMouse[2]=false;

    if(key && down && !(l&(1LL<<30)) && HotkeyMatches(view.hotkey,w)) {
        view.open=!view.open;
        if(view.open) {
            if(view.onFirstOpen) { auto fn=std::move(view.onFirstOpen); view.onFirstOpen=nullptr; fn(); }
            if(view.onOpen) view.onOpen();
        } else {
            releaseUntil=GetTickCount64()+120;
        }
        ImGui::GetIO().ClearInputKeys();
        swallowed[w&255]=true;
        UpdateInputCapture(GetTickCount64());
        return 0;
    }

    if(message==WM_KILLFOCUS) ResetInputState("窗口失焦");
    if(message==WM_ACTIVATEAPP && !w) ResetInputState("应用失活");
    if(message==WM_ACTIVATE && LOWORD(w)==WA_INACTIVE) ResetInputState("窗口失活");

    if(view.open) {
        if(message==WM_LBUTTONDOWN) swallowedMouse[0]=true;
        if(message==WM_RBUTTONDOWN) swallowedMouse[1]=true;
        if(message==WM_MBUTTONDOWN) swallowedMouse[2]=true;
        if(message==WM_LBUTTONUP) swallowedMouse[0]=releasedMouse[0]=false;
        if(message==WM_RBUTTONUP) swallowedMouse[1]=releasedMouse[1]=false;
        if(message==WM_MBUTTONUP) swallowedMouse[2]=releasedMouse[2]=false;
        if(key && !down) releasedKeys[w&255]=false;

        if(key && down && w==VK_ESCAPE && !(l&(1LL<<30)) && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)) {
            view.open=false;
            releaseUntil=GetTickCount64()+120;
            swallowed[w&255]=true;
            ImGui::GetIO().ClearInputKeys();
            UpdateInputCapture(GetTickCount64());
            return 0;
        }
        if(key && down && w!=VK_PROCESSKEY) swallowed[w&255]=true;

        ImGui_ImplWin32_WndProcHandler(hwnd,message,w,l);
        if((message>=WM_IME_STARTCOMPOSITION && message<=WM_IME_COMPOSITION) || (message>=WM_IME_SETCONTEXT && message<=WM_IME_KEYUP)) {
            useDefault=true;
            return 0;
        }
        if(key || message==WM_CHAR || message==WM_SYSCHAR || message==WM_INPUT || (message>=WM_MOUSEFIRST && message<=WM_MOUSELAST)) {
            if(key && !down) swallowed[w&255]=false;
            useDefault=(message==WM_INPUT);
            return 0;
        }
    } else if(message==WM_LBUTTONUP && (swallowedMouse[0] || releasedMouse[0])) { swallowedMouse[0]=releasedMouse[0]=false; return 0; }
    else if(message==WM_RBUTTONUP && (swallowedMouse[1] || releasedMouse[1])) { swallowedMouse[1]=releasedMouse[1]=false; return 0; }
    else if(message==WM_MBUTTONUP && (swallowedMouse[2] || releasedMouse[2])) { swallowedMouse[2]=releasedMouse[2]=false; return 0; }
    else if(key && !down && (swallowed[w&255] || releasedKeys[w&255])) { swallowed[w&255]=releasedKeys[w&255]=false; return 0; }
    else if(message==WM_CHAR && GetTickCount64()<releaseUntil) return 0;

    return previous?CallWindowProcW(previous,hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);
}

void Host::Frame(IDXGISwapChain* chain) {
    std::lock_guard lock(mutex);
    if(!initialized) return;

    if(!preview && !previous) {
        auto current=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC));
        if(current==ExtraSetsWndProc) { ResetInputState("窗口输入链状态异常"); return; }
        SetLastError(0);
        auto old=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(ExtraSetsWndProc));
        if(!old) { ResetInputState("窗口输入隔离安装失败"); return; }
        previous=reinterpret_cast<WNDPROC>(old);
    }

    ContextScope scope(context);
    const bool wasOpen=view.open;
    UpdateInputCapture(GetTickCount64());

    const RuntimeSnapshot snap=view.CurrentSnapshot();
    const bool drawHud=view.ShouldDrawHud(snap);
    if(!view.open && !drawHud && !preview) return;

    if(view.fontDirty) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        ImGui::GetIO().Fonts->Clear();
        SetupStyle(view.fontSize);
        view.fontDirty=false;
        view.styleDirty=false;
    }
    if(view.styleDirty) {
        ApplyUiTheme(ImGui::GetStyle());
        view.styleDirty=false;
    }

    ID3D11Texture2D* buffer=nullptr;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)))) return;
    D3D11_TEXTURE2D_DESC bufferDesc{};
    buffer->GetDesc(&bufferDesc);
    buffer->Release();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    auto& io=ImGui::GetIO();
    if(io.DisplaySize.x<=0 || io.DisplaySize.y<=0 || !bufferDesc.Width || !bufferDesc.Height) return;
    io.DisplayFramebufferScale={bufferDesc.Width/io.DisplaySize.x,bufferDesc.Height/io.DisplaySize.y};
    ImGui::NewFrame();
    view.Draw();
    ImGui::Render();

    if(wasOpen && !view.open) {
        releaseUntil=GetTickCount64()+120;
        ImGui::GetIO().ClearInputKeys();
        UpdateInputCapture(GetTickCount64());
    }

    buffer=nullptr;
    ID3D11RenderTargetView* target=nullptr;
    if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)))) {
        device->CreateRenderTargetView(buffer,nullptr,&target);
    }
    if(target) {
        const bool knownLuma=HasKnownDivaLuma();
        const auto output=ResolveUiColorOutput(view.uiOutputMode,bufferDesc.Format,knownLuma);
        const auto white=output==UiColorOutput::Legacy?UiWhiteDecision{}:SyncUiWhite(knownLuma);
        HRESULT colorResult=S_OK;
        if(output==UiColorOutput::Legacy) {
            uiColorRenderer.Reset();
            ID3D11RenderTargetView* previousTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
            ID3D11DepthStencilView* depth=nullptr;
            deviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,previousTargets,&depth);
            deviceContext->OMSetRenderTargets(1,&target,nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            deviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,previousTargets,depth);
            for(auto* rt:previousTargets) if(rt) rt->Release();
            if(depth) depth->Release();
        } else {
            const ImVec4* bounds=(view.hdrBackgroundBounds.z>view.hdrBackgroundBounds.x)?&view.hdrBackgroundBounds:nullptr;
            colorResult=uiColorRenderer.Render(device,deviceContext,buffer,target,ImGui::GetDrawData(),output,white.nits,bounds);
        }
        const auto colorDiag=std::string("界面色彩：")+UiColorOutputName(output)+"；配置="+view.uiOutputMode
            +"；格式="+std::to_string(bufferDesc.Format)+"；Luma="+(knownLuma?"是":"否")+"；结果="+std::to_string(colorResult);
        if(colorDiag!=lastUiColorDiagnostic) {
            lastUiColorDiagnostic=colorDiag;
            if(diagnostic) diagnostic(colorDiag);
        }
        target->Release();
    }
    if(buffer) buffer->Release();
}

void Host::Shutdown() {
    std::lock_guard lock(mutex);
    if(!context) return;
    ResetInputState("宿主关闭");
    uiColorRenderer.Reset();
    lastUiColorDiagnostic.clear();
    if(uiColorProfilePending.valid()) uiColorProfilePending.wait();
    ContextScope scope(context);
    if(initialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        initialized=false;
    }
    if(previous && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC))==ExtraSetsWndProc) {
        SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
    }
}
}
