#include "link_cast_lifecycle.h"
#include <cstdio>
using namespace eflink;using nlohmann::json;
int failures=0;void check(bool b,const char* label){std::printf("%s %s\n",b?"PASS":"FAIL",label);if(!b)++failures;}
json event(std::uint16_t peer=1,std::uint64_t actor=101,std::uint64_t cast=1) {
    return {{"peer",peer},{"actor",actor},{"epoch",1},{"model","Assets/chr_test_postmodel.prefab"},{"cast",cast},{"seq",1},
        {"phase","begin"},{"skill","normal"},{"key","enemy:1"},{"life",31}};
}
int main() {
    RoomCastLedger ledger;auto begin=event();auto accept=[&](json e,std::uint16_t p=1,std::uint64_t a=101,std::uint64_t t=100){return ledger.accept(e,p,a,1,"Assets/chr_test_postmodel.prefab",t);};
    check(accept(begin),"a native player's unique cast begins once");
    check(!accept(begin),"a repeated begin cannot replay effects twice");
    check(accept(event(2,102),2,102)&&ledger.active(100).size()==2,"two actors may use the same skill and cast number independently");
    auto ended=begin;ended["phase"]="end";ended["seq"]=2;
    check(accept(ended)&&ledger.active(100).size()==1,"ending one caster does not terminate another player's skill");
    check(!accept(begin)&&!accept(ended),"terminal casts cannot be resurrected or ended twice");
    auto second=event(1,101,2);check(accept(second),"the next cast of the same skill has its own identity");
    auto wrong=second;wrong["phase"]="interrupt";wrong["seq"]=2;wrong["skill"]="another";
    check(!accept(wrong),"a terminal event cannot switch the original cast's skill");
    wrong=second;wrong["phase"]="interrupt";wrong["seq"]=2;
    check(accept(wrong),"native interruption terminates the specific cast");
    auto reordered=event(1,101,3);reordered["phase"]="end";reordered["seq"]=2;
    check(accept(reordered)&&!accept(event(1,101,3)),"a terminal event arriving before begin leaves a tombstone");
    auto old=event(1,101,1);check(!accept(old,1,101,50000),"expired tombstones cannot allow old cast numbers to replay");
    auto spoof=event(1,101,4);spoof["peer"]=2;check(!accept(spoof),"a cast cannot claim another sender");
    spoof=event(1,101,4);spoof["actor"]=102;check(!accept(spoof),"a cast cannot claim another native actor");
    spoof=event(1,101,4);spoof["epoch"]=2;check(!accept(spoof),"another scene generation cannot mutate active casts");
    spoof=event(1,101,4);spoof["model"]="Assets/chr_other_postmodel.prefab";check(!accept(spoof),"operator replacement cannot retain the prior skill identity");
    spoof=event(1,101,4);spoof["life"]=0;check(!accept(spoof),"targeted casts require a target spawn identity");
    spoof=event(1,101,4);spoof["key"]="";spoof["life"]=0;check(accept(spoof),"untargeted skills have an explicit empty target");
    spoof=event(1,101,5);spoof["amount"]=999;check(!accept(spoof),"presentation events carry no damage result or arbitrary extra fields");
    check(ledger.active(50000).empty(),"abandoned casts expire from presentation baselines");
    ledger.clear();check(ledger.active(100).empty()&&accept(event()),"room reset clears every cast and permits a new scene namespace");
    auto next=event(1,101,2);check(accept(next)&&accept(ended)&&ledger.active(100).size()==1&&ledger.active(100)[0]["cast"]==2,
        "an old end callback cannot terminate a replacement cast of the same skill");
    ledger.clear();bool filled=true;
    for(std::uint64_t i=1;i<=RoomCastLedger::maxActive;++i)filled=accept(event(1,101,i))&&filled;
    check(filled&&!accept(event(1,101,RoomCastLedger::maxActive+1)),"active casts are bounded to the full-baseline capacity");
    auto firstEnd=event();firstEnd["phase"]="end";firstEnd["seq"]=2;
    check(accept(firstEnd)&&accept(event(1,101,RoomCastLedger::maxActive+1))&&ledger.active(100).size()==RoomCastLedger::maxActive,
        "terminal events free baseline capacity without consuming a rejected begin's identity");
    return failures?1:0;
}
