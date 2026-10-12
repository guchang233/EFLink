#include "link_presentation.h"
#include <cstdio>
using namespace eflink;using nlohmann::json;
int failed=0;
void check(bool value,const char* name){std::printf("%s %s\n",value?"PASS":"FAIL",name);if(!value)++failed;}
int main() {
    json member={{"slot",0},{"flags",1},{"life",1ull},{"controlled",true},{"model","chr_0017_yvonne/model"},
        {"position",{0,0,0}},{"rotation",{0,0,0,1}},{"root",{0,0,0}},{"bones",{0,0,0,32767}},{"names",{"Bip001"}}};
    json frame={{"sample_time",100ull},{"version",1},{"seq",1ull},{"members",json::array({member})},{"effects",json::array()}};
    check(validPresentation(frame),"one controlled operator presentation accepted");
    for(unsigned slot=1;slot<4;++slot){auto m=member;m["slot"]=slot;m["controlled"]=false;frame["members"].push_back(m);}
    check(validPresentation(frame),"four distinct slots accepted");
    auto invalid=frame;invalid["members"][1]["controlled"]=true;check(!validPresentation(invalid),"two controlled copies rejected");
    invalid=frame;invalid["members"][1]["slot"]=0;check(!validPresentation(invalid),"duplicate roster slot rejected");
    invalid=frame;invalid["members"][0]["bones"]={1,2,3};check(!validPresentation(invalid),"incomplete bone quaternion rejected");
    invalid=frame;invalid["members"][0]["position"]={NAN,0,0};check(!validPresentation(invalid),"non-finite movement rejected");
    invalid=frame;invalid["members"][0]["model"]="chr_0017/../../other";check(!validPresentation(invalid),"resource traversal rejected");
    json effect={{"id",21ull},{"name","fx_char_0017_attack"},{"position",{0,0,0}},{"rotation",{0,0,0,1}},{"scale",{1,1,1}}};
    frame["effects"].push_back(effect);check(validPresentation(frame),"isolated visual effect instance accepted");
    frame["effects"].push_back(effect);check(!validPresentation(frame),"duplicate effect lifetime rejected");
    frame["effects"]=json::array();frame["material_vfx"]=json::array({{{"id",30ull},{"slot",1},{"life",1ull},{"name","weapon_light"},{"path","weapon/root"}}});
    check(validPresentation(frame),"material effect binds to a specific roster lifetime");
    invalid=frame;invalid["material_vfx"][0]["life"]=9ull;check(!validPresentation(invalid),"material effects cannot bind to an old party allocation");
    invalid=frame;invalid["material_vfx"][0]["path"]="../../weapon";check(!validPresentation(invalid),"material helper lookup cannot escape the room actor");
    check(validPoise({{"value",25},{"max",100}}),"native poise range accepted");
    check(!validPoise({{"value",102},{"max",100}})&&!validPoise({{"value",NAN},{"max",100}}),"invalid poise cannot reach native setter");
    return failed?1:0;
}
