#pragma once
// Room manifests and native-authority transport. Network callbacks only copy
// bounded packets; game-thread adapters own every world operation and result.
#include "link_net.h"
#include "link_coop_contract.h"
#include "link_world_delta.h"
#include "link_profile.h"
#include "link_version.h"
#include "link_presentation.h"
#include "link_presentation_codec.h"
#include "authority/world_authority.h"
#include <optional>

namespace eflink {
using Json=nlohmann::json;
inline const std::uint32_t worldChannel=proto::fnv32("eflink.world.v1");
inline bool validVector(const Json& value) {return worldauthority::vector(value);}
// Scene manifests carry location and a read-only peer roster. Native entities,
// native casts and outcome values travel only through the common object ledger.
inline bool validRoomRoster(const Json& players,const Json& scene) {
    try {
        if(!players.is_array()||players.size()>32)return false;
        std::set<std::uint16_t> seen;
        for(const auto& p:players) {
            if(!worldauthority::exactFields(p,{"peer","model","map","level","in_scene","read_only"})||
               !worldauthority::positive(p.at("peer"))||p.at("peer").get<std::uint64_t>()>65535||
               !seen.insert(p.at("peer").get<std::uint16_t>()).second||!p.at("read_only").is_boolean()||p.at("read_only")!=true||
               !p.at("in_scene").is_boolean())return false;
            for(const char* field:{"model","map","level"})if(!p.at(field).is_string()||p.at(field).get_ref<const std::string&>().size()>(std::string(field)=="model"?256:64))return false;
            if(p.at("in_scene")==true&&(p.at("map")!=scene.at("map")||p.at("level")!=scene.at("level")))return false;
        }
        return true;
    }catch(...) {return false;}
}
inline bool validWorld(const Json& w,bool received=false) {
    try {
        if(!worldauthority::exactFields(w,received?std::initializer_list<const char*>{"map","level","level_num","position","entities","players"}:
                                       std::initializer_list<const char*>{"map","level","level_num","position","entities"})||
           !w.at("map").is_string()||w.at("map").get_ref<const std::string&>().empty()||w.at("map").get_ref<const std::string&>().size()>64||
           !w.at("level").is_string()||w.at("level").get_ref<const std::string&>().empty()||w.at("level").get_ref<const std::string&>().size()>64||
           !validVector(w.at("position"))||!w.at("level_num").is_number_integer()||w.at("level_num").get<std::int64_t>()<0||w.at("level_num").get<std::uint64_t>()>INT32_MAX||
           !w.at("entities").is_array()||!w.at("entities").empty())return false;
        return !received||validRoomRoster(w.at("players"),w);
    }catch(...) {return false;}
}
class Session {
    struct Packet {std::uint16_t from;Json body;std::size_t size;};
    std::mutex inboxMutex;std::deque<Packet> inbox;std::size_t inboxBytes=0;
    std::uint32_t room=0;Role role=Role::Offline;std::uint16_t localPeer=0;
    std::uint64_t epoch=0,revision=0,seenAt=0,lastPublish=0,lastBaseline=0,sentRevision=0;
    Json world=Json::object(),sentWorld;
    bool baselineMissing=false,publishBlocked=false,localSceneReady=false,sceneResetPending=false;
    std::uint64_t baselinesSent=0,deltasSent=0,wireBytes=0,congestionSkipped=0;
    struct Compatibility {bool compatible=false;std::uint64_t seen=0;std::string build;};
    std::map<std::uint16_t,Compatibility> compatibility;std::uint64_t lastHello=0;std::string adapterVersion;
    struct Presentation {Json data;std::uint64_t seen=0;};
    std::map<std::uint16_t,Presentation> presentations;
    std::atomic<std::uint64_t> presentationCoalesced{0};
    std::uint64_t presentationSequence=0,lastPresentation=0,presentationReceived=0,presentationSkipped=0,presentationRawBytes=0,presentationWireBytes=0,presentationPacked=0;
    std::uint64_t rejected=0,received=0,legacyRejected=0;std::string error;
    worldauthority::Host nativeAuthority;worldauthority::Replica nativeReplica;worldauthority::WorldHandoff nativeHandoff;
    worldauthority::NativeAdapter* nativeAdapter=nullptr;
    bool shutdownBarrierAvailable=false;
    std::map<std::uint16_t,std::uint64_t> nativeMembers;Json nativeGrant;std::uint64_t nativeRequest=0;
    std::string worldIncarnation;
    std::uint64_t sentAuthorityRevision=0,lastAuthorityPublish=0,lastAuthorityBaseline=0,authoritySeen=0;bool authoritySent=false;
    std::uint64_t authoritySequence=0,receivedAuthoritySequence=0;
    std::uint64_t authorityBaselines=0,authorityDeltas=0,authorityHeartbeats=0,authorityBytes=0;
    void resetSharedObjects() {
        nativeHandoff.withdraw();
        nativeMembers.clear();nativeGrant=nullptr;nativeRequest=0;
        nativeAuthority.reset();nativeReplica.reset();sentAuthorityRevision=lastAuthorityPublish=lastAuthorityBaseline=authoritySeen=0;authoritySent=false;
        authoritySequence=receivedAuthoritySequence=0;
    }
    bool adapterReady() const {
        return shutdownBarrierAvailable&&nativeAdapter&&!adapterVersion.empty()&&nativeAdapter->gameVersion()==adapterVersion&&
            !nativeAdapter->path().empty()&&nativeAdapter->executionVerified()&&nativeAdapter->isolationVerified()&&
            nativeAdapter->recoveryVerified()&&nativeAdapter->dualClientVerified();
    }
    bool nativeMemberPresent(const Snapshot& snapshot,std::uint16_t peer) const {
        const auto* member=find(snapshot,peer);
        if(!fresh(member)||member->info.mapId!=world.value("map",std::string{})||
           member->info.levelId!=world.value("level",std::string{}))return false;
        return peer==localPeer?role==Role::Host:protocolCompatible(peer);
    }
    bool send(Net& net,Json body,std::uint16_t to=proto::kEveryone) {
        body["schema"]=coopWorldSchema;body["room"]=room;
        const auto plain=body.dump();const bool presentation=body.value("kind",Json{})=="presentation";
        const auto text=presentation?packPresentation(plain,proto::kMaxReliableData):plain;
        if(text.size()>proto::kMaxReliableData) {error="世界消息超过网络上限";return false;}
        const bool sent=net.sendData(worldChannel,to,text.data(),text.size(),true);
        if(sent&&presentation) {presentationRawBytes+=plain.size();presentationWireBytes+=text.size();if(packedPresentation(text.data(),text.size()))++presentationPacked;}
        return sent;
    }
    const PeerView* find(const Snapshot& s,std::uint16_t id) const {for(const auto& p:s.peers)if(p.id==id)return &p;return nullptr;}
    bool fresh(const PeerView* p) const {return p&&p->hasInfo&&!p->states.empty()&&(p->states.back().state.flags&proto::StateValid)&&nowMs()-p->states.back().receivedMs<3000;}
    Json roomRoster(const Snapshot& s,const Json& scene) const {
        Json result=Json::array();
        for(const auto& p:s.peers)result.push_back({{"peer",p.id},{"model",p.info.modelPath},{"map",p.info.mapId},{"level",p.info.levelId},
            {"in_scene",fresh(&p)&&p.info.mapId==scene.at("map").get_ref<const std::string&>()&&p.info.levelId==scene.at("level").get_ref<const std::string&>()},{"read_only",true}});
        return result;
    }
    static bool retired(const std::string& kind) {
        static const std::set<std::string> kinds={"entry_prepare","entry_result","player_cast","skill_intent","hit_claim","hit","player_hit",
            "enemy_state","enemy_cast","resource_baseline","character_growth","character_appearance","location","teleport_request","teleport","teleport_error"};
        return kinds.contains(kind);
    }
public:
    bool followHost=true,combat=true;
    void receive(std::uint16_t from,const void* data,std::size_t size) {
        if(!data||!size||size>proto::kMaxReliableData)return;
        const bool packed=packedPresentation(data,size);std::optional<std::string> decoded;
        if(packed) {decoded=unpackPresentation(data,size,proto::kMaxReliableData);if(!decoded)return;data=decoded->data();size=decoded->size();}
        auto parsed=Json::parse(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+size,nullptr,false);
        if(parsed.is_discarded()||!parsed.is_object()||(packed&&parsed.value("kind",Json{})!="presentation"))return;
        std::lock_guard lock(inboxMutex);
        if(parsed.value("kind",Json{})=="presentation"&&parsed.contains("seq")&&parsed.at("seq").is_number_unsigned()) {
            for(auto it=inbox.begin();it!=inbox.end();) {
                if(it->from==from&&it->body.value("kind",Json{})=="presentation"&&it->body.value("room",Json{})==parsed.value("room",Json{})&&
                   it->body.value("epoch",Json{})==parsed.value("epoch",Json{})&&it->body.value("peer",Json{})==parsed.value("peer",Json{})&&
                   it->body.contains("seq")&&it->body.at("seq").is_number_unsigned()&&it->body.at("seq").get<std::uint64_t>()<parsed.at("seq").get<std::uint64_t>()) {
                    inboxBytes-=it->size;it=inbox.erase(it);++presentationCoalesced;
                }else ++it;
            }
        }
        if(inbox.size()>=128||inboxBytes+size>4u*1024*1024)return;
        inbox.push_back({from,std::move(parsed),size});inboxBytes+=size;
    }
    void update(Net& net) {
        const auto s=net.snapshot();localPeer=s.localId;
        if(s.roomId!=room||s.role!=role) {
            room=s.roomId;role=s.role;epoch=revision=seenAt=lastPublish=0;world=Json::object();sentWorld=nullptr;
            lastBaseline=sentRevision=0;baselineMissing=publishBlocked=sceneResetPending=false;
            presentations.clear();presentationSequence=lastPresentation=0;compatibility.clear();lastHello=0;error.clear();
            resetSharedObjects();worldIncarnation=role==Role::Host?worldauthority::newWorldIncarnation():std::string{};
        }
        for(auto it=compatibility.begin();it!=compatibility.end();)if(!find(s,it->first))it=compatibility.erase(it);else ++it;
        for(auto it=presentations.begin();it!=presentations.end();)if(!find(s,it->first))it=presentations.erase(it);else ++it;
        if((role==Role::Host||role==Role::Client)&&(!lastHello||nowMs()-lastHello>=1000)) {
            if(send(net,{{"kind","coop_hello"},{"world_schema",coopWorldSchema},{"contract_revision",coopContractRevision},{"contract",coopContractName},
                {"build",EFLINK_BUILD_ID},{"game_version",adapterVersion},{"world_incarnation",worldIncarnation}},role==Role::Host?proto::kEveryone:proto::kHostId))lastHello=nowMs();
        }
        std::deque<Packet> pending;{std::lock_guard lock(inboxMutex);pending.swap(inbox);inboxBytes=0;}
        for(const auto& packet:pending)try {
            const auto& b=packet.body;
            if(!room||b.at("schema")!=coopWorldSchema||!worldauthority::positive(b.at("room"))||b.at("room").get<std::uint64_t>()!=room||!find(s,packet.from)) {++rejected;continue;}
            const auto kind=b.at("kind").get<std::string>();
            if(retired(kind)) {++rejected;++legacyRejected;continue;}
            if(kind=="coop_hello") {
                if(packet.from==s.localId||(role==Role::Client&&packet.from!=proto::kHostId)||(role!=Role::Host&&role!=Role::Client)) {++rejected;continue;}
                bool compatible=validCoopHello(b)&&b.at("build")==EFLINK_BUILD_ID&&b.at("game_version")==adapterVersion;
                if(role==Role::Client&&compatible) {
                    auto nonce=b.value("world_incarnation",std::string{});
                    if(nonce.size()!=32||nonce.find_first_not_of("0123456789abcdef")!=std::string::npos||(!worldIncarnation.empty()&&worldIncarnation!=nonce))compatible=false;
                    else worldIncarnation=std::move(nonce);
                }
                compatibility[packet.from]={compatible,nowMs(),b.contains("build")&&b.at("build").is_string()?b.at("build").get<std::string>().substr(0,64):std::string{}};
                if(!compatible) {++rejected;nativeAuthority.revoke(packet.from);nativeMembers.erase(packet.from);if(role==Role::Client)nativeHandoff.withdraw();}
            }else if(kind=="snapshot"||kind=="snapshot_delta") {
                if(role!=Role::Client||packet.from!=proto::kHostId||!worldauthority::positive(b.at("epoch"))||!worldauthority::positive(b.at("revision"))) {++rejected;continue;}
                const auto e=b.at("epoch").get<std::uint64_t>(),r=b.at("revision").get<std::uint64_t>();
                if(e<epoch||(e==epoch&&r<=revision)) {++rejected;continue;}
                Json candidate;
                if(kind=="snapshot_delta") {
                    if(baselineMissing||e!=epoch||b.at("base_revision")!=revision) {baselineMissing=true;nativeHandoff.withdraw();++rejected;continue;}
                    try {candidate=applyWorldDelta(world,b.at("delta"));}catch(...) {baselineMissing=true;nativeHandoff.withdraw();++rejected;continue;}
                }else candidate=b.at("world");
                if(!validWorld(candidate,true)) {if(kind=="snapshot_delta") {baselineMissing=true;nativeHandoff.withdraw();}++rejected;continue;}
                if(e!=epoch) {resetSharedObjects();presentations.clear();}
                epoch=e;revision=r;world=std::move(candidate);seenAt=nowMs();baselineMissing=false;++received;
            }else if(kind=="authority_baseline"||kind=="authority_delta"||kind=="authority_heartbeat") {
                if(b.size()!=7||role!=Role::Client||packet.from!=proto::kHostId||!ready()||!localSceneOperational()||!followHost||!combat||!protocolCompatible(proto::kHostId)||
                   !worldauthority::positive(b.at("seq"))||b.at("seq").get<std::uint64_t>()<=receivedAuthoritySequence||
                   b.at("map")!=world.at("map")||b.at("level")!=world.at("level")) {++rejected;continue;}
                const worldauthority::Scope expected{room,worldIncarnation,epoch};
                if(kind=="authority_heartbeat") {
                    const auto& heartbeat=b.at("heartbeat");
                    if(!expected.valid()||!worldauthority::exactFields(heartbeat,{"scope","revision"})||
                       worldauthority::Scope::parse(heartbeat.at("scope"))!=expected||nativeReplica.scope()!=expected||
                       !heartbeat.at("revision").is_number_unsigned()||nativeReplica.needsBaseline()) {++rejected;continue;}
                    const auto reported=heartbeat.at("revision").get<std::uint64_t>();
                    if(reported!=nativeReplica.version()) {
                        if(reported>nativeReplica.version()) {nativeReplica.requireBaseline();nativeHandoff.withdraw();}
                        ++rejected;continue;
                    }
                }else if(!expected.valid()||(kind=="authority_baseline"?!nativeReplica.baseline(b.at("baseline"),expected,true):
                    worldauthority::Scope::parse(b.at("delta").at("scope"))!=expected||!nativeReplica.delta(b.at("delta"),true))) {++rejected;continue;}
                receivedAuthoritySequence=b.at("seq").get<std::uint64_t>();authoritySeen=nowMs();
            }else if(kind=="authority_grant") {
                if(b.size()!=4||role!=Role::Client||packet.from!=proto::kHostId||!protocolCompatible(proto::kHostId)||!ready()||!localSceneOperational()||!nativeAdapter) {++rejected;continue;}
                const auto& grant=b.at("grant");
                if(!worldauthority::exactFields(grant,{"scope","member","actors"})||worldauthority::Scope::parse(grant.at("scope"))!=worldauthority::Scope{room,worldIncarnation,epoch}||
                   !worldauthority::positive(grant.at("member"))||!grant.at("actors").is_array()||grant.at("actors").empty()||grant.at("actors").size()>4) {++rejected;continue;}
                std::set<std::pair<std::uint64_t,std::uint64_t>> actors;
                for(const auto& value:grant.at("actors")) {auto ref=worldauthority::Ref::parse(value);if(!actors.emplace(ref.id,ref.life).second)throw std::runtime_error("duplicate native party grant");}
                if(!nativeGrant.is_null()&&nativeGrant.at("scope")==grant.at("scope")&&nativeGrant.at("member")!=grant.at("member")) {++rejected;continue;}
                nativeGrant=grant;
            }else if(kind=="world_intent") {
                if(b.size()!=4||role!=Role::Host||!ready()||!localSceneOperational()||!followHost||!combat||!protocolCompatible(packet.from)||!nativeAdapter||!nativeMembers.contains(packet.from)||
                   !nativeMemberPresent(s,packet.from)||
                   !authoritySeen||nowMs()-authoritySeen>1500||!nativeAuthority.resume(*nativeAdapter)||!nativeAuthority.submit(b.at("intent"),packet.from,*nativeAdapter,nowMs()))++rejected;
            }else if(kind=="presentation") {
                const auto owner=b.at("peer").get<std::uint16_t>();const auto* p=find(s,owner);
                if(!ready()||!localSceneOperational()||!protocolCompatible(role==Role::Host?packet.from:proto::kHostId)||
                   (role==Role::Host?packet.from!=owner:role!=Role::Client||packet.from!=proto::kHostId)||owner==s.localId||owner>=8192||!fresh(p)||
                   b.at("epoch")!=epoch||b.at("map")!=world.at("map")||b.at("level")!=world.at("level")||
                   p->info.mapId!=world.at("map").get<std::string>()||p->info.levelId!=world.at("level").get<std::string>()||!validPresentation(b)) {++rejected;continue;}
                auto& prior=presentations[owner];
                if(!prior.data.empty()&&prior.data.value("epoch",std::uint64_t{})==epoch&&b.at("seq").get<std::uint64_t>()<=prior.data.at("seq").get<std::uint64_t>()) {++rejected;continue;}
                bool valid=true;for(const auto& m:b.at("members"))if(!validCharacterGrowth(m.at("growth"),m.at("model").get<std::string>())||
                    (!m.at("appearance").is_null()&&!validCharacterAppearance(m.at("appearance"),m.at("model").get<std::string>())))valid=false;
                if(!valid) {++rejected;continue;}
                prior={b,nowMs()};++presentationReceived;if(role==Role::Host&&net.transmitPressure()<64u*1024)send(net,b);
            }else ++rejected;
        }catch(...) {++rejected;}
        updateNativeAuthority(net,s);
    }
    bool publish(Net& net,Json value,bool force=false) {
        if(role!=Role::Host||!localSceneReady||!validWorld(value))return false;
        if(sceneResetPending||world.empty()||world.value("map",std::string{})!=value.at("map").get<std::string>()||world.value("level",std::string{})!=value.at("level").get<std::string>()) {
            if(epoch==UINT64_MAX)throw std::runtime_error("scene generation exhausted");
            ++epoch;revision=0;presentations.clear();sentWorld=nullptr;lastBaseline=sentRevision=0;sceneResetPending=false;
            resetSharedObjects();nativeAuthority.reset({room,worldIncarnation,epoch},adapterVersion);
        }
        value["players"]=roomRoster(net.snapshot(),value);if(!validWorld(value,true))return false;
        world=std::move(value);seenAt=nowMs();
        if(!force&&net.transmitPressure()>256u*1024) {++congestionSkipped;return true;}
        if(!force&&nowMs()-lastPublish<50)return true;
        if(revision==UINT64_MAX)throw std::runtime_error("scene revision exhausted");
        lastPublish=nowMs();const auto nextRevision=revision+1;
        Json message={{"kind","snapshot"},{"epoch",epoch},{"revision",nextRevision},{"world",world}};bool baseline=true;
        if(!force&&!sentWorld.is_null()&&nowMs()-lastBaseline<1000)try {
            Json incremental={{"kind","snapshot_delta"},{"epoch",epoch},{"revision",nextRevision},{"base_revision",sentRevision},{"delta",worldDelta(sentWorld,world)}};
            if(incremental.dump().size()<message.dump().size()*9/10) {message=std::move(incremental);baseline=false;}
        }catch(...) {}
        const auto bytes=message.dump().size();
        if(!send(net,std::move(message))) {publishBlocked=true;sentWorld=nullptr;nativeAuthority.suspend();return false;}
        publishBlocked=false;revision=sentRevision=nextRevision;sentWorld=world;wireBytes+=bytes;
        if(baseline) {lastBaseline=nowMs();++baselinesSent;}else ++deltasSent;return true;
    }
    void requestTeleport(Net&,std::uint16_t) {throw std::runtime_error("共享世界传送原生隔离与恢复适配未验收");}
    bool publishPresentation(Net& net,Json value) {
        const auto s=net.snapshot();const auto now=nowMs();
        if(!ready()||!localSceneOperational()||now-lastPresentation<50)return false;
        if(net.transmitPressure()>64u*1024){++presentationSkipped;return false;}
        value.update(Json{{"kind","presentation"},{"version",1},{"peer",s.localId},{"epoch",epoch},
            {"map",world.at("map")},{"level",world.at("level")},{"seq",++presentationSequence}});
        if(!validPresentation(value))return false;
        if(send(net,std::move(value),role==Role::Host?proto::kEveryone:proto::kHostId)){lastPresentation=now;return true;}
        return false;
    }
    bool observeNativeObjects(const std::string& kind,const std::vector<worldauthority::Observation>& values) {
        if(role!=Role::Host||!ready()||!localSceneOperational()||!followHost||!combat)return false;
        nativeAuthority.observe(kind,values);authoritySeen=nowMs();return true;
    }
    bool observeNativeFrame(const std::map<std::string,std::vector<worldauthority::Observation>>& frame) {
        if(role!=Role::Host||!ready()||!localSceneOperational()||!followHost||!combat)return false;
        nativeAuthority.observeFrame(frame);authoritySeen=nowMs();return true;
    }
    bool publishAuthority(Net& net,bool force=false) {
        if(role!=Role::Host||!ready()||!localSceneOperational()||!followHost||!combat||!authoritySeen||nowMs()-authoritySeen>1500)return false;
        if(!force&&(nowMs()-lastAuthorityPublish<50||net.transmitPressure()>64u*1024))return false;
        if(authoritySequence==UINT64_MAX) {nativeAuthority.suspend();error="authority packet sequence exhausted";return false;}
        Json message={{"map",world.at("map")},{"level",world.at("level")},{"seq",++authoritySequence}};
        const auto nativeRevision=nativeAuthority.version();
        bool full=force||!authoritySent||nowMs()-lastAuthorityBaseline>=1000;
        bool heartbeat=!full&&nativeRevision==sentAuthorityRevision;
        if(heartbeat) {message["kind"]="authority_heartbeat";message["heartbeat"]={{"scope",nativeAuthority.scope().json()},{"revision",nativeRevision}};}
        else {
            auto delta=full?std::optional<Json>{}:nativeAuthority.changesSince(sentAuthorityRevision);
            if(!delta)full=true;
            if(full) {message["kind"]="authority_baseline";message["baseline"]=nativeAuthority.baseline();}
            else {message["kind"]="authority_delta";message["delta"]=std::move(*delta);}
        }
        const auto bytes=message.dump().size();
        if(!send(net,std::move(message))) {nativeAuthority.suspend();return false;}
        authorityBytes+=bytes;if(full)++authorityBaselines;else if(heartbeat)++authorityHeartbeats;else ++authorityDeltas;
        authoritySent=true;sentAuthorityRevision=nativeRevision;lastAuthorityPublish=nowMs();if(full)lastAuthorityBaseline=nowMs();return true;
    }
    Json sharedObjects() const {
        if(!ready()||!localSceneOperational()||!followHost||!combat||!authoritySeen||nowMs()-authoritySeen>1500)return Json::array();
        return role==Role::Host?nativeAuthority.baseline().at("objects"):nativeReplica.snapshot();
    }
    Json authorityBaseline() const {return nativeAuthority.baseline();}
    std::uint64_t captureNativeResultStamp() {
        if(role!=Role::Host)throw std::runtime_error("native result clock belongs to the host");
        return nativeAuthority.captureNativeStamp();
    }
    // Adapter lifetime must exceed pending executions and retained personal
    // recovery. Detachment cancels host tickets but never discards a snapshot.
    void attachNativeAdapter(worldauthority::NativeAdapter* adapter) {
        if(adapter==nativeAdapter)return;
        if(nativeHandoff.recoveryRetained())throw std::runtime_error("native recovery must finish before replacing its adapter");
        if(adapter&&!shutdownBarrierAvailable)throw std::runtime_error("native adapter requires a host shutdown barrier");
        nativeAuthority.suspend();nativeMembers.clear();nativeGrant=nullptr;
        nativeAdapter=adapter;
    }
    void setShutdownBarrierAvailable(bool value) {
        if(!value&&nativeHandoff.recoveryRetained())throw std::runtime_error("cannot remove native shutdown barrier while recovery is retained");
        shutdownBarrierAvailable=value;
        if(!value)nativeAuthority.suspend();
    }
    // Called on the game thread before the host removes callbacks and hooks.
    // A pending or failed native restore keeps the module active and the gate
    // closed; only observed restoration permits teardown.
    bool prepareNativeShutdown() {
        nativeAuthority.suspend();nativeHandoff.withdraw();
        if(nativeHandoff.phase()==worldauthority::WorldHandoff::Phase::Restoring&&!nativeHandoff.restore())return false;
        return nativeHandoff.phase()==worldauthority::WorldHandoff::Phase::Personal&&!nativeHandoff.recoveryRetained();
    }
    bool grantNativeParty(Net& net,std::uint16_t peer,std::uint64_t member,std::vector<worldauthority::Ref> actors) {
        if(role!=Role::Host||!ready()||!localSceneOperational()||!followHost||!combat||!adapterReady()||
           !authoritySeen||nowMs()-authoritySeen>1500||!nativeMemberPresent(net.snapshot(),peer)||
           !nativeAuthority.resume(*nativeAdapter))return false;
        for(const auto actor:actors) {
            const auto* object=nativeAuthority.find(actor);if(!object||object->kind!="player")return false;
            try {const auto context=nativeAdapter->actorContext(*object);
                if(context.peer!=peer||context.domain!=(peer==localPeer?worldauthority::NativeActorDomain::HostPersonal:worldauthority::NativeActorDomain::RoomProxy))return false;
            }catch(...) {return false;}
        }
        if(auto old=nativeMembers.find(peer);old!=nativeMembers.end()) {
            if(old->second!=member||!nativeAuthority.replaceActors(peer,member,actors))return false;
        }else if(!nativeAuthority.bindGroup(peer,member,actors))return false;
        // Local ownership is granted by the native adapter, exactly as for a
        // remote member. The host never sends itself a network authorization.
        if(peer==localPeer) {nativeMembers[peer]=member;return true;}
        Json refs=Json::array();for(const auto actor:actors)refs.push_back(actor.json());
        Json grant={{"scope",nativeAuthority.scope().json()},{"member",member},{"actors",std::move(refs)}};
        if(!send(net,{{"kind","authority_grant"},{"grant",std::move(grant)}},peer)) {nativeAuthority.revoke(peer);nativeMembers.erase(peer);return false;}
        nativeMembers[peer]=member;return true;
    }
    bool submitNativeIntent(Net& net,worldauthority::Ref actor,worldauthority::Ref target,const std::string& operation,
                            std::uint64_t action,std::uint64_t strike,const Json& input=Json::object()) {
        const worldauthority::Scope scope{room,worldIncarnation,epoch};
        if(role==Role::Host) {
            if(!canAct(localPeer)||!nativeMemberPresent(net.snapshot(),localPeer)||nativeRequest==UINT64_MAX)return false;
            const auto* object=nativeAuthority.find(target);
            if(!object)return false;
            worldauthority::Intent intent{scope,localPeer,nativeMembers.at(localPeer),++nativeRequest,
                action,strike,object->version,actor,target,operation,input};
            // No shortcut to begin(): the host's input uses the same authority
            // validation, ownership, request/strike history and result witness.
            return nativeAuthority.resume(*nativeAdapter)&&
                nativeAuthority.submit(intent.json(),localPeer,*nativeAdapter,nowMs()).has_value();
        }
        if(role!=Role::Client||!ready()||!localSceneOperational()||!followHost||!combat||!nativeAdapter||
           !protocolCompatible(proto::kHostId)||nativeGrant.is_null()||!nativeHandoff.roomOperationAllowed(scope)||
           !authoritySeen||nowMs()-authoritySeen>1500||nativeReplica.needsBaseline()||nativeRequest==UINT64_MAX)return false;
        if(nativeGrant.at("scope")!=scope.json()||std::find(nativeGrant.at("actors").begin(),nativeGrant.at("actors").end(),actor.json())==nativeGrant.at("actors").end())return false;
        const auto objects=nativeReplica.snapshot();const Json* found=nullptr;bool actorLive=false;
        for(const auto& object:objects) {
            if(object.at("ref")==target.json())found=&object;
            if(object.at("ref")==actor.json()&&object.at("kind")=="player"&&!object.at("state").value("dead",false)&&!object.at("state").value("downed",false))actorLive=true;
        }
        if(!found||!actorLive)return false;
        worldauthority::Intent intent{scope,net.snapshot().localId,nativeGrant.at("member").get<std::uint64_t>(),++nativeRequest,
            action,strike,found->at("version").get<std::uint64_t>(),actor,target,operation,input};
        try {worldauthority::Intent::parse(intent.json());}catch(...) {return false;}
        return send(net,{{"kind","world_intent"},{"intent",intent.json()}},proto::kHostId);
    }
private:
    void updateNativeAuthority(Net& net,const Snapshot& s) {
        const bool fresh=ready()&&localSceneOperational()&&followHost&&combat&&authoritySeen&&nowMs()-authoritySeen<=1500;
        if(role==Role::Host) {
            for(auto it=nativeMembers.begin();it!=nativeMembers.end();) {
                if(!nativeMemberPresent(s,it->first)) {nativeAuthority.revoke(it->first);it=nativeMembers.erase(it);}else ++it;
            }
            if(!fresh||!adapterReady()||!nativeAuthority.resume(*nativeAdapter)) {nativeAuthority.suspend();return;}
            nativeAuthority.poll(nowMs());publishAuthority(net);
        }else if(role==Role::Client) {
            const worldauthority::Scope scope{room,worldIncarnation,epoch};
            if(!fresh||!adapterReady()||!protocolCompatible(proto::kHostId)||nativeReplica.needsBaseline())nativeHandoff.withdraw();
            else {
                if(nativeHandoff.phase()==worldauthority::WorldHandoff::Phase::Personal)nativeHandoff.begin(scope,*nativeAdapter,adapterVersion);
                nativeHandoff.admit(nativeReplica,adapterVersion);
            }
            if(nativeHandoff.phase()==worldauthority::WorldHandoff::Phase::Restoring)nativeHandoff.restore();
        }else {nativeHandoff.withdraw();if(nativeHandoff.phase()==worldauthority::WorldHandoff::Phase::Restoring)nativeHandoff.restore();}
    }
public:
    Json presentation(std::uint16_t peer) const {
        auto it=presentations.find(peer);if(it==presentations.end()||nowMs()-it->second.seen>500||it->second.data.value("epoch",std::uint64_t{})!=epoch)return Json{};
        auto result=it->second.data;result["received"]=it->second.seen;return result;
    }
    bool ready() const {return !sceneResetPending&&epoch&&!world.empty()&&nowMs()-seenAt<3000;}
    bool protocolCompatible(std::uint16_t peer) const {auto it=compatibility.find(peer);return it!=compatibility.end()&&it->second.compatible&&nowMs()-it->second.seen<=3500;}
    void setAdapterVersion(std::string version) {
        if(version.size()>64)version.clear();if(version==adapterVersion)return;
        adapterVersion=std::move(version);compatibility.clear();lastHello=0;resetSharedObjects();if(role==Role::Host&&!world.empty())sceneResetPending=true;
    }
    Json protocolStatus() const {
        auto peers=Json::array();for(const auto& [id,entry]:compatibility)peers.push_back({{"peer",id},{"compatible",protocolCompatible(id)},{"build",entry.build},{"age_ms",nowMs()-entry.seen}});
        return {{"world_schema",coopWorldSchema},{"contract_revision",coopContractRevision},{"contract",coopContractName},{"build",EFLINK_BUILD_ID},{"game_version",adapterVersion},
            {"peers",peers},{"compatible",role==Role::Host?!adapterVersion.empty():protocolCompatible(proto::kHostId)}};
    }
    void setLocalSceneReady(bool value) {
        if(localSceneReady==value)return;localSceneReady=value;
        if(!value) {nativeAuthority.suspend();nativeReplica.reset();authoritySeen=0;authoritySent=false;nativeHandoff.withdraw();}
    }
    bool localSceneOperational() const {return localSceneReady;}
    void invalidateNativeScene() {setLocalSceneReady(false);if(role==Role::Host&&!world.empty())sceneResetPending=true;}
    bool canAct(std::uint16_t id) const {
        if(!ready()||publishBlocked||!localSceneReady||!followHost||!combat||!adapterReady()||!authoritySeen||nowMs()-authoritySeen>1500)return false;
        if(role==Role::Client)return (id==0||id==localPeer)&&protocolCompatible(proto::kHostId)&&!nativeGrant.is_null()&&!nativeReplica.needsBaseline()&&
            nativeHandoff.roomOperationAllowed({room,worldIncarnation,epoch});
        return role==Role::Host&&nativeMembers.contains(id)&&(id==localPeer||protocolCompatible(id));
    }
    Json entryStatus() const {
        const bool active=canAct(localPeer);
        std::string phase=role==Role::Offline?"offline":!ready()?"awaiting_snapshot":active?"active":"native_adapter_incomplete";
        if(role==Role::Client&&!protocolCompatible(proto::kHostId))phase="negotiating_or_incompatible_protocol";
        else if(baselineMissing)phase="awaiting_full_baseline";
        else if(publishBlocked)phase="host_transport_blocked";
        else if(role!=Role::Offline&&!localSceneReady)phase="local_scene_loading";
        return {{"phase",phase},{"active",active},{"local_scene_ready",localSceneReady},{"official_instance_shared",false},{"account_state_authority","official_server"},
            {"stage1_verified",false},{"account_risk",true},{"legacy_entry_removed",true},{"blockers",active?Json::array():Json::array({"native_execution_isolation_recovery_unverified"})},
            {"players",world.value("players",Json::array())},{"handoff",nativeHandoff.status()}};
    }
    const Json& snapshot() const {return world;}
    std::uint64_t snapshotTime() const {return seenAt;}
    std::uint64_t worldRevision() const {return revision;}
    std::uint64_t generation() const {return epoch;}
    void failure(std::string value) {error=std::move(value);}
    Json status() const {
        return {{"schema",coopWorldSchema},{"protocol",protocolStatus()},{"epoch",epoch},{"revision",revision},{"ready",ready()},
            {"map",world.value("map",std::string{})},{"level",world.value("level",std::string{})},{"entities",sharedObjects().size()},{"follow_host",followHost},{"combat",combat},
            {"appearance_profiles",0},{"appearances",Json::array()},{"appearances_received",0},
            {"transport",{{"baselines_sent",baselinesSent},{"deltas_sent",deltasSent},{"world_bytes",wireBytes},{"baseline_missing",baselineMissing},{"publish_blocked",publishBlocked},{"congestion_skipped",congestionSkipped}}},
            {"presentation",{{"received",presentationReceived},{"raw_bytes",presentationRawBytes},{"wire_bytes",presentationWireBytes},{"compressed_frames",presentationPacked},
                {"congestion_skipped",presentationSkipped},{"coalesced",presentationCoalesced.load()},{"peers",presentations.size()},{"read_only",true},{"native_skill_replay",false}}},
            {"player_casts",{{"accepted",0},{"active",Json::array()},{"legacy_removed",true},{"replay_enabled",false}}},
            {"hit_claims",{{"accepted",0},{"legacy_removed",true},{"legacy_packets_rejected",legacyRejected},{"native_verification_required",true}}},
            {"world_authority",{{"host",nativeAuthority.status()},{"observed",sharedObjects().size()},{"replica_revision",nativeReplica.version()},{"handoff",nativeHandoff.status()},
                {"baseline_required",nativeReplica.needsBaseline()},{"age_ms",authoritySeen?nowMs()-authoritySeen:0},
                {"host_shutdown_barrier",shutdownBarrierAvailable},{"operation_adapter_verified",adapterReady()},
                {"transport",{{"baselines_sent",authorityBaselines},{"deltas_sent",authorityDeltas},{"heartbeats_sent",authorityHeartbeats},
                    {"bytes_sent",authorityBytes},{"last_sent_sequence",authoritySequence},{"last_received_sequence",receivedAuthoritySequence}}},
                {"replica_adapter_verified",adapterReady()&&nativeAdapter->dualClientVerified()},{"full_sync_verified",false},{"account_risk",true}}},
            {"age_ms",seenAt?nowMs()-seenAt:0},{"snapshots_received",received},{"skill_intents_accepted",0},{"hits_accepted",0},{"rejected",rejected},{"entry",entryStatus()},{"error",error}};
    }
};
}
