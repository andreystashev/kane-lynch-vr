#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace NativeBufferFlushKL {
struct Range {std::uint32_t offset=0,bytes=0;};
struct SparseResult {
    bool attempted=false,completed=false,lockFailed=false,unlockFailed=false,invalidMapping=false;
    HRESULT result=S_OK;
};
inline bool EnclosingRange(const Range* ranges,unsigned count,unsigned capacity,Range& span,unsigned alignment=1){
    if(!ranges||count<2||count>=2048||!capacity||!alignment)return false;
    unsigned first=capacity,last=0;
    for(unsigned i=0;i<count;++i){const auto r=ranges[i];
        if(!r.bytes||r.offset>=capacity||r.bytes>capacity-r.offset||r.offset%alignment||r.bytes%alignment)return false;
        if(r.offset<first)first=r.offset;if(r.offset+r.bytes>last)last=r.offset+r.bytes;}
    span={first,last-first};return span.bytes!=0;
}
// Flags0 retains existing GPU bytes. Only original dirty ranges are written;
// bytes in gaps are never copied from possibly stale/uninitialized CPU shadow.
// completed means native count/pending must be cleared, including failed
// Unlock: retry while mapping ownership is unknown would introduce a second Lock.
template<class Lock,class Unlock> SparseResult SparseUpload(const Range* ranges,unsigned count,unsigned capacity,
    const unsigned char* shadow,void*& pending,Lock lock,Unlock unlock,unsigned alignment=1){
    SparseResult out;Range span;
    if(!shadow||!EnclosingRange(ranges,count,capacity,span,alignment))return out;
    void* const previous=pending;out.attempted=true;
    out.result=lock(span.offset,span.bytes,&pending,0);
    if(FAILED(out.result)){out.lockFailed=true;pending=previous;return out;}
    if(!pending){out.invalidMapping=true;const auto hr=unlock();out.unlockFailed=FAILED(hr);
        out.result=out.unlockFailed?hr:E_POINTER;out.completed=out.unlockFailed;pending=out.completed?nullptr:previous;return out;}
    auto* mapped=static_cast<unsigned char*>(pending);
    for(unsigned i=0;i<count;++i)std::memcpy(mapped+(ranges[i].offset-span.offset),shadow+ranges[i].offset,ranges[i].bytes);
    out.result=unlock();out.unlockFailed=FAILED(out.result);out.completed=true;pending=nullptr;return out;
}
// The native worker also uses one range, or count>=2048 as a full-copy flag.
// Its original implementation ignores HRESULT in both cases. Keep its exact
// copy semantics while providing the same checked all-or-none map contract.
template<class Lock,class Unlock> SparseResult CheckedWorkerUpload(const Range* ranges,unsigned count,unsigned capacity,
    const unsigned char* shadow,void*& pending,Lock lock,Unlock unlock,unsigned alignment=1){
    if(count>1&&count<2048)return SparseUpload(ranges,count,capacity,shadow,pending,lock,unlock,alignment);
    SparseResult out;if(!shadow||!capacity||!count||!alignment)return out;
    const Range span=count>=2048?Range{0,capacity}:ranges?*ranges:Range{};
    if(!span.bytes||span.offset>=capacity||span.bytes>capacity-span.offset||span.offset%alignment||span.bytes%alignment)return out;
    void* const previous=pending;out.attempted=true;
    out.result=lock(span.offset,span.bytes,&pending,0);
    if(FAILED(out.result)){out.lockFailed=true;pending=previous;return out;}
    if(!pending){out.invalidMapping=true;const auto hr=unlock();out.unlockFailed=FAILED(hr);
        out.result=out.unlockFailed?hr:E_POINTER;out.completed=out.unlockFailed;pending=out.completed?nullptr:previous;return out;}
    std::memcpy(pending,shadow+span.offset,span.bytes);
    out.result=unlock();out.unlockFailed=FAILED(out.result);out.completed=true;pending=nullptr;return out;
}
// Same ordered per-range maps as the native worker. Used when wide sparse
// optimization is disabled (including DXVK). A failure keeps the entire CPU
// queue: already copied ranges are idempotent on retry, and the affected GPU
// buffer is gated until every range has completed.
template<class Lock,class Unlock> SparseResult CheckedNativeUpload(const Range* ranges,unsigned count,unsigned capacity,
    const unsigned char* shadow,void*& pending,Lock lock,Unlock unlock,unsigned alignment=1){
    if(count>=2048||count==1)return CheckedWorkerUpload(ranges,count,capacity,shadow,pending,lock,unlock,alignment);
    SparseResult out;Range unused;if(!shadow||!EnclosingRange(ranges,count,capacity,unused,alignment))return out;
    void* const previous=pending;out.attempted=true;
    for(unsigned i=0;i<count;++i){const auto range=ranges[i];out.result=lock(range.offset,range.bytes,&pending,0);
        if(FAILED(out.result)){out.lockFailed=true;pending=previous;return out;}
        if(!pending){out.invalidMapping=true;const auto hr=unlock();out.unlockFailed=FAILED(hr);
            out.result=out.unlockFailed?hr:E_POINTER;out.completed=out.unlockFailed;pending=out.completed?nullptr:previous;return out;}
        std::memcpy(pending,shadow+range.offset,range.bytes);
        out.result=unlock();out.unlockFailed=FAILED(out.result);
        if(out.unlockFailed){out.completed=true;pending=nullptr;return out;}pending=previous;
    }
    out.completed=true;pending=nullptr;return out;
}
}
