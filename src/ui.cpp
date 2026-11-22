#include "ui.h"
#include "i18n.h"
#include "search_ui_style.h"
#include "ui_theme.h"
#include "utf8_util.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace diva {
namespace {
ImFont* nativeHudFont=nullptr;

std::string SharedSlotLetter(int index) {
    if(index>=0 && index<26) return std::string(1,static_cast<char>('A'+index));
    return std::to_string(index+1);
}

bool SearchSegment(const char* label,bool selected,float width) {
    return ThemedTabButton(label,selected,{std::max(width,ImGui::CalcTextSize(label,nullptr,true).x+28.0f),36.0f});
}

void DrawBoldText(ImDrawList* dl,const ImVec2& pos,ImU32 col,const char* text) {
    dl->AddText(pos,col,text);
    dl->AddText({pos.x+0.7f,pos.y},col,text);
}

void DrawSlotChip(const char* label,bool active,bool hasData,bool extended,const ImVec2& size,const char* idSuffix) {
    (void)extended;
    const bool pearl=(CurrentUiTheme()==UiTheme::Pearl);
    ImDrawList* dl=ImGui::GetWindowDrawList();
    const ImVec2 min=ImGui::GetCursorScreenPos();
    const ImVec2 max{min.x+size.x,min.y+size.y};
    const bool hovered=ImGui::IsMouseHoveringRect(min,max);
    ImU32 bg=pearl?IM_COL32(231,238,243,255):IM_COL32(31,37,43,255);
    ImU32 border=pearl?IM_COL32(187,205,215,255):IM_COL32(60,72,82,255);
    ImU32 textCol=pearl?IM_COL32(36,68,84,255):IM_COL32(220,230,235,255);
    if(active) {
        bg=pearl?IM_COL32(0,191,203,255):IM_COL32(52,118,108,255);
        border=pearl?IM_COL32(39,124,144,255):IM_COL32(107,224,194,255);
        textCol=IM_COL32(255,255,255,255);
    } else if(hovered) {
        bg=pearl?IM_COL32(217,247,248,255):IM_COL32(48,66,72,255);
        border=pearl?IM_COL32(0,191,203,255):IM_COL32(107,224,194,255);
    }
    const float rounding=pearl?0.0f:4.0f;
    dl->AddRectFilled(min,max,bg,rounding);
    dl->AddRect(min,max,border,rounding,0,active?2.0f:1.0f);
    if(hasData && !active) {
        // 右上角粉色小圆点/方块标识“该拓展槽位已存有搭配”。
        const ImU32 dotCol=pearl?IM_COL32(229,93,159,255):IM_COL32(107,224,194,255);
        dl->AddRectFilled({max.x-9.0f,min.y+3.0f},{max.x-3.0f,min.y+9.0f},dotCol,pearl?0.0f:2.0f);
    }
    const ImVec2 ts=ImGui::CalcTextSize(label,nullptr,true);
    const ImVec2 tpos{min.x+(size.x-ts.x)*0.5f,min.y+(size.y-ts.y)*0.5f};
    if(active) DrawBoldText(dl,tpos,textCol,label);
    else dl->AddText(tpos,textCol,label);
    (void)idSuffix;
}
}

void SetupStyle(float size) {
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;

    static ImVector<ImWchar> ranges;
    ranges.clear();
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        builder.AddText("★☆·—↑↓←→…–");
        for(int i=0;i<kTxCount;++i) {
            builder.AddText(Tr(static_cast<Tx>(i)));
        }
        builder.BuildRanges(&ranges);
    }

    static ImVector<ImWchar> hudRanges;
    if(hudRanges.empty()) {
        // 仅收录 9 国游戏语言原生 Set 标签实际出现的字符，防止大号 HUD 字库撑爆 DX11 16384 纹理上限。
        static const ImWchar latinRanges[]={0x0020,0x00FF,0};
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(latinRanges);
        for(ImWchar wc:{
                ImWchar(0x5168),ImWchar(0x66F2),ImWchar(0x8BBE),ImWchar(0x5B9A),ImWchar(0x76EE),ImWchar(0x5355),ImWchar(0x72EC),
                ImWchar(0x5171),ImWchar(0x901A),ImWchar(0x8A2D),ImWchar(0x5225),ImWchar(0x6A02),ImWchar(0x55AE),ImWchar(0x7368),
                ImWchar(0x7528),ImWchar(0x7F6E),
                ImWchar(0xACF5),ImWchar(0xD1B5),ImWchar(0xC124),ImWchar(0xC815),ImWchar(0xACE1),ImWchar(0xBCC4)
            }) {
            builder.AddChar(wc);
        }
        builder.BuildRanges(&hudRanges);
    }

    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows,MAX_PATH);
    const auto fonts=fs::path(windows)/"Fonts";
    const auto yahei=fonts/"msyh.ttc";
    const bool chinese=CurrentLocale()=="zh-CN";
    const std::vector<fs::path> candidates=chinese
        ?std::vector<fs::path>{yahei,fonts/"YuGothM.ttc",fonts/"meiryo.ttc"}
        :std::vector<fs::path>{fonts/"YuGothM.ttc",fonts/"meiryo.ttc",yahei};
    const auto primary=std::find_if(candidates.begin(),candidates.end(),[](const fs::path& p){ return fs::exists(p); });
    if(primary==candidates.end()) {
        io.Fonts->AddFontDefault();
    } else {
        io.Fonts->AddFontFromFileTTF(Utf8(primary->wstring()).c_str(),size,nullptr,ranges.Data);
        if(!chinese && *primary!=yahei && fs::exists(yahei)) {
            ImFontConfig merge;
            merge.MergeMode=true;
            io.Fonts->AddFontFromFileTTF(Utf8(yahei.wstring()).c_str(),size,&merge,ranges.Data);
        }
    }

    // 为原生 16:9 Set 标签原地覆盖单独烘焙高清晰粗体字库，仅使用精确字符子集 hudRanges.Data。
    nativeHudFont=nullptr;
    const float hudFontPx=std::max(32.0f,std::round(size*1.85f));
    const fs::path msyhBold=fonts/"msyhbd.ttc";
    const fs::path yugothBold=fonts/"YuGothB.ttc";
    const fs::path malgunBold=fs::exists(fonts/"malgunbd.ttf")?(fonts/"malgunbd.ttf"):(fonts/"malgun.ttf");
    const fs::path latinBold=fs::exists(fonts/"arialbd.ttf")?(fonts/"arialbd.ttf"):(fonts/"segoeuib.ttf");
    std::vector<std::pair<fs::path,int>> hudFontList;
    if(chinese) {
        if(fs::exists(msyhBold)) hudFontList.push_back({msyhBold,1});
        if(fs::exists(yugothBold)) hudFontList.push_back({yugothBold,0});
    } else {
        if(fs::exists(yugothBold)) hudFontList.push_back({yugothBold,0});
        if(fs::exists(msyhBold)) hudFontList.push_back({msyhBold,1});
    }
    if(fs::exists(malgunBold)) hudFontList.push_back({malgunBold,0});
    if(fs::exists(latinBold)) hudFontList.push_back({latinBold,0});
    for(const auto& [fontPath,fontNo]:hudFontList) {
        ImFontConfig cfg;
        cfg.FontNo=fontNo;
        if(!nativeHudFont) {
            nativeHudFont=io.Fonts->AddFontFromFileTTF(Utf8(fontPath.wstring()).c_str(),hudFontPx,&cfg,hudRanges.Data);
        } else {
            cfg.MergeMode=true;
            io.Fonts->AddFontFromFileTTF(Utf8(fontPath.wstring()).c_str(),hudFontPx,&cfg,hudRanges.Data);
        }
    }
    ApplyUiTheme(ImGui::GetStyle());
}

void View::ReloadSettings() {
    settings=LoadSettings(home/"settings.ini");
    hotkey=ParseHotkey(settings.hotkey);
    uiOutputMode=settings.uiColorOutput;
    if(settings.uiWhiteNits=="auto") uiWhiteNits=0.0f;
    else {
        try { uiWhiteNits=std::clamp(std::stof(settings.uiWhiteNits),80.0f,400.0f); }
        catch(...) { uiWhiteNits=0.0f; }
    }
    if(const auto parsedTheme=ParseUiTheme(settings.uiTheme); parsedTheme && *parsedTheme!=CurrentUiTheme()) {
        SetUiTheme(*parsedTheme);
        styleDirty=true;
    }
}

void View::ToggleTheme() {
    const auto next=CurrentUiTheme()==UiTheme::Pearl?UiTheme::Classic:UiTheme::Pearl;
    SetUiTheme(next);
    settings.uiTheme=UiThemeName(next);
    styleDirty=true;
    if(onSettingsChanged) onSettingsChanged();
    if(onSaveSettings) onSaveSettings();
}

RuntimeSnapshot View::CurrentSnapshot() const {
    if(hasOverrideSnapshot) return overrideSnapshot;
    return Bridge().ReadSnapshot();
}

bool View::ShouldDrawHud(const RuntimeSnapshot& snap) const {
    if(!snap.enabled || settings.hudMode==HudMode::Off) return false;
    if(!snap.inCustomization && !snap.inSongSelect) return false;
    if(snap.isPlaylist) return false;
    if(settings.hudMode==HudMode::ExtendedOnly) {
        return snap.activeSlot.IsExtended() || (snap.activeSlot.kind==SlotKind::Song && snap.visibleSongSlots>1);
    }
    return true;
}

void View::Draw() {
    hdrBackgroundBounds={0,0,0,0};
    const RuntimeSnapshot snap=CurrentSnapshot();
    if(ShouldDrawHud(snap)) DrawHudBadge(snap);
    if(open) DrawSettingsWindow(snap);
}

void View::DrawHudBadge(const RuntimeSnapshot& snap) {
    const auto& io=ImGui::GetIO();
    const float w=io.DisplaySize.x;
    const float h=io.DisplaySize.y;
    if(w<=0.0f || h<=0.0f) return;

    // 映射至游戏 1920x1080 居中 16:9 2D UI 坐标系，自动兼容带黑边的非 16:9 窗口。
    constexpr float kTargetAspect=16.0f/9.0f;
    float uiW=w,uiH=h,uiX=0.0f,uiY=0.0f;
    if(w/h>kTargetAspect) {
        uiW=h*kTargetAspect;
        uiX=(w-uiW)*0.5f;
    } else if(w/h<kTargetAspect) {
        uiH=w/kTargetAspect;
        uiY=(h-uiH)*0.5f;
    }
    const float s=uiH/1080.0f;
    const float userScale=std::clamp(settings.hudScale,0.6f,2.0f);

    // 由实机 1080p 截图精确测量的内层银灰区域（完整保留原生白色外框与右侧 [F4] 键帽）：
    // 选曲页右下角：Top [1631..1836, 867] / Bottom [1597..1836, 901]；
    // 自定义页右上角：Top [1574..1847, 163] / Bottom [1528.5..1801.5, 208.5]。
    const bool customize=snap.inCustomization;
    const float cx=customize?1687.75f:1725.0f;
    const float cy=customize?185.75f:884.0f;
    const float tlX=customize?1574.0f:1631.0f;
    const float trX=customize?1847.0f:1836.0f;
    const float brX=customize?1801.5f:1836.0f;
    const float blX=customize?1528.5f:1597.0f;
    const float yTop=customize?163.0f:867.0f;
    const float yBot=customize?208.5f:901.0f;
    const float usableW=(customize?236.0f:192.0f)*s*userScale;
    const float baseFontSize=(customize?32.0f:24.5f)*s*userScale;
    const float outlineR=std::max(1.2f,(customize?2.5f:2.0f)*s*userScale);

    auto mapPt=[&](float px,float py) -> ImVec2 {
        const float sx=cx+(px-cx)*userScale;
        const float sy=cy+(py-cy)*userScale;
        return {uiX+sx*s+settings.hudOffsetX,uiY+sy*s+settings.hudOffsetY};
    };

    const std::string label=FormatNativeHudSlotLabel(
        snap.gameLanguage,
        snap.activeSlot.kind==SlotKind::Song,
        snap.activeSlot.index,
        snap.visibleSongSlots);

    ImGui::SetNextWindowPos({0,0},ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize,ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.0f);
    const ImGuiWindowFlags flags=ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground
        |ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus;
    if(ImGui::Begin("##DemsHudOverlay",nullptr,flags)) {
        ImDrawList* dl=ImGui::GetWindowDrawList();
        ImFont* font=nativeHudFont?nativeHudFont:ImGui::GetFont();

        const ImVec2 p0=mapPt(tlX,yTop);
        const ImVec2 p1=mapPt(trX,yTop);
        const ImVec2 p2=mapPt(brX,yBot);
        const ImVec2 p3=mapPt(blX,yBot);
        // 精确使用游戏原生银灰底色 #C5C5C5 遮盖底层 "Shared Set C" / "共通設定" 等贴图。
        dl->AddQuadFilled(p0,p1,p2,p3,IM_COL32(197,197,197,255));

        float drawFontSize=std::max(12.0f,baseFontSize);
        ImVec2 textSz=font->CalcTextSizeA(drawFontSize,FLT_MAX,0.0f,label.c_str());
        if(textSz.x>usableW && textSz.x>0.0f) {
            drawFontSize=std::max(12.0f*s*userScale,drawFontSize*(usableW/textSz.x));
            textSz=font->CalcTextSizeA(drawFontSize,FLT_MAX,0.0f,label.c_str());
        }
        const ImVec2 centerPx=mapPt(cx,cy);
        const ImVec2 textPos{std::round(centerPx.x-textSz.x*0.5f),std::round(centerPx.y-textSz.y*0.5f)};

        dl->PushClipRect({p3.x-2.0f,p0.y-1.0f},{p1.x+2.0f,p2.y+1.0f},true);
        // 1. 柔和右下投影。
        const ImVec2 shadowOffset{1.6f*s*userScale,2.0f*s*userScale};
        dl->AddText(font,drawFontSize,{textPos.x+shadowOffset.x,textPos.y+shadowOffset.y},IM_COL32(70,88,92,110),label.c_str());

        // 2. SEGA 原生初音青粗描边（#19B7BD）：外圈 16 向 + 内圈 8 向保证大号斜体笔画平滑无锯齿。
        constexpr float kPi=3.1415926535f;
        const ImU32 outlineCol=IM_COL32(25,183,189,255);
        for(int i=0;i<16;++i) {
            const float a=(2.0f*kPi*static_cast<float>(i))/16.0f;
            dl->AddText(font,drawFontSize,{textPos.x+std::cos(a)*outlineR,textPos.y+std::sin(a)*outlineR},outlineCol,label.c_str());
        }
        const float innerR=outlineR*0.55f;
        for(int i=0;i<8;++i) {
            const float a=(2.0f*kPi*static_cast<float>(i))/8.0f;
            dl->AddText(font,drawFontSize,{textPos.x+std::cos(a)*innerR,textPos.y+std::sin(a)*innerR},outlineCol,label.c_str());
        }

        // 3. 纯白文字主体 + 微量横向加粗，贴合原生界面字体视觉权重。
        const ImU32 fillCol=IM_COL32(255,255,255,255);
        dl->AddText(font,drawFontSize,textPos,fillCol,label.c_str());
        dl->AddText(font,drawFontSize,{textPos.x+0.4f*s*userScale,textPos.y},fillCol,label.c_str());
        dl->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void View::DrawSettingsWindow(const RuntimeSnapshot& snap) {
    const auto& io=ImGui::GetIO();
    const float winW=std::min(860.0f,io.DisplaySize.x*0.92f);
    const float winH=std::min(668.0f,io.DisplaySize.y*0.93f);
    ImGui::SetNextWindowSize({winW,winH},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({600.0f,460.0f},{1200.0f,960.0f});
    if(io.DisplaySize.x>0.0f && io.DisplaySize.y>0.0f) {
        ImGui::SetNextWindowPos({(io.DisplaySize.x-winW)*0.5f,(io.DisplaySize.y-winH)*0.5f},ImGuiCond_FirstUseEver);
    }

    std::string windowTitleBuf;
    const char* title=Tr(Tx::app_title);
    bool keepOpen=open;
    const ImGuiWindowFlags flags=ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
    if(ImGui::Begin(ThemedWindowName(title,windowTitleBuf),&keepOpen,flags)) {
        const ImVec2 winPos=ImGui::GetWindowPos();
        const ImVec2 winSize=ImGui::GetWindowSize();
        hdrBackgroundBounds={winPos.x,winPos.y,winPos.x+winSize.x,winPos.y+winSize.y};
        DrawThemedWindowFrame(title);

        // 顶部标签栏：对齐 Pearl 斜切平行四边形规范。
        const float overhang=ThemedTabOverhang(36.0f);
        if(overhang>0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+overhang);
        if(SearchSegment((std::string(Tr(Tx::panel_tab_slots))+"###tab_slots").c_str(),activeTab==0,140.0f)) activeTab=0;
        ImGui::SameLine();
        if(SearchSegment((std::string(Tr(Tx::panel_tab_display))+"###tab_display").c_str(),activeTab==1,140.0f)) activeTab=1;

        // 右上角显示当前游戏场景状态摘要。
        ImGui::SameLine();
        std::string sceneText;
        if(!snap.supported && snap.isGameHost) sceneText=Tr(Tx::panel_unsupported_notice);
        else if(snap.inCustomization) sceneText=Tr(Tx::panel_scene_customize);
        else if(snap.inSongSelect) sceneText=Trf(Tx::panel_scene_song_select,{{"pvid",std::to_string(snap.currentPvId)}});
        else sceneText=Tr(Tx::panel_scene_other);
        const float avail=ImGui::GetContentRegionAvail().x;
        const float textW=ImGui::CalcTextSize(sceneText.c_str(),nullptr,true).x;
        if(avail>textW+12.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+avail-textW);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(SearchSecondaryTextColor(),"%s",sceneText.c_str());

        ImGui::Separator();

        if(!snap.supported && snap.isGameHost) {
            ImGui::TextColored(ImVec4(0.82f,0.22f,0.30f,1.0f),"%s",Tr(Tx::panel_unsupported_notice));
            ImGui::Spacing();
        }
        if(snap.isPlaylist) {
            ImGui::TextColored(ImVec4(0.82f,0.48f,0.12f,1.0f),"%s",Tr(Tx::panel_playlist_notice));
            ImGui::Spacing();
        }

        // 滚动内容区，保证底部按钮栏固定可见。
        const float footerH=ImGui::GetFrameHeightWithSpacing()*1.45f;
        if(ImGui::BeginChild("##dems_body",{0,-footerH},false)) {
            if(activeTab==0) DrawSlotsTab(snap);
            else DrawDisplayTab(snap);
        }
        ImGui::EndChild();

        ImGui::Separator();
        // 底部保存与关闭按钮栏（按当前语言文案动态计算按钮宽度，避免伪翻译截断）。
        std::string saveStatusText;
        if(!statusBanner.empty()) saveStatusText=statusBanner;
        else if(snap.saveDirty) saveStatusText=Tr(Tx::save_status_dirty);
        else saveStatusText=Tr(Tx::save_status_clean);

        const char* lblCfg=Tr(Tx::save_btn_save_settings);
        const char* lblDat=Tr(Tx::save_btn_save_data);
        const char* lblClose=Tr(Tx::save_btn_close);
        const float btnH=34.0f;
        const float btnW1=std::max(126.0f,ImGui::CalcTextSize(lblCfg,nullptr,true).x+28.0f);
        const float btnW2=std::max(126.0f,ImGui::CalcTextSize(lblDat,nullptr,true).x+28.0f);
        const float btnW3=std::max(126.0f,ImGui::CalcTextSize(lblClose,nullptr,true).x+28.0f);
        const float gap=ImGui::GetStyle().ItemSpacing.x;
        const float totalBtnsW=btnW1+btnW2+btnW3+gap*2;
        const float rightEdge=ImGui::GetWindowContentRegionMax().x;
        const float buttonsStartX=rightEdge-totalBtnsW;

        ImGui::AlignTextToFramePadding();
        const float statusMaxW=std::max(80.0f,buttonsStartX-ImGui::GetCursorPosX()-12.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+statusMaxW);
        ImGui::TextColored(statusIsError?ImVec4(0.82f,0.22f,0.30f,1.0f):SearchSecondaryTextColor(),"%s",saveStatusText.c_str());
        ImGui::PopTextWrapPos();

        if(buttonsStartX>ImGui::GetCursorPosX()+12.0f) {
            ImGui::SameLine(buttonsStartX);
        }
        if(ThemedPrimaryButton((std::string(lblCfg)+"###btn_save_cfg").c_str(),{btnW1,btnH})) {
            const bool ok=onSaveSettings?onSaveSettings():false;
            statusIsError=!ok;
            statusBanner=Tr(ok?Tx::save_settings_saved:Tx::save_settings_failed);
        }
        ImGui::SameLine();
        if(ThemedSecondaryButton((std::string(lblDat)+"###btn_save_dat").c_str(),{btnW2,btnH})) {
            const bool ok=onManualSave?onManualSave():false;
            statusIsError=!ok;
            statusBanner=Tr(ok?Tx::save_status_saved:Tx::save_status_failed);
        }
        ImGui::SameLine();
        if(ThemedSecondaryButton((std::string(lblClose)+"###btn_close").c_str(),{btnW3,btnH})) {
            keepOpen=false;
        }
    }
    ImGui::End();
    open=keepOpen;
}

void View::DrawSlotsTab(const RuntimeSnapshot& snap) {
    // 1. 随时开关
    bool enabled=settings.enabled;
    if(ImGui::Checkbox((std::string(Tr(Tx::panel_enable_label))+"###chk_enable").c_str(),&enabled)) {
        settings.enabled=enabled;
        if(onSettingsChanged) onSettingsChanged();
    }
    ImGui::TextWrapped("%s",Tr(Tx::panel_enable_hint));
    ImGui::Separator();

    // 2. 槽位数量设置（非破坏式隐藏）
    DrawSearchGroupLabel(Tr(Tx::slots_section_counts));
    ImGui::NewLine();

    bool countsChanged=false;
    ImGui::SetNextItemWidth(190.0f);
    if(ImGui::SliderInt((std::string(Tr(Tx::slots_shared_label))+"###sld_shared").c_str(),&settings.sharedSlots,kMinSharedSlots,kMaxSharedSlots)) {
        countsChanged=true;
    }
    ImGui::SameLine();
    ImGui::TextColored(SearchSecondaryTextColor(),"%s",Tr(Tx::slots_shared_range_hint));

    ImGui::SetNextItemWidth(190.0f);
    if(ImGui::SliderInt((std::string(Tr(Tx::slots_song_label))+"###sld_song").c_str(),&settings.songSlots,kMinSongSlots,kMaxSongSlots)) {
        countsChanged=true;
    }
    ImGui::SameLine();
    ImGui::TextColored(SearchSecondaryTextColor(),"%s",Tr(Tx::slots_song_range_hint));

    if(countsChanged && onSettingsChanged) onSettingsChanged();

    const std::string preservedText=Trf(Tx::slots_hidden_preserved,{
        {"shared_saved",std::to_string(snap.storedSharedCount)},
        {"shared_hidden",std::to_string(snap.hiddenSharedCount)},
        {"song_saved",std::to_string(snap.storedSongCount)},
        {"song_hidden",std::to_string(snap.hiddenSongCount)},
    });
    ImGui::TextWrapped("%s",preservedText.c_str());
    ImGui::Separator();

    // 3. 快速槽位切换网格
    DrawSearchGroupLabel(Tr(Tx::slots_section_switcher));
    ImGui::NewLine();

    if(!settings.enabled) {
        ImGui::TextColored(SearchSecondaryTextColor(),"%s",Tr(Tx::slots_disabled_switcher));
        return;
    }

    ImGui::TextWrapped("%s",Tr(Tx::slots_jump_hint));

    // 通用设置 A-Z
    ImGui::Text("%s",Tr(Tx::slots_shared_group));
    const float chipW=56.0f;
    const float chipH=32.0f;
    const float maxW=ImGui::GetContentRegionAvail().x;
    float rowX=0.0f;
    for(int i=0;i<settings.sharedSlots;++i) {
        if(i>0 && rowX+chipW+8.0f<=maxW) {
            ImGui::SameLine();
        } else {
            rowX=0.0f;
        }
        const std::string letter=SharedSlotLetter(i);
        const bool active=(snap.activeSlot.kind==SlotKind::Shared && snap.activeSlot.index==i);
        const bool hasData=(i>=3) && (hasStoredShared?hasStoredShared(static_cast<uint32_t>(i)):false);
        const std::string btnId="##shared_chip_"+std::to_string(i);
        DrawSlotChip(letter.c_str(),active,hasData,i>=3,{chipW,chipH},btnId.c_str());
        if(ImGui::InvisibleButton(btnId.c_str(),{chipW,chipH})) {
            if(onSelectSlot) onSelectSlot(ActiveSlot{SlotKind::Shared,i});
        }
        rowX+=chipW+ImGui::GetStyle().ItemSpacing.x;
    }

    ImGui::Spacing();
    // 单曲设置 1-10
    ImGui::Text("%s",Tr(Tx::slots_song_group));
    const float songChipW=92.0f;
    rowX=0.0f;
    for(int i=0;i<settings.songSlots;++i) {
        if(i>0 && rowX+songChipW+8.0f<=maxW) {
            ImGui::SameLine();
        } else {
            rowX=0.0f;
        }
        const std::string chipLabel=Trf(Tx::slots_song_chip,{{"index",std::to_string(i+1)}});
        const bool active=(snap.activeSlot.kind==SlotKind::Song && snap.activeSlot.index==i);
        const bool hasData=(i>=1 && snap.currentPvId>0) && (hasStoredSong?hasStoredSong(snap.currentPvId,static_cast<uint32_t>(i)):false);
        const std::string btnId="##song_chip_"+std::to_string(i);
        DrawSlotChip(chipLabel.c_str(),active,hasData,i>=1,{songChipW,chipH},btnId.c_str());
        if(ImGui::InvisibleButton(btnId.c_str(),{songChipW,chipH})) {
            if(onSelectSlot) onSelectSlot(ActiveSlot{SlotKind::Song,i});
        }
        rowX+=songChipW+ImGui::GetStyle().ItemSpacing.x;
    }
}

void View::DrawDisplayTab(const RuntimeSnapshot& snap) {
    DrawSearchGroupLabel(Tr(Tx::display_section_hud));
    ImGui::NewLine();

    int modeIdx=static_cast<int>(settings.hudMode);
    const char* modeItems[3]{Tr(Tx::display_hud_always),Tr(Tx::display_hud_extended_only),Tr(Tx::display_hud_off)};
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::Combo((std::string(Tr(Tx::display_hud_mode))+"###cmb_hud").c_str(),&modeIdx,modeItems,3)) {
        settings.hudMode=static_cast<HudMode>(std::clamp(modeIdx,0,2));
        if(onSettingsChanged) onSettingsChanged();
    }

    bool hudChanged=false;
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::SliderFloat((std::string(Tr(Tx::display_hud_offset_x))+"###sld_hx").c_str(),&settings.hudOffsetX,-800.0f,200.0f,"%.0f px")) hudChanged=true;
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::SliderFloat((std::string(Tr(Tx::display_hud_offset_y))+"###sld_hy").c_str(),&settings.hudOffsetY,-200.0f,600.0f,"%.0f px")) hudChanged=true;
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::SliderFloat((std::string(Tr(Tx::display_hud_scale))+"###sld_hs").c_str(),&settings.hudScale,0.6f,2.0f,"%.2fx")) hudChanged=true;
    if(ThemedSecondaryButton((std::string(Tr(Tx::display_hud_reset))+"###btn_hud_reset").c_str(),{160.0f,30.0f})) {
        settings.hudOffsetX=0.0f; settings.hudOffsetY=0.0f; settings.hudScale=1.0f;
        hudChanged=true;
    }
    if(hudChanged && onSettingsChanged) onSettingsChanged();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawSearchGroupLabel(Tr(Tx::display_section_ui));
    ImGui::NewLine();

    const bool isPearl=(CurrentUiTheme()==UiTheme::Pearl);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s",Tr(Tx::display_theme_label));
    ImGui::SameLine();
    if(ThemedSecondaryButton((std::string(Tr(isPearl?Tx::display_theme_pearl:Tx::display_theme_classic))+"###btn_theme").c_str(),{180.0f,30.0f})) {
        ToggleTheme();
    }

    const char* langValues[4]{"auto","zh-CN","en","ja"};
    const char* langLabels[4]{Tr(Tx::display_language_auto),Tr(Tx::display_language_zh_cn),Tr(Tx::display_language_en),Tr(Tx::display_language_ja)};
    int langIdx=0;
    for(int i=0;i<4;++i) if(settings.language==langValues[i]) { langIdx=i; break; }
    ImGui::SetNextItemWidth(260.0f);
    if(ImGui::Combo((std::string(Tr(Tx::display_language_label))+"###cmb_lang").c_str(),&langIdx,langLabels,4)) {
        settings.language=langValues[langIdx];
        if(onSettingsChanged) onSettingsChanged();
    }

    const std::string hkText=Trf(Tx::display_hotkey_label,{{"hotkey",settings.hotkey}});
    ImGui::TextColored(SearchSecondaryTextColor(),"%s",hkText.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawSearchGroupLabel(Tr(Tx::save_section_title));
    ImGui::NewLine();
    const std::string pathMsg=snap.saveFilePath.empty()
        ?std::string(Tr(Tx::save_path_pending))
        :Trf(Tx::save_path_label,{{"path",snap.saveFilePath}});
    ImGui::TextWrapped("%s",pathMsg.c_str());
}
}
