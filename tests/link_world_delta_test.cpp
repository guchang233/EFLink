#include "link_world_delta.h"
#include <cstdio>
using nlohmann::json;using namespace eflink;
int failures=0;
void check(bool value,const char* label){std::printf("%s %s\n",value?"PASS":"FAIL",label);if(!value)++failures;}
json fixture() {
    return {{"map","map01"},{"entities",json::array({{{"key","level:1"},{"life",1},{"hp",100},{"position",{0,0,0}},{"official",std::string(16000,'a')}},
        {{"key","level:2"},{"life",2},{"hp",100}}})},{"players",json::array({{{"peer",1},{"entity",11}}})}};
}
int main() {
    auto before=fixture(),after=before;after["entities"][0]["position"]={2,0,3};after["entities"][0]["hp"]=70;
    auto delta=worldDelta(before,after);
    check(applyWorldDelta(before,delta)==after,"field deltas reconstruct the complete authoritative world");
    check(delta.dump().size()*20<after.dump().size(),"movement does not retransmit a monster's immutable official data");
    after["entities"].erase(after["entities"].begin());after["entities"][0]["hp"]=55;
    check(applyWorldDelta(before,worldDelta(before,after))==after,"removing one enemy cannot shift updates onto a different entity");
    after["entities"][0]["life"]=3;after["entities"][0]["hp"]=100;
    check(applyWorldDelta(before,worldDelta(before,after))==after,"a reused key receives the replacement spawn's full changed identity");
    after["players"].push_back({{"peer",2},{"entity",22}});after.erase("map");
    check(applyWorldDelta(before,worldDelta(before,after))==after,"late players and removed optional world fields reconstruct together");
    auto original=before;bool failed=false;
    try {applyWorldDelta(before,json::array({{{"op","replace"},{"path","/entities/level:1/key"},{"value","another"}}}));}catch(...) {failed=true;}
    check(failed&&before==original,"invalid identity changes fail without mutating the previous baseline");
    check(!validWorldDelta(json::array({{{"op","copy"},{"path","/entities"},{"from","/players"}}})),"non-state patch operations are refused");
    auto duplicated=before;duplicated["entities"].push_back(duplicated["entities"][0]);failed=false;
    try {worldDelta(duplicated,after);}catch(...) {failed=true;}
    check(failed,"duplicate baseline IDs cannot enter the delta encoder");
    check(applyWorldDelta(before,json::array())==before,"an empty state delta preserves a valid baseline");
    return failures?1:0;
}
