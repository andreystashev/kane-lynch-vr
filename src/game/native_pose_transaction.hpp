#pragma once
#include "native_camera_math.hpp"
#include <array>
#include <cstdint>

// A deferred render may consume these addresses after its camera builder
// returns. Preserve the first original pose until capture has finished all
// draws. Owners are revalidated before every write and restore.
namespace NativePoseTransaction {
template<class Owner,unsigned Capacity=16> class Pending {
    struct Item {std::uintptr_t address=0;Owner owner{};NativeCameraMath::CameraPose original{};};
    std::array<Item,Capacity> items_{};
    unsigned count_=0;
public:
    unsigned Count() const {return count_;}
    template<class Read,class Write,class Guard>
    bool Replace(std::uintptr_t address,const Owner& owner,const NativeCameraMath::CameraPose& pose,
                 Read read,Write write,Guard guard) {
        if(!address||!NativeCameraMath::ValidatePose(pose)||!guard(address,owner))return false;
        unsigned index=0;
        for(;index<count_;++index)if(items_[index].address==address)break;
        if(index<count_) {
            if(!(items_[index].owner==owner))return false;
        } else {
            if(count_==Capacity)return false;
            NativeCameraMath::CameraPose original;
            if(!read(address,original)||!NativeCameraMath::ValidatePose(original))return false;
            items_[count_++]={address,owner,original};
        }
        // Retain the original even if a guarded writer fails partway through;
        // Finish will still attempt recovery for this registered address.
        return write(address,pose);
    }
    template<class Write,class Guard> unsigned Finish(Write write,Guard guard) {
        unsigned restored=0;
        while(count_) {
            const auto item=items_[--count_];
            if(guard(item.address,item.owner)&&write(item.address,item.original))++restored;
        }
        return restored;
    }
};
}
