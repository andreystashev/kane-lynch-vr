#include "native_movie_kl.hpp"
#include <cstdio>
#include <map>
#include <cstring>

namespace {
constexpr std::uint32_t Image=0x400000,Object=0x10000000,Handle=0x11000000;
struct Memory {
    std::map<std::uintptr_t,unsigned char> data;
    template<class T>void Set(std::uintptr_t address,const T& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i)data[address+i]=p[i];}
    bool Read(std::uintptr_t address,void* output,std::size_t size)const{
        auto* p=static_cast<unsigned char*>(output);for(std::size_t i=0;i<size;++i){const auto found=data.find(address+i);if(found==data.end())return false;p[i]=found->second;}return true;
    }
    auto Reader()const{return [&](auto at,void* output,auto size){return Read(at,output,size);};}
    Memory(){
        using namespace NativeMovieKL;
        Set(Image+CurrentRva,Object);Set(Object,std::uint32_t(Image+VtableRva));Set(Object+ActiveOffset,std::uint8_t(1));Set(Object+HandleOffset,Handle);
        const std::uint32_t header[]={1920,1080,900,15};Set(Handle,header);
        const unsigned char start[]={0x83,0xEC,0x20,0x55,0x8B,0xE9},store[]={0x89,0x2D,0x64,0x56,0xC8,0};
        const unsigned char set[]={0xC6,0x85,0xB0,1,0,0,1},height[]={0x8B,0x57,4,0xDB,0x47,4},width[]={0x8B,7,0xDB,7};
        const unsigned char test[]={0x80,0xBF,0xB0,1,0,0,0},clear[]={0xC6,0x86,0xB0,1,0,0,0};
        Set(Image+StartRva,start);Set(Image+0x1CF34D,store);Set(Image+0x1CF3BA,set);Set(Image+0x1CF3C1,height);Set(Image+0x1CF3DD,width);
        Set(Image+0x1CF5A6,test);Set(Image+0x1CF7A8,clear);
        Set(Image+VtableRva,std::uint32_t(Image+CloseRva));Set(Image+VtableRva+0x18,std::uint32_t(Image+StartRva));Set(Image+VtableRva+0x24,std::uint32_t(Image+DrawRva));
    }
};
unsigned failures=0,checks=0;
void Check(bool okay,const char* message){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",message);}}
}
int main(){
    using namespace NativeMovieKL;
    Memory memory;
    Check(VerifyImage(Image,memory.Reader()),"pinned code and Bink vtable match");
    auto movie=Snapshot(Image,memory.Reader());const auto original=memory.data;
    Check(movie.valid&&movie.active&&movie.object==Object&&movie.handle==Handle&&movie.width==1920&&movie.height==1080&&movie.frames==900&&movie.frame==15,"exact active Bink header supplies authored dimensions");
    Check(memory.data==original,"snapshot performs no writes");
    memory.Set(Object+ActiveOffset,std::uint8_t(0));memory.data.erase(Handle);movie=Snapshot(Image,memory.Reader());
    Check(movie.valid&&!movie.active&&!movie.width&&!movie.handle,"inactive Bink does not dereference its stale handle");
    memory=Memory{};memory.Set(Image+CurrentRva,std::uint32_t(0));Check(Snapshot(Image,memory.Reader()).valid&&!Snapshot(Image,memory.Reader()).active,"null current object is inactive");
    memory=Memory{};memory.Set(Object,std::uint32_t(Image+VtableRva+4));Check(!Snapshot(Image,memory.Reader()).active,"other video classes do not prove Bink playback");
    memory=Memory{};memory.Set(Object+ActiveOffset,std::uint8_t(2));Check(!Snapshot(Image,memory.Reader()).valid,"unknown active flags fail closed");
    for(const auto value:{0u,16385u,0xffffffffu}){
        memory=Memory{};memory.Set(Handle,value);Check(!Snapshot(Image,memory.Reader()).valid,"invalid source width is rejected");
        memory=Memory{};memory.Set(Handle+4,value);Check(!Snapshot(Image,memory.Reader()).valid,"invalid source height is rejected");
    }
    memory=Memory{};memory.Set(Handle+12,std::uint32_t(902));Check(!Snapshot(Image,memory.Reader()).valid,"corrupt frame cursor is rejected");
    memory=Memory{};memory.Set(Handle+12,std::uint32_t(901));Check(Snapshot(Image,memory.Reader()).active,"native final-frame boundary remains a valid observation");
    memory=Memory{};memory.Set(Object+HandleOffset,std::uint32_t(0));Check(!Snapshot(Image,memory.Reader()).active,"active flag without handle is insufficient");
    memory=Memory{};unsigned globals=0;
    auto replaced=[&](std::uintptr_t at,void* output,std::size_t size){if(at==Image+CurrentRva&&++globals==2)memory.Set(at,std::uint32_t(0));return memory.Read(at,output,size);};
    Check(!Snapshot(Image,replaced).valid,"movie replacement during observation is rejected");
    memory=Memory{};unsigned handles=0;
    auto reopened=[&](std::uintptr_t at,void* output,std::size_t size){if(at==Object+HandleOffset&&++handles==2)memory.Set(at,std::uint32_t(Handle+4));return memory.Read(at,output,size);};
    Check(!Snapshot(Image,reopened).valid,"Bink handle replacement is rejected");
    memory=Memory{};memory.Set(Image+0x1CF5A6,std::uint8_t(0x90));Check(!VerifyImage(Image,memory.Reader()),"changed native draw signature disables movie proof");
    memory=Memory{};memory.Set(Image+VtableRva+0x24,std::uint32_t(Image+DrawRva+4));Check(!VerifyImage(Image,memory.Reader()),"changed vtable draw slot disables movie proof");
    Check(!Snapshot(0,memory.Reader()).valid&&!Snapshot(0xffff0000u,memory.Reader()).valid,"invalid image ranges are rejected");
    std::printf("Pinned native Bink movie observation: %u checks %s.\n",checks,failures?"FAILED":"passed");return failures?1:0;
}
