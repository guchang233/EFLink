#include "link_resource_authority.h"
#include <cstdio>
using namespace eflink;
using Json=nlohmann::json;
int failures=0;
void check(bool ok,const char* text){std::printf("%s %s\n",ok?"[ok]":"[FAIL]",text);if(!ok)++failures;}
Json mineral(int hp=10000,bool collected=false) {
    return {{"key","level:mine:logic:12"},{"kind","mine"},{"template","mineral_test"},{"position",{1,2,3}},
        {"hp_ratio",hp},{"max_hp_ratio",10000},{"collected",collected}};
}
Json intent(const Json& state,std::uint64_t seq=1) {
    return {{"room",17},{"epoch",5ull},{"peer",2},{"actor",29ull},{"request",seq},{"key",state.at("key")},
        {"life",state.at("life")},{"cast",3ull},{"strike",seq},{"operation","mine"}};
}
int main() {
    ResourceAuthority authority;authority.reset(17,5);
    auto first=authority.observe({{mineral(),123}});auto state=first.at(0);
    check(validResourceBaseline(first),"native observation becomes a bounded public resource baseline");
    auto invalid=mineral();invalid["account_id"]=999;
    check(!validResourceObservation(invalid),"account IDs and unknown fields cannot enter resource snapshots");
    invalid=mineral();invalid["hp_ratio"]=.5;
    check(!validResourceObservation(invalid),"fractional ore durability is rejected");
    invalid=mineral();invalid["position"][0]=std::numeric_limits<double>::infinity();
    check(!validResourceObservation(invalid),"nonfinite positions cannot identify or authorize a resource");
    auto request=intent(state);
    check(!authority.submit(request,2,29,false,100),"a resource census cannot grant an unverified operation adapter");
    check(!authority.submit(request,3,29,true,100),"another sender cannot impersonate the resource operator");
    check(!authority.submit(request,2,30,true,100),"actor ownership is checked independently of sender fields");
    auto bad=request;bad["damage"]=999999;
    check(!authority.submit(bad,2,29,true,100),"client damage and claimed broken state cannot enter a mining request");
    bad=request;bad["epoch"]=4;
    check(!authority.submit(bad,2,29,true,100),"old scene requests are rejected");
    bad=request;bad["request"]=0;
    check(!authority.submit(bad,2,29,true,100),"zero request IDs are rejected");
    check(authority.submit(request,2,29,true,100),"a verified adapter receives one owned current-life mining intent");
    check(!authority.submit(request,2,29,true,100),"a duplicate cannot execute twice");
    bad=request;bad["request"]=2;
    check(!authority.submit(bad,2,29,true,101),"a new transport request ID cannot disguise the same native cast/strike/target");
    const auto life=state.at("life").get<std::uint64_t>();
    check(!authority.confirm(2,29,1,life,1,true,200),"a native call without an observed state change is not success");
    state=authority.observe({{mineral(7500),123}}).at(0);
    check(state.at("life")==life&&state.at("version")==2,"durability changes retain object life and advance native state revision");
    check(!authority.confirm(2,30,1,life,2,true,200),"another room actor cannot confirm a pending operation sharing the request number");
    check(!authority.confirm(2,29,1,life,2,false,200),"a changed snapshot alone cannot confirm an unrelated operation");
    check(authority.confirm(2,29,1,life,2,true,200).has_value(),"matching native acceptance and observed state confirm the intent");
    check(!authority.confirm(2,29,1,life,2,true,201),"confirmed results are exactly once");
    check(authority.submit(intent(state,2),2,29,true,300),"the next strike is independently identified");
    check(authority.submit(intent(state,3),2,29,true,300),"two concurrent strikes can await separate native results");
    state=authority.observe({{mineral(5000),123}}).at(0);
    check(authority.confirm(2,29,2,life,3,true,400).has_value()&&!authority.confirm(2,29,3,life,3,true,400),
        "one native state revision cannot acknowledge two independently submitted strikes");
    state=authority.observe({{mineral(0,true),123}}).at(0);
    check(authority.confirm(2,29,3,life,4,true,450).has_value(),"native destruction remains confirmable after the resource becomes collected");
    check(!authority.submit(intent(state,4),2,29,true,500),"a destroyed resource cannot accept another strike");
    state=authority.observe({{mineral(),123}}).at(0);
    check(state.at("life")!=life&&!authority.submit(request,2,29,true,500),"in-place regeneration revokes requests for the old ore lifetime");
    const auto renewed=state.at("life");
    state=authority.observe({{mineral(),124}}).at(0);
    check(state.at("life")!=renewed,"a pooled object reusing the same resource key receives a new lifetime");
    check(authority.submit(intent(state,4),2,29,true,500),"a current-life request can be queued after regeneration");
    authority.expire(2001);check(authority.pendingCount()==0,"unconfirmed native requests time out");
    check(!authority.submit(intent(state,4),2,29,true,2002),"timeout cannot make an old request executable again");
    check(authority.submit(intent(state,5),2,29,true,2002),"later requests retain their own identity");
    authority.revokePeer(2);check(authority.pendingCount()==0,"disconnect revokes pending operations");
    auto replacement=intent(state,1);replacement["actor"]=30;
    check(authority.submit(replacement,2,30,true,2003),"a replacement room actor has its own request sequence namespace");
    check(!authority.submit(request,2,30,true,2004),"reusing a peer number cannot authorize the old actor's mining intent");
    const auto before=authority.baseline();bool threw=false;
    try {authority.observe({{mineral(),124},{mineral(),125}});}catch(...){threw=true;}
    check(threw&&authority.baseline()==before,"ambiguous native identity rejects the whole census without replacing the baseline");
    authority.observe({});check(authority.baseline().empty(),"a complete empty census retires disappeared resources");
    authority.reset(17,6);state=authority.observe({{mineral(),124}}).at(0);
    check(!authority.submit(intent(state,6),2,29,true,2200),"new scene initialization does not restore old operation permissions");
    authority.reset();authority.reset();check(authority.pendingCount()==0&&authority.baseline().empty(),"cleanup remains idempotent");
    return failures?1:0;
}
