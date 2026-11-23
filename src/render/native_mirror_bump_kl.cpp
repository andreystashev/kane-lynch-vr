#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_mirror_bump_kl.hpp"
#include "native_mirror_bump_math_kl.hpp"
#include "native_mirror_bump_programs_kl.hpp"
#include "native_tokyo_floor_program_kl.hpp"
#include "native_tower_environment.hpp"
#include "d3d9_capture.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
namespace NativeMirrorBumpKL {
namespace {
std::atomic<ReadView> reader{nullptr};
std::atomic<unsigned> tokyoFloorMode{0};
std::atomic<QualifyReflector> qualifyTokyoReflector{nullptr};
std::atomic<std::uint64_t> tokyoFloorEye[2]{},tokyoFloorUntracked{0},tokyoFloorRestored{0};
std::atomic<std::uint64_t> tokyoFloorLive{0},tokyoFloorHistory{0},tokyoFloorFallback{0};
std::atomic<std::uint64_t> tokyoFloorCopies{0},tokyoFloorCopyFailed{0};
std::atomic<std::uint64_t> retomotoFloorLive{0},retomotoFloorHistory{0},retomotoFloorFallback{0},retomotoFloorCopies{0};
std::atomic<std::uint64_t> towerCubeMatched{0},towerCubeDraws{0};
std::atomic<ReadReflection> reflectionReader{nullptr};
std::atomic<bool(*)(IDirect3DDevice9*)> writeCoverage{nullptr};
std::atomic<std::uint64_t(*)()> readActiveSerial{nullptr};
std::atomic<IDirect3DDevice9*> historyDevice{nullptr};
std::atomic<DWORD> historyThread{0};
std::atomic<unsigned> foreignWrites{0};
std::atomic<unsigned> epoch{1};
std::atomic<std::uint64_t> seen{0},recognized{0},applied{0},restored{0};
std::atomic<std::uint64_t> viewRejected{0},matrixRejected{0},zeroPreserved{0},failed{0},restoreFailed{0};
std::atomic<std::uint64_t> viewportApplied{0};
std::atomic<std::uint64_t> targetCaptured{0},targetRejected{0},targetEligible{0},targetApplied{0},targetRestored{0};
std::atomic<std::uint64_t> cachedTargetApplied{0},targetInvalidated{0};
std::atomic<std::uint64_t> cachedOtherEyeApplied{0};
std::atomic<std::uint64_t> historyRetained{0},historySceneRetires{0},historyForeignWrites{0},historyIneligible{0};
enum class Invalidation : unsigned {ProducerReplaced,Bound,ColorClear,Copy,CameraChanged,StillBound};
std::atomic<std::uint64_t> invalidationReasons[6]{};
std::atomic<DWORD> evidenceTick{0};SRWLOCK evidenceLock=SRWLOCK_INIT;MatrixEvidence lastEvidence;
void Rejected(const NativeCameraKL::EffectsView& view,const float* matrices,bool read,bool main,unsigned bump,
              const NativeMirrorBumpMathKL::TextureViewport* viewport){
    const auto now=GetTickCount();auto previous=evidenceTick.load(std::memory_order_relaxed);
    if(previous&&now-previous<1000)return;
    if(!evidenceTick.compare_exchange_strong(previous,now?now:1,std::memory_order_relaxed)||
       !TryAcquireSRWLockExclusive(&evidenceLock))return;
    MatrixEvidence e;e.valid=true;e.read=read;e.mainMatch=main;e.viewportRead=viewport!=nullptr;
    e.tick=now;e.bump=bump;e.eyeIndex=view.eyeIndex;e.state=view.state;e.frame=view.frame;
    e.modelAdjustment[0]=view.modelViewScale;e.modelAdjustment[1]=view.modelViewDepthBias;
    const auto& p=view.eye;const float eye[]{p.basis.right.x,p.basis.right.y,p.basis.right.z,p.basis.up.x,p.basis.up.y,p.basis.up.z,
        p.basis.forward.x,p.basis.forward.y,p.basis.forward.z,p.position.x,p.position.y,p.position.z};std::memcpy(e.eye,eye,sizeof(eye));
    if(viewport){const unsigned values[]{viewport->x,viewport->y,viewport->width,viewport->height,viewport->targetWidth,viewport->targetHeight};std::memcpy(e.viewport,values,sizeof(values));}
    std::memcpy(e.world,view.world,sizeof(e.world));std::memcpy(e.projection,view.projection,sizeof(e.projection));
    std::memcpy(e.actual,matrices,sizeof(e.actual));
    if(read)NativeMirrorBumpMathKL::Matches(view.world,view.eye,view.projection,matrices,e.expected,e.bounds,viewport,nullptr,
        view.modelViewScale,view.modelViewDepthBias);
    lastEvidence=e;ReleaseSRWLockExclusive(&evidenceLock);
}
template<class Shader>struct Entry {Shader* source=nullptr;unsigned program=0;std::uint64_t used=0;};
template<class Shader>unsigned Identify(Shader* shader,bool pixel) {
    constexpr unsigned limit=2048;
    const unsigned minimum=pixel?sizeof(NativeMirrorBumpProgramsKL::Pixel0):sizeof(NativeMirrorBumpProgramsKL::VertexSpecular);
    UINT bytes=0;if(FAILED(shader->GetFunction(nullptr,&bytes))||bytes<minimum||bytes>limit||(bytes&3))return 0;
    std::array<std::uint32_t,limit/4> code{};const UINT expected=bytes;
    if(FAILED(shader->GetFunction(code.data(),&bytes))||bytes!=expected)return 0;
    return pixel?NativeMirrorBumpProgramsKL::PixelId(code.data(),bytes/4):NativeMirrorBumpProgramsKL::VertexId(code.data(),bytes/4);
}
struct Target {
    IDirect3DTexture9* texture=nullptr;
    NativeCameraKL::ReflectionTargetView camera;
    NativeMirrorBumpMathKL::TextureViewport viewport;
    IDirect3DSurface9* surface=nullptr;
    DWORD used=0;
    bool invalidHistory=false;
    void Clear() {
        if(surface)surface->Release();
        if(texture)texture->Release();
        *this={};
    }
};
// Floor_Unique_Mirror_01_ in the M09B lobby. The game's shared vertex
// buffer packing changes minVertex and first across loads and processes.
bool TokyoFloorMesh(const D3D9Capture::DrawCall* call) {
    return call&&call->kind==1&&call->type==D3DPT_TRIANGLELIST&&call->baseVertex==0&&
        call->vertices==79&&call->count==105;
}
bool TokyoFloorLocation(const NativeCameraKL::EffectsView& view) {
    const auto& p=view.reference.position;
    // Authored Tokyo lobby, balcony and street. Use the paired reference
    // here so selection cannot toggle between the two tracked eyes.
    return p.x>1200.f&&p.x<7400.f&&p.y>100.f&&p.y<1400.f&&p.z>-9900.f&&p.z<-4700.f;
}
enum FloorRegion : unsigned {TokyoFloor,RetomotoOffice,RetomotoAtrium,RetomotoSecond,RetomotoGround,FloorRegionCount};
FloorRegion RetomotoFloorLocation(const NativeCameraKL::EffectsView& view) {
    const auto& p=view.reference.position;
    if(p.x< -3300.f||p.x>6950.f||p.y<19100.f||p.y>21400.f||p.z< -4100.f||p.z>4100.f)
        return FloorRegionCount;
    if(p.y<20100.f)return RetomotoGround;
    // The second floor is at Y20300. Classify the paired camera centre
    // between the standing eye heights of the second and third floors.
    if(p.y<20810.f)return RetomotoSecond;
    return p.x>=3550.f?RetomotoOffice:RetomotoAtrium;
}
FloorRegion FloorHistory(FloorRegion region){
    // The office and upper ring share the same physical plane.
    return region==RetomotoOffice?RetomotoAtrium:region;
}
FloorRegion RetomotoSurface(const NativeCameraKL::EffectsView& view,FloorRegion region){
    const auto height=view.world[10];
    // A lower ring visible through the atrium still belongs to its own
    // plane. Authoritative mesh transforms beat the viewer's floor.
    if(std::fabs(height-19600.f)<140.f)return RetomotoGround;
    if(std::fabs(height-20300.f)<140.f)return RetomotoSecond;
    if(std::fabs(height-21000.f)<140.f)return view.world[9]>=3550.f?RetomotoOffice:RetomotoAtrium;
    return region;
}
FloorRegion RetomotoReflectorFloor(const NativeCameraKL::ReflectionTargetView& view){
    if(!view.horizontal)return FloorRegionCount;
    if(std::fabs(view.floorHeight-20300.f)<160.f)return RetomotoSecond;
    if(std::fabs(view.floorHeight-19600.f)<160.f)return RetomotoGround;
    if(std::fabs(view.floorHeight-21000.f)<160.f)return RetomotoAtrium;
    return FloorRegionCount;
}
bool TokyoFloorMap(IDirect3DBaseTexture9* base,D3DFORMAT format,std::uint64_t expected) {
    if(!base||base->GetType()!=D3DRTYPE_TEXTURE||base->GetLevelCount()!=9)return false;
    auto* texture=static_cast<IDirect3DTexture9*>(base);
    D3DSURFACE_DESC desc{};
    if(FAILED(texture->GetLevelDesc(0,&desc))||desc.Format!=format||
       desc.Width!=256||desc.Height!=256||desc.Pool!=D3DPOOL_MANAGED||desc.Usage!=0)return false;
    // Glacier's compressed upload uses the CPU-backed managed allocation.
    // The three smallest mip levels are one 16-byte BC block each.
    std::uint64_t signature=0xcbf29ce484222325ULL;
    for(UINT level=6;level<=8;++level){
        D3DLOCKED_RECT block{};
        if(FAILED(texture->LockRect(level,&block,nullptr,D3DLOCK_READONLY|D3DLOCK_NO_DIRTY_UPDATE)))return false;
        const auto* bytes=static_cast<const unsigned char*>(block.pBits);
        const bool readable=bytes&&block.Pitch>=16;
        if(readable)for(unsigned i=0;i<16;++i)signature=(signature^bytes[i])*0x100000001b3ULL;
        if(FAILED(texture->UnlockRect(level))||!readable)return false;
    }
    return signature==expected;
}
struct FloorMaps {
    struct Pair {
        IDirect3DBaseTexture9* diffuse=nullptr;
        IDirect3DBaseTexture9* normal=nullptr;
        bool matched=false,certified=false;
        void Clear(){
            if(diffuse)diffuse->Release();
            if(normal)normal->Release();
            *this={};
        }
    };
    std::array<Pair,16> pairs{};
    unsigned next=0;
    void Clear(){
        for(auto& pair:pairs)pair.Clear();
        next=0;
    }
    bool Match(IDirect3DDevice9* device,bool inTokyo,bool inRetomoto,unsigned diffuseSlot,unsigned normalSlot){
        IDirect3DBaseTexture9* diffuse=nullptr;
        IDirect3DBaseTexture9* normal=nullptr;
        if(FAILED(device->GetTexture(diffuseSlot,&diffuse))||!diffuse)return false;
        if(FAILED(device->GetTexture(normalSlot,&normal))||!normal){diffuse->Release();return false;}
        for(auto& pair:pairs){
            if(pair.diffuse!=diffuse||pair.normal!=normal)continue;
            diffuse->Release();
            normal->Release();
            if(inTokyo&&pair.matched)pair.certified=true;
            return pair.matched&&(inRetomoto||pair.certified);
        }
        const bool matched=TokyoFloorMap(diffuse,D3DFMT_DXT3,0x258f31f7d8d9f9d7ULL)&&
            TokyoFloorMap(normal,D3DFMT_DXT5,0xf37efff6bb9f6e62ULL);
        // The Tokyo exception applies only to the current matching pair.
        // Remember alternate Mirror materials so draws that alternate with
        // the floor do not repeatedly lock the managed texture mip levels.
        auto& pair=pairs[next++%pairs.size()];
        pair.Clear();
        pair={diffuse,normal,matched,matched&&inTokyo};
        return matched&&(inRetomoto||pair.certified);
    }
};
struct FloorPlanar {
    struct Exposure {
        Target target;
        NativeCameraMath::CameraPose reference{};
        D3DFORMAT format=D3DFMT_UNKNOWN;
    };
    Target copy;
    std::array<Exposure,3> earlier{};
    NativeCameraMath::CameraPose reference{},pendingReference{};
    D3DFORMAT format=D3DFMT_UNKNOWN;
    std::uint64_t pending=0;
    std::uintptr_t reflector=0;
    DWORD lastAttempt=0,lastLive=0,resumed=0,lastUse=0;
    float world[12]{};
    bool worldKnown=false,valid=false,nearExit=false;
    void Clear(){
        copy.Clear();
        for(auto& exposure:earlier){exposure.target.Clear();exposure={};}
        format=D3DFMT_UNKNOWN;pending=0;reflector=0;lastAttempt=lastLive=resumed=lastUse=0;worldKnown=valid=nearExit=false;
    }
    Target* Saved(const NativeCameraMath::CameraPose& viewer) {
        using namespace NativeCameraMath;
        const auto score=[&](const CameraPose& source){
            const auto offset=Subtract(source.position,viewer.position);
            return (offset.x*offset.x+offset.z*offset.z)/360000.f+
                2.f*(1.f-Dot(source.basis.forward,viewer.basis.forward));
        };
        Target* best=valid?&copy:nullptr;
        float distance=best?score(reference):std::numeric_limits<float>::max();
        for(auto& exposure:earlier){
            if(!exposure.target.texture)continue;
            const float next=score(exposure.reference);
            if(next<distance){best=&exposure.target;distance=next;}
        }
        return best;
    }
    void Archive(const NativeCameraMath::CameraPose& next,D3DFORMAT sourceFormat,unsigned width,unsigned height){
        using namespace NativeCameraMath;
        if(!valid)return;
        const auto difference=Subtract(next.position,reference.position);
        if(difference.x*difference.x+difference.z*difference.z<160000.f&&
           Dot(reference.basis.forward,next.basis.forward)>.72f)return;
        auto* slot=&earlier.front();
        for(auto& exposure:earlier){
            if(!exposure.target.texture){slot=&exposure;break;}
            if(exposure.target.used<slot->target.used)slot=&exposure;
        }
        std::swap(copy,slot->target);slot->reference=reference;
        std::swap(format,slot->format);
        // A slot can supply its surface for reuse only if the new producer
        // has the same format. Save will also check the dimensions.
        if(format!=sourceFormat||copy.viewport.width!=width||copy.viewport.height!=height)copy.Clear();
        valid=false;
    }
    float Gloss(DWORD now,bool live){
        constexpr float strength=.78f;
        if(!live)return strength;
        if((lastLive&&now-lastLive>150)||!lastLive)resumed=now;
        lastLive=now;
        const float t=std::min(1.f,float(now-resumed)/220.f);
        return strength*t*t*(3.f-2.f*t);
    }
    void Save(IDirect3DDevice9* device,const Target& source,DWORD now,bool retomoto=false){
        D3DSURFACE_DESC desc{};
        if(FAILED(source.surface->GetDesc(&desc))||desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
            ++tokyoFloorCopyFailed;return;
        }
        const auto& shape=source.viewport;
        constexpr unsigned maxDimension=512;
        const unsigned longest=std::max(shape.width,shape.height);
        const unsigned width=longest<=maxDimension?shape.width:std::max(1u,shape.width*maxDimension/longest);
        const unsigned height=longest<=maxDimension?shape.height:std::max(1u,shape.height*maxDimension/longest);
        if(retomoto)Archive(pendingReference,desc.Format,width,height);
        if(!copy.texture||copy.viewport.width!=width||copy.viewport.height!=height||format!=desc.Format){
            copy.Clear();valid=false;
            if(FAILED(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,&copy.texture,nullptr))||
               !copy.texture||FAILED(copy.texture->GetSurfaceLevel(0,&copy.surface))||!copy.surface){
                copy.Clear();++tokyoFloorCopyFailed;return;
            }
            copy.viewport={0,0,width,height,width,height};format=desc.Format;
        }
        const RECT rect{LONG(shape.x),LONG(shape.y),LONG(shape.x+shape.width),LONG(shape.y+shape.height)};
        valid=false;
        if(FAILED(device->StretchRect(source.surface,&rect,copy.surface,nullptr,D3DTEXF_LINEAR))){
            ++tokyoFloorCopyFailed;return;
        }
        copy.camera=source.camera;copy.used=now;reference=pendingReference;valid=true;
        if(retomoto)++retomotoFloorCopies;else ++tokyoFloorCopies;
    }
};
struct Cache {
    IDirect3DDevice9* owner=nullptr;unsigned generation=0;std::uint64_t clock=0;
    IDirect3DCubeTexture9* lobbyCube=nullptr;
    bool lobbyUploadAttempted=false;
    std::array<Entry<IDirect3DPixelShader9>,64> pixels{};
    std::array<Entry<IDirect3DVertexShader9>,16> vertices{};
    FloorMaps floorMaps;
    std::array<FloorPlanar,FloorRegionCount> floorPlanar;
    enum FloorShader : unsigned {Matte,Glossy,FloorOnly,MaskedGlossy,MaskedFloorOnly,
        Environment,MaskedEnvironment,EnvironmentProjector,MaskedEnvironmentProjector,FloorShaderCount};
    IDirect3DPixelShader9* tokyoFloorShaders[FloorShaderCount]{};
    bool tokyoFloorShaderAttempted[FloorShaderCount]{};
    IDirect3DVertexShader9* ringVertex=nullptr;
    bool ringVertexAttempted=false;
    IDirect3DVertexShader9* RingVertex(IDirect3DDevice9* device){
        if(!ringVertexAttempted){
            ringVertexAttempted=true;
            if(FAILED(device->CreateVertexShader(NativeTokyoFloorProgramKL::RingProjectorVertex.data(),&ringVertex)))++failed;
        }
        return ringVertex;
    }
    IDirect3DPixelShader9* TokyoFloorShader(IDirect3DDevice9* device,FloorShader version) {
        auto*& shader=tokyoFloorShaders[version];
        if(!tokyoFloorShaderAttempted[version]){
            tokyoFloorShaderAttempted[version]=true;
            using namespace NativeTokyoFloorProgramKL;
            const DWORD* const programs[]{Pixel.data(),GlossyPixel,GlossyFallbackPixel,
                RingMaskedGloss.data(),RingMaskedFallback.data(),
                RetomotoEnvironment.data(),RetomotoMaskedEnvironment.data(),RingProjector.data(),RingMaskedProjector.data()};
            if(FAILED(device->CreatePixelShader(programs[version],&shader)))++failed;
        }
        return shader;
    }
    std::array<Target,16> targets{};std::uint64_t attempted=0,capturedFrame=UINT64_MAX;
    IDirect3DSurface9* activeReflection=nullptr;
    std::uint64_t activeSerial=0;
    bool history=false;
    bool historyChecked=false;
    unsigned observedWrites=0;
    void Prepare(IDirect3DDevice9* device){const unsigned now=epoch.load(std::memory_order_relaxed);
        if(owner!=device||generation!=now){
            Clear();owner=device;generation=now;
        }
        if(!history&&!historyChecked){
            historyChecked=true;
            const auto covered=writeCoverage.load(std::memory_order_relaxed);
            history=covered&&covered(device);
            observedWrites=foreignWrites.load();
            if(history){historyThread=GetCurrentThreadId();historyDevice=device;}
        }
        CheckForeignWrites();
    }
    void CheckForeignWrites(){
        if(!history)return;
        const auto next=foreignWrites.load(std::memory_order_relaxed);
        if(observedWrites!=next){if(HasTarget())++historyForeignWrites;ClearTargets();observedWrites=next;}
    }
    void Drop(Target& target){
        if(target.surface&&target.surface==activeReflection){activeReflection=nullptr;activeSerial=0;}
        target.Clear();
    }
    void Invalidate(Target& target,Invalidation reason){
        if(target.invalidHistory)return;
        ++targetInvalidated;++invalidationReasons[static_cast<unsigned>(reason)];
        if(target.camera.frame==capturedFrame){
            // Keep the established same-frame path while declining to
            // carry uncertain contents across a physical image boundary.
            target.invalidHistory=true;
            if(target.surface==activeReflection){activeReflection=nullptr;activeSerial=0;}
        } else Drop(target);
    }
    void InvalidateTarget(IDirect3DBaseTexture9* texture,Invalidation reason){
        if(!texture)return;
        for(auto& item:targets)if(item.texture==texture)Invalidate(item,reason);
    }
    bool HasTarget()const{
        for(const auto& target:targets)if(target.texture)return true;
        return false;
    }
    void InvalidateSurface(IDirect3DSurface9* surface,Invalidation reason){
        if(!surface||!HasTarget())return;
        for(auto& item:targets)if(item.surface==surface){
            Invalidate(item,reason);return;
        }
        IDirect3DTexture9* texture=nullptr;
        if(SUCCEEDED(surface->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&texture)))&&texture){
            InvalidateTarget(texture,reason);texture->Release();
        }
    }
    void Commit(Target& target){
        auto* slot=&targets[0];for(auto& item:targets){if(item.texture==target.texture){slot=&item;break;}
            if(!item.texture){slot=&item;break;}if(item.camera.serial<slot->camera.serial)slot=&item;}
        Drop(*slot);*slot=target;target={};capturedFrame=slot->camera.frame;++targetCaptured;
        if(slot->surface){activeReflection=slot->surface;activeSerial=slot->camera.serial;}
    }
    void ClearTargets(){
        for(auto& target:targets)Drop(target);
        activeReflection=nullptr;activeSerial=0;attempted=0;capturedFrame=UINT64_MAX;
    }
    void Finish(bool keep){
        historyChecked=false;
        if(!keep||!history){if(history&&HasTarget())++historySceneRetires;ClearTargets();return;}
        CheckForeignWrites();
        if(activeReflection)InvalidateSurface(activeReflection,Invalidation::StillBound);
        const DWORD now=GetTickCount();
        for(auto& target:targets)if(target.texture&&(target.invalidHistory||!target.surface||now-target.used>8000))Drop(target);
        // Bound extra references during changes of projection device/view.
        for(;;){
            Target* oldest=nullptr;unsigned count=0;
            for(auto& target:targets)if(target.texture){
                ++count;
                if(!oldest||now-target.used>now-oldest->used)oldest=&target;
            }
            if(count<=2)break;
            Drop(*oldest);
        }
        if(HasTarget())++historyRetained;
        attempted=0;capturedFrame=UINT64_MAX;
    }
    void Clear(){
        if(lobbyCube){lobbyCube->Release();lobbyCube=nullptr;}lobbyUploadAttempted=false;
        if(ringVertex){ringVertex->Release();ringVertex=nullptr;}ringVertexAttempted=false;
        for(auto& e:pixels){if(e.source)e.source->Release();e={};}
        for(auto& e:vertices){if(e.source)e.source->Release();e={};}
        floorMaps.Clear();for(auto& floor:floorPlanar)floor.Clear();
        for(unsigned i=0;i<FloorShaderCount;++i){
            if(tokyoFloorShaders[i])tokyoFloorShaders[i]->Release();
            tokyoFloorShaders[i]=nullptr;tokyoFloorShaderAttempted[i]=false;
        }
        ClearTargets();owner=nullptr;clock=0;history=false;historyChecked=false;
    }
    ~Cache(){Clear();}
    template<class Shader,std::size_t Count>
    unsigned Match(Shader* source,std::array<Entry<Shader>,Count>& entries,bool pixel) {
        for(auto& e:entries)if(e.source==source){e.used=++clock;source->Release();return e.program;}
        auto* slot=&entries[0];for(auto& e:entries)if(!e.source){slot=&e;break;}else if(e.used<slot->used)slot=&e;
        if(slot->source)slot->source->Release();*slot={source,Identify(source,pixel),++clock};return slot->program;
    }
    unsigned Accept(IDirect3DDevice9* device) {
        const unsigned now=epoch.load(std::memory_order_relaxed);
        if(owner!=device||generation!=now){Clear();owner=device;generation=now;}
        IDirect3DPixelShader9* ps=nullptr;
        if(FAILED(device->GetPixelShader(&ps))||!ps)return 0;
        const auto pixel=Match(ps,pixels,true);if(!pixel)return 0;
        IDirect3DVertexShader9* vs=nullptr;
        if(FAILED(device->GetVertexShader(&vs))||!vs)return 0;
        return Match(vs,vertices,false)==NativeMirrorBumpProgramsKL::Pixels[pixel-1].vertex?pixel:0;
    }
};
thread_local Cache cache;
struct Scope {
    IDirect3DDevice9* device=nullptr;float saved[4]{},savedUV[16]{};unsigned bump=1;
    bool active=false,failed=false,uvActive=false,capturing=false;Target target;
    IDirect3DPixelShader9* savedPixel=nullptr;
    IDirect3DVertexShader9* savedVertex=nullptr;
    IDirect3DBaseTexture9* savedEnvironment=nullptr;
    float savedWorldRay[12]{};
    bool environmentChanged=false,worldRayChanged=false;
    bool retomotoFloor=false;
    unsigned floorSampler=2;
    IDirect3DBaseTexture9* savedFloorTexture=nullptr;
    bool floorTextureChanged=false,floorConstantsChanged=false,floorProjectionChanged=false;
    float savedFloorConstants[16]{};
    bool Busy()const{return active||uvActive||capturing||savedPixel;}
    void Restore(){
        if(savedVertex){
            if(FAILED(device->SetVertexShader(savedVertex))){++restoreFailed;failed=true;}
            savedVertex->Release();savedVertex=nullptr;
        }
        if(worldRayChanged){
            worldRayChanged=false;
            if(FAILED(device->SetVertexShaderConstantF(16,savedWorldRay,3))){++restoreFailed;failed=true;}
        }
        if(environmentChanged){
            environmentChanged=false;
            if(FAILED(device->SetTexture(4,savedEnvironment))){++restoreFailed;failed=true;}
        }
        if(savedEnvironment){savedEnvironment->Release();savedEnvironment=nullptr;}
        if(floorTextureChanged){
            floorTextureChanged=false;
            if(FAILED(device->SetTexture(floorSampler,savedFloorTexture))){++restoreFailed;failed=true;}
        }
        if(savedFloorTexture){savedFloorTexture->Release();savedFloorTexture=nullptr;}
        if(floorConstantsChanged){
            floorConstantsChanged=false;
            if(FAILED(device->SetPixelShaderConstantF(6,savedFloorConstants,4))){++restoreFailed;failed=true;}
        }
        if(floorProjectionChanged){
            floorProjectionChanged=false;
            if(FAILED(device->SetVertexShaderConstantF(4,savedUV,4))){++restoreFailed;failed=true;}
        }
        if(savedPixel){
            if(FAILED(device->SetPixelShader(savedPixel))){++restoreFailed;failed=true;}else if(!retomotoFloor)++tokyoFloorRestored;
            savedPixel->Release();savedPixel=nullptr;
        }
        if(active){active=false;if(FAILED(device->SetPixelShaderConstantF(bump,saved,1))){++restoreFailed;failed=true;}else ++restored;}
        if(uvActive){uvActive=false;if(FAILED(device->SetVertexShaderConstantF(4,savedUV,4))){++restoreFailed;failed=true;}else ++targetRestored;}
    }
    void DropTarget(){target.Clear();capturing=false;}
};
thread_local Scope current;
thread_local bool entering=false;
bool PrepareTowerEnvironment(IDirect3DDevice9* device,const NativeCameraKL::EffectsView& view){
    auto* vertex=cache.RingVertex(device);
    if(!cache.lobbyCube||!vertex||FAILED(device->GetTexture(4,&current.savedEnvironment))||
       FAILED(device->GetVertexShaderConstantF(16,current.savedWorldRay,3)))return false;
    const auto* world=view.world;
    const auto& eye=view.eye.position;
    const float worldRay[12]{
        -world[6],-world[3],-world[0],eye.x-world[9],
        -world[7],-world[4],-world[1],eye.y-world[10],
        -world[8],-world[5],-world[2],eye.z-world[11]};
    current.environmentChanged=true;
    if(FAILED(device->SetTexture(4,cache.lobbyCube)))return false;
    current.worldRayChanged=true;
    if(FAILED(device->SetVertexShaderConstantF(16,worldRay,3)))return false;
    return SUCCEEDED(device->GetVertexShader(&current.savedVertex))&&current.savedVertex&&
        SUCCEEDED(device->SetVertexShader(vertex));
}

bool PrepareFloorGloss(IDirect3DDevice9* device,const NativeCameraKL::EffectsView& view,bool hasView,
                       FloorRegion region,bool& live,bool& retained) {
    const auto plane=FloorHistory(region);
    auto& planar=cache.floorPlanar[plane];
    if(hasView){
        std::memcpy(planar.world,view.world,sizeof(planar.world));
        planar.worldKnown=true;
        planar.nearExit=region==TokyoFloor&&view.reference.position.x<3900.f;
    }
    // The private copy is immutable while sampled, including when the native
    // projector is withdrawn. A new source earns a receipt only at the exact
    // floor consumer, with the current camera, texture contents and viewport.
    Target* source=nullptr;
    if(hasView&&cache.capturedFrame==view.frame){
        if(FAILED(device->GetTexture(current.floorSampler,&current.savedFloorTexture)))return false;
        Target* boundSource=nullptr;
        for(auto& target:cache.targets){
            if(!target.texture||target.invalidHistory||
               target.camera.frame!=view.frame||target.camera.eyeIndex!=view.eyeIndex||
               (target.surface&&target.surface==cache.activeReflection))continue;
            const auto targetPlane=region==TokyoFloor?region:RetomotoReflectorFloor(target.camera);
            const bool bound=target.texture==current.savedFloorTexture;
            if(bound&&targetPlane!=FloorRegionCount){
                if(auto qualify=qualifyTokyoReflector.load(std::memory_order_relaxed);qualify&&target.camera.reflector)
                    qualify(target.camera.reflector,region!=TokyoFloor);
                cache.floorPlanar[targetPlane].reflector=target.camera.reflector;
            }
            // A certified native horizontal camera can render an adjacent
            // ring while the game's material is using another projector.
            // Freeze its actual texture for that ring, by physical height.
            if(targetPlane!=FloorRegionCount&&target.surface&&cache.history&&
               (bound||(targetPlane!=TokyoFloor&&target.camera.reflector))){
                auto& history=cache.floorPlanar[targetPlane];
                if(bound||target.camera.serial>history.pending){
                    history.pending=target.camera.serial;history.pendingReference=view.reference;
                }
            }
            if(targetPlane!=plane)continue;
            if(bound){boundSource=&target;continue;}
            // Native packets already prove a horizontal reflector and its
            // height. Separate batches can use alternate coplanar reflectors.
            if(plane!=TokyoFloor&&target.camera.reflector&&
               (!source||target.camera.reflector==planar.reflector||
                (source->camera.reflector!=planar.reflector&&target.camera.serial>source->camera.serial))){
                source=&target;
            }
        }
        if(boundSource)source=boundSource;
        if(source){
            live=true;source->used=GetTickCount();
            if(source->surface&&cache.history){
                planar.pending=source->camera.serial;planar.pendingReference=view.reference;
            }
        }
    }
    if(!source&&planar.worldKnown){
        source=region==TokyoFloor?(planar.valid?&planar.copy:nullptr):planar.Saved(view.reference);
        retained=source!=nullptr;
    }
    if(!source)return true;
    planar.lastUse=GetTickCount();
    float uv[16]{},constants[16]{};
    if(!NativeMirrorBumpMathKL::TargetProjection(planar.world,source->camera.view,source->camera.projection,source->viewport,uv))return false;
    // The X/Z rows are in the same coordinates as t1 even after a change
    // in the tracked eye. Keep local roughness attached to the tile.
    constexpr float displacement=26.f;
    for(unsigned i=0;i<4;++i){constants[i]=displacement*uv[i];constants[4+i]=displacement*uv[8+i];}
    const auto& shape=source->viewport;
    constants[8]=float(shape.x)/shape.targetWidth;constants[9]=float(shape.y)/shape.targetHeight;
    constants[10]=float(shape.x+shape.width)/shape.targetWidth;constants[11]=float(shape.y+shape.height)/shape.targetHeight;
    // Fade at and behind the producer's camera. Keep the reciprocal finite.
    constants[12]=12.f;constants[13]=.02f;constants[14]=planar.Gloss(GetTickCount(),live);
    if(FAILED(device->GetPixelShaderConstantF(6,current.savedFloorConstants,4)))return false;
    if(FAILED(device->GetVertexShaderConstantF(4,current.savedUV,4)))return false;
    if(source->texture!=current.savedFloorTexture){
        if(!current.savedFloorTexture&&FAILED(device->GetTexture(current.floorSampler,&current.savedFloorTexture)))return false;
        current.floorTextureChanged=true;
        if(FAILED(device->SetTexture(current.floorSampler,source->texture)))return false;
    }
    current.floorProjectionChanged=true;
    if(FAILED(device->SetVertexShaderConstantF(4,uv,4)))return false;
    current.floorConstantsChanged=true;
    return SUCCEEDED(device->SetPixelShaderConstantF(6,constants,4));
}
}
void Configure(ReadView read,ReadReflection reflection,unsigned floorMode,QualifyReflector qualifyReflector){
    tokyoFloorMode.store(floorMode<=2?floorMode:0,std::memory_order_relaxed);
    reader.store(read,std::memory_order_relaxed);
    reflectionReader.store(reflection,std::memory_order_relaxed);
    qualifyTokyoReflector.store(qualifyReflector,std::memory_order_relaxed);
}
void ConfigureTargetHistory(bool (*covered)(IDirect3DDevice9*),std::uint64_t (*activeSerial)()){
    readActiveSerial.store(activeSerial);writeCoverage.store(covered);epoch.fetch_add(1);
}
void ObserveTargetChange(IDirect3DDevice9* device,D3D9Capture::TargetChange change,void* destination,unsigned slot){
    if(device!=historyDevice.load(std::memory_order_relaxed))return;
    if(GetCurrentThreadId()!=historyThread.load(std::memory_order_relaxed)){++foreignWrites;return;}
    if(!cache.history||device!=cache.owner)return;
    using D3D9Capture::TargetChange;
    switch(change){
      case TargetChange::Bind:
        cache.InvalidateSurface(static_cast<IDirect3DSurface9*>(destination),Invalidation::Bound);
        if(!slot){cache.activeReflection=nullptr;cache.activeSerial=0;}
        break;
      case TargetChange::Clear:
        if(cache.activeReflection)cache.InvalidateSurface(cache.activeReflection,Invalidation::ColorClear);
        break;
      case TargetChange::SurfaceWrite:
        cache.InvalidateSurface(static_cast<IDirect3DSurface9*>(destination),Invalidation::Copy);break;
      case TargetChange::TextureWrite:
        cache.InvalidateTarget(static_cast<IDirect3DBaseTexture9*>(destination),Invalidation::Copy);break;
    }
}
void* BeginDraw(IDirect3DDevice9* device,const D3D9Capture::DrawCall* call){
    if(!device||current.Busy()||entering)return nullptr;
    struct Guard{Guard(){entering=true;}~Guard(){entering=false;}} guard;
    const auto read=reader.load(std::memory_order_relaxed);if(!read)return nullptr;
    cache.Prepare(device);
    if(const auto readTarget=reflectionReader.load(std::memory_order_relaxed)){
        NativeCameraKL::ReflectionTargetView camera;
        const bool producing=readTarget(camera)&&camera.serial&&camera.state&&camera.eyeIndex<2;
        if(cache.activeReflection&&(!producing||cache.activeSerial!=camera.serial)){
            const auto lease=readActiveSerial.load(std::memory_order_relaxed);
            if(!lease||lease()!=cache.activeSerial){
                IDirect3DSurface9* bound=nullptr;
                const auto result=device->GetRenderTarget(0,&bound);
                const bool unbound=SUCCEEDED(result)&&bound&&bound!=cache.activeReflection;
                if(bound)bound->Release();
                if(unbound){cache.activeReflection=nullptr;cache.activeSerial=0;}
                else {
                    DWORD mask=0;
                    if(FAILED(device->GetRenderState(D3DRS_COLORWRITEENABLE,&mask))||mask)
                        cache.InvalidateSurface(cache.activeReflection,Invalidation::CameraChanged);
                }
            }
        }
        if(producing){
            if(cache.attempted==camera.serial)return nullptr;cache.attempted=camera.serial;
            IDirect3DSurface9* surface=nullptr;Target target;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
            bool okay=SUCCEEDED(device->GetRenderTarget(0,&surface))&&surface;
            if(okay){okay=SUCCEEDED(surface->GetDesc(&desc))&&desc.Type==D3DRTYPE_SURFACE&&
                SUCCEEDED(surface->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&target.texture)))&&target.texture;
                if(okay&&cache.history&&desc.Pool==D3DPOOL_DEFAULT&&(desc.Usage&D3DUSAGE_RENDERTARGET))target.surface=surface;
                else {if(okay&&cache.history)++historyIneligible;surface->Release();}}
            if(okay)okay=SUCCEEDED(device->GetViewport(&viewport))&&viewport.Width&&viewport.Height&&
                viewport.X<=desc.Width&&viewport.Width<=desc.Width-viewport.X&&
                viewport.Y<=desc.Height&&viewport.Height<=desc.Height-viewport.Y;
            if(!okay){target.Clear();++targetRejected;return nullptr;}
            target.camera=camera;target.viewport={viewport.X,viewport.Y,viewport.Width,viewport.Height,desc.Width,desc.Height};
            target.used=GetTickCount();
            // The producer may have cleared/overwritten this RT already. A
            // failed first draw must not leave its previous successful stamp.
            for(auto& prior:cache.targets)if(prior.texture==target.texture)cache.Drop(prior);
            current={};current.device=device;current.capturing=true;current.target=target;return &current;
        }
    }
    ++seen;const auto pixel=cache.Accept(device);if(!pixel)return nullptr;++recognized;
    const auto& program=NativeMirrorBumpProgramsKL::Pixels[pixel-1];
    const unsigned bump=program.bump,sampler=program.reflection;
    NativeCameraKL::EffectsView view;
    const bool hasView=read(view)&&view.state&&view.frame&&view.worldKnown&&view.eyeIndex<2;
    // Once the lobby material is identified, a dropped tracking packet
    // must not restore the old planar image for a frame.
    const auto floorMode=tokyoFloorMode.load(std::memory_order_relaxed);
    const auto retRegion=hasView&&floorMode==2?RetomotoFloorLocation(view):FloorRegionCount;
    const bool inTokyo=hasView&&TokyoFloorLocation(view);
    const bool tokyoMesh=floorMode&&TokyoFloorMesh(call)&&(!hasView||inTokyo);
    // The plain Mirror variants share the VS and the normal scale. The
    // masked variant puts its diffuse in s3, normal in s0, projector in s1.
    const bool maskedFloor=pixel==3;
    const bool plainFloor=pixel==1||(retRegion!=FloorRegionCount&&(maskedFloor||pixel==5));
    if(plainFloor&&(tokyoMesh||retRegion!=FloorRegionCount)&&
       cache.floorMaps.Match(device,tokyoMesh&&inTokyo,retRegion!=FloorRegionCount,
                             maskedFloor?3:0,program.normal)){
        const bool glossy=floorMode==2;
        const auto region=retRegion!=FloorRegionCount?RetomotoSurface(view,retRegion):TokyoFloor;
        current={};current.device=device;
        current.retomotoFloor=region!=TokyoFloor;
        current.floorSampler=sampler;
        bool live=false,retained=false;
        if(glossy&&!PrepareFloorGloss(device,view,hasView,region,live,retained)){
            ++failed;
            current.Restore();
            if(current.failed)return &current;
            live=retained=false;
        }
        bool environment=false;
        if(glossy&&current.retomotoFloor&&region!=RetomotoOffice&&cache.lobbyCube){
            // Use the native room image wherever it covers the floor.
            // Outside that footprint the same cube stays visible.
            if(live||retained){
                const auto hybrid=maskedFloor?Cache::MaskedEnvironmentProjector:Cache::EnvironmentProjector;
                if(!cache.TokyoFloorShader(device,hybrid)){
                    current.Restore();if(current.failed)return &current;
                    live=retained=false;
                }
            }
            environment=PrepareTowerEnvironment(device,view);
            if(!environment){
                current.Restore();if(current.failed)return &current;
                live=retained=false;
            }
        }
        {
            const bool projected=live||retained;
            auto version=!glossy?Cache::Matte:projected?Cache::Glossy:Cache::FloorOnly;
            if(glossy&&maskedFloor)version=projected?Cache::MaskedGlossy:Cache::MaskedFloorOnly;
            if(environment&&projected)version=maskedFloor?Cache::MaskedEnvironmentProjector:Cache::EnvironmentProjector;
            else if(environment)version=maskedFloor?Cache::MaskedEnvironment:Cache::Environment;
            if(auto* replacement=cache.TokyoFloorShader(device,version)){
                if(SUCCEEDED(device->GetPixelShader(&current.savedPixel))&&current.savedPixel&&
                   SUCCEEDED(device->SetPixelShader(replacement))){
                    if(current.retomotoFloor){
                        if(environment)++towerCubeDraws;
                        if(live)++retomotoFloorLive;else if(retained)++retomotoFloorHistory;
                        else if(!environment)++retomotoFloorFallback;
                    }else {
                        if(hasView)++tokyoFloorEye[view.eyeIndex];else ++tokyoFloorUntracked;
                        if(glossy){if(live)++tokyoFloorLive;else if(retained)++tokyoFloorHistory;else ++tokyoFloorFallback;}
                    }
                    return &current;
                }
            }
        }
        ++failed;current.Restore();if(current.failed)return &current;
    }
    if(!hasView){++viewRejected;return nullptr;}
    float matrices[32]{};bool main=false,viewportAdjusted=false;
    const bool matricesRead=SUCCEEDED(device->GetVertexShaderConstantF(0,matrices,8));
    NativeMirrorBumpMathKL::TextureViewport texture;bool textureRead=false;
    bool matched=matricesRead&&NativeMirrorBumpMathKL::Matches(view.world,view.eye,view.projection,matrices,nullptr,nullptr,nullptr,&main,
        view.modelViewScale,view.modelViewDepthBias);
    if(!matched&&main){
        // Semantic7 also handles viewport subrects. Query their exact current
        // operands only after the ordinary main WVP has already matched.
        D3DVIEWPORT9 viewport{};IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};
        if(SUCCEEDED(device->GetViewport(&viewport))&&SUCCEEDED(device->GetRenderTarget(0,&target))&&target){
            textureRead=SUCCEEDED(target->GetDesc(&desc));target->Release();
            if(textureRead){texture={viewport.X,viewport.Y,viewport.Width,viewport.Height,desc.Width,desc.Height};
                matched=NativeMirrorBumpMathKL::Matches(view.world,view.eye,view.projection,matrices,nullptr,nullptr,&texture,nullptr,
                    view.modelViewScale,view.modelViewDepthBias);
                viewportAdjusted=matched;}
        }
    }
    if(!matched){++matrixRejected;Rejected(view,matrices,matricesRead,main,bump,textureRead?&texture:nullptr);return nullptr;}
    float uv[16]{};bool targetMatch=false,previousTarget=false,otherEye=false;
    if(cache.capturedFrame==view.frame||(cache.history&&cache.HasTarget())){
        IDirect3DBaseTexture9* texture=nullptr;
        if(SUCCEEDED(device->GetTexture(sampler,&texture))&&texture){
            for(auto& target:cache.targets)if(target.texture==texture&&
               ((target.camera.frame==view.frame&&target.camera.eyeIndex==view.eyeIndex)||
                (cache.history&&target.surface&&!target.invalidHistory&&target.camera.frame<view.frame))){
                ++targetEligible;targetMatch=NativeMirrorBumpMathKL::TargetProjection(view.world,target.camera.view,
                    target.camera.projection,target.viewport,uv);
                previousTarget=target.camera.frame!=view.frame;
                otherEye=target.camera.eyeIndex!=view.eyeIndex;
                if(targetMatch)target.used=GetTickCount();
                break;}
            texture->Release();
        }
    }
    float saved[4]{};
    if(FAILED(device->GetPixelShaderConstantF(bump,saved,1))){++failed;return nullptr;}
    for(float value:saved)if(!std::isfinite(value)){++failed;return nullptr;}
    if(saved[0]==0&&saved[1]==0&&!targetMatch){++zeroPreserved;return nullptr;}
    current={};current.device=device;current.bump=bump;std::memcpy(current.saved,saved,sizeof(saved));
    if(targetMatch){
        std::memcpy(current.savedUV,matrices+16,sizeof(current.savedUV));current.uvActive=true;
        if(FAILED(device->SetVertexShaderConstantF(4,uv,4))){++failed;current.Restore();return current.failed?&current:nullptr;}
        ++targetApplied;
        if(previousTarget){
            ++cachedTargetApplied;
            if(otherEye)++cachedOtherEyeApplied;
        }
    }
    float stable[4]{0,0,saved[2],saved[3]};current.active=true;
    if(FAILED(device->SetPixelShaderConstantF(bump,stable,1))){++failed;current.Restore();return current.failed?&current:nullptr;}
    ++applied;if(viewportAdjusted)++viewportApplied;return &current;
}
HRESULT FinishDraw(void* token,HRESULT result){
    if(token!=&current)return result;
    if(current.capturing){if(SUCCEEDED(result))cache.Commit(current.target);else ++targetRejected;current.DropTarget();}
    current.Restore();const bool failure=current.failed;current={};
    return failure&&SUCCEEDED(result)?E_FAIL:result;
}
void BeforeCapture(IDirect3DDevice9* device){
    if(cache.owner!=device||tokyoFloorMode.load(std::memory_order_relaxed)!=2)return;
    if(!cache.lobbyUploadAttempted&&NativeTowerEnvironment::state.load(std::memory_order_acquire)==NativeTowerEnvironment::State::Ready){
        cache.lobbyUploadAttempted=true;
        cache.lobbyCube=NativeTowerEnvironment::Upload(device,NativeTowerEnvironment::loaded);
        if(cache.lobbyCube)++towerCubeMatched;
    }
    if(std::none_of(cache.floorPlanar.begin(),cache.floorPlanar.end(),[](const auto& p){return p.pending!=0;}))return;
    cache.CheckForeignWrites();
    const auto now=GetTickCount();
    for(unsigned region=0;region<FloorRegionCount;++region){
        auto& planar=cache.floorPlanar[region];
        const auto serial=planar.pending;planar.pending=0;
        const DWORD interval=planar.nearExit?300:650;
        if(!serial||(planar.lastAttempt&&now-planar.lastAttempt<interval))continue;
        for(const auto& target:cache.targets){
            if(!target.surface||target.invalidHistory||target.camera.serial!=serial||
               target.surface==cache.activeReflection)continue;
            planar.lastAttempt=now;
            planar.Save(device,target,now,region!=TokyoFloor);
            break;
        }
    }
}
void WarmTowerEnvironment(const NativeCameraMath::Vec3& position){
    if(tokyoFloorMode.load(std::memory_order_relaxed)==2&&position.x>=-3500.f&&position.x<=7000.f&&
       position.y>=19000.f&&position.y<=26000.f&&position.z>=-4200.f&&position.z<=4200.f&&
       NativeTowerEnvironment::state.load(std::memory_order_relaxed)==NativeTowerEnvironment::State::Idle)
        NativeTowerEnvironment::Start();
}
void RetireFrameTargets(bool keepSceneHistory,bool keepTokyoFloor,bool bridgeRetomoto){
    current.Restore();current.DropTarget();cache.Finish(keepSceneHistory);
    const auto now=GetTickCount();
    for(unsigned region=0;region<FloorRegionCount;++region){
        auto& floor=cache.floorPlanar[region];floor.pending=0;
        // The tower hands off cameras at the office door and the stairwell.
        // Bridge a short camera gap; loading or a device reset clears it below.
        const bool recentlyVisible=bridgeRetomoto&&region!=TokyoFloor&&floor.lastUse&&now-floor.lastUse<=1000;
        if(!keepTokyoFloor&&!recentlyVisible)floor.Clear();
    }
    if(!keepTokyoFloor&&tokyoFloorMode.load(std::memory_order_relaxed)==2)cache.floorMaps.Clear();
}
void InvalidateDevice(){current.DropTarget();current.Restore();cache.Clear();historyDevice=nullptr;epoch.fetch_add(1,std::memory_order_relaxed);}
Stats Snapshot(){Stats s{seen.load(),recognized.load(),applied.load(),restored.load(),viewRejected.load(),matrixRejected.load(),
    zeroPreserved.load(),failed.load(),restoreFailed.load(),viewportApplied.load(),{}};
    s.towerCubeMatched=towerCubeMatched;s.towerCubeDraws=towerCubeDraws;
    AcquireSRWLockShared(&evidenceLock);s.lastRejected=lastEvidence;ReleaseSRWLockShared(&evidenceLock);
    s.targetCaptured=targetCaptured;s.targetRejected=targetRejected;s.targetEligible=targetEligible;s.targetApplied=targetApplied;s.targetRestored=targetRestored;
    s.cachedTargetApplied=cachedTargetApplied;s.cachedOtherEyeApplied=cachedOtherEyeApplied;s.targetInvalidated=targetInvalidated;
    for(unsigned i=0;i<std::size(s.invalidationReasons);++i)s.invalidationReasons[i]=invalidationReasons[i].load();
    s.historyRetained=historyRetained;s.historySceneRetires=historySceneRetires;
    s.historyForeignWrites=historyForeignWrites;s.historyIneligible=historyIneligible;
    for(unsigned i=0;i<2;++i)s.tokyoFloorEye[i]=tokyoFloorEye[i];
    s.tokyoFloorUntracked=tokyoFloorUntracked;s.tokyoFloorRestored=tokyoFloorRestored;
    s.retomotoFloorLive=retomotoFloorLive;s.retomotoFloorHistory=retomotoFloorHistory;
    s.retomotoFloorFallback=retomotoFloorFallback;s.retomotoFloorCopies=retomotoFloorCopies;
    s.tokyoFloorLive=tokyoFloorLive;s.tokyoFloorHistory=tokyoFloorHistory;s.tokyoFloorFallback=tokyoFloorFallback;
    s.tokyoFloorCopies=tokyoFloorCopies;s.tokyoFloorCopyFailed=tokyoFloorCopyFailed;
    return s;}
}
