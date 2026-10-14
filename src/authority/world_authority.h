#pragma once
// Pure room rules. Only the native adapter supplies observations and execution
// witnesses; packets never contain pointers, native IDs or account identifiers.
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace eflink::worldauthority {
using Json=nlohmann::json;
inline bool positive(const Json& v) {
    return (v.is_number_unsigned()&&v.get<std::uint64_t>()>0)||
        (v.is_number_integer()&&!v.is_number_unsigned()&&v.get<std::int64_t>()>0);
}
inline bool exactFields(const Json& value,std::initializer_list<const char*> fields) {
    if(!value.is_object()||value.size()!=fields.size())return false;
    for(auto name:fields)if(!value.contains(name))return false;
    return true;
}
inline bool vector(const Json& v,std::size_t count=3) {
    if(!v.is_array()||v.size()!=count)return false;
    double length=0;
    for(const auto& n:v) {
        if(!n.is_number())return false;
        auto f=n.get<double>();if(!std::isfinite(f)||std::abs(f)>1000000)return false;
        length+=f*f;
    }
    return count!=4||(length>=.5&&length<=1.5);
}
inline bool knownKind(const std::string& kind) {
    static const std::set<std::string> kinds={"player","enemy","mineral","mechanism","destructible","skill","projectile","summon","effect","wave","drop_display"};
    return kinds.contains(kind);
}
// One public schema for every object. New fields require explicit validation;
// adapters must translate native state instead of dumping private objects.
inline bool validState(const Json& state) {
    if(!state.is_object()||state.empty()||state.size()>20||state.dump().size()>4096)return false;
    for(auto it=state.begin();it!=state.end();++it) {
        const auto& k=it.key();const auto& v=it.value();
        if(k=="position") {if(!vector(v))return false;}
        else if(k=="rotation") {if(!vector(v,4))return false;}
        else if(k=="template"||k=="phase"||k=="animation") {if(!v.is_string()||v.get_ref<const std::string&>().size()>256)return false;}
        else if(k=="active"||k=="dead"||k=="collected"||k=="downed") {if(!v.is_boolean())return false;}
        else if(k=="hp"||k=="max_hp"||k=="poise"||k=="max_poise") {
            if(!v.is_number()||!std::isfinite(v.get<double>())||v.get<double>()<0||v.get<double>()>1e12)return false;
        } else if(k=="hp_ratio"||k=="max_hp_ratio"||k=="stage"||k=="wave") {
            if(!v.is_number_integer()||v.get<std::int64_t>()<0||v.get<std::uint64_t>()>100000000)return false;
        } else return false;
    }
    for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"hp","max_hp"},{"poise","max_poise"},{"hp_ratio","max_hp_ratio"}})
        if(state.contains(pair.first)&&state.contains(pair.second)&&
           (state.at(pair.second).get<double>()<=0||state.at(pair.first).get<double>()>state.at(pair.second).get<double>()))return false;
    return true;
}
struct Scope {
    std::uint32_t room=0;std::string world;std::uint64_t epoch=0;
    bool operator==(const Scope&) const=default;
    bool valid() const {
        return room&&epoch&&world.size()==32&&world.find_first_not_of("0123456789abcdef")==std::string::npos;
    }
    Json json() const {return {{"room",room},{"world",world},{"epoch",epoch}};}
    static Scope parse(const Json& j) {
        if(!exactFields(j,{"room","world","epoch"})||!positive(j.at("room"))||j.at("room").get<std::uint64_t>()>UINT32_MAX||!positive(j.at("epoch")))throw std::runtime_error("invalid world scope");
        Scope s{j.at("room").get<std::uint32_t>(),j.at("world").get<std::string>(),j.at("epoch").get<std::uint64_t>()};
        if(!s.valid())throw std::runtime_error("invalid world incarnation");return s;
    }
};
inline std::string newWorldIncarnation() {
    std::random_device random;std::string value;value.reserve(32);
    for(unsigned i=0;i<16;++i){const auto b=static_cast<unsigned>(random())&255;value.push_back("0123456789abcdef"[b>>4]);value.push_back("0123456789abcdef"[b&15]);}
    return value;
}
struct Ref {
    std::uint64_t id=0,life=0;bool operator==(const Ref&) const=default;
    Json json() const {return {{"id",id},{"life",life}};}
    static Ref parse(const Json& j) {
        if(!exactFields(j,{"id","life"})||!positive(j.at("id"))||!positive(j.at("life")))throw std::runtime_error("invalid object reference");
        return {j.at("id").get<std::uint64_t>(),j.at("life").get<std::uint64_t>()};
    }
};
inline bool relationName(const std::string& name) {
    static const std::set<std::string> names={"owner","caster","parent","target","controller","origin"};
    return names.contains(name);
}
inline bool validRelations(const Json& value) {
    try {
        if(!value.is_object()||value.size()>6)return false;
        for(auto it=value.begin();it!=value.end();++it) {if(!relationName(it.key()))return false;Ref::parse(it.value());}
        return true;
    }catch(...) {return false;}
}
// Private native anchors, resolved only after all kinds in a frame have been
// enumerated. Cookies/lifecycle numbers never appear in public relations.
struct NativeRelation {
    std::string kind,key;std::uint64_t cookie=0,nativeLife=1;
};
using NativeRelations=std::map<std::string,NativeRelation>;
struct Observation {
    std::string key,kind;Json state;std::uint64_t cookie=0,nativeLife=1;NativeRelations relations;
};
struct Object {
    Ref ref;std::string key,kind;Json state;std::uint64_t cookie=0,version=0,nativeStamp=0,nativeLife=1;
    NativeRelations nativeRelations;Json relations=Json::object();
    Json json() const {return {{"ref",ref.json()},{"key",key},{"kind",kind},{"version",version},{"state",state},{"relations",relations}};}
    static bool validWire(const Json& j) {
        try {
            Ref::parse(j.at("ref"));
            return exactFields(j,{"ref","key","kind","version","state","relations"})&&positive(j.at("version"))&&j.at("key").is_string()&&
                !j.at("key").get_ref<const std::string&>().empty()&&j.at("key").get_ref<const std::string&>().size()<=160&&
                knownKind(j.at("kind").get<std::string>())&&validState(j.at("state"))&&validRelations(j.at("relations"));
        }catch(...) {return false;}
    }
};
inline bool validGraph(const std::map<std::uint64_t,Json>& objects) {
    try {
        for(const auto& [id,object]:objects)for(auto it=object.at("relations").begin();it!=object.at("relations").end();++it) {
            const auto ref=Ref::parse(it.value());auto target=objects.find(ref.id);
            if(target==objects.end()||target->second.at("ref")!=ref.json()||(it.key()=="parent"&&ref.id==id))return false;
            if(it.key()=="caster") {
                const auto kind=target->second.at("kind").get<std::string>();
                if(kind!="player"&&kind!="enemy"&&kind!="summon")return false;
            }
        }
        // Parentage is a forest. Iterative traversal avoids stack exhaustion
        // from a hostile or malformed baseline with a long dependency chain.
        std::set<std::uint64_t> finished;
        for(const auto& [id,object]:objects) {
            std::set<std::uint64_t> active;auto cursor=id;
            while(!finished.contains(cursor)) {
                if(!active.insert(cursor).second)return false;
                const auto& links=objects.at(cursor).at("relations");
                if(!links.contains("parent"))break;
                cursor=Ref::parse(links.at("parent")).id;
            }
            finished.insert(active.begin(),active.end());
        }
        return true;
    }catch(...) {return false;}
}
struct Intent {
    Scope scope;std::uint16_t peer=0;std::uint64_t member=0,request=0,action=0,strike=0,expectedVersion=0;
    Ref actor,target;std::string operation;Json input=Json::object();
    // These are input controls, never outcomes, native IDs or executable names.
    // The native adapter resolves a skill/interaction from the host's actor.
    static bool validInput(const std::string& op,const Json& value) {
        if(!value.is_object()||value.dump().size()>256)return false;
        auto smallInteger=[](const Json& n,unsigned maximum) {
            return n.is_number_integer()&&(n.is_number_unsigned()||n.get<std::int64_t>()>=0)&&n.get<std::uint64_t>()<=maximum;
        };
        if(op=="mine"||op=="interrupt"||op=="revive"||op=="select")return value.empty();
        if(op=="attack")return exactFields(value,{"button","phase"})&&
            (value.at("button")=="primary"||value.at("button")=="secondary")&&
            (value.at("phase")=="press"||value.at("phase")=="release");
        if(op=="cast")return exactFields(value,{"slot","phase"})&&smallInteger(value.at("slot"),3)&&
            (value.at("phase")=="press"||value.at("phase")=="release");
        if(op=="interact")return exactFields(value,{"choice"})&&smallInteger(value.at("choice"),31);
        if(op=="move") {
            if(!exactFields(value,{"axes","heading","mode","duration_ms"})||!vector(value.at("axes"),2)||
               !value.at("heading").is_number()||!std::isfinite(value.at("heading").get<double>())||
               std::abs(value.at("heading").get<double>())>3.141592653589793||
               (value.at("mode")!="walk"&&value.at("mode")!="run")||!positive(value.at("duration_ms"))||
               !smallInteger(value.at("duration_ms"),50))return false;
            const auto x=value.at("axes")[0].get<double>(),y=value.at("axes")[1].get<double>();
            return x*x+y*y<=1.000001;
        }
        return false;
    }
    Json json() const {return {{"scope",scope.json()},{"peer",peer},{"member",member},{"request",request},{"action",action},
        {"strike",strike},{"actor",actor.json()},{"target",target.json()},{"expected_version",expectedVersion},{"operation",operation},{"input",input}};}
    static Intent parse(const Json& j) {
        if(!exactFields(j,{"scope","peer","member","request","action","strike","actor","target","expected_version","operation","input"}))throw std::runtime_error("intent contains outcome or unsupported fields");
        for(auto name:{"peer","member","request","action","strike","expected_version"})if(!positive(j.at(name)))throw std::runtime_error("invalid intent identity");
        if(j.at("peer").get<std::uint64_t>()>65535)throw std::runtime_error("invalid sender");
        auto op=j.at("operation").get<std::string>();
        if(!validInput(op,j.at("input")))throw std::runtime_error("unsupported operation or input controls");
        return {Scope::parse(j.at("scope")),j.at("peer").get<std::uint16_t>(),j.at("member").get<std::uint64_t>(),
            j.at("request").get<std::uint64_t>(),j.at("action").get<std::uint64_t>(),j.at("strike").get<std::uint64_t>(),
            j.at("expected_version").get<std::uint64_t>(),Ref::parse(j.at("actor")),Ref::parse(j.at("target")),op,j.at("input")};
    }
};
inline bool validReceipt(const Json& value) {
    if(value.is_null())return true;
    if(!exactFields(value,{"peer","member","request","action","strike"}))return false;
    for(auto field:{"peer","member","request","action","strike"})if(!positive(value.at(field)))return false;
    return value.at("peer").get<std::uint64_t>()<=65535;
}
struct Ticket {
    Scope scope;std::uint64_t serial=0,actorCookie=0,targetCookie=0,beforeVersion=0;Intent intent;std::uint64_t beginStamp=0;
    std::string path;
};
struct Witness {
    // Adapter must associate the native callback with THIS execution. A generic
    // later census or function return cannot produce a witness. The private
    // capture stamp is issued at the actual callback, never when poll runs.
    // Event nonces are monotonically issued within a compiled adapter path;
    // they are not per-caster skill IDs or client-supplied hit identifiers.
    std::uint64_t serial=0,actorCookie=0,targetCookie=0,event=0;Json state;std::uint64_t nativeStamp=0;
};
class NativeAdapter {
public:
    virtual ~NativeAdapter()=default;
    virtual std::string gameVersion() const=0;
    virtual std::string path() const=0;
    virtual bool executionVerified() const {return false;}
    virtual bool isolationVerified() const {return false;}
    virtual bool recoveryVerified() const {return false;}
    virtual bool dualClientVerified() const {return false;}
    virtual bool supports(const std::string&,const std::string&) const {return false;}
    virtual bool validate(const Intent&,const Object&,const Object&) const {return false;}
    virtual bool begin(const Ticket&) {return false;}
    virtual std::optional<Witness> poll(const Ticket&) {return {};}
    virtual void cancel(const Ticket&) noexcept {}
    // Personal snapshot handles stay private to the native adapter. Room JSON
    // and guest messages cannot supply a handle or assert restoration success.
    virtual std::optional<std::uint64_t> capturePersonal() {return {};}
    virtual bool personalRetained(std::uint64_t) const {return false;}
    virtual bool beginShared(std::uint64_t,const Scope&) {return false;}
    virtual bool nativeInitialized(const Scope&) const {return false;}
    virtual bool applyBaseline(const Scope&,std::uint64_t,const Json&) {return false;}
    virtual bool baselineObserved(const Scope&,std::uint64_t) const {return false;}
    virtual void suspendShared(const Scope&) noexcept {}
    virtual bool beginRestore(std::uint64_t) {return false;}
    virtual bool restoreObserved(std::uint64_t) const {return false;}
    virtual void releasePersonal(std::uint64_t) noexcept {}
};
class Host {
    struct Member {std::uint64_t incarnation=0,highRequest=0;std::vector<Ref> actors;bool active=false;};
    struct Pending {Ticket ticket;NativeAdapter* adapter=nullptr;std::uint64_t deadline=0;};
    Scope scope_;std::string gameVersion_;std::thread::id owner;
    std::map<std::string,Object> objects;
    std::map<std::uint16_t,Member> members;
    std::map<std::uint64_t,Pending> pending;
    using Strike=std::tuple<std::uint16_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>;
    std::map<Strike,std::uint64_t> strikes;std::deque<Strike> strikeOrder;
    using ActorCommand=std::tuple<std::uint16_t,std::uint64_t,std::uint64_t,std::uint64_t>;
    std::map<ActorCommand,std::uint64_t> retiredActions;
    struct EventWindow {std::uint64_t high=0;std::set<std::uint64_t> seen;};
    std::map<std::string,EventWindow> nativeEvents;
    std::deque<Json> journal;
    std::uint64_t nextObject=0,nextLife=0,nextTicket=0,nextNativeStamp=0,revision=0,accepted=0,rejected=0,confirmed=0,expired=0;
    bool suspended=true;
    void thread() {
        if(owner==std::thread::id{})owner=std::this_thread::get_id();
        if(owner!=std::this_thread::get_id())throw std::runtime_error("authority mutation outside game thread");
    }
    static void advance(std::uint64_t& n) {if(n==UINT64_MAX)throw std::runtime_error("authority identity exhausted");++n;}
    static std::string index(const std::string& kind,const std::string& key) {return kind+"/"+key;}
    static ActorCommand actorCommand(const Intent& intent) {return {intent.peer,intent.member,intent.actor.id,intent.actor.life};}
    bool commandRetired(const Intent& intent) const {
        auto old=retiredActions.find(actorCommand(intent));return old!=retiredActions.end()&&intent.action<=old->second;
    }
    void rememberStrike(const Strike& strike,std::uint64_t request) {
        while(strikeOrder.size()>=8192) {
            const auto old=strikeOrder.front();strikeOrder.pop_front();strikes.erase(old);
            const ActorCommand actor{std::get<0>(old),std::get<1>(old),std::get<2>(old),std::get<3>(old)};
            retiredActions[actor]=(std::max)(retiredActions[actor],std::get<4>(old));
        }
        strikes[strike]=request;strikeOrder.push_back(strike);
    }
    bool repeatedEvent(const std::string& path,std::uint64_t event) const {
        auto found=nativeEvents.find(path);if(found==nativeEvents.end())return nativeEvents.size()>=16;
        const auto& window=found->second;
        return window.seen.contains(event)||(window.high>8192&&event<=window.high-8192);
    }
    void rememberEvent(const std::string& path,std::uint64_t event) {
        auto& window=nativeEvents[path];window.high=(std::max)(window.high,event);window.seen.insert(event);
        while(!window.seen.empty()&&window.high>8192&&*window.seen.begin()<=window.high-8192)window.seen.erase(window.seen.begin());
    }
    void record(const std::string& type,const Object& object,Json receipt=nullptr) {
        advance(revision);Json event={{"scope",scope_.json()},{"revision",revision},{"type",type},{"object",object.json()},{"receipt",std::move(receipt)}};
        journal.push_back(std::move(event));while(journal.size()>256)journal.pop_front();
    }
    void cancelAll() noexcept {for(auto& [id,p]:pending)p.adapter->cancel(p.ticket);pending.clear();}
    static bool owns(const Member& member,Ref actor) {return std::find(member.actors.begin(),member.actors.end(),actor)!=member.actors.end();}
    bool ownedGroup(std::uint16_t peer,std::vector<Ref>& actors) const {
        if(!peer||actors.empty()||actors.size()>4)return false;
        std::sort(actors.begin(),actors.end(),[](Ref a,Ref b){return std::tie(a.id,a.life)<std::tie(b.id,b.life);});
        if(std::adjacent_find(actors.begin(),actors.end())!=actors.end())return false;
        for(const auto actor:actors) {
            const auto* native=find(actor);if(!native||native->kind!="player")return false;
            for(const auto& [other,m]:members)if(other!=peer&&m.active&&owns(m,actor))return false;
        }
        return true;
    }
    bool usable(const Intent& intent) const {
        auto m=members.find(intent.peer);const auto* actor=find(intent.actor);const auto* target=find(intent.target);
        return !suspended&&intent.scope==scope_&&m!=members.end()&&m->second.active&&m->second.incarnation==intent.member&&
            owns(m->second,intent.actor)&&actor&&actor->kind=="player"&&target&&intent.expectedVersion>0&&target->version>=intent.expectedVersion&&
            (intent.operation!="move"||intent.target==intent.actor)&&
            (intent.operation!="select"||(target->kind=="player"&&owns(m->second,intent.target)))&&
            !actor->state.value("dead",false)&&!actor->state.value("downed",false)&&!target->state.value("collected",false)&&
            target->state.value("active",true);
    }
    void invalidatePending() {
        for(auto it=retiredActions.begin();it!=retiredActions.end();) {
            const Ref actor{std::get<2>(it->first),std::get<3>(it->first)};
            auto member=members.find(std::get<0>(it->first));
            if(!find(actor)||member==members.end()||member->second.incarnation!=std::get<1>(it->first))it=retiredActions.erase(it);else ++it;
        }
        for(auto it=pending.begin();it!=pending.end();) {
            const auto& t=it->second.ticket;const auto* a=find(t.intent.actor);const auto* o=find(t.intent.target);
            auto m=members.find(t.intent.peer);
            if(t.scope!=scope_||!a||!o||a->cookie!=t.actorCookie||o->cookie!=t.targetCookie||
               m==members.end()||!m->second.active||m->second.incarnation!=t.intent.member||!owns(m->second,t.intent.actor)) {
                it->second.adapter->cancel(t);it=pending.erase(it);
            }else ++it;
        }
    }
public:
    static constexpr std::size_t maxObjects=1024,maxWire=240*1024;
    Host()=default;Host(const Host&)=delete;Host& operator=(const Host&)=delete;
    // Adapter lifetime must exceed all pending tickets. Reset before destruction.
    void reset(Scope scope={},std::string gameVersion={}) {
        thread();cancelAll();scope_=std::move(scope);gameVersion_=std::move(gameVersion);objects.clear();members.clear();
        strikes.clear();strikeOrder.clear();retiredActions.clear();nativeEvents.clear();journal.clear();revision=0;suspended=true;
        // Allocators survive room/scene resets, including reuse of a network ID.
    }
    const Scope& scope() const {return scope_;}
    // Native adapters call this on the game thread at capture time. A delayed
    // callback may acknowledge its ticket, but cannot roll back a newer census.
    // This clock and native cookies are private and never accepted from JSON.
    std::uint64_t captureNativeStamp() {thread();advance(nextNativeStamp);return nextNativeStamp;}
    const Object* find(Ref ref) const {for(const auto& [key,o]:objects)if(o.ref==ref)return &o;return nullptr;}
    const Object* find(const std::string& kind,const std::string& key) const {auto it=objects.find(index(kind,key));return it==objects.end()?nullptr:&it->second;}
    Json baseline() const {
        Json list=Json::array();for(const auto& [key,o]:objects)list.push_back(o.json());
        std::sort(list.begin(),list.end(),[](const Json& a,const Json& b){return a.at("ref").at("id").get<std::uint64_t>()<b.at("ref").at("id").get<std::uint64_t>();});
        return {{"scope",scope_.json()},{"revision",revision},{"objects",std::move(list)}};
    }
    // Complete censuses for the named kinds, committed as ONE native frame.
    // Unmentioned kinds are retained. Dependencies must resolve to actual live
    // allocations in the candidate graph, never guessed native/entity numbers.
    void observeFrame(const std::map<std::string,std::vector<Observation>>& frame) {
        thread();if(!scope_.valid()||frame.empty()||frame.size()>11)throw std::runtime_error("unavailable native frame");
        const auto stamp=captureNativeStamp();auto candidate=objects;auto id=nextObject,life=nextLife;
        for(const auto& [kind,observations]:frame) {
            if(!knownKind(kind)||observations.size()>maxObjects)throw std::runtime_error("unavailable native census");
            std::set<std::string> seen;
            for(const auto& v:observations) {
                if(v.kind!=kind||v.key.empty()||v.key.size()>160||!v.cookie||!v.nativeLife||!validState(v.state)||
                   v.relations.size()>6||!seen.insert(index(kind,v.key)).second)throw std::runtime_error("invalid or ambiguous native identity");
                for(const auto& [name,target]:v.relations)if(!relationName(name)||!knownKind(target.kind)||target.key.empty()||target.key.size()>160||!target.cookie||!target.nativeLife)
                    throw std::runtime_error("invalid native relation anchor");
                const auto key=index(kind,v.key);auto it=candidate.find(key);
                if(it!=candidate.end()&&it->second.cookie==v.cookie&&v.nativeLife<it->second.nativeLife)throw std::runtime_error("native lifecycle observation regressed");
                if(it==candidate.end()||it->second.cookie!=v.cookie||it->second.nativeLife!=v.nativeLife) {
                    advance(id);advance(life);candidate[key]=Object{{id,life},v.key,kind,v.state,v.cookie,1,stamp,v.nativeLife,v.relations};
                }else {it->second.nativeStamp=stamp;it->second.state=v.state;it->second.nativeRelations=v.relations;}
            }
            for(auto it=candidate.begin();it!=candidate.end();)if(it->second.kind==kind&&!seen.contains(it->first))it=candidate.erase(it);else ++it;
        }
        if(candidate.size()>maxObjects)throw std::runtime_error("authority capacity exhausted");
        std::map<std::uint64_t,Json> graph;
        for(auto& [key,o]:candidate) {
            o.relations=Json::object();
            for(const auto& [name,target]:o.nativeRelations) {
                auto resolved=candidate.find(index(target.kind,target.key));
                if(resolved==candidate.end()||resolved->second.cookie!=target.cookie||resolved->second.nativeLife!=target.nativeLife)
                    throw std::runtime_error("native provenance target is unavailable or recycled; whole frame retained");
                o.relations[name]=resolved->second.ref.json();
            }
            graph.emplace(o.ref.id,o.json());
        }
        if(!validGraph(graph))throw std::runtime_error("invalid native dependency graph");
        std::vector<std::pair<std::string,Object>> events;
        for(const auto& [key,o]:objects) {auto next=candidate.find(key);if(next==candidate.end()||next->second.ref!=o.ref)events.emplace_back("retire",o);}
        for(auto& [key,o]:candidate) {
            auto old=objects.find(key);
            if(old==objects.end()||old->second.ref!=o.ref)events.emplace_back("upsert",o);
            else if(o.state!=old->second.state||o.relations!=old->second.relations) {advance(o.version);events.emplace_back("upsert",o);}
        }
        if(events.size()>UINT64_MAX-revision)throw std::runtime_error("authority revision exhausted");
        Json list=Json::array();for(const auto& [key,o]:candidate)list.push_back(o.json());
        if(list.dump().size()>maxWire-512)throw std::runtime_error("native baseline exceeds transport budget");
        objects=std::move(candidate);nextObject=id;nextLife=life;
        for(const auto& [type,o]:events)record(type,o);invalidatePending();
    }
    void observe(const std::string& kind,const std::vector<Observation>& observations) {observeFrame({{kind,observations}});}
    bool bind(std::uint16_t peer,std::uint64_t incarnation,Ref actor) {
        return bindGroup(peer,incarnation,{actor});
    }
    bool bindGroup(std::uint16_t peer,std::uint64_t incarnation,std::vector<Ref> actors) {
        thread();if(!incarnation||!ownedGroup(peer,actors))return false;
        auto it=members.find(peer);
        if(it!=members.end()) {
            if(incarnation<it->second.incarnation)return false;
            if(incarnation==it->second.incarnation)return it->second.actors==actors&&it->second.active;
        }
        members[peer]={incarnation,0,std::move(actors),true};invalidatePending();return true;
    }
    // Host-native party replacement is separate from membership admission.
    // It preserves request deduplication and revokes tickets for removed actors.
    // There is no room JSON/RPC entry point for granting arbitrary ownership.
    bool replaceActors(std::uint16_t peer,std::uint64_t incarnation,std::vector<Ref> actors) {
        thread();auto member=members.find(peer);
        if(member==members.end()||!member->second.active||member->second.incarnation!=incarnation||!ownedGroup(peer,actors))return false;
        member->second.actors=std::move(actors);invalidatePending();return true;
    }
    void revoke(std::uint16_t peer) {thread();auto it=members.find(peer);if(it!=members.end())it->second.active=false;invalidatePending();}
    void suspend() {thread();suspended=true;cancelAll();}
    // Admission can be opened only for a compiled adapter matching the native
    // build and proven isolation/recovery. Observation support grants nothing.
    bool resume(const NativeAdapter& adapter) {
        thread();suspended=!(scope_.valid()&&!gameVersion_.empty()&&adapter.gameVersion()==gameVersion_&&!adapter.path().empty()&&
            adapter.path().size()<=128&&adapter.executionVerified()&&adapter.isolationVerified()&&adapter.recoveryVerified());return !suspended;
    }
    std::optional<std::uint64_t> submit(const Json& body,std::uint16_t sender,NativeAdapter& adapter,std::uint64_t now) {
        thread();try {
            Intent intent=Intent::parse(body);
            if(sender!=intent.peer||!usable(intent)||pending.size()>=128||commandRetired(intent)||!adapter.executionVerified()||!adapter.isolationVerified()||
                !adapter.recoveryVerified()||adapter.gameVersion()!=gameVersion_||adapter.path().empty()||adapter.path().size()>128) {++rejected;return {};}
            auto& m=members.at(sender);const auto* actor=find(intent.actor);const auto* target=find(intent.target);
            Strike strike{sender,intent.member,intent.actor.id,intent.actor.life,intent.action,intent.target.id,intent.target.life,intent.strike};
            if(intent.request<=m.highRequest||strikes.contains(strike)||!adapter.supports(target->kind,intent.operation)||!adapter.validate(intent,*actor,*target)) {++rejected;return {};}
            advance(nextTicket);Ticket ticket{scope_,nextTicket,actor->cookie,target->cookie,target->version,intent,captureNativeStamp(),adapter.path()};
            m.highRequest=intent.request;rememberStrike(strike,intent.request);
            // Making room can retire this very old action. Consuming its new
            // request identity must never dispatch it across that boundary.
            if(commandRetired(intent)) {++rejected;return {};}
            if(now>UINT64_MAX-1500) {++rejected;return {};}
            pending.emplace(ticket.serial,Pending{ticket,&adapter,now+1500});
            // Consume request identity even when native dispatch fails: an
            // ambiguous failure must not be retried as a second native action.
            bool began=false;try {began=adapter.begin(ticket);}catch(...) {}
            if(!began) {adapter.cancel(ticket);pending.erase(ticket.serial);++rejected;return {};}
            ++accepted;return ticket.serial;
        }catch(...) {++rejected;return {};}
    }
    void poll(std::uint64_t now) {
        thread();invalidatePending();
        for(auto it=pending.begin();it!=pending.end();) {
            auto& p=it->second;const auto& t=p.ticket;
            if(!p.adapter->executionVerified()||!p.adapter->isolationVerified()||!p.adapter->recoveryVerified()||p.adapter->gameVersion()!=gameVersion_||p.adapter->path()!=t.path) {
                p.adapter->cancel(t);it=pending.erase(it);++rejected;continue;
            }
            if(now>=p.deadline) {p.adapter->cancel(t);it=pending.erase(it);++expired;continue;}
            std::optional<Witness> witness;try {witness=p.adapter->poll(t);}catch(...) {}
            if(!witness) {++it;continue;}
            auto* existing=find(t.intent.target);
            if(witness->serial!=t.serial||witness->actorCookie!=t.actorCookie||witness->targetCookie!=t.targetCookie||!witness->event||
                !validState(witness->state)||!existing||repeatedEvent(t.path,witness->event)||
                !witness->nativeStamp||witness->nativeStamp<=t.beginStamp||witness->nativeStamp>nextNativeStamp||
                (witness->nativeStamp==existing->nativeStamp&&witness->state!=existing->state)||
                revision==UINT64_MAX) {
                ++rejected;p.adapter->cancel(t);it=pending.erase(it);continue;
            }
            auto& object=objects.at(index(existing->kind,existing->key));
            auto candidate=object;
            const bool changed=witness->nativeStamp>object.nativeStamp&&witness->state!=object.state;
            if(changed) {
                if(candidate.version==UINT64_MAX) {++rejected;p.adapter->cancel(t);it=pending.erase(it);continue;}
                candidate.state=witness->state;++candidate.version;
            }
            candidate.nativeStamp=std::max(candidate.nativeStamp,witness->nativeStamp);
            auto full=baseline();for(auto& o:full["objects"])if(o.at("ref")==candidate.ref.json())o=candidate.json();
            if(full.dump().size()>maxWire) {++rejected;p.adapter->cancel(t);it=pending.erase(it);continue;}
            object=std::move(candidate);rememberEvent(t.path,witness->event);
            record(changed?"upsert":"receipt",object,{{"peer",t.intent.peer},{"member",t.intent.member},{"request",t.intent.request},{"action",t.intent.action},{"strike",t.intent.strike}});
            ++confirmed;it=pending.erase(it);
        }
    }
    std::optional<Json> changesSince(std::uint64_t since) const {
        if(since>revision||(!journal.empty()&&since<journal.front().at("revision").get<std::uint64_t>()-1))return {};
        Json changes=Json::array();for(const auto& e:journal)if(e.at("revision").get<std::uint64_t>()>since)changes.push_back(e);
        if(changes.dump().size()>maxWire-512)return {};
        return Json{{"scope",scope_.json()},{"base",since},{"revision",revision},{"events",std::move(changes)}};
    }
    Json status() const {return {{"objects",objects.size()},{"revision",revision},{"pending",pending.size()},{"suspended",suspended},
        {"intents_accepted",accepted},{"intents_rejected",rejected},{"results_confirmed",confirmed},{"expired",expired},
        {"dedup_commands",strikes.size()},{"dedup_event_paths",nativeEvents.size()},
        {"full_sync_verified",false},{"native_execution_supported",false},{"account_risk",true}};}
};
class Replica {
    Scope scope_;std::map<std::uint64_t,Json> objects;std::uint64_t revision=0;bool missing=true;
public:
    void reset(Scope scope={}) {scope_=std::move(scope);objects.clear();revision=0;missing=true;}
    const Scope& scope() const {return scope_;}
    bool baseline(const Json& packet,const Scope& expected,bool fromHost) {
        try {
            if(!fromHost||!exactFields(packet,{"scope","revision","objects"})||Scope::parse(packet.at("scope"))!=expected||
                !packet.at("revision").is_number_unsigned()||!packet.at("objects").is_array()||packet.at("objects").size()>Host::maxObjects||packet.dump().size()>Host::maxWire)return false;
            auto next=packet.at("revision").get<std::uint64_t>();if(scope_==expected&&next<revision)return false;
            std::map<std::uint64_t,Json> candidate;std::set<std::pair<std::string,std::string>> keys;
            for(const auto& o:packet.at("objects"))if(!Object::validWire(o)||!candidate.emplace(o.at("ref").at("id").get<std::uint64_t>(),o).second||
                !keys.emplace(o.at("kind").get<std::string>(),o.at("key").get<std::string>()).second)return false;
            if(!validGraph(candidate))return false;
            if(scope_==expected) {
                if(next==revision&&candidate!=objects)return false;
                for(const auto& [id,o]:candidate)if(auto it=objects.find(id);it!=objects.end()&&
                    (it->second.at("ref")!=o.at("ref")||it->second.at("kind")!=o.at("kind")||it->second.at("key")!=o.at("key")||
                     o.at("version").get<std::uint64_t>()<it->second.at("version").get<std::uint64_t>()||
                     (o.at("version")==it->second.at("version")&&(o.at("state")!=it->second.at("state")||o.at("relations")!=it->second.at("relations")))))return false;
            }
            scope_=expected;objects=std::move(candidate);revision=next;missing=false;return true;
        }catch(...) {return false;}
    }
    bool delta(const Json& packet,bool fromHost) {
        try {
            if(!fromHost||missing||!exactFields(packet,{"scope","base","revision","events"})||Scope::parse(packet.at("scope"))!=scope_||
                !packet.at("base").is_number_unsigned()||!packet.at("revision").is_number_unsigned()||!packet.at("events").is_array()||packet.at("events").size()>256||packet.dump().size()>Host::maxWire)return false;
            const auto end=packet.at("revision").get<std::uint64_t>();if(end<=revision)return false;
            if(packet.at("base")!=revision) {missing=true;return false;}
            auto candidate=objects;auto next=revision;
            for(const auto& event:packet.at("events")) {
                if(next==UINT64_MAX||!exactFields(event,{"scope","revision","type","object","receipt"})||Scope::parse(event.at("scope"))!=scope_||
                    !positive(event.at("revision"))||event.at("revision")!=++next||!Object::validWire(event.at("object"))||!validReceipt(event.at("receipt"))) {missing=true;return false;}
                const auto& o=event.at("object");const auto ref=Ref::parse(o.at("ref"));auto prior=candidate.find(ref.id);
                if(event.at("type")=="retire") {
                    if(prior==candidate.end()||prior->second.at("ref")!=ref.json()) {missing=true;return false;}candidate.erase(prior);
                }else if(event.at("type")=="upsert") {
                    if(prior!=candidate.end()&&(prior->second.at("ref")!=ref.json()||prior->second.at("kind")!=o.at("kind")||prior->second.at("key")!=o.at("key")||
                        prior->second.at("version").get<std::uint64_t>()==UINT64_MAX||o.at("version")!=prior->second.at("version").get<std::uint64_t>()+1)) {missing=true;return false;}
                    if(prior==candidate.end()&&o.at("version")!=1) {missing=true;return false;}candidate[ref.id]=o;
                }else if(event.at("type")=="receipt") {
                    // A correlated result already covered by a later native
                    // observation acknowledges only; it cannot replace state.
                    if(prior==candidate.end()||prior->second!=o||event.at("receipt").is_null()) {missing=true;return false;}
                }else {missing=true;return false;}
            }
            if(next!=end||candidate.size()>Host::maxObjects) {missing=true;return false;}
            std::set<std::pair<std::string,std::string>> keys;
            Json all=Json::array();for(const auto& [id,o]:candidate) {if(!keys.emplace(o.at("kind").get<std::string>(),o.at("key").get<std::string>()).second){missing=true;return false;}all.push_back(o);}
            if(!validGraph(candidate)) {missing=true;return false;}
            if(all.dump().size()>Host::maxWire-512) {missing=true;return false;}
            objects=std::move(candidate);revision=end;return true;
        }catch(...) {missing=true;return false;}
    }
    bool needsBaseline() const {return missing;}
    std::uint64_t version() const {return revision;}
    Json snapshot() const {Json list=Json::array();if(!missing)for(const auto& [id,o]:objects)list.push_back(o);return list;}
};
// Common personal-world handoff. Opening gameplay requires proven isolation,
// apply and recovery; a baseline merely stored in memory never opens the gate.
class WorldHandoff {
public:
    enum class Phase {Personal,Loading,Shared,Restoring,Failed};
private:
    Phase phase_=Phase::Personal;Scope scope_;std::uint64_t snapshot=0,appliedRevision=0;
    NativeAdapter* adapter=nullptr;bool restoreStarted=false,baselineSubmitted=false;std::string gameVersion_;
    std::thread::id owner;
    void thread() {
        if(owner==std::thread::id{})owner=std::this_thread::get_id();
        if(owner!=std::this_thread::get_id())throw std::runtime_error("native handoff outside game thread");
    }
    bool verified(const std::string& gameVersion) const {
        return adapter&&!gameVersion.empty()&&adapter->gameVersion()==gameVersion&&!adapter->path().empty()&&
            adapter->executionVerified()&&adapter->isolationVerified()&&adapter->recoveryVerified()&&adapter->personalRetained(snapshot);
    }
public:
    WorldHandoff()=default;WorldHandoff(const WorldHandoff&)=delete;WorldHandoff& operator=(const WorldHandoff&)=delete;
    Phase phase() const {return phase_;}
    bool begin(const Scope& scope,NativeAdapter& native,const std::string& gameVersion) {
        thread();
        if(phase_!=Phase::Personal||!scope.valid()||gameVersion.empty()||native.gameVersion()!=gameVersion||native.path().empty()||
            !native.executionVerified()||!native.isolationVerified()||!native.recoveryVerified())return false;
        std::optional<std::uint64_t> saved;try {saved=native.capturePersonal();}catch(...) {return false;}
        if(!saved||!*saved||!native.personalRetained(*saved))return false;
        scope_=scope;adapter=&native;snapshot=*saved;gameVersion_=gameVersion;phase_=Phase::Loading;restoreStarted=baselineSubmitted=false;
        // Once capture succeeded, any native begin failure requires recovery;
        // it cannot discard the original world or reopen account operations.
        bool started=false;try {started=native.beginShared(snapshot,scope);}catch(...) {}
        if(!started) {withdraw();return false;}return true;
    }
    bool admit(const Replica& replica,const std::string& gameVersion) {
        thread();
        if(phase_==Phase::Shared) {
            if(!verified(gameVersion)||gameVersion!=gameVersion_||replica.scope()!=scope_||replica.needsBaseline()||!adapter->nativeInitialized(scope_)) {withdraw();return false;}
            if(replica.version()==appliedRevision)return true;
            phase_=Phase::Loading;baselineSubmitted=false;
        }
        if(phase_!=Phase::Loading||!verified(gameVersion)||gameVersion!=gameVersion_||replica.scope()!=scope_||replica.needsBaseline()||!adapter->nativeInitialized(scope_))return false;
        if(baselineSubmitted) {
            if(!adapter->baselineObserved(scope_,appliedRevision))return false;
            if(replica.version()==appliedRevision) {phase_=Phase::Shared;return true;}
            // Wait for the prior native application before catching up. Never
            // dispatch overlapping baselines or admit an older revision.
            baselineSubmitted=false;
        }
        if(!baselineSubmitted) {
            bool submitted=false;try {submitted=adapter->applyBaseline(scope_,replica.version(),replica.snapshot());}catch(...) {}
            if(!submitted) {withdraw();return false;}
            appliedRevision=replica.version();baselineSubmitted=true;
        }
        if(replica.version()!=appliedRevision||!adapter->baselineObserved(scope_,appliedRevision))return false;
        phase_=Phase::Shared;return true;
    }
    void withdraw() {
        thread();
        if(phase_==Phase::Personal)return;
        if(adapter)adapter->suspendShared(scope_);
        if(phase_!=Phase::Restoring)restoreStarted=false;
        phase_=Phase::Restoring;
    }
    bool restore() {
        thread();
        if(phase_==Phase::Personal)return true;
        if(phase_!=Phase::Restoring||!adapter||!adapter->personalRetained(snapshot))return false;
        if(!restoreStarted) {
            bool started=false;try {started=adapter->beginRestore(snapshot);}catch(...) {}
            if(!started) {phase_=Phase::Failed;return false;}restoreStarted=true;
        }
        // Return/timeout is never native restoration evidence. Keep the native
        // snapshot and the closed gate until the adapter observes restoration.
        if(!adapter->restoreObserved(snapshot))return false;
        adapter->releasePersonal(snapshot);snapshot=0;adapter=nullptr;scope_={};phase_=Phase::Personal;return true;
    }
    bool roomOperationAllowed(const Scope& scope) const {return phase_==Phase::Shared&&scope==scope_&&verified(gameVersion_)&&adapter->nativeInitialized(scope_);}
    bool personalOperationAllowed() const {return phase_==Phase::Personal;}
    bool recoveryRetained() const {return snapshot!=0;}
    Json status() const {
        const char* phase=phase_==Phase::Personal?"personal":phase_==Phase::Loading?"loading":phase_==Phase::Shared?"shared":phase_==Phase::Restoring?"restoring":"restore_failed";
        return {{"phase",phase},{"personal_snapshot_retained",recoveryRetained()},{"restore_started",restoreStarted},
            {"personal_operation_allowed",personalOperationAllowed()},{"shared_operation_allowed",roomOperationAllowed(scope_)},{"full_sync_verified",false}};
    }
};
}
