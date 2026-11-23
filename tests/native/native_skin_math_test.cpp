#include "native_skin_math.hpp"
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <fstream>
#include <iterator>
#include <vector>

using namespace NativeCameraMath;
namespace {
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
bool Near(float a,float b){return std::fabs(a-b)<.0001f;}
bool Near(Vec3 a,Vec3 b){return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z);}
Vec3 Apply(const float p[12],Vec3 point){return {p[0]*point.x+p[1]*point.y+p[2]*point.z+p[3],
    p[4]*point.x+p[5]*point.y+p[6]*point.z+p[7],p[8]*point.x+p[9]*point.y+p[10]*point.z+p[11]};}
CameraPose Pose(Quat q,Vec3 position) {
    return {{Rotate(q,{0,0,1}),Rotate(q,{0,1,0}),Rotate(q,{1,0,0})},position};
}
struct SourceMemory {
    static constexpr std::uint32_t Image=0x400000,Entry=0x10000000,State=0x11000000,Cache=0x12000000;
    static constexpr std::uint32_t List=0x13000000,Rows=0x14000000,Alternate=0x15000000;
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T>void Set(std::uintptr_t at,const T& value){
        const auto* source=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i)bytes[at+i]=source[i];
    }
    bool Read(std::uintptr_t at,void* target,std::size_t count)const{
        auto* output=static_cast<unsigned char*>(target);
        for(std::size_t i=0;i<count;++i){const auto found=bytes.find(at+i);if(found==bytes.end())return false;output[i]=found->second;}return true;
    }
    auto Reader()const{return [&](std::uintptr_t at,void* target,std::size_t count){return Read(at,target,count);};}
    void Row(unsigned index,const CameraPose& root,std::uint32_t palette){
        unsigned char row[0x38]{};std::memcpy(row,&root,sizeof(root));std::memcpy(row+0x30,&palette,4);Set(Rows+index*0x38,row);
    }
    SourceMemory(){Set(State+0x3BC,Cache+0x90);Set(State+0x8C,Pose({0,0,0,1},{10,20,30}));}
    void UseAlternate(){
        Set(Image+0x7678A4,std::uint32_t(1));Set(Entry+0x94,List);Set(List,std::uint32_t(2));Set(List+4,Rows);
        Set(State+0x3C4,std::uint32_t(1));Set(State+0x3BC,Alternate);
        const auto root=Pose({0,std::sqrt(.5f),0,std::sqrt(.5f)},{-15,8,44});
        Set(State+0x8C,root);Row(0,Pose({0,0,0,1},{100,200,300}),Alternate+0x1000);Row(1,root,Alternate);
    }
    bool Resolve(NativeSkinMath::PaletteSource& output,bool alternateVerified=true)const{
        return NativeSkinMath::ResolvePaletteSource(Image,Entry,State,Cache,64,Reader(),output,alternateVerified);
    }
};
void PaletteSources() {
    using NativeSkinMath::PaletteSource;
    SourceMemory memory;PaletteSource source;
    const auto initial=memory.bytes;
    Check(memory.Resolve(source)&&!source.alternate&&source.palette==SourceMemory::Cache+0x90&&
          Near(source.root.position,{10,20,30}),"direct palette uses the actual native draw world, without speculative instance traversal");
    Check(memory.bytes==initial,"palette-source qualification never writes native caches or matrices");
    Check(memory.Resolve(source,false),"primary native palette still works when alternate layout is unavailable");
    memory.UseAlternate();const auto before=memory.bytes;
    Check(memory.Resolve(source)&&source.alternate&&source.index==1&&source.instances==2&&
          source.palette==SourceMemory::Alternate&&Near(source.root.position,{-15,8,44}),
          "exact selected native instance supplies both its own world root and its own palette");
    Check(memory.bytes==before,"instance palette lookup remains read-only");
    const auto original=source;
    Check(!memory.Resolve(source,false)&&NativeSkinMath::SamePaletteSource(source,original),
          "unproved alternate layout retains native draw and leaves output unchanged");
    for(unsigned broken=0;broken<12;++broken){
        memory=SourceMemory{};memory.UseAlternate();source=original;
        switch(broken){
        case 0:memory.Set(SourceMemory::Image+0x7678A4,std::uint32_t(2));break;
        case 1:memory.Set(SourceMemory::Entry+0x94,std::uint32_t(0));break;
        case 2:memory.Set(SourceMemory::List,std::uint32_t(0));break;
        case 3:memory.Set(SourceMemory::List,std::uint32_t(1025));break;
        case 4:memory.Set(SourceMemory::State+0x3C4,std::uint32_t(2));break;
        case 5:memory.Set(SourceMemory::List+4,std::uint32_t(0xFFFFFFF0u));break;
        case 6:memory.Set(SourceMemory::State+0x3BC,std::uint32_t(SourceMemory::Alternate+4));break;
        case 7:memory.Set(SourceMemory::Rows+0x38+0x30,std::uint32_t(SourceMemory::Alternate+4));break;
        case 8:memory.Set(SourceMemory::State+0x8C,Pose({0,0,0,1},{0,0,0}));break;
        case 9:memory.Set(SourceMemory::State+0x8C,std::numeric_limits<float>::quiet_NaN());break;
        case 10:memory.bytes.erase(SourceMemory::Rows+0x38);break;
        case 11:memory.Set(SourceMemory::Rows+0x38+0x30,std::uint32_t(0xFFFFFFF0u));memory.Set(SourceMemory::State+0x3BC,std::uint32_t(0xFFFFFFF0u));break;
        }
        Check(!memory.Resolve(source)&&NativeSkinMath::SamePaletteSource(source,original),
              "bad index/count, foreign palette, wrong root, overflow and partial rows fail without a partial override");
    }
    memory=SourceMemory{};memory.UseAlternate();
    auto first=original,second=original;
    Check(memory.Resolve(first)&&memory.Resolve(second)&&NativeSkinMath::SamePaletteSource(first,second),
          "unchanged native source survives late binding revalidation");
    auto moved=first.root;moved.position.x+=1;memory.Set(SourceMemory::State+0x8C,moved);memory.Row(1,moved,SourceMemory::Alternate);
    Check(memory.Resolve(second)&&!NativeSkinMath::SamePaletteSource(first,second),
          "a changed native instance world cannot reuse another body's prepared staging");
    memory=SourceMemory{};memory.UseAlternate();memory.Set(SourceMemory::State+0x3BC,std::uint32_t(SourceMemory::Alternate+0x1000));
    memory.Row(1,first.root,SourceMemory::Alternate+0x1000);
    Check(memory.Resolve(second)&&!NativeSkinMath::SamePaletteSource(first,second),
          "a changed native palette cannot reuse a preview or previous instance's head mask");
    memory=SourceMemory{};memory.UseAlternate();memory.Set(SourceMemory::List,std::uint32_t(1024));
    memory.Set(SourceMemory::State+0x3C4,std::uint32_t(1023));memory.Row(1023,first.root,SourceMemory::Alternate);
    Check(memory.Resolve(second)&&second.index==1023,"bounded native instance array permits its final valid row");
    Check(!NativeSkinMath::ResolvePaletteSource(0,SourceMemory::Entry,SourceMemory::State,SourceMemory::Cache,64,memory.Reader(),source)&&
          !NativeSkinMath::ResolvePaletteSource(SourceMemory::Image,SourceMemory::Entry,SourceMemory::State,SourceMemory::Cache,1,memory.Reader(),source)&&
          !NativeSkinMath::ResolvePaletteSource(SourceMemory::Image,SourceMemory::Entry,0xFFFFFFF0u,SourceMemory::Cache,64,memory.Reader(),source),
          "invalid module, palette count or wrapped native state are rejected before traversal");
}
std::uint32_t Read32(const std::vector<unsigned char>& bytes,std::size_t at){std::uint32_t v=0;if(at+4<=bytes.size())std::memcpy(&v,bytes.data()+at,4);return v;}
std::uint16_t Read16(const std::vector<unsigned char>& bytes,std::size_t at){std::uint16_t v=0;if(at+2<=bytes.size())std::memcpy(&v,bytes.data()+at,2);return v;}
void VerifyActualPaletteImage(const char* path){
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
    Check(!raw.empty(),"actual game image is readable without launching it");if(raw.empty())return;
    const auto nt=Read32(raw,0x3C),optional=nt+24,base=Read32(raw,optional+28),size=Read32(raw,optional+56);
    Check(Read16(raw,0)==0x5A4D&&Read32(raw,nt)==0x4550&&Read16(raw,nt+4)==0x14C&&size&&size<0x2000000,
          "actual binder qualification uses a bounded x86 PE image");
    if(Read16(raw,0)!=0x5A4D||Read32(raw,nt)!=0x4550||Read16(raw,nt+4)!=0x14C||!size||size>=0x2000000)return;
    std::vector<unsigned char> mapped(size);const auto sections=Read16(raw,nt+6);const auto table=optional+Read16(raw,nt+20);
    for(unsigned i=0;i<sections;++i){const auto at=table+i*40,rva=Read32(raw,at+12),count=Read32(raw,at+16),offset=Read32(raw,at+20);
        if(rva<=mapped.size()&&count<=mapped.size()-rva&&offset<=raw.size()&&count<=raw.size()-offset)
            std::memcpy(mapped.data()+rva,raw.data()+offset,count);}
    const auto read=[&](std::uintptr_t at,void* output,std::size_t bytes){
        if(at<base||at-base>mapped.size()||bytes>mapped.size()-(at-base))return false;
        std::memcpy(output,mapped.data()+at-base,bytes);return true;};
    Check(NativeSkinMath::VerifyPaletteSourceImage(base,read),"actual native count, stride, row root and palette consumer match the qualified layout");
    mapped[0x72063]^=1;
    Check(!NativeSkinMath::VerifyPaletteSourceImage(base,read),"changed instance consumer disables alternate palette support");
}
}
int main(int argc,char** argv) {
    PaletteSources();if(argc>1)VerifyActualPaletteImage(argv[1]);
    const std::uint32_t parents[3]={0,0,1};
    NativeSkinMath::BindLocal local[3]{};local[1].position={0,10,0};local[2].position={10,0,0};
    const float s=std::sqrt(.5f);local[2].rotation={0,0,s,s};
    CameraPose bind[3];Check(NativeSkinMath::BindPose(local,parents,3,bind),"hierarchical bind quaternion pose builds");
    Check(Near(bind[2].position,{10,10,0}),"child bind translation uses its actual parent");
    Check(Near(NativeRigMath::Transform(bind[2].basis,{1,0,0}),{0,1,0}),"bind quaternion XYZW and R/U/F axis order agree");
    for(unsigned i=0;i<3;++i) {
        const NativeRigMath::Bone pose{bind[i].basis,bind[i].position};float palette[12];
        Check(NativeSkinMath::Palette(bind[i],pose,palette),"native reflected FUR basis produces a proper GPU palette");
        Check(Near(Apply(palette,{2,13,4}),{2,13,4}),"bind pose skin is identity for an arbitrary vertex");
    }
    const NativeRigMath::Bone animated{{{1,0,0},{0,1,0},{0,0,-1}},{3,20,5}};
    float palette[12];Check(NativeSkinMath::Palette(bind[1],animated,palette),"animated palette builds");
    Check(Near(Apply(palette,{2,13,4}),{7,23,3}),"GPU three float4 rows map bind vertices through inverse bind and animated model pose");
    Check(Near(Apply(palette,bind[1].position),animated.position),"wrist bind origin lands at exact tracked native wrist");
    NativeRigMath::Bone recovered;
    Check(NativeSkinMath::PoseFromPalette(bind[1],palette,recovered),"original GPU pose recovers without reading mutable CPU animation");
    Check(Near(recovered.position,animated.position)&&Near(recovered.basis.forward,animated.basis.forward)&&
          Near(recovered.basis.up,animated.basis.up)&&Near(recovered.basis.right,animated.basis.right),"GPU palette and bind reconstruct the exact original cumulative pose");
    for(unsigned sample=0;sample<72;++sample) {
        const float angle=float(sample)*.0872664626f;
        const float sine=std::sin(angle*.5f),cosine=std::cos(angle*.5f);
        const CameraPose rest=Pose({sine*.26726124f,sine*.53452248f,sine*.80178373f,cosine},{12,-8,21});
        const CameraPose moved=Pose({sine*.70710678f,-sine*.70710678f,0,cosine},{-17,39,4});
        const NativeRigMath::Bone original{moved.basis,moved.position};
        float skin[12];NativeRigMath::Bone result;
        Check(NativeSkinMath::Palette(rest,original,skin),"rotated bind and animated palette builds");
        Check(NativeSkinMath::PoseFromPalette(rest,skin,result),"GPU original reconstructs across full rotation range");
        Check(Near(result.position,original.position)&&Near(result.basis.forward,original.basis.forward)&&
              Near(result.basis.up,original.basis.up)&&Near(result.basis.right,original.basis.right),"GPU recovery retains all original animated axes and position");
        const Vec3 bindVertex{5,13,-2};
        const Vec3 expected=Add(original.position,NativeRigMath::Transform(original.basis,
            NativeRigMath::Inverse(rest.basis,Subtract(bindVertex,rest.position))));
        Check(Near(Apply(skin,bindVertex),expected),"skin maps independent bind vertex through the original animated pose");
    }
    {
        constexpr unsigned count=8,upper=3,lower=4,wrist=5;
        const std::uint32_t hierarchy[count]={0,0,1,1,3,4,5,0};
        NativeSkinMath::BindLocal restLocal[count]{};
        restLocal[1].position={0,110,0};restLocal[2].position={0,40,0};
        restLocal[3].position={18,28,0};restLocal[4].position={8,-24,1};
        restLocal[5].position={9,-22,-3};restLocal[6].position={6,0,0};restLocal[7].position={-10,40,0};
        CameraPose rest[count];Check(NativeSkinMath::BindPose(restLocal,hierarchy,count,rest),"GPU arm test has complete native bind hierarchy");
        std::array<std::array<float,12>,count> native{};
        std::array<NativeRigMath::Bone,count> raw{},posed{};
        for(unsigned i=0;i<count;++i) {
            const NativeRigMath::Bone bone{rest[i].basis,rest[i].position};
            Check(NativeSkinMath::Palette(rest[i],bone,native[i].data()),"original native palette builds for test skeleton");
        }
        // Unused native slots may carry a special matrix. They must remain
        // exact bytes even when they cannot be interpreted as a rigid bone.
        native[0][0]=std::numeric_limits<float>::quiet_NaN();
        const auto source=native;auto gpu=source;
        for(unsigned i=upper;i<=6;++i)
            Check(NativeSkinMath::PoseFromPalette(rest[i],source[i].data(),raw[i]),"arm input comes from original GPU palette");
        const CameraPose target{rest[wrist].basis,Add(rest[upper].position,{0,-25,24})};
        NativeRigMath::Result solve;
        Check(NativeRigMath::SolveArm(raw.data(),hierarchy,count,upper,lower,wrist,target,{1,-.5f,0},posed.data(),solve),"IK solves the recovered GPU arm without any CPU animation pose");
        for(unsigned i=upper;i<count;++i)if(NativeRigMath::Descendant(i,upper,hierarchy)) {
            Check(NativeSkinMath::Palette(rest[i],posed[i],gpu[i].data()),"only solved arm descendants receive replacement matrices");
            Check(Near(Apply(gpu[i].data(),rest[i].position),posed[i].position),"replacement palette reaches each solved joint exactly");
        }
        Check(Near(posed[wrist].position,target.position),"GPU reconstructed arm reaches reachable controller target");
        for(unsigned i:{0u,1u,2u,7u})
            Check(!std::memcmp(gpu[i].data(),source[i].data(),sizeof(gpu[i])),"torso head legs and special slots preserve original palette bytes");
        Check(!std::memcmp(native.data(),source.data(),sizeof(native)),"source native palette remains completely read-only");
    }
    {
        const NativeRigMath::Bone before=recovered;
        float corrupt[12];std::memcpy(corrupt,palette,sizeof(corrupt));corrupt[3]=std::numeric_limits<float>::infinity();
        Check(!NativeSkinMath::PoseFromPalette(bind[1],corrupt,recovered)&&!std::memcmp(&before,&recovered,sizeof(before)),"nonfinite GPU input rejects without changing the output pose");
        Check(!NativeSkinMath::PoseFromPalette(bind[1],nullptr,recovered)&&!std::memcmp(&before,&recovered,sizeof(before)),"missing GPU palette rejects without changing the output pose");
        std::memcpy(corrupt,palette,sizeof(corrupt));corrupt[0]*=2;corrupt[4]*=2;corrupt[8]*=2;
        Check(!NativeSkinMath::PoseFromPalette(bind[1],corrupt,recovered)&&!std::memcmp(&before,&recovered,sizeof(before)),"nonrigid GPU matrix rejects without changing the output pose");
    }
    for(float units:{50.f,100.f,200.f})for(unsigned sample=0;sample<72;++sample) {
        const float angle=sample*.0872664626f;
        const CameraPose bindHead=Pose({0,0,0,1},{0,1.53f*units,-.13f*units});
        const Vec3 authoredOffset{-.105f*units,.095f*units,-.089f*units};
        CameraPose bindEye=bindHead;bindEye.position=Add(bindHead.position,authoredOffset);
        const auto animated=Pose({0,std::sin(angle*.5f),0,std::cos(angle*.5f)},
                                 {.02f*units,1.52f*units,-.12f*units});
        const NativeRigMath::Bone realHead{animated.basis,animated.position};
        Vec3 eye;
        Check(NativeSkinMath::AnatomicalEye(bindHead,bindEye,realHead,units,eye),"real HEAD plus bind attacher yields anatomical eye");
        const auto expected=Add(realHead.position,NativeRigMath::Transform(realHead.basis,authoredOffset));
        Check(Near(eye,expected),"authored eye offset follows actual head rotation and posture");
        const auto root=Pose({0,std::sin(angle*.5f),0,std::cos(angle*.5f)},{321,12,-456});
        Vec3 stable,shifted;
        Check(NativeSkinMath::StableEyeAnchor(root,eye,stable)&&
              NativeSkinMath::StableEyeAnchor(root,Add(eye,{.25f*units,0,-.4f*units}),shifted)&&Near(stable,shifted),
              "animated horizontal head movement and turn offsets never sway the first-person camera");
        Check(Near(stable,{root.position.x,root.position.y+eye.y,root.position.z}),
              "camera stays over the native root while preserving the current stance eye height");
        Check(NativeSkinMath::StableEyeAnchor(root,Add(eye,{0,-.3f*units,0}),shifted)&&
              Near(shifted,Add(stable,{0,-.3f*units,0})),"native crouching still lowers the stable eye anchor");
        const auto stableBefore=stable;
        Vec3 animatedAnchor;
        Check(NativeSkinMath::EyeAnchor(root,eye,true,animatedAnchor)&&
              Near(animatedAnchor,Add(root.position,NativeRigMath::Transform(root.basis,eye))),
              "native pickup and rappel retain the entire anatomical eye offset instead of moving the actor away from its equipment");
        Check(NativeSkinMath::EyeAnchor(root,eye,false,animatedAnchor)&&Near(animatedAnchor,stableBefore),
              "ordinary camera gait filtering remains unchanged with animation following disabled");
        Check(!NativeSkinMath::StableEyeAnchor(root,{0,std::numeric_limits<float>::quiet_NaN(),0},stable)&&
              Near(stable,stableBefore),"invalid stance input leaves the camera anchor unchanged");
        const auto sentinel=eye;auto unrelated=bindEye;unrelated.position.x+=units;
        Check(!NativeSkinMath::AnatomicalEye(bindHead,unrelated,realHead,units,eye)&&Near(eye,sentinel),
              "unrelated attacher beyond anatomical bound leaves output unchanged");
    }
    {
        constexpr unsigned count=8,head=2;
        const std::uint32_t hierarchy[count]={0,0,1,2,1,4,5,1};
        std::array<std::array<float,12>,count> source{};
        for(unsigned i=0;i<count;++i){source[i][0]=source[i][5]=source[i][10]=1.f;source[i][3]=float(i*7);source[i][7]=float(i*11);source[i][11]=float(i*13);}
        source[0][0]=std::numeric_limits<float>::quiet_NaN(); // unrelated special native slot
        auto left=source,right=source;const Vec3 pivot{3,155,-9};
        Check(NativeSkinMath::MaskHead(left,count,head,hierarchy,pivot)&&NativeSkinMath::MaskHead(right,count,head,hierarchy,pivot)&&
              !std::memcmp(left.data(),right.data(),sizeof(left)),"both eyes receive the same finite head-only palette mask");
        for(unsigned i:{0u,1u,4u,5u,6u,7u})
            Check(!std::memcmp(left[i].data(),source[i].data(),sizeof(left[i])),"head-only camera keeps native arms, torso, legs and special palette slots byte exact");
        for(unsigned i:{2u,3u}) {
            const auto& rows=left[i];const Vec3 transformedNormal{rows[1],rows[5],rows[9]};const auto length2=Dot(transformedNormal,transformedNormal);
            const auto normalized=Scale(transformedNormal,1.f/std::sqrt(length2));
            Check(length2>0&&Finite(normalized)&&Near(normalized,{0,1,0}),"hidden head retains a usable normal/tangent basis instead of normalizing a zero vector");
            Check(rows[0]*rows[5]*rows[10]>0&&rows[0]<.001f&&Near(Vec3{rows[3],rows[7],rows[11]},pivot),
                  "microscopic head mask keeps positive winding and collapses at the authored head pivot");
        }
        const auto before=left;
        Check(!NativeSkinMath::MaskHead(left,count+1,head,hierarchy,pivot)&&!std::memcmp(left.data(),before.data(),sizeof(left)),
              "out-of-capacity mask refuses to alter the palette");
        auto invalidParents=std::array<std::uint32_t,count>{0,0,1,2,5,4,5,1};
        Check(!NativeSkinMath::MaskHead(left,count,head,invalidParents.data(),pivot)&&!std::memcmp(left.data(),before.data(),sizeof(left)),
              "malformed hierarchy cannot partially mask unrelated geometry");
        const auto actor=NativeSkinMath::Policy(false,true,false),gameplay=NativeSkinMath::Policy(true,false,false),external=NativeSkinMath::Policy(false,false,false);
        const auto constrained=NativeSkinMath::Policy(true,false,true);
        Check(constrained.headMask&&constrained.armTracking&&!constrained.bodyFollow&&!constrained.rootAlignment,
              "rappel and native interaction retain the actor root shared by physics rope and attached equipment");
        Check(actor.headMask&&!actor.armTracking&&!actor.bodyFollow&&!actor.rootAlignment,
              "authorized actor cinematic hides only the nearby head and preserves native action arms and body");
        for(float units:{50.f,100.f,200.f}) {
            const Vec3 nativeHead{4.f*units,1.6f*units,-3.f*units};
            Check(NativeSkinMath::ActorHeadNear(nativeHead,Add(nativeHead,{.34f*units,0,0}),units)&&
                  !NativeSkinMath::ActorHeadNear(nativeHead,Add(nativeHead,{.36f*units,0,0}),units)&&
                  !NativeSkinMath::ActorHeadNear(nativeHead,Add(nativeHead,{0,0,1.5f*units}),units),
                  "authored local cinematic masks a nearby own head but retains it in an external over-shoulder shot");
        }
        Check(gameplay.headMask&&gameplay.armTracking&&gameplay.bodyFollow&&gameplay.rootAlignment&&!external.headMask,
              "ordinary first-person policy stays tracked while external cameras retain their head");
        Check(NativeSkinMath::DrawContextCurrent(true,17,17,true),
              "draw authorization remains valid for a slow frame until its applied camera generation is retired");
        Check(!NativeSkinMath::DrawContextCurrent(false,17,17,true)&&!NativeSkinMath::DrawContextCurrent(true,17,18,true)&&
              !NativeSkinMath::DrawContextCurrent(true,0,0,true)&&!NativeSkinMath::DrawContextCurrent(true,17,17,false),
              "frame retirement, replacement, missing identity and stopped host close render authorization immediately");
    }
    const std::uint32_t broken[3]={0,0,2};Check(!NativeSkinMath::BindPose(local,broken,3,bind),"cyclic bind parent rejected");
    local[2].rotation={0,0,0,0};Check(!NativeSkinMath::BindPose(local,parents,3,bind),"invalid native bind quaternion rejected");
    {
        const CameraPose root=Pose({0.f,.19866933f,0.f,.98006658f},{100,12,-90});
        const CameraPose restHead=Pose({0.f,.34289781f,0.f,.93937271f},{0,160,0});
        for(Vec3 axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})for(float angle:{-1.57079633f,0.f,1.57079633f,3.14159265f}) {
            const float s=std::sin(angle*.5f);const Quat q{axis.x*s,axis.y*s,axis.z*s,std::cos(angle*.5f)};
            const NativeRigMath::Bone animated{{Rotate(q,restHead.basis.forward),Rotate(q,restHead.basis.up),Rotate(q,restHead.basis.right)},{10,80,70}};
            Vec3 up;
            Check(NativeSkinMath::AnimatedHeadUp(root,restHead,animated,up)&&
                  Near(up,NativeRigMath::Transform(root.basis,Rotate(q,{0,1,0}))),
                  "rappel world-up follows actual animated head relative to costume bind axes, including horizontal and inverted poses");
            Vec3 forward;
            Check(NativeSkinMath::AnimatedHeadForward(root,restHead,animated,forward)&&
                  Near(forward,NativeRigMath::Transform(root.basis,Rotate(q,{0,0,-1})))&&std::fabs(Dot(forward,up))<.0001f,
                  "rappel facing comes from model-facing minus-Z in the same animated palette, not an ambiguous camera-heading projection");
        }
        Vec3 unchanged{7,8,9};auto bad=restHead;bad.basis.up={};
        Check(!NativeSkinMath::AnimatedHeadUp(root,bad,{restHead.basis,{}},unchanged)&&Near(unchanged,{7,8,9}),
              "invalid bind head cannot fabricate a rappel orientation or change output");
    }
    {
        std::array<CameraPose,8> bind{};std::array<NativeRigMath::Bone,8> pose{};
        const std::uint32_t parents[]={0,0,1,2,3,2,1,6};std::uint8_t changed[8]{};
        for(unsigned i=0;i<8;++i){bind[i].position={0,float(i)*20,0};pose[i]={bind[i].basis,bind[i].position};}
        pose[1].position.y-=30;pose[3].position.x=40;pose[4].position={50,120,30};pose[6].position.x=15;
        const auto leg=pose[6];const auto original=pose;
        Check(NativeSkinMath::StableUpperBody(bind.data(),original.data(),pose.data(),parents,8,1,4,changed),"tracked upper body has an independent visual stance");
        Check(Near(pose[4].position,{0,50,0})&&changed[4],"native fire overlay cannot move head or torso away from stable crouch-height stance");
        Check(Near(pose[6].position,leg.position)&&!changed[6],"leg locomotion remains authored under stable tracked upper body");
    }
    std::printf("Native GPU skin math: %u checks passed.\n",checks);return 0;
}
