#pragma once
#include "diagnostic_session.hpp"
#include <cstring>
#include <cstddef>
#include <new>

// Fileless, process-qualified routing state for the optional video recorder.
namespace AudioEndpointIPC {
constexpr DWORD Magic=0x41454c4b,Version=1;
struct State {
    volatile LONG sequence=0;
    DWORD magic=Magic,version=Version,bytes=88,pid=0,reserved=0;
    std::uint64_t creation=0;
    DWORD installed=0,redirected=0;
    LONG result=0;
    DWORD padding=0;
    char endpoint[40]{};
};
static_assert(sizeof(State)==88&&offsetof(State,endpoint)==48,"Recorder routing ABI");
inline std::wstring Name(DWORD pid){return L"Local\\KaneLynchVR-AudioEndpoint-"+std::to_wstring(pid);}
class Writer {
    HANDLE mapping_=nullptr;
    State* state_=nullptr;
public:
    ~Writer(){if(state_)UnmapViewOfFile(state_);if(mapping_)CloseHandle(mapping_);}
    bool Open(){
        if(state_)return true;
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(State),Name(GetCurrentProcessId()).c_str());
        if(!mapping_)return false;
        if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mapping_);mapping_=nullptr;return false;}
        state_=static_cast<State*>(MapViewOfFile(mapping_,FILE_MAP_WRITE,0,0,sizeof(State)));
        if(!state_){CloseHandle(mapping_);mapping_=nullptr;return false;}
        new(state_) State{};state_->pid=GetCurrentProcessId();state_->creation=DiagnosticsSession::Creation(GetCurrentProcess());return true;
    }
    void Publish(bool installed,unsigned redirected,LONG result,const char* endpoint){
        if(!state_)return;
        InterlockedIncrement(&state_->sequence);
        state_->installed=installed?1:0;state_->redirected=redirected;state_->result=result;
        std::memset(state_->endpoint,0,sizeof(state_->endpoint));
        if(endpoint)std::strncpy(state_->endpoint,endpoint,sizeof(state_->endpoint)-1);
        InterlockedIncrement(&state_->sequence);
    }
};
}
