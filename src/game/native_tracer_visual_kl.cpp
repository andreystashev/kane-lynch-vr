#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_tracer_visual_kl.hpp"
#include <atomic>

namespace NativeTracerVisualKL {namespace {
std::uintptr_t image=0;std::atomic<bool> installed{false};
std::atomic<std::uint64_t> emits{0},captured{0},referenceMatched{0},rejected{0},producerReads{0},producerRejected{0};
SRWLOCK lock=SRWLOCK_INIT;Packets packets;
bool Read(std::uintptr_t at,void* out,std::size_t bytes){SIZE_T got=0;return at>=0x10000u&&at<=0xFFFFFFFFu-bytes&&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,bytes,&got)&&got==bytes;}
StackRange CurrentStack(){
    const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    return {reinterpret_cast<std::uintptr_t>(tib->StackLimit),reinterpret_cast<std::uintptr_t>(tib->StackBase)};
}
Producer CaptureImplementation(std::uintptr_t manager,std::uint32_t caller,unsigned order,unsigned flags,unsigned extra,
    std::uintptr_t transform,std::uintptr_t position){
    if(!installed||caller!=EmitCallerRva||order!=9||flags||extra)return {};
    ++producerReads;const auto result=CaptureProducer(manager,caller,order,flags,extra,transform,position,CurrentStack(),Read);
    if(!result.valid)++producerRejected;return result;
}
}
void ObserveImplementation(std::uintptr_t,std::uintptr_t,std::uint32_t,unsigned,unsigned,unsigned,const Producer&);
bool ReferenceImplementation(std::uintptr_t,std::uintptr_t,std::uintptr_t,unsigned,unsigned,unsigned,NativeCameraMath::CameraPose&);
bool Install(std::uintptr_t base,LogCallback log){
    if(installed)return true;
    if(sizeof(void*)!=4||!Verify(base,Read)){if(log)log("KL1 tracer visual: native camera/emitter signatures differ; original tracer retained.");return false;}
    image=base;installed=true;producerReader=CaptureImplementation;emissionObserver=ObserveImplementation;referenceReader=ReferenceImplementation;
    if(log)log("KL1 tracer visual: exact native emitter stack recovers the source camera without a tracer Draw detour; projectile state retained.");return true;
}
void ObserveImplementation(std::uintptr_t entry,std::uintptr_t manager,std::uint32_t caller,unsigned order,unsigned flags,unsigned extra,const Producer& source){
    if(!installed)return;++emits;Producer producer;
    if(source.valid){if(ProducerStillCurrent(source,manager,caller,order,flags,extra,CurrentStack(),Read))producer=source;
        else ++producerRejected;}
    AcquireSRWLockExclusive(&lock);
    const bool accepted=packets.Observe(image,producer,entry,manager,caller,order,flags,extra,Read);ReleaseSRWLockExclusive(&lock);
    if(accepted)++captured;
}
bool ReferenceImplementation(std::uintptr_t entry,std::uintptr_t geometry,std::uintptr_t records,unsigned type,unsigned first,unsigned count,NativeCameraMath::CameraPose& output){
    if(!installed)return false;AcquireSRWLockShared(&lock);
    const bool valid=packets.Reference(image,entry,geometry,records,type,first,count,Read,output);ReleaseSRWLockShared(&lock);
    if(valid)++referenceMatched;else ++rejected;return valid;
}
Stats Snapshot(){return {installed.load(),0,emits.load(),captured.load(),referenceMatched.load(),rejected.load(),producerReads.load(),producerRejected.load()};}
}
