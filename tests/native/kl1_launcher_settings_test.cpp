#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "kl1_launcher_app.hpp"
#include <cstdio>

namespace {
unsigned failures=0;
void Check(bool okay,const char* message){if(!okay){std::printf("FAIL: %s\n",message);++failures;}}
struct TemporaryFolder {
    std::wstring path;
    TemporaryFolder(){wchar_t root[MAX_PATH]{},name[MAX_PATH]{};if(GetTempPathW(MAX_PATH,root)&&GetTempFileNameW(root,L"kl1",0,name)){DeleteFileW(name);if(CreateDirectoryW(name,nullptr))path=name;}}
    ~TemporaryFolder(){if(!path.empty()){DeleteFileW(Kl1LauncherPaths::Join(path,L"vr.ini").c_str());DeleteFileW(Kl1LauncherPaths::Join(path,L"KANEANDLYNCH.EXE").c_str());DeleteFileW(Kl1LauncherPaths::Join(path,L"legacy.exe").c_str());RemoveDirectoryW(Kl1LauncherPaths::Join(path,L"package").c_str());RemoveDirectoryW(path.c_str());}}
};
}
int main(){
    TemporaryFolder temporary;if(temporary.path.empty())return 2;
    const auto ini=Kl1LauncherPaths::Join(temporary.path,L"vr.ini");
    Check(WritePrivateProfileStringW(L"Launcher",L"Renderer",L"Original",ini.c_str()),"minimal public configuration");
    Check(WritePrivateProfileStringW(L"Launcher",L"Renderer",L"d3d9on12",ini.c_str()),"write temporary renderer");
    Check(WritePrivateProfileStringW(L"Controls",L"TurnMode",L"1",ini.c_str()),"write snap preference");
    Check(WritePrivateProfileStringW(L"Controls",L"SnapTurnDegrees",L"45",ini.c_str()),"write custom snap angle");
    Check(WritePrivateProfileStringW(L"VR",L"HeadPositionMm",L"0",ini.c_str()),"write hidden translation preference");
    VrModLauncher::App app;std::wstring error;
    if(!Kl1LauncherApp::Create(temporary.path,app,error)){std::printf("FAIL: cannot create launcher model\n");return 2;}
    auto& model=app.model;
    Check(app.id==Kl1LauncherPaths::SettingsInstanceId(temporary.path)&&app.title==L"Kane & Lynch VR " KL1_RELEASE_VERSION_W,
        "settings singleton is scoped to this package while the visible title is unchanged");
    auto alternateCase=temporary.path;for(auto& c:alternateCase)if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';
    auto alternateSlashes=temporary.path;for(auto& c:alternateSlashes)if(c==L'\\')c=L'/';
    Check(Kl1LauncherPaths::SettingsInstanceId(alternateCase)==app.id,"same package casing keeps one singleton");
    Check(Kl1LauncherPaths::SettingsInstanceId(alternateSlashes+L"/")==app.id,"same package slash and trailing separator keep one singleton");
    Check(Kl1LauncherPaths::SettingsInstanceId(temporary.path+L"\\unused\\..")==app.id,"canonical dot segments keep one singleton");
    Check(Kl1LauncherPaths::SettingsInstanceId(L"relative-folder").empty(),"relative singleton paths are rejected without depending on shell cwd");
    const auto secondId=Kl1LauncherPaths::SettingsInstanceId(temporary.path+L"\\second-package");
    Check(!secondId.empty()&&secondId!=app.id,"different package directories have independent singleton IDs");
    const auto mutexName=L"Local\\VrModLauncher-"+app.id,secondName=L"Local\\VrModLauncher-"+secondId;
    HANDLE first=CreateMutexW(nullptr,FALSE,mutexName.c_str());const auto firstError=GetLastError();
    HANDLE repeated=CreateMutexW(nullptr,FALSE,mutexName.c_str());const auto repeatedError=GetLastError();
    HANDLE separate=CreateMutexW(nullptr,FALSE,secondName.c_str());const auto separateError=GetLastError();
    Check(first&&firstError!=ERROR_ALREADY_EXISTS&&repeated&&repeatedError==ERROR_ALREADY_EXISTS&&separate&&separateError!=ERROR_ALREADY_EXISTS,
        "actual Windows mutex deduplicates the same package and permits the other package concurrently");
    if(separate)CloseHandle(separate);if(repeated)CloseHandle(repeated);if(first)CloseHandle(first);
    Check(Kl1LauncherApp::ValidateSettings(model,error),"valid settings without installed game");
    Check(model.Get(L"renderer").value==L"D3D9On12","canonical renderer choice accepts case-insensitive INI");
    Check(Kl1LauncherApp::SelectedTurn(model)==1&&model.Get(L"snapDegrees").value==L"45","preserve custom snap turn preference");
    Check(model.Get(L"diagnosticLogs").value==L"0","diagnostic logging defaults off");
    for (const auto& page : model.pages) {
        Check(page.id != L"modifiers" && page.id != L"weapons", "gameplay modifiers are managed in the game");
        for (const auto& field : page.fields) {
            const auto& id = field.id;
            Check(id != L"testingInvulnerable" && id != L"physicalReload" && id != L"reticleStyle",
                  "gameplay options are absent from the launcher");
        }
    }
    model.Get(L"diagnosticLogs").value=L"1";
    VrModIni::File diagSettings;Kl1LauncherApp::PatchSettings(diagSettings,model);
    Check(VrModIni::Read(diagSettings,L"Diagnostics",L"Enabled",L"0")==L"1","one checkbox persists the diagnostic opt-in");
    model.Get(L"diagnosticLogs").value=L"0";Kl1LauncherApp::PatchSettings(diagSettings,model);
    Check(VrModIni::Read(diagSettings,L"Diagnostics",L"Enabled",L"1")==L"0","checkbox turns off retained probes without deleting their settings");
    Kl1LauncherApp::Changed(model);
    Check(model.Get(L"snapDegrees").enabled&&!model.Get(L"smoothSpeed").enabled,"turn mode enables its numeric comfort control");
    for(const auto* invalid:{L"29",L"241",L"90junk",L"90\n[VR]",L"-90",L"99999999999999999999999"}){
        model.Get(L"smoothSpeed").value=invalid;Check(!Kl1LauncherApp::ValidateSettings(model,error),"reject invalid numeric setting before persistence");
    }
    model.Get(L"smoothSpeed").value=L" 120 ";model.Get(L"turnMode").value=Kl1LauncherApp::TurnLabel(2);
    Check(Kl1LauncherApp::ValidateSettings(model,error),"accept custom smooth speed");
    VrModIni::File settings;settings.text=L"; user comment\n[Controls]\nTurnMode=1 ; keep comment\nCustomAction=42\nEnabled=0\nMovementHeadRelative=0\nImmersiveLayout=0\n[VR]\nHeadPositionMm=0\nNearPlaneMm=75\nStereoMode=0\nNativeCamera=0\nFirstPerson=0\n[Hands]\nNativeArms=0\nHideHead=0\nWeaponAim=0\n[Weapons]\nSupportGrip=0\nPhysicalReload=0\nReticleStyle=0\nMountedReticle=1\nLaserSight=1\n[HUD]\nStereo=0\nHideAuxiliaryPip=0\n[Audio]\nOutput=Default\n";
    const auto original=settings;
    Kl1LauncherApp::PatchSettings(settings,model);
    Check(VrModIni::Integer(settings,L"Weapons",L"PhysicalReload",-1)==0 &&
          VrModIni::Integer(settings,L"Weapons",L"ReticleStyle",-1)==0 &&
          VrModIni::Integer(settings,L"Weapons",L"MountedReticle",-1)==1 &&
          VrModIni::Integer(settings,L"Weapons",L"LaserSight",-1)==1,
          "saving launcher settings preserves gameplay preferences");
    Check(VrModIni::Integer(settings,L"Controls",L"TurnMode",-1)==2&&VrModIni::Integer(settings,L"Controls",L"SmoothTurnDegreesPerSecond",-1)==120,"choice and numeric comfort settings map to engine values");
    Check(settings.text.find(L"; user comment")!=std::wstring::npos&&settings.text.find(L"; keep comment")!=std::wstring::npos,"preserve INI comments");
    for(const auto* section:{L"VR",L"Hands",L"Controls",L"Weapons",L"HUD",L"Audio"})for(const auto* key:{L"HeadPositionMm",L"NearPlaneMm",L"StereoMode",L"NativeCamera",L"FirstPerson",L"NativeArms",L"HideHead",L"WeaponAim",L"Enabled",L"MovementHeadRelative",L"ImmersiveLayout",L"SupportGrip",L"Stereo",L"HideAuxiliaryPip",L"CustomAction",L"Output"}){
        const auto before=VrModIni::Read(original,section,key,L"<absent>");
        Check(VrModIni::Read(settings,section,key,L"<absent>")==before,"saving visible switches neither resets nor creates hidden technical keys");
    }
    Check(VrModIni::Integer(settings,L"VR",L"CinematicVR",-1)==-1 &&
          VrModIni::Integer(settings,L"Debug",L"Invulnerable",-1)==-1 &&
          VrModIni::Integer(settings,L"HUD",L"GameHints",-1)==-1,
          "saving launcher settings does not introduce gameplay options");
    const auto package=Kl1LauncherPaths::Join(temporary.path,L"package");CreateDirectoryW(package.c_str(),nullptr);
    const auto game=Kl1LauncherPaths::Join(temporary.path,L"KANEANDLYNCH.EXE");
    const auto handle=CreateFileW(game.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
    Check(Kl1LauncherPaths::Default(package,L"",L"",L"")==game,"find game executable in parent of package");
    const auto legacy=Kl1LauncherPaths::Join(temporary.path,L"legacy.exe"),missing=Kl1LauncherPaths::Join(temporary.path,L"missing.exe");
    const auto legacyFile=CreateFileW(legacy.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(legacyFile!=INVALID_HANDLE_VALUE)CloseHandle(legacyFile);
    Check(Kl1LauncherPaths::Default(package,L"",game,legacy)==game,"Steam is the first automatic selection before legacy or adjacent copies");
    Check(Kl1LauncherPaths::Default(package,missing,game,legacy)==game,"a stale saved path cannot hide the installed Steam copy");
    Check(Kl1LauncherPaths::Default(package,legacy,game,L"")==legacy,"an explicit installed older edition remains selectable after reopening settings");
    Check(Kl1LauncherPaths::Default(package,L"",missing,legacy)==game,"a missing Steam discovery cannot hide a working adjacent install");
    Check(Kl1LauncherPaths::Default(package,game,temporary.path,L"")==game,"Steam discovery accepts an installation folder as well as executable");
    std::printf("KL1 launcher settings: %s; no game or headset session opened.\n",failures?"FAILED":"passed");return failures?1:0;
}
