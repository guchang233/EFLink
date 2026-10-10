#include "authority/link_native_op_policy.h"
#include <cstdlib>
#include <iostream>
void check(bool value,const char* text) {if(!value){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}std::cout<<"PASS "<<text<<'\n';}
int main() {
    eflink::authority::NativeOpPolicy policy;
    constexpr std::uint64_t privateId=2100390320,hostId=2100391044,otherId=2100390311;
    check(!policy.suppress(privateId,123459),"private official battle reports are allowed before joining");
    policy.rememberFed(hostId);
    check(policy.suppress(hostId,0),"native host feed operations never enter the private battle stream");
    policy.rememberRetired({privateId});
    // The captured native Finish/Withdraw stack retains serverId while its
    // battle instance is zero, even after the synchronous feed call has ended.
    check(policy.suppress(privateId,0),"asynchronous private withdrawal is attributed after the feed call returns");
    check(policy.suppress(hostId,0),"host withdrawal remains attributed after leaving the room");
    check(!policy.suppress(privateId,123459),"restoring the same private server ID with its official battle identity resumes reports");
    check(!policy.suppress(hostId,456789),"the official world may reuse a previous host monster ID without suppression");
    check(!policy.suppress(otherId,0)&&!policy.suppress(0,0),"unrelated and unidentified sources are never blanket blocked");
    check(policy.suppress(0xEF00000000000001ULL,0),"existing native room actor reports remain isolated");
    eflink::authority::NativeOpPolicy bounded(2);bounded.rememberFed(hostId);
    bool refused=false;try {bounded.rememberRetired({privateId,otherId});}catch(const std::exception&){refused=true;}
    check(refused&&!bounded.suppress(privateId,0)&&!bounded.suppress(otherId,0),"provenance capacity refuses a whole batch before partial changes");
    bounded.rememberRetired({hostId,privateId,privateId});
    check(bounded.retiredCount()==2&&bounded.suppress(privateId,0),"duplicate IDs share capacity across feed and withdrawal history");
}
