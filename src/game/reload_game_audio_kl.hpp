#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Read-only installed-game WHD/WAV cues. No exported game assets, decoder DLL,
// temporary WAVs or filesystem writes. Called only by worker initialization.
namespace ReloadGameAudioKL {
struct Cue {const wchar_t* name;const char* resource;bool arcade=false;};
inline constexpr Cue Cues[]={
 {L"release.wav","Usp/Usp_ClipOut.wav"},{L"take.wav","Usp/Usp_ClpiFromBelt_01.wav"},
 {L"insert.wav","Usp/Usp_ClipIn_01.wav"},{L"shell_insert.wav","ShotGun/ShotGun_Insert_Bullet_01.wav"},
 {L"back.wav","ShotGun/ShotGun_Cock_Back_01.wav"},
 {L"forward.wav","ShotGun/ShotGun_Cock_Release_01.wav"},{L"impact.wav","Sig_552/Sig_552_ClipInHit_01.wav"},
 {L"longgun_release.wav","Sig_552/Sig_552_ClipOut_01.wav"},{L"longgun_insert.wav","Sig_552/Sig_552_ClipIn_01.wav"},
 {L"smg_back.wav","ShotGun/ShotGun_Cock_Back_01.wav"},{L"rifle_back.wav","ShotGun/ShotGun_Cock_Back_01.wav"},
 {L"sniper_back.wav","ShotGun/ShotGun_Cock_Back_01.wav"},
 {L"empty.wav","SFX/Weapons/_Common/DryFire/DryFire_Small.wav"},
 {L"smg_empty.wav","SFX/Weapons/_Common/DryFire/DryFire_Small.wav"},
 {L"rifle_empty.wav","SFX/Weapons/_Common/DryFire/DryFire_Big.wav"}
};
struct Record {std::uint32_t rate=0,bytes=0,offset=0,samples=0;};
inline std::uint32_t U32(const unsigned char* p){std::uint32_t n=0;std::memcpy(&n,p,4);return n;}
inline bool Find(const std::vector<unsigned char>& header,const char* resource,std::uint64_t waveBytes,Record& output){
    const std::string name=std::strncmp(resource,"SFX/",4)==0?resource:std::string("SFX/Weapons/_Common/Reloads/")+resource;
    const auto length=name.size()+1;
    if(header.size()<length+44)return false;
    const auto* needle=reinterpret_cast<const unsigned char*>(name.c_str());
    auto begin=header.begin();
    while(begin!=header.end()){
        const auto match=std::search(begin,header.end(),needle,needle+length);if(match==header.end())return false;
        const auto pos=std::size_t(match-header.begin()),end=pos+length;
        for(auto at=end;at+44<=header.size()&&at<end+48;++at){
            if(at<4||U32(header.data()+at-4)!=pos)continue;
            const auto* p=header.data()+at;Record r{U32(p+4),U32(p+16),U32(p+24),U32(p+28)};
            if(U32(p)!=0x11||U32(p+8)!=4||U32(p+20)!=1||r.rate<8000||r.rate>96000||
               U32(p+32)!=1024||U32(p+36)!=2041||!r.bytes||r.bytes%1024||
               !r.samples||r.samples>r.rate*5||U32(p+12)!=r.samples*2||
               r.bytes>5*96000u*1024/2041+1024||r.samples>std::uint64_t(r.bytes/1024)*2041||
               r.samples<=std::uint64_t(r.bytes/1024-1)*2041||std::uint64_t(r.offset)+r.bytes>waveBytes)continue;
            output=r;return true;
        }
        begin=match+1;
    }return false;
}
// Standard mono IMA WAV blocks: signed predictor, index, reserved byte,
// then low/high 4-bit codes. Integer multiply rounding matches the game cues.
inline bool Decode(const std::vector<unsigned char>& input,unsigned samples,std::vector<std::int16_t>& output){
    constexpr int steps[]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    constexpr int indexDelta[]={-1,-1,-1,-1,2,4,6,8};
    if(input.empty()||input.size()%1024||!samples||samples>5*96000u||samples>input.size()/1024*2041)return false;
    std::vector<std::int16_t> decoded;decoded.reserve(samples);
    for(std::size_t block=0;block<input.size()&&decoded.size()<samples;block+=1024){
        std::int16_t first=0;std::memcpy(&first,input.data()+block,2);int predictor=first,index=input[block+2];
        if(index>88||input[block+3])return false;decoded.push_back(first);
        for(unsigned byte=4;byte<1024&&decoded.size()<samples;++byte)for(unsigned shift:{0u,4u}){
            if(decoded.size()==samples)break;const unsigned code=(input[block+byte]>>shift)&15;
            const int magnitude=((2*int(code&7)+1)*steps[index])>>3;
            predictor=std::clamp(predictor+((code&8)?-magnitude:magnitude),-32768,32767);
            index=std::clamp(index+indexDelta[code&7],0,88);decoded.push_back(std::int16_t(predictor));
        }
    }
    if(decoded.size()!=samples)return false;output.swap(decoded);return true;
}
inline bool Size(HANDLE file,std::uint64_t& bytes){LARGE_INTEGER n{};if(!GetFileSizeEx(file,&n)||n.QuadPart<=0)return false;bytes=std::uint64_t(n.QuadPart);return true;}
inline bool Read(HANDLE file,std::uint64_t offset,void* out,DWORD bytes){
    LARGE_INTEGER position{};position.QuadPart=LONGLONG(offset);DWORD got=0;
    return SetFilePointerEx(file,position,nullptr,FILE_BEGIN)&&ReadFile(file,out,bytes,&got,nullptr)&&got==bytes;
}
inline std::wstring GameFolder(){wchar_t file[32768]{};const auto n=GetModuleFileNameW(nullptr,file,32768);
    if(!n||n>=32768)return {};std::wstring path(file,n);const auto end=path.find_last_of(L"/\\");return end==std::wstring::npos?L"":path.substr(0,end+1);}
struct Archive {
    HANDLE wave=INVALID_HANDLE_VALUE;std::uint64_t waveBytes=0;std::vector<unsigned char> header;bool attempted=false;
    Archive()=default;Archive(const Archive&)=delete;Archive& operator=(const Archive&)=delete;
    ~Archive(){if(wave!=INVALID_HANDLE_VALUE)CloseHandle(wave);}
    bool Open(const std::wstring& base){
        if(attempted)return wave!=INVALID_HANDLE_VALUE;attempted=true;
        const auto open=[](const std::wstring& path){return CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);};
        const auto whd=open(base+L".WHD");if(whd==INVALID_HANDLE_VALUE)return false;std::uint64_t bytes=0;
        const bool bounded=Size(whd,bytes)&&bytes>=44&&bytes<=16*1024*1024;
        if(bounded)header.resize(std::size_t(bytes));const bool read=bounded&&Read(whd,0,header.data(),DWORD(header.size()));CloseHandle(whd);
        if(!read){header.clear();return false;}
        wave=open(base+L".WAV");if(wave==INVALID_HANDLE_VALUE)return false;
        if(!Size(wave,waveBytes)){CloseHandle(wave);wave=INVALID_HANDLE_VALUE;return false;}return true;
    }
};
struct Installed {
    std::wstring folder;Archive main,arcade;
    explicit Installed(std::wstring root=GameFolder()):folder(std::move(root)){}
    bool Load(const wchar_t* name,std::vector<std::int16_t>& output,unsigned& rate){
        if(folder.empty()||!name)return false;
        for(const auto& cue:Cues)if(!std::wcscmp(name,cue.name)){
            auto& archive=cue.arcade?arcade:main;
            if(!archive.Open(folder+L"Scenes/Locations/M09_A/M09A_Main"))return false;
            Record record;if(!Find(archive.header,cue.resource,archive.waveBytes,record))return false;
            std::vector<unsigned char> encoded(record.bytes);
            if(!Read(archive.wave,record.offset,encoded.data(),record.bytes)||!Decode(encoded,record.samples,output))return false;
            rate=record.rate;return true;
        }
        return false;
    }
};
}
