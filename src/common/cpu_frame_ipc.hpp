#pragma once
#include "vr_ipc.hpp"
#include "stereo_pair_policy.hpp"
#include <cstring>
#include <limits>
// Exact-size CPU pixels are an optional, epoch-qualified side-channel.
// Resizing creates a new arena; old readers retain their original offsets.
namespace CPUFrame {
constexpr std::uint32_t Magic=0x43564C32,Version=3;
struct alignas(64) Shared {
    std::uint32_t magic,version,headerBytes,pid,epoch,width,height,stride;
    std::uint64_t imageBytes,mappingBytes;
    std::uint32_t reserved[4];
};
static_assert(sizeof(Shared)==64&&offsetof(Shared,imageBytes)==32,"CPU arena header ABI");
struct alignas(64) Publication {volatile LONG sequence;std::uint32_t bank;};
struct alignas(64) Bank {volatile LONG sequence;std::uint32_t mask;VRIPC::EyeMetadata metadata[2];};
constexpr unsigned BankCount=2;
constexpr std::size_t PixelOffset=sizeof(Shared)+sizeof(Publication)+BankCount*sizeof(Bank);
static_assert(sizeof(Publication)==64&&sizeof(Bank)==192&&PixelOffset==512,"CPU publication ABI");
struct Snapshot {LONG publication=0,sequence=0;unsigned bank=0,mask=0;VRIPC::EyeMetadata metadata[2]{};};
// CPU readers intentionally map FILE_MAP_READ. Aligned Win32 LONG loads are
// atomic; an Interlocked read-modify-write would fault on that read-only view.
inline LONG Atomic(const volatile LONG& value){const LONG result=value;MemoryBarrier();return result;}
inline const Publication& Published(const Shared* header){return *reinterpret_cast<const Publication*>(reinterpret_cast<const std::uint8_t*>(header)+sizeof(Shared));}
inline const Bank& GetBank(const Shared* header,unsigned bank){return reinterpret_cast<const Bank*>(reinterpret_cast<const std::uint8_t*>(header)+sizeof(Shared)+sizeof(Publication))[bank];}
inline bool Stable(const Shared* header,const Snapshot& value){
    if(!header||value.bank>=BankCount||!value.publication||(value.publication&1)||!value.sequence||(value.sequence&1))return false;
    const auto& publication=Published(header);const auto& bank=GetBank(header,value.bank);MemoryBarrier();
    return Atomic(publication.sequence)==value.publication&&publication.bank==value.bank&&Atomic(bank.sequence)==value.sequence;
}
inline bool ReadSnapshot(const Shared* header,Snapshot& output){
    if(!header)return false;const auto& publication=Published(header);Snapshot next;
    next.publication=Atomic(publication.sequence);next.bank=publication.bank;
    if(!next.publication||(next.publication&1)||next.bank>=BankCount)return false;
    const auto& bank=GetBank(header,next.bank);next.sequence=Atomic(bank.sequence);
    if(!next.sequence||(next.sequence&1))return false;
    next.mask=bank.mask;std::memcpy(next.metadata,bank.metadata,sizeof(next.metadata));
    if((next.mask!=1&&next.mask!=3)||!Stable(header,next))return false;
    output=next;return true;
}
struct Layout {
    std::uint32_t width=0,height=0,stride=0;
    std::size_t imageBytes=0,mappingBytes=0;
};
inline bool Capacity(std::uint32_t width,std::uint32_t height,Layout& output) {
    if(!VRIPC::ValidImageDimensions(width,height,VRIPC::kCPU))return false;
    const std::uint64_t stride=std::uint64_t(width)*4;
    const std::uint64_t image=stride*height,mapping=PixelOffset+BankCount*2*image;
    if(stride>(std::numeric_limits<DWORD>::max)()||mapping>(std::numeric_limits<DWORD>::max)()||
       mapping>(std::numeric_limits<std::size_t>::max)())return false;
    output={width,height,static_cast<std::uint32_t>(stride),static_cast<std::size_t>(image),static_cast<std::size_t>(mapping)};
    return true;
}
inline bool ValidHeader(const Shared& header,DWORD pid,unsigned epoch,Layout& layout) {
    return pid&&epoch&&header.magic==Magic&&header.version==Version&&header.headerBytes==PixelOffset&&
        header.pid==pid&&header.epoch==epoch&&Capacity(header.width,header.height,layout)&&
        header.stride==layout.stride&&header.imageBytes==layout.imageBytes&&header.mappingBytes==layout.mappingBytes;
}
inline void Name(wchar_t* out,std::size_t count,DWORD pid,unsigned epoch){
    swprintf(out,count,L"Local\\KaneLynchVR-CPU-%lu-%u",pid,epoch);
}
struct Writer {
    HANDLE mapping=nullptr;Shared* shared=nullptr;DWORD pid=0;unsigned epoch=0;Layout layout{};
    bool pending=false;unsigned pendingBank=0;
    Writer()=default;Writer(const Writer&)=delete;Writer& operator=(const Writer&)=delete;
    ~Writer(){Close();}
    void Close(){if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);shared=nullptr;mapping=nullptr;pid=epoch=0;layout={};pending=false;pendingBank=0;}
    bool Matches(DWORD process,unsigned generation,std::uint32_t width,std::uint32_t height)const {
        return shared&&pid==process&&epoch==generation&&layout.width==width&&layout.height==height;
    }
    bool Open(DWORD process,unsigned generation,std::uint32_t width,std::uint32_t height){
        Layout wanted;
        if(!process||!generation||!Capacity(width,height,wanted)){SetLastError(ERROR_INVALID_PARAMETER);return false;}
        if(shared&&pid==process&&epoch==generation){
            if(Matches(process,generation,width,height))return true;
            SetLastError(ERROR_INVALID_PARAMETER);return false;
        }
        Close();
        wchar_t name[128]{};Name(name,128,process,generation);
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(wanted.mappingBytes),name);
        if(!mapping)return false;
        if(GetLastError()==ERROR_ALREADY_EXISTS){Close();SetLastError(ERROR_ALREADY_EXISTS);return false;}
        shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,wanted.mappingBytes));
        if(!shared){const auto error=GetLastError();Close();SetLastError(error);return false;}
        // New page-file sections are zeroed by Windows. Touch only metadata;
        // individual eye writes initialize exactly the committed image bytes.
        std::memset(shared,0,PixelOffset);
        shared->version=Version;shared->headerBytes=PixelOffset;shared->pid=process;shared->epoch=generation;
        shared->width=wanted.width;shared->height=wanted.height;shared->stride=wanted.stride;
        shared->imageBytes=wanted.imageBytes;shared->mappingBytes=wanted.mappingBytes;
        MemoryBarrier();shared->magic=Magic;pid=process;epoch=generation;layout=wanted;return true;
    }
    std::uint8_t* Pixels(unsigned side,unsigned bank=0)const {
        if(!shared||side>1||bank>=BankCount||!layout.imageBytes)return nullptr;
        return reinterpret_cast<std::uint8_t*>(shared)+PixelOffset+(bank*2+side)*layout.imageBytes;
    }
    // Copy each source eye exactly once into the unpublished bank. A new LEFT
    // cannot replace either image of the last completed pair. Bank seqlocks
    // reject a host copy raced by a later complete pair reusing that bank.
    template<class Copy> bool WriteEye(unsigned side,const VRIPC::EyeMetadata& metadata,Copy copy,bool& committed){
        committed=false;
        if(!shared||side>1||metadata.width!=layout.width||metadata.height!=layout.height||metadata.stride!=layout.stride||
           !metadata.frameId||metadata.poseValid<VRIPC::kSceneFrame||metadata.poseValid>VRIPC::kContextFrame)return false;
        auto& publication=const_cast<Publication&>(Published(shared));
        const bool scene=metadata.poseValid==VRIPC::kSceneFrame;
        if(scene&&side==1){
            if(!pending)return true;auto& bank=const_cast<Bank&>(GetBank(shared,pendingBank));const auto& left=bank.metadata[0];
            if(!StereoPairPolicy::Compatible(left,metadata)){pending=false;return true;}
            copy(Pixels(1,pendingBank));bank.metadata[1]=metadata;bank.mask=3;
        }else{
            Snapshot old;
            if(!scene&&ReadSnapshot(shared,old)&&old.mask==1&&!std::memcmp(&old.metadata[0],&metadata,sizeof(metadata)))return true;
            pendingBank=Atomic(publication.sequence)?(publication.bank^1u):0u;
            auto& bank=const_cast<Bank&>(GetBank(shared,pendingBank));
            if(!(Atomic(bank.sequence)&1))InterlockedIncrement(&bank.sequence);
            bank.mask=1;bank.metadata[0]=metadata;bank.metadata[1]=metadata;copy(Pixels(0,pendingBank));
            pending=scene;
            if(scene)return true;
        }
        auto& bank=const_cast<Bank&>(GetBank(shared,pendingBank));
        MemoryBarrier();InterlockedIncrement(&bank.sequence);
        InterlockedIncrement(&publication.sequence);publication.bank=pendingBank;
        MemoryBarrier();InterlockedIncrement(&publication.sequence);pending=false;committed=true;return true;
    }
};
struct Reader {
    HANDLE mapping=nullptr;const Shared* shared=nullptr;DWORD pid=0;unsigned epoch=0;
    Layout layout{};Shared header{};
    Reader()=default;Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
    ~Reader(){Close();}
    void Close(){if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);shared=nullptr;mapping=nullptr;pid=epoch=0;layout={};header={};}
    bool Open(DWORD process,unsigned generation){
        if(shared&&pid==process&&epoch==generation)return !std::memcmp(shared,&header,sizeof(header));
        Close();if(!process||!generation)return false;
        wchar_t name[128]{};Name(name,128,process,generation);
        mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);if(!mapping)return false;
        const auto* prefix=static_cast<const Shared*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(Shared)));
        if(!prefix){Close();return false;}
        Shared snapshot;std::memcpy(&snapshot,prefix,sizeof(snapshot));UnmapViewOfFile(prefix);
        Layout wanted;if(!ValidHeader(snapshot,process,generation,wanted)){Close();return false;}
        shared=static_cast<const Shared*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,wanted.mappingBytes));
        if(!shared||std::memcmp(shared,&snapshot,sizeof(snapshot))){Close();return false;}
        header=snapshot;layout=wanted;pid=process;epoch=generation;return true;
    }
    const std::uint8_t* Pixels(unsigned side,const VRIPC::EyeMetadata& metadata,unsigned bank=0)const {
        if(!shared||side>1||bank>=BankCount||metadata.width!=layout.width||metadata.height!=layout.height||
           metadata.stride!=layout.stride||std::memcmp(shared,&header,sizeof(header)))return nullptr;
        const std::uint64_t bytes=std::uint64_t(metadata.stride)*metadata.height;
        const std::uint64_t offset=PixelOffset+(bank*2+side)*std::uint64_t(layout.imageBytes);
        if(bytes!=layout.imageBytes||offset>layout.mappingBytes||bytes>layout.mappingBytes-offset)return nullptr;
        return reinterpret_cast<const std::uint8_t*>(shared)+static_cast<std::size_t>(offset);
    }
    bool SnapshotPublished(Snapshot& value)const{return shared&&!std::memcmp(shared,&header,sizeof(header))&&ReadSnapshot(shared,value);}
    bool StillPublished(const Snapshot& value)const{return shared&&!std::memcmp(shared,&header,sizeof(header))&&Stable(shared,value);}
    const std::uint8_t* Pixels(const Snapshot& value,unsigned side)const{
        return side<2&&StillPublished(value)?Pixels(value.mask==1?0:side,value.metadata[side],value.bank):nullptr;
    }
};
}
