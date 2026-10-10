#include "authority/link_official_view.h"
#include <cstdlib>
#include <iostream>
using namespace eflink::authority;
void check(bool value,const char* text) {if(!value){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}std::cout<<"PASS "<<text<<'\n';}
std::string msg(unsigned field,const std::string& body) {std::string out;view_wire::message(out,field,body);return out;}
int main() try {
    const std::string character="\x08\x01",monster="\x08\x02",npc="\x08\x03";
    const auto objects=msg(1,character)+msg(2,monster)+msg(4,npc);
    // Unknown fields and envelope flags must survive untouched in both halves.
    const auto unknown=msg(77,std::string("\x00\xff",2));const std::string flags="\x10\x01";
    const auto split=splitOfficialView("SC_OBJECT_ENTER_VIEW",msg(1,objects)+flags+unknown);
    check(split.changed&&split.visible==msg(1,msg(1,character)+msg(4,npc))+flags+unknown,
        "mixed enter preserves characters, NPCs, flags and unknown fields");
    check(split.withheld==msg(1,msg(2,monster))+flags+unknown,
        "only the private monster fragment is retained for native restoration");
    const auto pure=splitOfficialView("SC_OBJECT_ENTER_VIEW",msg(1,msg(2,monster))+flags);
    check(pure.changed&&pure.visible.empty()&&pure.withheld==msg(1,msg(2,monster))+flags,
        "pure monster enter is withheld completely even with envelope flags");
    check(!splitOfficialView("SC_OBJECT_ENTER_VIEW",msg(1,msg(1,character))).changed,
        "character-only enter passes the original response unchanged");
    const auto repeated=splitOfficialView("SC_OBJECT_ENTER_VIEW",msg(1,msg(1,character))+msg(1,msg(2,monster)));
    check(repeated.visible==msg(1,msg(1,character))&&repeated.withheld==msg(1,msg(2,monster)),
        "multiple detail fields retain protobuf merge semantics");
    const std::string monsterLeave="\x08\x03\x10\x07",charLeave="\x10\x08";
    const auto leave=splitOfficialView("SC_OBJECT_LEAVE_VIEW",msg(1,charLeave)+msg(1,monsterLeave)+unknown);
    check(leave.changed&&leave.visible==msg(1,charLeave)+unknown&&leave.withheld==msg(1,monsterLeave)+unknown,
        "mixed leave keeps character removal and withholds only monster removal");
    check(splitOfficialView("SC_OBJECT_LEAVE_VIEW",msg(1,monsterLeave)).visible.empty(),
        "pure monster leave does not dispatch an empty native response");
    check(!splitOfficialView("SC_OBJECT_LEAVE_VIEW",msg(1,charLeave)).changed,
        "default object type zero remains a character removal");
    const std::string overwritten("\x08\x03\x08\x00\x10\x09",6);
    check(!splitOfficialView("SC_OBJECT_LEAVE_VIEW",msg(1,overwritten)).changed,
        "duplicate object type uses the last value like the protobuf parser");
    bool malformed=false;try {splitOfficialView("SC_OBJECT_ENTER_VIEW",std::string("\x0a\x05\x00",3));}catch(const std::exception&){malformed=true;}
    check(malformed,"malformed lengths refuse partial partitioning");
    check(!splitOfficialView("SC_OTHER_MESSAGE",msg(1,msg(2,monster))).changed,
        "other gameplay messages remain outside this view-only adapter");
} catch(const std::exception& error) {std::cerr<<"FAIL unexpected exception: "<<error.what()<<'\n';return 1;}
