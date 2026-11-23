#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cwchar>
#include <cstring>
#include <string>
#include <vector>

// INI snapshots retain encoding, comments and unrelated keys. Saving refuses
// concurrent edits and restores previous bytes if a later write fails.
namespace VrModIni {
struct File {std::wstring name,text;std::string bytes;bool existed=false;enum class Encoding{Utf8,Utf8Bom,Utf16,Ansi} encoding=Encoding::Utf8;};
inline std::wstring Trim(std::wstring value){const auto first=value.find_first_not_of(L" \t\r\n");if(first==std::wstring::npos)return {};return value.substr(first,value.find_last_not_of(L" \t\r\n")-first+1);}
inline bool Same(const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
inline bool Bytes(HANDLE file,std::string& bytes){LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>4*1024*1024)return false;bytes.resize(static_cast<size_t>(size.QuadPart));DWORD got=0;return !size.QuadPart||(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size());}
inline bool Decode(File& file){
    const auto& bytes=file.bytes;
    if(bytes.size()>=2&&static_cast<unsigned char>(bytes[0])==0xff&&static_cast<unsigned char>(bytes[1])==0xfe){
        if(bytes.size()%2)return false;file.encoding=File::Encoding::Utf16;
        file.text.resize((bytes.size()-2)/2);if(!file.text.empty())std::memcpy(file.text.data(),bytes.data()+2,bytes.size()-2);return true;
    }
    size_t offset=0;file.encoding=File::Encoding::Utf8;
    if(bytes.size()>=3&&bytes.compare(0,3,"\xef\xbb\xbf")==0){offset=3;file.encoding=File::Encoding::Utf8Bom;}
    if(bytes.size()==offset){file.text.clear();return true;}
    UINT page=CP_UTF8;int count=MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,bytes.data()+offset,static_cast<int>(bytes.size()-offset),nullptr,0);
    if(!count){page=CP_ACP;file.encoding=File::Encoding::Ansi;count=MultiByteToWideChar(page,0,bytes.data()+offset,static_cast<int>(bytes.size()-offset),nullptr,0);}
    if(!count)return false;file.text.resize(count);return MultiByteToWideChar(page,0,bytes.data()+offset,static_cast<int>(bytes.size()-offset),file.text.data(),count)==count;
}
inline bool Encode(const File& file,std::string& bytes){
    if(file.encoding==File::Encoding::Utf16){bytes="\xff\xfe";bytes.append(reinterpret_cast<const char*>(file.text.data()),file.text.size()*sizeof(wchar_t));return true;}
    const UINT page=file.encoding==File::Encoding::Ansi?CP_ACP:CP_UTF8;
    if(file.text.empty()){bytes=file.encoding==File::Encoding::Utf8Bom?"\xef\xbb\xbf":"";return true;}
    BOOL replaced=FALSE;BOOL* defaultCharacter=page==CP_ACP?&replaced:nullptr;
    const int count=WideCharToMultiByte(page,0,file.text.data(),static_cast<int>(file.text.size()),nullptr,0,nullptr,defaultCharacter);
    if(count<=0||replaced)return false;bytes.resize(count);
    if(WideCharToMultiByte(page,0,file.text.data(),static_cast<int>(file.text.size()),bytes.data(),count,nullptr,defaultCharacter)!=count||replaced)return false;
    if(file.encoding==File::Encoding::Utf8Bom)bytes.insert(0,"\xef\xbb\xbf");return true;
}
inline std::wstring Read(const File& file,const std::wstring& section,const std::wstring& key,const std::wstring& fallback){
    bool inside=false;
    for(size_t at=0;at<file.text.size();){const auto end=file.text.find(L'\n',at);auto line=Trim(file.text.substr(at,end==std::wstring::npos?std::wstring::npos:end-at));at=end==std::wstring::npos?file.text.size():end+1;
        if(line.empty()||line[0]==L';'||line[0]==L'#')continue;
        if(line.front()==L'['&&line.find(L']')!=std::wstring::npos){if(inside)return fallback;inside=Same(Trim(line.substr(1,line.find(L']')-1)),section);continue;}
        const auto equal=line.find(L'=');if(inside&&equal!=std::wstring::npos&&Same(Trim(line.substr(0,equal)),key)){auto value=Trim(line.substr(equal+1));if(value.size()>1&&value.front()==L'"'&&value.back()==L'"')value=value.substr(1,value.size()-2);return value;}
    }return fallback;
}
inline int Integer(const File& file,const wchar_t* section,const wchar_t* key,int fallback){const auto text=Read(file,section,key,L"");wchar_t* end=nullptr;const long value=std::wcstol(text.c_str(),&end,10);return !text.empty()&&end!=text.c_str()&&value>=-1000000&&value<=1000000?static_cast<int>(value):fallback;}
inline bool Scalar(const std::wstring& value){return value.find_first_of(L"\r\n\0",0,3)==std::wstring::npos;}
inline void Patch(File& file,const std::wstring& section,const std::wstring& key,const std::wstring& value,bool preserveInlineComment=true){
    const auto newline=file.text.find(L"\r\n")!=std::wstring::npos?L"\r\n":L"\n";bool inside=false;size_t insert=file.text.size();
    for(size_t at=0;at<file.text.size();){const auto end=file.text.find(L'\n',at);const auto length=end==std::wstring::npos?file.text.size()-at:end-at;auto line=Trim(file.text.substr(at,length));
        if(!line.empty()&&line.front()==L'['&&line.find(L']')!=std::wstring::npos){if(inside){insert=at;break;}inside=Same(Trim(line.substr(1,line.find(L']')-1)),section);}
        else if(inside){const auto equal=file.text.find(L'=',at);if(equal<at+length&&Same(Trim(file.text.substr(at,equal-at)),key)){
                size_t contentEnd=at+length;if(contentEnd>at&&file.text[contentEnd-1]==L'\r')--contentEnd;
                const auto old=file.text.substr(equal+1,contentEnd-equal-1);std::wstring suffix;
                if(preserveInlineComment)for(size_t i=1;i<old.size();++i)if((old[i]==L';'||old[i]==L'#')&&(old[i-1]==L' '||old[i-1]==L'\t')){suffix=old.substr(i-1);break;}
                file.text.replace(equal+1,contentEnd-equal-1,value+suffix);return;
            }}
        at=end==std::wstring::npos?file.text.size():end+1;
    }
    std::wstring addition=key+L"="+value+newline;
    if(inside){if(insert&&file.text[insert-1]!=L'\n')addition=newline+addition;file.text.insert(insert,addition);}
    else {if(!file.text.empty()&&file.text.back()!=L'\n')file.text+=newline;file.text+=L"["+section+L"]"+newline+addition;}
}

inline bool Load(const std::wstring& path,File& output,std::wstring& error,bool required=false){
    File file;file.name=path;
    HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(handle==INVALID_HANDLE_VALUE){
        if(!required&&GetLastError()==ERROR_FILE_NOT_FOUND){file.encoding=File::Encoding::Utf16;output=file;return true;}
        error=L"Could not read settings: "+path;return false;
    }
    file.existed=true;const bool okay=Bytes(handle,file.bytes);CloseHandle(handle);
    if(!okay||!Decode(file)||file.text.find(L'\0')!=std::wstring::npos){error=L"Invalid or oversized settings: "+path;return false;}
    output=std::move(file);return true;
}
inline bool Save(std::vector<File>& snapshots,std::vector<File> updated,std::wstring& error){
    if(snapshots.size()!=updated.size()){error=L"Settings snapshot mismatch.";return false;}
    const auto count=updated.size();std::vector<std::string> bytes(count);std::vector<HANDLE> handles(count,INVALID_HANDLE_VALUE);std::vector<bool> created(count);
    auto release=[&]{for(auto& h:handles)if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);h=INVALID_HANDLE_VALUE;}};
    auto cleanup=[&]{release();for(size_t i=0;i<count;++i)if(created[i])DeleteFileW(updated[i].name.c_str());};
    for(size_t i=0;i<count;++i){
        if(updated[i].name!=snapshots[i].name||!Encode(updated[i],bytes[i])){error=L"Could not encode settings.";cleanup();return false;}
        if((!snapshots[i].existed&&updated[i].text.empty())||(bytes[i]==snapshots[i].bytes&&snapshots[i].existed))continue;
        handles[i]=CreateFileW(updated[i].name.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,snapshots[i].existed?OPEN_EXISTING:CREATE_NEW,0,nullptr);
        if(handles[i]==INVALID_HANDLE_VALUE){error=L"Could not open settings for writing: "+updated[i].name;cleanup();return false;}
        created[i]=!snapshots[i].existed;std::string current;
        if(!Bytes(handles[i],current)||current!=snapshots[i].bytes){error=L"Settings were edited by another application. Close and reopen this window.";cleanup();return false;}
    }
    auto write=[](HANDLE handle,const std::string& data){LARGE_INTEGER zero{};DWORD n=0;return SetFilePointerEx(handle,zero,nullptr,FILE_BEGIN)&&WriteFile(handle,data.data(),static_cast<DWORD>(data.size()),&n,nullptr)&&n==data.size()&&SetEndOfFile(handle)&&FlushFileBuffers(handle);};
    for(size_t i=0;i<count;++i)if(handles[i]!=INVALID_HANDLE_VALUE&&!write(handles[i],bytes[i])){
        bool restored=true;for(size_t j=0;j<=i;++j)if(handles[j]!=INVALID_HANDLE_VALUE&&!created[j])restored=write(handles[j],snapshots[j].bytes)&&restored;
        error=restored?L"Could not save settings. Previous values were restored.":L"Could not save or fully restore settings. Check file permissions and disk space.";cleanup();return false;
    }
    for(size_t i=0;i<count;++i)if(handles[i]!=INVALID_HANDLE_VALUE){updated[i].bytes=std::move(bytes[i]);updated[i].existed=true;}
    release();snapshots=std::move(updated);error.clear();return true;
}
}
