#pragma once

#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace NativeTowerEnvironment {
constexpr unsigned Edge=128;
constexpr std::array<unsigned,8> MipBytes{8192,2048,512,128,32,8,8,8};
constexpr unsigned FaceBytes=10936;
using Images=std::array<std::array<unsigned char,FaceBytes>,6>;
// The lobby asset stores the four walls first, followed by ceiling and
// floor. D3D places the vertical faces between the X and Z pairs.
inline constexpr D3DCUBEMAP_FACES LobbyFaces[]{
    D3DCUBEMAP_FACE_POSITIVE_X,D3DCUBEMAP_FACE_NEGATIVE_X,
    D3DCUBEMAP_FACE_NEGATIVE_Z,D3DCUBEMAP_FACE_POSITIVE_Z,
    D3DCUBEMAP_FACE_POSITIVE_Y,D3DCUBEMAP_FACE_NEGATIVE_Y,
};

inline bool Extract(const std::vector<unsigned char>& archiveData,Images& images){
    constexpr char name[]="Locations/_JPN/Effects/Reflection_EnvironmentMap_Cubic_Lobby_01_JPN";
    constexpr std::uint64_t fingerprints[]{0x21d5252b69e3b413ULL,0xb17eede8ec184a99ULL,0xf6f917fde35a7ffbULL,
        0x4037f278e30b8e81ULL,0xc5f9d623865e3e02ULL,0xa940b914966496d2ULL};
    auto next=archiveData.begin();
    for(unsigned face=0;face<images.size();++face){
        next=std::search(next,archiveData.end(),name,std::end(name));
        if(next==archiveData.end())return false;
        next+=sizeof(name);
        unsigned offset=0;
        for(const auto bytes:MipBytes){
            if(archiveData.end()-next<static_cast<std::ptrdiff_t>(sizeof(bytes)+bytes))return false;
            std::uint32_t size=0;std::memcpy(&size,&*next,sizeof(size));next+=sizeof(size);
            if(size!=bytes)return false;
            std::copy_n(next,bytes,images[face].begin()+offset);next+=bytes;offset+=bytes;
        }
        std::uint64_t hash=14695981039346656037ULL;
        for(auto i=FaceBytes-24;i<FaceBytes;++i){hash^=images[face][i];hash*=1099511628211ULL;}
        if(hash!=fingerprints[face])return false;
    }
    return true;
}

struct Handle {
    HANDLE value=nullptr;
    Handle()=default;
    explicit Handle(HANDLE handle):value(handle){}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    ~Handle(){Close();}
    void Close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}
};
inline bool ReadInstalledArchive(const std::wstring& archive,Images& output){
    wchar_t windows[MAX_PATH]{};
    if(!GetSystemWindowsDirectoryW(windows,MAX_PATH)||GetFileAttributesW(archive.c_str())==INVALID_FILE_ATTRIBUTES)return false;
    BOOL wow64=FALSE;
    IsWow64Process(GetCurrentProcess(),&wow64);
    const std::wstring tar=std::wstring(windows)+(wow64?L"\\Sysnative\\tar.exe":L"\\System32\\tar.exe");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
    Handle read,write;
    if(!CreatePipe(&read.value,&write.value,&attributes,0)||!SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0))return false;
    Handle null(CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,nullptr));
    if(null.value==INVALID_HANDLE_VALUE)return false;
    SIZE_T bytes=0;
    InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    if(!bytes)return false;
    std::vector<unsigned char> memory(bytes);
    auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(memory.data());
    if(!InitializeProcThreadAttributeList(list,1,0,&bytes))return false;
    HANDLE handles[]{write.value,null.value};
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb=sizeof(startup);
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=startup.StartupInfo.hStdError=null.value;
    startup.StartupInfo.hStdOutput=write.value;
    startup.lpAttributeList=list;
    PROCESS_INFORMATION child{};
    std::wstring command=L"\""+tar+L"\" -xOf \""+archive+L"\" Scenes/Locations/M09_A/M09A_Main.TEX";
    const bool launched=UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)&&
        CreateProcessW(tar.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,
            nullptr,nullptr,&startup.StartupInfo,&child);
    DeleteProcThreadAttributeList(list);
    if(!launched)return false;
    Handle process(child.hProcess),thread(child.hThread);
    write.Close();
    // The six authenticated images are near the start of the texture entry.
    // Bound the input, close the pipe at the end of the required range and
    // leave the original archive untouched.
    constexpr unsigned ProbeBytes=11*1024*1024;
    std::vector<unsigned char> data(ProbeBytes);
    DWORD got=0;unsigned used=0;
    while(used<data.size()&&ReadFile(read.value,data.data()+used,static_cast<DWORD>(data.size()-used),&got,nullptr)&&got)used+=got;
    read.Close();
    data.resize(used);
    return Extract(data,output);
}

enum class State {Idle,Loading,Ready,Unavailable};
inline std::atomic<State> state{State::Idle};
inline Images loaded{};
inline DWORD WINAPI Load(void*){
    try {
        wchar_t image[32768]{};
        const auto length=GetModuleFileNameW(nullptr,image,std::size(image));
        if(length&&length<std::size(image)){
            std::wstring path(image,length);
            const auto slash=path.find_last_of(L"\\/");
            if(slash!=std::wstring::npos&&ReadInstalledArchive(path.substr(0,slash)+L"\\Scenes\\Locations\\M09_A\\M09A_Main.zip",loaded)){
                state.store(State::Ready,std::memory_order_release);return 0;
            }
        }
    }catch(...){}
    state.store(State::Unavailable,std::memory_order_release);return 0;
}
inline void Start(){
    auto expected=State::Idle;
    if(!state.compare_exchange_strong(expected,State::Loading))return;
    const Handle worker(CreateThread(nullptr,0,Load,nullptr,0,nullptr));
    if(!worker.value)state.store(State::Unavailable,std::memory_order_release);
}

inline IDirect3DCubeTexture9* Upload(IDirect3DDevice9* device,const Images& images){
    struct Cube {
        IDirect3DCubeTexture9* value=nullptr;
        ~Cube(){if(value)value->Release();}
    } source,target;
    if(FAILED(device->CreateCubeTexture(Edge,MipBytes.size(),0,D3DFMT_DXT1,D3DPOOL_SYSTEMMEM,&source.value,nullptr))||!source.value||
       FAILED(device->CreateCubeTexture(Edge,MipBytes.size(),0,D3DFMT_DXT1,D3DPOOL_DEFAULT,&target.value,nullptr))||!target.value)return nullptr;
    for(unsigned face=0;face<images.size();++face){
        unsigned offset=0;
        const auto side=LobbyFaces[face];
        for(unsigned mip=0;mip<MipBytes.size();++mip){
            D3DLOCKED_RECT lock{};
            if(FAILED(source.value->LockRect(side,mip,&lock,nullptr,0)))return nullptr;
            const auto blocks=std::max(1u,(Edge>>mip)/4);
            const auto rowBytes=blocks*8;
            const bool valid=lock.pBits&&lock.Pitch>=static_cast<INT>(rowBytes);
            if(valid)for(unsigned row=0;row<blocks;++row)
                std::memcpy(static_cast<unsigned char*>(lock.pBits)+row*lock.Pitch,
                    images[face].data()+offset+row*rowBytes,rowBytes);
            if(FAILED(source.value->UnlockRect(side,mip))||!valid)return nullptr;
            offset+=MipBytes[mip];
        }
    }
    if(FAILED(device->UpdateTexture(source.value,target.value)))return nullptr;
    auto* cube=target.value;target.value=nullptr;return cube;
}
}
