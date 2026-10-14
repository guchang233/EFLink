#include "link_coop_contract.h"
#include <cstdio>
using namespace eflink;
using nlohmann::json;
int failures=0;
void check(bool ok,const char* label) {std::printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;}
int main() {
    json hello={{"world_schema",coopWorldSchema},{"contract_revision",coopContractRevision},{"contract",coopContractName},{"build","test"},{"game_version","fixture"}};
    check(validCoopHello(hello),"current room contract is compatible");
    auto bad=hello;bad["contract_revision"]=0;check(!validCoopHello(bad),"an old room contract cannot certify cooperation");
    bad=hello;bad["world_schema"]=2;check(!validCoopHello(bad),"legacy snapshots do not pass negotiation");
    bad=hello;bad["contract"]="other";check(!validCoopHello(bad),"matching numbers alone cannot select another contract");
    bad=hello;bad["build"]=12;check(!validCoopHello(bad),"malformed negotiation is rejected");
    check(!positiveRoomId(-1)&&!positiveRoomId(0)&&positiveRoomId(std::uint64_t(1)),"room identities reject zero and negative numbers");
    return failures?1:0;
}
