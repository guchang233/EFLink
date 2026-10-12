#pragma once
// Passive, local-only cache of official character/weapon relationships. Wire
// fields are from LR-S 936df33f and the project's existing corrected schema.
// Never injects a bag message, writes a CharBag, loads an asset or sends an RPC.
#include "link_official_view.h"
#include "../link_profile.h"
#include <map>
#include <limits>

namespace eflink::authority {
class OfficialAppearance {
    struct Character {std::string templateId;std::uint64_t weapon=0;};
    struct Weapon {int templateId=0;std::uint64_t character=0;};
    std::map<std::uint64_t,Character> characters;
    std::map<std::uint64_t,Weapon> weapons;
    static constexpr std::size_t maxEntries=4096;
    std::uint64_t observed=0,rejected=0;
    std::string error;
    using Fields=std::vector<view_wire::Field>;
    static std::uint64_t number(const Fields& fields,unsigned tag,std::uint64_t fallback=0) {
        auto value=fallback;
        for(const auto& f:fields)if(f.number==tag) {
            if(f.wire!=0)throw std::runtime_error("appearance scalar has wrong wire type");value=f.value;
        }
        return value;
    }
    static std::string_view message(const Fields& fields,unsigned tag) {
        std::string_view value;
        for(const auto& f:fields)if(f.number==tag) {
            if(f.wire!=2)throw std::runtime_error("appearance message has wrong wire type");value=f.payload;
        }
        return value;
    }
    static void readCharacter(std::string_view bytes,std::map<std::uint64_t,Character>& target) {
        const auto f=view_wire::fields(bytes);const auto id=number(f,1);
        const std::string name(message(f,2));
        if(!id||name.empty()||characterTemplate(name+"_postmodel.prefab")!=name)
            throw std::runtime_error("invalid official appearance character");
        if(!target.contains(id)&&target.size()>=maxEntries)throw std::runtime_error("appearance character limit");
        target[id]={name,number(f,12)};
    }
    static void readGrid(std::string_view bytes,std::map<std::uint64_t,Weapon>& target) {
        const auto grid=view_wire::fields(bytes);const auto raw=message(grid,4);if(raw.empty())return;
        const auto inst=view_wire::fields(raw);const auto id=number(inst,1);
        // ITEM_INST is a oneof: the last alternative wins, as in protobuf.
        std::string_view weapon;
        for(const auto& f:inst)if(f.number==2||f.number==3||f.number==4||f.number==31) {
            if(f.wire!=2)throw std::runtime_error("appearance item has wrong wire type");
            weapon=f.number==4?f.payload:std::string_view{};
        }
        if(weapon.empty()) {target.erase(id);return;}
        const auto w=view_wire::fields(weapon);const auto templateId=number(w,2);
        if(!id||number(w,1)!=id||!templateId||templateId>std::uint64_t(std::numeric_limits<int>::max()))
            throw std::runtime_error("invalid official appearance weapon");
        if(!target.contains(id)&&target.size()>=maxEntries)throw std::runtime_error("appearance weapon limit");
        target[id]={int(templateId),number(w,7)};
    }
public:
    static bool accepts(std::string_view type) {
        return type=="SC_SYNC_CHAR_BAG_INFO"||type=="SC_CHAR_BAG_ADD_CHAR"||type=="SC_ITEM_BAG_SCOPE_SYNC"||
            type=="SC_ITEM_BAG_SCOPE_MODIFY"||type=="SC_WEAPON_PUTON";
    }
    void clear() {characters.clear();weapons.clear();error.clear();}
    void reject(const std::string& reason) {clear();++rejected;error=reason;}
    bool observe(std::string_view type,std::string_view bytes) {
        if(!accepts(type))return false;
        try {
            if(bytes.size()>1024u*1024)throw std::runtime_error("appearance message limit");
            const auto f=view_wire::fields(bytes);
            if(type=="SC_SYNC_CHAR_BAG_INFO"||type=="SC_CHAR_BAG_ADD_CHAR") {
                const auto scope=number(f,type=="SC_SYNC_CHAR_BAG_INFO"?10u:2u);
                if(scope!=1)return false; // only the ordinary account scope
                auto next=type=="SC_SYNC_CHAR_BAG_INFO"?std::map<std::uint64_t,Character>{}:characters;
                if(type=="SC_SYNC_CHAR_BAG_INFO"&&number(f,11))next.clear();
                else for(const auto& c:f)if(c.number==1) {
                    if(c.wire!=2)throw std::runtime_error("appearance character list wire type");
                    readCharacter(c.payload,next);
                }
                characters=std::move(next);
            } else if(type=="SC_ITEM_BAG_SCOPE_SYNC"||type=="SC_ITEM_BAG_SCOPE_MODIFY") {
                if(number(f,11)!=1)return false;
                auto next=type=="SC_ITEM_BAG_SCOPE_SYNC"?std::map<std::uint64_t,Weapon>{}:weapons;
                if(type=="SC_ITEM_BAG_SCOPE_MODIFY"&&number(f,14))next.clear();
                else for(const auto& entry:f)if(entry.number==3) {
                    if(entry.wire!=2)throw std::runtime_error("appearance depot wire type");
                    const auto e=view_wire::fields(entry.payload);if(number(e,1)!=1)continue;
                    const auto depot=view_wire::fields(message(e,2));
                    for(const auto& grid:depot) {
                        if(grid.number==2) {
                            if(grid.wire!=2)throw std::runtime_error("appearance grid wire type");
                            readGrid(grid.payload,next);
                        } else if(grid.number==3&&type=="SC_ITEM_BAG_SCOPE_MODIFY") {
                            if(grid.wire==0)next.erase(grid.value);
                            else if(grid.wire==2) {
                                std::size_t at=0;while(at<grid.payload.size())next.erase(view_wire::varint(grid.payload,at));
                            } else throw std::runtime_error("appearance deletion wire type");
                        }
                    }
                }
                weapons=std::move(next);
            } else {
                const auto who=number(f,1),weapon=number(f,2),off=number(f,3),displaced=number(f,4);
                if(!who)throw std::runtime_error("appearance equip character missing");
                if(auto c=characters.find(who);c!=characters.end())c->second.weapon=weapon;
                if(auto w=weapons.find(off);w!=weapons.end())w->second.character=0;
                if(auto w=weapons.find(weapon);w!=weapons.end())w->second.character=who;
                // The displaced player's replacement cannot be inferred from this
                // message alone. Wait for complete character data rather than guess.
                if(displaced&&displaced!=who)characters.erase(displaced);
            }
            ++observed;error.clear();return true;
        }catch(const std::exception& e) {
            // Do not publish an old weapon after a relevant update failed to parse.
            reject(e.what());return false;
        }
    }
    nlohmann::json profile(std::uint64_t localCharacter,const std::string& model) const {
        auto c=characters.find(localCharacter);
        if(c==characters.end()||c->second.templateId!=characterTemplate(model))return nullptr;
        int templateId=0;
        if(c->second.weapon) {
            auto w=weapons.find(c->second.weapon);
            if(w==weapons.end()||w->second.character!=localCharacter)return nullptr;
            templateId=w->second.templateId;
        }
        return {{"template",c->second.templateId},{"weapon_template",templateId}};
    }
    nlohmann::json status() const {
        return {{"characters",characters.size()},{"weapons",weapons.size()},{"messages",observed},
            {"rejected",rejected},{"error",error},{"source","official_downlink"},{"weapon_applied",false},{"effects_replayed",false}};
    }
};
}
