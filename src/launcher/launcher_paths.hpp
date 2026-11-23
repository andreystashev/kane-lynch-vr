#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "release_version.hpp"
#include "vr_mod_shared/game_discovery.hpp"
#include "vr_mod_shared/ini.hpp"

// Every package path is rooted at the executable, never the shell's working
// directory. A game path is a local preference, not part of a distributed INI.
namespace Kl1LauncherPaths {
inline std::wstring Join(const std::wstring& folder,const std::wstring& name) {
    return VrModConnection::Detail::Join(folder,name);
}
inline std::wstring Folder() {
    wchar_t path[32768]{};const DWORD size=GetModuleFileNameW(nullptr,path,32768);
    return size&&size<32768?VrModConnection::Detail::Parent(std::wstring(path,size)):std::wstring{};
}
inline std::wstring ResolveFile(const std::wstring& folder,std::wstring path) {
    if(!VrModConnection::AbsolutePath(folder))return {};
    path=VrModIni::Trim(path);if(path.empty())return {};
    if(path.find_first_of(L"\"*?<>|")!=std::wstring::npos||std::any_of(path.begin(),path.end(),[](wchar_t c){return c<32;}))return {};
    for(auto& c:path)if(c==L'/')c=L'\\';
    // Drive-relative/root-relative Windows paths depend on external process
    // state. Accept a complete absolute path or an ordinary relative one.
    if(!VrModConnection::AbsolutePath(path)) {
        if(path.front()==L'\\'||path.find(L':')!=std::wstring::npos)return {};
        path=Join(folder,path);
    }
    const auto colon=path.find(L':');
    if(colon!=std::wstring::npos&&(colon!=1||path.find(L':',colon+1)!=std::wstring::npos))return {};
    path=VrModConnection::Detail::Full(path);
    return path;
}
inline std::wstring SettingsInstanceId(const std::wstring& folder) {
    // Installed and portable copies must never activate each other's settings
    // window. Hash a canonical, invariant-case path without exposing it in the
    // window class/mutex name. This is an instance key, not a security hash.
    auto path=ResolveFile(folder,L".");if(path.empty())return {};
    while(path.size()>3&&path.back()==L'\\')path.pop_back();
    const int size=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr,0);
    if(!size)return {};
    std::wstring folded(static_cast<size_t>(size),L'\0');
    if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,path.data(),static_cast<int>(path.size()),folded.data(),size,nullptr,nullptr,0)!=size)return {};
    unsigned long long hash=14695981039346656037ull;
    for(const auto unit:folded){hash^=static_cast<unsigned char>(unit&255);hash*=1099511628211ull;hash^=static_cast<unsigned char>((unit>>8)&255);hash*=1099511628211ull;}
    wchar_t suffix[17]{};for(unsigned i=0;i<16;++i)suffix[15-i]=L"0123456789abcdef"[(hash>>(i*4))&15];
    std::wstring version=KL1_RELEASE_VERSION_W;
    version.erase(std::remove(version.begin(),version.end(),L'.'),version.end());
    return L"KaneLynchVRLauncher-"+version+L"-"+suffix;
}
inline std::wstring Resolve(const std::wstring& folder,std::wstring path) {
    path=ResolveFile(folder,std::move(path));if(path.empty())return {};
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY))path=Join(path,L"KANEANDLYNCH.EXE");
    return path;
}
inline std::wstring Discover() {return VrModGameDiscovery::Discover({L"8080",L"KANEANDLYNCH.EXE"});}
inline std::wstring Discover(const VrModGameDiscovery::Sources& sources) {return VrModGameDiscovery::Discover({L"8080",L"KANEANDLYNCH.EXE"},sources);}
inline std::wstring Default(const std::wstring& folder,const std::wstring& saved,const std::wstring& steam,const std::wstring& legacy) {
    // Preserve a deliberate installed override. Steam is the first automatic
    // choice, ahead of adjacent or legacy installs; stale paths cannot mask it.
    const auto selected=Resolve(folder,saved);
    if(!selected.empty()&&VrModGameDiscovery::Detail::File(selected))return selected;
    const auto installedSteam=Resolve(folder,steam);
    if(!installedSteam.empty()&&VrModGameDiscovery::Detail::File(installedSteam))return installedSteam;
    for(const auto& path:{Join(folder,L"KANEANDLYNCH.EXE"),Join(folder,L"..\\KANEANDLYNCH.EXE")})
        if(VrModGameDiscovery::Detail::File(path))return Resolve(folder,path);
    const auto previous=Resolve(folder,legacy);
    if(!previous.empty()&&VrModGameDiscovery::Detail::File(previous))return previous;
    return !selected.empty()?selected:previous;
}
inline std::wstring Default(const std::wstring& folder) {
    wchar_t saved[32768]{},legacy[32768]{};
    GetPrivateProfileStringW(L"Launcher",L"GameExe",L"",saved,32768,Join(folder,L"launcher.ini").c_str());
    GetPrivateProfileStringW(L"Launcher",L"GamePath",L"",legacy,32768,Join(folder,L"vr.ini").c_str());
    return Default(folder,saved,Discover(),legacy);
}
inline std::wstring Root(const std::wstring& path) {
    if(path.size()>2&&path[1]==L':'&&path[2]==L'\\')return path.substr(0,3);
    if(path.rfind(L"\\\\",0)==0){const auto server=path.find(L'\\',2);if(server!=std::wstring::npos){const auto share=path.find(L'\\',server+1);return share==std::wstring::npos?path+L"\\":path.substr(0,share+1);}}
    return {};
}
inline std::vector<std::wstring> Parts(const std::wstring& path,size_t begin){
    std::vector<std::wstring> result;
    while(begin<path.size()){const auto end=path.find(L'\\',begin);if(end!=begin)result.push_back(path.substr(begin,end==std::wstring::npos?end:end-begin));if(end==std::wstring::npos)break;begin=end+1;}return result;
}
inline std::wstring PortableFile(const std::wstring& folder,const std::wstring& file) {
    const auto full=ResolveFile(folder,file),base=ResolveFile(folder,L".");
    if(full.empty()||base.empty())return {};
    const auto root=Root(full),baseRoot=Root(base);
    // A cross-volume override cannot be expressed as a Windows relative path;
    // it is a local preference only, never part of a distributed configuration.
    if(root.empty()||!VrModIni::Same(root,baseRoot))return full;
    const auto target=Parts(full,root.size()),origin=Parts(base,baseRoot.size());
    size_t same=0;while(same<target.size()&&same<origin.size()&&VrModIni::Same(target[same],origin[same]))++same;
    std::wstring result;for(size_t i=same;i<origin.size();++i)result+=L"..\\";
    for(size_t i=same;i<target.size();++i){if(!result.empty()&&result.back()!=L'\\')result+=L'\\';result+=target[i];}
    return result.empty()?L".":result;
}
inline std::wstring Portable(const std::wstring& folder,const std::wstring& game,const std::wstring& steam) {
    const auto full=Resolve(folder,game);if(full.empty())return {};
    // Auto-discovered Steam locations are re-discovered on the destination PC.
    if(!steam.empty()&&VrModIni::Same(full,Resolve(folder,steam)))return {};
    return PortableFile(folder,full);
}
}
