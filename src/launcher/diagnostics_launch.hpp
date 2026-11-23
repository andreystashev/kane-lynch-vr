#pragma once
#include "diagnostic_session.hpp"
#include <cwchar>

namespace DiagnosticsLaunch {
inline bool Start(const std::wstring& package,HANDLE game,std::wstring* failure=nullptr){
    const auto fail=[&](const wchar_t* text){if(failure)*failure=text;return false;};
    if(!DiagnosticsSession::Enabled(package))return true;
    if(!GetPrivateProfileIntW(L"Diagnostics",L"SessionMonitor",1,DiagnosticsSession::Join(package,L"vr.ini").c_str()))return true;
    const DWORD pid=GetProcessId(game);const auto created=DiagnosticsSession::Creation(game);
    wchar_t image[32768]{};DWORD count=32768;
    if(!pid||!created||!QueryFullProcessImageNameW(game,0,image,&count))return fail(L"Cannot qualify the game process for the session monitor.");
    wchar_t observer[144]{};std::swprintf(observer,144,L"Local\\KaneLynchVR-Observer-%lu-%016llx",pid,static_cast<unsigned long long>(created));
    if(HANDLE existing=OpenMutexW(SYNCHRONIZE,FALSE,observer)){CloseHandle(existing);return true;}
    const auto executable=DiagnosticsSession::Join(package,L"KaneLynchVRMonitor.exe");
    const auto attributes=GetFileAttributesW(executable.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY))return fail(L"KaneLynchVRMonitor.exe is missing beside the mod.");
    std::wstring command=L"\""+executable+L"\" --pid "+std::to_wstring(pid)+L" --created "+std::to_wstring(created)+
        L" --game \""+image+L"\" --package \""+package+L"\"";
    // A terminal backslash must not escape the closing command-line quote.
    if(!package.empty()&&package.back()==L'\\')command.insert(command.size()-1,1,L'\\');
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,package.c_str(),&startup,&child))
        return fail(L"The automatic session monitor could not be started.");
    CloseHandle(child.hThread);CloseHandle(child.hProcess);return true;
}
}
