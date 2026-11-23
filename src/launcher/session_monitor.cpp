#define WIN32_LEAN_AND_MEAN
#include "session_monitor.hpp"
#include "release_version.hpp"
#include <cerrno>
#include <limits>

namespace {
bool Integer(const wchar_t* text,std::uint64_t& value){
    if(!text||!*text||*text==L'-'||*text==L'+')return false;
    wchar_t* end=nullptr;errno=0;value=std::wcstoull(text,&end,10);return !errno&&end&&!*end;
}
}
int wmain(int argc,wchar_t** argv){
    DWORD pid=0;std::uint64_t creation=0;std::wstring game,package;
    for(int index=1;index<argc;++index){
        if(index+1==argc)return 1;
        const auto option=std::wstring(argv[index++]);std::uint64_t value=0;
        if(option==L"--pid"){if(!Integer(argv[index],value)||!value||value>MAXDWORD)return 1;pid=DWORD(value);}
        else if(option==L"--created"){if(!Integer(argv[index],creation)||!creation)return 1;}
        else if(option==L"--game")game=argv[index];else if(option==L"--package")package=argv[index];else return 1;
    }
    if(!pid||!creation||game.empty()||package.empty())return 1;
    if(!DiagnosticsSession::Enabled(package))return 0;
    SessionMonitor::Handle process;process.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process.value)return 2;
    char digest[65]{};
    if(!SessionMonitor::Matches(process.value,game,creation)||!HashSupportedGameFile(game.c_str(),digest))return 3;
    return SessionMonitor::Observe(process.value,pid,creation,game,package,KL1_RELEASE_VERSION);
}
