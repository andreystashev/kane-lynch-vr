#pragma once
#include "renderer_support.hpp"
#include "renderer_identity.hpp"
#include "vr_mod_shared/ini.hpp"
#include <tlhelp32.h>
#include <string_view>

// Optional game-side D3D9 translation. The 64-bit VR compositor keeps D3D11.
// A renderer can be replaced only when its bytes and the game-directory
// receipt prove that the mod owns it.
namespace GameRenderer {
using Kind=RendererIdentity::Kind;
inline constexpr const char* DllSha256=RendererIdentity::DXVKDllSha256;
inline constexpr const char* LicenseSha256="a5cb1a6ded7d2d7e92d550ba28edd21be2d1d4044662b399887351023e30ce64";
inline bool Fail(std::wstring& error,const wchar_t* message){error=message;return false;}
inline std::wstring Join(const std::wstring& folder,const wchar_t* name){return LauncherConnection::Detail::Join(folder,name);}
inline bool Exists(const std::wstring& path){return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;}
inline bool Hash(const std::wstring& path,const char* expected){char digest[65]{};return HashFile(path.c_str(),digest)&&!std::strcmp(digest,expected);}
inline bool Regular(const std::wstring& path){const auto a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
inline const wchar_t* BundleFile(Kind renderer){return renderer==Kind::D3D9On12?L"renderer-d3d9on12-x86\\d3d9.dll":L"renderer-dxvk-x86\\d3d9.dll";}
inline bool KnownOwnedDigest(std::string_view digest) {
    return digest==DllSha256 || digest==RendererIdentity::On12DllSha256 ||
           digest==RendererIdentity::On12ReleaseDllSha256 || digest==RendererIdentity::On12LegacyDllSha256 ||
           digest==RendererIdentity::On12ResearchDllSha256 || digest==RendererIdentity::On12StockDllSha256;
}

inline bool BundleDigest(const std::wstring& folder, Kind renderer, char digest[65], std::wstring& error) {
    const auto dll=Join(folder,BundleFile(renderer)),license=Join(folder,L"renderer-dxvk-x86\\LICENSE");
    if(!Regular(dll)||!HashFile(dll.c_str(),digest))
        return Fail(error,L"The selected renderer bundle is missing. Restore the complete VR mod package.");
    const std::string_view actual=digest;
    const bool approved=renderer==Kind::D3D9On12
        ? actual==RendererIdentity::On12DllSha256||actual==RendererIdentity::On12ReleaseDllSha256
        : actual==DllSha256&&Regular(license)&&Hash(license,LicenseSha256);
    if(!approved)
        return Fail(error,L"The selected renderer does not match this mod build. Restore the complete VR mod package.");
    LauncherSettings::Detail::Handle file(CreateFileW(dll.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
    IMAGE_DOS_HEADER dos{};DWORD read=0;IMAGE_NT_HEADERS32 nt{};
    if(!file||!ReadFile(file.value,&dos,sizeof(dos),&read,nullptr)||read!=sizeof(dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||
       SetFilePointer(file.value,dos.e_lfanew,nullptr,FILE_BEGIN)==INVALID_SET_FILE_POINTER||
       !ReadFile(file.value,&nt,sizeof(nt),&read,nullptr)||read!=sizeof(nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return Fail(error,L"The selected D3D9 renderer library must be a PE32 x86 image.");
    return true;
}

inline bool Bundle(const std::wstring& folder,std::wstring& error,Kind renderer=Kind::DXVK) {
    char digest[65]{};
    return BundleDigest(folder,renderer,digest,error);
}
inline bool Selection(const std::wstring& folder,Kind& renderer,std::wstring& error){
    // Use the settings window's decoder: Win32 profile APIs can miss a first
    // [Launcher] section behind a UTF-8 BOM and silently choose Original.
    error.clear();renderer=Kind::Unknown;VrModIni::File settings;
    if(!VrModIni::Load(Join(folder,L"vr.ini"),settings,error))return false;
    renderer=RendererIdentity::Parse(VrModIni::Read(settings,L"Launcher",L"Renderer",L"Original").c_str());
    return renderer!=Kind::Unknown||Fail(error,L"Unknown renderer. Select Original, DXVK or D3D9On12 in VR settings.");
}
inline bool NoGame(std::wstring& error){
    LauncherSettings::Detail::Handle processes(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(!processes)return Fail(error,L"Cannot inspect running games before changing the renderer.");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(!Process32FirstW(processes.value,&entry))return Fail(error,L"Cannot enumerate running games.");
    do{if(!_wcsicmp(entry.szExeFile,L"KANEANDLYNCH.EXE"))return Fail(error,L"Close Kane & Lynch before changing its renderer.");}while(Process32NextW(processes.value,&entry));
    return GetLastError()==ERROR_NO_MORE_FILES||Fail(error,L"Process enumeration failed before renderer switching.");
}
inline std::wstring Receipt(const std::wstring& directory,const char* gameHash) {
    auto canonical=LauncherSettings::Detail::Lower(LauncherConnection::Detail::Full(directory));
    for(auto& c:canonical)if(c==L'/')c=L'\\';
    while(canonical.size()>3&&canonical.back()==L'\\')canonical.pop_back();
    return L"KaneLynchVR-Renderer-1\r\n"+canonical+L"\r\n"+LauncherSettings::Detail::Wide(gameHash)+L"\r\nOriginalD3D9=Absent\r\n";
}

inline bool LoadReceipt(const std::wstring& path, VrModIni::File& receipt) {
    std::wstring error;
    return Regular(path)&&VrModIni::Load(path,receipt,error,true);
}

inline bool MatchRecordedDigests(std::wstring_view text, const std::wstring& identity, const char* installed, bool& matches) {
    if(text.substr(0,identity.size())!=identity)return false;
    text.remove_prefix(identity.size());
    const auto installedHash=LauncherSettings::Detail::Wide(installed);
    // A renderer change records the pending and previous digests so the
    // existing DLL remains attributable if the final replace fails.
    for(unsigned count=0;!text.empty();++count) {
        if(count==2||text.size()<66||text.substr(64,2)!=L"\r\n")return false;
        const auto digest=text.substr(0,64);
        for(wchar_t c:digest)if(!(c>=L'0'&&c<=L'9')&&!(c>=L'a'&&c<=L'f'))return false;
        matches|=digest==installedHash;
        text.remove_prefix(66);
    }
    return true;
}

inline bool SaveReceipt(const std::wstring& path,const std::wstring& temp, const std::wstring& text,bool replace) {
    VrModIni::File receipt;
    receipt.encoding=VrModIni::File::Encoding::Utf16;
    receipt.text=text;
    std::string encoded;
    if(!VrModIni::Encode(receipt,encoded))return false;
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    const bool flushed=WriteFile(file,encoded.data(),static_cast<DWORD>(encoded.size()),&written,nullptr)&&
                       written==encoded.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(flushed&&MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)))return true;
    DeleteFileW(temp.c_str());
    return false;
}

// The caller has qualified the game and checked that it is idle.
inline bool Switch(const std::wstring& package,const std::wstring& directory,const char* gameHash,Kind renderer,std::wstring& error) {
    if(renderer==Kind::Unknown)return Fail(error,L"Invalid renderer selection.");
    error.clear();
    const auto target=Join(directory,L"d3d9.dll"),receipt=Join(directory,L".kl1-vr-dxvk-owner");
    const auto digests=Join(directory,L".kl1-vr-renderer-digests");
    if(renderer==Kind::Original&&!Exists(receipt))return true;
    char selected[65]{};
    if(renderer!=Kind::Original&&!BundleDigest(package,renderer,selected,error))return false;
    LauncherSettings::Detail::Handle lock(CreateFileW(Join(directory,L".kl1-vr-renderer-lock").c_str(),
        GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr));
    if(!lock)return Fail(error,L"Cannot lock the game renderer for switching. Check game folder write access and other launchers.");

    const bool installed=Exists(target),owned=Exists(receipt);
    const auto identity=Receipt(directory,gameHash);
    char previous[65]{};
    if(installed&&(!Regular(target)||!HashFile(target.c_str(),previous)))
        return Fail(error,L"The existing d3d9.dll cannot be verified. Renderer files were preserved.");
    bool matches=KnownOwnedDigest(previous);
    VrModIni::File owner, recorded;
    const bool recordedExists=Exists(digests);
    if((owned&&(!LoadReceipt(receipt,owner)||owner.text!=identity))||
       (recordedExists&&(!owned||!LoadReceipt(digests,recorded)||recorded.text.size()<=identity.size()||
                        !MatchRecordedDigests(recorded.text,identity,previous,matches))))
        return Fail(error,L"The renderer ownership receipt belongs to another game/path or was changed. Renderer files were preserved.");
    if(installed&&(!owned||!matches))
        return Fail(error,L"An existing d3d9.dll is not owned by this VR renderer switch. Preserve/remove your ReShade, ENB or other renderer manually before enabling a renderer.");
    if(renderer==Kind::Original){
        if(installed&&!DeleteFileW(target.c_str()))
            return Fail(error,L"Cannot remove the owned renderer DLL. Close the game and check write access.");
        return true;
    }
    if(installed&&!std::strcmp(previous,selected))return true;

    const auto tempName=L".kl1-vr-d3d9-"+std::to_wstring(GetCurrentProcessId());
    const auto stage=Join(directory,(tempName+L".tmp").c_str());
    if(!CopyFileW(Join(package,BundleFile(renderer)).c_str(),stage.c_str(),TRUE))
        return Fail(error,L"Cannot stage the selected renderer beside the game. No renderer DLL was installed.");
    const auto receiptStage=Join(directory,(tempName+L".owner.tmp").c_str());
    const auto digestStage=Join(directory,(tempName+L".digests.tmp").c_str());
    auto recordedText=identity+LauncherSettings::Detail::Wide(selected)+L"\r\n";
    if(*previous)recordedText+=LauncherSettings::Detail::Wide(previous)+L"\r\n";
    // Older launchers still recognize the original, unchanged ownership marker.
    const bool ok=Hash(stage,selected)&&
                  (owned||SaveReceipt(receipt,receiptStage,identity,false))&&
                  SaveReceipt(digests,digestStage,recordedText,recordedExists)&&
                  MoveFileExW(stage.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(installed?MOVEFILE_REPLACE_EXISTING:0));
    if(!ok){
        DeleteFileW(stage.c_str());
        return Fail(error,L"Cannot verify/install the selected renderer. The existing renderer slot was preserved.");
    }
    return true;
}
inline bool Prepare(const std::wstring& package,const std::wstring& game,Kind& renderer,std::wstring& error){
    if(!Selection(package,renderer,error))return false;
    if(renderer==Kind::D3D9On12){
        const auto native=LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        const bool supported=native&&GetProcAddress(native,"Direct3DCreate9On12");if(native)FreeLibrary(native);
        if(!supported)return Fail(error,L"Windows D3D9On12 is unavailable. Select Original or DXVK on this system.");
    }
    char digest[65]{};if(!HashSupportedGameFile(game.c_str(),digest))return Fail(error,L"Renderer switch refused an incompatible game executable.");
    if(!NoGame(error))return false;
    const auto directory=LauncherConnection::Detail::Parent(LauncherConnection::Detail::Full(game));
    return !directory.empty()&&Switch(package,directory,digest,renderer,error);
}
}
