#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "cpu_frame_ipc.hpp"
#include <cstdio>
#include <utility>

namespace {
unsigned checks=0,failures=0;
void Check(bool okay,const char* message){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",message);}}
VRIPC::EyeMetadata Metadata(unsigned width,unsigned height){VRIPC::EyeMetadata value{};value.width=width;value.height=height;value.stride=width*4;return value;}
void Layouts() {
    CPUFrame::Layout layout;
    for(const auto size:{std::pair<unsigned,unsigned>{1920,1080},{3840,2160},{5120,2880},{7680,4320},{16384,8192}}) {
        const auto width=size.first,height=size.second;
        Check(CPUFrame::Capacity(width,height,layout)&&layout.width==width&&layout.height==height&&layout.stride==width*4&&
              layout.imageBytes==std::uint64_t(width)*height*4&&layout.mappingBytes==CPUFrame::PixelOffset+std::uint64_t(width)*height*16,
              "1080p, 4K, 5K, 8K and wide16K use exact published and pending banks without downscaling");
    }
    Check(CPUFrame::Capacity(1920,1080,layout)&&layout.mappingBytes==33178112,
          "1080p allocates exactly four native images and bounded publication metadata");
    for(const auto size:{std::pair<unsigned,unsigned>{0,1080},{1920,0},{15,16},{16,15},{16385,2160},{3840,16385},{16384,16384},{0xffffffffu,0xffffffffu}}) {
        const auto before=layout;
        Check(!CPUFrame::Capacity(size.first,size.second,layout)&&layout.mappingBytes==before.mappingBytes,
              "zero, undersized, out-of-device bounds and overflowing dimensions leave output untouched");
    }
    Check(sizeof(CPUFrame::Shared)==64&&CPUFrame::PixelOffset==512&&VRIPC::kVersion==16&&CPUFrame::Version==3,
          "completed CPU banks have a distinct payload ABI with unchanged compact control metadata");
}
void RoundTripAndResize() {
    const DWORD pid=GetCurrentProcessId();CPUFrame::Writer writer,duplicate;CPUFrame::Reader oldReader,reader;
    const auto fullHD=Metadata(1920,1080),fourK=Metadata(3840,2160),fiveK=Metadata(5120,2880);
    Check(writer.Open(pid,1,1920,1080)&&oldReader.Open(pid,1),"native 1080p writer and read-only reader connect");
    if(!writer.shared||!oldReader.shared)return;
    writer.Pixels(0)[0]=11;writer.Pixels(0)[writer.layout.imageBytes-1]=12;
    writer.Pixels(1)[0]=21;writer.Pixels(1)[writer.layout.imageBytes-1]=22;
    const auto* oldLeft=oldReader.Pixels(0,fullHD);const auto* oldRight=oldReader.Pixels(1,fullHD);
    Check(oldLeft&&oldRight&&oldLeft[0]==11&&oldLeft[oldReader.layout.imageBytes-1]==12&&
          oldRight[0]==21&&oldRight[oldReader.layout.imageBytes-1]==22,
          "both native eye boundaries are writable and visible without crossing the mapped arena");
    Check(!duplicate.Open(pid,1,1920,1080)&&GetLastError()==ERROR_ALREADY_EXISTS&&oldReader.Pixels(0,fullHD)[0]==11,
          "another writer cannot overwrite an active arena with the same PID and epoch");
    Check(writer.Open(pid,1,1920,1080)&&writer.Pixels(1)[0]==21,"same-size reopen preserves existing image bytes");
    Check(!writer.Open(pid,1,3840,2160)&&writer.Matches(pid,1,1920,1080)&&oldReader.Pixels(1,fullHD)[0]==21,
          "resize cannot mutate offsets in an existing epoch");
    Check(writer.Open(pid,2,3840,2160)&&reader.Open(pid,2)&&writer.layout.mappingBytes==132710912,
          "4K resize creates a new exact-size epoch-qualified arena");
    if(!writer.shared||!reader.shared)return;
    writer.Pixels(0)[0]=31;writer.Pixels(1)[writer.layout.imageBytes-1]=32;
    Check(reader.Pixels(0,fourK)&&reader.Pixels(0,fourK)[0]==31&&reader.Pixels(1,fourK)[reader.layout.imageBytes-1]==32,
          "4K eye offsets follow the resized arena rather than the old 1080p stride");
    Check(oldReader.Pixels(0,fullHD)&&oldReader.Pixels(0,fullHD)[0]==11&&oldReader.Pixels(1,fullHD)[oldReader.layout.imageBytes-1]==22,
          "old reader retains immutable 1080p pixels while the producer has moved to 4K");
    Check(!reader.Pixels(0,fullHD)&&!oldReader.Pixels(0,fourK)&&!reader.Pixels(2,fourK),
          "metadata dimensions and eye index must fit the reader's own epoch exactly");
    auto malformed=fourK;malformed.stride=0xffffffffu;
    Check(!reader.Pixels(0,malformed),"untrusted metadata stride cannot address outside the mapped view");
    CPUFrame::Reader fourKReader;Check(fourKReader.Open(pid,2),"retain independent old 4K reader before another resize");
    Check(writer.Open(pid,3,5120,2880)&&reader.Open(pid,3)&&writer.layout.mappingBytes==235930112,
          "5K pixels retain full native dimensions in exact published and pending banks");
    if(!writer.shared||!reader.shared)return;
    writer.Pixels(0)[0]=41;writer.Pixels(1)[writer.layout.imageBytes-1]=42;
    Check(reader.Pixels(0,fiveK)[0]==41&&reader.Pixels(1,fiveK)[reader.layout.imageBytes-1]==42,
          "5K first and last bytes of each eye stay within the allocated arena");
    Check(fourKReader.Pixels(0,fourK)&&fourKReader.Pixels(0,fourK)[0]==31&&oldReader.Pixels(0,fullHD)[0]==11,
          "two retained historical readers survive successive resolution changes");
    writer.Close();
    Check(reader.Pixels(1,fiveK)&&reader.Pixels(1,fiveK)[reader.layout.imageBytes-1]==42,
          "reader owns its section lifetime after the producer releases a resized arena");
    Check(!duplicate.Open(pid,3,5120,2880)&&reader.Pixels(0,fiveK)[0]==41,
          "producer cannot reuse an epoch while a previous reader retains its section");
    reader.Close();Check(duplicate.Open(pid,3,5120,2880),"an epoch name becomes available only when every old section reference has closed");
    Check(!reader.Open(pid,9999)&&!reader.shared,"missing epoch cannot fall back to another arena");
    Check(!duplicate.Open(0,4,1920,1080)&&!duplicate.Open(pid,0,1920,1080),"zero PID and epoch never create a mapping");
}
void MalformedHeaders() {
    const DWORD pid=GetCurrentProcessId();CPUFrame::Writer writer;
    Check(writer.Open(pid,10,1920,1080),"create isolated arena for header validation");
    if(!writer.shared)return;
    const auto original=*writer.shared;
    const auto reject=[&](const CPUFrame::Shared& changed,const char* reason){*writer.shared=changed;CPUFrame::Reader reader;Check(!reader.Open(pid,10)&&!reader.shared,reason);*writer.shared=original;};
    auto changed=original;changed.version=1;reject(changed,"old fixed-arena protocol cannot be interpreted as dynamic pixels");
    changed=original;changed.pid=pid+1;reject(changed,"mapping header cannot claim a different source PID");
    changed=original;changed.epoch=11;reject(changed,"mapping header must match the requested transport epoch");
    changed=original;changed.headerBytes=128;reject(changed,"unrecognized header offset is rejected");
    changed=original;changed.width=0xffffffffu;reject(changed,"malformed native dimensions cannot overflow capacity arithmetic");
    changed=original;changed.stride=original.stride+4;reject(changed,"nonmatching stride cannot alter eye offsets");
    changed=original;changed.imageBytes=std::uint64_t(-1);reject(changed,"malformed per-eye byte size is rejected before mapping pixels");
    changed=original;changed.mappingBytes=64;reject(changed,"undersized declared mapping cannot expose image pointers");
    changed=original;changed.mappingBytes=std::uint64_t(-1);reject(changed,"oversized declared mapping cannot exceed DWORD or size_t bounds");
    CPUFrame::Reader reader;Check(reader.Open(pid,10),"valid reader reconnects after malformed fixtures");
    writer.shared->width=3840;Check(!reader.Pixels(0,Metadata(1920,1080))&&!reader.Open(pid,10),
                                  "header mutation after mapping invalidates pixel access and cached reopen");
    *writer.shared=original;

    wchar_t name[128]{};CPUFrame::Name(name,128,pid,20);
    HANDLE shortMapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(CPUFrame::Shared),name);
    auto* header=shortMapping?static_cast<CPUFrame::Shared*>(MapViewOfFile(shortMapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(CPUFrame::Shared))):nullptr;
    Check(header!=nullptr,"create deliberately truncated physical section");
    if(header){*header=original;header->epoch=20;CPUFrame::Reader truncated;
        Check(!truncated.Open(pid,20)&&!truncated.shared,"header claiming more bytes than the physical section cannot map a readable arena");
        UnmapViewOfFile(header);}
    if(shortMapping)CloseHandle(shortMapping);
}
void CompletedPairs() {
    CPUFrame::Writer writer;CPUFrame::Reader reader;const DWORD pid=GetCurrentProcessId();
    Check(writer.Open(pid,30,16,16)&&reader.Open(pid,30),"isolated completed-pair arena connects");
    if(!writer.shared||!reader.shared)return;
    CPUFrame::Snapshot snapshot;Check(!reader.SnapshotPublished(snapshot),"empty arena never exposes a half-initialized publication");
    unsigned copies=0;bool committed=false;
    const auto metadata=[](std::uint64_t id,unsigned kind=VRIPC::kSceneFrame){auto m=Metadata(16,16);
        m.frameId=id;m.tick=1000+static_cast<DWORD>(id);m.poseValid=kind;m.sensorTime=4.;m.headOrientation[3]=1;
        for(auto& f:m.fov)f=1;return m;};
    const auto write=[&](unsigned side,VRIPC::EyeMetadata m,unsigned char color){
        committed=false;return writer.WriteEye(side,m,[&](std::uint8_t* pixels){++copies;std::memset(pixels,color,writer.layout.imageBytes);},committed);};
    Check(write(1,metadata(3),3)&&!committed&&!copies&&!reader.SnapshotPublished(snapshot),
          "right-first capture cannot publish an unpaired image or consume a copy");
    Check(write(0,metadata(2),2)&&!committed&&copies==1&&!reader.SnapshotPublished(snapshot),
          "first left capture is staged exactly once without publishing");
    Check(write(1,metadata(3),3)&&committed&&copies==2&&reader.SnapshotPublished(snapshot)&&snapshot.mask==3&&
          snapshot.metadata[0].frameId==2&&snapshot.metadata[1].frameId==3&&reader.Pixels(snapshot,0)[0]==2&&reader.Pixels(snapshot,1)[1023]==3,
          "right completion atomically publishes exact metadata and both complete image boundaries");
    const auto old=snapshot;
    Check(write(0,metadata(4),4)&&!committed&&reader.StillPublished(old)&&reader.SnapshotPublished(snapshot)&&
          snapshot.publication==old.publication&&reader.Pixels(snapshot,0)[0]==2&&reader.Pixels(snapshot,1)[0]==3,
          "slow reader recovers completed pair2/3 even after next left4 has overwritten its pending bank");
    auto wrong=metadata(5);wrong.sensorTime=5.;
    Check(write(1,wrong,5)&&!committed&&copies==3&&reader.StillPublished(old),
          "different tracking sample rejects the right image before copy while preserving last completed pair");
    Check(write(0,metadata(6),6)&&!committed&&write(1,metadata(7),7)&&committed&&reader.SnapshotPublished(snapshot)&&
          snapshot.metadata[0].frameId==6&&snapshot.metadata[1].frameId==7&&!reader.StillPublished(old),
          "a later valid complete pair replaces the publication without requiring intermediate host polls");
    const auto raced=snapshot;std::uint8_t captured=reader.Pixels(snapshot,0)[0];
    Check(write(0,metadata(8),8)&&!committed&&write(1,metadata(9),9)&&committed&&captured==6&&!reader.StillPublished(raced),
          "a publication changed between eye copies fails final validation instead of committing mixed pairs");
    Check(write(0,metadata(10),10)&&!committed&&write(0,metadata(11,VRIPC::kScopeFrame),11)&&committed&&
          reader.SnapshotPublished(snapshot)&&snapshot.mask==1&&snapshot.metadata[0].poseValid==VRIPC::kScopeFrame&&
          reader.Pixels(snapshot,0)[0]==11&&reader.Pixels(snapshot,1)[0]==11,
          "mono scope immediately supersedes a staged scene and both eyes address the same copied image");
    const auto monoCopies=copies;const auto monoPublication=snapshot.publication;
    Check(write(1,metadata(11,VRIPC::kScopeFrame),11)&&!committed&&copies==monoCopies&&
          reader.SnapshotPublished(snapshot)&&snapshot.publication==monoPublication,
          "duplicate mono right publication requires no second image copy or publication");
    Check(write(1,metadata(11),12)&&!committed&&copies==monoCopies&&reader.StillPublished(snapshot),
          "late scene right cannot complete a half-pair retired by a mono transition");
    auto malformed=metadata(12);malformed.stride+=4;
    Check(!write(0,malformed,12)&&copies==monoCopies&&reader.StillPublished(snapshot),
          "invalid dimensions or stride cannot copy bytes or replace a complete publication");
    Check(writer.Open(pid,31,16,16)&&!writer.pending&&reader.Open(pid,31)&&!reader.SnapshotPublished(snapshot),
          "new epoch retires pending state and begins without a stale complete pair");
    Check(write(0,metadata(20),20)&&!committed&&write(1,metadata(23),23)&&!committed&&!reader.SnapshotPublished(snapshot),
          "frame identity mismatch cannot be relabelled as a complete pair");
}
}
int main(){Layouts();RoundTripAndResize();MalformedHeaders();CompletedPairs();
    std::printf("Dynamic CPU frame IPC: %u checks, %u failures; isolated page-file mappings only.\n",checks,failures);return failures?1:0;}
