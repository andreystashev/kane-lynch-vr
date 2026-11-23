#define main ScopeProofBaseline
#include "native_scope_kl_test.cpp"
#undef main
#include "native_scope_overlay_kl.hpp"
int main(int argc,char** argv) {
    if(ScopeProofBaseline(argc,argv))return 1;
    if(argc<2)return 2;
    FileImage image;Check(image.Load(argv[1]),"overlay proof loads actual pinned PE32");
    const auto read=[&](auto at,void* output,auto size){return image.Read(at,output,size);};
    Check(NativeScopeOverlayKL::Verify(0x400000,read),"actual native activation/clear/variant/owner constructors prove the authored overlay lifecycle");
    for(const auto rva:{0x22BFD0u,0x22C0C0u,0x1E78FEu,0x230A34u,0x236156u,0x236168u,0x230310u,0x22B018u,0x22E772u,0x235C40u}) {
        const auto corrupt=[&](auto at,void* output,auto size){if(!read(at,output,size))return false;
            if(at==0x400000+rva)static_cast<unsigned char*>(output)[0]^=1;return true;};
        Check(!NativeScopeOverlayKL::Verify(0x400000,corrupt),"changed native overlay evidence disables original-camera activation");
    }
    std::printf("KL1 original camera optic proof: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
