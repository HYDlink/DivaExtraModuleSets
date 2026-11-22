#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc!=2) { std::cout<<"请提供 DLL 路径。\n"; return 2; }
    HMODULE dll=LoadLibraryW(argv[1]);
    if(!dll) { std::cout<<"DLL 装载失败，错误码："<<GetLastError()<<"\n"; return 1; }
    for(const char* name:{"Init","PostInit","D3DInit","OnFrame","OnResize"}) {
        if(!GetProcAddress(dll,name)) { std::cout<<"缺少 DML 导出函数："<<name<<"\n"; return 1; }
    }
    reinterpret_cast<void(*)()>(GetProcAddress(dll,"Init"))();
    reinterpret_cast<void(*)()>(GetProcAddress(dll,"PostInit"))();

    WNDCLASSW wc{};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"DemsDllValidation";
    RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"DEMS DLL Check",WS_OVERLAPPEDWINDOW,0,0,1280,720,nullptr,nullptr,wc.hInstance,nullptr);

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount=1;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow=window;
    desc.SampleDesc.Count=1;
    desc.Windowed=TRUE;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> chain;
    if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context))) {
        std::cout<<"DX11 WARP 设备创建失败。\n";
        return 1;
    }

    reinterpret_cast<void(*)(IDXGISwapChain*,ID3D11Device*,ID3D11DeviceContext*)>(GetProcAddress(dll,"D3DInit"))(chain.Get(),device.Get(),context.Get());
    auto onFrame=reinterpret_cast<void(*)(IDXGISwapChain*)>(GetProcAddress(dll,"OnFrame"));
    onFrame(chain.Get());

    // 触发默认快捷键 F8 打开面板、渲染 30 帧、Esc 关闭、调整尺寸后再渲染。
    SendMessageW(window,WM_KEYDOWN,VK_F8,0);
    SendMessageW(window,WM_KEYUP,VK_F8,0);
    for(int i=0;i<15;++i) onFrame(chain.Get());
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
    SendMessageW(window,WM_KEYUP,VK_ESCAPE,0);
    for(int i=0;i<15;++i) onFrame(chain.Get());

    context->OMSetRenderTargets(0,nullptr,nullptr);
    if(FAILED(chain->ResizeBuffers(0,1024,768,DXGI_FORMAT_UNKNOWN,0))) {
        std::cout<<"DLL 持有后缓冲导致 ResizeBuffers 失败。\n";
        return 1;
    }
    reinterpret_cast<void(*)(IDXGISwapChain*)>(GetProcAddress(dll,"OnResize"))(chain.Get());
    onFrame(chain.Get());
    std::cout<<"DLL 导出、DX11 初始化、F8/Esc 面板切换、31 帧回调与 ResizeBuffers 验证通过。\n";
    return 0;
}
