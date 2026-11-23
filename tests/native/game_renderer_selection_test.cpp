#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "kl1_launcher_app.hpp"
#include <cstdio>

namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* description){++checks;if(!value){++failures;std::printf("FAIL: %s\n",description);}}
struct TemporaryFolder {
    std::wstring path,ini;
    TemporaryFolder(){wchar_t root[MAX_PATH]{},name[MAX_PATH]{};
        if(GetTempPathW(MAX_PATH,root)&&GetTempFileNameW(root,L"krs",0,name)){
            DeleteFileW(name);if(CreateDirectoryW(name,nullptr)){path=name;ini=GameRenderer::Join(path,L"vr.ini");}}}
    ~TemporaryFolder(){
        if(path.empty())return;
        for(const auto* file:{L"vr.ini",L"d3d9.dll",L".kl1-vr-dxvk-owner",L".kl1-vr-renderer-digests",L"recorded.tmp"})
            DeleteFileW(GameRenderer::Join(path,file).c_str());
        RemoveDirectoryW(path.c_str());
    }
    bool Write(const std::string& bytes){HANDLE file=CreateFileW(ini.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;DWORD written=0;
        const bool okay=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();CloseHandle(file);return okay;}
};
void Select(TemporaryFolder& folder,const std::string& bytes,GameRenderer::Kind expected,bool compareGui=true){
    Check(folder.Write(bytes),"write private test config");GameRenderer::Kind selected=GameRenderer::Kind::Unknown;std::wstring error;
    Check(GameRenderer::Selection(folder.path,selected,error)&&selected==expected,"launch reader selects explicit renderer regardless of encoding");
    if(compareGui){VrModLauncher::App app;Check(Kl1LauncherApp::Create(folder.path,app,error),"GUI loads same private config");
        Check(RendererIdentity::Parse(app.model.Get(L"renderer").value.c_str())==selected,"GUI selection matches launch reader for explicit renderer");}
    VrModIni::File after;Check(VrModIni::Load(folder.ini,after,error,true)&&after.bytes==bytes,"renderer selection preserves original config bytes");
}
void Reject(TemporaryFolder& folder,const std::string& bytes){
    Check(folder.Write(bytes),"write invalid private config");GameRenderer::Kind selected=GameRenderer::Kind::Original;std::wstring error;
    Check(!GameRenderer::Selection(folder.path,selected,error)&&!error.empty(),"invalid config cannot silently launch Original");
}
void VerifyBundleAndSwitch(const std::wstring& package,const std::wstring& isolatedGame) {
    using namespace GameRenderer;
    const auto target=Join(isolatedGame,L"d3d9.dll"),owner=Join(isolatedGame,L".kl1-vr-dxvk-owner");
    const auto receipt=Join(isolatedGame,L".kl1-vr-renderer-digests");
    const auto temp=Join(isolatedGame,L"recorded.tmp");
    const auto identity=Receipt(isolatedGame,kSupportedSteamSha256);
    std::wstring error;
    Check(Bundle(package,error,Kind::DXVK),"the packaged DXVK DLL matches its pinned digest");
    Check(Bundle(package,error,Kind::D3D9On12),"the source-built On12 DLL passes the real launcher bundle check");
    if(!Switch(package,isolatedGame,kSupportedSteamSha256,Kind::D3D9On12,error)){
        Check(false,"install the selected On12 renderer");
        return;
    }
    Check(Hash(target,RendererIdentity::On12DllSha256),"On12 bytes installed from the selected bundle");
    VrModIni::File read;
    Check(LoadReceipt(owner,read)&&read.text==identity,"preserve the original ownership receipt format");
    Check(Switch(package,isolatedGame,kSupportedSteamSha256,Kind::DXVK,error)&&Hash(target,DllSha256),
          "switch from the new On12 bundle to DXVK");
    Check(Switch(package,isolatedGame,kSupportedSteamSha256,Kind::Original,error)&&!Exists(target),
          "switch from DXVK back to the original renderer");

    // Model an older source-built selector that is known only to this game's receipt.
    Check(SaveReceipt(target,temp,L"previous build fixture",false),"write a distinct previous DLL fixture");
    char previous[65]{};
    Check(HashFile(target.c_str(),previous)&&!KnownOwnedDigest(previous),"previous-build digest is unknown to this binary");
    Check(SaveReceipt(receipt,temp,identity+LauncherSettings::Detail::Wide(previous)+L"\r\n",true),
          "record the previously installed selector");
    Check(Switch(package,isolatedGame,kSupportedSteamSha256,Kind::D3D9On12,error)&&Hash(target,RendererIdentity::On12DllSha256),
          "upgrade a renderer recorded by an earlier build");
    Check(LoadReceipt(receipt,read)&&read.text.find(LauncherSettings::Detail::Wide(previous))!=std::wstring::npos,
          "the previous digest survives an interrupted replacement");
    Check(SaveReceipt(target,temp,L"foreign renderer fixture",true),"replace test bytes with a foreign renderer");
    Check(HashFile(target.c_str(),previous),"read foreign fixture digest");
    Check(!Switch(package,isolatedGame,kSupportedSteamSha256,Kind::DXVK,error)&&Hash(target,previous),
          "an old ownership receipt cannot authorize replacement of foreign bytes");
}

}
int wmain(int argc,wchar_t** argv){
    TemporaryFolder folder;if(folder.path.empty())return 2;
    const std::string on12="[Launcher]\r\nRenderer=D3D9On12\r\n[VR]\r\nNativeCamera=1\r\n";
    Select(folder,std::string("\xef\xbb\xbf")+on12,GameRenderer::Kind::D3D9On12);
    Select(folder,on12,GameRenderer::Kind::D3D9On12);
    std::string utf16="\xff\xfe";for(const auto c:on12){utf16+=c;utf16+='\0';}Select(folder,utf16,GameRenderer::Kind::D3D9On12);
    Select(folder,"[launcher]\nrenderer=oRiGiNaL\n",GameRenderer::Kind::Original);
    Select(folder,std::string("\xef\xbb\xbf")+"[LAUNCHER]\nRENDERER=dXvK\n",GameRenderer::Kind::DXVK);
    Select(folder,"[ Launcher ]\n Renderer = \"d3d9ON12\" \n",GameRenderer::Kind::D3D9On12);
    Select(folder,"; comment\n[VR]\nNativeCamera=1\n[Launcher]\nRenderer=DXVK\n",GameRenderer::Kind::DXVK);
    Reject(folder,"[Launcher]\nRenderer=Unsupported\n");
    Reject(folder,"[Launcher]\nRenderer=\n");
    Reject(folder,std::string("\xff\xfe[") );
    Reject(folder,on12+std::string(1,'\0'));
    Reject(folder,std::string(4*1024*1024+1,' '));
    // Preserve direct launch's established default. GUI offers On12 for new
    // settings; this test does not change that separate UI default.
    Select(folder,"",GameRenderer::Kind::Original,false);
    Select(folder,"[VR]\nNativeCamera=1\n",GameRenderer::Kind::Original,false);
    Select(folder,"[Launcher]\nOther=1\n",GameRenderer::Kind::Original,false);
    Check(DeleteFileW(folder.ini.c_str())!=FALSE,"remove only private test config");
    GameRenderer::Kind selected=GameRenderer::Kind::Unknown;std::wstring error;
    Check(GameRenderer::Selection(folder.path,selected,error)&&selected==GameRenderer::Kind::Original,"missing legacy vr.ini keeps Original default");
    Check(folder.Write(on12),"prepare private access-error config");
    HANDLE lock=CreateFileW(folder.ini.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    Check(lock!=INVALID_HANDLE_VALUE,"lock private config for access-error case");
    Check(!GameRenderer::Selection(folder.path,selected,error)&&!error.empty(),"unreadable settings fail visibly");
    if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
    if(argc==2)VerifyBundleAndSwitch(argv[1],folder.path);
    std::printf("game_renderer_selection_test: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
