#pragma once
#include "profile.hpp"
#include <string>
#include <cwctype>
// KL1 equivalents of the Blood Money launcher's file/path helpers.
namespace LauncherSettings { namespace Detail {
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(*this)CloseHandle(value);}
    explicit operator bool() const{return value&&value!=INVALID_HANDLE_VALUE;}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
inline std::wstring Lower(std::wstring value){for(auto& c:value)c=static_cast<wchar_t>(towlower(c));return value;}
inline std::wstring Wide(const char* value){std::wstring result;while(*value)result+=static_cast<unsigned char>(*value++);return result;}
}}
namespace LauncherConnection { namespace Detail {
inline std::wstring Join(const std::wstring& folder,const wchar_t* name){return folder+(folder.empty()||folder.back()==L'\\'?L"":L"\\")+name;}
inline std::wstring Full(const std::wstring& path){wchar_t full[32768];const DWORD n=GetFullPathNameW(path.c_str(),32768,full,nullptr);return n&&n<32768?full:L"";}
inline std::wstring Parent(const std::wstring& path){const auto pos=path.find_last_of(L"\\/");return pos==std::wstring::npos?L"":path.substr(0,pos);}
inline bool Read(const std::wstring& path,std::string& bytes,std::wstring& error){
    LauncherSettings::Detail::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
    LARGE_INTEGER size{};DWORD count=0;
    if(!file||!GetFileSizeEx(file.value,&size)||size.QuadPart<0||size.QuadPart>65536){error=L"Cannot read renderer ownership receipt.";return false;}
    bytes.resize(static_cast<size_t>(size.QuadPart));
    if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)||count!=bytes.size()){error=L"Incomplete renderer ownership receipt.";return false;}
    return true;
}
}}
