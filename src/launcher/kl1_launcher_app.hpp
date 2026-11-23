#pragma once
#include "launcher_paths.hpp"
#include "game_renderer.hpp"
#include "kl1_launch_connection.hpp"
#include "launcher_child.hpp"
#include "launcher_startup_route.hpp"
#include "release_version.hpp"
#include "vr_mod_shared/ini_game_adapter.hpp"
#include <memory>

namespace Kl1LauncherApp {
using namespace VrModLauncher;
struct NumberSetting {const wchar_t* id;const wchar_t* section;const wchar_t* key;const wchar_t* label;const wchar_t* help;int fallback,minimum,maximum;};
inline constexpr NumberSetting NumberSettings[]={
    {L"smoothSpeed",L"Controls",L"SmoothTurnDegreesPerSecond",L"Smooth turn speed (degrees per second)",L"30-240 degrees per second at full stick deflection.",90,30,240},
    {L"snapDegrees",L"Controls",L"SnapTurnDegrees",L"Snap turn angle (degrees)",L"15-90 degrees per step. Return the stick to centre before the next step.",30,15,90},
    {L"deadzone",L"Controls",L"DeadzonePercent",L"Stick deadzone (%)",L"5-70 percent. Raise this if a resting stick drifts.",35,5,70}
};
inline const wchar_t* TurnLabel(int mode){return mode==0?L"Mouse compatibility":mode==1?L"Snap turn":L"Smooth turn";}
inline int SelectedTurn(const Model& model){const auto& value=model.Get(L"turnMode").value;for(int mode=0;mode<3;++mode)if(value==TurnLabel(mode))return mode;return -1;}
inline bool NumberValue(const std::wstring& text,const NumberSetting& setting,int& value){
    const auto trimmed=VrModIni::Trim(text);if(trimmed.empty())return false;
    int parsed=0;
    for(const auto character:trimmed){if(character<L'0'||character>L'9'||parsed>setting.maximum/10)return false;parsed=parsed*10+int(character-L'0');if(parsed>setting.maximum)return false;}
    if(parsed<setting.minimum)return false;value=parsed;return true;
}
inline bool ValidateSettings(const Model& model,std::wstring& error){
    if(!ValidateModel(model,error))return false;
    if(SelectedTurn(model)<0){error=L"Choose a supported turn mode.";return false;}
    for(const auto& setting:NumberSettings){int value=0;if(!NumberValue(model.Get(setting.id).value,setting,value)){error=std::wstring(setting.label)+L": "+setting.help;return false;}}
    error.clear();return true;
}
inline void PatchSettings(VrModIni::File& settings,const Model& model){
    VrModIni::Patch(settings,L"Diagnostics",L"Enabled",model.Get(L"diagnosticLogs").value);
    VrModIni::Patch(settings,L"Controls",L"TurnMode",std::to_wstring(SelectedTurn(model)));
    for(const auto& setting:NumberSettings){int value=0;NumberValue(model.Get(setting.id).value,setting,value);VrModIni::Patch(settings,setting.section,setting.key,std::to_wstring(value));}
}
struct Session {std::wstring folder;std::vector<VrModIni::File> files;};
inline std::vector<std::wstring> Connections(){std::vector<std::wstring> labels;for(const auto mode:VrModConnection::Modes)labels.push_back(VrModConnection::Label(mode));return labels;}
inline VrModConnection::Mode SelectedConnection(const Model& model){for(const auto mode:VrModConnection::Modes)if(model.Get(L"connection").value==VrModConnection::Label(mode))return mode;return VrModConnection::Mode::Invalid;}
inline void Changed(Model& model){
    model.Get(L"runtime").enabled=VrModConnection::IsOpenXR(SelectedConnection(model));
    model.Get(L"smoothSpeed").enabled=SelectedTurn(model)==2;
    model.Get(L"snapDegrees").enabled=SelectedTurn(model)==1;
}
inline bool Validate(const std::wstring& folder,Model& model,std::wstring& error) {
    if(!ValidateSettings(model,error))return false;
    if(!VrModIni::Scalar(model.Get(L"runtime").value)){error=L"The OpenXR runtime path must be a single line.";return false;}
    const auto connection=SelectedConnection(model);
    if(connection==VrModConnection::Mode::Invalid){error=L"Choose a headset connection.";return false;}
    if(RendererIdentity::Parse(model.Get(L"renderer").value.c_str())==GameRenderer::Kind::Unknown){error=L"Choose a supported game renderer.";return false;}
    if(VrModConnection::IsOpenXR(connection)&&!VrModIni::Trim(model.Get(L"runtime").value).empty()){
        const auto runtime=Kl1LauncherPaths::ResolveFile(folder,model.Get(L"runtime").value);
        if(runtime.empty()||!VrModConnection::ValidateRuntime(runtime,error)){if(error.empty())error=L"Choose a valid OpenXR runtime JSON manifest, or leave it empty for automatic detection.";return false;}
    }
    const auto game=Kl1LauncherPaths::Resolve(folder,model.Get(L"game").value);char hash[65]{};
    if(game.empty()||!HashSupportedGameFile(game.c_str(),hash)) {
        error=L"Select the supported KANEANDLYNCH.EXE, or use Find game for Steam App 8080.";return false;
    }
    error.clear();return true;
}
inline bool Inspect(const std::wstring& folder,const Model& model,std::wstring& report) {
    auto checked=model;if(!Validate(folder,checked,report))return false;
    const auto game=Kl1LauncherPaths::Resolve(folder,model.Get(L"game").value);Kl1ExecutableIdentity identity;
    if(!InspectExecutable(game.c_str(),identity)||!identity.supported){report=L"The selected game executable could not be qualified.";return false;}
    char launcherHash[65]{};
    LauncherStartupRoute::ReadNativeLauncher(game,launcherHash);
    if(LauncherStartupRoute::Select(identity.sha256,false,launcherHash).kind==LauncherStartupRoute::Kind::Invalid) {
        report=L"The selected game requires its original, supported Launcher.exe in the same folder.";
        return false;
    }
    for(const auto* file:{L"LaunchVR.exe",L"KaneLynchVR.dll",L"vr.ini"})
        if(!GameRenderer::Regular(Kl1LauncherPaths::Join(folder,file))){report=L"Missing package file: "+std::wstring(file);return false;}
    const auto renderer=RendererIdentity::Parse(model.Get(L"renderer").value.c_str());
    if(renderer!=GameRenderer::Kind::Original&&!GameRenderer::Bundle(folder,report,renderer))return false;
    Kl1LaunchConnection::Plan plan;
    if(!Kl1LaunchConnection::Prepare(folder,SelectedConnection(model),model.Get(L"runtime").value,MetaRuntime::Installed(true),plan,report))return false;
    report=L"Game, renderer and selected VR host verified.\r\nConnection: "+std::wstring(VrModConnection::Label(plan.connection.mode));
    if(identity.metadataVariant)report+=L"\r\nWarning: this exact EXE hash is new. Complete native code, data, resources and loader layout match; only audited PE metadata differs. This variant has not been tested in gameplay.";
    if(!plan.connection.runtimeJson.empty())report+=L"\r\nOpenXR runtime: "+plan.connection.runtimeJson;
    report+=L"\r\nConnect the headset through the selected PC application before playing. This check does not start a game or headset session.";return true;
}
inline bool Create(const std::wstring& folder,App& app,std::wstring& error) {
    auto session=std::make_shared<Session>();session->folder=folder;session->files.resize(2);
    if(!VrModIni::Load(Kl1LauncherPaths::Join(folder,L"vr.ini"),session->files[0],error,true)||
       !VrModIni::Load(Kl1LauncherPaths::Join(folder,L"launcher.ini"),session->files[1],error))return false;
    app.id=Kl1LauncherPaths::SettingsInstanceId(folder);app.title=L"Kane & Lynch VR " KL1_RELEASE_VERSION_W;app.heading=L"KANE & LYNCH  /  VR";
    app.description=KL1_RELEASE_VERSION_W L" - gameplay options are available in the game under Options > VR Settings.";
    const auto game=Kl1LauncherPaths::Default(folder,VrModIni::Read(session->files[1],L"Launcher",L"GameExe",L""),
        Kl1LauncherPaths::Discover(),VrModIni::Read(session->files[0],L"Launcher",L"GamePath",L""));
    const auto connection=VrModConnection::Parse(VrModIni::Read(session->files[0],L"Connection",L"Mode",L"Auto"));
    if(connection==VrModConnection::Mode::Invalid){error=L"Unknown headset connection in vr.ini. Supported profiles: Auto, Meta, VDXR, SteamVR and OpenXR.";return false;}
    const auto rendererKind=RendererIdentity::Parse(VrModIni::Read(session->files[0],L"Launcher",L"Renderer",L"D3D9On12").c_str());
    if(rendererKind==GameRenderer::Kind::Unknown){error=L"Unknown renderer in vr.ini. Use Original, DXVK or D3D9On12.";return false;}
    const auto renderer=RendererIdentity::Name(rendererKind);
    app.model.pages.push_back({L"connection",L"Connection & game",{
        {L"game",L"Game executable or folder",L"Find game detects Steam. You can also select KANEANDLYNCH.EXE or its folder.",game,Kind::File,{},true},
        {L"connection",L"Headset connection",L"Start Quest Link, Virtual Desktop Streamer, or SteamVR before playing.",VrModConnection::Label(connection),Kind::Choice,Connections(),true},
        {L"renderer",L"Game renderer",L"D3D9On12: GPU transfer. DXVK: Vulkan. Original: Direct3D 9. Change graphics and resolution inside the game.",renderer,Kind::Choice,{L"Original",L"DXVK",L"D3D9On12"},true},
        {L"runtime",L"OpenXR runtime JSON (optional)",L"Leave blank for automatic detection, or select the runtime JSON.",VrModIni::Read(session->files[0],L"Connection",L"RuntimeJson",L""),Kind::File,{},VrModConnection::IsOpenXR(connection)}}});
    Page movement{L"movement",L"Movement",{}};
    const auto turnMode=VrModIni::Integer(session->files[0],L"Controls",L"TurnMode",2);
    movement.fields.push_back({L"turnMode",L"Right stick turn mode",L"Smooth and snap turning keep the first-person heading independent of native camera orbit.",TurnLabel(turnMode),Kind::Choice,{TurnLabel(2),TurnLabel(1),TurnLabel(0)},true});
    for(const auto& setting:NumberSettings){
        int value=VrModIni::Integer(session->files[0],setting.section,setting.key,setting.fallback);
        if(value>setting.maximum||value<setting.minimum)value=setting.fallback;
        movement.fields.push_back({setting.id,setting.label,setting.help,std::to_wstring(value),Kind::Text,{},true});
    }
    app.model.pages.push_back(std::move(movement));
    app.model.pages.push_back({L"diagnostics",L"Diagnostics",{
        {L"diagnosticLogs",L"Enable diagnostic logs",L"Record startup, performance, crashes and render evidence for troubleshooting. Off during normal play. Restart the game after changing this setting.",
         VrModIni::Integer(session->files[0],L"Diagnostics",L"Enabled",0)?L"1":L"0",Kind::Boolean,{},true}}});
    app.model.pages.push_back({L"help",L"Controls",{
        {L"controls",L"Controller controls",L"Left stick: move; click toggles walk/run. Right stick: VR turn; click: recenter.\r\nRight trigger: fire. Left trigger does not enter native aim.\r\nA short release: reload; hold 350ms while moving: sprint.\r\nB tap: crouch/stand; hold B to climb.\r\nX: action.\r\nY tap: switch weapon. Hold Y to show inventory, map and team. Right stick selects weapons; release Y to equip and close.\r\nWhile holding Y: press Right Grip to cycle through recipients; A recalls, B moves to the aiming point, right trigger orders attack.\r\nNear a teammate, repeat a centred stick excursion for native weapon exchange.\r\nLeft grip near fore-end: two-hand hold outside the panels.\r\nOutside the panels, right grip hold: grenade; release: throw. While preparing: Y changes type, X cancels.\r\nMenu: pause. Native menus: A/trigger confirm, B/Y back, X additional action.",L"",Kind::Note,{},true}}});
    app.discover=[] {return Kl1LauncherPaths::Discover();};
    app.changed=Changed;
    app.validate=[folder](Model& model,std::wstring& message){return Validate(folder,model,message);};
    app.save=[session](Model& model,std::wstring& message) {
        if(!Validate(session->folder,model,message))return false;
        auto next=session->files;
        VrModIni::Patch(next[1],L"Launcher",L"GameExe",Kl1LauncherPaths::Portable(session->folder,model.Get(L"game").value,Kl1LauncherPaths::Discover()),false);
        VrModIni::Patch(next[0],L"Launcher",L"Renderer",model.Get(L"renderer").value);
        VrModIni::Patch(next[0],L"Launcher",L"GamePath",L"",false);
        VrModIni::Patch(next[0],L"Connection",L"Mode",VrModConnection::Name(SelectedConnection(model)));
        const auto runtime=VrModIni::Trim(model.Get(L"runtime").value);
        const auto portableRuntime=runtime.empty()?std::wstring{}:Kl1LauncherPaths::PortableFile(session->folder,runtime);
        VrModIni::Patch(next[0],L"Connection",L"RuntimeJson",!runtime.empty()&&portableRuntime.empty()?runtime:portableRuntime,false);
        PatchSettings(next[0],model);
        VrModIni::Patch(next[0],L"VR",L"GPUTransport",L"1");
        VrModIni::Patch(next[0],L"VR",L"ForcePanel",L"0");
        VrModIni::Patch(next[0],L"Launcher",L"GraphicsProfile",L"Keep");
        VrModIni::Patch(next[0],L"Launcher",L"NativeResolution",L"1");
        return VrModIni::Save(session->files,std::move(next),message);
    };
    app.inspect=[folder](const Model& model,std::wstring& report){return Inspect(folder,model,report);};
    app.launch=[folder](const Model& model,std::wstring& report,std::function<void(const std::wstring&)> progress) {
        if(!Inspect(folder,model,report))return false;
        const auto launcher=Kl1LauncherPaths::Join(folder,L"LaunchVR.exe");
        auto command=VrModIniGame::Quote(launcher)+L" "+VrModIniGame::Quote(Kl1LauncherPaths::Resolve(folder,model.Get(L"game").value));
        progress(L"Starting the game through its edition-specific launcher...");
        Kl1LauncherChild::Result child;
        if(!Kl1LauncherChild::Run(launcher,std::move(command),folder,child,progress)) {
            report=L"Could not run LaunchVR.exe. Windows error "+std::to_wstring(child.error);
            const auto output=Kl1LauncherChild::Text(child);if(!output.empty())report+=L"\r\n"+output;return false;
        }
        const bool okay=child.exitCode==0;report=okay?L"The game launcher completed. Put on the headset to play.":L"Launch failed (code "+std::to_wstring(child.exitCode)+L").";
        const auto output=Kl1LauncherChild::Text(child);if(!output.empty())report+=L"\r\n"+output;return okay;
    };
    return ValidateModel(app.model,error);
}
}
