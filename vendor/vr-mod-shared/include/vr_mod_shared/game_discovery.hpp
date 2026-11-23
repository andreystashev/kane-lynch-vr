#pragma once
#include "connection.hpp"
#include <algorithm>
#include <utility>

// Read the installed Steam game location. No drive letters, game-directory
// guesses or registry writes. The launch preflight still verifies the EXE hash.
namespace VrModGameDiscovery {
struct Profile {std::wstring appId,executable;};
namespace Detail {
using VrModConnection::Detail::Join;
using VrModConnection::Detail::Full;
inline bool Same(const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
struct Node {bool object=false;std::wstring text;std::vector<std::pair<std::wstring,Node>> children;};
struct Vdf {
    const std::wstring& text;std::size_t at=0,entries=0;
    void Space(){for(;;){while(at<text.size()&&std::iswspace(text[at]))++at;
        if(at+1<text.size()&&text[at]==L'/'&&text[at+1]==L'/'){at=text.find(L'\n',at+2);if(at==std::wstring::npos)at=text.size();}else break;}}
    bool Token(std::wstring& value){Space();if(at==text.size()||text[at]==L'{'||text[at]==L'}')return false;
        if(text[at]==L'"'){++at;while(at<text.size()){const auto c=text[at++];if(c==L'"')return true;if(c<L' ')return false;
                if(c==L'\\'&&at<text.size()&&(text[at]==L'\\'||text[at]==L'"'))value+=text[at++];else value+=c;}return false;}
        const auto begin=at;while(at<text.size()&&!std::iswspace(text[at])&&text[at]!=L'{'&&text[at]!=L'}')++at;value=text.substr(begin,at-begin);return !value.empty();}
    bool Object(Node& output,unsigned depth=0,bool enclosed=false){
        if(depth>32)return false;output.object=true;
        for(;;){Space();if(at==text.size())return !enclosed;if(text[at]==L'}'){if(!enclosed)return false;++at;return true;}
            if(++entries>20000)return false;std::wstring key;Node value;if(!Token(key))return false;Space();
            if(at<text.size()&&text[at]==L'{'){++at;if(!Object(value,depth+1,true))return false;}
            else if(!Token(value.text))return false;output.children.emplace_back(std::move(key),std::move(value));}
    }
};
inline bool Parse(const std::wstring& text,Node& root){if(text.size()>1024*1024||text.find(L'\0')!=std::wstring::npos)return false;Vdf parser{text};return parser.Object(root);}
inline const Node* Child(const Node& parent,const wchar_t* name){const Node* result=nullptr;for(const auto& entry:parent.children)if(Same(entry.first,name)){if(result)return nullptr;result=&entry.second;}return result;}
inline bool Read(const std::wstring& path,Node& root){std::string bytes;std::wstring error,text;return VrModConnection::Detail::Read(path,bytes,error)&&VrModConnection::Detail::Decode(bytes,text)&&Parse(text,root);}
inline bool Digits(const std::wstring& value){return !value.empty()&&std::all_of(value.begin(),value.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';});}
inline std::vector<std::wstring> Libraries(const Node& root){
    std::vector<std::wstring> values;const auto* libraries=Child(root,L"libraryfolders");if(!libraries||!libraries->object)return values;
    for(const auto& entry:libraries->children)if(Digits(entry.first)){
        const auto* path=entry.second.object?Child(entry.second,L"path"):&entry.second;
        if(path&&!path->object&&VrModConnection::AbsolutePath(path->text))values.push_back(path->text);
    }
    return values;
}
inline bool SingleFolder(const std::wstring& name){return !name.empty()&&name!=L"."&&name!=L".."&&name.find_first_of(L"\\/:*?\"<>|") ==std::wstring::npos&&std::none_of(name.begin(),name.end(),[](wchar_t c){return c<32;})&&name.back()!=L' '&&name.back()!=L'.';}
inline std::wstring InstallFolder(const Node& root,const Profile& profile){const auto* state=Child(root,L"AppState");if(!state||!state->object)return {};
    const auto* app=Child(*state,L"appid");const auto* folder=Child(*state,L"installdir");
    return app&&!app->object&&app->text==profile.appId&&folder&&!folder->object&&SingleFolder(folder->text)?folder->text:std::wstring{};
}
inline bool File(const std::wstring& path){const auto a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
inline std::wstring Candidate(const std::wstring& folder,const Profile& profile){if(!VrModConnection::AbsolutePath(folder))return {};const auto exe=Full(Join(folder,profile.executable));return File(exe)?exe:std::wstring{};}
inline void Unique(std::vector<std::wstring>& values,const std::wstring& candidate){if(!VrModConnection::AbsolutePath(candidate))return;const auto path=Full(candidate);if(path.empty())return;for(const auto& item:values)if(Same(item,path))return;values.push_back(path);}
}
struct Sources {std::vector<std::wstring> installedFolders,steamRoots;};
inline std::wstring Discover(const Profile& profile,const Sources& sources){
    if(!Detail::Digits(profile.appId)||!Detail::SingleFolder(profile.executable))return {};
    for(const auto& folder:sources.installedFolders){const auto found=Detail::Candidate(folder,profile);if(!found.empty())return found;}
    std::vector<std::wstring> libraries;
    for(const auto& root:sources.steamRoots){
        if(!VrModConnection::AbsolutePath(root))continue;Detail::Unique(libraries,root);Detail::Node document;
        if(Detail::Read(Detail::Join(root,L"steamapps\\libraryfolders.vdf"),document))for(const auto& path:Detail::Libraries(document))Detail::Unique(libraries,path);
    }
    for(const auto& library:libraries){Detail::Node app;
        if(!Detail::Read(Detail::Join(library,L"steamapps\\appmanifest_"+profile.appId+L".acf"),app))continue;
        const auto folder=Detail::InstallFolder(app,profile);if(folder.empty())continue;
        const auto found=Detail::Candidate(Detail::Join(Detail::Join(library,L"steamapps\\common"),folder),profile);if(!found.empty())return found;
    }return {};
}
inline std::wstring Discover(const Profile& profile){
    if(!Detail::Digits(profile.appId)||!Detail::SingleFolder(profile.executable))return {};
    Sources sources;using VrModConnection::Detail::Registry;
    for(auto root:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(auto view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}){
        const auto installed=Registry(root,(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App "+profile.appId).c_str(),L"InstallLocation",view);
        if(!installed.empty())sources.installedFolders.push_back(installed);
        for(const auto* name:{L"SteamPath",L"InstallPath"}){const auto steam=Registry(root,L"SOFTWARE\\Valve\\Steam",name,view);if(!steam.empty())sources.steamRoots.push_back(steam);}
    }
    // Standard Steam folders are candidates only when their real manifests
    // describe an installed game; they never become an invented game path.
    for(const auto* variable:{L"ProgramFiles(x86)",L"ProgramW6432",L"ProgramFiles"}){
        const auto folder=VrModConnection::Detail::Environment(variable);if(!folder.empty())sources.steamRoots.push_back(Detail::Join(folder,L"Steam"));
    }
    return Discover(profile,sources);
}
}
