#include "link_coop_contract.h"
#include <cstdio>
using namespace eflink;
using nlohmann::json;
int failures=0;
void check(bool ok,const char* label) {std::printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;}
json enemy(std::uint64_t native=11,std::uint16_t owner=1) {
    return {{"key","level:enemy:7"},{"hp",100},{"server_id",7},{"battle_inst",19},{"native_spawn",native},{"owner",owner}};
}
int main() {
    json hello={{"world_schema",coopWorldSchema},{"contract_revision",coopContractRevision},{"contract",coopContractName},{"build","test"},{"game_version","fixture"}};
    check(validCoopHello(hello),"current room contract is compatible");
    auto bad=hello;bad["contract_revision"]=0;check(!validCoopHello(bad),"an old room contract cannot certify cooperation");
    bad=hello;bad["world_schema"]=2;check(!validCoopHello(bad),"legacy snapshots do not pass negotiation");
    bad=hello;bad["contract"]="other";check(!validCoopHello(bad),"matching numbers alone cannot select another contract");
    bad=hello;bad["build"]=12;check(!validCoopHello(bad),"malformed negotiation is rejected");
    check(!positiveRoomId(-1)&&!positiveRoomId(0)&&positiveRoomId(std::uint64_t(1)),"room identities reject zero and negative numbers");
    RoomEntityLedger ledger;
    auto entities=json::array({enemy()});ledger.annotate(entities);
    const auto life=entities[0]["life"].get<std::uint64_t>();
    check(life&&entities[0]["lease"]==1&&!entities[0].contains("native_spawn"),"host assigns a public lifetime without leaking a native UID");
    entities=json::array({enemy()});ledger.annotate(entities);
    check(entities[0]["life"]==life&&ledger.matches("level:enemy:7",life,1,1),"ordinary state updates preserve the spawn identity");
    check(ledger.assign("level:enemy:7",2)&&!ledger.matches("level:enemy:7",life,1,1),"ownership handover immediately revokes the old control lease");
    check(ledger.matches("level:enemy:7",life,2,2),"only the new owner and lease can control the same spawn");
    entities=json::array({enemy(11,2)});ledger.annotate(entities);
    check(entities[0]["lease"]==2&&entities[0]["life"]==life,"publishing the owner does not renew its lease again");
    entities=json::array({enemy(12,2)});ledger.annotate(entities);
    check(entities[0]["life"]!=life&&!ledger.matches("level:enemy:7",life),"a native allocation recycled under the same server ID gets a new room lifetime");
    const auto recycled=entities[0]["life"].get<std::uint64_t>();
    entities=json::array();ledger.annotate(entities);
    check(ledger.reference("level:enemy:7").is_null(),"unloaded entities cannot authorize late operations");
    entities=json::array({enemy(12,2)});ledger.annotate(entities);
    check(entities[0]["life"]!=recycled,"a removed key cannot revive its previous public identity");
    entities[0]["alive"]=false;entities[0]["hp"]=0;ledger.annotate(entities);
    check(ledger.reference("level:enemy:7").is_null(),"a death tombstone cannot receive further hits or control");
    bool duplicate=false;try {auto repeated=json::array({enemy(),enemy()});ledger.annotate(repeated);}catch(...) {duplicate=true;}
    check(duplicate,"duplicate keys are refused");
    auto preserved=json::array({enemy(31,1)});ledger.annotate(preserved);
    auto preservedRef=ledger.reference("level:enemy:7");
    auto invalid=json::array({enemy(42,2),enemy(43,2)});auto original=invalid;
    try {ledger.annotate(invalid);}catch(...) {}
    check(invalid==original&&ledger.reference("level:enemy:7")==preservedRef,"an invalid snapshot cannot partially change native control leases");
    ledger.clear();check(ledger.reference("level:enemy:7").is_null(),"scene and room reset clear all entity leases");
    return failures?1:0;
}
