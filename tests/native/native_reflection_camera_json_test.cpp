#include "native_reflection_camera_json.hpp"
#include "performance_log.hpp"
#include <cstdlib>
#include <limits>
#include <string>
namespace {unsigned checks=0;void Check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}}
int main(){
    auto* out=std::tmpfile();Check(out!=nullptr,"private temporary serializer stream");
    NativeCameraKL::ReflectionEvidence absent,value;value.valid=true;value.sequence=UINT64_MAX;value.reason=2;value.eye=1;
    value.originalView[0]=std::numeric_limits<float>::quiet_NaN();value.originalView[1]=std::numeric_limits<float>::infinity();
    value.originalView[2]=-std::numeric_limits<float>::infinity();value.originalView[3]=25522.125f;
    value.resultProjection[0]=.78819764f;
    std::fputs("{\"pid\":18700,",out);NativeReflectionCameraJSON::WriteEvidence(out,"absent",absent);std::fputc(',',out);
    NativeReflectionCameraJSON::WriteEvidence(out,"rejected",value);std::fputc(',',out);
    NativeCameraKL::ReflectionProducerEvidence producer;producer.valid=true;producer.sequence=UINT64_MAX;
    producer.trace.reason=NativeReflectionPacketsKL::Failure::Entry;producer.trace.header[1]=0xdeadcafe;
    producer.trace.observed.packets[0].address=0x12345678;
    std::memcpy(producer.trace.observed.packets[0].bytes.data()+24,value.originalView,sizeof(value.originalView));
    NativeReflectionCameraJSON::WriteProducerEvidence(out,"producer",producer);std::fputc('}',out);
    Check(!std::ferror(out)&&std::fflush(out)==0&&std::fseek(out,0,SEEK_SET)==0,"complete serializer stream readable");
    std::string json;char bytes[512];std::size_t n=0;while((n=std::fread(bytes,1,sizeof(bytes),out)))json.append(bytes,n);std::fclose(out);
    std::uint64_t pid=0;Check(PerformanceLog::Json(json).Unsigned("pid",pid)&&pid==18700,"actual recorder parser accepts complete nested evidence with invalid native floats");
    Check(json.find("\"absent\":null")!=std::string::npos,"missing evidence is explicitly unavailable");
    Check(json.find("\"originalView\":[null,null,null,25522.125,")!=std::string::npos,"NaN and infinities retain positions without corrupting the document");
    Check(json.find("18446744073709551615")!=std::string::npos,"64-bit evidence sequence is not truncated");
    Check(json.find("\"reason\":9")!=std::string::npos&&json.find("3735931646")!=std::string::npos,"producer rejection preserves exact reason and header before certificate authorization");
    Check(json.find("\"view\":[null,null,null,25522.125,")!=std::string::npos&&json.find("\"rawHex\":\"")!=std::string::npos,"rejected producer includes readable matrices and exact packet bytes without invalid JSON");
    Check(json.find("\"originalProjection\":[")!=std::string::npos&&json.find("\"obliqueProjection\":[")!=std::string::npos&&
        json.find("\"finalNativeProjection\":[")!=std::string::npos&&json.find("\"vrProjection\":[")!=std::string::npos&&json.find("\"resultProjection\":[")!=std::string::npos,"all native and VR lenses survive the production serializer");
    std::printf("Reflection evidence JSON: %u checks PASS.\n",checks);return 0;
}
