#include "audio_profile_math.hpp"
#include <cstdio>
#include <cstdlib>
namespace {unsigned checks=0;void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}}
int main() {
    using namespace AudioProfileMath;
    for(const auto& source:{std::string("UseEAX 1\r\nResolution 3840x2160\r\nAntialias 8\r\n"),std::string("  uSeEaX = 1 ; saved\nResolution 1280x720\n"),std::string("; UseEAX 0\nUseEAX\t1//comment")}) {
        std::string result;Check(DisableEax(source,result)==Result::Changed,"enabled EAX is disabled");
        unsigned differences=0;for(std::size_t i=0;i<source.size();++i)if(source[i]!=result[i]){++differences;Check(source[i]=='1'&&result[i]=='0',"only the EAX value changes");}
        Check(source.size()==result.size()&&differences==1,"all other bytes including graphics settings are preserved");
        std::string again;Check(DisableEax(result,again)==Result::Unchanged&&again==result,"already-disabled profile stays byte-exact");
    }
    for(const auto& source:{std::string("Resolution 1280x720\r\n"),std::string("UseEAX 0\n"),std::string("// UseEAX 1\n"),std::string("UseEAXExtra 1\n")}) {
        std::string out;Check(DisableEax(source,out)==Result::Unchanged&&out==source,"unrelated or already disabled profile is preserved");
    }
    for(const auto& source:{std::string("UseEAX 1\nUseEAX 0\n"),std::string("UseEAX 10\n"),std::string("UseEAX yes\n"),std::string("UseEAX =\n"),std::string("UseEAX 1\0ignored",16)}) {
        std::string out="unchanged";Check(DisableEax(source,out)==Result::Invalid&&out=="unchanged","ambiguous or unsupported profile rejected without output writes");
    }
    std::printf("Audio profile preservation: %u checks passed.\n",checks);
}
