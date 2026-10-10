#include "authority/link_native_feed.h"
#include <cstdlib>
#include <iostream>
using namespace eflink::authority;
void check(bool value,const char* text) {if(!value){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}std::cout<<"PASS "<<text<<'\n';}
std::string field(const std::string& raw,int number) {
    std::vector<RawField> fields;if(!rawFields(raw,fields))return "<malformed>";
    for(const auto& f:fields)if(f.field==number)return std::string(f.whole);
    return {};
}
std::string payload(const std::string& raw,int number) {
    std::vector<RawField> fields;rawFields(raw,fields);
    for(const auto& f:fields)if(f.field==number&&f.wire==2)return std::string(f.payload);
    return {};
}
int main() try {
    const float spawn[3]{1,2,3},now[3]{10,20,30},rotation[3]{0,90,0};
    ProtoWriter common;common.u64(1,4614575380069703684ull);common.i32(2,3);common.str(3,"eny_0072_slimeml");
    common.msg(4,vector3(spawn));common.msg(5,vector3(rotation));common.u64(6,2100460001ull);common.i32(7,101);
    common.f64(8,29700);common.u64(9,0);common.u64(10,1);common.i32(11,80);
    ProtoWriter battle;battle.u64(3,7);battle.u64(4,106609);ProtoWriter part;battle.msg(6,part);
    ProtoWriter monster;monster.msg(1,common);monster.u64(2,99);monster.i32(3,40);monster.msg(5,battle);monster.str(6,"lib_wave_1");
    const auto adapted=officialMonsterForGuest(monster.bytes(),now,1234.5);
    const auto c=payload(adapted,1),b=payload(adapted,5);
    ProtoWriter expectPos;expectPos.msg(4,vector3(now));ProtoWriter expectHp;expectHp.f64(8,1234.5);
    check(field(c,4)==expectPos.bytes(),"position replaced with the host's current position");
    check(field(c,8)==expectHp.bytes(),"hp replaced with the host's current value");
    check(field(c,6)==field(payload(monster.bytes(),1),6)&&field(c,10)==field(payload(monster.bytes(),1),10)&&field(c,11)==field(payload(monster.bytes(),1),11),
        "script ownership, battle-create flag and AOI radius kept verbatim");
    check(field(b,4).empty(),"battle_inst_id removed");
    check(field(b,3)==field(payload(monster.bytes(),5),3)&&field(b,6)==field(payload(monster.bytes(),5),6),"msg_generation and part info kept");
    check(field(adapted,2)==field(monster.bytes(),2)&&field(adapted,3)==field(monster.bytes(),3)&&field(adapted,6)==field(monster.bytes(),6),
        "origin id, level and spawner library kept");
    bool rejected=false;try {officialMonsterForGuest(std::string("\x0a\x05\x08",3),now,1);}catch(const std::exception&) {rejected=true;}
    check(rejected,"truncated official data is rejected");
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
