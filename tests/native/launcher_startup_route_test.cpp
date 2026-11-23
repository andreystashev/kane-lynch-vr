#include "launcher_startup_route.hpp"
#include <cstdio>
#include <cstring>
namespace R=LauncherStartupRoute;
int main(){
    unsigned checks=0,failed=0;
    const auto check=[&](const char* name,const char* game,bool audio,const char* official,R::Kind kind,bool attach){
        ++checks;const auto route=R::Select(game,audio,official);
        if(route.kind!=kind||route.attachAudio!=attach){++failed;std::printf("FAIL %s\n",name);}
    };
    check("Steam endpoint OFF preserves official bootstrap",kSupportedSteamSha256,false,AudioBootstrap::OfficialSteamLauncherSha,R::Kind::OfficialLauncher,false);
    check("Steam endpoint ON uses same official bootstrap",kSupportedSteamSha256,true,AudioBootstrap::OfficialSteamLauncherSha,R::Kind::OfficialLauncher,true);
    for(bool audio:{false,true}){
        check("Steam absent launcher rejects before native launch",kSupportedSteamSha256,audio,nullptr,R::Kind::Invalid,false);
        check("Steam unreadable launcher rejects",kSupportedSteamSha256,audio,"",R::Kind::Invalid,false);
        check("Steam unknown launcher rejects",kSupportedSteamSha256,audio,"unknown",R::Kind::Invalid,false);
        check("Shared main code with exact legacy launcher retains its native policy",kSupportedSteamSha256,audio,AudioBootstrap::OfficialLauncherSha,audio?R::Kind::OfficialLauncher:R::Kind::DirectGame,audio);
    }
    check("Legacy endpoint OFF keeps direct route",kSupportedSha256,false,nullptr,R::Kind::DirectGame,false);
    check("Legacy OFF ignores valid optional launcher",kSupportedSha256,false,AudioBootstrap::OfficialLauncherSha,R::Kind::DirectGame,false);
    check("Legacy OFF ignores foreign optional launcher",kSupportedSha256,false,AudioBootstrap::OfficialSteamLauncherSha,R::Kind::DirectGame,false);
    check("Legacy ON routes its exact official launcher",kSupportedSha256,true,AudioBootstrap::OfficialLauncherSha,R::Kind::OfficialLauncher,true);
    check("Legacy ON unknown launcher keeps native route without audio",kSupportedSha256,true,"unknown",R::Kind::DirectGame,false);
    check("Legacy ON missing launcher keeps native route without audio",kSupportedSha256,true,nullptr,R::Kind::DirectGame,false);
    check("Legacy ON cannot attach through foreign Steam launcher",kSupportedSha256,true,AudioBootstrap::OfficialSteamLauncherSha,R::Kind::DirectGame,false);
    check("Unknown game does not inherit Steam route","unknown",false,AudioBootstrap::OfficialSteamLauncherSha,R::Kind::Invalid,false);
    check("Null game does not inherit legacy audio route",nullptr,true,AudioBootstrap::OfficialLauncherSha,R::Kind::Invalid,false);
    char changed[65];std::strcpy(changed,AudioBootstrap::OfficialSteamLauncherSha);changed[0]=changed[0]=='0'?'1':'0';
    check("One changed launcher hash byte fails exact profile",kSupportedSteamSha256,false,changed,R::Kind::Invalid,false);
    for(bool audio:{false,true}){
        check("Byte-identical GOG main preserves its unpacked native launcher",kSupportedSteamSha256,audio,R::GogLauncherSha,R::Kind::OfficialLauncher,audio);
        check("Existing wrapper runs unchanged with no early audio patch",kSupportedSteamSha256,audio,R::ExistingWrapperLauncherSha,R::Kind::OfficialLauncher,false);
        check("Retail wrapper runs unchanged with no early audio patch",kSupportedSteamSha256,audio,R::RetailLauncherSha,R::Kind::OfficialLauncher,false);
    }
    ++checks;if(!R::Select(kSupportedSteamSha256,false,AudioBootstrap::OfficialSteamLauncherSha).needsSteam)++failed;
    for(const auto* hash:{R::GogLauncherSha,R::ExistingWrapperLauncherSha,R::RetailLauncherSha}){++checks;if(R::Select(kSupportedSteamSha256,false,hash).needsSteam)++failed;}
    std::printf("launcher_startup_route: %u checks, %u failures\n",checks,failed);return failed?1:0;
}
