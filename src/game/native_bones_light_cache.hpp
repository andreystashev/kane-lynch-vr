#pragma once
#include "native_camera_math.hpp"
#include <array>
#include <cstdint>
#include <cstring>

// Native low/11 BonesLights semantic68 consumer490EF7. HQ semantic66 at
//490E30 always transforms/uploads and does not use this pointer cache.
namespace NativeBonesLightCache {
using namespace NativeCameraMath;
constexpr std::uintptr_t CacheRva=0x7887A8,ConsumerRva=0x90EF7;
enum class Result {Unchanged,Invalidated,Rejected};
inline bool Address(std::uintptr_t at,std::size_t n){return at>=0x10000u&&n<=0xFFFFFFFFu&&at<=0xFFFFFFFFu-n;}
template<class Reader>bool Verify(std::uintptr_t image,Reader read){
    if(!Address(image+CacheRva,4))return false;
    std::array<unsigned char,46> actual{},expected{{0x83,0xBD,0xC4,3,0,0,0,0x75,0x0A,0xC7,5,0,0,0,0,0,0,0,0,
        0x8B,0xB5,0xC0,3,0,0,0x39,0x35,0,0,0,0,0x0F,0x84,0x49,9,0,0,0x83,0xFE,0x20,0x89,0x35,0,0,0,0}};
    const auto cache=static_cast<std::uint32_t>(image+CacheRva);
    for(unsigned offset:{11u,27u,42u})std::memcpy(expected.data()+offset,&cache,4);
    return read(image+ConsumerRva,actual.data(),actual.size())&&actual==expected;
}
// Call ONLY after exact owned WORLD rebase and native SetWorld completion.
// This never edits light vectors/colors, palette, WORLD or native resources.
// Caller verifies the pinned image once during installation.
template<class Reader,class Writer>Result Invalidate(std::uintptr_t image,std::uintptr_t state,
        const CameraPose& before,const CameraPose& after,Reader read,Writer write,bool imageVerified){
    if(!imageVerified||!Address(image+CacheRva,4)||!Address(state,0x3C8)||
       !ValidatePose(before,.05f)||!ValidatePose(after,.05f))return Result::Rejected;
    if(!std::memcmp(&before,&after,sizeof(before)))return Result::Unchanged;
    CameraPose current;std::uint32_t index=0,lights=0,cached=0;
    if(!read(state+0x8C,&current,sizeof(current))||std::memcmp(&current,&after,sizeof(after))||
       !read(state+0x3C4,&index,4))return Result::Rejected;
    // Native index0 already invalidates this cache before every consumption.
    if(!index)return Result::Unchanged;
    if(index>=1024||!read(state+0x3C0,&lights,4)||!Address(lights,0x70)||
       !read(image+CacheRva,&cached,4))return Result::Rejected;
    if(cached!=lights)return Result::Unchanged;
    // Close both sources immediately before the single cache-word write.
    std::uint32_t againLights=0,againIndex=0,againCached=0;CameraPose againWorld;
    if(!read(state+0x8C,&againWorld,sizeof(againWorld))||std::memcmp(&againWorld,&after,sizeof(after))||
       !read(state+0x3C0,&againLights,4)||againLights!=lights||!read(state+0x3C4,&againIndex,4)||againIndex!=index||
       !read(image+CacheRva,&againCached,4)||againCached!=cached)return Result::Rejected;
    const std::uint32_t zero=0;
    if(!write(image+CacheRva,&zero,4)||!read(image+CacheRva,&againCached,4)||againCached)return Result::Rejected;
    return Result::Invalidated;
}
}
