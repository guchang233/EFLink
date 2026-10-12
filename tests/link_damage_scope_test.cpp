#include "link_damage_scope.h"
#include <cstdio>
#include <thread>
#include <atomic>
using namespace eflink;
int failures=0;
void check(bool value,const char* label){std::printf("%s %s\n",value?"PASS":"FAIL",label);if(!value)++failures;}
int main() {
    int ability=1,part=2,modifier=3,other=4;
    {
        DamageApplicationScope transferred(&ability,&modifier);check(transferred.observe(),"outer native transfer owns one result observation");
        {DamageApplicationScope apply(&ability,&modifier);check(!apply.observe(),"nested ApplyModifier executes without reporting the same application twice");}
        {DamageApplicationScope again(&ability,&modifier);check(!again.observe(),"an inner return cannot release its outer application's identity");}
        DamageApplicationScope anotherTarget(&part,&modifier);check(anotherTarget.observe(),"a different native target retains its own observation");
        DamageApplicationScope anotherModifier(&ability,&other);check(anotherModifier.observe(),"an independent modifier is not merged into an existing hit");
        std::atomic<bool> separate=false;std::thread worker([&]{DamageApplicationScope concurrent(&ability,&modifier);separate=concurrent.observe();});worker.join();
        check(separate,"concurrent native threads do not consume each other's application scopes");
    }
    {DamageApplicationScope next(&ability,&modifier);check(next.observe(),"pooled pointer reuse after completion represents a new native application");}
    try {DamageApplicationScope failed(&ability,&modifier);throw 1;}catch(...) {}
    DamageApplicationScope recovered(&ability,&modifier);check(recovered.observe(),"native exception unwinding releases the observation scope");
    return failures?1:0;
}
