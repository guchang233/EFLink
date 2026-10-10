#include "link_map_consent.h"
#include <iostream>
#include <cstdlib>
void check(bool value,const char* text) {if(!value){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}std::cout<<"PASS "<<text<<'\n';}
int main() {
    eflink::HostMapConsent c;
    c.observe("map/level1",false);check(!c.pending(),"initial admission does not ask twice");
    c.observe("map/level2",false);auto first=c.token();
    check(c.pending()&&c.takeNotice()&&!c.takeNotice(),"host level change announces once and waits");
    c.observe("map/level2",false);check(c.pending()&&c.token()==first,"repeated snapshots cannot grant travel");
    c.choose(first,false);check(c.pending()&&c.deferred(),"decline keeps native travel blocked");
    c.observe("map/level2",false);check(c.pending()&&c.deferred(),"later snapshots preserve decline");
    c.observe("map2/level1",false);check(c.pending()&&!c.deferred()&&c.takeNotice(),"another map supersedes the deferred choice");
    bool rejected=false;try{c.choose(first,true);}catch(const std::exception&){rejected=true;}
    check(rejected&&c.pending(),"a stale confirmation cannot approve a new destination");
    auto second=c.token();c.choose(second,true);check(!c.pending(),"current confirmation permits native travel");
    c.observe("map2/level1",false);check(!c.pending(),"approved loading does not prompt every frame");
    c.observe("map/level1",true);check(!c.pending()&&!c.takeNotice(),"host returning to the guest map resumes directly");
    c.observe("map3/level1",false);auto oldRoom=c.token();c.reset();c.observe("map/level1",false);c.observe("map4/level1",false);
    rejected=false;try{c.choose(oldRoom,true);}catch(const std::exception&){rejected=true;}
    check(rejected&&c.pending(),"reconnecting cannot accept a previous room's confirmation");
    c.explicitTeleport("another/level");check(c.pending(),"unrelated teleport cannot approve host travel");
    c.explicitTeleport("map4/level1");check(!c.pending(),"explicit player teleport to the host destination is consent");
}
