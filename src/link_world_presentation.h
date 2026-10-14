    // Included inside World: native presentation adapters, never skill execution.
    struct PartySample {void* entity=nullptr;void* object=nullptr;NativeIdentity identity;
        std::vector<void*> transforms;std::vector<std::string> names;Json growth,appearance;
        std::uint64_t sampled=0,life=0;};
    std::map<int,PartySample> partySamples;std::uint64_t nextPartyLife=0,nextEffectLife=0;
    std::map<std::uint64_t,std::uint64_t> effectLives;
    Json sampledParty=Json::array(),sampledEffects=Json::array();
    Json nativeCombatReadback=Json::array(),visualReadback=Json::array();
    std::uint64_t nativePoiseWrites=0,nativeCombatSampleAt=0;
    std::string presentationError,partySamplingError,effectSamplingError;std::uint64_t presentationSampleAt=0,effectsCreated=0,effectsReleased=0,effectsExpired=0;
    #include "link_world_material_vfx.h"
    void* instanceObject(const char* name) {
        auto klass=api.klass("GameInstance","Gameplay.Beyond.dll","Beyond.Gameplay");
        auto* f=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,name);
        if(!f)throw std::runtime_error(std::string("GameInstance field unavailable: ")+name);
        void* value=nullptr;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(f,&value);return value;
    }
    void clearPresentation() {
        clearMaterials();
        partySamples.clear();sampledParty=Json::array();sampledEffects=Json::array();effectLives.clear();visualReadback=Json::array();nativeCombatReadback=Json::array();
    }
    Json readPoise(void* ability) {
        const auto value=api.value<float>(gm("AbilitySystem","get_poise","","System.Single"),ability);
        const auto max=api.value<float>(gm("AbilitySystem","get_maxPoise","","System.Single"),ability);
        Json p={{"value",value},{"max",max}};
        if(!validPoise(p))throw std::runtime_error("invalid native poise state");return p;
    }
    void sampleParty() {
        sampledParty=Json::array();std::set<int> present;
        for(const auto& [slot,entity]:nativeRealSquadMembers()) {
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
        nativeCombatReadback=Json::array(); // Common enemy/native result adapter is not verified yet.
    }
    void updatePresentation(Net& net,Session& link) {
        if(!link.localSceneOperational()||!link.ready()||!character||link.snapshot().value("map",std::string{})!=mapId||link.snapshot().value("level",std::string{})!=levelId) {if(!partySamples.empty()||!materialLives.empty())clearPresentation();return;}
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
