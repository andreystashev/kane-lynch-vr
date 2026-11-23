#pragma once
#include "native_skin_math.hpp"

// A render-state bone pointer is shared by subsequent rigid/unrelated draws.
// The VR copy belongs only to the body binder which authorized it; WORLD and
// rigid binders do not assign +3BC. Return that field to its original native
// source before crossing the next native object boundary, or shader-package
// scope exit. Never write a field replaced by another native writer.
namespace NativeSkinPaletteLease {
struct Lease {
    std::uintptr_t image=0,state=0,entry=0,geometry=0,player=0,model=0,cache=0,staging=0;
    NativeSkinMath::PaletteSource source{};
    unsigned count=0,depth=0;
    bool active=false;
};
enum class Result {None,Replaced,Restored,Rejected,Fallback};
template<class Reader> bool OriginalCurrent(const Lease& lease,Reader read) {
    const auto get=[&](std::uintptr_t at,auto& value){return NativeSkinMath::SourceAddress(at,sizeof(value))&&read(at,&value,sizeof(value));};
    std::uint32_t cache=0,geometry=0,owner=0,model=0,count=0,vtable=0;
    float sourceProbe[12]{};
    if(!get(lease.entry+0x108,cache)||cache!=lease.cache||!get(lease.entry+0xF4,geometry)||geometry!=lease.geometry||
       !get(geometry+0x60,owner)||owner!=lease.player||!get(owner,vtable)||vtable!=lease.image+0x56582C||
       !get(owner+0x70,model)||model!=lease.model||
       !get(lease.entry+0x114,count)||count!=lease.count||!lease.count||lease.count>NativeRigMath::MaximumBones||
       !NativeSkinMath::SourceAddress(lease.source.palette,lease.count*48)||
       !read(lease.source.palette,sourceProbe,sizeof(sourceProbe)))return false;
    if(lease.source.alternate) {
        struct Row {NativeCameraMath::CameraPose root;std::uint32_t palette,auxiliary;} row{};
        std::uint32_t list=0,rows=0,instances=0;
        if(!get(lease.entry+0x90+lease.source.gpuIndex*4,list)||list!=lease.source.list||
           !get(list,instances)||instances!=lease.source.instances||!get(list+4,rows)||rows!=lease.source.rows||
           !read(rows+lease.source.index*0x38,&row,sizeof(row))||row.palette!=lease.source.palette||
           std::memcmp(&row.root,&lease.source.root,sizeof(row.root)))return false;
    } else if(lease.source.palette!=lease.cache+0x90)return false;
    return true;
}
template<class Reader,class Writer> Result Retire(Lease& current,Reader read,Writer write,std::uintptr_t identityFallback=0) {
    if(!current.active)return Result::None;
    const auto lease=current;current={};std::uint32_t bound=0;
    const auto get=[&](std::uint32_t& value){return NativeSkinMath::SourceAddress(lease.state+0x3BC,4)&&read(lease.state+0x3BC,&value,4);};
    if(!get(bound))return Result::Rejected;
    if(bound!=lease.staging)return Result::Replaced;
    if(!OriginalCurrent(lease,read)) {
        // The queued native cache may have retired. Never resurrect its old
        // pointer, and never leave a proved VR pointer in an unrelated draw.
        // The caller owns a complete immutable identity palette, equivalent
        // to native490DC5's identity rows. A non-null address also changes the
        // native pointer cache, unlike null==cached0 which can skip upload.
        if(!identityFallback||!NativeSkinMath::SourceAddress(identityFallback,NativeRigMath::MaximumBones*48)||!get(bound)||bound!=lease.staging)return Result::Rejected;
        const auto fallback=static_cast<std::uint32_t>(identityFallback);
        return write(lease.state+0x3BC,&fallback,sizeof(fallback))?Result::Fallback:Result::Rejected;
    }
    // Re-read immediately before the only field mutation. A native replacement
    // cannot be overwritten with this retired body's saved palette.
    if(!get(bound)||bound!=lease.staging)return Result::Replaced;
    const auto original=static_cast<std::uint32_t>(lease.source.palette);
    if(!write(lease.state+0x3BC,&original,sizeof(original)))return Result::Rejected;
    return Result::Restored;
}
template<class Reader,class Writer> bool Resume(const Lease& lease,Reader read,Writer write) {
    std::uint32_t bound=0;
    if(!lease.active||!OriginalCurrent(lease,read)||!read(lease.state+0x3BC,&bound,4)||bound!=lease.source.palette)return false;
    const auto staging=static_cast<std::uint32_t>(lease.staging);
    return write(lease.state+0x3BC,&staging,sizeof(staging));
}
}
