#pragma once
#include "native_hooks.h"
#include "settings.h"
#include <imgui.h>
#include <filesystem>
#include <functional>
#include <string>

namespace diva {
namespace fs=std::filesystem;

void SetupStyle(float size);

class View {
public:
    fs::path home,game;
    ModSettings settings;
    ParsedHotkey hotkey;
    std::string uiOutputMode="auto";
    float uiWhiteNits=0.0f;
    float fontSize=19.0f;
    bool open=false;
    bool fontDirty=false;
    bool styleDirty=false;
    bool previewMode=false;
    int activeTab=0;
    std::string statusBanner;
    bool statusIsError=false;
    ImVec4 hdrBackgroundBounds{0,0,0,0};

    // 供独立预览进程覆盖的只读快照；实机运行时为空则读取 NativeBridge。
    bool hasOverrideSnapshot=false;
    RuntimeSnapshot overrideSnapshot{};

    std::function<void()> onFirstOpen;
    std::function<void()> onOpen;
    std::function<void()> onSettingsChanged;
    std::function<bool()> onSaveSettings;
    std::function<bool()> onManualSave;
    std::function<void(ActiveSlot)> onSelectSlot;
    std::function<bool(uint32_t)> hasStoredShared;
    std::function<bool(int32_t,uint32_t)> hasStoredSong;

    void ReloadSettings();
    void ToggleTheme();
    RuntimeSnapshot CurrentSnapshot() const;
    bool ShouldDrawHud(const RuntimeSnapshot& snap) const;
    void Draw();

private:
    void DrawHudBadge(const RuntimeSnapshot& snap);
    void DrawSettingsWindow(const RuntimeSnapshot& snap);
    void DrawSlotsTab(const RuntimeSnapshot& snap);
    void DrawDisplayTab(const RuntimeSnapshot& snap);
};
}
