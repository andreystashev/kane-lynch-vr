#include "performance_log.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdlib>
namespace fs=std::filesystem;
namespace {
unsigned checks=0;
void Check(bool good,const char* reason){++checks;if(!good){std::fprintf(stderr,"FAIL: %s\n",reason);std::abort();}}
void Write(const fs::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;Check(file.good(),"fixture write");}
std::string Read(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
std::string SampleLine(const fs::path& path){const auto all=Read(path);const auto at=all.rfind("{\"event\":\"sample\"");return at==std::string::npos?"":all.substr(at,all.find('\n',at)-at);}
struct Fixture {
    fs::path root,parent;
    Fixture(){wchar_t temp[MAX_PATH]{},name[MAX_PATH]{};Check(GetTempPathW(MAX_PATH,temp)!=0,"temp root");parent=temp;
        Check(GetTempFileNameW(temp,L"k1p",0,name)&&DeleteFileW(name)&&CreateDirectoryW(name,nullptr),"isolated fixture directory");root=name;}
    ~Fixture(){const auto target=fs::weakly_canonical(root);Check(target.parent_path()==fs::weakly_canonical(parent)&&target.filename().wstring().find(L"k1p")==0,"recursive cleanup stays in the exact verified temporary fixture");fs::remove_all(target);}
};
struct Host {
    PROCESS_INFORMATION process{};
    Host(const fs::path& root,const wchar_t* filename){wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);const auto exe=root/filename;
        Check(CopyFileW(own,exe.c_str(),FALSE)!=0,"copy isolated host helper");std::wstring command=L"\""+exe.wstring()+L"\" --host-fixture";
        std::vector<wchar_t> writable(command.begin(),command.end());writable.push_back(0);STARTUPINFOW startup{};startup.cb=sizeof(startup);
        Check(CreateProcessW(exe.c_str(),writable.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process)!=0,"start hidden test helper, no game/runtime");CloseHandle(process.hThread);}
    void Stop(){if(process.hProcess){TerminateProcess(process.hProcess,0);WaitForSingleObject(process.hProcess,1000);CloseHandle(process.hProcess);process.hProcess=nullptr;}}
    ~Host(){Stop();}
};
void ParserAndRates(){
    using namespace PerformanceLog;
    for(const std::string bytes:{"{}","{\"nested\":{\"pid\":77}}","{\"pid\":77,\"x\":}","{\"pid\":77} trailing","{\"pid\":77,\"pid\":78}","{\"pid\":-77}","{\"pid\":77.0}","{\"pid\":077}","{\"pid\":18446744073709551616}"}){std::uint64_t value=0;Check(!Json(bytes).Unsigned("pid",value),"complete JSON and unsigned top-level unique identity required");}
    const std::string spaced="{\n \"pid\":77, \"ready\":false, \"label\":\"space and \\\"quote\\\"\", \"v\":[true,null,-2.5e-3]}\n";
    Json parsed(spaced);std::uint64_t value=0;bool ready=true;
    Check(parsed.Unsigned("pid",value)&&value==77&&parsed.Boolean("ready",ready)&&!ready,"validated parser can read top-level numeric and boolean fields");
    Check(Compact(spaced).find('\n')==std::string::npos&&Compact(spaced).find("space and")!=std::string::npos,"compact JSON preserves quoted spaces");
    CounterRate rate;Check(!rate.Sample(10,1000).valid,"first rate is unknown");auto next=rate.Sample(70,2000);
    Check(next.valid&&next.hz==60&&next.windowMs==1000,"counter delta uses real sample window");
    Check(!rate.Sample(5,3000).valid&&!rate.Sample(15,4000,2).valid,"reset or replaced counter cannot invent a rate");
    Check(!rate.JsonSample("null","presents",5000).valid&&!rate.Sample(50,6000,2).valid,"missing sample resets the counter baseline");
    ProcessSampler process;const auto pid=GetCurrentProcessId(),now=GetTickCount();const auto first=process.Sample(pid,now),second=process.Sample(pid,std::uint64_t(now)+1000);
    Check(first.valid&&first.creation&&first.memoryValid&&first.workingSet&&first.privateBytes&&!first.cpuValid,"first process sample has memory and creation identity but no invented CPU percentage");
    Check(second.cpuValid&&second.cpuPercent>=0,"process CPU has a delta after its baseline");
    Check(first.ioValid&&second.ioValid&&second.ioReadBytes>=first.ioReadBytes&&second.ioWriteBytes>=first.ioWriteBytes&&second.pageFaultCount>=first.pageFaultCount,
          "process I/O and page-fault counters retain monotonic raw evidence for streaming stalls");
    std::uint64_t ioBytes=0;const auto processJson=second.Json();
    Check(Json(processJson).Unsigned("ioReadBytes",ioBytes)&&ioBytes==second.ioReadBytes,
          "process streaming evidence is serialized as counters rather than inferred disk stalls");
    Check(!process.Sample(MAXDWORD,std::uint64_t(now)+2000).valid&&!process.Sample(pid,std::uint64_t(now)+3000).cpuValid,"missing process discards CPU baseline");
}
void RecorderTest(){
    using namespace PerformanceLog;Fixture fixture;const auto& root=fixture.root;
    const auto ini=root/L"vr.ini",status=root/L"KaneLynchVR-status.json",hostStatus=root/L"KaneLynchVRHost-status.json";
    const auto pid=GetCurrentProcessId();const auto now=GetTickCount64();
    const auto sample=[&](std::uint64_t time,std::uint64_t frames=100,std::uint64_t scene=50,const std::string& padding=""){
        Write(status,"{\"pid\":"+std::to_string(pid)+",\"updatedTick\":"+std::to_string(DWORD(time))+",\"phase\":\"submitting\",\"presents\":"+std::to_string(frames)+",\"sceneFrames\":"+std::to_string(scene)+",\"padding\":\""+padding+"\"}");};
    Write(ini,"[Diagnostics]\nPerformanceLogging=1\nCrashLogging=1\nVisualDrawCapture=1\n");Recorder legacy;legacy.Tick(root.wstring(),pid,"test",now);Check(legacy.Path().empty()&&!fs::exists(root/L"logs"),"legacy diagnostic flags cannot override release opt-out");
    Write(ini,"[Diagnostics]\nPerformanceLogging=0\n");Recorder recorder;recorder.Tick(root.wstring(),pid,"test",now);Check(recorder.Path().empty()&&recorder.AcceptedGameSamples()==0,"disabled logger creates no file or acknowledgement");
    Write(ini,"\xef\xbb\xbf[Launcher]\nRenderer=D3D9On12\n[Diagnostics]\nEnabled=1\nPerformanceLogging=1\nPerformanceMaxMegabytes=1\nPerformanceSegments=2\n");sample(now);recorder.Tick(root.wstring(),pid,"test",now);
    Check(recorder.Active()&&!recorder.Failed()&&recorder.AcceptedGameSamples()==1,"session started and valid written game sample acknowledged");const fs::path first=recorder.Path();const auto initial=Read(first);
    Check(initial.find("\"configuredRenderer\":\"D3D9On12\"")!=std::string::npos,"BOM first section records the same renderer as the launcher");
    Check(initial.find("\"processCreationFileTime\":")!=std::string::npos&&initial.find("\"host\":null")!=std::string::npos&&initial.find("\"sourceHz\":null")!=std::string::npos,"session correlation, raw game, missing host and unknown initial rates preserved");
    recorder.Tick(root.wstring(),pid,"test",now);Check(Read(first)==initial&&recorder.AcceptedGameSamples()==1,"one-second sampling prevents duplicate/render-rate writes or acknowledgement");
    Check(!recorder.Event("{\"serial\":1,\"event\":}")&&Read(first)==initial,"torn explicit event rejected");
    Check(recorder.Event("{\"serial\":1,\"event\":\"shot\",\"value\":[1,2,3]}")&&recorder.AcceptedGameSamples()==1,"unrelated explicit event does not acknowledge a game sample");
    Write(status,"{\"pid\":"+std::to_string(pid)+",\"broken\":}");recorder.Tick(root.wstring(),pid,"test",now+1000);
    Check(SampleLine(first).find("\"game\":null")!=std::string::npos&&SampleLine(first).find("invalid_or_changing_file")!=std::string::npos&&recorder.AcceptedGameSamples()==1,"torn/frozen null game sample is written without acknowledgement");
    sample(now+2000,160,80);recorder.Tick(root.wstring(),pid,"test",now+2000);sample(now+3000,220,110);recorder.Tick(root.wstring(),pid,"test",now+3000);
    const auto rates=SampleLine(first);Check(rates.find("\"sourceHz\":60.000")!=std::string::npos&&rates.find("\"sceneEyeHz\":30.000")!=std::string::npos&&rates.find("\"scenePairHz\":15.000")!=std::string::npos&&rates.find("\"scenePairEstimated\":true")!=std::string::npos,"source, scene-eye and estimated game-pair rates stay distinct");
    Check(Snapshot(status.wstring(),"pid",pid+1,DWORD(now+3000),true)=="null"&&Snapshot(status.wstring(),"pid",pid,DWORD(now+8000),true)=="null","foreign PID and stale status rejected");
    FILETIME clock{};GetSystemTimeAsFileTime(&clock);const auto oldTicks=FileTicks(clock)-40000000;
    FILETIME old{DWORD(oldTicks),DWORD(oldTicks>>32)};
    HANDLE stamp=CreateFileW(status.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(stamp!=INVALID_HANDLE_VALUE&&SetFileTime(stamp,nullptr,nullptr,&old),"isolated stale-file timestamp fixture");CloseHandle(stamp);
    Check(ReadSnapshot(status.wstring(),"pid",pid,DWORD(now+3000),true).reason=="stale_file","fresh tick cannot authorize an older physical status file");sample(now+3000,220,110);
    wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);FILETIME utc{};GetSystemTimeAsFileTime(&utc);
    Check(ProcessMatches(pid,utc,own)&&!ProcessMatches(pid,FILETIME{},own)&&!ProcessMatches(pid,utc,Join(root.wstring(),L"other.exe")),"process path and creation time exclude older files/PID reuse");
    Write(hostStatus,"{\"gamePid\":"+std::to_string(pid)+",\"hostPid\":4294967295}");Check(HostSnapshot(root.wstring(),pid,DWORD(now))=="null","dead host cannot authorize status");
    {Host host(root,L"KaneLynchVRHost.exe");
        FILETIME created{},exited{},kernel{},user{};
        Check(GetProcessTimes(host.process.hProcess,&created,&exited,&kernel,&user)!=0,"host fixture creation timestamp");
        const auto writeHost=[&](unsigned pairs,unsigned submitted,unsigned stampMs){
            Write(hostStatus,"{\"gamePid\":"+std::to_string(pid)+",\"hostPid\":"+std::to_string(host.process.dwProcessId)+",\"hostReady\":true,\"newScenePairs\":"+std::to_string(pairs)+",\"framesSubmitted\":"+std::to_string(submitted)+"}");
            const auto ticks=FileTicks(created)+std::uint64_t(stampMs)*10000;
            const FILETIME written{DWORD(ticks),DWORD(ticks>>32)};
            const HANDLE file=CreateFileW(hostStatus.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            Check(file!=INVALID_HANDLE_VALUE&&SetFileTime(file,nullptr,nullptr,&written),"host fixture report has its own clock");
            CloseHandle(file);
        };
        writeHost(10,24,0);sample(now+4000);recorder.Tick(root.wstring(),pid,"test",now+4000);Check(SampleLine(first).find("\"hostValid\":true")!=std::string::npos,"exact matching live Meta host accepted");
        Sleep(250); // Make the deterministic timestamp below older than the observation.
        writeHost(13,48,200);sample(now+5000);recorder.Tick(root.wstring(),pid,"test",now+5000);
        const auto hostRates=SampleLine(first);
        Check(hostRates.find("\"hostSubmitHz\":120.000")!=std::string::npos&&hostRates.find("\"hostScenePairHz\":15.000")!=std::string::npos&&hostRates.find("\"hostWindowMs\":200")!=std::string::npos,
              "host pair and submit rates use the host report interval independently of game sampling");
        recorder.FlushFinal(root.wstring(),pid,"test",now+5050);
        Check(SampleLine(first).find("\"hostSubmitHz\":null")!=std::string::npos&&SampleLine(first).find("\"hostWindowMs\":null")!=std::string::npos,
              "rereading the same host status does not invent a zero-rate interval");
        host.Stop();sample(now+6000);recorder.FlushFinal(root.wstring(),pid,"test",now+6000);Check(SampleLine(first).find("host_process_identity")!=std::string::npos,"host exit invalidates raw host snapshot");}
    {Host host(root,L"KaneLynchOpenXRHost.exe");Write(hostStatus,"{\"gamePid\":"+std::to_string(pid)+",\"hostPid\":"+std::to_string(host.process.dwProcessId)+",\"hostReady\":false}");Check(HostSnapshot(root.wstring(),pid,DWORD(now))!="null","exact live OpenXR host accepted while readiness is false");
        const auto exe=root/L"KaneLynchOpenXRHost.exe",alias=root/L"alias.exe",copy=root/L"copy.exe";
        Check(CreateHardLinkW(alias.c_str(),exe.c_str(),nullptr)&&CopyFileW(exe.c_str(),copy.c_str(),FALSE),"create executable identity controls");
        GetSystemTimeAsFileTime(&utc);
        Check(ProcessMatches(host.process.dwProcessId,utc,alias.wstring())&&!ProcessMatches(host.process.dwProcessId,utc,copy.wstring()),
              "same executable through another path is accepted while a separate copy is rejected");}
    Write(root/L"logs"/L"performance-old.jsonl","keep old session\n");
    for(unsigned i=7;i<67;++i){sample(now+i*1000,100+i*60,50+i*30,std::string(58000,'x'));recorder.Tick(root.wstring(),pid,"test",now+i*1000);Check(!recorder.Failed(),"rotation remains writable");}
    Check(fs::exists(first.wstring()+L".1")&&!fs::exists(first.wstring()+L".2")&&fs::file_size(first)<=1024*1024&&fs::file_size(first.wstring()+L".1")<=1024*1024,"bounded active/archive segments stay under strict byte limits");
    Check(Read(root/L"logs"/L"performance-old.jsonl")=="keep old session\n"&&Read(first).find("\"event\":\"session\"")==1,"rotation retains full records and unrelated prior sessions");
    Write(ini,"[Diagnostics]\nEnabled=1\nPerformanceLogging=1\nPerformanceMaxMegabytes=1\nPerformanceSegments=1\n");sample(now+67000);recorder.Tick(root.wstring(),pid,"test",now+67000);Check(!fs::exists(first.wstring()+L".1"),"live segment reduction removes only this session archive");
    Write(ini,"[Diagnostics]\nPerformanceLogging=0\n");recorder.Tick(root.wstring(),pid,"test",now+68000);Check(!recorder.Active()&&Read(first).find("\"reason\":\"disabled\"")!=std::string::npos,"live disable saves final record");
    const auto stopped=Read(first);const auto beforeResume=recorder.AcceptedGameSamples();sample(now+69000);Write(ini,"[Diagnostics]\nEnabled=1\nPerformanceLogging=1\n");recorder.Tick(root.wstring(),pid,"test",now+69000);const fs::path second=recorder.Path();Check(second!=first&&Read(first)==stopped&&recorder.AcceptedGameSamples()==beforeResume+1,"re-enable creates a distinct session while acknowledgement remains monotonic");
    recorder.Close("test_finished");Check(!recorder.Active()&&Read(second).find("\"reason\":\"test_finished\"")!=std::string::npos,"public Close saves orderly final reason");
    sample(now,100,50,std::string(600000,'x'));
    Check(ReadSnapshot(status.wstring(),"pid",pid,DWORD(now),true).Valid(),"combined opt-in traces above the old 256KB bound remain recordable");
    sample(now,100,50,std::string(static_cast<std::size_t>(MaxSnapshotBytes)+1,'x'));
    Check(!ReadSnapshot(status.wstring(),"pid",pid,DWORD(now),true).Valid(),"snapshot above the new 4MB bound is rejected before allocation");
    Write(ini,"[Diagnostics]\nEnabled=1\nPerformanceLogging=1\nPerformanceMaxMegabytes=1\nPerformanceSegments=4\n");
    Recorder final;
    sample(now+90000,100,50,"initial-only");final.Tick(root.wstring(),pid,"final-fixture",now+90000);
    Check(final.AcceptedGameSamples()==1,"fresh final-fixture sample acknowledged once");
    const fs::path finalPath=final.Path();const auto periodic=Read(finalPath);
    sample(now+90050,101,51,"final-only");final.Tick(root.wstring(),pid,"final-fixture",now+90050);
    Check(Read(finalPath)==periodic&&final.AcceptedGameSamples()==1,"ordinary sample remains rate-limited without acknowledgement within one second");
    final.FlushFinal(root.wstring(),pid,"final-fixture",now+90050);
    const auto finalLine=SampleLine(finalPath);
    Check(finalLine.find("\"final\":true")!=std::string::npos&&finalLine.find("final-only")!=std::string::npos&&final.AcceptedGameSamples()==2,
          "final flush records fresh status and buffered evidence within one second");
    sample(now+90050,101,51,"stale-only");final.FlushFinal(root.wstring(),pid,"final-fixture",now+95000);
    Check(SampleLine(finalPath).find("\"game\":null")!=std::string::npos&&final.AcceptedGameSamples()==2,
          "forced stale game snapshot is not acknowledged");
    sample(now+95050,102,52,"fresh-after-stale");final.FlushFinal(root.wstring(),pid,"final-fixture",now+95050);
    Check(final.AcceptedGameSamples()==3&&SampleLine(finalPath).find("fresh-after-stale")!=std::string::npos,
          "later fresh forced snapshot acknowledges only after successful write");
    final.Close("test_finished");
    Write(ini,"[Diagnostics]\nPerformanceLogging=0\n");Recorder disabledFinal;
    disabledFinal.FlushFinal(root.wstring(),pid,"disabled-final",now+91000);
    Check(!disabledFinal.Active()&&disabledFinal.Path().empty()&&disabledFinal.AcceptedGameSamples()==0,"final flush never overrides disabled logging or acknowledges data");
    Recorder absentFinal;absentFinal.FlushFinal((root/L"nonexistent"/L"nested").wstring(),pid,"absent-final",now+91000);
    Check(!absentFinal.Failed()&&absentFinal.Path().empty(),"missing configuration keeps release diagnostics off");
    const auto blocked=root/L"blocked";fs::create_directory(blocked);
    Write(blocked/L"vr.ini","[Diagnostics]\nEnabled=1\nPerformanceLogging=1\n");Write(blocked/L"logs","blocks directory creation");
    Recorder failedFinal;failedFinal.FlushFinal(blocked.wstring(),pid,"failed-final",now+91000);
    Check(failedFinal.Failed()&&failedFinal.AcceptedGameSamples()==0,"failed output creation cannot acknowledge a game snapshot");
    // Exercise the real rotating writer rather than merely inspecting the
    // configuration: 32 allowed segments retain 31 backups, and reducing the
    // setting subsequently removes only this session's excess archives.
    Write(ini,"[Diagnostics]\nEnabled=1\nPerformanceLogging=1\nPerformanceMaxMegabytes=1\nPerformanceSegments=32\n");
    Recorder extended;
    for(unsigned i=0;i<34;++i){const auto tick=now+100000+i*1000;sample(tick,100+i,50+i,std::string(600000,'x'));extended.Tick(root.wstring(),pid,"extended",tick);Check(!extended.Failed(),"large trace/extended rotation remains writable");}
    const fs::path extendedPath=extended.Path();
    Check(fs::exists(extendedPath.wstring()+L".31")&&!fs::exists(extendedPath.wstring()+L".32"),"32 segments retain exactly 31 archived files");
    Check(SampleLine(extendedPath).find("\"game\":{\"pid\":")!=std::string::npos&&SampleLine(extendedPath).find(std::string(300000,'x'))!=std::string::npos,
          "large complete status is present in a real recorded JSONL sample");
    Write(ini,"[Diagnostics]\nEnabled=1\nPerformanceLogging=1\nPerformanceMaxMegabytes=1\nPerformanceSegments=4\n");
    sample(now+135000,200,100,std::string(600000,'x'));extended.Tick(root.wstring(),pid,"extended",now+135000);
    Check(!fs::exists(extendedPath.wstring()+L".31")&&fs::exists(extendedPath.wstring()+L".3")&&!fs::exists(extendedPath.wstring()+L".4"),
          "reducing segments removes only this session's excess archives");
    Check(Read(root/L"logs"/L"performance-old.jsonl")=="keep old session\n","extended rotation leaves unrelated sessions untouched");
    extended.Close("test_finished");
}
}
int main(int argc,char** argv){if(argc==2&&std::string(argv[1])=="--host-fixture"){Sleep(60000);return 0;}ParserAndRates();RecorderTest();std::printf("KL1 performance recorder: %u checks PASS; isolated files/processes only.\n",checks);}
