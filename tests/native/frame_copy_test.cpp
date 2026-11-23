#include "frame_copy.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
unsigned checks=0;
void Check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void Fill(std::uint8_t* source,std::size_t count){for(std::size_t i=0;i<count;++i)source[i]=static_cast<std::uint8_t>((i*73u)^(i>>11)^37u);}
bool Pixels(const std::uint8_t* out,const std::uint8_t* in,unsigned width,unsigned height,std::size_t stride,bool opaque) {
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<4;++c)
        if(out[(std::size_t(y)*width+x)*4+c]!=(opaque&&c==3?255:in[std::size_t(y)*stride+x*4+c]))return false;
    return true;
}
void SmallLayouts() {
    for(unsigned width:{1u,2u,3u,4u,15u,16u,17u,31u,32u,33u,63u,64u,65u})
        for(unsigned height:{1u,2u,7u})for(unsigned pad:{0u,3u,17u})
            for(unsigned sourceOffset:{0u,1u,3u,15u})for(unsigned destinationOffset:{0u,1u,4u,8u,12u,15u})
                for(bool opaque:{false,true}) {
                    const std::size_t row=width*4,stride=row+pad,bytes=row*height;
                    std::vector<std::uint8_t> source(stride*height+sourceOffset+32),out(bytes+destinationOffset+32,0xc9);
                    Fill(source.data(),source.size());const auto before=source;
                    Check(FrameCopy::BGRA(out.data()+destinationOffset,source.data()+sourceOffset,width,height,stride,opaque)&&
                          Pixels(out.data()+destinationOffset,source.data()+sourceOffset,width,height,stride,opaque),
                          "unaligned rows, source padding, alpha preservation and every vector/tail boundary are bit exact");
                    Check(source==before&&std::all_of(out.begin(),out.begin()+destinationOffset,[](auto value){return value==0xc9;})&&
                          std::all_of(out.begin()+destinationOffset+bytes,out.end(),[](auto value){return value==0xc9;}),
                          "source and destination boundary bytes remain unchanged");
                }
}
void LargeLayouts() {
    constexpr unsigned width=4097,height=1025;const std::size_t row=width*4,stride=row+13,bytes=row*height;
    std::vector<std::uint8_t> source(stride*height+64);Fill(source.data(),source.size());const auto before=source;
    for(unsigned offset=0;offset<16;++offset) {
        std::vector<std::uint8_t> out(bytes+64,0xc9);
        Check(FrameCopy::BGRA(out.data()+offset,source.data()+3,width,height,stride,true)&&
              Pixels(out.data()+offset,source.data()+3,width,height,stride,true),
              "large NT-eligible and fallback alignments preserve all native pixels and opaque alpha");
        Check(source==before&&std::all_of(out.begin(),out.begin()+offset,[](auto value){return value==0xc9;})&&
              std::all_of(out.begin()+offset+bytes,out.end(),[](auto value){return value==0xc9;}),
              "large streaming prefixes and row tails never write outside tightly packed output");
    }
    std::vector<std::uint8_t> out(bytes);
    Check(FrameCopy::BGRA(out.data(),source.data()+3,width,height,stride,false)&&
          Pixels(out.data(),source.data()+3,width,height,stride,false),"large no-alpha publication keeps source alpha exactly");
    // Existing exact in-place behavior must not acquire unsafe NT aliasing.
    std::vector<std::uint8_t> inPlace(bytes);Fill(inPlace.data(),inPlace.size());const auto original=inPlace;
    Check(FrameCopy::BGRA(inPlace.data(),inPlace.data(),width,height,row,true)&&
          Pixels(inPlace.data(),original.data(),width,height,row,true),"overlapping image falls back and retains existing exact in-place semantics");
}
#if defined(_WIN32)
struct Guarded {
    std::uint8_t* allocation=nullptr,*data=nullptr;
    std::size_t rounded=0;
    explicit Guarded(std::size_t bytes) {
        constexpr std::size_t page=4096;rounded=(bytes+page-1)&~(page-1);
        allocation=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,rounded+2*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        Check(allocation!=nullptr,"allocate isolated guard-page image");DWORD old=0;
        Check(VirtualProtect(allocation,page,PAGE_NOACCESS,&old)&&VirtualProtect(allocation+page+rounded,page,PAGE_NOACCESS,&old),
              "bound image with unreadable guard pages");data=allocation+page+rounded-bytes;
    }
    ~Guarded(){if(allocation)VirtualFree(allocation,0,MEM_RELEASE);}
    void ReadOnly(){DWORD old=0;Check(VirtualProtect(allocation+4096,rounded,PAGE_READONLY,&old)!=FALSE,"borrowed source is physically read-only");}
};
void GuardPages() {
    for(const unsigned width:{4096u,4097u,4103u}) {
        constexpr unsigned height=1025;const std::size_t row=width*4,stride=row+13,sourceSpan=(height-1)*stride+row;
        Guarded source(sourceSpan),destination(row*height);Fill(source.data,sourceSpan);source.ReadOnly();
        Check(FrameCopy::BGRA(destination.data,source.data,width,height,stride,true)&&
              Pixels(destination.data,source.data,width,height,stride,true),
              "streaming final source row and destination tail touch neither guard page and never mutate source");
    }
}
#endif
}
int main() {
    std::uint8_t byte=91;
    Check(!FrameCopy::BGRA(nullptr,&byte,1,1,4,true)&&!FrameCopy::BGRA(&byte,nullptr,1,1,4,true)&&
          !FrameCopy::BGRA(&byte,&byte,0,1,4,true)&&!FrameCopy::BGRA(&byte,&byte,1,0,4,true)&&
          !FrameCopy::BGRA(&byte,&byte,1,1,3,true)&&byte==91,"invalid input is rejected before touching borrowed memory");
    Check(!FrameCopy::ValidLayout(1,2,std::numeric_limits<std::size_t>::max()),"overflowing full image stride is rejected");
    SmallLayouts();LargeLayouts();
#if defined(_WIN32)
    GuardPages();
#endif
    std::printf("Frame copy exact pixels: %u checks PASS (unaligned/stride/tail/alpha/source guards).\n",checks);
}
