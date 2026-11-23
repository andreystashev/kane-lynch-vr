#pragma once
#include "diagnostic_session.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include "profile.hpp"
#include "vr_mod_shared/ini.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <limits>
#include <cmath>
#include <string>

// Adapted from the released KL2 status recorder. Only the diagnostics
// worker calls this after atomic status publication, never a render callback.
namespace PerformanceLog {
// Simultaneous bounded frame, wait, material and per-eye traces can exceed
// 256 KiB. Keep a hard ceiling while retaining the full diagnostic snapshot.
inline constexpr std::uint64_t MaxSnapshotBytes=4*1024*1024;
inline std::string Quote(const std::string& text){
    std::string out="\"";constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}
    return out+'"';
}
inline std::string Utf8(const std::wstring& text){
    const int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};std::string result(n,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),result.data(),n,nullptr,nullptr);return result;
}
inline std::string Compact(const std::string& text){
    std::string out;bool string=false,escape=false;
    for(char c:text){
        if(string){out+=c;if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')string=false;}
        else if(c=='"'){string=true;out+=c;}else if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n')out+=c;
    }return out;
}
// Validate a complete bounded object before embedding it in JSONL. In addition
// to rejecting torn writes, only top-level identity fields may match a PID.
class Json {
    const std::string& s;std::size_t at=0;std::string wanted;
    bool found=false,duplicate=false,readBoolean=false,boolean=false;std::uint64_t integer=0;
    void Space(){while(at<s.size()&&(s[at]==' '||s[at]=='\n'||s[at]=='\r'||s[at]=='\t'))++at;}
    bool Take(char c){Space();if(at==s.size()||s[at]!=c)return false;++at;return true;}
    bool String(std::string* value=nullptr){
        if(!Take('"'))return false;
        while(at<s.size()){
            unsigned char c=s[at++];if(c=='"')return true;if(c<32)return false;
            if(c=='\\'){
                if(at==s.size())return false;c=s[at++];
                if(c=='u'){for(unsigned i=0;i<4;++i){if(at==s.size())return false;
                    const char h=s[at++];if(!((h>='0'&&h<='9')||(h>='a'&&h<='f')||(h>='A'&&h<='F')))return false;}c='?';}
                else if(c!='"'&&c!='\\'&&c!='/'&&c!='b'&&c!='f'&&c!='n'&&c!='r'&&c!='t')return false;
            }
            if(value)*value+=char(c);
        }return false;
    }
    bool Number(){
        if(at<s.size()&&s[at]=='-')++at;
        if(at==s.size())return false;
        if(s[at]=='0')++at;else {if(s[at]<'1'||s[at]>'9')return false;while(at<s.size()&&s[at]>='0'&&s[at]<='9')++at;}
        if(at<s.size()&&s[at]=='.'){++at;const auto start=at;while(at<s.size()&&s[at]>='0'&&s[at]<='9')++at;if(start==at)return false;}
        if(at<s.size()&&(s[at]=='e'||s[at]=='E')){++at;if(at<s.size()&&(s[at]=='+'||s[at]=='-'))++at;
            const auto start=at;while(at<s.size()&&s[at]>='0'&&s[at]<='9')++at;if(start==at)return false;}
        return true;
    }
    bool Value(unsigned depth){
        Space();if(at==s.size()||depth>24)return false;
        if(s[at]=='{')return Object(depth);
        if(s[at]=='['){++at;Space();if(Take(']'))return true;do{if(!Value(depth+1))return false;Space();if(Take(']'))return true;}while(Take(','));return false;}
        if(s[at]=='"')return String();
        for(const char* word:{"true","false","null"}){const std::size_t n=std::strlen(word);if(s.compare(at,n,word)==0){at+=n;return true;}}
        return Number();
    }
    bool Object(unsigned depth){
        if(!Take('{'))return false;Space();if(Take('}'))return true;
        do{
            std::string key;if(!String(&key)||!Take(':'))return false;Space();const auto first=at;
            if(!Value(depth+1))return false;
            if(!depth&&key==wanted){
                if(found)duplicate=true;found=true;integer=0;
                if(first==at)return false;
                if(readBoolean){
                    if(s.compare(first,at-first,"true")==0)boolean=true;
                    else if(s.compare(first,at-first,"false")==0)boolean=false;else return false;
                }else for(std::size_t i=first;i<at;++i){const char c=s[i];if(c<'0'||c>'9'||integer>(std::numeric_limits<std::uint64_t>::max()-unsigned(c-'0'))/10)return false;integer=integer*10+unsigned(c-'0');}
            }
            Space();if(Take('}'))return true;
        }while(Take(','));return false;
    }
public:
    explicit Json(const std::string& bytes):s(bytes){}
    bool Unsigned(const char* key,std::uint64_t& value){
        at=0;found=duplicate=false;readBoolean=false;wanted=key;
        if(!Object(0))return false;Space();if(at!=s.size()||!found||duplicate)return false;value=integer;return true;
    }
    bool Boolean(const char* key,bool& value){
        at=0;found=duplicate=false;readBoolean=true;wanted=key;
        if(!Object(0))return false;Space();if(at!=s.size()||!found||duplicate)return false;value=boolean;return true;
    }
};
inline std::wstring Join(const std::wstring& directory,const std::wstring& name){
    if(directory.empty()||directory.size()+name.size()+2>32700)return {};
    return directory+(directory.back()==L'\\'||directory.back()==L'/'?L"":L"\\")+name;
}
inline std::uint64_t FileTicks(FILETIME time){return (std::uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;}
struct SnapshotRecord {
    std::string json="null",reason="missing";
    FILETIME written{};
    std::uint64_t fileAgeMs=0,tickAgeMs=0;
    bool fileAgeKnown=false,tickAgeKnown=false;
    bool Valid()const{return json!="null";}
};
inline SnapshotRecord ReadSnapshot(const std::wstring& path,const char* pidKey,DWORD pid,DWORD now,bool checkTick){
    SnapshotRecord result;
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return result;
    LARGE_INTEGER size{};FILETIME stamp{},clock{};GetSystemTimeAsFileTime(&clock);
    bool ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&std::uint64_t(size.QuadPart)<=MaxSnapshotBytes&&GetFileTime(file,nullptr,nullptr,&stamp);
    if(ok&&FileTicks(clock)>=FileTicks(stamp)){result.fileAgeKnown=true;result.fileAgeMs=(FileTicks(clock)-FileTicks(stamp))/10000;}
    std::string bytes(ok?std::size_t(size.QuadPart):0,'\0');DWORD got=0;
    ok=ok&&ReadFile(file,bytes.data(),DWORD(bytes.size()),&got,nullptr)&&got==bytes.size();
    LARGE_INTEGER afterSize{};FILETIME afterStamp{};
    ok=ok&&GetFileSizeEx(file,&afterSize)&&afterSize.QuadPart==size.QuadPart&&GetFileTime(file,nullptr,nullptr,&afterStamp)&&FileTicks(afterStamp)==FileTicks(stamp);
    CloseHandle(file);result.written=stamp;
    std::uint64_t identity=0,tick=0;
    result.reason="invalid_or_changing_file";
    if(!ok||!Json(bytes).Unsigned(pidKey,identity))return result;
    if(identity!=pid||!pid){result.reason="foreign_pid";return result;}
    if(checkTick){
        if(!Json(bytes).Unsigned("updatedTick",tick)||tick>MAXDWORD){result.reason="invalid_tick";return result;}
        result.tickAgeKnown=true;result.tickAgeMs=DWORD(now-DWORD(tick));
        if(result.tickAgeMs>3000){result.reason="stale_tick";return result;}
    }
    if(!result.fileAgeKnown||result.fileAgeMs>3000){result.reason="stale_file";return result;}
    result.json=Compact(bytes);result.reason="fresh";return result;
}
inline std::string Snapshot(const std::wstring& path,const char* pidKey,DWORD pid,DWORD now,bool checkTick,FILETIME* written=nullptr){
    const auto result=ReadSnapshot(path,pidKey,pid,now,checkTick);
    if(written)*written=result.written;return result.json;
}
inline bool SameExecutable(const wchar_t* actual,const std::wstring& expected){
    if(!_wcsicmp(actual,expected.c_str()))return true;
    const auto info=[](const wchar_t* path,BY_HANDLE_FILE_INFORMATION& output){
        const HANDLE file=CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        const bool okay=GetFileInformationByHandle(file,&output)!=0;
        CloseHandle(file);return okay;
    };
    BY_HANDLE_FILE_INFORMATION a{},b{};
    return info(actual,a)&&info(expected.c_str(),b)&&a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&
        (a.nFileIndexLow||a.nFileIndexHigh)&&a.nFileIndexLow==b.nFileIndexLow&&a.nFileIndexHigh==b.nFileIndexHigh;
}
inline bool ProcessMatches(DWORD pid,FILETIME written,const std::wstring& expected){
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return false;
    FILETIME created{},exit{},kernel{},user{};wchar_t path[32768]{};DWORD length=32768;
    const bool valid=WaitForSingleObject(process,0)==WAIT_TIMEOUT&&GetProcessTimes(process,&created,&exit,&kernel,&user)&&
        FileTicks(created)<=FileTicks(written)&&QueryFullProcessImageNameW(process,0,path,&length)&&SameExecutable(path,expected);
    CloseHandle(process);return valid;
}
inline SnapshotRecord ReadHostSnapshot(const std::wstring& directory,DWORD gamePid,DWORD now){
    auto result=ReadSnapshot(Join(directory,L"KaneLynchVRHost-status.json"),"gamePid",gamePid,now,false);
    std::uint64_t hostPid=0;
    if(result.Valid()&&(!Json(result.json).Unsigned("hostPid",hostPid)||!hostPid||hostPid>MAXDWORD||
       (!ProcessMatches(DWORD(hostPid),result.written,Join(directory,L"KaneLynchVRHost.exe"))&&
        !ProcessMatches(DWORD(hostPid),result.written,Join(directory,L"KaneLynchOpenXRHost.exe"))))){
        result.json="null";result.reason="host_process_identity";
    }
    return result;
}
inline std::string HostSnapshot(const std::wstring& directory,DWORD gamePid,DWORD now){return ReadHostSnapshot(directory,gamePid,now).json;}
inline std::string Utc(SYSTEMTIME time){char text[48]{};std::snprintf(text,sizeof(text),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);return text;}
inline std::string Number(double value){if(!std::isfinite(value))return "null";char out[64]{};std::snprintf(out,sizeof(out),"%.3f",value);return out;}
struct Rate {
    bool valid=false;double hz=0;std::uint64_t windowMs=0;
    std::string Json()const{return valid?Number(hz):"null";}
};
struct CounterRate {
    bool valid=false;std::uint64_t count=0,tick=0,identity=0;
    void Reset(){*this={};}
    Rate Sample(std::uint64_t next,std::uint64_t now,std::uint64_t source=1){
        Rate result;
        if(valid&&identity==source&&now>tick&&next>=count){result={true,double(next-count)*1000.0/double(now-tick),now-tick};}
        valid=true;count=next;tick=now;identity=source;return result;
    }
    Rate JsonSample(const std::string& json,const char* key,std::uint64_t now,std::uint64_t source=1){
        std::uint64_t next=0;if(json=="null"||!PerformanceLog::Json(json).Unsigned(key,next)){Reset();return {};}
        return Sample(next,now,source);
    }
};
struct ProcessSample {
    bool valid=false,memoryValid=false,cpuValid=false,ioValid=false;
    DWORD pid=0,processors=1;
    std::uint64_t creation=0,cpuTicks=0,workingSet=0,privateBytes=0;
    std::uint64_t pageFaultCount=0,ioReadBytes=0,ioWriteBytes=0,ioReadOperations=0,ioWriteOperations=0;
    double cpuPercent=0,cpuCorePercent=0;
    std::string Json()const{
        if(!valid)return "null";
        return "{\"pid\":"+std::to_string(pid)+",\"creationFileTime\":"+std::to_string(creation)+
            ",\"cpuPercent\":"+(cpuValid?Number(cpuPercent):"null")+",\"cpuCorePercent\":"+(cpuValid?Number(cpuCorePercent):"null")+
            ",\"logicalProcessors\":"+std::to_string(processors)+",\"workingSetBytes\":"+(memoryValid?std::to_string(workingSet):"null")+
            ",\"privateBytes\":"+(memoryValid?std::to_string(privateBytes):"null")+
            ",\"pageFaultCount\":"+(memoryValid?std::to_string(pageFaultCount):"null")+
            ",\"ioReadBytes\":"+(ioValid?std::to_string(ioReadBytes):"null")+",\"ioWriteBytes\":"+(ioValid?std::to_string(ioWriteBytes):"null")+
            ",\"ioReadOperations\":"+(ioValid?std::to_string(ioReadOperations):"null")+",\"ioWriteOperations\":"+(ioValid?std::to_string(ioWriteOperations):"null")+"}";
    }
};
struct ProcessSampler {
    DWORD pid=0;std::uint64_t creation=0,cpu=0,tick=0;bool previous=false;
    void Reset(){*this={};}
    ProcessSample Sample(DWORD target,std::uint64_t now){
        ProcessSample result;result.pid=target;
        HANDLE process=target?OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,target):nullptr;
        if(!process&&target)process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,target);
        if(!process){Reset();return result;}
        FILETIME born{},exited{},kernel{},user{};
        if(WaitForSingleObject(process,0)!=WAIT_TIMEOUT||!GetProcessTimes(process,&born,&exited,&kernel,&user)){CloseHandle(process);Reset();return result;}
        result.valid=true;result.creation=FileTicks(born);result.cpuTicks=FileTicks(kernel)+FileTicks(user);
        SYSTEM_INFO system{};GetSystemInfo(&system);result.processors=std::max<DWORD>(1,system.dwNumberOfProcessors);
        using MemoryInfo=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
        static const auto memoryInfo=reinterpret_cast<MemoryInfo>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        if(memoryInfo&&memoryInfo(process,reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory),sizeof(memory))){
            result.memoryValid=true;result.workingSet=memory.WorkingSetSize;result.privateBytes=memory.PrivateUsage;result.pageFaultCount=memory.PageFaultCount;
        }
        IO_COUNTERS io{};
        if(GetProcessIoCounters(process,&io)){
            result.ioValid=true;result.ioReadBytes=io.ReadTransferCount;result.ioWriteBytes=io.WriteTransferCount;
            result.ioReadOperations=io.ReadOperationCount;result.ioWriteOperations=io.WriteOperationCount;
        }
        CloseHandle(process);
        if(previous&&pid==target&&creation==result.creation&&now>tick&&result.cpuTicks>=cpu){
            result.cpuValid=true;result.cpuCorePercent=double(result.cpuTicks-cpu)/double(now-tick)/100.0;
            result.cpuPercent=result.cpuCorePercent/result.processors;
        }
        previous=true;pid=target;creation=result.creation;cpu=result.cpuTicks;tick=now;return result;
    }
};
class Recorder {
    HANDLE file_=INVALID_HANDLE_VALUE;
    std::wstring path_;
    std::string session_;
    std::uint64_t bytes_=0,limit_=16ull*1024*1024,started_=0,last_=0,sequence_=0,acceptedGameSamples_=0;
    unsigned parts_=4;
    bool failed_=false;
    CounterRate sourceRate_,sceneRate_,hostSubmitRate_,hostPairRate_;
    ProcessSampler gameProcess_,hostProcess_;
    void CloseFile(const char* reason){
        if(file_==INVALID_HANDLE_VALUE)return;
        SYSTEMTIME time{};GetSystemTime(&time);
        const std::string end="{\"event\":\"end\",\"reason\":"+Quote(std::string(reason?reason:"closed").substr(0,48))+
            ",\"utc\":"+Quote(Utc(time))+",\"sequence\":"+std::to_string(sequence_)+"}\n";DWORD written=0;
        WriteFile(file_,end.data(),DWORD(end.size()),&written,nullptr);CloseHandle(file_);file_=INVALID_HANDLE_VALUE;
    }
    bool Raw(const std::string& text){
        DWORD written=0;if(file_==INVALID_HANDLE_VALUE||!WriteFile(file_,text.data(),DWORD(text.size()),&written,nullptr)||written!=text.size()){
            CloseFile("write_error");failed_=true;return false;
        }bytes_+=written;return true;
    }
    bool Part(){
        file_=CreateFileW(path_.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        bytes_=0;return file_!=INVALID_HANDLE_VALUE&&Raw(session_);
    }
    bool Rotate(){
        CloseFile("rotation");
        // All targets derive from this recorder's CREATE_NEW session stem.
        // Only its own bounded archive files can be replaced; no old session
        // discovery, recursive moves/deletes or render-thread filesystem work.
        for(unsigned i=parts_-1;i>0;--i){const auto from=i==1?path_:path_+L"."+std::to_wstring(i-1);const auto to=path_+L"."+std::to_wstring(i);
            if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING)&&GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
        }
        if(parts_==1){ // Keep one bounded file by replacing only our own stem.
            file_=CreateFileW(path_.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            bytes_=0;return file_!=INVALID_HANDLE_VALUE&&Raw(session_);
        }
        return Part();
    }
    bool Open(const std::wstring& directory,DWORD pid,const char* version,std::uint64_t now){
        const auto logs=Join(directory,L"logs");if(!CreateDirectoryW(logs.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        SYSTEMTIME time{};GetSystemTime(&time);wchar_t name[180]{};
        FILETIME born{},exited{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&born,&exited,&kernel,&user);
        const auto creation=FileTicks(born);
        for(unsigned i=0;i<32;++i){std::swprintf(name,180,L"performance-%04u%02u%02uT%02u%02u%02u-%03u-%lu-%llu-%u.jsonl",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,pid,static_cast<unsigned long long>(creation),i);
            path_=Join(logs,name);file_=CreateFileW(path_.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file_!=INVALID_HANDLE_VALUE)break;if(GetLastError()!=ERROR_FILE_EXISTS)return false;
        }
        if(file_==INVALID_HANDLE_VALUE)return false;
        wchar_t exe[32768]{},module[32768]{};char exeHash[65]{},moduleHash[65]{};
        GetModuleFileNameW(nullptr,exe,32768);HashFile(exe,exeHash);
        if(const auto d3d=GetModuleHandleW(L"d3d9.dll")){GetModuleFileNameW(d3d,module,32768);HashFile(module,moduleHash);}
        VrModIni::File settings;std::wstring settingsError;
        const auto configured=VrModIni::Load(Join(directory,L"vr.ini"),settings,settingsError,true)
            ?VrModIni::Read(settings,L"Launcher",L"Renderer",L"Original"):L"Unreadable";
        started_=now;last_=0;sequence_=0;bytes_=0;
        session_="{\"event\":\"session\",\"formatVersion\":1,\"version\":"+Quote(version)+",\"utc\":"+Quote(Utc(time))+",\"gamePid\":"+std::to_string(pid)+
            ",\"processCreationFileTime\":"+std::to_string(creation)+
            ",\"gameExeSha256\":"+Quote(exeHash)+",\"d3d9Module\":"+Quote(Utf8(module))+",\"d3d9ModuleSha256\":"+Quote(moduleHash)+
            ",\"configuredRenderer\":"+Quote(Utf8(configured))+",\"sampling\":\"one-second status windows; source frames and stereo pairs differ; CPU scopes overlap; cpuPercent is normalized across logical processors\"}\n";
        return Raw(session_);
    }
public:
    Recorder()=default;Recorder(const Recorder&)=delete;Recorder& operator=(const Recorder&)=delete;
    ~Recorder(){Close("worker_exit");}
    void Close(const char* reason="worker_exit"){
        CloseFile(reason);last_=0;sourceRate_.Reset();sceneRate_.Reset();hostSubmitRate_.Reset();hostPairRate_.Reset();gameProcess_.Reset();hostProcess_.Reset();
    }
    const std::wstring& Path()const{return path_;}
    // Monotonic over this Recorder's lifetime, including close/reopen. The
    // producer can retire trace cursors only after a *valid* game snapshot
    // has actually been appended to JSONL; skipped/failed/null samples do not
    // grant that acknowledgement. Recorder is worker-thread-owned.
    std::uint64_t AcceptedGameSamples()const{return acceptedGameSamples_;}
    bool Failed()const{return failed_;}
    bool Active()const{return file_!=INVALID_HANDLE_VALUE;}
    // Worker-only export of a completed bounded shot record, once per serial.
    bool Event(const std::string& json){
        std::uint64_t serial=0;
        if(!Active()||json.size()>65536||!Json(json).Unsigned("serial",serial)||!serial)return false;
        const auto line=Compact(json)+"\n";
        if(bytes_+line.size()+512>limit_&&!Rotate()){Close("rotation_error");failed_=true;return false;}
        return Raw(line);
    }
    void Tick(const std::wstring& directory,DWORD pid,const char* version,std::uint64_t now=GetTickCount64(),bool finalSample=false){
        const auto ini=Join(directory,L"vr.ini");
        if(!DiagnosticsSession::Enabled(directory)||!GetPrivateProfileIntW(L"Diagnostics",L"PerformanceLogging",1,ini.c_str())){Close("disabled");failed_=false;return;}
        if(failed_|| (!finalSample&&file_!=INVALID_HANDLE_VALUE&&now>=last_&&now-last_<1000))return;
        limit_=std::clamp(GetPrivateProfileIntW(L"Diagnostics",L"PerformanceMaxMegabytes",16,ini.c_str()),1u,64u)*1024ull*1024;
        const auto parts=std::clamp(GetPrivateProfileIntW(L"Diagnostics",L"PerformanceSegments",4,ini.c_str()),1u,32u);
        if(parts<parts_&&!path_.empty())for(unsigned i=parts;i<parts_;++i)DeleteFileW((path_+L"."+std::to_wstring(i)).c_str());
        parts_=parts;
        if(file_==INVALID_HANDLE_VALUE&&!Open(directory,pid,version,now)){failed_=true;return;}
        auto game=ReadSnapshot(Join(directory,L"KaneLynchVR-status.json"),"pid",pid,DWORD(now),true);
        const auto host=ReadHostSnapshot(directory,pid,DWORD(now));SYSTEMTIME time{};GetSystemTime(&time);
        const auto gameProcess=gameProcess_.Sample(pid,now);std::uint64_t hostPid=0;
        if(game.Valid()&&(!gameProcess.valid||gameProcess.creation>FileTicks(game.written))){game.json="null";game.reason="game_process_identity";}
        if(host.Valid())Json(host.json).Unsigned("hostPid",hostPid);
        const auto hostProcess=hostProcess_.Sample(DWORD(hostPid),now);
        const auto source=sourceRate_.JsonSample(game.json,"presents",now,gameProcess.creation);
        const auto scene=sceneRate_.JsonSample(game.json,"sceneFrames",now,gameProcess.creation);
        const auto hostIdentity=hostProcess.valid?hostProcess.creation:hostPid;
        // The host writes on its own cadence. Two reads may see one report
        // twice or skip a report, so game sampling time distorts host rates.
        const auto hostSampleMs=FileTicks(host.written)/10000;
        const auto hostSubmit=hostSubmitRate_.JsonSample(host.json,"framesSubmitted",hostSampleMs,hostIdentity);
        const auto hostPair=hostPairRate_.JsonSample(host.json,"newScenePairs",hostSampleMs,hostIdentity);
        const auto hostWindowMs=hostSubmit.valid?hostSubmit.windowMs:hostPair.windowMs;
        bool hostReady=false;const bool readyKnown=host.Valid()&&Json(host.json).Boolean("hostReady",hostReady);
        const auto age=[](const SnapshotRecord& value){return value.tickAgeKnown?std::to_string(value.tickAgeMs):value.fileAgeKnown?std::to_string(value.fileAgeMs):"null";};
        const auto nextSequence=sequence_+1;
        const auto line="{\"event\":\"sample\",\"final\":"+(finalSample?std::string("true"):std::string("false"))+",\"utc\":"+Quote(Utc(time))+",\"sequence\":"+std::to_string(nextSequence)+",\"elapsedMs\":"+std::to_string(now>=started_?now-started_:0)+
            ",\"game\":"+game.json+",\"host\":"+host.json+
            ",\"freshness\":{\"gameValid\":"+(game.Valid()?"true":"false")+",\"hostValid\":"+(host.Valid()?"true":"false")+
            ",\"gameAgeMs\":"+age(game)+",\"hostAgeMs\":"+age(host)+",\"gameReason\":"+Quote(game.reason)+",\"hostReason\":"+Quote(host.reason)+
            ",\"hostReady\":"+(readyKnown?(hostReady?"true":"false"):"null")+"},\"rates\":{\"sourceHz\":"+source.Json()+",\"sceneEyeHz\":"+scene.Json()+
            ",\"scenePairHz\":"+(scene.valid?Number(scene.hz*.5):"null")+",\"scenePairEstimated\":true,\"hostSubmitHz\":"+hostSubmit.Json()+
            ",\"hostScenePairHz\":"+hostPair.Json()+",\"windowMs\":"+(source.valid?std::to_string(source.windowMs):"null")+
            ",\"hostWindowMs\":"+(hostWindowMs?std::to_string(hostWindowMs):"null")+
            "},\"process\":{\"game\":"+gameProcess.Json()+",\"host\":"+hostProcess.Json()+"}}\n";
        if(bytes_+line.size()+512>limit_&&!Rotate()){Close("rotation_error");failed_=true;return;}
        if(Raw(line)){if(game.Valid())++acceptedGameSamples_;sequence_=nextSequence;last_=now;}
    }
    // Worker shutdown writes a fresh status after the last periodic Tick.
    // Force that final snapshot even within the normal one-second interval;
    // disabled logging and identity/freshness checks are still respected.
    void FlushFinal(const std::wstring& directory,DWORD pid,const char* version,std::uint64_t now=GetTickCount64()){
        Tick(directory,pid,version,now,true);
    }
};
}
