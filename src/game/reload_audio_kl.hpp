#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include "reload_game_audio_kl.hpp"
#include "audio_endpoint_ipc.hpp"

// Installed KL1 action cues, with a mechanical fallback if extraction is absent.
// File I/O and buffer creation happen once on the worker, never during a draw
// or ammunition transaction. Play only starts an already prepared voice.
namespace ReloadAudioKL {
enum class Effect : unsigned {Release,Take,Insert,Back,Forward,Impact,Empty,Recover,Count};
enum class LongVoice : unsigned {Release,Insert,SMGBack,RifleBack,SniperBack,SMGEmpty,RifleEmpty,ShellInsert,Count};
enum class Profile : unsigned {Pistol,SMG,Rifle,Sniper};
inline Profile ForCategory(unsigned category){
    switch(category){case 2:return Profile::SMG;case 3:case 4:case 6:case 16:case 17:return Profile::Rifle;case 15:return Profile::Sniper;default:return Profile::Pistol;}
}
constexpr const wchar_t* LongNames[]={L"longgun_release.wav",L"longgun_insert.wav",L"smg_back.wav",L"rifle_back.wav",L"sniper_back.wav",L"smg_empty.wav",L"rifle_empty.wav",L"shell_insert.wav"};
constexpr unsigned Rate=22050,Count=Rate*12/100;
inline IDirectSound* device=nullptr;inline IDirectSoundBuffer* voices[unsigned(Effect::Count)]{};
inline IDirectSoundBuffer* longVoices[unsigned(LongVoice::Count)]{};
inline unsigned nativeVoices=0;
inline bool Endpoint(GUID& out){
 const auto handle=OpenFileMappingW(FILE_MAP_READ,FALSE,AudioEndpointIPC::Name(GetCurrentProcessId()).c_str());if(!handle)return false;
 const auto* shared=static_cast<const AudioEndpointIPC::State*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,sizeof(AudioEndpointIPC::State)));bool okay=false;
 if(shared){const auto seq=shared->sequence;MemoryBarrier();const auto value=*shared;MemoryBarrier();
  if(seq&&!(seq&1)&&seq==shared->sequence&&value.magic==AudioEndpointIPC::Magic&&value.version==AudioEndpointIPC::Version&&value.bytes==sizeof(value)&&value.pid==GetCurrentProcessId()&&value.creation==DiagnosticsSession::Creation(GetCurrentProcess())&&value.installed&&value.endpoint[39]==0){
   unsigned a=0,b=0,c=0,d[8]{};if(std::sscanf(value.endpoint,"{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}",&a,&b,&c,&d[0],&d[1],&d[2],&d[3],&d[4],&d[5],&d[6],&d[7])==11){out.Data1=a;out.Data2=WORD(b);out.Data3=WORD(c);for(unsigned i=0;i<8;++i)out.Data4[i]=BYTE(d[i]);okay=true;}
  }UnmapViewOfFile(shared);
 }CloseHandle(handle);return okay;
}
inline std::wstring Folder(){
    HMODULE module=nullptr;wchar_t path[MAX_PATH]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&voices),&module)||!GetModuleFileNameW(module,path,MAX_PATH))return {};
    std::wstring folder(path);const auto end=folder.find_last_of(L"/\\");return end==std::wstring::npos?L"":folder.substr(0,end+1)+L"reload-audio\\";
}
inline bool Load(const std::wstring& path,std::vector<std::int16_t>& output){
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    const DWORD size=GetFileSize(file,nullptr);std::vector<unsigned char> bytes;DWORD got=0;
    if(size>=44&&size<=Rate*2*5)bytes.resize(size);
    const bool read=!bytes.empty()&&ReadFile(file,bytes.data(),size,&got,nullptr)&&got==size;CloseHandle(file);
    if(!read||std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4))return false;
    const auto u16=[&](unsigned at){std::uint16_t v=0;std::memcpy(&v,bytes.data()+at,2);return v;};
    const auto u32=[&](unsigned at){std::uint32_t v=0;std::memcpy(&v,bytes.data()+at,4);return v;};
    if(u32(4)!=size-8)return false;bool format=false;unsigned data=0,length=0;
    for(unsigned at=12;at+8<=size;){const auto n=u32(at+4);if(n>size-at-8)return false;
        if(!std::memcmp(bytes.data()+at,"fmt ",4)){if(n<16)return false;
            format=u16(at+8)==1&&u16(at+10)==1&&u32(at+12)==Rate&&u32(at+16)==Rate*2&&u16(at+20)==2&&u16(at+22)==16;
        }else if(!std::memcmp(bytes.data()+at,"data",4)){data=at+8;length=n;}
        at+=8+n+(n&1);
    }
    if(!format||!data||length<2||(length&1))return false;
    output.resize(length/2);std::memcpy(output.data(),bytes.data()+data,length);return true;
}
inline void Generate(std::int16_t* samples,Effect effect){
    std::uint32_t noise=0x4AB5u+unsigned(effect)*7919;float low=0,prior=0;
    for(unsigned i=0;i<Count;++i){const float t=float(i)/Rate;noise=noise*1664525u+1013904223u;
        const float n=float((noise>>16)&65535)/32767.5f-1;low+=.13f*(n-low);const float dry=n-low,body=low-prior;prior=low;float value=0;
        if(effect==Effect::Back||effect==Effect::Forward){const bool forward=effect==Effect::Forward;
            const float drag=std::exp(-t*(forward?65.f:28.f)),stop=t>(forward?.016f:.055f)?std::exp(-(t-(forward?.016f:.055f))*230):0;
            value=dry*(drag*.14f*(std::sin(t*1193.8f)>0?.75f:.35f)+stop*.55f)+body*(drag*1.6f+stop*2.5f);
        }else if(effect==Effect::Release||effect==Effect::Take||effect==Effect::Empty)value=(dry*.48f+body*2)*std::exp(-t*170);
        else {const float bounce=t>.028f?std::exp(-(t-.028f)*130):0;
            value=(low*2.8f+dry*.24f)*(std::exp(-t*75)+bounce*.4f);}
        samples[i]=std::int16_t(std::clamp(value,-1.f,1.f)*26000);
    }
}
inline bool Initialize(const std::wstring& folder=Folder()){
    if(device)return true;IDirectSound* next=nullptr;static HMODULE library=LoadLibraryW(L"dsound.dll");
    using Create=HRESULT(WINAPI*)(LPCGUID,LPDIRECTSOUND*,LPUNKNOWN);const auto create=library?reinterpret_cast<Create>(GetProcAddress(library,"DirectSoundCreate")):nullptr;
    GUID endpoint{};const bool routed=Endpoint(endpoint);if(!create)return false;
    auto result=create(routed?&endpoint:nullptr,&next,nullptr);if(FAILED(result)&&routed)result=create(nullptr,&next,nullptr);if(FAILED(result))return false;
    if(FAILED(next->SetCooperativeLevel(GetDesktopWindow(),DSSCL_NORMAL))){next->Release();return false;}
    constexpr const wchar_t* names[]={L"release.wav",L"take.wav",L"insert.wav",L"back.wav",L"forward.wav",L"impact.wav",L"empty.wav",L"recover.wav"};
    ReloadGameAudioKL::Installed installed;
    const auto prepare=[&](const wchar_t* name,Effect fallback,IDirectSoundBuffer*& output){std::vector<std::int16_t> samples;unsigned rate=Rate;
        const bool native=(!folder.empty()&&Load(folder+name,samples))||installed.Load(name,samples,rate);
        if(!native){samples.resize(Count);Generate(samples.data(),fallback);}
        WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=1;f.nSamplesPerSec=rate;f.wBitsPerSample=16;f.nBlockAlign=2;f.nAvgBytesPerSec=rate*2;
        DSBUFFERDESC d{};d.dwSize=sizeof(d);d.dwBufferBytes=DWORD(samples.size()*2);d.dwFlags=DSBCAPS_CTRLVOLUME|DSBCAPS_GLOBALFOCUS;d.lpwfxFormat=&f;
        IDirectSoundBuffer* voice=nullptr;if(FAILED(next->CreateSoundBuffer(&d,&voice,nullptr)))return;
        void* a=nullptr;void* b=nullptr;DWORD na=0,nb=0;
        if(SUCCEEDED(voice->Lock(0,d.dwBufferBytes,&a,&na,&b,&nb,0))){std::memcpy(a,samples.data(),na);if(nb)std::memcpy(b,reinterpret_cast<const char*>(samples.data())+na,nb);
            voice->Unlock(a,na,b,nb);voice->SetVolume(-700);output=voice;if(native)++nativeVoices;}else voice->Release();
    };
    for(unsigned i=0;i<unsigned(Effect::Count);++i)prepare(names[i],Effect(i),voices[i]);
    constexpr Effect fallbacks[]={Effect::Release,Effect::Insert,Effect::Back,Effect::Back,Effect::Back,Effect::Empty,Effect::Empty,Effect::Insert};
    for(unsigned i=0;i<unsigned(LongVoice::Count);++i)prepare(LongNames[i],fallbacks[i],longVoices[i]);
    device=next;return true;
}
inline IDirectSoundBuffer* Voice(Effect effect,unsigned category=0){
    if(unsigned(effect)>=unsigned(Effect::Count))return nullptr;
    if(category==5&&effect==Effect::Insert)return longVoices[unsigned(LongVoice::ShellInsert)];
    if(category==5&&effect==Effect::Empty)return longVoices[unsigned(LongVoice::RifleEmpty)];
    const auto profile=ForCategory(category);
    if(profile!=Profile::Pistol){
        switch(effect){
            case Effect::Release:return longVoices[unsigned(LongVoice::Release)];
            case Effect::Insert:return longVoices[unsigned(LongVoice::Insert)];
            case Effect::Back:return longVoices[unsigned(profile==Profile::SMG?LongVoice::SMGBack:profile==Profile::Sniper?LongVoice::SniperBack:LongVoice::RifleBack)];
            case Effect::Empty:return longVoices[unsigned(profile==Profile::SMG?LongVoice::SMGEmpty:LongVoice::RifleEmpty)];
            // KL1 has separate authored rear and release strokes.
            case Effect::Forward:return voices[unsigned(Effect::Forward)];
            default:break;
        }
    }
    return voices[unsigned(effect)];
}
inline bool Play(Effect effect,unsigned category=0){auto* voice=Voice(effect,category);if(!voice)return false;
    voice->Stop();voice->SetCurrentPosition(0);return SUCCEEDED(voice->Play(0,0,0));}
}
