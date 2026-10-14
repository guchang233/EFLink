    // Included inside World. Native resource census runs on the game thread.
    // No private account ID, resource pointer or locally allocated nature ID is published.
    std::uint32_t resourceRoom=0;std::uint64_t resourceEpoch=0,lastResourceScan=0;
    nlohmann::json nativeResourceCensus=nlohmann::json::array();
    std::map<std::string,std::size_t> resourceNativeTypes;
    std::string resourceError;std::uint64_t resourceScans=0,resourceUnknown=0,lastResourceSuccess=0;
    struct NativeResource {void* object=nullptr;std::uint32_t root=0,component=0;std::uint64_t incarnation=0,natureId=0;
        NativeIdentity identity;std::string type,space,key;};
    std::map<std::string,NativeResource> nativeResources;
    struct NativeResourceSample {
        World& world;std::map<std::string,NativeResource> resources;
        std::vector<worldauthority::Observation> facts;
        nlohmann::json census=nlohmann::json::array();
        explicit NativeResourceSample(World& value):world(value) {}
        ~NativeResourceSample() {for(auto& [key,value]:resources)world.release(value.root);}
        NativeResourceSample(const NativeResourceSample&)=delete;
        NativeResourceSample& operator=(const NativeResourceSample&)=delete;
    };
    void clearResources() {
        nativeObjectBindings.retireKind("mineral");personalObjectBindings.retireKind("mineral");
        for(auto& [key,resource]:nativeResources)release(resource.root);
        nativeResources.clear();nativeResourceCensus=nlohmann::json::array();
        resourceRoom=0;resourceEpoch=lastResourceScan=lastResourceSuccess=0;
        clearNativeCensus();
    }
    void sampleResources(worldauthority::NativeObjectBindings& staged,NativeResourceSample& output) {
        std::vector<worldauthority::Observation> result;
        std::vector<worldauthority::NativeBindingInput> bindings;
        std::map<std::string,NativeResource> sampled;
        auto cleanup=[&] {for(auto& [key,value]:sampled)release(value.root);};
        try {
            auto* manager=staticObject("natureResourceManager");
            if(!manager)throw std::runtime_error("native nature resource manager unavailable");
            auto* dictionary=readField<void*>(manager,"NatureResourceManager","m_resources","Beyond.Gameplay");
            if(!dictionary)throw std::runtime_error("native resource collection unavailable");
            auto* values=collection(dictionary,"get_Values");eflink::unity::Api::TemporaryRoot valuesRoot(runtime,values);
            auto* enumerator=genericValueCall(values,"GetEnumerator");eflink::unity::Api::TemporaryRoot enumRoot(runtime,enumerator);
            const auto mine=enumValue<int>("ENatureType","Mine","Beyond.Gameplay");
            bool complete=false;resourceUnknown=0;resourceNativeTypes.clear();
            for(unsigned index=0;index<4096;++index) {
                auto* box=genericValueCall(enumerator,"MoveNext");bool more=false;
                if(!box)throw std::runtime_error("native resource enumeration returned no result");
                std::memcpy(&more,runtime->object_unbox(runtime->context,box),sizeof more);
                if(!more){complete=true;break;}
                auto* object=genericValueCall(enumerator,"get_Current");if(!object)continue;
                const auto full=classOf(object);++resourceNativeTypes[full];std::string type,space,key,resourceTemplate;std::uint64_t incarnation=0,natureId=0;
                if(full=="Beyond.Gameplay.InteractiveLogicDoodadCommon") {type="InteractiveLogicDoodadCommon";space="Beyond.Gameplay";}
                else if(full=="Beyond.Gameplay.Core.InteractiveDoodadCommonComponent") {type="InteractiveDoodadCommonComponent";space="Beyond.Gameplay.Core";}
                else {++resourceUnknown;continue;} // ECS resources need their own property/lifetime adapter.
                if(api.value<int>(gm(type.c_str(),"get_natureResourceType","","Beyond.Gameplay.ENatureType",space.c_str()),object)!=mine)continue;
                void* entity=nullptr;
                if(type=="InteractiveLogicDoodadCommon") {
                    const auto logic=readField<std::uint64_t>(object,type.c_str(),"m_logicId",space.c_str());
                    auto* controller=readField<void*>(object,"InteractiveLogicBase","<controller>k__BackingField","Beyond.Gameplay");
                    entity=controller?api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),controller):nullptr;
                    if(logic)key=levelId+":mine:logic:"+std::to_string(logic);
                }else {
                    entity=api.call(gm("BaseComponent","get_entity","","Beyond.Gameplay.Core.Entity"),object);
                    if(usable(entity))key=entityKey(entity);
                    if(!key.empty())key+="/mine";
                }
                if(!usable(entity)||key.empty()){++resourceUnknown;continue;}
                const auto identity=identityOf(entity);
                const auto component=readField<std::uint32_t>(type=="InteractiveLogicDoodadCommon"?
                    readField<void*>(object,"InteractiveLogicBase","<controller>k__BackingField","Beyond.Gameplay"):object,
                    "BaseComponent","<instanceUid>k__BackingField");
                if(!component||!identity.captured){++resourceUnknown;continue;}
                resourceTemplate=text(readField<void*>(object,type.c_str(),"m_doodadId",space.c_str()));
                if(resourceTemplate.empty()){++resourceUnknown;continue;}
                natureId=api.value<std::uint64_t>(gm(type.c_str(),"get_natureResourceId","","System.UInt64",space.c_str()),object);
                // Commit identity only after the entire census and its native
                // state have passed validation. The allocator is shared by all
                // world kinds; minerals do not maintain a parallel lifecycle.
                incarnation=1; // provisional; never published before reconcile
                const auto position=api.value<V3>(gm(type.c_str(),"get_natureResourcePosition","","UnityEngine.Vector3",space.c_str()),object);
                const auto hp=readField<int>(object,type.c_str(),"m_hpRatio",space.c_str());
                const auto max=enumValue<int>(type.c_str(),"MAX_HP_RATIO",space.c_str());
                const bool collected=readField<bool>(object,type.c_str(),"m_isCollected",space.c_str());
                auto observation=worldauthority::mineralObservation({key,resourceTemplate,{position.x,position.y,position.z},incarnation,hp,
                    readField<int>(object,type.c_str(),"m_hpRatioRuntime",space.c_str()),max,collected});
                if(sampled.contains(key))throw std::runtime_error("multiple native minerals share an identity");
                auto root=rootOf(object);if(!root)throw std::runtime_error("native mineral rooting failed");
                try {sampled.emplace(key,NativeResource{object,root,component,incarnation,natureId,identity,type,space,key});}
                catch(...) {release(root);throw;}
                bindings.push_back({key,{reinterpret_cast<std::uintptr_t>(object),identity.instance,component,
                    identity.generation,identity.server,natureId}});
                result.push_back(std::move(observation));
                if(result.size()>512)throw std::runtime_error("native mineral baseline exceeds capacity");
            }
            if(!complete)throw std::runtime_error("native resource census truncated; previous baseline retained");
            staged.reconcile("mineral",bindings);
            for(auto& observation:result) {
                const auto cookie=staged.find("mineral",observation.key)->cookie;
                observation.cookie=cookie;sampled.at(observation.key).incarnation=cookie;
                auto state=observation.state;state["key"]=observation.key;state["kind"]=observation.kind;
                output.census.push_back(std::move(state));
            }
            output.resources.swap(sampled);output.facts.swap(result);
        }catch(...) {cleanup();throw;}
    }
    void updateResources(Net& net,Session& link) {
        const auto snapshot=net.snapshot();
        if(snapshot.role==Role::Offline||!link.followHost||!link.combat||!link.localSceneOperational()||!nativeSceneInitialized||
           link.snapshot().value("map",std::string{})!=mapId||link.snapshot().value("level",std::string{})!=levelId) {
            if(resourceRoom)clearResources();
            if(guestRoomBindings.size())guestRoomBindings.clear();
            return;
        }
        if(resourceRoom!=snapshot.roomId||resourceEpoch!=link.generation()) {
            clearResources();guestRoomBindings.clear();resourceRoom=snapshot.roomId;resourceEpoch=link.generation();
        }
        if(nowMs()-lastResourceScan<100)return;lastResourceScan=nowMs();
        NativeResourceSample sample(*this);
        try {
            auto& bindings=snapshot.role==Role::Host?nativeObjectBindings:personalObjectBindings;
            auto staged=bindings.preview();
            sampleResources(staged,sample);
            std::map<std::string,std::vector<worldauthority::Observation>> frame;
            if(snapshot.role==Role::Host) {
                for(const std::string kind:{"player","enemy"}) {
                    try {frame[kind]=sampleNativeBattle(kind,staged);}
                    catch(const std::exception& e) {actorCensusStatus[kind].error=e.what();throw;}
                }
                frame["mineral"]=sample.facts;
            }
            if(!bindings.commitWith(staged,[&] {return snapshot.role!=Role::Host||link.observeNativeFrame(frame);}))
                throw std::runtime_error("unified authority rejected native frame; bindings and previous baseline retained");
            nativeResources.swap(sample.resources);nativeResourceCensus.swap(sample.census);
            for(const auto& [kind,facts]:frame) {
                if(kind=="mineral")continue;
                auto& status=actorCensusStatus[kind];++status.scans;status.objects=facts.size();status.observed=nowMs();status.error.clear();
            }
            ++resourceScans;lastResourceSuccess=nowMs();resourceError.clear();
            if(snapshot.role==Role::Host)link.publishAuthority(net);
        }catch(const std::exception& e) {resourceError=e.what();}
    }
    nlohmann::json resourceProperties() {
        nlohmann::json result=nlohmann::json::object();
        for(const auto& [key,resource]:nativeResources) {
            if(resource.type!="InteractiveDoodadCommonComponent")continue;
            auto* board=genericValueCall(resource.object,"get_properties");if(!board)continue;
            auto* dictionary=readField<void*>(board,"ParamBlackboard","variableDict","Beyond.Gameplay");if(!dictionary)continue;
            auto* keys=collection(dictionary,"get_Keys");eflink::unity::Api::TemporaryRoot keysRoot(runtime,keys);
            auto* enumerator=genericValueCall(keys,"GetEnumerator");eflink::unity::Api::TemporaryRoot enumRoot(runtime,enumerator);
            auto properties=nlohmann::json::object();
            for(unsigned i=0;i<64;++i) {
                auto* box=genericValueCall(enumerator,"MoveNext");bool more=false;
                if(box)std::memcpy(&more,runtime->object_unbox(runtime->context,box),sizeof more);if(!more)break;
                const auto name=text(genericValueCall(enumerator,"get_Current"));
                if(name.empty()||name.size()>128)continue;
                auto* variable=collection(dictionary,"get_Item",{api.string(name)});if(!variable)continue;
                auto* raw=genericValueCall(variable,"get_rawValue");if(!raw)continue;
                const auto type=classOf(raw);
                nlohmann::json value={{"type",type}};
                if(type=="System.Int32") {int number=0;std::memcpy(&number,runtime->object_unbox(runtime->context,raw),sizeof number);value["value"]=number;}
                else if(type=="System.Int64") {std::int64_t number=0;std::memcpy(&number,runtime->object_unbox(runtime->context,raw),sizeof number);value["value"]=number;}
                else if(type=="System.Boolean") {bool flag=false;std::memcpy(&flag,runtime->object_unbox(runtime->context,raw),sizeof flag);value["value"]=flag;}
                properties[name]=std::move(value);
            }
            result[key]=std::move(properties);
        }
        return result;
    }
