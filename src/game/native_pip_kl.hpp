#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Read-only ownership proof for the pinned native picture-in-picture camera.
// VerifyImage runs once after the supported EXE hash check. An active PIP is
// an auxiliary view; it never makes the entire main scene a flat movie panel.
namespace NativePipKL {
inline constexpr std::uintptr_t GameDataRva=0x68760C,FrontendVtableRva=0x5755FC;
inline constexpr std::uintptr_t ControllerVtableRva=0x563CA4,CameraVtableRva=0x545A3C;
struct State {
    bool valid=false,active=false;
    std::uintptr_t controller=0,geometry=0,camera=0;
    std::uint32_t mode=0;
    std::int32_t left=0,top=0,right=0,bottom=0;
};
inline bool Address(std::uintptr_t at,std::size_t size) {
    return at>=0x10000u&&size<=0xffffffffu&&at<=0xffffffffu-size;
}
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read) {
    if(!Address(image,GameDataRva+4))return false;
    const auto match=[&](std::uintptr_t rva,const auto& expected){
        unsigned char actual[32]{};
        return sizeof(expected)<=sizeof(actual)&&read(image+rva,actual,sizeof(expected))&&
            !std::memcmp(actual,expected,sizeof(expected));
    };
    const unsigned char accessor[]={0x8B,0x44,0x24,4,0x8B,0x89,0x4C,0x15,0,0,0x50,
        0xE8,0x30,0x5F,0,0,0x8B,0x80,0x14,9,0,0,0xC2,4,0};
    const unsigned char frontendIndex[]={0x8B,0x44,0x24,4,0x83,0xF8,1,0x77,9,
        0x8D,4,0x40,0x8B,4,0x81,0xC2,4,0};
    unsigned char constructor[]={0xC7,6,0,0,0,0};
    const auto vtable=static_cast<std::uint32_t>(image+ControllerVtableRva);
    std::memcpy(constructor+2,&vtable,4);
    const unsigned char activate[]={0xC6,0x86,0x44,5,0,0,1};
    const unsigned char deactivate[]={0xC6,0x86,0x44,5,0,0,0};
    std::uint32_t destructor=0,update=0;
    return match(0x2704B0,accessor)&&match(0x2763F0,frontendIndex)&&
        match(0x23641A,constructor)&&match(0x236CB8,activate)&&match(0x236A38,deactivate)&&
        read(image+ControllerVtableRva,&destructor,4)&&destructor==image+0x1DAAF0&&
        read(image+ControllerVtableRva+0xE0,&update,4)&&update==image+0x236D70;
}
template<class Reader> State Snapshot(std::uintptr_t image,Reader read,unsigned playerIndex=0) {
    State output;
    if(playerIndex>1||!Address(image,GameDataRva+4))return output;
    const auto get=[&](std::uintptr_t at,auto& value){return Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    struct Links {
        std::uint32_t data=0,manager=0,frontend=0,frontendVtable=0,controller=0,controllerVtable=0;
        std::uint32_t mode=0,camera=0,cameraVtable=0,geometry=0,backlink=0;
        std::uint8_t registered=0;
        std::int32_t rectangle[4]{};
    } first,second;
    const auto collect=[&](Links& links){
        if(!get(image+GameDataRva,links.data))return false;
        if(!links.data)return true;
        if(!Address(links.data,0x1550)||!get(links.data+0x154C,links.manager))return false;
        if(!links.manager)return true;
        if(!get(links.manager+playerIndex*12,links.frontend))return false;
        if(!links.frontend)return true;
        if(!Address(links.frontend,0x918)||!get(links.frontend,links.frontendVtable)||
           links.frontendVtable!=image+FrontendVtableRva||!get(links.frontend+0x914,links.controller))return false;
        if(!links.controller)return true;
        if(!Address(links.controller,0x545)||!get(links.controller,links.controllerVtable)||
           links.controllerVtable!=image+ControllerVtableRva||!get(links.controller+0x544,links.registered)||
           links.registered>1||!get(links.controller+0x4B8,links.mode)||links.mode>6)return false;
        if(!links.registered||!links.mode)return true;
        // Controller+24 is the ZCAMERA, not its geometry. Resolve the same
        // camera->geometry->camera backlink used by native camera packets.
        if(!get(links.controller+0x24,links.camera)||!Address(links.camera,8)||
           !get(links.camera,links.cameraVtable)||links.cameraVtable!=image+CameraVtableRva||
           !get(links.camera+4,links.geometry)||!Address(links.geometry,0x64)||
           !get(links.geometry+0x60,links.backlink)||links.backlink!=links.camera||
           !read(links.controller+0x4A8,links.rectangle,sizeof(links.rectangle)))return false;
        return true;
    };
    if(!collect(first)||!collect(second)||first.data!=second.data||first.manager!=second.manager||
       first.frontend!=second.frontend||first.frontendVtable!=second.frontendVtable||
       first.controller!=second.controller||first.controllerVtable!=second.controllerVtable||
       first.mode!=second.mode||first.registered!=second.registered||first.camera!=second.camera||
       first.cameraVtable!=second.cameraVtable||first.geometry!=second.geometry||first.backlink!=second.backlink||
       std::memcmp(first.rectangle,second.rectangle,sizeof(first.rectangle)))return output;
    output.valid=true;output.active=first.registered&&first.mode;
    output.controller=first.controller;output.geometry=first.geometry;output.camera=first.camera;output.mode=first.mode;
    output.left=first.rectangle[0];output.top=first.rectangle[1];output.right=first.rectangle[2];output.bottom=first.rectangle[3];
    return output;
}
}
