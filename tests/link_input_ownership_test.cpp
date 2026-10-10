#include "link_input_ownership.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
void check(bool value) {++checks;if(!value)std::abort();}
int main() {
    // The game must receive a release after opening the menu during a hold.
    // A menu press must not leak its repeats or its release after closing it.
    for(auto id:{0u,65u,256u,260u,577u,768u,772u}) {
        eflink::InputOwnership route;
        check(route.route(id,true,false));
        check(route.route(id,false,true));
        check(!route.route(id,true,true));
        check(!route.route(id,true,false));
        check(!route.route(id,false,false));
        check(route.route(id,true,false));
        route.focusLost();
        check(!route.route(id,false,true));
        route.seedGame(id);check(route.route(id,false,true));
    }
    // Keyboard and raw/window mouse streams keep independent ownership.
    eflink::InputOwnership route;
    check(route.route(256,true,false));check(!route.route(768,true,true));
    check(route.route(256,false,true));check(!route.route(768,false,false));
    std::printf("%u input ownership checks passed\n",checks);
}
