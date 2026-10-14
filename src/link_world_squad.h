    // Native official squad enumeration. Current squad can temporarily be a
    // single-character mining/cinematic squad; it is not the complete party.
    Json nativeSquadSource=Json::object();
    std::vector<std::pair<int,void*>> nativeRealSquadMembers() {
        auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
        auto* player=instance?api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"),instance):nullptr;
        auto* manager=player?readField<void*>(player,"GamePlayer","squadManager","Beyond.Gameplay"):nullptr;
        if(!manager)throw std::runtime_error("native squad manager unavailable");
        auto* real=api.call(gm("SquadManager","get_curRealSquad","","Beyond.Gameplay.Core.SquadManager.ServerSquad"),manager);
        auto* current=api.call(gm("SquadManager","get_curSquad","","Beyond.Gameplay.Core.SquadManager.Squad"),manager);
        if(!real||api.value<bool>(gm("SquadManager/Squad","get_isReleased","","System.Boolean"),real))
            throw std::runtime_error("native real squad unavailable or released");
        eflink::unity::Api::TemporaryRoot squadRoot(runtime,real);
        auto* slots=readField<void*>(real,"SquadManager/Squad","slots");
        if(!slots)throw std::runtime_error("native real squad slots unavailable");
        eflink::unity::Api::TemporaryRoot slotsRoot(runtime,slots);
        auto* boxed=collection(slots,"get_Count");
        auto* countValue=boxed?runtime->object_unbox(runtime->context,boxed):nullptr;
        if(!countValue)throw std::runtime_error("native real squad count unavailable");
        int count=0;std::memcpy(&count,countValue,sizeof count);
        if(count<0||count>4)throw std::runtime_error("unsupported native real squad size");
        std::vector<std::pair<int,void*>> result;std::set<void*> seen;
        for(int slot=0;slot<count;++slot) {
            auto* entity=api.call(gm("SquadManager/Squad","GetMemberBySlot","System.Int32","Beyond.Gameplay.Core.Entity"),real,{&slot});
            if(!entity)continue;
            if(!seen.insert(entity).second)throw std::runtime_error("native real squad aliases a character across slots");
            result.emplace_back(slot,entity);
        }
        const auto currentCount=api.value<int>(gm("SquadManager","get_slotCount","","System.Int32"),manager);
        if(currentCount<0||currentCount>4)throw std::runtime_error("unsupported native current squad size");
        nativeSquadSource={{"real_slots",count},{"real_entities",result.size()},{"current_slots",currentCount},
            {"current_is_real",current==real},{"source","native_server_squad"},{"read_only",true}};
        return result;
    }
