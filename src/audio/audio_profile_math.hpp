#pragma once
#include <string>
#include <cctype>
namespace AudioProfileMath {
enum class Result { Unchanged, Changed, Invalid };
inline Result DisableEax(const std::string& source,std::string& destination) {
    if(source.find('\0')!=std::string::npos)return Result::Invalid;
    std::size_t change=std::string::npos;unsigned found=0;
    for(std::size_t start=0;start<source.size();) {
        const auto end=source.find('\n',start),limit=end==std::string::npos?source.size():end;
        auto at=start;while(at<limit&&(source[at]==' '||source[at]=='\t'||source[at]=='\r'))++at;
        constexpr char key[]="useeax";bool matches=true;
        for(unsigned i=0;i<6;++i)if(at+i>=limit||std::tolower(static_cast<unsigned char>(source[at+i]))!=key[i])matches=false;
        if(matches&&at+6<limit&&(source[at+6]==' '||source[at+6]=='\t'||source[at+6]=='=')) {
            if(++found>1)return Result::Invalid;at+=6;
            while(at<limit&&(source[at]==' '||source[at]=='\t'||source[at]=='='))++at;
            if(at>=limit||(source[at]!='0'&&source[at]!='1'))return Result::Invalid;
            const auto value=at++;
            while(at<limit&&(source[at]==' '||source[at]=='\t'||source[at]=='\r'))++at;
            if(at<limit&&source[at]!=';'&&source.compare(at,2,"//")!=0)return Result::Invalid;
            if(source[value]=='1')change=value;
        }
        if(end==std::string::npos)break;start=end+1;
    }
    destination=source;
    if(change==std::string::npos)return Result::Unchanged;
    destination[change]='0';return Result::Changed;
}
}
