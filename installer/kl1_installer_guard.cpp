#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <shlwapi.h>
#include "profile.hpp"

// Installer preflight only. Never injects, changes input, or closes a process.
static int VerifyGame(const wchar_t* path) {
    // Inspect the original file without loading it or invoking the Steam adapter.
    // This branch only reads the EXE; installation/configuration has separate modes.
    Kl1ExecutableIdentity identity{};
    if(!InspectExecutable(path,identity)) {
        std::fputs("Could not read the game executable.\n",stderr);
        return 5;
    }
    const bool supported=identity.supported;
    const char* build=!supported?"incompatible executable":identity.metadataVariant?"compatible metadata variant":identity.largeAddressAware?"verified legacy/LAA executable":"verified Steam-layout executable";
    std::printf("%s: %s\nSHA256: %s\n",supported?"Supported":"Unsupported",build,identity.sha256);
    if(identity.metadataVariant)
        std::puts("Warning: this exact SHA256 is new. Complete normalized code, data, imports, resources and layout match; only audited timestamp/checksum/LAA metadata differs.");
    else if(!supported)std::puts("Qualification: executable contents or layout are incompatible.");
    return !supported?3:identity.metadataVariant?10:0;
}
static std::wstring Full(const std::wstring& path) {
    wchar_t output[32768]{};
    const DWORD size=GetFullPathNameW(path.c_str(),32768,output,nullptr);
    if(!size||size>=32768)return {};
    std::wstring value(output,size);
    for(auto& ch:value)if(ch==L'/')ch=L'\\';
    while(value.size()>3&&value.back()==L'\\')value.pop_back();
    return value;
}
static bool Under(const std::wstring& path,const std::wstring& folder) {
    return path.size()>folder.size()&&_wcsnicmp(path.c_str(),folder.c_str(),folder.size())==0&&path[folder.size()]==L'\\';
}
static bool Relevant(const wchar_t* name) {
    for(const wchar_t* candidate:{L"KANEANDLYNCH.EXE",L"LaunchVR.exe",L"KaneLynchVRHost.exe",L"KaneLynchVRMonitor.exe",L"KaneLynchOpenXRHost.exe",L"KaneLynchVRSettings.exe",L"KaneLynchVRSetupGuard.exe"})
        if(_wcsicmp(name,candidate)==0)return true;
    return false;
}
static int Check(const wchar_t* requested) {
    const std::wstring input=requested;
    if(input.size()<3||input.find_first_of(L"\r\n\"")!=std::wstring::npos||
       !((input[1]==L':'&&(input[2]==L'\\'||input[2]==L'/'))||input.substr(0,2)==L"\\\\"))return 5;
    const auto folder=Full(input);if(folder.empty())return 5;
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return 5;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);int result=0;
    if(!Process32FirstW(snapshot,&entry)){CloseHandle(snapshot);return 5;}
    do {
        if(entry.th32ProcessID==GetCurrentProcessId()||!Relevant(entry.szExeFile))continue;
        const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);
        if(!process){if(GetLastError()!=ERROR_INVALID_PARAMETER)result=5;continue;}
        wchar_t image[32768]{};DWORD length=32768;
        if(!QueryFullProcessImageNameW(process,0,image,&length)) {
            if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0)result=5;
            CloseHandle(process);continue;
        }
        const auto path=Full(image);
        if(path.empty()){CloseHandle(process);result=5;continue;}
        if(Under(path,folder)){CloseHandle(process);result=4;break;}
        if(_wcsicmp(entry.szExeFile,L"KANEANDLYNCH.EXE")==0) {
            HANDLE modules=INVALID_HANDLE_VALUE;
            for(int attempt=0;attempt<8;++attempt) {
                modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,entry.th32ProcessID);
                if(modules!=INVALID_HANDLE_VALUE||GetLastError()!=ERROR_BAD_LENGTH)break;
                Sleep(1);
            }
            if(modules==INVALID_HANDLE_VALUE){if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0)result=5;}
            else {
                MODULEENTRY32W module{};module.dwSize=sizeof(module);
                if(!Module32FirstW(modules,&module)){if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0)result=5;}
                else {
                    do {
                        const auto modulePath=Full(module.szExePath);
                        if(modulePath.empty()){result=5;continue;}
                        if(Under(modulePath,folder)){result=4;break;}
                    }while(Module32NextW(modules,&module));
                    if(result!=4&&GetLastError()!=ERROR_NO_MORE_FILES&&WaitForSingleObject(process,0)!=WAIT_OBJECT_0)result=5;
                }
                CloseHandle(modules);
            }
        }
        CloseHandle(process);if(result==4)break;
    }while(Process32NextW(snapshot,&entry));
    if(result!=4&&GetLastError()!=ERROR_NO_MORE_FILES)result=5;
    CloseHandle(snapshot);return result;
}
struct IniPatch {const wchar_t *section,*key,*value;bool onlyIfMissing=false;};
static int PatchConfig(const wchar_t* folder,const wchar_t* file,std::initializer_list<IniPatch> changes) {
    const auto app=Full(folder);if(app.empty())return 5;
    const auto attributes=GetFileAttributesW(app.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return 5;
    const auto config=app+L"\\"+file;std::wstring text;
    HANDLE input=CreateFileW(config.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(input!=INVALID_HANDLE_VALUE){
        LARGE_INTEGER length{};
        if(!GetFileSizeEx(input,&length)||length.QuadPart<0||length.QuadPart>4194304){CloseHandle(input);return 5;}
        const DWORD size=static_cast<DWORD>(length.QuadPart);std::vector<char> bytes(size);DWORD read=0;
        const bool ok=ReadFile(input,bytes.data(),size,&read,nullptr)&&read==size;CloseHandle(input);if(!ok)return 5;
        if(size>=2&&static_cast<unsigned char>(bytes[0])==255&&static_cast<unsigned char>(bytes[1])==254){
            if(size&1)return 5;text.assign(reinterpret_cast<const wchar_t*>(bytes.data()+2),(size-2)/2);
        }else if(size){
            const int offset=size>=3&&static_cast<unsigned char>(bytes[0])==239&&static_cast<unsigned char>(bytes[1])==187&&static_cast<unsigned char>(bytes[2])==191?3:0;
            int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+offset,size-offset,nullptr,0);
            const UINT encoding=length?CP_UTF8:CP_ACP;
            if(!length)length=MultiByteToWideChar(encoding,0,bytes.data()+offset,size-offset,nullptr,0);
            if(!length)return 5;text.resize(length);MultiByteToWideChar(encoding,0,bytes.data()+offset,size-offset,text.data(),length);
        }
    }else if(GetLastError()!=ERROR_FILE_NOT_FOUND)return 5;
    const auto stage=config+L".setup-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    HANDLE output=CreateFileW(stage.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(output==INVALID_HANDLE_VALUE)return 5;
    const wchar_t bom=0xfeff;DWORD written=0;
    bool ok=WriteFile(output,&bom,2,&written,nullptr)&&written==2;
    ok=ok&&WriteFile(output,text.data(),DWORD(text.size()*2),&written,nullptr)&&written==text.size()*2;
    ok=ok&&FlushFileBuffers(output);CloseHandle(output);
    for(const auto& change:changes){
        if(!ok)break;
        if(change.onlyIfMissing){
            // A missing key follows both supplied defaults. An explicit
            // empty value or even a value matching one default stays intact.
            wchar_t current[2]{};
            GetPrivateProfileStringW(change.section,change.key,L"0",current,2,stage.c_str());
            if(current[0]!=L'0')continue;
            GetPrivateProfileStringW(change.section,change.key,L"1",current,2,stage.c_str());
            if(current[0]!=L'1')continue;
        }
        ok=WritePrivateProfileStringW(change.section,change.key,change.value,stage.c_str())!=FALSE;
    }
    if(ok)ok=MoveFileExW(stage.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(stage.c_str());return ok?0:5;
}
static int SaveSelection(const wchar_t* folder,const wchar_t* game,bool automatic) {
    const auto app=Full(folder),target=Full(game);if(app.empty()||target.empty()||
        GetFileAttributesW(target.c_str())==INVALID_FILE_ATTRIBUTES||_wcsicmp(PathFindFileNameW(target.c_str()),L"KANEANDLYNCH.EXE"))return 5;
    Kl1ExecutableIdentity identity;if(!InspectExecutable(target.c_str(),identity)||!identity.supported)return 3;
    const auto parent=Full(target.substr(0,target.find_last_of(L'\\')));
    if(_wcsicmp(app.c_str(),parent.c_str())==0||Under(app,parent)||Under(parent,app))return 5;
    std::wstring selected;
    if(!automatic){
        wchar_t relative[MAX_PATH]{};
        selected=PathRelativePathToW(relative,app.c_str(),FILE_ATTRIBUTE_DIRECTORY,target.c_str(),FILE_ATTRIBUTE_NORMAL)?relative:target;
        if(selected.size()>2&&selected.substr(0,2)==L".\\")selected.erase(0,2);
    }
    return PatchConfig(folder,L"launcher.ini",{{L"Launcher",L"GameExe",selected.c_str()}});
}
static int ReleaseProfile(const wchar_t* folder) {
    // The public installation always exits developer/testing mode. Sub-probe
    // flags remain meaningful so the one Diagnostics checkbox can enable them.
    return PatchConfig(folder,L"vr.ini",{
        {L"Diagnostics",L"Enabled",L"0"},
        {L"Debug",L"Invulnerable",L"0"},{L"Debug",L"TestingModifiers",L"0"},
        {L"Recording",L"Enabled",L"0"},
        {L"Weapons",L"NativeReticle",L"0",true},{L"Weapons",L"LaserSight",L"1",true},
        {L"Weapons",L"MountedReticle",L"1",true},{L"Weapons",L"ReticleStyle",L"1",true},
        {L"Weapons",L"PhysicalReload",L"1",true},
        {L"VR",L"CaptureWithoutHost",L"0"},{L"VR",L"ForcePanel",L"0"},
        {L"VR",L"AutoStart",L"1"},{L"VR",L"DXVKGPUTransport",L"1",true},
        {L"VR",L"GPUSharedD3D9",L"1",true}});
}
int wmain(int count,wchar_t** arguments) {
    if(count==3&&std::wcscmp(arguments[1],L"--verify-game")==0)return VerifyGame(arguments[2]);
    if(count==3&&std::wcscmp(arguments[1],L"--check-install-path")==0)return Check(arguments[2]);
    if(count==4&&std::wcscmp(arguments[1],L"--save-game-selection")==0)return SaveSelection(arguments[2],arguments[3],false);
    if(count==4&&std::wcscmp(arguments[1],L"--save-auto-selection")==0)return SaveSelection(arguments[2],arguments[3],true);
    if(count==3&&std::wcscmp(arguments[1],L"--apply-release-profile")==0)return ReleaseProfile(arguments[2]);
    // Isolated smoke fixtures use an event instead of terminating a test process.
    if(count==3&&std::wcscmp(arguments[1],L"--hold-test-event")==0) {
        const HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,arguments[2]);if(!event)return 5;
        const DWORD result=WaitForSingleObject(event,300000);CloseHandle(event);
        return result==WAIT_OBJECT_0?0:5;
    }
    return 5;
}
