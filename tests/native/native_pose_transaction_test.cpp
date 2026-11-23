#include "native_pose_transaction.hpp"
#include <cstdio>
#include <map>
using namespace NativeCameraMath;
int main() {
    NativePoseTransaction::Pending<unsigned,2> pending;
    struct Object {unsigned owner;CameraPose pose;};
    std::map<std::uintptr_t,Object> memory;
    memory[0x10000]={7,{{},{10,20,30}}};
    memory[0x20000]={7,{{},{40,50,60}}};
    memory[0x30000]={7,{{},{70,80,90}}};
    unsigned checks=0,failed=0,writes=0;
    const auto check=[&](bool ok,const char* message){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",message);}};
    const auto read=[&](std::uintptr_t at,CameraPose& pose){auto i=memory.find(at);if(i==memory.end())return false;pose=i->second.pose;return true;};
    const auto write=[&](std::uintptr_t at,const CameraPose& pose){auto i=memory.find(at);if(i==memory.end())return false;++writes;i->second.pose=pose;return true;};
    const auto guard=[&](std::uintptr_t at,unsigned owner){auto i=memory.find(at);return i!=memory.end()&&i->second.owner==owner;};
    CameraPose left{{},{100,170,200}},right{{},{106.4f,170,200}};
    check(pending.Replace(0x10000,7,left,read,write,guard),"eye installed for deferred draw");
    check(memory[0x10000].pose.position.y==170&&pending.Count()==1,"camera builder return retains pose for later GPU work");
    check(pending.Replace(0x10000,7,right,read,write,guard)&&pending.Count()==1,"repeated render of same camera retains first original");
    check(pending.Replace(0x20000,7,left,read,write,guard),"source geometry and cache held together");
    check(!pending.Replace(0x30000,7,left,read,write,guard)&&memory[0x30000].pose.position.x==70,"capacity failure never writes unregistered geometry");
    check(pending.Finish(write,guard)==2&&memory[0x10000].pose.position.x==10&&memory[0x20000].pose.position.x==40,"capture completion restores both originals");
    const auto after=writes;
    check(pending.Finish(write,guard)==0&&writes==after,"fallback frame cleanup restores exactly once");
    check(pending.Replace(0x10000,7,left,read,write,guard),"new frame can hold camera again");
    memory[0x10000].owner=8;memory[0x10000].pose=right;
    check(pending.Finish(write,guard)==0&&memory[0x10000].pose.position.x==106.4f,"reused streamed object ownership prevents stale restore");
    check(!pending.Replace(0x10000,7,left,read,write,guard),"unowned geometry cannot be modified");
    memory[0x10000].owner=7;CameraPose invalid=left;invalid.basis.up={};
    check(!pending.Replace(0x10000,7,invalid,read,write,guard)&&pending.Count()==0,"corrupt replacement is rejected before any write");
    std::printf("Native deferred pose transaction: %u checks, %u failures.\n",checks,failed);
    return failed?1:0;
}
