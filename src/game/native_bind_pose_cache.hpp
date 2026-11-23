#pragma once
#include "native_skin_math.hpp"
#include <cstring>
// Pure arithmetic memo: every source byte and parent is compared on a hit.
// This caches no ownership, camera, native palette, draw lease or animation.
namespace NativeBindPoseCache {
class Cache {
    struct Entry {
        bool valid=false;
        unsigned count=0;
        std::array<NativeSkinMath::BindLocal,NativeRigMath::MaximumBones> local{};
        std::array<std::uint32_t,NativeRigMath::MaximumBones> parents{};
        std::array<NativeCameraMath::CameraPose,NativeRigMath::MaximumBones> bind{};
    } entries_[2]; // head and body binders alternate within one native frame
    unsigned replace_=0;
public:
    std::uint64_t hits=0,misses=0;
    bool Resolve(const NativeSkinMath::BindLocal* local,const std::uint32_t* parents,unsigned count,NativeCameraMath::CameraPose* output){
        if(!local||!parents||!output||!count||count>NativeRigMath::MaximumBones)return false;
        for(const auto& e:entries_)if(e.valid&&e.count==count&&!std::memcmp(e.local.data(),local,count*sizeof(*local))&&!std::memcmp(e.parents.data(),parents,count*sizeof(*parents))){
            ++hits;std::memcpy(output,e.bind.data(),count*sizeof(*output));return true;
        }
        ++misses;std::array<NativeCameraMath::CameraPose,NativeRigMath::MaximumBones> next{};
        if(!NativeSkinMath::BindPose(local,parents,count,next.data()))return false;
        auto& e=entries_[replace_];replace_=(replace_+1)%2;
        std::memcpy(e.local.data(),local,count*sizeof(*local));std::memcpy(e.parents.data(),parents,count*sizeof(*parents));
        e.bind=next;e.count=count;e.valid=true;std::memcpy(output,e.bind.data(),count*sizeof(*output));return true;
    }
};
}
