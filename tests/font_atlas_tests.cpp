#include "i18n.h"
#include "ui.h"
#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(expr) do { \
    if(!(expr)) { \
        std::cerr<<"CHECK failed at line "<<__LINE__<<": "<<#expr<<"\n"; \
        std::abort(); \
    } \
} while(0)

using Microsoft::WRL::ComPtr;
using namespace diva;

namespace {
std::vector<uint32_t> DecodeUtf8Codepoints(const std::string& text) {
    std::vector<uint32_t> out;
    const auto* p=reinterpret_cast<const unsigned char*>(text.data());
    size_t i=0;
    while(i<text.size()) {
        const unsigned char b=p[i];
        uint32_t cp=0;
        if((b&0x80)==0) { cp=b; i+=1; }
        else if((b&0xE0)==0xC0 && i+1<text.size()) { cp=((b&0x1F)<<6)|(p[i+1]&0x3F); i+=2; }
        else if((b&0xF0)==0xE0 && i+2<text.size()) { cp=((b&0x0F)<<12)|((p[i+1]&0x3F)<<6)|(p[i+2]&0x3F); i+=3; }
        else if((b&0xF8)==0xF0 && i+3<text.size()) { cp=((b&0x07)<<18)|((p[i+1]&0x3F)<<12)|((p[i+2]&0x3F)<<6)|(p[i+3]&0x3F); i+=4; }
        else { i+=1; continue; }
        out.push_back(cp);
    }
    return out;
}

ComPtr<ID3D11Device> CreateTestD3D11Device() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,2,D3D11_SDK_VERSION,&device,nullptr,&ctx);
    if(FAILED(hr)) {
        hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,2,D3D11_SDK_VERSION,&device,nullptr,&ctx);
    }
    CHECK(SUCCEEDED(hr) && device!=nullptr);
    return device;
}
}

int main() {
    auto device=CreateTestD3D11Device();
    const std::filesystem::path emptyHome=std::filesystem::temp_directory_path()/L"dems_font_atlas_test_empty";

    for(const char* locale:{"zh-CN","en","ja"}) {
        InitLocale(emptyHome,locale);
        for(float fontSize:{19.0f,24.0f}) {
            ImGuiContext* ctx=ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            SetupStyle(fontSize);

            auto& io=ImGui::GetIO();
            unsigned char* pixels=nullptr;
            int texW=0,texH=0;
            io.Fonts->GetTexDataAsRGBA32(&pixels,&texW,&texH);
            std::cout<<"Atlas locale="<<locale<<" size="<<fontSize<<" -> "<<texW<<"x"<<texH
                     <<" fonts="<<io.Fonts->Fonts.Size<<"\n";

            CHECK(pixels!=nullptr);
            CHECK(texW>0 && texH>0);
            // 严格控制在 8192 以内，远离 DX11 16384 上限，杜绝 CreateTexture2D E_INVALIDARG 崩溃。
            CHECK(texW<=8192 && texH<=8192);

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=static_cast<UINT>(texW);
            desc.Height=static_cast<UINT>(texH);
            desc.MipLevels=1;
            desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count=1;
            desc.Usage=D3D11_USAGE_DEFAULT;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA initData{};
            initData.pSysMem=pixels;
            initData.SysMemPitch=static_cast<UINT>(texW*4);

            ComPtr<ID3D11Texture2D> tex;
            HRESULT hr=device->CreateTexture2D(&desc,&initData,&tex);
            CHECK(SUCCEEDED(hr) && tex!=nullptr);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels=1;
            ComPtr<ID3D11ShaderResourceView> srv;
            hr=device->CreateShaderResourceView(tex.Get(),&srvDesc,&srv);
            CHECK(SUCCEEDED(hr) && srv!=nullptr);

            ImFont* hudFont=io.Fonts->Fonts.back();
            CHECK(hudFont!=nullptr);
            for(const char* gameLang:{"cn","tw","jp","en","kr","fr","ge","it","sp"}) {
                const std::string l1=FormatNativeHudSlotLabel(gameLang,false,5,1);
                const std::string l2=FormatNativeHudSlotLabel(gameLang,true,1,3);
                for(const auto& s:{l1,l2}) {
                    for(uint32_t cp:DecodeUtf8Codepoints(s)) {
                        const ImFontGlyph* g=hudFont->FindGlyphNoFallback(static_cast<ImWchar>(cp));
                        if(!g) {
                            std::cerr<<"Missing glyph U+"<<std::hex<<cp<<std::dec<<" for gameLang="<<gameLang<<" locale="<<locale<<"\n";
                        }
                        CHECK(g!=nullptr);
                    }
                }
            }

            ImGui::DestroyContext(ctx);
        }
    }

    std::cout<<"FontAtlasTests: all locales, DX11 textures, and 9-language HUD glyphs passed.\n";
    return 0;
}
