#pragma once
#include "model.hpp"
#include "ini.hpp"
#include "game_discovery.hpp"
#include <memory>

// Adapter for existing command-line injectors whose settings live in vr.ini.
// UI and discovery are shared; profiles retain each game's keys and defaults.
namespace VrModIniGame {
using VrModLauncher::App;using VrModLauncher::Field;using VrModLauncher::Kind;
using VrModLauncher::Model;using VrModLauncher::Page;
struct Option {
    std::wstring id,section,key,label,help,fallback=L"1";
    Kind kind=Kind::Boolean;std::vector<std::wstring> choices,stored;
};
struct Profile {
    std::wstring id,title,heading,appId,executable,module,host;
    std::vector<Option> options;
    std::function<bool(const std::wstring&,std::wstring&)> verifyGame;
};
inline std::wstring Join(const std::wstring& folder,const std::wstring& name){return VrModConnection::Detail::Join(folder,name);}
inline bool Regular(const std::wstring& path){const DWORD a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
inline std::wstring PackageFolder(){wchar_t path[32768]{};const DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return {};return VrModConnection::Detail::Parent(std::wstring(path,n));}
inline std::wstring GamePath(const Profile& profile,const Model& model){auto path=VrModIni::Trim(model.Get(L"game").value);const DWORD a=GetFileAttributesW(path.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY))path=Join(path,profile.executable);return path;}
inline std::wstring Quote(const std::wstring& text){
    std::wstring result=L"\"";size_t slashes=0;
    for(auto c:text){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){result.append(slashes*2+1,L'\\');result+=c;}else{result.append(slashes,L'\\');result+=c;}slashes=0;}
    result.append(slashes*2,L'\\');return result+L'\"';
}
inline bool Validate(const Profile& profile,Model& model,std::wstring& error){
    if(!VrModLauncher::ValidateModel(model,error))return false;
    const auto path=GamePath(profile,model);
    if(!VrModConnection::AbsolutePath(path)){error=L"Select the game executable or its folder.";return false;}
    for(const auto& option:profile.options){const auto& field=model.Get(option.id);
        if(option.kind==Kind::Choice&&option.choices.size()!=option.stored.size()){error=L"Invalid game settings profile.";return false;}
        if(option.kind!=Kind::Boolean&&option.kind!=Kind::Choice){error=L"Unsupported game settings profile.";return false;}
        if(!VrModIni::Scalar(field.value)){error=L"Invalid setting value.";return false;}
    }
    model.Get(L"game").value=path;error.clear();return true;
}
struct Session {
    Profile profile;std::wstring folder;std::vector<VrModIni::File> files;
    std::vector<std::wstring> original;
};
inline bool Inspect(const Profile& profile,const std::wstring& folder,const Model& model,std::wstring& report){
    const auto game=GamePath(profile,model);
    if(!Regular(game)){report=L"The selected game executable was not found.";return false;}
    if(!profile.verifyGame||!profile.verifyGame(game,report))return false;
    for(const auto& file:{L"LaunchVR.exe",profile.module.c_str(),profile.host.c_str(),L"vr.ini"})if(!Regular(Join(folder,file))){report=L"Required package file is missing: "+std::wstring(file);return false;}
    report=L"Game build and package files passed inspection.\r\nConnect the headset through Meta Quest Link / Air Link before playing. This check does not open the headset runtime.";return true;
}
inline bool CreateApp(const Profile& profile,const std::wstring& folder,App& app,std::wstring& error){
    auto session=std::make_shared<Session>();session->profile=profile;session->folder=folder;session->files.resize(2);
    if(!VrModIni::Load(Join(folder,L"vr.ini"),session->files[0],error,true)||!VrModIni::Load(Join(folder,L"launcher.ini"),session->files[1],error))return false;
    app.id=profile.id;app.title=profile.title;app.heading=profile.heading;
    app.description=L"Changes apply the next time you start the mod.";
    Page connection{L"connection",L"Connection & game",{}};
    Field headset;headset.id=L"connection";headset.kind=Kind::Note;headset.label=L"Meta Quest Link / Air Link";
    headset.help=L"This package currently uses the Meta compositor. OpenXR / Virtual Desktop / SteamVR host selection is not available in this build.";
    connection.fields.push_back(headset);
    Field game;game.id=L"game";game.label=L"Game executable or game folder";game.kind=Kind::File;
    game.value=VrModIni::Read(session->files[1],L"Launcher",L"GameExe",L"");
    if(game.value.empty())game.value=VrModGameDiscovery::Discover({profile.appId,profile.executable});
    connection.fields.push_back(game);app.model.pages.push_back(connection);
    Page modifiers{L"modifiers",L"Modifiers",{}};
    for(const auto& option:profile.options){
        auto stored=VrModIni::Read(session->files[0],option.section,option.key,option.fallback);
        Field field;field.id=option.id;field.label=option.label;field.help=option.help;field.kind=option.kind;field.choices=option.choices;
        if(option.kind==Kind::Boolean)field.value=VrModIni::Integer(session->files[0],option.section.c_str(),option.key.c_str(),option.fallback==L"1"?1:0)!=0?L"1":L"0";
        else {const auto at=std::find(option.stored.begin(),option.stored.end(),stored);if(at==option.stored.end()){error=L"Unsupported configured value for "+option.label+L". Edit vr.ini or restore a supported value before opening settings.";return false;}field.value=option.choices.at(static_cast<size_t>(at-option.stored.begin()));}
        session->original.push_back(field.value);modifiers.fields.push_back(std::move(field));
    }
    app.model.pages.push_back(std::move(modifiers));
    app.discover=[profile]{return VrModGameDiscovery::Discover({profile.appId,profile.executable});};
    app.validate=[profile](Model& model,std::wstring& message){return Validate(profile,model,message);};
    app.save=[session](Model& model,std::wstring& message){
        if(!Validate(session->profile,model,message))return false;
        auto next=session->files;
        if(model.Get(L"game").value!=VrModIni::Read(next[1],L"Launcher",L"GameExe",L""))VrModIni::Patch(next[1],L"Launcher",L"GameExe",model.Get(L"game").value,false);
        for(size_t i=0;i<session->profile.options.size();++i){const auto& option=session->profile.options[i];const auto& value=model.Get(option.id).value;
            if(value==session->original[i])continue;
            const auto stored=option.kind==Kind::Boolean?value:option.stored.at(static_cast<size_t>(std::find(option.choices.begin(),option.choices.end(),value)-option.choices.begin()));
            VrModIni::Patch(next[0],option.section,option.key,stored);
        }
        if(!VrModIni::Save(session->files,std::move(next),message))return false;
        for(size_t i=0;i<session->profile.options.size();++i)session->original[i]=model.Get(session->profile.options[i].id).value;
        return true;
    };
    app.inspect=[profile,folder](const Model& model,std::wstring& report){return Inspect(profile,folder,model,report);};
    app.launch=[profile,folder](const Model& model,std::wstring& report,std::function<void(const std::wstring&)> progress){
        if(!Inspect(profile,folder,model,report))return false;
        const auto launcher=Join(folder,L"LaunchVR.exe");auto command=Quote(launcher)+L" "+Quote(GamePath(profile,model));
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        progress(L"Starting the existing game launcher...");
        if(!CreateProcessW(launcher.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,folder.c_str(),&startup,&process)){report=L"Could not start LaunchVR.exe (Windows error "+std::to_wstring(GetLastError())+L").";return false;}
        CloseHandle(process.hThread);const DWORD waited=WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;
        const bool okay=waited==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==0;CloseHandle(process.hProcess);
        report=okay?L"The game launcher completed. Game output and detailed diagnostics are stored beside the mod.":L"LaunchVR.exe failed (exit code "+std::to_wstring(code)+L"). Check the package logs or run LaunchVR.exe from a terminal for details.";return okay;
    };
    return VrModLauncher::ValidateModel(app.model,error);
}
}
