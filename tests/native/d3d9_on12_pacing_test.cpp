#include "d3d9_on12_pacing.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0,queries=0,interfaces=0,live=0,issues=0,flush=0;
bool supported=true,pending=false,issueFail=false,dataFail=false;
void Check(bool value,const char* text){++checks;if(!value){std::printf("FAIL %s\n",text);std::exit(1);}}
struct Bridge final:Device9On12 {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) noexcept override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() noexcept override{return 2;}
    ULONG STDMETHODCALLTYPE Release() noexcept override{return 1;}
    HRESULT STDMETHODCALLTYPE GetD3D12Device(REFIID,void**) noexcept override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE UnwrapUnderlyingResource(IDirect3DResource9*,ID3D12CommandQueue*,REFIID,void**) noexcept override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReturnUnderlyingResource(IDirect3DResource9*,UINT,UINT64*,ID3D12Fence**) noexcept override{return E_NOTIMPL;}
} bridge;
struct Query final:IDirect3DQuery9 {
    ULONG refs=1;Query(){++live;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) noexcept override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() noexcept override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() noexcept override{if(--refs)return refs;--live;delete this;return 0;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) noexcept override{return E_NOTIMPL;}
    D3DQUERYTYPE STDMETHODCALLTYPE GetType() noexcept override{return D3DQUERYTYPE_EVENT;}
    DWORD STDMETHODCALLTYPE GetDataSize() noexcept override{return sizeof(BOOL);}
    HRESULT STDMETHODCALLTYPE Issue(DWORD flags) noexcept override{Check(flags==D3DISSUE_END,"only native event END is submitted");++issues;return issueFail?E_FAIL:S_OK;}
    HRESULT STDMETHODCALLTYPE GetData(void* output,DWORD bytes,DWORD flags) noexcept override{
        if(flags)++flush;Check(!output&&!bytes&&!flags,"pacing polls without data readback or FLUSH");return dataFail?E_FAIL:pending?S_FALSE:S_OK;}
};
HRESULT STDMETHODCALLTYPE Interface(IDirect3DDevice9*,REFIID iid,void** output){++interfaces;Check(IsEqualGUID(iid,iid9on12),"pacing probes only official On12 interop capability");*output=supported?&bridge:nullptr;return supported?S_OK:E_NOINTERFACE;}
HRESULT STDMETHODCALLTYPE Create(IDirect3DDevice9*,D3DQUERYTYPE type,IDirect3DQuery9** output){++queries;Check(type==D3DQUERYTYPE_EVENT,"pacer never allocates timestamps or textures");*output=new Query;return S_OK;}
struct Device {void* methods[119]{};void** table=methods;Device(){methods[0]=reinterpret_cast<void*>(&Interface);methods[118]=reinterpret_cast<void*>(&Create);}IDirect3DDevice9* Value(){return reinterpret_cast<IDirect3DDevice9*>(&table);}};
}
int main(){
    using namespace D3D9On12Pacing;Device fixture;auto* device=fixture.Value();
    Reset();Configure(false);Check(!BeforePresent(device,true)&&!interfaces,"disabled pacer changes no driver state");
    Configure(true);Check(!BeforePresent(device,false)&&!interfaces,"menu or untracked frames never pace");
    supported=false;Check(!BeforePresent(device,true)&&queries==0,"Original and DXVK reject On12-only pacing");
    Check(!BeforePresent(device,true)&&interfaces==1,"unsupported device result is cached without per-frame COM probes");
    Reset();supported=true;
    Check(BeforePresent(device,true)&&issues==1,"first rendered frame submits its own event");
    Check(AfterPresent(device,true)==0&&Snapshot().pending==1,"first frame does not wait for itself");
    Check(BeforePresent(device,true)&&issues==2,"second rendered frame gets the second event");
    AfterPresent(device,true);Check(Snapshot().completed==1&&Snapshot().pending==1&&queries==2,"a completed oldest event bounds render-ahead with only two resources");
    pending=true;Check(BeforePresent(device,true),"next frame can reuse the completed query");
    AfterPresent(device,true);const auto before=issues;
    Check(Snapshot().budgetExhausted==1&&Snapshot().pending==2,"GPU stall returns after bounded budget while retaining unfinished events");
    Check(BeforePresent(device,true)&&issues==before&&Snapshot().skipped==1,"budget exhaustion cannot reissue an unfinished query or stop native rendering");
    pending=false;AfterPresent(device,true);Check(Snapshot().pending==1,"GPU recovery releases the oldest pending event");
    Configure(false);const auto polls=Snapshot().polls;AfterPresent(device,true);Check(Snapshot().polls==polls,"live disable immediately stops polling");
    Reset();Check(!live&&!Snapshot().pending,"Reset releases every query without waiting for the GPU");
    Configure(true);issueFail=true;Check(!BeforePresent(device,true)&&Snapshot().failures>=1,"query issue failure leaves the normal rendering path available");
    const auto failedIssues=issues;Check(!BeforePresent(device,true)&&issues==failedIssues,"failed query path is quarantined until Reset");
    Reset();issueFail=false;BeforePresent(device,true);BeforePresent(device,true);dataFail=true;AfterPresent(device,true);
    Check(!Snapshot().supported&&!BeforePresent(device,true),"poll failures also quarantine pacing instead of repeated driver calls");
    Reset();Check(!live&&!flush,"all fixture resources retire and no GetData FLUSH was issued");
    std::printf("Production On12 queue pacing: %u checks PASS; fake COM only.\n",checks);return 0;
}
