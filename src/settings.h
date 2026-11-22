#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace diva {
namespace fs=std::filesystem;

inline constexpr int kMinSharedSlots=1;
inline constexpr int kMaxSharedSlots=26;
inline constexpr int kVanillaSharedSlots=3;
inline constexpr int kMinSongSlots=1;
inline constexpr int kMaxSongSlots=10;
inline constexpr int kVanillaSongSlots=1;

enum class HudMode { Always=0, ExtendedOnly=1, Off=2 };

struct ParsedHotkey {
    unsigned int vk=0x77; // VK_F8
    bool ctrl=false;
    bool shift=false;
    bool alt=false;
    bool valid=true;
};

struct ModSettings {
    bool enabled=true;
    int sharedSlots=6;
    int songSlots=2;
    HudMode hudMode=HudMode::Always;
    float hudOffsetX=0.0f;
    float hudOffsetY=0.0f;
    float hudScale=1.0f;
    std::string uiTheme="pearl";
    std::string language="auto";
    std::string hotkey="F8";
    std::string uiColorOutput="auto";
    std::string uiWhiteNits="auto";

    bool operator==(const ModSettings&) const=default;
};

const char* HudModeName(HudMode mode);
HudMode ParseHudMode(std::string_view text);
ParsedHotkey ParseHotkey(std::string_view text);
std::string FormatHotkey(const ParsedHotkey& hotkey);
ModSettings LoadSettings(const fs::path& path);
bool SaveSettings(const fs::path& path,const ModSettings& settings);
}
