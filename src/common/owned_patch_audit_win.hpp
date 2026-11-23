// Current-process ownership certificate. No SDK calls or process writes.
#pragma once
#include "owned_patch_page_audit.hpp"
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <string>
#include "profile.hpp"

namespace KL1OwnedPatchAudit {
inline bool ReadSelf(std::uintptr_t address,void* output,std::size_t bytes){
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,bytes,&copied)&&copied==bytes;
}
inline bool Sha256(const std::vector<unsigned char>& bytes,std::array<unsigned char,32>& digest){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD size=0,returned=0;
    bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(okay)okay=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&returned,0)>=0;
    std::vector<unsigned char> object(size);
    if(okay)okay=BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0;
    if(okay)okay=bytes.size()<=std::numeric_limits<ULONG>::max()&&BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0;
    if(okay)okay=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return okay;
}

class VerifiedSteamImage {
    std::vector<unsigned char> text_;
    Bounds game_{};
    std::uint32_t textRva_=0;
public:
    bool LoadCurrentMain(){
        text_.clear();game_={};textRva_=0;
        const auto module=GetModuleHandleW(nullptr);if(!module)return false;
        std::array<wchar_t,32768> path{};
        const DWORD length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
        if(!length||length>=path.size())return false;
        Kl1ExecutableIdentity identity;
        if(!InspectExecutable(path.data(),identity)||!identity.supported)return false;
        HANDLE file=CreateFileW(path.data(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size{};bool okay=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=32*1024*1024;
        std::vector<unsigned char> bytes(okay?static_cast<std::size_t>(size.QuadPart):0);DWORD read=0;
        if(okay)okay=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size();
        CloseHandle(file);if(!okay)return false;
        std::array<unsigned char,32> actual{};if(!Sha256(bytes,actual))return false;
        char digest[65]{};for(unsigned i=0;i<32;++i)std::sprintf(digest+i*2,"%02x",actual[i]);
        if(std::strcmp(digest,identity.sha256))return false;
        if(bytes.size()<sizeof(IMAGE_DOS_HEADER))return false;
        IMAGE_DOS_HEADER dos{};std::memcpy(&dos,bytes.data(),sizeof(dos));
        if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||static_cast<std::size_t>(dos.e_lfanew)>bytes.size()-sizeof(IMAGE_NT_HEADERS32))return false;
        IMAGE_NT_HEADERS32 nt{};std::memcpy(&nt,bytes.data()+dos.e_lfanew,sizeof(nt));
        if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
           nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt.OptionalHeader.ImageBase!=0x400000||
           reinterpret_cast<std::uintptr_t>(module)!=nt.OptionalHeader.ImageBase)return false;
        IMAGE_NT_HEADERS32 liveNt{};
        if(!ReadSelf(reinterpret_cast<std::uintptr_t>(module)+dos.e_lfanew,&liveNt,sizeof(liveNt))||
           liveNt.Signature!=nt.Signature||liveNt.FileHeader.Machine!=nt.FileHeader.Machine||
           liveNt.OptionalHeader.SizeOfImage!=nt.OptionalHeader.SizeOfImage||liveNt.OptionalHeader.ImageBase!=nt.OptionalHeader.ImageBase)return false;
        const std::size_t sectionAt=static_cast<std::size_t>(dos.e_lfanew)+24+nt.FileHeader.SizeOfOptionalHeader;
        if(sectionAt>bytes.size()||nt.FileHeader.NumberOfSections>(bytes.size()-sectionAt)/sizeof(IMAGE_SECTION_HEADER))return false;
        unsigned found=0;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){
            IMAGE_SECTION_HEADER section{};std::memcpy(&section,bytes.data()+sectionAt+i*sizeof(section),sizeof(section));
            if(std::memcmp(section.Name,".text\0\0\0",8))continue;
            if(++found!=1||!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)||
               section.Misc.VirtualSize>section.SizeOfRawData||section.PointerToRawData>bytes.size()||
               section.Misc.VirtualSize>bytes.size()-section.PointerToRawData||
               section.VirtualAddress>nt.OptionalHeader.SizeOfImage||
               section.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-section.VirtualAddress)return false;
            text_.assign(bytes.data()+section.PointerToRawData,bytes.data()+section.PointerToRawData+section.Misc.VirtualSize);
            textRva_=section.VirtualAddress;
        }
        if(found!=1||text_.empty())return false;
        game_={reinterpret_cast<std::uintptr_t>(module),nt.OptionalHeader.SizeOfImage};return true;
    }
    const std::vector<unsigned char>& Text()const{return text_;}
    Bounds Game()const{return game_;}
    std::uint32_t TextRva()const{return textRva_;}
};

// Bind owner bounds to the actual module containing this adapter, rather than
// accepting a caller-supplied arbitrary address interval as production proof.
inline bool CurrentOwnerBounds(Bounds& result){
    result={};HMODULE owner=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
       reinterpret_cast<LPCWSTR>(&CurrentOwnerBounds),&owner))return false;
    const auto base=reinterpret_cast<std::uintptr_t>(owner);IMAGE_DOS_HEADER dos{};
    if(!ReadSelf(base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>1024*1024)return false;
    IMAGE_NT_HEADERS32 nt{};if(!ReadSelf(base+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||!nt.OptionalHeader.SizeOfImage)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<const void*>(base),&info,sizeof(info))!=sizeof(info)||info.AllocationBase!=owner||info.Type!=MEM_IMAGE)return false;
    result={base,nt.OptionalHeader.SizeOfImage};return true;
}

inline Result AuditCurrentVerifiedGame(std::vector<Page>& output,unsigned int& ledgerCount){
    output.clear();ledgerCount=0;VerifiedSteamImage image;
    Bounds owner{};if(!image.LoadCurrentMain()||!CurrentOwnerBounds(owner))return Result::InvalidImage;
    unsigned int count=0;
    const auto initial=KL1_MH_AuditEnabledPatches(nullptr,0,&count);
    if(initial!=KL1_MH_AUDIT_OK&&initial!=KL1_MH_AUDIT_CAPACITY)return Result::InvalidRecord;
    if(count>4096)return Result::InvalidRecord;
    std::vector<KL1_MH_OWNED_PATCH> records(count);unsigned int actual=0;
    if(KL1_MH_AuditEnabledPatches(records.data(),count,&actual)!=KL1_MH_AUDIT_OK||actual!=count)return Result::InvalidRecord;
    for(const auto& record:records){
        if(!Contains(image.Game(),record.patchAddress,1)&&!Contains(image.Game(),record.target,1))continue;
        MEMORY_BASIC_INFORMATION info{};
        if(!Contains(owner,record.detour,1)||VirtualQuery(reinterpret_cast<const void*>(record.detour),&info,sizeof(info))!=sizeof(info)||
           reinterpret_cast<std::uintptr_t>(info.AllocationBase)!=owner.base||info.Type!=MEM_IMAGE||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return Result::ForeignDetour;
        const auto access=info.Protect&0xff;
        if(access!=PAGE_EXECUTE&&access!=PAGE_EXECUTE_READ&&access!=PAGE_EXECUTE_READWRITE&&access!=PAGE_EXECUTE_WRITECOPY)return Result::ForeignDetour;
    }
    std::vector<unsigned char> live(image.Text().size());
    if(!ReadSelf(image.Game().base+image.TextRva(),live.data(),live.size()))return Result::InvalidRange;
    const auto result=CompareText(image.Text().data(),live.data(),live.size(),image.TextRva(),image.Game(),owner,records.data(),count,output);
    if(result==Result::Okay)ledgerCount=count;
    return result;
}
} // namespace KL1OwnedPatchAudit
