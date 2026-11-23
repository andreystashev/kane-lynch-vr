#pragma once
#include "profile.hpp"
#include "audio_bootstrap.hpp"
#include <cstring>
#include <string>
namespace LauncherStartupRoute {
enum class Kind { Invalid, DirectGame, OfficialLauncher };
struct Selection {Kind kind=Kind::Invalid;bool attachAudio=false;bool needsSteam=false;};
constexpr const char* GogLauncherSha="bbef9a979b04f8d7fcdcd6e970b92a0877c9cdc54db7317eb1b3d69e6b3dd580";
constexpr const char* ExistingWrapperLauncherSha="f2f6714091db133de863fcc974ce6cc17b23d3f2754d280b8112510c85e1894f";
constexpr const char* RetailLauncherSha="92ba3b88de743c7e7c4ed97ed03aec52b3aa096faead36efc4690b85ef022290";
// The exact Steam launcher owns its SteamStartup/bootstrap path regardless
// of optional endpoint routing. Legacy keeps its existing direct route when
// audio preparation is disabled; an audio request may select its own proven
// official launcher, never the launcher belonging to the other EXE profile.
inline Selection Select(const char* gameHash,bool audioPrepared,const char* officialHash){
    if(!gameHash)return {};
    const bool steam=IsSupportedExecutableHash(gameHash)&&std::strcmp(gameHash,kSupportedSha256)!=0;
    const bool legacy=std::strcmp(gameHash,kSupportedSha256)==0;
    if(!steam&&!legacy)return {};
    if(steam){
        if(!officialHash)return {};
        if(!std::strcmp(officialHash,AudioBootstrap::OfficialSteamLauncherSha))return {Kind::OfficialLauncher,audioPrepared,true};
        if(!std::strcmp(officialHash,AudioBootstrap::OfficialLauncherSha))return {audioPrepared?Kind::OfficialLauncher:Kind::DirectGame,audioPrepared,false};
        // Identical main game code is shipped with different native startup
        // executables. Preserve the selected installation's existing route;
        // never copy a launcher, crack, SDK or DRM component between folders.
        if(!std::strcmp(officialHash,GogLauncherSha))return {Kind::OfficialLauncher,audioPrepared,false};
        if(!std::strcmp(officialHash,ExistingWrapperLauncherSha)||!std::strcmp(officialHash,RetailLauncherSha))return {Kind::OfficialLauncher,false,false};
        return {};
    }
    if(audioPrepared&&officialHash&&!std::strcmp(officialHash,AudioBootstrap::OfficialLauncherSha))
        return {Kind::OfficialLauncher,true};
    return {Kind::DirectGame,false};
}
inline std::wstring ReadNativeLauncher(const std::wstring& game, char digest[65]) {
    digest[0]='\0';
    const auto separator=game.find_last_of(L"\\/");
    if(separator==std::wstring::npos)return {};
    const auto launcher=game.substr(0,separator+1)+L"Launcher.exe";
    HashFile(launcher.c_str(),digest);
    return launcher;
}

}
