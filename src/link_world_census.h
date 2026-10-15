    // Included inside World. Real host Entity/AbilitySystem facts use the same
    // binding allocator and ledger as minerals. Census never executes inputs or
    // supplies an operation witness, ownership grant or gameplay certificate.
    struct CensusStatus {std::uint64_t scans=0,observed=0;std::size_t objects=0;std::string error;};
    std::map<std::string,CensusStatus> actorCensusStatus;
    void clearNativeCensus() {
        nativeObjectBindings.retireKind("player");nativeObjectBindings.retireKind("enemy");
        actorCensusStatus.clear();nativeSquadSource=Json::object();
    }
    worldauthority::Observation readNativeBattle(void* entity,const std::string& kind,const std::string& key,int partySlot=-1) {
        if(!usable(entity)||!api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),entity))
            throw std::runtime_error("native battle entity is not initialized");
        eflink::unity::Api::TemporaryRoot entityRoot(runtime,entity);
        auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),entity);
        if(!ability)throw std::runtime_error("native battle ability state is unavailable");
        std::string templateId;Json appearance=Json::object();
        if(kind=="player") {
            // SquadManager allocates characters directly; a real character can
            // have no EntityManager BaseEntityData node. Read its actual native
            // root's replicated metadata, without synthesizing a CharInfo or
            // looking up another account/character by a transmitted identifier.
            auto* root=api.call(gm("Entity","get_rootCom","","Beyond.Gameplay.Core.RootComponent"),entity);
            if(!root||classOf(root)!="Beyond.Gameplay.Core.CharacterRootComponent")
                throw std::runtime_error("native player root is unadapted");
            auto* info=api.call(gm("CharacterRootComponent","get_replicatedCharInfo","","Beyond.Gameplay.CharInfo"),root);
            if(!info)throw std::runtime_error("native player metadata is unavailable");
            eflink::unity::Api::TemporaryRoot infoRoot(runtime,info);
            templateId=text(readField<void*>(info,"CharInfo","templateId","Beyond.Gameplay"));
            if(partySlot<0||partySlot>3)throw std::runtime_error("native player party slot unavailable");
            appearance["party_slot"]=partySlot;
            appearance["controlled"]=api.call(mainCharacter)==entity;
            const auto weaponId=readField<std::uint64_t>(info,"CharInfo","weaponInstId","Beyond.Gameplay");
            std::string weaponTemplate;
            if(weaponId) {
                auto* weapon=collection(info,"GetWeaponInstData");
                if(!weapon)throw std::runtime_error("native player weapon data unavailable; previous frame retained");
                eflink::unity::Api::TemporaryRoot weaponRoot(runtime,weapon);
                if(readField<std::uint64_t>(weapon,"InventorySystem/ItemInstData","instId","Beyond.Gameplay")!=weaponId)
                    throw std::runtime_error("native player weapon ownership differs; previous frame retained");
                weaponTemplate=text(readField<void*>(weapon,"InventorySystem/ItemInstData","templateId","Beyond.Gameplay"));
                if(weaponTemplate.empty())throw std::runtime_error("native equipped weapon template unavailable");
            }
            // An observed zero equipment ID means unequipped. Missing metadata
            // cannot silently become an unequip. No account instance ID travels.
            appearance["weapon_template"]=std::move(weaponTemplate);
            if(auto* weapon=api.call(gm("Entity","get_weaponCom","","Beyond.Gameplay.View.WeaponComponent"),entity))
                appearance["weapon_state"]=api.value<int>(gm("WeaponComponent","get_weaponState","","Beyond.Gameplay.View.WeaponComponent.WeaponState","Beyond.Gameplay.View"),weapon);
        }else {
            auto* data=api.call(gm("Entity","get_entityData","","Beyond.Gameplay.Core.BaseEntityData"),entity);
            if(!data)throw std::runtime_error("native battle template is unavailable");
            templateId=text(api.call(gm("BaseEntityData","get_templateId","","System.String"),data));
        }
        auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
        const auto identity=identityOf(entity);
        if(!identity.captured||!identity.instance)throw std::runtime_error("native battle allocation is unavailable");
        auto observation=worldauthority::nativeBattleObservation({kind,key,templateId,1,{p.x,p.y,p.z},
            api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability),
            api.value<double>(gm("AbilitySystem","get_maxHp","","System.Double"),ability),
            api.value<float>(gm("AbilitySystem","get_poise","","System.Single"),ability),
            api.value<float>(gm("AbilitySystem","get_maxPoise","","System.Single"),ability),
            api.value<bool>(entityAlive,entity)});
        const auto rotation=api.value<Q>(gm("Entity","get_rotation","","UnityEngine.Quaternion"),entity);
        observation.state["rotation"]={rotation.x,rotation.y,rotation.z,rotation.w};
        observation.state.update(appearance);
        if(!worldauthority::validStateForKind(kind,observation.state))throw std::runtime_error("native appearance state is unadapted; previous frame retained");
        return observation;
    }
    std::vector<worldauthority::Observation> sampleNativeBattle(const std::string& kind,worldauthority::NativeObjectBindings& staged) {
        std::vector<std::pair<std::string,void*>> entities;
        std::map<void*,int> partySlots;
        if(kind=="player") {
            for(const auto& [slot,entity]:nativeRealSquadMembers()) {
                entities.emplace_back(levelId+":host-squad:"+std::to_string(slot),entity);
                partySlots.emplace(entity,slot);
            }
        }else if(kind=="enemy") {
            auto* container=staticObject("objectContainer");
            if(!container||!api.call(gm("ObjectContainer","get_allEnemies","","Beyond.UnorderedList<Beyond.Gameplay.Core.Entity>"),container))
                throw std::runtime_error("native enemy collection is not initialized; previous census retained");
            auto current=containerEnemies();
            if(!current)throw std::runtime_error("native enemy container is unavailable; previous census retained");
            for(auto* entity:*current) {
                if(!entity)throw std::runtime_error("native enemy census contains an unresolved entry");
                auto key=entityKey(entity);
                if(key.empty())throw std::runtime_error("runtime enemy identity is unadapted; previous census retained");
                entities.emplace_back(std::move(key),entity);
            }
        }else throw std::runtime_error("unsupported battle census kind");
        if(entities.size()>worldauthority::Host::maxObjects)throw std::runtime_error("native battle census exceeds capacity");
        std::vector<worldauthority::Observation> facts;std::vector<worldauthority::NativeBindingInput> bindings;
        for(const auto& [key,entity]:entities) {
            facts.push_back(readNativeBattle(entity,kind,key,kind=="player"?partySlots.at(entity):-1));
            const auto identity=identityOf(entity);
            bindings.push_back({key,{reinterpret_cast<std::uintptr_t>(entity),identity.instance,0,identity.generation,identity.server,0}});
        }
        staged.reconcile(kind,bindings);
        for(auto& fact:facts) {
            fact.cookie=staged.cookie(kind,fact.key);
            if(!fact.cookie)throw std::runtime_error("staged actor identity unavailable");
        }
        return facts;
    }
    Json nativeCensusStatus() const {
        Json value=Json::object();
        for(const auto& [kind,status]:actorCensusStatus)value[kind]={{"scans",status.scans},{"objects",status.objects},
            {"age_ms",status.observed?nowMs()-status.observed:0},{"observed",status.observed!=0},{"error",status.error},
            {"read_only",true},{"execution_supported",false},{"dual_client_verified",false}};
        return value;
    }
