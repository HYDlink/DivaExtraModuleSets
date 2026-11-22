#include "settings.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace diva {
namespace {
std::string TrimCopy(std::string_view sv) {
    const auto begin=sv.find_first_not_of(" \t\r\n");
    if(begin==sv.npos) return {};
    const auto end=sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(begin,end-begin+1));
}
std::string LowerCopy(std::string_view sv) {
    std::string out(sv);
    for(char& c:out) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
bool ParseBool(std::string_view sv,bool fallback) {
    const auto low=LowerCopy(TrimCopy(sv));
    if(low=="1" || low=="true" || low=="yes" || low=="on") return true;
    if(low=="0" || low=="false" || low=="no" || low=="off") return false;
    return fallback;
}
std::string FormatFloat(float v) {
    char buf[32]{};
    std::snprintf(buf,sizeof(buf),"%.2f",v);
    std::string s(buf);
    while(s.find('.')!=std::string::npos && s.back()=='0') s.pop_back();
    if(!s.empty() && s.back()=='.') s.pop_back();
    return s;
}
}

const char* HudModeName(HudMode mode) {
    switch(mode) {
    case HudMode::ExtendedOnly: return "extended_only";
    case HudMode::Off: return "off";
    default: return "always";
    }
}

HudMode ParseHudMode(std::string_view text) {
    const auto low=LowerCopy(TrimCopy(text));
    if(low=="extended_only" || low=="extended") return HudMode::ExtendedOnly;
    if(low=="off" || low=="none" || low=="hidden") return HudMode::Off;
    return HudMode::Always;
}

ParsedHotkey ParseHotkey(std::string_view text) {
    ParsedHotkey out{};
    out.valid=false;
    std::string raw=TrimCopy(text);
    if(raw.empty()) { out.vk=VK_F8; out.valid=true; return out; }
    size_t start=0;
    std::string token;
    while(start<=raw.size()) {
        const size_t plus=raw.find('+',start);
        const std::string part=TrimCopy(plus==std::string::npos?std::string_view(raw).substr(start):std::string_view(raw).substr(start,plus-start));
        const std::string low=LowerCopy(part);
        if(low=="ctrl" || low=="control") out.ctrl=true;
        else if(low=="shift") out.shift=true;
        else if(low=="alt") out.alt=true;
        else if(!part.empty()) token=low;
        if(plus==std::string::npos) break;
        start=plus+1;
    }
    if(token.empty()) { out.vk=VK_F8; out.valid=true; return out; }
    if(token.size()>=2 && token[0]=='f') {
        try {
            const int fn=std::stoi(token.substr(1));
            if(fn>=1 && fn<=12) { out.vk=static_cast<unsigned int>(VK_F1+(fn-1)); out.valid=true; return out; }
        } catch(...) {}
    }
    if(token.size()==1) {
        const unsigned char ch=static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(token[0])));
        if((ch>='A' && ch<='Z') || (ch>='0' && ch<='9')) {
            out.vk=ch; out.valid=true; return out;
        }
    }
    if(token=="insert" || token=="ins") { out.vk=VK_INSERT; out.valid=true; return out; }
    if(token=="home") { out.vk=VK_HOME; out.valid=true; return out; }
    if(token=="end") { out.vk=VK_END; out.valid=true; return out; }
    out.vk=VK_F8; out.ctrl=out.shift=out.alt=false; out.valid=false;
    return out;
}

std::string FormatHotkey(const ParsedHotkey& hotkey) {
    std::string out;
    if(hotkey.ctrl) out+="Ctrl+";
    if(hotkey.shift) out+="Shift+";
    if(hotkey.alt) out+="Alt+";
    if(hotkey.vk>=VK_F1 && hotkey.vk<=VK_F12) out+="F"+std::to_string(hotkey.vk-VK_F1+1);
    else if((hotkey.vk>='A' && hotkey.vk<='Z') || (hotkey.vk>='0' && hotkey.vk<='9')) out+=static_cast<char>(hotkey.vk);
    else if(hotkey.vk==VK_INSERT) out+="Insert";
    else if(hotkey.vk==VK_HOME) out+="Home";
    else if(hotkey.vk==VK_END) out+="End";
    else out+="F8";
    return out;
}

ModSettings LoadSettings(const fs::path& path) {
    ModSettings s;
    std::error_code ec;
    if(!fs::exists(path,ec)) return s;
    std::ifstream in(path,std::ios::binary);
    if(!in.is_open()) return s;
    std::string line;
    while(std::getline(in,line)) {
        const auto trimmed=TrimCopy(line);
        if(trimmed.empty() || trimmed[0]=='#' || trimmed[0]==';') continue;
        const size_t eq=trimmed.find('=');
        if(eq==std::string::npos) continue;
        const std::string key=LowerCopy(TrimCopy(std::string_view(trimmed).substr(0,eq)));
        std::string val=TrimCopy(std::string_view(trimmed).substr(eq+1));
        try {
            if(key=="enabled") s.enabled=ParseBool(val,s.enabled);
            else if(key=="shared_slots") s.sharedSlots=std::clamp(std::stoi(val),kMinSharedSlots,kMaxSharedSlots);
            else if(key=="song_slots") s.songSlots=std::clamp(std::stoi(val),kMinSongSlots,kMaxSongSlots);
            else if(key=="hud_mode") s.hudMode=ParseHudMode(val);
            else if(key=="hud_offset_x") {
                const float f=std::stof(val);
                if(std::isfinite(f)) s.hudOffsetX=std::clamp(f,-2000.0f,2000.0f);
            }
            else if(key=="hud_offset_y") {
                const float f=std::stof(val);
                if(std::isfinite(f)) s.hudOffsetY=std::clamp(f,-2000.0f,2000.0f);
            }
            else if(key=="hud_scale") {
                const float f=std::stof(val);
                if(std::isfinite(f)) s.hudScale=std::clamp(f,0.6f,2.0f);
            }
            else if(key=="ui_theme") {
                const auto low=LowerCopy(val);
                if(low=="pearl" || low=="classic") s.uiTheme=low;
            }
            else if(key=="language") {
                if(val=="auto" || val=="zh-CN" || val=="en" || val=="ja") s.language=val;
            }
            else if(key=="hotkey") {
                const auto parsed=ParseHotkey(val);
                if(parsed.valid) s.hotkey=FormatHotkey(parsed);
            }
            else if(key=="ui_color_output") {
                const auto low=LowerCopy(val);
                if(low=="auto" || low=="sdr" || low=="hdr10" || low=="scrgb") s.uiColorOutput=low;
            }
            else if(key=="ui_white_nits") {
                const auto low=LowerCopy(val);
                if(low=="auto") s.uiWhiteNits="auto";
                else {
                    const float nits=std::stof(val);
                    if(std::isfinite(nits)) s.uiWhiteNits=FormatFloat(std::clamp(nits,80.0f,400.0f));
                }
            }
        } catch(...) {}
    }
    return s;
}

bool SaveSettings(const fs::path& path,const ModSettings& settings) {
    std::string text;
    {
        std::ifstream in(path,std::ios::binary);
        if(in.is_open()) text.assign(std::istreambuf_iterator<char>(in),{});
    }
    const std::string newline=text.find("\r\n")!=std::string::npos?"\r\n":"\n";
    const std::vector<std::pair<std::string,std::string>> entries{
        {"enabled", settings.enabled?"true":"false"},
        {"shared_slots", std::to_string(std::clamp(settings.sharedSlots,kMinSharedSlots,kMaxSharedSlots))},
        {"song_slots", std::to_string(std::clamp(settings.songSlots,kMinSongSlots,kMaxSongSlots))},
        {"hud_mode", HudModeName(settings.hudMode)},
        {"hud_offset_x", FormatFloat(settings.hudOffsetX)},
        {"hud_offset_y", FormatFloat(settings.hudOffsetY)},
        {"hud_scale", FormatFloat(settings.hudScale)},
        {"ui_theme", settings.uiTheme},
        {"language", settings.language},
        {"hotkey", settings.hotkey},
        {"ui_color_output", settings.uiColorOutput},
        {"ui_white_nits", settings.uiWhiteNits},
    };
    std::vector<bool> written(entries.size(),false);
    std::string out;
    size_t start=0;
    while(start<text.size()) {
        size_t end=text.find('\n',start);
        const bool last=end==std::string::npos;
        if(last) end=text.size();
        std::string line=text.substr(start,end-start);
        if(!line.empty() && line.back()=='\r') line.pop_back();
        bool replaced=false,duplicate=false;
        const auto trimmed=TrimCopy(line);
        if(!trimmed.empty() && trimmed[0]!='#' && trimmed[0]!=';') {
            const size_t eq=trimmed.find('=');
            if(eq!=std::string::npos) {
                const std::string key=LowerCopy(TrimCopy(std::string_view(trimmed).substr(0,eq)));
                for(size_t i=0;i<entries.size();++i) {
                    if(key==entries[i].first) {
                        if(written[i]) duplicate=true;
                        else { line=entries[i].first+"="+entries[i].second; written[i]=true; replaced=true; }
                        break;
                    }
                }
            }
        }
        if(!duplicate) {
            out+=line;
            if(!last || replaced) out+=newline;
        }
        start=end+1;
    }
    for(size_t i=0;i<entries.size();++i) {
        if(!written[i]) {
            if(!out.empty() && !out.ends_with("\n")) out+=newline;
            out+=entries[i].first+"="+entries[i].second+newline;
        }
    }
    std::error_code ec;
    if(!path.parent_path().empty()) fs::create_directories(path.parent_path(),ec);
    const fs::path tmp=fs::path(path).concat(L".tmp");
    {
        std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
        f<<out;
        f.flush();
        if(!f.good()) return false;
    }
    return MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
}
