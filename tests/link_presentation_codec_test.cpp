#include "link_presentation_codec.h"
#include <cstdio>
using namespace eflink;
int failed=0;void check(bool v,const char* s){std::printf("%s %s\n",v?"PASS":"FAIL",s);if(!v)++failed;}
int main() {
    std::string plain;for(unsigned i=0;i<200;++i)plain+="{\"name\":\"Bip001_L_Finger01\",\"rotation\":[120,20,15,32000]},";
    auto packed=packPresentation(plain,65536);
    check(packedPresentation(packed.data(),packed.size())&&packed.size()<plain.size()/2,"repeated roster metadata compresses substantially");
    auto decoded=unpackPresentation(packed.data(),packed.size(),65536);
    check(decoded&&*decoded==plain,"a full independent frame round trips without a previous baseline");
    check(packPresentation("small",65536)=="small","small events remain unchanged");
    auto bad=packed;bad[4]=bad[5]=bad[6]=bad[7]=char(255);
    check(!unpackPresentation(bad.data(),bad.size(),65536),"declared oversized output is refused before allocation");
    check(!unpackPresentation(packed.data(),8,65536),"truncated frame is refused");
    check(!unpackPresentation(packed.data(),packed.size()/2,65536),"truncated compressed body cannot produce partial state");
    check(!unpackPresentation(packed.data(),packed.size(),10),"encoded and decoded capacities are bounded");
    return failed?1:0;
}
