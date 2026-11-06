// Included inside World (private). Following the host and teleporting to players.
//
// Every move is this account's own official action, never a room result applied
// to the account: the map screen's hub teleport (GameAction.TeleportToPosition to
// one of this account's hubs, validated by the official server), the game's own
// dungeon entry/leave flow, and an in-map load at a position behind the loading
// panel. Arrival invalidates the scene (setLocalSceneReady false while loading), so
// room operations stay closed until the new scene's authority baseline arrives.
struct Travel {
    std::string map,level;V3 position{};std::uint64_t until=0;bool started=false;int levelNumber=0;
    bool homeward=false,observedLoading=false;std::uint64_t startedAt=0;
    bool official=false;  // an official hub teleport to the target map is in flight
    std::string dungeon;  // target dungeon ID ("" = overworld)
    int dungeonStage=0;   // 1 leaving this account's dungeon, 2 entering the target's
    int inMapLoads=0;     // same-map loads tried (bounded: never loop)
    bool following=false; // follows the host's scene (retargeted while not yet under way)
};
struct HubTarget {std::string level,inst;std::uint64_t node=0;V3 position{},rotation{};};
std::optional<Travel> travel;std::string travelState="idle";nlohmann::json travelLifecycle=nlohmann::json::object();
std::optional<Travel> home; // where this guest stood before entering the host's world
nlohmann::json officialTeleportStatus=nlohmann::json::object();
std::uint64_t officialTeleports=0,completedTeleports=0,followGeneration=0,followRoom=0;
bool emergencyHomePending=false;std::string travelError;

template<class T> void setField(void* object,const char* type,const char* name,T value,const char* space="Beyond.Gameplay.Core") {
    auto klass=api.klass(type,"Gameplay.Beyond.dll",space);
    auto* descriptor=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass.class_info,name);
    if(!descriptor) throw std::runtime_error(std::string("field unavailable: ")+type+"."+name);
    symbol<void(*)(void*,const void*,void*)>("il2cpp_field_set_value")(object,descriptor,&value);
}
static Travel makeTravel(std::string map,std::string level,V3 position,int levelNumber,std::string dungeon,std::uint64_t timeoutMs=90000) {
    Travel t;t.map=std::move(map);t.level=std::move(level);t.position=position;t.levelNumber=levelNumber;t.dungeon=std::move(dungeon);
    t.until=nowMs()+timeoutMs;return t;
}
static V3 vec(const nlohmann::json& p) {return {p.at(0).get<float>(),p.at(1).get<float>(),p.at(2).get<float>()};}
std::string currentDungeon() {
    auto* dungeon=api.call(gm("GameInstance","get_dungeonManager","","Beyond.Gameplay.Core.DungeonManager","Beyond.Gameplay"));
    if(!dungeon||!api.value<bool>(gm("DungeonManager","get_inDungeon","","System.Boolean"),dungeon))return {};
    return text(api.call(gm("DungeonManager","get_curDungeonId","","System.String"),dungeon));
}
void teleportTo(V3 position) {
    if(!usable(character)||api.value<bool>(entityCinematic,character)) throw std::runtime_error("当前角色未就绪或正在过场");
    auto* movement=api.call(gm("Entity","get_movementComponent","","Beyond.Gameplay.Core.MovementComponent"),character);
    if(!movement) throw std::runtime_error("当前角色没有移动组件");
    bool warning=false;api.call(gm("MovementComponent","TeleportTo","UnityEngine.Vector3|System.Boolean"),movement,{&position,&warning});
}
void failTravel(const std::string& why) {travel.reset();travelState="failed";travelError=why;}
void updateTravel() {
    if(!travel) return;
    auto* levelLoader=staticObject("levelLoader");
    if(nowMs()>travel->until) {failTravel("地图加载或传送超时；未报告成功");return;}
    if(!levelLoader||!character) {travelState="waiting_scene";return;}
    auto* info=staticObject("worldInfo");
    if(!info) {travelState="waiting_scene";return;}
    auto* mode=api.call(gm("GameInstance","get_mode","","Beyond.Gameplay.Core.BaseGameMode","Beyond.Gameplay"));
    auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
    auto* dungeon=api.call(gm("GameInstance","get_dungeonManager","","Beyond.Gameplay.Core.DungeonManager","Beyond.Gameplay"));
    auto* subgame=staticObject("subGameManager");
    const bool loading=api.value<bool>(gm("GameLevelLoader","get_isLoading","","System.Boolean"),levelLoader);
    const bool started=api.value<bool>(gm("WorldInfo","get_isStarted","","System.Boolean"),info);
    const bool delayed=instance&&readField<bool>(instance,"GameInstance","m_isModeDelayedToSwitch","Beyond.Gameplay");
    const bool inDungeon=dungeon&&api.value<bool>(gm("DungeonManager","get_inDungeon","","System.Boolean"),dungeon);
    const bool inSubgame=subgame&&api.value<bool>(gm("SubGameManager","get_inSubGame","","System.Boolean"),subgame);
    const auto modeId=mode?text(api.call(gm("BaseGameMode","get_modeId","","System.String"),mode)):std::string{};
    const bool mainScope=api.value<bool>(gm("GameInstance","get_isMainScope","","System.Boolean","Beyond.Gameplay"));
    travelLifecycle={{"world_started",started},{"mode_switch_delayed",delayed},{"mode_id",modeId},
        {"in_dungeon",inDungeon},{"in_subgame",inSubgame},{"main_scope",mainScope},{"loading",loading}};
    if(loading) {
        travel->observedLoading=true;travelState="loading";
        travelLifecycle["loading_ms"]=nowMs()-travel->startedAt;
        travelLifecycle["progress"]=api.value<float>(gm("GameLevelLoader","get_progress","","System.Single"),levelLoader);
        return;
    }
    // Official hub teleport in flight: wait for the target map, then finish the
    // rest within that map like any same-map move.
    if(travel->official) {
        if(mapId!=travel->map) {travelState="official_teleport";return;}
        travel->official=false;travel->started=false;travel->observedLoading=false;
    }
    {
        // Dungeons are entered and left by this account through the game's own
        // dungeon flow, never a local load.
        const auto current=inDungeon?text(api.call(gm("DungeonManager","get_curDungeonId","","System.String"),dungeon)):std::string{};
        if(travel->dungeonStage==1) {if(inDungeon) {travelState="leaving_dungeon";return;}travel->dungeonStage=0;travel->started=false;}
        if(travel->dungeonStage==2) {if(current!=travel->dungeon||!started) {travelState="entering_dungeon";return;}travel->dungeonStage=0;travel->started=false;travel->observedLoading=false;}
        if(inDungeon&&current!=travel->dungeon) {
            api.call(gm("DungeonManager","LeaveDungeon"),dungeon);
            travel->dungeonStage=1;travelState="leaving_dungeon";return;
        }
        if(!travel->dungeon.empty()&&!inDungeon) {
            if(!dungeon)throw std::runtime_error("副本管理器尚未就绪");
            auto* id=api.string(travel->dungeon);
            auto* player=instance?api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"),instance):nullptr;
            auto* bag=player?readField<void*>(player,"GamePlayer","charBag","Beyond.Gameplay"):nullptr;
            int team=bag?api.value<int>(gm("CharBagSystem","get_curTeamIndex","","System.Int32","Beyond.Gameplay"),bag):0;
            auto* preset=api.string("");
            const bool requested=api.value<bool>(gm("DungeonManager","TryReqEnterDungeon","System.String|System.String|System.Int32","System.Boolean"),dungeon,{id,preset,&team});
            if(!requested)throw std::runtime_error("游戏未允许进入副本 "+travel->dungeon+"（未解锁或条件不满足，见游戏内提示）");
            officialTeleportStatus={{"purpose","follow_dungeon"},{"dungeon",travel->dungeon},{"team",team},{"at",nowMs()}};
            travel->dungeonStage=2;travelState="entering_dungeon";return;
        }
    }
    const bool targetLoaded=mapId==travel->map&&levelId==travel->level;
    const bool overworld=(mainScope&&mode&&modeId.empty()&&!inDungeon&&!inSubgame&&
        mapId.rfind("map",0)==0&&travel->map.rfind("map",0)==0)||(inDungeon&&!travel->dungeon.empty());
    // A private subgame has its own exit; never strand it with a scene swap.
    if((!targetLoaded||travel->started)&&!overworld)
        throw std::runtime_error("当前处于子玩法或特殊模式，无法联机切图；请先通过游戏退出");
    // The loader queues a pipeline; wait until it has begun before treating
    // "not loading" as finished.
    if(travel->started&&!travel->observedLoading&&nowMs()-travel->startedAt<3000) {travelState="loading_requested";return;}
    const bool sameMap=mapId==travel->map;
    if(sameMap&&!started&&travel->started) {
        // LoadAtPos can leave a loaded world awaiting StartWorld: use the
        // idempotent native lifecycle.
        bool inNewMap=true;
        api.call(gm("WorldInfo","StartWorld","System.Boolean"),info,{&inNewMap});
        travelState="starting_world";return;
    }
    if(!started||delayed) {travelState="waiting_world_start";return;}
    if(sameMap&&(travel->started||!overworld)) {
        teleportTo(travel->position);
        auto actual=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
        auto p=travel->position;float d=(actual.x-p.x)*(actual.x-p.x)+(actual.y-p.y)*(actual.y-p.y)+(actual.z-p.z)*(actual.z-p.z);
        // Arrival is judged by position on the target map (the level part may differ).
        if(d<9) {travelState="complete";++completedTeleports;travel.reset();travelError.clear();}
        else if(travel->started) {travel->started=false;travel->observedLoading=false;} // load once more, bounded below
        return;
    }
    if(mapId!=travel->map&&travel->dungeon.empty()) {
        // Another map: this account gets there through an official teleport to
        // its own hub nearest the destination, then moves within that map.
        officialHubTeleport(travel->map,travel->position,travel->homeward?"return_home":travel->following?"follow_host":"teleport_to_player");
        travel->official=true;travel->started=true;travel->observedLoading=false;travel->startedAt=nowMs();travelState="official_teleport";
        return;
    }
    // Same map: an in-map load at the position behind the game's loading panel.
    if(mapId!=travel->map)throw std::runtime_error("目标副本地图与当前地图不一致");
    if(++travel->inMapLoads>2)throw std::runtime_error("同图加载两次仍未到达目标位置，已停止传送");
    auto* parameter=runtime->object_new(runtime->context,api.klass("TeleportParam","Gameplay.Beyond.dll","Beyond.Gameplay").class_info);
    eflink::unity::Api::TemporaryRoot parameterRoot(runtime,parameter);
    setField(parameter,"TeleportParam","uiType",enumValue<int>("TeleportUIType","WithLoading","Beyond.Gameplay"),"Beyond.Gameplay");
    auto* raw=runtime->object_unbox(runtime->context,parameter);if(!raw) throw std::runtime_error("地图传送参数 ABI 不可用");
    travel->started=true;travel->observedLoading=false;travel->startedAt=nowMs();travelState="loading";
    try {
        api.call(gm("GameLevelLoader","LoadAtPosInCurrentMap","UnityEngine.Vector3|Beyond.Gameplay.TeleportParam&|System.Action|System.Action"),levelLoader,{&travel->position,raw,nullptr,nullptr});
    }catch(const std::exception& e) {
        // Still arrive, without the panel, rather than strand the player.
        travelError=std::string("原生读条传送不可用，已直接传送: ")+e.what();
        travel->observedLoading=true;
    }
}
// This account's teleportable factory hubs on a map (marks of GamePlayer.mapManager).
std::vector<HubTarget> hubTargets(const std::string& mapName) {
    std::vector<HubTarget> out;
    auto* instance=api.call(gm("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Beyond.Gameplay"));
    auto* player=instance?api.call(gm("GameInstance","get_player","","Beyond.Gameplay.GamePlayer","Beyond.Gameplay"),instance):nullptr;
    auto* map=player?readField<void*>(player,"GamePlayer","mapManager","Beyond.Gameplay"):nullptr;
    if(!map)throw std::runtime_error("地图管理器尚未就绪");
    auto* sequence=api.call(gm("MapManager","_EnumerateMarksInMap","System.String","System.Collections.Generic.IEnumerable<Beyond.Gameplay.MarkInstRuntimeData>","Beyond.Gameplay"),map,{api.string(mapName)});
    if(!sequence)return out;
    eflink::unity::Api::TemporaryRoot sequenceRoot(runtime,sequence);
    auto objectClass=symbol<const void*(*)(void*)>("il2cpp_object_get_class");
    auto methodsOf=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
    auto methodName=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
    auto paramCount=symbol<unsigned(*)(const void*)>("il2cpp_method_get_param_count");
    auto find=[&](void* object,const char* suffix)->const void* {
        void* it=nullptr;const void* found=nullptr;const auto n=std::strlen(suffix);
        while(const auto* m=methodsOf(objectClass(object),&it)) {
            const std::string name=methodName(m);
            if(paramCount(m)==0&&name.size()>=n&&name.compare(name.size()-n,n,suffix)==0) {found=m;if(name.find("Generic")!=std::string::npos)break;}
        }
        return found;};
    auto invoke=[&](const void* method,void* self)->void* {BE_ResolvedMethodV1 r{};r.method_info=method;return api.call(r,self);};
    const auto* getEnumerator=find(sequence,"GetEnumerator");if(!getEnumerator)return out;
    auto* enumerator=invoke(getEnumerator,sequence);if(!enumerator)return out;
    eflink::unity::Api::TemporaryRoot enumeratorRoot(runtime,enumerator);
    const auto* moveNext=find(enumerator,"MoveNext");const auto* current=find(enumerator,"get_Current");
    if(!moveNext||!current)throw std::runtime_error("地图标记枚举不可用");
    for(int guard=0;guard<4096;++guard) {
        auto* more=invoke(moveNext,enumerator);if(!more||!*static_cast<bool*>(runtime->object_unbox(runtime->context,more)))break;
        auto* mark=invoke(current,enumerator);
        if(!mark||classOf(mark)!="Beyond.Gameplay.FacMarkInstRuntimeData")continue;
        // 1.6.3 renamed isTpRelated to isTravelRelated (offline metadata, 2026-11-05).
        bool travelHub=false;
        try {travelHub=readField<bool>(mark,"FacMarkInstRuntimeData","isTravelRelated","Beyond.Gameplay");}
        catch(...) {travelHub=readField<bool>(mark,"FacMarkInstRuntimeData","isTpRelated","Beyond.Gameplay");}
        if(!travelHub)continue;
        HubTarget hub;hub.node=readField<std::uint32_t>(mark,"FacMarkInstRuntimeData","nodeId","Beyond.Gameplay");if(!hub.node)continue;
        hub.level=text(readField<void*>(mark,"MarkInstRuntimeData","levelId","Beyond.Gameplay"));
        hub.inst=text(readField<void*>(mark,"MarkInstRuntimeData","instId","Beyond.Gameplay"));
        const auto* position=find(mark,"GetTeleportPosition");const auto* rotation=find(mark,"GetTeleportRotation");
        if(position)if(auto* v=invoke(position,mark))hub.position=*static_cast<V3*>(runtime->object_unbox(runtime->context,v));
        if(rotation)if(auto* v=invoke(rotation,mark))hub.rotation=*static_cast<V3*>(runtime->object_unbox(runtime->context,v));
        out.push_back(std::move(hub));
    }
    return out;
}
// The map screen's official teleport to a hub of this account.
void officialHubTeleport(const std::string& mapName,V3 destination,const char* purpose) {
    const auto hubs=hubTargets(mapName);
    if(hubs.empty())throw std::runtime_error("本账号在 "+mapName+" 没有可传送的枢纽；请先在该地图解锁或建造枢纽");
    const HubTarget* best=&hubs.front();double bestDistance=1e18;
    for(const auto& hub:hubs){const double d=std::hypot(hub.position.x-destination.x,hub.position.z-destination.z);if(d<bestDistance){bestDistance=d;best=&hub;}}
    int reason=1; // C2STeleportReason.ServerGotoHub (observed from the map screen)
    try {reason=enumValue<int>("C2STeleportReason","ServerGotoHub","Beyond.GEnums");}catch(...) {}
    int ui=enumValue<int>("TeleportUIType","Default","Beyond.Gameplay");int options=0;
    V3 position=best->position,rotation=best->rotation;std::uint32_t node=std::uint32_t(best->node);
    api.call(gm("GameAction","TeleportToPosition","Beyond.GEnums.C2STeleportReason|System.String|UnityEngine.Vector3|UnityEngine.Vector3|Beyond.Gameplay.TeleportUIType|Beyond.Gameplay.TeleportOptions|System.Action|System.UInt32","System.Void","Beyond.Gameplay.Actions"),
        nullptr,{&reason,api.string(best->level),&position,&rotation,&ui,&options,nullptr,&node});
    ++officialTeleports;
    officialTeleportStatus={{"purpose",purpose},{"map",mapName},{"hub",best->inst},{"node",best->node},{"level",best->level},
        {"position",{position.x,position.y,position.z}},{"distance_to_target",bestDistance},{"candidates",hubs.size()},{"reason",reason},{"at",nowMs()}};
}
void updateEmergencyHome(Net& net) {
    if(!emergencyHomePending)return;
    if(net.currentRole()!=Role::Offline||travel)return; // wait until out of the room and back home
    emergencyHomePending=false;
    try {
        const V3 here=character?api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character):V3{};
        officialHubTeleport("map01",here,"emergency_home");
    }catch(const std::exception& e) {officialTeleportStatus={{"purpose","emergency_home"},{"error",e.what()},{"at",nowMs()}};}
}
void runTravel() {try {updateTravel();}catch(const std::exception& e) {failTravel(e.what());}}
// Guest side, every replication tick: follow the host's scene, keep home, and
// return home on leave. Host consent gates every map change after entry.
void followTick(Net& net,Session& link) {
    updateEmergencyHome(net);
    const auto role=net.currentRole();
    if(role!=Role::Client||!link.followHost) {
        hostMapConsent.reset();hostMapChoiceMap.clear();hostMapChoiceLevel.clear();followGeneration=followRoom=0;
        // Leaving the host's world returns this player to where they entered from.
        if(home) {travel=std::move(*home);home.reset();travel->until=nowMs()+90000;travelState="returning_home";travelError.clear();}
        else if(travel&&!travel->homeward&&travel->following) {travel.reset();travelState="idle";}
        runTravel();return;
    }
    if(!link.ready()||!character||mapId.empty()) {runTravel();return;}
    const auto& snapshot=link.snapshot();
    const auto room=net.snapshot().roomId;
    if(followRoom!=room) {hostMapConsent.reset();followRoom=room;followGeneration=0;}
    const auto hostMap=snapshot.at("map").get<std::string>(),hostLevel=snapshot.at("level").get<std::string>();
    const auto hostDungeon=snapshot.value("dungeon",std::string{});
    const auto hostPosition=vec(snapshot.at("position"));const int hostLevelNumber=snapshot.value("level_num",0);
    hostMapChoiceMap=hostMap;hostMapChoiceLevel=hostLevel;
    const bool here=hostMap==mapId&&hostDungeon==currentDungeon();
    hostMapConsent.observe(hostMap+"/"+hostLevel+"/"+hostDungeon,here);
    if(hostMapConsent.pending()&&travel&&travel->following&&!travel->started)travel.reset();
    auto beside=hostPosition;beside.x+=1.2f;beside.z+=.6f; // arrive beside the host, not inside them
    if(!followGeneration) {
        // Entering the room: remember home, then go to the host.
        followGeneration=link.generation();
        if(!home) {
            if(travel&&travel->homeward) {home=*travel;home->started=home->observedLoading=false;}
            else if(auto* info=staticObject("worldInfo")) {
                auto p=api.value<V3>(gm("Entity","get_position","","UnityEngine.Vector3"),character);
                home=makeTravel(mapId,levelId,p,api.value<int>(gm("WorldInfo","get_curLevelIdNum","","System.Int32"),info),currentDungeon());
                home->homeward=true;
            }
        }
        if(!hostMapConsent.pending()) {
            travel=makeTravel(hostMap,hostLevel,beside,hostLevelNumber,hostDungeon);travel->following=true;
            travelState="entering_host_world";travelError.clear();
        }
    }
    if(!here) {
        if(hostMapConsent.pending())travelState=hostMapConsent.deferred()?"host_map_follow_deferred":"awaiting_host_map_choice";
        else if(travel&&travel->following&&!travel->started&&!travel->official&&!travel->dungeonStage&&
                (travel->map!=hostMap||travel->dungeon!=hostDungeon)) {
            // The host moved again before this guest set off: retarget.
            travel->map=hostMap;travel->level=hostLevel;travel->position=hostPosition;travel->levelNumber=hostLevelNumber;travel->dungeon=hostDungeon;
            travel->until=nowMs()+90000;
        }else if(!travel&&travelState!="failed") {
            travel=makeTravel(hostMap,hostLevel,hostPosition,hostLevelNumber,hostDungeon);travel->following=true;travelState="following_host";travelError.clear();
        }
    }
    runTravel();
}
