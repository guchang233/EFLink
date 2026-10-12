#include "link_protocol.h"
#include "link_settings.h"
#include <cstdio>
#include <limits>
using namespace eflink;
int main() {
    int failures=0;auto check=[&](bool ok,const char* text){std::printf("%s %s\n",ok?"[ok]":"[FAIL]",text);if(!ok) ++failures;};
    proto::Writer w;w.str("中文中文",5);proto::Reader r(w.bytes.data(),w.bytes.size());check(r.str(5)=="中","UTF-8 truncation preserves complete characters");
    bool rejected=false;try {proto::Reader shortFrame(w.bytes.data(),0);shortFrame.u32();} catch(...) {rejected=true;}check(rejected,"truncated frame rejected");
    proto::PlayerState pose;pose.position[0]=std::numeric_limits<float>::quiet_NaN();proto::Writer payload;proto::write(payload,pose);rejected=false;
    try {proto::Reader reader(payload.bytes.data(),payload.bytes.size());proto::readPlayerState(reader);} catch(...) {rejected=true;}check(rejected,"nonfinite pose rejected before touching Unity");
    rejected=false;try {validateSettings({{"port",47616}});} catch(...) {rejected=true;}check(rejected,"connection port cannot collide with discovery");
    rejected=false;try {validateSettings({{"saved_servers",nlohmann::json::array({"bad"})}});} catch(...) {rejected=true;}check(rejected,"malformed saved servers rejected");
    check(proto::quantize(2)==32767&&proto::quantize(-2)==-32767,"bone rotations are bounded");
    proto::PlayerState visual;visual.flags=std::uint8_t(proto::StateValid|proto::StateAlive|proto::StateWeaponValid|(5<<5));
    proto::Writer visualBytes;proto::write(visualBytes,visual);proto::Reader visualReader(visualBytes.bytes.data(),visualBytes.bytes.size());
    auto decoded=proto::readPlayerState(visualReader);
    check((decoded.flags&proto::StateValid)&&(decoded.flags&proto::StateAlive)&&(decoded.flags&proto::StateWeaponValid)&&proto::weaponState(decoded.flags)==5,
        "optional weapon view state preserves life flags and the existing pose layout");
    SetEnvironmentVariableW(L"EFLINK_PROFILE_DIR",L"D:\\EF Link\\tests\\isolated");
    check(settingsPath()==std::filesystem::path(L"D:\\EF Link\\tests\\isolated\\settings.json"),"isolated instance preferences use the explicit profile");
    SetEnvironmentVariableW(L"EFLINK_PROFILE_DIR",L"relative");rejected=false;
    try {settingsPath();} catch(...) {rejected=true;}check(rejected,"relative profile override rejected");
    SetEnvironmentVariableW(L"EFLINK_PROFILE_DIR",nullptr);
    return failures?1:0;
}
