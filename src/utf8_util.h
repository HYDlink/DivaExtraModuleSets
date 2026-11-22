#pragma once
#include <Windows.h>
#include <string>
#include <string_view>

namespace diva {
inline std::string Utf8(std::wstring_view wide) {
    if(wide.empty()) return {};
    const int needed=WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
    if(needed<=0) return {};
    std::string out(static_cast<size_t>(needed),'\0');
    WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),out.data(),needed,nullptr,nullptr);
    return out;
}
inline std::wstring Wide(std::string_view utf8) {
    if(utf8.empty()) return {};
    const int needed=MultiByteToWideChar(CP_UTF8,0,utf8.data(),static_cast<int>(utf8.size()),nullptr,0);
    if(needed<=0) return {};
    std::wstring out(static_cast<size_t>(needed),L'\0');
    MultiByteToWideChar(CP_UTF8,0,utf8.data(),static_cast<int>(utf8.size()),out.data(),needed);
    return out;
}
}
