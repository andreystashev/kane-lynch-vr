#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_body_kl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace NativeCameraMath;
unsigned checks=0;
void Check(bool okay,const char* message){++checks;if(!okay){std::printf("FAIL: %s\n",message);std::exit(1);}}
DWORD WINAPI OtherThread(void*){
    CameraPose root,eye,out;eye.position={0,160,0};
    return NativeBodyKL::Adjust(0x11000000,root,{0,160,0},{1,0,0},{1,0,0},out,7)?1:0;
}
int main(){
    CameraPose root,eye,first,late;eye.position={0,160,0};
    NativeBodyKL::SetFrame(eye,7,100,true,1);
    Check(NativeBodyKL::Adjust(0x11000000,root,{0,160,0},{1,0,0},{1,0,0},first,7),"fresh current pair has visual body adjustment");
    Sleep(130);
    Check(NativeBodyKL::Adjust(0x11000000,root,{0,160,0},{1,0,0},{1,0,0},late,7),"production body adjustment survives a current GPU generation delayed beyond100ms");
    Check(!std::memcmp(&first,&late,sizeof(first)),"same current pair retains identical body root across the delay");
    Check(!NativeBodyKL::Adjust(0x11000000,root,{0,160,0},{1,0,0},{1,0,0},late,8),"another pair cannot reuse body authorization");
    HANDLE thread=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);Check(thread!=nullptr,"independent render worker created");
    WaitForSingleObject(thread,INFINITE);DWORD code=~0u;GetExitCodeThread(thread,&code);CloseHandle(thread);
    Check(code==0,"another worker has no inherited body context");
    NativeBodyKL::ClearFrame();
    Check(!NativeBodyKL::Adjust(0x11000000,root,{0,160,0},{1,0,0},{1,0,0},late,7),"explicit camera retirement closes body authorization immediately");
    std::printf("Production body lifetime: %u checks PASS.\n",checks);
}
