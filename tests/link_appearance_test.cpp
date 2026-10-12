#include "authority/link_official_appearance.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace eflink;
using namespace eflink::authority;
int failures=0;
void check(bool condition,const char* name) {std::printf("%s %s\n",condition?"[ok]":"[FAIL]",name);if(!condition)++failures;}
std::string num(unsigned tag,std::uint64_t value) {
    std::string out;view_wire::writeVarint(out,std::uint64_t(tag)<<3);view_wire::writeVarint(out,value);return out;
}
std::string msg(unsigned tag,const std::string& bytes) {std::string out;view_wire::message(out,tag,bytes);return out;}
std::string character(std::uint64_t id,std::uint64_t weapon,const char* name="chr_test") {
    return num(1,id)+msg(2,name)+num(12,weapon);
}
std::string charBag(const std::string& raw,unsigned scope=1) {return msg(1,raw)+num(10,scope);}
std::string grid(std::uint64_t id,int templateId,std::uint64_t characterId) {
    return msg(4,num(1,id)+msg(4,num(1,id)+num(2,std::uint64_t(templateId))+num(7,characterId)));
}
std::string itemBag(const std::string& grids,unsigned scope=1,unsigned depot=1) {
    return msg(3,num(1,depot)+msg(2,grids))+num(11,scope);
}
const std::string model="Assets/chr_test_postmodel.prefab";
int main(int argc,char** argv) try {
    const nlohmann::json profile={{"template","chr_test"},{"weapon_template",8002}};
    check(validCharacterAppearance(profile,model),"bounded public weapon template matches the operator");
    for(const char* forbidden:{"inst_id","weapon_id","equip_char_id","hp","model_path"}) {
        auto invalid=profile;invalid[forbidden]=123;
        check(!validCharacterAppearance(invalid,model),"private IDs, claimed stats and arbitrary asset paths are rejected");
    }
    auto bad=profile;bad["weapon_template"]=-1;check(!validCharacterAppearance(bad,model),"negative weapon template rejected");
    bad["weapon_template"]=1.5;check(!validCharacterAppearance(bad,model),"fractional weapon template rejected");
    bad["weapon_template"]=std::uint64_t(-1);check(!validCharacterAppearance(bad,model),"overflowing unsigned weapon template rejected");
    check(!validCharacterAppearance(profile,"Assets/chr_other_postmodel.prefab"),"appearance cannot be applied to another operator");

    OfficialAppearance cache;
    check(cache.observe("SC_SYNC_CHAR_BAG_INFO",charBag(character(1,2))),"official character metadata is observed");
    check(cache.profile(1,model).is_null(),"missing weapon data stays unknown instead of inventing a weapon");
    check(cache.observe("SC_ITEM_BAG_SCOPE_SYNC",itemBag(msg(2,grid(2,8002,1))))&&cache.profile(1,model)==profile,
        "character and weapon use different instance IDs and resolve to a public template");
    check(cache.profile(2,model).is_null()&&cache.profile(1,"Assets/chr_other_postmodel.prefab").is_null(),
        "unknown identities and operator changes cannot reuse a cached weapon");
    const auto publicText=cache.profile(1,model).dump();
    check(publicText.find("inst_id")==std::string::npos&&publicText.find("weapon_id")==std::string::npos,
        "account character and weapon instance IDs do not enter the public profile");
    check(!cache.observe("SC_SYNC_CHAR_BAG_INFO",charBag(character(1,999),2))&&cache.profile(1,model)==profile,
        "temporary world scope cannot overwrite the account appearance");
    check(cache.observe("SC_ITEM_BAG_SCOPE_SYNC",itemBag(msg(2,grid(2,8002,9))))&&cache.profile(1,model).is_null(),
        "a weapon assigned to another character remains unresolved");
    cache.observe("SC_ITEM_BAG_SCOPE_SYNC",itemBag(msg(2,grid(2,8002,1))+msg(2,grid(3,8003,0))));
    check(cache.observe("SC_WEAPON_PUTON",num(1,1)+num(2,3)+num(3,2))&&cache.profile(1,model)["weapon_template"]==8003,
        "official weapon changes update both ends of the relationship");
    check(cache.observe("SC_WEAPON_PUTON",num(1,1)+num(2,0)+num(3,3))&&cache.profile(1,model)["weapon_template"]==0,
        "an observed unequipped state is distinct from unknown metadata");
    cache.observe("SC_WEAPON_PUTON",num(1,1)+num(2,3));
    std::string packed;view_wire::writeVarint(packed,3);
    check(cache.observe("SC_ITEM_BAG_SCOPE_MODIFY",itemBag(msg(3,packed)))&&cache.profile(1,model).is_null(),
        "packed weapon deletion removes stale appearance");
    cache.observe("SC_ITEM_BAG_SCOPE_MODIFY",itemBag(msg(2,grid(3,8003,1))));
    check(cache.profile(1,model)["weapon_template"]==8003,"incremental weapon additions restore a complete relation");
    cache.observe("SC_ITEM_BAG_SCOPE_MODIFY",itemBag(num(3,3)));
    check(cache.profile(1,model).is_null(),"unpacked deletion is supported too");
    cache.observe("SC_ITEM_BAG_SCOPE_SYNC",itemBag(msg(2,grid(3,8003,1))));
    const std::string invalid="\x0a\x05\x01";
    check(!cache.observe("SC_SYNC_CHAR_BAG_INFO",invalid)&&cache.profile(1,model).is_null()&&cache.status()["rejected"]==1,
        "malformed relevant updates invalidate old data and expose an error");
    cache.observe("SC_SYNC_CHAR_BAG_INFO",charBag(character(1,2)));
    cache.observe("SC_ITEM_BAG_SCOPE_SYNC",itemBag(msg(2,grid(2,8002,1))));
    check(cache.status()["error"]==""&&cache.profile(1,model)==profile,"fresh complete official data recovers after a parse failure");
    const auto emptyItem=msg(4,num(1,2)+msg(31,""));
    cache.observe("SC_ITEM_BAG_SCOPE_MODIFY",itemBag(msg(2,emptyItem)));
    check(cache.profile(1,model).is_null(),"a non-weapon replacement cannot preserve the old weapon instance");
    cache.observe("SC_ITEM_BAG_SCOPE_SYNC",itemBag(msg(2,grid(2,8002,1))));
    check(!cache.observe("SC_ITEM_BAG_SCOPE_MODIFY",itemBag(msg(2,msg(4,num(1,2)+msg(4,num(1,9)+num(2,8002)+num(7,1))))))&&
        cache.profile(1,model).is_null(),"mismatched item and weapon instance IDs invalidate the cache");
    cache.observe("SC_SYNC_CHAR_BAG_INFO",charBag(character(1,2)));
    cache.observe("SC_ITEM_BAG_SCOPE_SYNC",itemBag(msg(2,grid(2,8002,1))));
    cache.observe("SC_ITEM_BAG_SCOPE_MODIFY",num(11,1)+num(14,1));
    check(cache.profile(1,model).is_null(),"deleting the account item scope clears its weapon relationships");
    cache.observe("SC_ITEM_BAG_SCOPE_SYNC",itemBag(msg(2,grid(2,8002,1))));
    cache.observe("SC_SYNC_CHAR_BAG_INFO",num(10,1)+num(11,1));
    check(cache.profile(1,model).is_null(),"deleting the account character scope clears its character relationships");
    cache.observe("SC_CHAR_BAG_ADD_CHAR",msg(1,character(1,2))+num(2,1));
    check(cache.profile(1,model)==profile,"an incremental character update restores a complete relation");
    cache.observe("SC_CHAR_BAG_ADD_CHAR",msg(1,character(4,2))+num(2,1));
    cache.observe("SC_WEAPON_PUTON",num(1,4)+num(2,2)+num(4,1));
    check(cache.profile(1,model).is_null()&&cache.profile(4,model)==profile,"displaced characters stay unknown until their replacement is observed");
    cache.reject("offline serialization failure");
    check(cache.profile(4,model).is_null()&&cache.status()["error"]=="offline serialization failure",
        "serialization failure also clears stale metadata and exposes a diagnostic");
    cache.clear();check(cache.profile(1,model).is_null(),"login and shutdown clear account-specific relationships");
    check(!cache.observe("SC_OBJECT_ENTER_VIEW",msg(1,"x")),"scene messages are not treated as account equipment data");

    // Optional fixtures emitted by the unmodified LR-S production producers.
    if(argc==3) {
        auto read=[](const char* path) {std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("fixture missing");return std::string(std::istreambuf_iterator<char>(file),{});};
        const auto chars=read(argv[1]),items=read(argv[2]);
        check(cache.observe("SC_SYNC_CHAR_BAG_INFO",chars)&&cache.observe("SC_ITEM_BAG_SCOPE_SYNC",items),"actual LR-S-produced offline wire fixtures parse");
        check(cache.profile(1,"Assets/chr_9001_probe_a_postmodel.prefab")["weapon_template"]==8002&&
            cache.profile(2,"Assets/chr_9002_probe_b_postmodel.prefab")["weapon_template"]==8001,
            "actual LR-S crossed role/weapon IDs resolve in the production C++ cache");
    }
    return failures?1:0;
}catch(const std::exception& e) {std::printf("[FAIL] unexpected exception: %s\n",e.what());return 1;}
