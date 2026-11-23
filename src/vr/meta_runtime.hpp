#pragma once
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

// One discovery/load policy for x86 camera/Touch and the x64 compositor.
// Read the installed application's locations; never change registry or search
// the game's/current directory for an arbitrary LibOVR replacement.
namespace MetaRuntime {
inline std::wstring Environment(const wchar_t* name){
    wchar_t value[32768]{};const auto n=GetEnvironmentVariableW(name,value,32768);
    return n&&n<32768?value:L"";
}
inline std::wstring Registry(const wchar_t* key,const wchar_t* name,REGSAM view){
    HKEY handle=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,key,0,KEY_QUERY_VALUE|view,&handle)!=ERROR_SUCCESS)return {};
    wchar_t value[32768]{};DWORD bytes=sizeof(value);
    const auto result=RegGetValueW(handle,nullptr,name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,nullptr,value,&bytes);
    RegCloseKey(handle);return result==ERROR_SUCCESS?value:L"";
}
inline std::wstring Normalize(std::wstring path){
    const auto first=path.find_first_not_of(L" \t\r\n\"");
    if(first==std::wstring::npos)return {};
    path=path.substr(first,path.find_last_not_of(L" \t\r\n\"")-first+1);
    wchar_t expanded[32768]{};
    const auto count=ExpandEnvironmentStringsW(path.c_str(),expanded,32768);
    if(!count||count>32768)return {};
    path=expanded;std::replace(path.begin(),path.end(),L'/',L'\\');
    // Relative installation values must not turn into current-directory loads.
    if(!((path.size()>2&&path[1]==L':'&&path[2]==L'\\')||path.rfind(L"\\\\",0)==0))return {};
    while(path.size()>3&&path.back()==L'\\')path.pop_back();
    return path;
}
inline void Add(std::vector<std::wstring>& directories,const std::wstring& value,bool installRoot){
    auto path=Normalize(value);if(path.empty())return;
    if(installRoot)path+=L"\\Support\\oculus-runtime";
    for(const auto& prior:directories)if(!_wcsicmp(prior.c_str(),path.c_str()))return;
    directories.push_back(path);
}
inline std::wstring ServiceDirectory(std::wstring command){
    const auto start=command.find_first_not_of(L" \t");if(start==std::wstring::npos)return {};
    command.erase(0,start);
    if(command.front()==L'"'){
        const auto end=command.find(L'"',1);if(end==std::wstring::npos)return {};
        command=command.substr(1,end-1);
    }else{
        auto lower=command;std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return std::towlower(c);});
        const auto end=lower.find(L".exe");if(end==std::wstring::npos)return {};
        command.resize(end+4);
    }
    command=Normalize(command);const auto slash=command.find_last_of(L'\\');
    return slash==std::wstring::npos?L"":command.substr(0,slash);
}
template<class Env,class Reg>
std::vector<std::wstring> Discover(Env env,Reg reg){
    std::vector<std::wstring> result;
    Add(result,env(L"OculusBase"),true);
    for(const auto view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY})
        Add(result,reg(L"SOFTWARE\\Oculus VR, LLC\\Oculus",L"Base",view),true);
    Add(result,ServiceDirectory(reg(L"SYSTEM\\CurrentControlSet\\Services\\OVRService",L"ImagePath",KEY_WOW64_64KEY)),false);
    Add(result,env(L"ProgramW6432")+L"\\Oculus",true);
    Add(result,env(L"ProgramFiles")+L"\\Oculus",true);
    return result;
}
inline std::vector<std::wstring> Directories(){return Discover(Environment,Registry);}
inline const wchar_t* Filename(bool x64){return x64?L"LibOVRRT64_1.dll":L"LibOVRRT32_1.dll";}
inline bool Exists(const std::wstring& path){
    const auto attrs=GetFileAttributesW(path.c_str());return attrs!=INVALID_FILE_ATTRIBUTES&&!(attrs&FILE_ATTRIBUTE_DIRECTORY);
}
inline bool Installed(bool x64=false){
    for(const auto& directory:Directories())if(Exists(directory+L"\\"+Filename(x64)))return true;
    return false;
}
inline std::string Utf8(const std::wstring& text){
    const auto size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(!size)return {};
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,result.data(),size,nullptr,nullptr);
    result.pop_back();return result;
}
struct Result {HMODULE module=nullptr;DWORD error=ERROR_MOD_NOT_FOUND;bool fileFound=false;std::wstring path;std::string message;};
template<class FileExists,class Loader,class Error>
Result TryLoad(const std::vector<std::wstring>& directories,bool x64,FileExists exists,Loader load,Error lastError){
    Result result;std::string attempted;
    for(const auto& directory:directories){
        const auto path=directory+L"\\"+Filename(x64);
        const bool found=exists(path);
        if(!attempted.empty())attempted+="; ";attempted+=Utf8(path);
        if(!found){attempted+=" [missing]";continue;}
        const auto module=load(path);
        if(module){result.module=module;result.error=0;result.fileFound=true;result.path=path;
            result.message="Loaded installed Meta "+std::string(x64?"x64":"x86")+" runtime: "+Utf8(path);return result;}
        const auto error=lastError();attempted+=" [Win32 "+std::to_string(error)+"]";
        if(!result.fileFound){result.fileFound=true;result.error=error;result.path=path;}
    }
    result.message="Cannot load installed Meta "+std::string(x64?"x64":"x86")+" runtime. ";
    result.message+=result.fileFound?"DLL exists but Windows could not load it or a dependency. ":"Runtime DLL was not found in the discovered installation locations. ";
    result.message+="Win32 error "+std::to_string(result.error)+". Tried: "+attempted;
    return result;
}
inline Result Load(){
    return TryLoad(Directories(),sizeof(void*)==8,Exists,[](const std::wstring& path){
        return LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    },[]{return GetLastError();});
}
}
