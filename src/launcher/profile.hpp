#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <mutex>
// Both verified builds have identical code/data sections and native layout.
// The sole file difference is IMAGE_FILE_LARGE_ADDRESS_AWARE (COFF byte0x146):
// Steam retail clears it; the existing ReBuild image sets it. Do not modify
// the user's EXE. Unknown raw hashes need the complete normalized-file proof
// below: this is not a pattern scan or a selected-hook-only exception.
constexpr const char* kSupportedSha256="a8a2c14a4cb0f8898b8df91d9c4415d397a2bf21bed6ad6e126bad1dfe1e4380";
constexpr const char* kSupportedSteamSha256="3b70900fc19fc217d4a6ac37c8df7ee0d4de68da3ab175f938b0189759763937";
namespace Kl1ExecutableProfile {
inline std::mutex mutex;
inline char certified[8][65]{};
inline unsigned next=0;
inline bool Exact(const char* digest) {
    return digest&&(!std::strcmp(digest,kSupportedSha256)||!std::strcmp(digest,kSupportedSteamSha256));
}
}
inline bool IsSupportedExecutableHash(const char* digest) {
    if(Kl1ExecutableProfile::Exact(digest))return true;
    if(!digest)return false;
    std::lock_guard<std::mutex> lock(Kl1ExecutableProfile::mutex);
    for(const auto& known:Kl1ExecutableProfile::certified)if(known[0]&&!std::strcmp(digest,known))return true;
    return false;
}
inline bool HashFile(const wchar_t* path, char out[65]) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    unsigned char buf[65536], digest[32]; DWORD n=0;
    while(ok) {
        if(!ReadFile(f,buf,sizeof(buf),&n,nullptr)){ok=false;break;}
        if(!n)break;
        ok=BCryptHashData(hash,buf,n,0)>=0;
    }
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash); if(alg)BCryptCloseAlgorithmProvider(alg,0); CloseHandle(f);
    if(ok)for(int i=0;i<32;++i)std::sprintf(out+i*2,"%02x",digest[i]);
    return ok;
}
struct Kl1ExecutableIdentity {
    char sha256[65]{};
    bool supported=false,metadataVariant=false,largeAddressAware=false;
};
inline bool InspectExecutable(const wchar_t* path,Kl1ExecutableIdentity& out) {
    out={};
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    struct Close {HANDLE file;~Close(){CloseHandle(file);}} close{file};
    LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart!=7542024)return true;
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE raw=nullptr,normalized=nullptr;
    bool okay=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(okay)okay=BCryptCreateHash(alg,&raw,nullptr,0,nullptr,0,0)>=0;
    if(okay)okay=BCryptCreateHash(alg,&normalized,nullptr,0,nullptr,0,0)>=0;
    unsigned char buffer[65536];DWORD read=0;std::uint64_t offset=0;
    while(okay&&offset<static_cast<std::uint64_t>(size.QuadPart)){
        okay=ReadFile(file,buffer,sizeof(buffer),&read,nullptr)&&read!=0;
        if(!okay)break;
        okay=BCryptHashData(raw,buffer,read,0)>=0;
        if(!offset){
            // A fixed normalized whole-file digest below authenticates every
            // loader field, section, native instruction, global initializer,
            // import, resource and overlay byte. Only these non-code fields
            // vary: COFF timestamp, checksum, and the already audited LAA bit.
            if(read<0x190){okay=false;break;}
            WORD chars=0;std::memcpy(&chars,buffer+0x146,2);
            out.largeAddressAware=(chars&IMAGE_FILE_LARGE_ADDRESS_AWARE)!=0;
            std::memset(buffer+0x138,0,4);buffer[0x146]&=~IMAGE_FILE_LARGE_ADDRESS_AWARE;
            std::memset(buffer+0x188,0,4);
        }
        if(okay)okay=BCryptHashData(normalized,buffer,read,0)>=0;
        offset+=read;
    }
    unsigned char actual[32]{},stable[32]{};
    if(okay)okay=BCryptFinishHash(raw,actual,sizeof(actual),0)>=0&&BCryptFinishHash(normalized,stable,sizeof(stable),0)>=0;
    if(raw)BCryptDestroyHash(raw);if(normalized)BCryptDestroyHash(normalized);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(!okay)return false;
    char normalizedHash[65]{};
    for(unsigned i=0;i<32;++i){std::sprintf(out.sha256+i*2,"%02x",actual[i]);std::sprintf(normalizedHash+i*2,"%02x",stable[i]);}
    out.supported=!std::strcmp(normalizedHash,"82760829dca5e1d80170ba7c04ef7c3ed6d719a3ade921c6e5277583a8b9f1c8");
    out.metadataVariant=out.supported&&!Kl1ExecutableProfile::Exact(out.sha256);
    if(out.metadataVariant){std::lock_guard<std::mutex> lock(Kl1ExecutableProfile::mutex);
        std::memcpy(Kl1ExecutableProfile::certified[Kl1ExecutableProfile::next++%8],out.sha256,65);}
    return true;
}
inline bool HashSupportedGameFile(const wchar_t* path,char digest[65]) {
    Kl1ExecutableIdentity identity;const bool readable=InspectExecutable(path,identity);
    std::memcpy(digest,identity.sha256,65);return readable&&identity.supported;
}
