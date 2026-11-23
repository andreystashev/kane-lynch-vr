#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

// Shared process identity for the DLL, compositor and external exit observer.
// No file discovery or previous-session deletion is performed here.
namespace DiagnosticsSession {
// Explicit release opt-in. Legacy per-probe keys cannot enable disk I/O by themselves.
inline bool Enabled(const std::wstring& package){
    const auto ini=package+(package.empty()||package.back()==L'\\'||package.back()==L'/'?L"":L"\\")+L"vr.ini";
    return GetPrivateProfileIntW(L"Diagnostics",L"Enabled",0,ini.c_str())!=0;
}
inline std::uint64_t Ticks(FILETIME time){return (std::uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;}
inline std::uint64_t Creation(HANDLE process){FILETIME created{},exit{},kernel{},user{};
    return GetProcessTimes(process,&created,&exit,&kernel,&user)?Ticks(created):0;}
inline std::wstring Join(const std::wstring& base,const std::wstring& name){
    return base+(base.empty()||base.back()==L'\\'||base.back()==L'/'?L"":L"\\")+name;
}
inline std::wstring Directory(const std::wstring& package,DWORD pid,std::uint64_t creation){
    if(package.empty()||!pid||!creation)return {};
    FILETIME file{DWORD(creation),DWORD(creation>>32)};SYSTEMTIME time{};
    if(!FileTimeToSystemTime(&file,&time))return {};
    wchar_t name[144]{};
    std::swprintf(name,144,L"session-%04u%02u%02uT%02u%02u%02uZ-%lu-%016llx",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,pid,
        static_cast<unsigned long long>(creation));
    return Join(Join(package,L"logs"),name);
}
inline bool Ensure(const std::wstring& directory){
    if(directory.empty())return false;
    if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    const auto attributes=GetFileAttributesW(directory.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
inline std::wstring CurrentDirectory(const std::wstring& package){
    const auto directory=Directory(package,GetCurrentProcessId(),Creation(GetCurrentProcess()));
    return Ensure(Join(package,L"logs"))&&Ensure(directory)?directory:std::wstring{};
}
inline std::string Utc(){SYSTEMTIME time{};GetSystemTime(&time);char text[48]{};
    std::snprintf(text,sizeof(text),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",time.wYear,time.wMonth,time.wDay,
        time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);return text;
}
inline std::string Utf8(const std::wstring& value){
    const int count=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(count<=0)return {};std::string out(count,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),out.data(),count,nullptr,nullptr);return out;
}
inline std::string Quote(const std::string& value){std::string out="\"";constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';
}
}
