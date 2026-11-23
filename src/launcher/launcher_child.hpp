#pragma once
#include "vr_mod_shared/ini.hpp"
#include <algorithm>
#include <functional>

// Console-free injector invocation. Drain continuously, retaining the final
// error instead of only a large startup preamble. No log file is required.
namespace Kl1LauncherChild {
constexpr size_t OutputLimit=64*1024;
struct Result {bool started=false,truncated=false;DWORD exitCode=1,error=0;std::string output;};
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}void Close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}};
inline void Append(Result& result,const char* bytes,size_t count){
    if(result.output.size()+count>OutputLimit){result.truncated=true;const auto remove=result.output.size()+count-OutputLimit;
        if(remove>=result.output.size()){const auto skip=remove-result.output.size();result.output.assign(bytes+skip,count-skip);return;}
        result.output.erase(0,remove);}
    result.output.append(bytes,count);
}
inline bool Drain(HANDLE pipe,Result& result){
    bool read=false;
    for(unsigned chunk=0;chunk<16;++chunk){DWORD available=0;if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)||!available)break;
        char bytes[4096];DWORD count=0;if(!ReadFile(pipe,bytes,std::min<DWORD>(available,sizeof bytes),&count,nullptr)||!count)break;
        Append(result,bytes,count);read=true;}
    return read;
}
inline std::wstring Text(const Result& result){
    VrModIni::File decoded;decoded.bytes=result.output;if(!VrModIni::Decode(decoded))return {};
    decoded.text.erase(std::remove(decoded.text.begin(),decoded.text.end(),L'\0'),decoded.text.end());
    auto text=VrModIni::Trim(decoded.text);if(result.truncated)text=L"(Earlier output omitted.)\r\n"+text;return text;
}
inline std::wstring LatestLine(const Result& result){
    auto end=result.output.find_last_of('\n');
    while(end!=std::string::npos){
        const auto previous=end?result.output.rfind('\n',end-1):std::string::npos;
        const auto begin=previous==std::string::npos?0:previous+1;
        if(previous==std::string::npos&&result.truncated)return {};
        Result line;line.output=result.output.substr(begin,end-begin);auto text=Text(line);
        if(!text.empty())return text;
        end=previous;
    }
    return {};
}
inline bool Run(const std::wstring& executable,std::wstring command,const std::wstring& folder,Result& result,
                const std::function<void(const std::wstring&)>& progress={}){
    result={};SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};Handle input,read,write,thread,process;
    if(!CreatePipe(&read.value,&write.value,&security,0)||!SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0)){result.error=GetLastError();return false;}
    input.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(input.value==INVALID_HANDLE_VALUE){result.error=GetLastError();return false;}
    STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);start.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    start.StartupInfo.wShowWindow=SW_HIDE;start.StartupInfo.hStdInput=input.value;start.StartupInfo.hStdOutput=start.StartupInfo.hStdError=write.value;
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> attributes(bytes);
    start.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if(!InitializeProcThreadAttributeList(start.lpAttributeList,1,0,&bytes)){result.error=GetLastError();return false;}
    const HANDLE inherited[]{input.value,write.value};
    if(!UpdateProcThreadAttribute(start.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(inherited),sizeof inherited,nullptr,nullptr)){
        result.error=GetLastError();DeleteProcThreadAttributeList(start.lpAttributeList);return false;}
    PROCESS_INFORMATION child{};
    result.started=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,folder.c_str(),&start.StartupInfo,&child)!=FALSE;
    result.error=result.started?0:GetLastError();DeleteProcThreadAttributeList(start.lpAttributeList);
    if(!result.started)return false;
    process.value=child.hProcess;thread.value=child.hThread;thread.Close();write.Close();input.Close();
    std::wstring reported;
    const auto report=[&]{if(progress){const auto text=LatestLine(result);if(!text.empty()&&text!=reported){reported=text;progress(text);}}};
    for(;;){if(Drain(read.value,result))report();const auto waited=WaitForSingleObject(process.value,10);
        if(waited==WAIT_OBJECT_0)break;
        if(waited!=WAIT_TIMEOUT){result.error=GetLastError();return false;}}
    // Descendants must not keep this worker waiting for pipe EOF forever.
    for(unsigned pass=0;pass<64&&Drain(read.value,result);++pass){}report();
    if(!GetExitCodeProcess(process.value,&result.exitCode)){result.error=GetLastError();return false;}
    return true;
}
}
