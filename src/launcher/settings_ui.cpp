#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "kl1_launcher_app.hpp"
#include "vr_mod_shared/window.hpp"

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,wchar_t*,int) {
    int count=0;auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    const bool play=arguments&&count==2&&std::wcscmp(arguments[1],L"--launch")==0;
    if(arguments)LocalFree(arguments);
    const bool smoke=VrModLauncher::SmokeRequested();VrModLauncher::App app;std::wstring error;
    if(!Kl1LauncherApp::Create(Kl1LauncherPaths::Folder(),app,error)) {
        if(!smoke)MessageBoxW(nullptr,error.c_str(),L"Kane & Lynch VR",MB_OK|MB_ICONERROR);
        return 2;
    }
    // The release Play shortcut uses this Windows-subsystem entry. It shares
    // the exact settings adapter/validation and starts the CLI with NO_WINDOW,
    // avoiding a console that could steal fullscreen focus during Steam start.
    if(play){
        const bool okay=app.validate(app.model,error)&&app.launch(app.model,error,[](const std::wstring&){});
        if(!okay)MessageBoxW(nullptr,error.c_str(),L"Kane & Lynch VR",MB_OK|MB_ICONERROR);
        return okay?0:3;
    }
    return VrModLauncher::Run(instance,std::move(app),smoke);
}
