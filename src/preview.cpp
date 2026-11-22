#include "host.h"
#include "i18n.h"
#include "ui_theme.h"
#include "utf8_util.h"
#include <imgui.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
class OffscreenSwapChain final : public IDXGISwapChain {
public:
    OffscreenSwapChain(ID3D11Device* device,HWND window,UINT width,UINT height):device_(device),window_(window) { Resize(width,height); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** object) override {
        if(!object) return E_POINTER;
        if(riid==__uuidof(IUnknown) || riid==__uuidof(IDXGIObject) || riid==__uuidof(IDXGIDeviceSubObject) || riid==__uuidof(IDXGISwapChain)) {
            *object=this; AddRef(); return S_OK;
        }
        *object=nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG left=--refs_; if(!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID,void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid,void** object) override { return device_->QueryInterface(riid,object); }
    HRESULT STDMETHODCALLTYPE Present(UINT,UINT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID riid,void** surface) override {
        return index?DXGI_ERROR_INVALID_CALL:buffer_->QueryInterface(riid,surface);
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL,IDXGIOutput*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fs,IDXGIOutput** target) override {
        if(fs) *fs=FALSE;
        if(target) *target=nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override {
        if(!desc) return E_POINTER;
        D3D11_TEXTURE2D_DESC td{};
        buffer_->GetDesc(&td);
        *desc={};
        desc->BufferDesc.Width=td.Width;
        desc->BufferDesc.Height=td.Height;
        desc->BufferDesc.Format=td.Format;
        desc->SampleDesc.Count=1;
        desc->BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc->BufferCount=1;
        desc->OutputWindow=window_;
        desc->Windowed=TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT,UINT width,UINT height,DXGI_FORMAT,UINT) override {
        D3D11_TEXTURE2D_DESC td{};
        buffer_->GetDesc(&td);
        return Resize(width?width:td.Width,height?height:td.Height);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput**) override { return DXGI_ERROR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS*) override { return DXGI_ERROR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* count) override { if(count) *count=0; return S_OK; }
private:
    HRESULT Resize(UINT width,UINT height) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        buffer_.Reset();
        return device_->CreateTexture2D(&desc,nullptr,&buffer_);
    }
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11Texture2D> buffer_;
    HWND window_;
    ULONG refs_=1;
};

bool Screenshot(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context,const diva::fs::path& path) {
    ComPtr<ID3D11Texture2D> back,staging;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return false;
    context->CopyResource(staging.Get(),back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr)) hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr)) hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr)) hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr)) hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr)) hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr)) hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr)) hr=frame->SetSize(desc.Width,desc.Height);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppRGBA;
    if(SUCCEEDED(hr)) hr=frame->SetPixelFormat(&format);
    std::vector<BYTE> pixels;
    BYTE* data=static_cast<BYTE*>(mapped.pData);
    if(format==GUID_WICPixelFormat32bppBGRA) {
        pixels.resize(static_cast<size_t>(mapped.RowPitch)*desc.Height);
        std::memcpy(pixels.data(),data,pixels.size());
        for(UINT y=0;y<desc.Height;++y) {
            for(UINT x=0;x<desc.Width;++x) {
                std::swap(pixels[static_cast<size_t>(y)*mapped.RowPitch+x*4],pixels[static_cast<size_t>(y)*mapped.RowPitch+x*4+2]);
            }
        }
        data=pixels.data();
    }
    if(SUCCEEDED(hr)) hr=frame->WritePixels(desc.Height,mapped.RowPitch,mapped.RowPitch*desc.Height,data);
    if(SUCCEEDED(hr)) hr=frame->Commit();
    if(SUCCEEDED(hr)) hr=encoder->Commit();
    context->Unmap(staging.Get(),0);
    return SUCCEEDED(hr);
}

LRESULT CALLBACK PreviewWindow(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return diva::App().Message(hwnd,message,w,l);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int argc=0;
    auto* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    wchar_t exePath[32768]{};
    GetModuleFileNameW(nullptr,exePath,32768);
    diva::fs::path home=diva::fs::path(exePath).parent_path();
    diva::fs::path output=diva::fs::current_path()/"artifacts/preview.png";
    bool smoke=false;
    std::string language,theme,page="panel";

    for(int i=1;i<argc;++i) {
        std::wstring arg=argv[i];
        if(arg==L"--smoke") smoke=true;
        else if(arg==L"--home" && i+1<argc) home=argv[++i];
        else if(arg==L"--output" && i+1<argc) output=argv[++i];
        else if(arg==L"--language" && i+1<argc) language=diva::Utf8(argv[++i]);
        else if(arg==L"--theme" && i+1<argc) theme=diva::Utf8(argv[++i]);
        else if(arg==L"--page" && i+1<argc) page=diva::Utf8(argv[++i]);
    }
    if(argv) LocalFree(argv);

    diva::InitLocale(home,language);
    if(const auto parsedTheme=diva::ParseUiTheme(theme)) diva::SetUiTheme(*parsedTheme);

    WNDCLASSW wc{};
    wc.lpfnWndProc=PreviewWindow;
    wc.hInstance=instance;
    wc.lpszClassName=L"DivaExtraModuleSetsPreview";
    RegisterClassW(&wc);
    const DWORD style=smoke?WS_OVERLAPPEDWINDOW:(WS_OVERLAPPEDWINDOW|WS_VISIBLE);
    HWND window=CreateWindowW(wc.lpszClassName,L"DivaExtraModuleSets Preview",style,CW_USEDEFAULT,CW_USEDEFAULT,1280,720,nullptr,nullptr,instance,nullptr);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    if(smoke) {
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) return 1;
        chain=new OffscreenSwapChain(device.Get(),window,1280,720);
        chain->Release();
    } else {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount=1;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow=window;
        desc.SampleDesc.Count=1;
        desc.Windowed=TRUE;
        if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context))) return 1;
    }

    auto& host=diva::App();
    host.preview=true;
    host.Init(home,home);
    if(const auto parsedTheme=diva::ParseUiTheme(theme)) {
        diva::SetUiTheme(*parsedTheme);
        host.view.settings.uiTheme=diva::UiThemeName(*parsedTheme);
        host.view.styleDirty=true;
    }
    host.view.previewMode=true;
    host.view.hasOverrideSnapshot=true;
    host.view.overrideSnapshot.supported=true;
    host.view.overrideSnapshot.enabled=true;
    host.view.overrideSnapshot.activeSlot={diva::SlotKind::Shared,3}; // Shared Set D
    host.view.overrideSnapshot.visibleSharedSlots=6;
    host.view.overrideSnapshot.visibleSongSlots=3;
    host.view.overrideSnapshot.storedSharedCount=4;
    host.view.overrideSnapshot.hiddenSharedCount=1;
    host.view.overrideSnapshot.storedSongCount=2;
    host.view.overrideSnapshot.hiddenSongCount=0;
    host.view.overrideSnapshot.saveFilePath=R"(C:\Users\Player\AppData\Roaming\SEGA\Project DIVA MEGA39's\Steam\76561198000000000\DivaExtraModuleSets.dat)";

    host.view.hasStoredShared=[](uint32_t idx) { return idx==3 || idx==4 || idx==5; };
    host.view.hasStoredSong=[](int32_t,uint32_t idx) { return idx==1; };
    host.view.onSelectSlot=[&](diva::ActiveSlot slot) {
        host.view.overrideSnapshot.activeSlot=slot;
    };

    if(page=="hud_customize") {
        host.view.open=false;
        host.view.overrideSnapshot.inCustomization=true;
        host.view.overrideSnapshot.activeSlot={diva::SlotKind::Shared,3};
    } else if(page=="hud_songselect") {
        host.view.open=false;
        host.view.overrideSnapshot.inSongSelect=true;
        host.view.overrideSnapshot.currentPvId=103;
        host.view.overrideSnapshot.activeSlot={diva::SlotKind::Song,1};
    } else {
        host.view.open=true;
        host.view.overrideSnapshot.inCustomization=true;
        host.view.settings.sharedSlots=6;
        host.view.settings.songSlots=3;
    }

    host.Attach(chain.Get(),device.Get(),context.Get());
    MSG msg{};
    int frames=0;
    while(msg.message!=WM_QUIT) {
        if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); continue; }
        ComPtr<ID3D11Texture2D> back;
        ComPtr<ID3D11RenderTargetView> rtv;
        if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&back))) && SUCCEEDED(device->CreateRenderTargetView(back.Get(),nullptr,&rtv))) {
            const float clear[4]{0.11f,0.16f,0.20f,1.0f};
            context->ClearRenderTargetView(rtv.Get(),clear);
        }
        host.Frame(chain.Get());
        chain->Present(smoke?0:1,0);
        if(smoke && ++frames==5) {
            std::error_code ec;
            diva::fs::create_directories(output.parent_path(),ec);
            if(!Screenshot(chain.Get(),device.Get(),context.Get(),output)) return 1;
            break;
        }
    }
    host.Shutdown();
    return 0;
}
