    // Included inside World. Real host Entity/AbilitySystem facts use the same
    // binding allocator and ledger as minerals. Census never executes inputs or
    // supplies an operation witness, ownership grant or gameplay certificate.
    std::uint32_t actorCensusRoom=0;std::uint64_t actorCensusEpoch=0,lastActorCensus=0;
    struct CensusStatus {std::uint64_t scans=0,observed=0;std::size_t objects=0;std::string error;};
    std::map<std::string,CensusStatus> actorCensusStatus;
    void clearNativeCensus() {
        nativeObjectBindings.retireKind("player");nativeObjectBindings.retireKind("enemy");
        actorCensusRoom=0;actorCensusEpoch=lastActorCensus=0;actorCensusStatus.clear();
    }
    worldauthority::Observation readNativeBattle(void* entity,const std::string& kind,const std::string& key) {
        if(!usable(entity)||!api.value<bool>(gm("Entity","get_hasStarted","","System.Boolean"),entity))
            throw std::runtime_error("native battle entity is not initialized");
        eflink::unity::Api::TemporaryRoot entityRoot(runtime,entity);
        auto* ability=api.call(gm("Entity","get_abilityCom","","Beyond.Gameplay.Core.AbilitySystem"),entity);
        if(!ability)throw std::runtime_error("native battle ability state is unavailable");
        std::string templateId;
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
        }else {
            auto* data=api.call(gm("Entity","get_entityData","","Beyond.Gameplay.Core.BaseEntityData"),entity);
            if(!data)throw std::runtime_error("native battle template is unavailable");
            templateId=text(api.call(gm("BaseEntityData","get_templateId","","System.String"),data));
        }
        auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),entity);
        const auto identity=identityOf(entity);
        if(!identity.captured||!identity.instance)throw std::runtime_error("native battle allocation is unavailable");
        return worldauthority::nativeBattleObservation({kind,key,templateId,1,{p.x,p.y,p.z},
            api.value<double>(gm("AbilitySystem","get_hp","","System.Double"),ability),
            api.value<double>(gm("AbilitySystem","get_maxHp","","System.Double"),ability),
            api.value<float>(gm("AbilitySystem","get_poise","","System.Single"),ability),
            api.value<float>(gm("AbilitySystem","get_maxPoise","","System.Single"),ability),
            api.value<bool>(entityAlive,entity)});
    }
    std::vector<worldauthority::Observation> sampleNativeBattle(const std::string& kind) {
        std::vector<std::pair<std::string,void*>> entities;
        if(kind=="player") {
            auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
            auto* player=instance?api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"),instance):nullptr;
            auto* squad=player?readField<void*>(player,"GamePlayer","squadManager","Beyond.Gameplay"):nullptr;
            if(!squad)throw std::runtime_error("native squad is unavailable; previous census retained");
            const auto count=api.value<int>(gm("SquadManager","get_slotCount","","System.Int32"),squad);
            if(count<0||count>4)throw std::runtime_error("unsupported native squad size");
            for(int slot=0;slot<count;++slot) {
                auto* entity=api.call(gm("SquadManager","GetMemberBySlot","System.Int32","Beyond.Gameplay.Core.Entity"),squad,{&slot});
                // A vacant squad slot is not an allocation; a present but
                // partially initialized actor fails the entire census below.
                if(entity)entities.emplace_back(levelId+":host-squad:"+std::to_string(slot),entity);
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
            facts.push_back(readNativeBattle(entity,kind,key));
            const auto identity=identityOf(entity);
            bindings.push_back({key,{reinterpret_cast<std::uintptr_t>(entity),identity.instance,0,identity.generation,identity.server,0}});
        }
        nativeObjectBindings.reconcile(kind,bindings);
        for(auto& fact:facts)fact.cookie=nativeObjectBindings.find(kind,fact.key)->cookie;
        return facts;
    }
    void updateNativeCensus(Net& net,Session& link) {
        const auto snapshot=net.snapshot();
        if(snapshot.role!=Role::Host||!link.ready()||!link.followHost||!link.combat||!link.localSceneOperational()||!nativeSceneInitialized||
           link.snapshot().value("map",std::string{})!=mapId||link.snapshot().value("level",std::string{})!=levelId) {
            if(actorCensusRoom)clearNativeCensus();return;
        }
        if(actorCensusRoom!=snapshot.roomId||actorCensusEpoch!=link.generation()) {
            clearNativeCensus();actorCensusRoom=snapshot.roomId;actorCensusEpoch=link.generation();
        }
        if(nowMs()-lastActorCensus<100)return;lastActorCensus=nowMs();
        for(const std::string kind:{"player","enemy"}) {
            auto& status=actorCensusStatus[kind];
            try {
                auto facts=sampleNativeBattle(kind);
                if(!link.observeNativeObjects(kind,facts))throw std::runtime_error("unified authority rejected native census; previous baseline retained");
                ++status.scans;status.objects=facts.size();status.observed=nowMs();status.error.clear();
            }catch(const std::exception& e) {status.error=e.what();}
        }
        link.publishAuthority(net);
    }
    Json nativeCensusStatus() const {
        Json value=Json::object();
        for(const auto& [kind,status]:actorCensusStatus)value[kind]={{"scans",status.scans},{"objects",status.objects},
            {"age_ms",status.observed?nowMs()-status.observed:0},{"observed",status.observed!=0},{"error",status.error},
            {"read_only",true},{"execution_supported",false},{"dual_client_verified",false}};
        return value;
    }
