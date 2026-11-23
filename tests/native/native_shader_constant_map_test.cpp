#include "native_shader_constant_map.hpp"
#include "native_skin_lighting_shader.hpp"
#include <cstdio>
#include <vector>
int main(){
    unsigned checks=0,failed=0;const auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}};
    for(const auto& shader:NativeSkinLightingShader::Entries) {
        NativeShaderConstantMap::Map original,patched;
        check(NativeShaderConstantMap::Parse(shader.original,shader.originalBytes,original),"native original CTAB is bounded/readable");
        check(NativeShaderConstantMap::Parse(shader.patched,shader.patchedBytes,patched),"patched CTAB is bounded/readable");
        const auto same=[](NativeShaderConstantMap::Range a,NativeShaderConstantMap::Range b){return a.start==b.start&&a.count==b.count;};
        check(same(original.mvp,patched.mvp)&&same(original.eye,patched.eye)&&same(original.lights,patched.lights)&&same(original.lights11,patched.lights11)&&original.mvpColumns==patched.mvpColumns,"native MVP/eye/light register ABI stays unchanged by TBN-only variant");
        check(original.mvpColumns&&patched.mvpColumns,"replay matrix is proven float 4x4 MATRIX_COLUMNS in both actual shaders");
        check(original.mvp.count==4&&original.eye.count==1,"native complete projection and object eye have exact widths");
        check(original.mvp.start+original.mvp.count<=256,"register map remains inside native VS constant bank");
        std::vector<std::uint32_t> corrupt(shader.original,shader.original+shader.originalBytes/4);
        corrupt[1]=0x7ffffffeu;check(!NativeShaderConstantMap::Parse(corrupt.data(),corrupt.size()*4,patched),"oversized/truncated comment cannot escape native code span");
        corrupt.assign(shader.original,shader.original+shader.originalBytes/4);
        // A wraparound MVP type offset or non-4x4 shape must never qualify
        // a copied draw; constant order varies across the native shaders.
        const auto base=std::size_t(3*4);std::uint32_t constants=0,type=0,count=0;
        std::memcpy(&constants,reinterpret_cast<unsigned char*>(corrupt.data())+base+16,4);
        std::memcpy(&count,reinterpret_cast<unsigned char*>(corrupt.data())+base+12,4);
        std::size_t mvp=0;for(unsigned i=0;i<count;++i){std::uint32_t name=0;const auto at=base+constants+i*20;
            std::memcpy(&name,reinterpret_cast<unsigned char*>(corrupt.data())+at,4);
            if(!std::strcmp(reinterpret_cast<const char*>(corrupt.data())+base+name,"m44ModelViewProj")){mvp=at;break;}}
        check(mvp!=0,"actual shader has a named MVP constant to corrupt");
        std::memcpy(&type,reinterpret_cast<unsigned char*>(corrupt.data())+mvp+12,4);
        const std::uint32_t overflow=0xfffffffeu;std::memcpy(reinterpret_cast<unsigned char*>(corrupt.data())+mvp+12,&overflow,4);
        check(NativeShaderConstantMap::Parse(corrupt.data(),corrupt.size()*4,patched)&&!patched.mvpColumns,"overflowed CTAB type stays inside the comment and cannot qualify a matrix");
        corrupt.assign(shader.original,shader.original+shader.originalBytes/4);const std::uint16_t rows=3;
        std::memcpy(reinterpret_cast<unsigned char*>(corrupt.data())+base+type+4,&rows,2);
        check(NativeShaderConstantMap::Parse(corrupt.data(),corrupt.size()*4,patched)&&!patched.mvpColumns,"non-4x4 CTAB matrix cannot qualify native dropped mesh replay");
    }
    NativeShaderConstantMap::Map out;const std::uint32_t unrelated[]={0xfffe0200,0x0000ffff};
    check(!NativeShaderConstantMap::Parse(unrelated,sizeof(unrelated),out),"unrecognized shader without CTAB has no guessed register layout");
    check(!NativeShaderConstantMap::Parse(nullptr,0,out),"null shader input rejected");
    std::printf("Native shader CTAB contract: %u checks, %u failures; CPU only.\n",checks,failed);return failed?1:0;
}
