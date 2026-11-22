#include "i18n.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#define CHECK(expr) do { \
    if(!(expr)) { \
        std::cerr<<"CHECK failed at line "<<__LINE__<<": "<<#expr<<"\n"; \
        std::abort(); \
    } \
} while(0)

namespace fs=std::filesystem;
using namespace diva;

int main() {
    const fs::path emptyHome=fs::temp_directory_path()/L"dems_i18n_test_empty";
    for(const char* lang:{"zh-CN","en","ja","qps"}) {
        InitLocale(emptyHome,lang);
        CHECK(CurrentLocale()==lang);
        for(int i=0;i<kTxCount;++i) {
            const char* text=Tr(static_cast<Tx>(i));
            CHECK(text && text[0]!='\0');
        }
        const std::string s1=Trf(Tx::hud_shared_set,{{"letter","D"}});
        CHECK(s1.find('D')!=std::string::npos);
        const std::string s2=Trf(Tx::slots_hidden_preserved,{
            {"shared_saved","3"},{"shared_hidden","1"},{"song_saved","2"},{"song_hidden","1"}
        });
        CHECK(s2.find('3')!=std::string::npos && s2.find('2')!=std::string::npos);
    }

    // 验证全部 9 种 PC 1.03 游戏语言的原地 HUD 标签格式化。
    for(const char* gameLang:{"cn","tw","jp","en","kr","fr","ge","it","sp"}) {
        const std::string shA=FormatNativeHudSlotLabel(gameLang,false,0,1);
        const std::string shF=FormatNativeHudSlotLabel(gameLang,false,5,1);
        const std::string sg1Single=FormatNativeHudSlotLabel(gameLang,true,0,1);
        const std::string sg1Multi=FormatNativeHudSlotLabel(gameLang,true,0,3);
        const std::string sg2=FormatNativeHudSlotLabel(gameLang,true,1,3);
        CHECK(!shA.empty() && shA.find('A')!=std::string::npos);
        CHECK(!shF.empty() && shF.find('F')!=std::string::npos && shF!=shA);
        CHECK(!sg1Single.empty() && sg1Single.find('1')==std::string::npos);
        CHECK(!sg1Multi.empty() && sg1Multi.find('1')!=std::string::npos);
        CHECK(!sg2.empty() && sg2.find('2')!=std::string::npos && sg2!=sg1Multi);
    }
    CHECK(FormatNativeHudSlotLabel("en",false,5,1)=="Shared Set F");
    CHECK(FormatNativeHudSlotLabel("en",true,1,3)=="Song Specific 2");
    CHECK(FormatNativeHudSlotLabel("cn",true,0,1)=="曲目单独设定");
    CHECK(FormatNativeHudSlotLabel("cn",false,5,1)=="全曲设定 F");
    CHECK(FormatNativeHudSlotLabel("tw",false,5,1)=="全曲設定 F");
    CHECK(FormatNativeHudSlotLabel("tw",true,1,3)=="曲目單獨設定 2");
    CHECK(FormatNativeHudSlotLabel("jp",false,5,1)=="共通設定 F");
    CHECK(FormatNativeHudSlotLabel("jp",true,1,3)=="曲別設定 2");

    // 验证空 gameLanguage 时回退当前界面语言（预览宿主模式）。
    InitLocale(emptyHome,"en");
    CHECK(FormatNativeHudSlotLabel("",false,3,1)=="Shared Set D");

    std::cout<<"I18nCheck: all "<<kTxCount<<" keys across 4 locales + 9 game HUD languages passed.\n";
    return 0;
}
