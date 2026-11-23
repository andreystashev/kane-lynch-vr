#include "native_effects_math_kl.hpp"
#include "native_effects_shader_kl.hpp"
#include "native_effects_shader_kl_fixture.hpp"
#include "native_effects_image_kl.hpp"
#include "native_effects_producer_kl.hpp"
#include <cstdio>
#include <limits>
#include <map>
#include <array>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
using namespace NativeEffectsKLMath;
unsigned checks=0,failures=0;
void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL: %s\n",text);}}
bool Near(Vec3 a,Vec3 b,float tolerance=.001f){return Dot(Subtract(a,b),Subtract(a,b))<=tolerance*tolerance;}
CameraPose Eye(float yaw,float pitch,float roll,Vec3 centre,float offset){
    const Vec3 f{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const Vec3 r{std::cos(yaw),0,-std::sin(yaw)},u=Cross(f,r);
    const Vec3 right=Add(Scale(r,std::cos(roll)),Scale(u,std::sin(roll)));
    const Vec3 up=Subtract(Scale(u,std::cos(roll)),Scale(r,std::sin(roll)));
    return {{f,up,right},Add(centre,Scale(right,offset))};
}
void Maths(){
    const Vec3 centre{20,150,-30},object{100,230,700};
    const auto right=Scale(Cross({0,1,0},Subtract(object,centre)),1/std::sqrt((object.x-centre.x)*(object.x-centre.x)+(object.z-centre.z)*(object.z-centre.z)));
    for(float yaw:{-2.4f,-.8f,0.f,.8f,2.4f})for(float pitch:{-1.f,0.f,1.f})for(float roll:{-1.3f,0.f,1.3f})for(float side:{-3.2f,3.2f}){
        const auto eye=Eye(yaw,pitch,roll,centre,side);float columns[8]{};
        Check(Columns(eye,centre,columns),"world-up and frozen stereo-centre columns qualify");
        const Vec3 nativeCentre=ToView(eye.basis,Subtract(object,eye.position));
        for(float angle:{-.5f,0.f,.9f}){
            Vertex native[4],corrected[4];
            const float x[4]={-30,-30,30,30},y[4]={-70,70,70,-70};
            for(unsigned i=0;i<4;++i){const float dx=x[i]*std::cos(angle)-y[i]*std::sin(angle),dy=x[i]*std::sin(angle)+y[i]*std::cos(angle);
                native[i]={nativeCentre.x+dx,nativeCentre.y+dy,nativeCentre.z,0x10203040u+i,float(i)*.25f,float(i)*.1f};}
            Check(Quad(native,columns,corrected),"exact view-space world quad converts without moving its centre");
            Vec3 resultCentre{};
            for(unsigned i=0;i<4;++i){const float dx=native[i].x-nativeCentre.x,dy=native[i].y-nativeCentre.y;
                const auto world=Add(object,Add(Scale(right,dx),Vec3{0,dy,0}));
                const auto expected=ToView(eye.basis,Subtract(world,eye.position));
                Check(Near({corrected[i].x,corrected[i].y,corrected[i].z},expected),"raster point agrees with independent fixed world plane under yaw/pitch/roll and IPD");
                Check(corrected[i].colour==native[i].colour&&corrected[i].u==native[i].u&&corrected[i].v==native[i].v,"colour UV size and particle angle retained");
                resultCentre=Add(resultCentre,Scale(Vec3{corrected[i].x,corrected[i].y,corrected[i].z},.25f));
            }
            Check(Near(resultCentre,nativeCentre),"world sprite centre remains unchanged in view");
        }
    }
    float columns[8]{};const auto eye=Eye(0,0,0,{},0);Check(Columns(eye,{},columns),"identity world effect columns");
    Vertex batch[8];for(unsigned q=0;q<2;++q)for(unsigned i=0;i<4;++i)batch[q*4+i]={float(i<2?-10:10),float(i==0||i==3?-20:20),500+float(q)*100,0x12345678,.1f,.2f};
    Vertex before[8];std::memcpy(before,batch,sizeof(batch));batch[7].x=std::numeric_limits<float>::infinity();
    Vertex invalid[8];std::memcpy(invalid,batch,sizeof(batch));
    Check(!MappedQuads(batch,sizeof(batch),columns)&&!std::memcmp(batch,invalid,sizeof(batch)),"invalid later quad cannot partially patch an earlier quad");
    std::memcpy(batch,before,sizeof(batch));
    Check(!MappedQuads(batch,sizeof(batch)-1,columns)&&!MappedQuads(batch,2048*4*sizeof(Vertex)+1,columns)&&!MappedQuads(nullptr,0,columns),"partial oversized and empty spans are rejected");
    Check(MappedQuads(batch,sizeof(batch),columns),"complete bounded mapped span is corrected in place");
    float projection[16]{};projection[0]=.7f;projection[5]=1.1f;float rollColumns[8]{};
    Check(RollColumns(Eye(0,.4f,.6f,{},0),projection,rollColumns),"SpriteBox only rotates its screen-corner offset with asymmetric FOV");
    auto invalidEye=eye;invalidEye.basis.up.x=std::numeric_limits<float>::quiet_NaN();
    Check(!Columns(invalidEye,{},columns)&&!RollColumns(invalidEye,projection,rollColumns),"invalid tracked view never supplies effect constants");
    float p[16]={.92f,0,0,0,0,1.07f,0,0,.13f,-.08f,30.f/29.9f,1,0,0,-.1f*30.f/29.9f,0},transposed[16]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)transposed[r*4+c]=p[c*4+r];
    Check(ProjectionMatches(p,p)&&ProjectionMatches(p,transposed),"exact native eye projection accepts row and native transposed uniform storage with D3D depth range");
    transposed[0]*=1.1f;Check(!ProjectionMatches(p,transposed),"a special or shadow projection cannot reuse main-camera effect correction");
    float zero[16]{};Check(!ProjectionMatches(zero,zero),"zero or missing projection never qualifies");
}
void Shaders(){
    const std::vector<std::uint32_t>* programs[]={&NativeScatterAmbient,&NativeScatterZPass,&NativeScatterNewAmbient,&NativeScatterNewZPass,&NativeSpriteBox};
    for(unsigned i=0;i<5;++i){std::vector<std::uint32_t> patched,again;unsigned family=0;
        Check(NativeEffectsKLShader::Patch(*programs[i],patched,&family)&&family==i+1&&!patched.empty(),"only exact native family qualifies");
        auto commented=*programs[i];commented.insert(commented.begin()+1,{0x0002FFFEu,0x42415443u,0x12345678u});
        Check(NativeEffectsKLShader::Patch(commented,again)&&again==patched,"compiler CTAB metadata never changes program qualification");
        for(unsigned position:{2u,unsigned(programs[i]->size()/2),unsigned(programs[i]->size()-2)}){
            auto altered=*programs[i];altered[position]^=1;
            Check(!NativeEffectsKLShader::Patch(altered,again,&family)&&again.empty()&&!family,"changed instruction or operand cannot become another world shader");
        }
        Check(patched[0]==(i==3?0xFFFE0200u:(*programs[i])[0]),"only long ScatterNew depth correction upgrades its shader model");
    }
    std::vector<std::uint32_t> out;
    Check(!NativeEffectsKLShader::Patch({0xFFFE0101u,0xFFFFFEu},out)&&
          !NativeEffectsKLShader::Patch({0xFFFE0101u,0x0008FFFEu,0xFFFFu},out),"malformed comments and truncated shaders fail closed");
    const std::vector<std::uint32_t>* observed[]={&NativeManualAmbient,&NativeManualZPass,&NativeCpuSprite,&NativeSpriteRain};
    for(unsigned i=0;i<4;++i){
        Check(NativeEffectsKLShader::ObservedFamily(*observed[i])==i+6,"exact CPU ManualScatter/Sprite and unchanged Rain families are diagnosed");
        if(i==2){unsigned family=0;Check(NativeEffectsKLShader::Patch(*observed[i],out,&family)&&family==8,"exact ordinary Sprite has a source-qualified radial fog replacement");}
        else Check(!NativeEffectsKLShader::Patch(*observed[i],out)&&out.empty(),"Manual/Rain shaders are never replaced");
        auto changed=*observed[i];changed[changed.size()/2]^=1;
        Check(!NativeEffectsKLShader::ObservedFamily(changed),"modified CPU shader cannot qualify by prefix");
    }
}
void Manual(){
    const Vec3 centre{100,140,-70},object{300,260,900};
    for(float referenceYaw:{-.9f,.3f,1.6f})for(float yaw:{-1.1f,0.f,.8f})for(float pitch:{-.6f,.5f})for(float roll:{-.7f,.4f})for(float side:{-3.2f,3.2f}){
        const auto reference=Eye(referenceYaw,.2f,-.1f,centre,0),eye=Eye(yaw,pitch,roll,centre,side);
        const auto inView=ToView(eye.basis,Subtract(object,eye.position));
        ManualVertex input[4],output[4];
        // Native manual scatter's authored tilted, wind-rotated plane has
        // genuine Z offsets. Treating it as flat XYZ/colour/UV would damage it.
        const Vec3 a{28,9,-12},b{-3,63,19};const float x[]{-1,-1,1,1},y[]{-1,1,1,-1};
        Vec3 mean{};
        for(unsigned i=0;i<4;++i){const auto d=Add(Scale(a,x[i]),Scale(b,y[i]));
            const auto p=Add(inView,d);input[i]={p.x,p.y,p.z,0xC0123400u+i,0xFF000000u+i,float(i)*.17f,float(i)*.23f};}
        Check(ManualQuad(input,eye,reference,output),"pinned ManualScatter non-flat authored quad qualifies");
        for(unsigned i=0;i<4;++i){const auto d=Subtract(Vec3{input[i].x,input[i].y,input[i].z},inView);
            const auto world=Add(object,Add(Add(Scale(reference.basis.right,d.x),Scale(reference.basis.up,d.y)),Scale(reference.basis.forward,d.z)));
            const auto expected=ToView(eye.basis,Subtract(world,eye.position));
            Check(Near({output[i].x,output[i].y,output[i].z},expected),"ManualScatter corners remain in reference world axes under HMD yaw pitch roll and stereo");
            Check(!std::memcmp(reinterpret_cast<const unsigned char*>(input+i)+12,reinterpret_cast<const unsigned char*>(output+i)+12,16),"ManualScatter colour/private index/UV remain bit exact");
            mean=Add(mean,Scale(Vec3{output[i].x,output[i].y,output[i].z},.25f));
        }
        Check(Near(mean,inView),"ManualScatter original world centre remains unchanged");
        Check(ManualQuad(input,reference,reference,output),"neutral ManualScatter reference qualifies");
        for(unsigned i=0;i<4;++i)Check(Near({input[i].x,input[i].y,input[i].z},{output[i].x,output[i].y,output[i].z}),"neutral camera retains native authored offsets");
    }
    ManualVertex batch[8]{};const auto eye=Eye(.4f,.2f,.3f,{},0),reference=Eye(0,0,0,{},0);
    for(unsigned q=0;q<2;++q)for(unsigned i=0;i<4;++i)batch[q*4+i]={float(i<2?-10:10),float(i==0||i==3?-20:20),500+float(q)*100,1,77,.1f,.2f};
    auto saved=std::array<ManualVertex,8>{};std::memcpy(saved.data(),batch,sizeof(batch));batch[7].z+=2;
    auto bad=saved;std::memcpy(bad.data(),batch,sizeof(batch));
    Check(!MappedManualQuads(batch,sizeof(batch),eye,reference)&&!std::memcmp(batch,bad.data(),sizeof(batch)),"non-parallelogram later manual quad cannot partially mutate earlier data");
    std::memcpy(batch,saved.data(),sizeof(batch));
    Check(!MappedManualQuads(batch,sizeof(batch)-1,eye,reference)&&!MappedManualQuads(batch,2048*4*sizeof(ManualVertex)+1,eye,reference),"manual partial/oversized native spans remain untouched");
    auto invalid=eye;invalid.basis.up.x=std::numeric_limits<float>::infinity();
    Check(!MappedManualQuads(batch,sizeof(batch),invalid,reference)&&!std::memcmp(batch,saved.data(),sizeof(batch)),"invalid actual/reference view cannot mutate manual vertices");
    Check(MappedManualQuads(batch,sizeof(batch),eye,reference),"complete bounded ManualScatter native batch transforms before Unlock");
}
void Producer(){
    std::map<std::uintptr_t,std::uint32_t> memory;
    const auto read=[&](auto at,auto& out){auto i=memory.find(at);if(i==memory.end())return false;out=static_cast<std::decay_t<decltype(out)>>(i->second);return true;};
    constexpr std::uintptr_t image=0x400000,binder=0x100000,entry=0x110000,geometry=0x120000,state=0x130000,frame=0x140000;
    memory[binder]=image+0x53FB88;memory[binder+0xC]=entry;memory[entry]=image+0x540EAC;memory[image+0x7678A4]=1;
    memory[entry+0x108+20+0xC]=7;memory[entry+0x104]=geometry;memory[geometry+0x30]=0;unsigned mode=~0u;
    Check(NativeEffectsKLProducer::ParticleSource(image,binder,state,read,mode)&&mode==0,"exact native type7 mode0 source qualifies");
    memory[geometry+0x30]=1;Check(NativeEffectsKLProducer::ParticleSource(image,binder,state,read,mode)&&mode==1,"velocity-aligned source is identified separately, never mistaken for mode0");
    memory[geometry+0x30]=0;
    for(auto at:{binder,entry,entry+0x108+20+0xC,image+0x7678A4}){const auto saved=memory[at];memory[at]=~0u;
        Check(!NativeEffectsKLProducer::ParticleSource(image,binder,state,read,mode),"foreign binder entry type or buffer slot cannot qualify particle source");memory[at]=saved;}
    memory.erase(geometry+0x30);Check(!NativeEffectsKLProducer::ParticleSource(image,binder,state,read,mode),"unreadable particle mode retains original");
    memory[frame+8+0x74]=3;memory[frame+8+0x94]=5;unsigned live=99;
    Check(NativeEffectsKLProducer::ParticlePrefix(0x9A19B,frame,5,read,live)&&live==3,"exact native post-call stack yields live prefix, not allocation tail");
    Check(!NativeEffectsKLProducer::ParticlePrefix(0x9A19C,frame,5,read,live)&&!NativeEffectsKLProducer::ParticlePrefix(0x9A19B,frame,4,read,live),"wrong return PC or native batch count cannot authorize stack interpretation");
    memory[frame+8+0x74]=6;Check(!NativeEffectsKLProducer::ParticlePrefix(0x9A19B,frame,5,read,live),"native live count must fit allocation");
    memory[frame]=0x150000;memory[0x150000+0x3F10]=state;std::uintptr_t source=0;
    Check(NativeEffectsKLProducer::ManualState(0xA3EA4,frame,read,source)&&source==state,"exact ManualScatter caller frame identifies original source state");
    Check(!NativeEffectsKLProducer::ManualState(0xA3EA5,frame,read,source),"unrelated native callers never receive ManualScatter frame interpretation");
}
void Radial(){
    const Vec3 centre{17,120,-33},object{130,240,650};const auto radial=Subtract(object,centre);
    const float distance=std::sqrt(Dot(radial,radial));bool nativeChanged=false;float previousDepth=0;
    for(float yaw:{-.7f,0.f,.8f})for(float pitch:{-.4f,.6f})for(float roll:{-.5f,.3f})for(float side:{-3.2f,3.2f}){
        const auto eye=Eye(yaw,pitch,roll,centre,side);float columns[8]{};Check(Columns(eye,centre,columns),"radial effect same-pair constants qualify");
        const auto view=ToView(eye.basis,Subtract(object,eye.position));const auto point=Add(view,{columns[4],columns[5],columns[6]});
        const float actual=std::sqrt(Dot(point,point));
        Check(std::fabs(actual-distance)<.001f,"fixed world point retains identical radial fog/fade distance under head rotation and IPD");
        const float expectedFog=1-distance/1000.f,actualFog=1-actual/1000.f;
        Check(std::fabs(expectedFog-actualFog)<1e-5f,"authored linear fog profile stays invariant without changing projected position");
        nativeChanged=nativeChanged||(previousDepth&&std::fabs(previousDepth-view.z)>10);previousDepth=view.z;
        const float world[12]={0,0,1.4f,0,.7f,.2f,1.1f,0,0,40,100,200};float model[16]{};
        Check(WorldViewShader(world,eye,model),"raw native scaled model world converts to its exact shader WV contract");
        const Vec3 local{13,17,19},worldPoint{40+13*1.1f,100+17*.7f,200+17*.2f+19*1.4f};
        const auto expected=ToView(eye.basis,Subtract(worldPoint,eye.position));
        const Vec3 transformed{local.x*model[0]+local.y*model[4]+local.z*model[8]+model[12],
            local.x*model[1]+local.y*model[5]+local.z*model[9]+model[13],local.x*model[2]+local.y*model[6]+local.z*model[10]+model[14]};
        Check(Near(expected,transformed),"MV diagnostic uses independent world geometry, not a guessed row/column camera sign");
    }
    Check(nativeChanged,"native view-Z fog/fade changes with headset orientation for the same world point");
}
template<class V>std::vector<V> MappedSeed(unsigned quads){
    std::vector<V> out(quads*4);
    for(unsigned q=0;q<quads;++q)for(unsigned i=0;i<4;++i){auto& v=out[q*4+i];
        v.x=float(q%64)*2.f+float(i<2?-1:1);v.y=float(q/64)*2.f+float(i==0||i==3?-1:1);v.z=300.f+float(q%7);
        v.colour=0x8F00AB00u+q;v.u=float(i)*.25f;v.v=float(i)*.125f;
        if constexpr(sizeof(V)==sizeof(ManualVertex))v.index=0xFFFF0000u+q;
    }
    return out;
}
void MappedSnapshots(){
    const auto eye=Eye(.4f,.2f,.3f,{},0),reference=Eye(0,0,0,{},0);float columns[8]{};
    Check(Columns(eye,{},columns),"mapped regression camera qualifies");
    auto vertices=MappedSeed<Vertex>(2048);auto manual=MappedSeed<ManualVertex>(2048);
    auto expected=vertices;auto manualExpected=manual;
    for(unsigned q=0;q<2048;++q){Check(Quad(vertices.data()+q*4,columns,expected.data()+q*4),"max sprite independent quad qualifies");
        Check(ManualQuad(manual.data()+q*4,eye,reference,manualExpected.data()+q*4),"max manual independent quad qualifies");}
    auto actual=vertices;
    Check(MappedQuads(actual.data(),actual.size()*sizeof(Vertex),columns)&&!std::memcmp(actual.data(),expected.data(),actual.size()*sizeof(Vertex)),
        "maximum sprite batch matches per-quad output and preserves all native attributes");
    auto actualManual=manual;
    Check(MappedManualQuads(actualManual.data(),actualManual.size()*sizeof(ManualVertex),eye,reference)&&
        !std::memcmp(actualManual.data(),manualExpected.data(),actualManual.size()*sizeof(ManualVertex)),
        "maximum manual batch fits shared cache and preserves private index plus color and UV");
    // A rejected final quad has already dirtied the scratch cache. Neither
    // its original allocation nor the next shorter writer may see those bytes.
    for(unsigned kind=0;kind<3;++kind){
        if(kind==1){actualManual=manual;actualManual.back().z+=2.f;const auto invalid=actualManual;
            Check(!MappedManualQuads(actualManual.data(),actualManual.size()*sizeof(ManualVertex),eye,reference)&&
                !std::memcmp(actualManual.data(),invalid.data(),actualManual.size()*sizeof(ManualVertex)),"invalid final maximum manual quad leaves original completely unchanged");}
        else {actual=vertices;actual.back().z=std::numeric_limits<float>::infinity();const auto invalid=actual;
            const bool result=kind==0?MappedQuads(actual.data(),actual.size()*sizeof(Vertex),columns):MappedParticleQuads(actual.data(),2048,eye,reference);
            Check(!result&&!std::memcmp(actual.data(),invalid.data(),actual.size()*sizeof(Vertex)),"invalid final maximum sprite/particle quad leaves original completely unchanged");}
        std::array<Vertex,5> shortSpan{};std::memcpy(shortSpan.data(),vertices.data(),4*sizeof(Vertex));shortSpan[4]={1,2,3,0xABABCDEFu,.2f,.3f};const auto tail=shortSpan[4];
        Check(MappedQuads(shortSpan.data(),4*sizeof(Vertex),columns)&&!std::memcmp(shortSpan.data(),expected.data(),4*sizeof(Vertex))&&
            !std::memcmp(&shortSpan[4],&tail,sizeof(tail)),"short writer overwrites prior cache contents and leaves adjacent tail untouched");
    }
    actual=vertices;auto particleExpected=vertices;
    for(unsigned q=0;q<2048;++q)Check(ReferenceQuad(vertices.data()+q*4,eye,reference,particleExpected.data()+q*4),"max particle independent quad qualifies");
    Check(MappedParticleQuads(actual.data(),2048,eye,reference)&&!std::memcmp(actual.data(),particleExpected.data(),actual.size()*sizeof(Vertex)),
        "maximum live particle prefix matches per-quad output");
    std::atomic<bool> start{false},okay{true};
    const auto worker=[&](bool isManual){while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned repeat=0;repeat<32;++repeat){if(isManual){auto batch=manual;
                if(!MappedManualQuads(batch.data(),batch.size()*sizeof(ManualVertex),eye,reference)||std::memcmp(batch.data(),manualExpected.data(),batch.size()*sizeof(ManualVertex)))okay=false;
            }else{auto batch=vertices;if(!MappedQuads(batch.data(),batch.size()*sizeof(Vertex),columns)||std::memcmp(batch.data(),expected.data(),batch.size()*sizeof(Vertex)))okay=false;}}};
    std::thread a(worker,false),b(worker,true);start.store(true,std::memory_order_release);a.join();b.join();
    Check(okay,"parallel native writers have independent bounded scratch storage");
#ifdef _WIN32
    SYSTEM_INFO system{};GetSystemInfo(&system);const auto page=system.dwPageSize;
    auto* area=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*3,MEM_RESERVE,PAGE_NOACCESS));
    const bool committed=area&&VirtualAlloc(area+page,page,MEM_COMMIT,PAGE_READWRITE);
    Check(committed,"guarded native allocation reserved");
    if(committed){
        auto* live=area+2*page-4*sizeof(Vertex);std::memcpy(live,vertices.data(),4*sizeof(Vertex));
        Check(MappedParticleQuads(live,1,eye,reference)&&!std::memcmp(live,particleExpected.data(),4*sizeof(Vertex)),
            "particle path reads and writes only proven live prefix before inaccessible dead tail");
        Check(MappedParticleQuads(area,0,eye,reference),"empty live prefix never reads or writes inaccessible dead allocation");
        for(unsigned kind=0;kind<3;++kind){const auto bytes=kind==1?4*sizeof(ManualVertex):4*sizeof(Vertex);auto* span=area+2*page-bytes;
            std::memcpy(span,kind==1?static_cast<const void*>(manual.data()):vertices.data(),bytes);
            const float invalid=std::numeric_limits<float>::infinity();std::memcpy(span+bytes-(kind==1?sizeof(ManualVertex):sizeof(Vertex)),&invalid,sizeof(invalid));
            DWORD old=0;Check(VirtualProtect(area+page,page,PAGE_READONLY,&old)!=FALSE,"rejected mapped span is physically read only");
            const bool rejected=kind==0?!MappedQuads(span,bytes,columns):kind==1?!MappedManualQuads(span,bytes,eye,reference):!MappedParticleQuads(span,1,eye,reference);
            Check(rejected,"invalid batch never writes any native bytes before complete validation");
            Check(VirtualProtect(area+page,page,PAGE_READWRITE,&old)!=FALSE,"restore guarded fixture page");
        }
    }
    if(area)Check(VirtualFree(area,0,MEM_RELEASE)!=FALSE,"guarded native allocation released");
#endif
}
}
int main(){Maths();Shaders();Manual();Producer();Radial();MappedSnapshots();std::printf("Native anchored world effects: %u checks, %u failures.\n",checks,failures);return failures?1:0;}
