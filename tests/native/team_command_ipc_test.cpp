#include "team_command_ipc.hpp"
#include "vr_ipc.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
unsigned checks=0;
void Check(bool okay,const char* text){++checks;if(!okay){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);}}
using namespace TeamCommandIPC;
Shared Frame() {
    Shared shared;shared.magic=Magic;shared.version=Version;shared.bytes=sizeof(shared);shared.gamePid=123;
    return shared;
}
ReceiverCertificate Certificate(std::uint32_t now=100) {
    ReceiverCertificate value;value.player=0x100000;value.control=0x110000;value.receiver=0x120000;
    value.nativeId=7;value.tick=now;value.recenter=5;value.valid=true;return value;
}
void DataContracts() {
    auto shared=Frame();auto receiver=Certificate();ReceiverCertificate copied;
    Check(sizeof(VRIPC::SharedFrames)==512&&sizeof(Shared)==192,"command side-channel leaves image IPC512 bytes unchanged");
    Check(!ReadReceiver(shared,123,100,5,copied),"unpublished receiver cannot authorize a command");
    Publish(shared.receiver,receiver);
    Check(ReadReceiver(shared,123,100,5,copied)&&SameReceiver(copied,receiver),"coherent fresh displayed receiver roundtrips");
    Check(!ReadReceiver(shared,123,301,5,copied),"stale displayed receiver is rejected");
    Check(!ReadReceiver(shared,123,99,5,copied),"future receiver timestamp is rejected");
    Check(!ReadReceiver(shared,124,100,5,copied),"receiver from another PID is rejected");
    Check(!ReadReceiver(shared,123,100,6,copied),"receiver from before recenter is rejected");
    InterlockedIncrement(&shared.receiver.sequence);
    Check(!ReadReceiver(shared,123,100,5,copied),"odd receiver publish cannot be read halfway");
    InterlockedIncrement(&shared.receiver.sequence);
    auto changed=receiver;changed.receiver+=4;Check(!SameReceiver(receiver,changed),"native receiver pointer participates in certificate identity");
    changed=receiver;changed.control+=4;Check(!SameReceiver(receiver,changed),"native control owner participates in certificate identity");
    changed=receiver;changed.nativeId++;Check(!SameReceiver(receiver,changed),"native receiver identity participates in certificate");
    changed=receiver;changed.all=true;changed.receiver=changed.nativeId=0;
    Check(Fresh(changed,100,5)&&!SameReceiver(receiver,changed),"all-receiver certificate is distinct and has no object pointer");
    changed.receiver=4;Check(!Fresh(changed,100,5),"all receiver cannot smuggle an individual pointer");
    changed=receiver;changed.tick=0xfffffff0u;Check(Fresh(changed,10,5),"fresh receiver timestamp survives tick wrap");
    Request request;request.serial=9;request.tick=110;request.recenter=5;request.command=Command::Attack;request.selected=receiver;
    Request output;Publish(shared.request,request);
    Check(ReadRequest(shared,123,110,5,output)&&output.serial==9&&output.command==Command::Attack,"request preserves command serial and selected receiver");
    Check(!ReadRequest(shared,123,361,5,output),"expired command cannot be replayed");
    request.command=Command::None;Publish(shared.request,request);
    Check(!ReadRequest(shared,123,110,5,output),"cancelled mailbox cannot issue an order");
    request.command=static_cast<Command>(99);Publish(shared.request,request);
    Check(!ReadRequest(shared,123,110,5,output),"unknown command rejects without reinterpretation");
    request.command=Command::Recall;request.serial=0;Publish(shared.request,request);
    Check(!ReadRequest(shared,123,110,5,output),"serial zero cannot issue an order");
    request.serial=10;request.recenter=6;Publish(shared.request,request);
    Check(!ReadRequest(shared,123,110,5,output),"request and receiver must share tracking origin");
    request.recenter=5;request.selected.valid=false;Publish(shared.request,request);
    Check(!ReadRequest(shared,123,110,5,output),"undrawn receiver certificate cannot authorize a request");
    Result result;result.serial=9;result.tick=120;result.command=Command::Attack;result.code=ResultCode::NoTarget;result.nativeId=7;
    Publish(shared.result,result);Result answer;
    Check(ReadResult(shared,123,answer)&&answer.serial==9&&answer.code==ResultCode::NoTarget,"native rejection acknowledgement retains exact request serial");
    result.code=static_cast<ResultCode>(99);Publish(shared.result,result);
    Check(!ReadResult(shared,123,answer),"unknown native result is rejected");
    std::uint32_t serial=20;
    Check(PublishCommand(shared,123,110,5,Command::Recall,99,serial)&&serial==21,"production host publisher emits one serial for a fresh displayed receiver");
    Check(ReadRequest(shared,123,110,5,output)&&output.serial==21&&SameReceiver(output.selected,receiver),"production publisher binds command to the displayed certificate");
    Check(!PublishCommand(shared,123,110,5,Command::Move,100,serial)&&serial==21,"receiver drawn before navigation cannot receive a new command");
    Check(!PublishCommand(shared,123,110,5,Command::None,0,serial)&&serial==21,"neutral mapper output cannot publish a command");
    CancelCommand(shared,123,117,serial);
    Check(!ReadRequest(shared,123,117,5,output),"production close-panel cancellation retires the pending request immediately");
    serial=0xffffffffu;
    Check(PublishCommand(shared,123,120,5,Command::Attack,0,serial)&&serial==1,"command serial wrap never publishes forbidden zero");
    shared.version++;
    Check(!ReadReceiver(shared,123,100,5,copied)&&!ReadRequest(shared,123,110,5,output),"incompatible mailbox version never attaches");
}
void ActualMapping() {
    wchar_t name[128];swprintf(name,std::size(name),L"Local\\KL1-TeamCommand-Fixture-%lu-%lu",GetCurrentProcessId(),GetTickCount());
    HANDLE writer=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name);
    Check(writer!=nullptr&&GetLastError()!=ERROR_ALREADY_EXISTS,"fixture creates an isolated page-file mailbox");
    auto* published=static_cast<Shared*>(MapViewOfFile(writer,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
    Check(published!=nullptr,"fixture maps writer");
    const auto initial=Frame();std::memcpy(published,&initial,sizeof(initial));Publish(published->receiver,Certificate());
    HANDLE reader=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);Check(reader!=nullptr,"second endpoint opens same named mailbox");
    auto* observed=static_cast<Shared*>(MapViewOfFile(reader,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));ReceiverCertificate certificate;
    Check(observed&&ReadReceiver(*observed,123,100,5,certificate)&&certificate.nativeId==7,"receiver certificate crosses real mapping views");
    Request request;request.serial=15;request.tick=110;request.recenter=5;request.command=Command::Move;request.selected=certificate;
    Publish(observed->request,request);Request consumed;
    Check(ReadRequest(*published,123,110,5,consumed)&&consumed.serial==15,"host request crosses real mapping into native endpoint");
    Result result;result.serial=15;result.tick=120;result.command=Command::Move;result.code=ResultCode::Accepted;result.point[0]=25;
    Publish(published->result,result);Result reply;
    Check(ReadResult(*observed,123,reply)&&reply.point[0]==25,"native result crosses mapping into host endpoint");
    UnmapViewOfFile(observed);UnmapViewOfFile(published);CloseHandle(reader);CloseHandle(writer);
}
}
int main(){DataContracts();ActualMapping();std::printf("Team command IPC: %u checks PASS; isolated mapping only, no game/runtime.\n",checks);return 0;}
