    // Included inside World: native presentation adapters, never skill execution.
    struct PartySample {void* entity=nullptr;void* object=nullptr;NativeIdentity identity;
        std::vector<void*> transforms;std::vector<std::string> names;Json growth,appearance;
        std::uint64_t sampled=0,life=0;};
    std::map<int,PartySample> partySamples;std::uint64_t nextPartyLife=0,nextEffectLife=0;
    std::map<std::uint64_t,std::uint64_t> effectLives;
    std::map<std::uint16_t,Json> extraMembers;
    std::map<std::uint16_t,std::deque<TimedState>> extraPoses;
    struct VisualEffect {void* effect=nullptr;std::uint32_t root=0,uid=0;bool loaded=false,expired=false;std::string name;};
    std::map<std::pair<std::uint16_t,std::uint64_t>,VisualEffect> visualEffects;
    Json sampledParty=Json::array(),sampledEffects=Json::array();
    Json nativeCombatReadback=Json::array(),visualReadback=Json::array();
    std::uint64_t nativePoiseWrites=0,nativeCombatSampleAt=0;
    std::string presentationError,partySamplingError,effectSamplingError;std::uint64_t presentationSampleAt=0,effectsCreated=0,effectsReleased=0,effectsExpired=0;
    #include "link_world_material_vfx.h"
    bool currentVisual(const VisualEffect& e) {
        return e.effect&&api.value<std::uint32_t>(gm("EffectInstance","get_instanceUid","","System.UInt32","Beyond.Gameplay"),e.effect)==e.uid&&
            !api.value<bool>(gm("EffectInstance","get_inPool","","System.Boolean","Beyond.Gameplay"),e.effect)&&
            !api.value<bool>(gm("EffectInstance","get_recycled","","System.Boolean","Beyond.Gameplay"),e.effect);
    }
    void* instanceObject(const char* name) {
        auto klass=api.klass("GameInstance","Gameplay.Beyond.dll","Beyond.Gameplay");
        auto* f=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,name);
        if(!f)throw std::runtime_error(std::string("GameInstance field unavailable: ")+name);
        void* value=nullptr;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(f,&value);return value;
    }
    void finishVisual(VisualEffect& e) {
        try {if(currentVisual(e)) {
            bool force=true,fade=false;api.call(gm("EffectInstance","Finish","System.Boolean|System.Boolean","System.Void","Beyond.Gameplay"),e.effect,{&force,&fade});
        }}catch(...) {}
        release(e.root);e.effect=nullptr;++effectsReleased;
    }
    void clearPresentation() {
        clearMaterials();
        for(auto& [key,e]:visualEffects)finishVisual(e);visualEffects.clear();
        partySamples.clear();extraMembers.clear();extraPoses.clear();sampledParty=Json::array();sampledEffects=Json::array();effectLives.clear();visualReadback=Json::array();nativeCombatReadback=Json::array();
    }
    Json readPoise(void* ability) {
        const auto value=api.value<float>(gm("AbilitySystem","get_poise","","System.Single"),ability);
        const auto max=api.value<float>(gm("AbilitySystem","get_maxPoise","","System.Single"),ability);
        Json p={{"value",value},{"max",max}};
        if(!validPoise(p))throw std::runtime_error("invalid native poise state");return p;
    }
    void sampleParty() {
        sampledParty=Json::array();std::set<int> present;
        auto* player=api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"));
        auto* squad=player?readField<void*>(player,"GamePlayer","squadManager","Beyond.Gameplay"):nullptr;
        if(!squad)return;
        const int count=api.value<int>(gm("SquadManager","get_slotCount","","System.Int32"),squad);
        if(count<0||count>4)throw std::runtime_error("unsupported native squad size");
        for(int slot=0;slot<count;++slot) {
            auto* entity=api.call(gm("SquadManager","GetMemberBySlot","System.Int32","Beyond.Gameplay.Core.Entity"),squad,{&slot});
            if(!usable(entity)||!api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),entity))continue;
            auto* component=api.call(entityModel,entity);auto* object=component?api.call(modelGo,component):nullptr;
            if(!object||!alive(object))continue;
            present.insert(slot);auto& cached=partySamples[slot];const auto identity=identityOf(entity);
            if(cached.entity!=entity||!cached.identity.matches(identity)||cached.object!=object) {
                cached={};cached.entity=entity;cached.identity=identity;cached.object=object;cached.life=++nextPartyLife;
                for(auto* transform:components(object,"Transform")) {
                    auto name=text(api.call(objectName,transform));
                    if(name.rfind("Bip001",0)==0&&cached.transforms.size()<proto::kMaxBones) {cached.names.push_back(name);cached.transforms.push_back(transform);}
                }
            }
            const auto path=text(api.call(modelPath,component));
            if(nowMs()-cached.sampled>=500||cached.growth.is_null()) {
                auto* root=api.call(gm("Entity","get_rootCom","","Beyond.Gameplay.Core.RootComponent"),entity);
                auto* info=root?api.call(gm("CharacterRootComponent","get_replicatedCharInfo","","Beyond.Gameplay.CharInfo"),root):nullptr;
                cached.growth=infoGrowth(info,path);cached.appearance=nullptr;
                if(info) {
                    auto expected=readField<std::uint64_t>(info,"CharInfo","weaponInstId","Beyond.Gameplay");int number=0;
                    if(expected) {auto* weapon=collection(info,"GetWeaponInstData");
                        if(weapon)number=itemNumber(text(readField<void*>(weapon,"InventorySystem/ItemInstData","templateId","Beyond.Gameplay")));}
                    cached.appearance={{"template",cached.growth.value("template",std::string{})},{"weapon_template",number}};
                }
                cached.sampled=nowMs();
            }
            if(cached.growth.is_null())continue;
            auto* transform=api.call(getTransform,object);auto p=api.value<V3>(getPosition,transform);auto q=api.value<Q>(getRotation,transform);
            Json rotations=Json::array();V3 root{};
            for(std::size_t i=0;i<cached.transforms.size();++i) {
                auto b=api.value<Q>(getLocalRotation,cached.transforms[i]);for(float v:{b.x,b.y,b.z,b.w})rotations.push_back(proto::quantize(v));
                if(!i)root=api.value<V3>(getLocalPosition,cached.transforms[i]);
            }
            unsigned flags=proto::StateValid;if(api.value<bool>(entityAlive,entity))flags|=proto::StateAlive;
            if(auto* weapon=api.call(gm("Entity","get_weaponCom","","Beyond.Gameplay.View.WeaponComponent"),entity)) {
                const int state=api.value<int>(gm("WeaponComponent","get_weaponState","","Beyond.Gameplay.View.WeaponComponent.WeaponState","Beyond.Gameplay.View"),weapon);
                if(state>=0&&state<=5)flags|=unsigned(proto::StateWeaponValid|(state<<5));
            }
            bool hidden=api.value<bool>(gm("Entity","get_isHide","","System.Boolean"),entity);
            if(hidden)flags&=~proto::StateValid;
            sampledParty.push_back({{"slot",slot},{"life",cached.life},
                {"controlled",entity==character},{"model",path},{"position",{p.x,p.y,p.z}},{"rotation",{q.x,q.y,q.z,q.w}},
                {"root",{root.x,root.y,root.z}},{"names",cached.names},{"bones",rotations},{"flags",flags},
                {"growth",cached.growth},{"appearance",cached.appearance}});
        }
        for(auto it=partySamples.begin();it!=partySamples.end();)if(!present.contains(it->first))it=partySamples.erase(it);else ++it;
    }
    void sampleEffects() {
        sampledEffects=Json::array();std::set<std::uint64_t> present;auto* manager=instanceObject("effectManager");if(!manager)return;
        auto* active=readField<void*>(manager,"EffectManager","m_activeEffects","Beyond.Gameplay");if(!active)return;
        const auto count=symbol<std::uintptr_t(*)(void*)>("il2cpp_array_length")(active);if(count>256)throw std::runtime_error("unsupported effect categories");
        auto** categories=reinterpret_cast<void**>(static_cast<unsigned char*>(active)+32);
        const auto normal=api.klass("BattleNormalEffect","Gameplay.Beyond.dll","Beyond.Gameplay").class_info;
        for(std::uintptr_t c=0;c<count;++c)if(categories[c]) {
            auto* list=readField<void*>(categories[c],"EffectData","data","Beyond.Gameplay");if(!list)continue;
            auto* boxed=collection(list,"get_Count");int size=0;if(boxed)std::memcpy(&size,runtime->object_unbox(runtime->context,boxed),sizeof size);
            if(size<0||size>4096)throw std::runtime_error("invalid effect list size");
            for(int i=0;i<size&&sampledEffects.size()<96;++i) {
                auto* effect=collection(list,"get_Item",{&i});if(!effect)continue;
                auto klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(effect);
                bool owned=false;
                if(symbol<bool(*)(const void*,const void*)>("il2cpp_class_is_assignable_from")(normal,klass)) {
                    auto* source=api.call(gm("BattleNormalEffect","get_source","","Beyond.Gameplay.Core.Entity","Beyond.Gameplay"),effect);
                    auto* targetAbility=api.call(gm("BattleNormalEffect","get_target","","Beyond.Gameplay.Core.AbilitySystem","Beyond.Gameplay"),effect);
                    auto* target=targetAbility?api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),targetAbility):nullptr;
                    for(const auto& [slot,p]:partySamples)if((p.entity==source||p.entity==target)&&currentEntity(p.entity,p.identity))owned=true;
                    auto* part=readField<void*>(effect,"BattleNormalEffect","m_weaponModelViewPart","Beyond.Gameplay");
                    auto* helper=part?readField<void*>(part,"ComplexModelViewPart","m_entityRenderHelper","Beyond.Gameplay.View"):nullptr;
                    auto* entity=helper?readField<void*>(helper,"EntityRenderHelper","m_entity","Beyond.Gameplay.View"):nullptr;
                    for(const auto& [slot,p]:partySamples)if(p.entity==entity&&currentEntity(entity,p.identity))owned=true;
                }
                if(!owned) {
                    auto* follow=readField<void*>(effect,"EffectInstance","m_followTarget","Beyond.Gameplay");
                    auto* parent=readField<void*>(effect,"EffectInstance","m_effectParent","Beyond.Gameplay");
                    for(const auto& [slot,p]:partySamples)if(currentEntity(p.entity,p.identity)&&alive(p.object)) {
                        auto* root=api.call(getTransform,p.object);
                        for(auto* candidate:{follow,parent})if(candidate&&alive(candidate)&&api.value<bool>(m("Transform","IsChildOf","UnityEngine.Transform","System.Boolean"),candidate,{root}))owned=true;
                    }
                }
                if(!owned)continue;
                auto* go=api.call(gm("EffectInstance","get_effectObj","","UnityEngine.GameObject","Beyond.Gameplay"),effect);
                if(!go||!alive(go)||!api.value<bool>(m("GameObject","get_activeInHierarchy","","System.Boolean"),go))continue;
                auto* t=api.call(getTransform,go);auto p=api.value<V3>(getPosition,t);auto q=api.value<Q>(getRotation,t);
                auto scale=api.value<V3>(m("Transform","get_lossyScale","","UnityEngine.Vector3"),t);
                auto name=text(api.call(gm("EffectInstance","get_effectName","","System.String","Beyond.Gameplay"),effect));
                auto id=api.value<int>(gm("EffectInstance","get_instanceId","","System.Int32","Beyond.Gameplay"),effect);
                auto uid=api.value<std::uint32_t>(gm("EffectInstance","get_instanceUid","","System.UInt32","Beyond.Gameplay"),effect);
                if(!presentationResource(name)||!uid)continue;
                const auto native=(std::uint64_t(uid)<<32)|std::uint32_t(id);present.insert(native);
                auto& publicId=effectLives[native];if(!publicId)publicId=++nextEffectLife;
                sampledEffects.push_back({{"id",publicId},{"name",name},
                    {"position",{p.x,p.y,p.z}},{"rotation",{q.x,q.y,q.z,q.w}},{"scale",{scale.x,scale.y,scale.z}},
                    {"time",api.value<float>(gm("EffectInstance","get_runningTime","","System.Single","Beyond.Gameplay"),effect)}});
            }
        }
        for(auto it=effectLives.begin();it!=effectLives.end();)if(!present.contains(it->first))it=effectLives.erase(it);else ++it;
    }
    void sampleNativeCombat() {
        if(nowMs()-nativeCombatSampleAt<100)return;nativeCombatSampleAt=nowMs();nativeCombatReadback=Json::array();
        for(const auto& [key,e]:enemies)if(currentEnemy(e)&&nativeCombatReadback.size()<64) {
            auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),e.entity);if(!ability)continue;
            const auto expected=session->entityReference(key);if(expected.is_null())continue;
            Json row={{"key",key},{"life",expected.at("life")},{"poise",readPoise(ability)},
                {"hp",api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability)}};
            for(const auto& state:session->snapshot().at("entities"))if(state.at("key")==key) {
                row["expected_poise"]=state.value("poise",Json{});row["expected_hp"]=state.at("hp");break;
            }
            nativeCombatReadback.push_back(std::move(row));
        }
    }
    Snapshot presentationView(Snapshot view,Session& link) {
        std::set<std::uint16_t> present;
        const auto peers=view.peers;
        for(const auto& peer:peers)if(!peer.local) {
            auto frame=link.presentation(peer.id);if(frame.is_null())continue;
            for(const auto& member:frame.at("members")) {
                if(member.at("controlled").get<bool>())continue;
                // Internal render keys cannot be confused with network peer IDs.
                const auto key=std::uint16_t(0x8000+peer.id*4+member.at("slot").get<unsigned>());
                present.insert(key);auto old=extraMembers.find(key);
                if(old==extraMembers.end()||old->second.at("life")!=member.at("life")||old->second.at("model")!=member.at("model"))extraPoses[key].clear();
                extraMembers[key]=member;
                proto::PlayerState state;state.id=key;state.timeMs=std::uint32_t(frame.at("sample_time").get<std::uint64_t>());
                state.mapHash=proto::fnv32(frame.at("map").get<std::string>());state.flags=member.at("flags").get<std::uint8_t>();
                state.skeleton=std::uint16_t(proto::fnv32(member.at("model").get<std::string>()));
                for(int i=0;i<3;++i){state.position[i]=member.at("position")[i].get<float>();state.rootOffset[i]=member.at("root")[i].get<float>();}
                for(int i=0;i<4;++i)state.rotation[i]=member.at("rotation")[i].get<float>();
                state.bones=member.at("bones").get<std::vector<std::int16_t>>();
                auto& poses=extraPoses[key];auto received=frame.at("received").get<std::uint64_t>();
                if(poses.empty()||poses.back().receivedMs!=received){poses.push_back({state,received});while(poses.size()>12)poses.pop_front();}
                PeerView remote=peer;remote.id=key;remote.info.modelPath=member.at("model").get<std::string>();
                remote.info.skeleton=state.skeleton;remote.info.bones=member.at("names").get<std::vector<std::string>>();remote.states=poses;
                view.peers.push_back(std::move(remote));
            }
        }
        for(auto it=extraMembers.begin();it!=extraMembers.end();)if(!present.contains(it->first)){extraPoses.erase(it->first);it=extraMembers.erase(it);}else ++it;
        return view;
    }
    void replayEffects(const Snapshot& view,Session& link) {
        visualReadback=Json::array();std::set<std::pair<std::uint16_t,std::uint64_t>> present;auto* manager=instanceObject("effectManager");if(!manager)return;
        for(const auto& peer:view.peers)if(!peer.local) {
            auto frame=link.presentation(peer.id);if(frame.is_null())continue;
            for(const auto& state:frame.at("effects")) {
                auto key=std::make_pair(peer.id,state.at("id").get<std::uint64_t>());present.insert(key);
                auto& visual=visualEffects[key];auto p=state.at("position");V3 position{p[0].get<float>(),p[1].get<float>(),p[2].get<float>()};
                auto r=state.at("rotation");Q rotation{r[0].get<float>(),r[1].get<float>(),r[2].get<float>(),r[3].get<float>()};
                auto s=state.at("scale");V3 scale{s[0].get<float>(),s[1].get<float>(),s[2].get<float>()};
                if(visual.effect&&!currentVisual(visual)) {
                    release(visual.root);visual.effect=nullptr;visual.expired=true;visual.loaded=false;++effectsExpired;
                }
                if(visual.expired) {visualReadback.push_back({{"peer",peer.id},{"instance",key.second},{"expired",true},{"loaded",false}});continue;}
                if(!visual.effect) {
                    auto* boxed=api.call(gm("EffectManager","CreateStationaryEffect","System.String|UnityEngine.Vector3|UnityEngine.Quaternion|UnityEngine.Vector3","Beyond.ObjectPtr<Beyond.Gameplay.EffectInstance>","Beyond.Gameplay"),manager,{api.string(state.at("name").get<std::string>()),&position,&rotation,&scale});
                    if(!boxed)throw std::runtime_error("native visual effect creation returned no handle");
                    eflink::unity::Api::TemporaryRoot retain(runtime,boxed);
                    auto klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(boxed);
                    auto* f=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass,"obj");
                    if(!f)throw std::runtime_error("native visual effect handle layout unavailable");
                    void* effect=nullptr;symbol<void(*)(void*,const void*,void*)>("il2cpp_field_get_value")(boxed,f,&effect);
                    if(!effect)throw std::runtime_error("native visual effect handle is empty");
                    visual.effect=effect;visual.root=rootOf(effect);visual.name=state.at("name").get<std::string>();
                    float keepAlive=.5f;api.call(gm("EffectInstance","set_remainTime","System.Single","System.Void","Beyond.Gameplay"),effect,{&keepAlive});
                    visual.uid=api.value<std::uint32_t>(gm("EffectInstance","get_instanceUid","","System.UInt32","Beyond.Gameplay"),effect);++effectsCreated;
                }
                float keepAlive=.5f;api.call(gm("EffectInstance","set_remainTime","System.Single","System.Void","Beyond.Gameplay"),visual.effect,{&keepAlive});
                auto* go=api.call(gm("EffectInstance","get_effectObj","","UnityEngine.GameObject","Beyond.Gameplay"),visual.effect);
                visual.loaded=go&&alive(go);
                if(visual.loaded) {auto* t=api.call(getTransform,go);api.call(setPosition,t,{&position});api.call(setRotation,t,{&rotation});
                    api.call(m("Transform","set_localScale","UnityEngine.Vector3","System.Void"),t,{&scale});}
                visualReadback.push_back({{"peer",peer.id},{"instance",key.second},{"name",visual.name},{"loaded",visual.loaded},
                    {"active",visual.loaded&&api.value<bool>(m("GameObject","get_activeInHierarchy","","System.Boolean"),go)}});
            }
        }
        for(auto it=visualEffects.begin();it!=visualEffects.end();)if(!present.contains(it->first)){finishVisual(it->second);it=visualEffects.erase(it);}else ++it;
    }
    void updatePresentation(Net& net,Session& link) {
        if(!link.localSceneOperational()||!link.ready()||!character||link.snapshot().value("map",std::string{})!=mapId||link.snapshot().value("level",std::string{})!=levelId) {if(!partySamples.empty()||!visualEffects.empty()||!materialVisuals.empty())clearPresentation();return;}
        try {
            if(nowMs()-presentationSampleAt>=50) {
                presentationSampleAt=nowMs();
                try {sampleParty();partySamplingError.clear();}catch(const std::exception& e){sampledParty=Json::array();partySamplingError=e.what();}
                try {sampleEffects();effectSamplingError.clear();}catch(const std::exception& e){sampledEffects=Json::array();effectSamplingError=e.what();}
                try {sampleMaterials();materialError.clear();}catch(const std::exception& e){sampledMaterials=Json::array();materialError=e.what();}
                link.publishPresentation(net,{{"material_vfx",sampledMaterials},{"members",sampledParty},{"effects",sampledEffects},{"sample_time",nowMs()}});
            }
            // Read-only evidence while native display/apply paths are uncertified.
            sampleNativeCombat();presentationError.clear();
        }catch(const std::exception& e) {presentationError=e.what();}
    }
