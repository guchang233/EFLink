    // Included inside World. Shader effects on room models use display-only APIs.
    struct MaterialVisual {void* helper=nullptr;std::uint32_t root=0;std::uint16_t actor=0;
        NativeIdentity identity;std::string name;};
    std::map<std::pair<std::uint16_t,std::uint64_t>,MaterialVisual> materialVisuals;
    std::map<std::pair<std::uintptr_t,std::uint64_t>,std::uint64_t> materialLives;
    Json sampledMaterials=Json::array(),materialReadback=Json::array();
    std::map<void*,std::set<std::string>> materialCensus;
    std::string materialError;std::uint64_t materialsStarted=0,materialsStopped=0;
    void* genericValueCall(void* object,const char* name) {
        auto klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(object);
        auto* method=symbol<const void*(*)(const void*,const char*,int)>("il2cpp_class_get_method_from_name")(klass,name,0);
        if(!method)throw std::runtime_error(std::string("generic iterator method unavailable: ")+name);
        BE_ResolvedMethodV1 resolved{};resolved.method_info=method;
        return api.call(resolved,symbol<bool(*)(const void*)>("il2cpp_class_is_valuetype")(klass)?runtime->object_unbox(runtime->context,object):object);
    }
    std::optional<std::string> helperPath(void* helper,void* modelObject) {
        auto* object=api.call(m("Component","get_gameObject","","UnityEngine.GameObject"),helper);
        if(!object||!alive(object)||!alive(modelObject))return std::nullopt;
        auto* root=api.call(getTransform,modelObject);auto* cursor=api.call(getTransform,object);
        std::vector<std::string> parts;
        for(unsigned i=0;i<32&&cursor;i++) {
            if(cursor==root) {
                std::string path;for(auto it=parts.rbegin();it!=parts.rend();++it){if(!path.empty())path+='/';path+=*it;}
                if(path.size()<=512)return path;return std::nullopt;
            }
            auto name=text(api.call(objectName,cursor));if(name.find('/')!=std::string::npos)return std::nullopt;
            parts.push_back(name);cursor=api.call(m("Transform","get_parent","","UnityEngine.Transform"),cursor);
        }
        return std::nullopt;
    }
    void sampleMaterials() {
        sampledMaterials=Json::array();materialCensus.clear();std::set<std::pair<std::uintptr_t,std::uint64_t>> present;
        auto* manager=instanceObject("effectManager");if(!manager)return;
        auto* dictionary=readField<void*>(manager,"EffectManager","m_activeVfx","Beyond.Gameplay");if(!dictionary)return;
        auto* values=collection(dictionary,"get_Values");eflink::unity::Api::TemporaryRoot valuesRoot(runtime,values);
        auto* enumerator=genericValueCall(values,"GetEnumerator");eflink::unity::Api::TemporaryRoot enumRoot(runtime,enumerator);
        for(unsigned groups=0;groups<512;++groups) {
            auto* box=genericValueCall(enumerator,"MoveNext");bool more=false;
            if(box)std::memcpy(&more,runtime->object_unbox(runtime->context,box),sizeof more);if(!more)break;
            auto* data=genericValueCall(enumerator,"get_Current");if(!data)continue;
            auto* list=readField<void*>(data,"VFXData","data","Beyond.Gameplay");if(!list)continue;
            auto* countBox=collection(list,"get_Count");int count=0;
            if(countBox)std::memcpy(&count,runtime->object_unbox(runtime->context,countBox),sizeof count);
            if(count<0||count>4096)throw std::runtime_error("invalid native material effect list");
            for(int i=0;i<count&&sampledMaterials.size()<64;++i) {
                auto* controller=collection(list,"get_Item",{&i});if(!controller)continue;
                constexpr const char* space="Beyond.Rendering",*library="Rendering.Beyond.dll";
                if(!readField<bool>(controller,"EntityVFXControllerBase","m_isPlaying",space,library))continue;
                auto* helper=readField<void*>(controller,"EntityVFXControllerBase","m_owner",space,library);
                if(!helper||classOf(helper)!="Beyond.Gameplay.View.EntityRenderHelper"||!alive(helper))continue;
                auto name=text(readField<void*>(controller,"EntityVFXControllerBase","m_vfxName",space,library));
                if(!presentationResource(name))continue;materialCensus[helper].insert(name);
                auto* entity=readField<void*>(helper,"EntityRenderHelper","m_entity","Beyond.Gameplay.View");
                for(const auto& [slot,p]:partySamples)if(p.entity==entity&&currentEntity(entity,p.identity)) {
                    auto path=helperPath(helper,p.object);if(!path)continue;

                    const auto key=std::make_pair(reinterpret_cast<std::uintptr_t>(controller),p.life);present.insert(key);
                    auto& id=materialLives[key];if(!id)id=++nextEffectLife;
                    sampledMaterials.push_back({{"id",id},{"slot",slot},{"life",p.life},{"path",*path},{"name",name}});break;
                }
            }
        }
        for(auto it=materialLives.begin();it!=materialLives.end();)if(!present.contains(it->first))it=materialLives.erase(it);else ++it;
    }
    void stopMaterial(MaterialVisual& visual) {
        auto actor=actors.find(visual.actor);
        try {if(visual.helper&&actor!=actors.end()&&currentActor(actor->second)&&visual.identity.matches(actor->second.identity)&&alive(visual.helper))
            api.call(gm("EntityRenderHelper","Stop","System.String","System.Void","Beyond.Gameplay.View"),visual.helper,{api.string(visual.name)});
        }catch(...) {}
        release(visual.root);visual.helper=nullptr;++materialsStopped;
    }
    void clearMaterials() {
        for(auto& [key,v]:materialVisuals)stopMaterial(v);materialVisuals.clear();materialLives.clear();
        sampledMaterials=Json::array();materialReadback=Json::array();
    }
    void replayMaterials(const Snapshot& view,Session& link) {
        materialReadback=Json::array();std::set<std::pair<std::uint16_t,std::uint64_t>> present;
        for(const auto& peer:view.peers)if(!peer.local) {
            auto frame=link.presentation(peer.id);if(frame.is_null())continue;
            for(const auto& state:frame.value("material_vfx",Json::array())) {
                const Json* member=nullptr;for(const auto& m:frame.at("members"))if(m.at("slot")==state.at("slot")&&m.at("life")==state.at("life"))member=&m;
                if(!member)continue;
                const auto actorKey=member->at("controlled").get<bool>()?peer.id:std::uint16_t(0x8000+peer.id*4+member->at("slot").get<unsigned>());
                auto actor=actors.find(actorKey);if(actor==actors.end()||!actor->second.started||!currentActor(actor->second))continue;
                auto key=std::make_pair(peer.id,state.at("id").get<std::uint64_t>());present.insert(key);auto& visual=materialVisuals[key];
                if(visual.helper&&(visual.actor!=actorKey||!visual.identity.matches(actor->second.identity)))stopMaterial(visual);
                if(!visual.helper) {
                    auto* component=api.call(entityModel,actor->second.entity);auto* object=component?api.call(modelGo,component):nullptr;
                    if(!object||!alive(object))continue;
                    for(auto* helper:components(object,"EntityRenderHelper","Gameplay.Beyond.dll","Beyond.Gameplay.View")) {
                        auto path=helperPath(helper,object);if(!path||*path!=state.at("path").get<std::string>())continue;
                        visual.helper=helper;visual.root=rootOf(helper);visual.actor=actorKey;visual.identity=actor->second.identity;
                        visual.name=state.at("name").get<std::string>();
                        api.call(gm("EntityRenderHelper","Play","System.String","System.Void","Beyond.Gameplay.View"),helper,{api.string(visual.name)});++materialsStarted;break;
                    }
                }
                materialReadback.push_back({{"peer",peer.id},{"instance",key.second},{"name",state.at("name")},{"bound",visual.helper!=nullptr},{"playing",visual.helper&&materialCensus[visual.helper].contains(visual.name)}});
            }
        }
        for(auto it=materialVisuals.begin();it!=materialVisuals.end();)if(!present.contains(it->first)){stopMaterial(it->second);it=materialVisuals.erase(it);}else ++it;
    }
