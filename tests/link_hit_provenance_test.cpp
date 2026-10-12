#include "link_hit_provenance.h"
#include "link_cast_lifecycle.h"
#include <cstdio>
using namespace eflink;using nlohmann::json;
int failures=0;void check(bool b,const char* s){std::printf("%s %s\n",b?"PASS":"FAIL",s);if(!b)++failures;}
int main() {
    NativeCastBindings bindings;NativeIdentity owner{17,3,123456,true};
    check(bindings.bind(0x123,4,1,101,1,11,owner,100),"native skill identity binds to one public room cast");
    check(bindings.resolve(0x123,4,1,101,1,owner,200)==11,"damage metadata resolves its original cast");
    check(!bindings.bind(0x123,4,1,101,1,12,owner,200),"reusing a native cast key cannot overwrite a still-live projectile identity");
    check(bindings.bind(0x123,5,1,101,1,12,owner,200)&&bindings.resolve(0x123,4,1,101,1,owner,300)==11,
        "pooled Skill reuse preserves the previous cast's delayed projectile attribution");
    auto recycled=owner;recycled.instance++;
    check(!bindings.resolve(0x123,4,1,101,1,recycled,300),"a recycled native owner cannot inherit old casts");
    check(!bindings.resolve(0x123,4,2,101,1,owner,300)&&!bindings.resolve(0x123,4,1,102,1,owner,300)&&
        !bindings.resolve(0x123,4,1,101,2,owner,300),"room, actor and scene mismatches cannot attribute hits");
    check(!bindings.resolve(0x123,4,1,101,1,owner,99)&&!bindings.resolve(0x123,4,1,101,1,owner,30100),"clock rollback and expired bindings cannot authorize a claim");
    check(!bindings.bind(0x124,0,1,101,1,13,owner,200),"an unknown zero native cast ID is never invented");
    bindings.retain(1,102,1,300);check(bindings.size()==0,"operator replacement releases native lookup records");
    json claim={{"actor",101},{"epoch",1},{"cast",11},{"key","enemy:1"},{"life",7},{"strike",1},{"action",0},{"unit",0}};
    check(validHitClaim(claim),"bounded public hit claims contain identity and damage-unit indices");
    auto bad=claim;bad["amount"]=5;check(!validHitClaim(bad),"client damage amounts cannot enter a hit claim");
    bad=claim;bad["native_skill"]=0x123;check(!validHitClaim(bad),"native addresses cannot enter a hit claim");
    bad=claim;bad["action"]=-1;check(!validHitClaim(bad),"unadapted negative native action indices are rejected");
    HitClaimLedger hits;check(hits.accept(claim,100)&&!hits.accept(claim,101),"a repeated hit occurrence is deduplicated");
    bad=claim;bad["unit"]=1;check(!hits.accept(bad,102),"changing a unit does not create a new occurrence identity");
    auto dot=claim;dot["strike"]=2;check(hits.accept(dot,103),"a separate continuous-damage tick can reuse the same action and unit");
    auto multi=claim;multi["key"]="enemy:2";check(hits.accept(multi,103),"a multi-target observation retains distinct target identity");
    RoomCastLedger casts;
    json began={{"peer",1},{"actor",101},{"epoch",1},{"model","Assets/chr_test_postmodel.prefab"},{"cast",11},{"seq",1},
        {"phase","begin"},{"skill","normal"},{"key","enemy:1"},{"life",7}};
    check(casts.accept(began,1,101,1,"Assets/chr_test_postmodel.prefab",100)&&casts.permitsClaim(claim,1,200),"a matching observed cast is required before claims");
    check(!casts.permitsClaim(claim,2,200)&&!casts.permitsClaim(multi,1,200),"claims cannot substitute another caster or unsupported secondary target");
    auto ended=began;ended["phase"]="end";ended["seq"]=2;
    check(casts.accept(ended,1,101,1,"Assets/chr_test_postmodel.prefab",29000)&&casts.permitsClaim(claim,1,29500)&&
        !casts.permitsClaim(claim,1,30100),"normal end retains delayed hits without extending the original claim window");
    casts.clear();ended["phase"]="interrupt";
    check(casts.accept(began,1,101,1,"Assets/chr_test_postmodel.prefab",100)&&casts.accept(ended,1,101,1,"Assets/chr_test_postmodel.prefab",200)&&
        !casts.permitsClaim(claim,1,201),"interrupted casts cannot authorize later claims");
    casts.clear();ended["phase"]="end";
    check(casts.accept(ended,1,101,1,"Assets/chr_test_postmodel.prefab",100)&&!casts.permitsClaim(claim,1,200),"an end-before-begin tombstone is not proof that a cast began");
    return failures?1:0;
}
