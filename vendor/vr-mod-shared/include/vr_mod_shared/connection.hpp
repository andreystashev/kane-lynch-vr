#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

// Runtime manifest validation/discovery follows Blood Money VR's connection
// policy. It reads installed configuration; it never sets the global runtime.
namespace VrModConnection {
enum class Mode {Auto,Meta,VDXR,SteamVR,OpenXR,Invalid};
inline constexpr std::array<Mode,5> Modes{Mode::Auto,Mode::Meta,Mode::VDXR,Mode::SteamVR,Mode::OpenXR};
inline const wchar_t* Name(Mode mode){switch(mode){case Mode::Auto:return L"Auto";case Mode::Meta:return L"Meta";case Mode::VDXR:return L"VDXR";case Mode::SteamVR:return L"SteamVR";case Mode::OpenXR:return L"OpenXR";default:return L"Invalid";}}
inline const wchar_t* Label(Mode mode){switch(mode){case Mode::Auto:return L"Automatic";case Mode::Meta:return L"Quest Link / Air Link (Meta)";case Mode::VDXR:return L"Virtual Desktop (VDXR)";case Mode::SteamVR:return L"SteamVR / Steam Link (OpenXR)";case Mode::OpenXR:return L"OpenXR (active runtime / custom JSON)";default:return L"Invalid";}}
inline Mode Parse(const std::wstring& name){if(!_wcsicmp(name.c_str(),L"meta-link"))return Mode::Meta;for(auto mode:Modes)if(!_wcsicmp(name.c_str(),Name(mode)))return mode;return Mode::Invalid;}
inline bool IsOpenXR(Mode mode){return mode==Mode::VDXR||mode==Mode::SteamVR||mode==Mode::OpenXR;}
inline bool AbsolutePath(const std::wstring& path){
    if(path.empty()||path.size()>=32760)return false;for(auto c:path)if(c<32||c==L'"'||c==L'*'||c==L'?'||c==L'|')return false;
    if(path.size()>=3&&((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&path[1]==L':'&&(path[2]==L'\\'||path[2]==L'/'))return true;
    if(path.size()>5&&path[0]==L'\\'&&path[1]==L'\\'&&path[2]!=L'\\'&&path[2]!=L'?'&&path[2]!=L'.'){
        const auto end=path.find_first_of(L"\\/",2);return end!=std::wstring::npos&&end+1<path.size()&&path[end+1]!=L'\\'&&path[end+1]!=L'/';}return false;
}
namespace Detail {
struct Handle {HANDLE value;explicit Handle(HANDLE h):value(h){}~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}explicit operator bool()const{return value!=INVALID_HANDLE_VALUE&&value;}};
inline bool Fail(std::wstring& error,const wchar_t* message){error=message;return false;}
inline std::wstring Join(const std::wstring& folder,const std::wstring& name){return folder+(folder.empty()||folder.back()==L'\\'||folder.back()==L'/'?L"":L"\\")+name;}
inline std::wstring Parent(const std::wstring& path){const auto at=path.find_last_of(L"\\/");return at==std::wstring::npos?L"":path.substr(0,at);}
inline std::wstring Full(const std::wstring& path){wchar_t value[32768]{};const DWORD count=GetFullPathNameW(path.c_str(),32768,value,nullptr);return count&&count<32768?std::wstring(value,count):std::wstring{};}
inline std::wstring Lower(std::wstring value){for(auto& c:value)c=std::towlower(c);return value;}
inline bool Read(const std::wstring& path,std::string& bytes,std::wstring& error){Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));LARGE_INTEGER size{};
    if(!file||!GetFileSizeEx(file.value,&size)||size.QuadPart<0||size.QuadPart>1024*1024)return Fail(error,L"Could not read the runtime manifest.");bytes.resize(static_cast<std::size_t>(size.QuadPart));DWORD count=0;return bytes.empty()||(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size());}
inline bool Decode(const std::string& bytes,std::wstring& text){text.clear();if(bytes.size()>=2&&bytes.compare(0,2,"\xff\xfe")==0){if(bytes.size()%2)return false;text.resize((bytes.size()-2)/2);if(!text.empty())std::memcpy(text.data(),bytes.data()+2,bytes.size()-2);}
    else{const auto start=bytes.compare(0,3,"\xef\xbb\xbf")==0?3u:0u;const int size=static_cast<int>(bytes.size()-start);if(!size)return true;const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+start,size,nullptr,0);if(!count)return false;text.resize(count);if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+start,size,text.data(),count)!=count)return false;}
    return text.find(L'\0')==std::wstring::npos;}
struct Json {
    const std::wstring& text;std::size_t at=0;std::wstring library;bool runtimeSeen=false,librarySeen=false;
    explicit Json(const std::wstring& value):text(value){}
    void Space(){while(at<text.size()&&(text[at]==L' '||text[at]==L'\t'||text[at]==L'\r'||text[at]==L'\n'))++at;}
    bool String(std::wstring& out){
        if(at==text.size()||text[at++]!=L'"')return false;
        while(at<text.size()){
            wchar_t c=text[at++];if(c==L'"')return true;if(c<32)return false;
            if(c==L'\\'){
                if(at==text.size())return false;c=text[at++];
                if(c==L'u'){
                    unsigned value=0;for(unsigned i=0;i<4;++i){if(at==text.size())return false;const auto h=text[at++];
                        const unsigned digit=h>=L'0'&&h<=L'9'?unsigned(h-L'0'):h>=L'a'&&h<=L'f'?unsigned(h-L'a'+10):h>=L'A'&&h<=L'F'?unsigned(h-L'A'+10):16;
                        if(digit==16)return false;value=value*16+digit;}c=wchar_t(value);
                }else if(c==L'b')c=L'\b';else if(c==L'f')c=L'\f';else if(c==L'n')c=L'\n';else if(c==L'r')c=L'\r';else if(c==L't')c=L'\t';
                else if(c!=L'"'&&c!=L'\\'&&c!=L'/')return false;
            }
            out+=c;
        }return false;
    }
    bool Value(unsigned depth,const std::wstring& context=L""){
        if(depth>32)return false;Space();if(at==text.size())return false;
        if(text[at]==L'{'){
            ++at;Space();if(at<text.size()&&text[at]==L'}'){++at;return true;}
            for(;;){std::wstring key;Space();if(!String(key))return false;Space();if(at==text.size()||text[at++]!=L':')return false;Space();
                if(context==L"root"&&key==L"runtime"){
                    if(runtimeSeen||at==text.size()||text[at]!=L'{')return false;runtimeSeen=true;if(!Value(depth+1,L"runtime"))return false;
                }else if(context==L"runtime"&&key==L"library_path"){
                    if(librarySeen||!String(library))return false;librarySeen=true;
                }else if(!Value(depth+1))return false;
                Space();if(at==text.size())return false;const auto c=text[at++];if(c==L'}')return true;if(c!=L',')return false;
            }
        }
        if(text[at]==L'['){++at;Space();if(at<text.size()&&text[at]==L']'){++at;return true;}for(;;){if(!Value(depth+1))return false;Space();if(at==text.size())return false;const auto c=text[at++];if(c==L']')return true;if(c!=L',')return false;}}
        if(text[at]==L'"'){std::wstring ignored;return String(ignored);}
        for(const auto* literal:{L"true",L"false",L"null"}){const auto count=std::wcslen(literal);if(text.compare(at,count,literal)==0){at+=count;return true;}}
        if(text[at]==L'-')++at;if(at==text.size())return false;
        if(text[at]==L'0')++at;else{if(text[at]<L'1'||text[at]>L'9')return false;while(at<text.size()&&text[at]>=L'0'&&text[at]<=L'9')++at;}
        if(at<text.size()&&text[at]==L'.'){++at;const auto start=at;while(at<text.size()&&text[at]>=L'0'&&text[at]<=L'9')++at;if(start==at)return false;}
        if(at<text.size()&&(text[at]==L'e'||text[at]==L'E')){++at;if(at<text.size()&&(text[at]==L'+'||text[at]==L'-'))++at;const auto start=at;while(at<text.size()&&text[at]>=L'0'&&text[at]<=L'9')++at;if(start==at)return false;}
        return true;
    }
    bool Parse(){Space();if(at==text.size()||text[at]!=L'{'||!Value(0,L"root"))return false;Space();return at==text.size()&&runtimeSeen&&librarySeen&&!library.empty();}
};

inline std::wstring Environment(const wchar_t* name){wchar_t value[32768]{};const DWORD n=GetEnvironmentVariableW(name,value,32768);return n&&n<32768?std::wstring(value,n):std::wstring{};}
inline std::wstring Registry(HKEY root,const wchar_t* key,const wchar_t* name,REGSAM view=0){
    using ReadValue=LSTATUS(WINAPI*)(HKEY,LPCWSTR,LPCWSTR,DWORD,LPDWORD,PVOID,LPDWORD);
    static HMODULE module=LoadLibraryExW(L"advapi32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto read=module?reinterpret_cast<ReadValue>(GetProcAddress(module,"RegGetValueW")):nullptr;if(!read)return {};
    wchar_t value[32768]{};DWORD bytes=sizeof(value);const auto status=read(root,key,name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ|(view==KEY_WOW64_64KEY?0x00010000u:view==KEY_WOW64_32KEY?0x00020000u:0),nullptr,value,&bytes);
    return status==ERROR_SUCCESS?std::wstring(value):std::wstring{};
}
}
inline bool ValidateRuntime(const std::wstring& requested,std::wstring& error){
    error.clear();if(!AbsolutePath(requested)||requested.size()<5||_wcsicmp(requested.substr(requested.size()-5).c_str(),L".json"))
        return Detail::Fail(error,L"Choose an absolute path to an OpenXR runtime JSON manifest.");
    std::string bytes;if(!Detail::Read(requested,bytes,error))return false;
    std::wstring text;if(!Detail::Decode(bytes,text))return Detail::Fail(error,L"Runtime manifest is not valid UTF-8 or UTF-16 text.");
    Detail::Json json{text};if(!json.Parse())return Detail::Fail(error,L"The manifest must contain valid JSON with runtime.library_path.");
    auto library=json.library;for(auto c:library)if(c<32||c==L'"'||c==L'%'||c==L'*'||c==L'?'||c==L'|')return Detail::Fail(error,L"The runtime library path is invalid.");
    if(!AbsolutePath(library))library=Detail::Join(Detail::Parent(requested),library);library=Detail::Full(library);
    Detail::Handle file(CreateFileW(library.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!file)return Detail::Fail(error,L"The runtime library named by the manifest is missing or unreadable.");
    IMAGE_DOS_HEADER dos{};DWORD read=0;if(!ReadFile(file.value,&dos,sizeof(dos),&read,nullptr)||read!=sizeof(dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<64||dos.e_lfanew>4*1024*1024)
        return Detail::Fail(error,L"The selected runtime library is not a valid Windows PE library.");
    LARGE_INTEGER offset{};offset.QuadPart=dos.e_lfanew;IMAGE_NT_HEADERS64 nt{};
    if(!SetFilePointerEx(file.value,offset,nullptr,FILE_BEGIN)||!ReadFile(file.value,&nt,sizeof(nt),&read,nullptr)||read!=sizeof(nt)||
       nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return Detail::Fail(error,L"The VR host requires a 64-bit x64 OpenXR runtime. Choose its 64-bit manifest.");
    return true;
}
struct DiscoverySources {std::wstring activeRuntime;std::vector<std::wstring> programFolders,steamRoots;};
inline std::wstring DiscoverRuntime(Mode mode,const DiscoverySources& sources){
    std::vector<std::wstring> candidates;const auto& active=sources.activeRuntime;
    const auto lower=Detail::Lower;
    if(!active.empty()&&(mode==Mode::OpenXR||(mode==Mode::VDXR&&lower(active).find(L"virtualdesktop")!=std::wstring::npos)||(mode==Mode::SteamVR&&lower(active).find(L"steamvr")!=std::wstring::npos)))candidates.push_back(active);
    if(mode==Mode::VDXR)for(const auto& base:sources.programFolders)if(!base.empty())candidates.push_back(Detail::Join(base,L"Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr.json"));
    if(mode==Mode::SteamVR){
        auto roots=sources.steamRoots;const auto initial=roots;
        for(const auto& root:initial){
            std::string bytes;std::wstring error;if(!Detail::Read(Detail::Join(root,L"steamapps\\libraryfolders.vdf"),bytes,error))continue;
            std::wstring text;if(!Detail::Decode(bytes,text))continue;std::size_t at=0;
            while((at=text.find(L"\"path\"",at))!=std::wstring::npos){at+=6;while(at<text.size()&&(text[at]==L' '||text[at]==L'\t'||text[at]==L'\r'||text[at]==L'\n'))++at;
                Detail::Json token{text};token.at=at;std::wstring library;if(token.String(library)&&AbsolutePath(library))roots.push_back(library);at=token.at>at?token.at:at+1;
            }
        }
        for(const auto& root:roots)if(!root.empty())candidates.push_back(Detail::Join(root,L"steamapps\\common\\SteamVR\\steamxr_win64.json"));
    }
    // A blank saved manifest explicitly requests the system runtime. Ignore
    // stale XR_RUNTIME_JSON inherited from Steam, a terminal or another mod.
    for(const auto& candidate:candidates){std::wstring error;if(ValidateRuntime(candidate,error))return Detail::Full(candidate);}return {};
}
inline std::wstring DiscoverRuntime(Mode mode){
    DiscoverySources sources;sources.activeRuntime=Detail::Registry(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\OpenXR\\1",L"ActiveRuntime",KEY_WOW64_64KEY);
    if(mode==Mode::VDXR)for(const auto* variable:{L"ProgramW6432",L"ProgramFiles",L"ProgramFiles(x86)"})sources.programFolders.push_back(Detail::Environment(variable));
    if(mode==Mode::SteamVR)sources.steamRoots={Detail::Registry(HKEY_CURRENT_USER,L"SOFTWARE\\Valve\\Steam",L"SteamPath"),Detail::Registry(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Valve\\Steam",L"InstallPath",KEY_WOW64_32KEY),Detail::Join(Detail::Environment(L"ProgramFiles(x86)"),L"Steam")};
    return DiscoverRuntime(mode,sources);
}

struct Choice {Mode mode=Mode::Invalid;std::wstring runtimeJson;};
inline bool Resolve(Mode requested,const std::wstring& custom,bool metaReady,Choice& choice,std::wstring& error){
    error.clear();choice={};if(requested==Mode::Invalid)return Detail::Fail(error,L"Select an available headset connection profile.");
    if(requested==Mode::Meta||(requested==Mode::Auto&&metaReady)){choice.mode=Mode::Meta;return true;}
    choice.mode=requested==Mode::Auto?Mode::OpenXR:requested;
    if(!IsOpenXR(choice.mode))return Detail::Fail(error,L"Select an available headset connection profile.");
    if(!custom.empty()){if(!ValidateRuntime(custom,error))return false;choice.runtimeJson=Detail::Full(custom);return true;}
    choice.runtimeJson=DiscoverRuntime(choice.mode);
    if(choice.runtimeJson.empty())return Detail::Fail(error,choice.mode==Mode::VDXR?L"Virtual Desktop's 64-bit VDXR runtime was not found. Install Virtual Desktop Streamer or browse to its runtime JSON.":choice.mode==Mode::SteamVR?L"SteamVR's 64-bit OpenXR runtime was not found. Install SteamVR or browse to steamxr_win64.json.":L"No usable 64-bit OpenXR runtime is configured. Set up your headset PC runtime or select its JSON manifest.");
    return true;
}
}
