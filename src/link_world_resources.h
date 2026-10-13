    // Included inside World. Native resource census runs on the game thread.
    // No private account ID, resource pointer or locally allocated nature ID is published.
    std::uint32_t resourceRoom=0;std::uint64_t resourceEpoch=0,lastResourceScan=0;
    nlohmann::json nativeResourceCensus=nlohmann::json::array();
    std::map<std::string,std::size_t> resourceNativeTypes;
    std::string resourceError;std::uint64_t resourceScans=0,resourceUnknown=0,nextResourceCookie=0,lastResourceSuccess=0;
    struct NativeResource {void* object=nullptr;std::uint32_t root=0,component=0;std::uint64_t incarnation=0,natureId=0;
        NativeIdentity identity;std::string type,space,key;};
    std::map<std::string,NativeResource> nativeResources;
    void clearResources() {
        for(auto& [key,resource]:nativeResources)release(resource.root);
        nativeResources.clear();nativeResourceCensus=nlohmann::json::array();
        resourceRoom=0;resourceEpoch=lastResourceScan=lastResourceSuccess=0;
    }
    std::vector<worldauthority::Observation> sampleResources() {
        std::vector<worldauthority::Observation> result;
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
                auto old=nativeResources.find(key);
                if(old!=nativeResources.end()&&old->second.object==object&&old->second.component==component&&
                   old->second.natureId==natureId&&old->second.identity.matches(identity))incarnation=old->second.incarnation;
                else {if(nextResourceCookie==UINT64_MAX)throw std::runtime_error("native mineral allocation cookie exhausted");incarnation=++nextResourceCookie;}
                const auto position=api.value<V3>(gm(type.c_str(),"get_natureResourcePosition","","UnityEngine.Vector3",space.c_str()),object);
                const auto hp=readField<int>(object,type.c_str(),"m_hpRatioRuntime",space.c_str());
                const auto max=enumValue<int>(type.c_str(),"MAX_HP_RATIO",space.c_str());
                const bool collected=readField<bool>(object,type.c_str(),"m_isCollected",space.c_str());
                nlohmann::json state={{"template",resourceTemplate},{"position",{position.x,position.y,position.z}},
                    {"hp_ratio",hp},{"max_hp_ratio",max},{"collected",collected}};
                if(!worldauthority::validState(state)||max<=0)throw std::runtime_error("native mineral state outside verified bounds");
                if(sampled.contains(key))throw std::runtime_error("multiple native minerals share an identity");
                const auto root=rootOf(object);if(!root)throw std::runtime_error("native mineral rooting failed");
                sampled.emplace(key,NativeResource{object,root,component,incarnation,natureId,identity,type,space,key});
                result.push_back({key,"mineral",std::move(state),incarnation});
                if(result.size()>512)throw std::runtime_error("native mineral baseline exceeds capacity");
            }
            if(!complete)throw std::runtime_error("native resource census truncated; previous baseline retained");
            for(auto& [key,value]:nativeResources)release(value.root);
            nativeResources=std::move(sampled);return result;
        }catch(...) {cleanup();throw;}
    }
    void updateResources(Net& net,Session& link) {
        const auto snapshot=net.snapshot();
        if(snapshot.role==Role::Offline||!link.followHost||!link.combat||!link.localSceneOperational()||!nativeSceneInitialized||travel||
           link.snapshot().value("map",std::string{})!=mapId||link.snapshot().value("level",std::string{})!=levelId) {
            if(resourceRoom)clearResources();return;
        }
        if(resourceRoom!=snapshot.roomId||resourceEpoch!=link.generation()) {
            clearResources();resourceRoom=snapshot.roomId;resourceEpoch=link.generation();
        }
        if(nowMs()-lastResourceScan<100)return;lastResourceScan=nowMs();
        try {
            const auto observations=sampleResources();nativeResourceCensus=nlohmann::json::array();
            for(const auto& observation:observations) {
                auto state=observation.state;state["key"]=observation.key;state["kind"]=observation.kind;nativeResourceCensus.push_back(std::move(state));
            }
            if(snapshot.role==Role::Host) {link.observeNativeObjects("mineral",observations);link.publishAuthority(net);}
            ++resourceScans;lastResourceSuccess=nowMs();resourceError.clear();
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
