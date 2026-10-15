#pragma once
// Private native identities for every shared kind. No address or account-side
// identifier in this table is serialized into the room protocol.
#include "world_authority.h"
#include "native_object_pins.h"

namespace eflink::worldauthority {
struct NativeAllocation {
    std::uintptr_t address=0;
    std::uint32_t allocation=0,component=0,generation=0;
    std::uint64_t nativeId=0,secondaryId=0;
    bool operator==(const NativeAllocation&) const=default;
    bool valid() const {return address&&allocation;}
};
enum class NativeDomain { Personal, HostWorld, GuestRoom };
struct NativeBindingInput {std::string key;NativeAllocation allocation;NativePin pin;};
struct NativeBinding {std::string kind,key;NativeAllocation allocation;std::uint64_t cookie=0;NativeDomain domain=NativeDomain::Personal;};
class NativeObjectBindings {
    const NativeDomain domain;
    struct Entry {NativeBinding value;NativePin pin;};
    NativeObjectPins* pins=nullptr;
    std::map<std::pair<std::string,std::string>,Entry> entries;
    std::uint64_t nextCookie=0;
    std::uint64_t revision=0,sourceRevision=0;
    const NativeObjectBindings* source=nullptr;
    bool committing=false;
    std::thread::id owner;
    explicit NativeObjectBindings(const NativeObjectBindings& value,int)
        :domain(value.domain),pins(value.pins),entries(value.entries),nextCookie(value.nextCookie),sourceRevision(value.revision),source(&value),owner(value.owner) {}
    void thread() {
        if(committing)throw std::runtime_error("native binding mutation during authority commit");
        if(owner==std::thread::id{})owner=std::this_thread::get_id();
        if(owner!=std::this_thread::get_id())throw std::runtime_error("native binding mutation outside game thread");
    }
    const NativeBinding* find(const std::string& kind,const std::string& key) const {
        auto it=entries.find({kind,key});return it==entries.end()?nullptr:&it->second.value;
    }
public:
    explicit NativeObjectBindings(NativeDomain value,NativeObjectPins* retention=nullptr):domain(value),pins(retention) {}
    NativeObjectBindings(const NativeObjectBindings&)=delete;
    NativeObjectBindings& operator=(const NativeObjectBindings&)=delete;
    // Prepare all native kinds privately. Cookies and retirements become
    // visible only after the authority accepts the complete observed frame.
    NativeObjectBindings preview() {thread();return NativeObjectBindings(*this,0);}
    // Source census supplies its actual current allocation. Reuse an existing
    // root only for that complete identity, otherwise retain a new native object.
    NativePin retain(const std::string& kind,const std::string& key,const NativeAllocation& current) {
        thread();if(!pins||!current.valid()||!knownKind(kind)||key.empty()||key.size()>160)
            throw std::runtime_error("native retention provider or allocation unavailable");
        auto old=entries.find({kind,key});
        if(old!=entries.end()&&old->second.value.allocation==current&&pins->matches(old->second.pin,current.address))return old->second.pin;
        return pins->retain(current.address);
    }
    template<class Accept> bool commitWith(NativeObjectBindings& candidate,Accept accept) {
        thread();candidate.thread();
        if(candidate.source!=this||candidate.domain!=domain||candidate.pins!=pins||candidate.sourceRevision!=revision)
            throw std::runtime_error("stale or foreign native binding transaction");
        committing=candidate.committing=true;bool accepted=false;
        try {accepted=accept();}catch(...) {committing=candidate.committing=false;throw;}
        committing=candidate.committing=false;if(!accepted)return false;
        // No allocation or game call after authority acceptance.
        entries.swap(candidate.entries);nextCookie=candidate.nextCookie;++revision;
        candidate.source=nullptr;return true;
    }
    // An adapter supplies a COMPLETE census for one kind. Failure/truncation
    // must not retire live objects. Other kinds retain their own bindings.
    void reconcile(const std::string& kind,const std::vector<NativeBindingInput>& census) {
        thread();
        if(!knownKind(kind)||census.size()>Host::maxObjects)throw std::runtime_error("invalid native binding census");
        auto candidate=entries;
        std::set<std::string> seen;
        std::set<std::tuple<std::uintptr_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint64_t,std::uint64_t>> allocations;
        auto counter=nextCookie;
        for(const auto& input:census) {
            const auto& a=input.allocation;
            if(input.key.empty()||input.key.size()>160||!a.valid()||!seen.insert(input.key).second||
               !allocations.emplace(a.address,a.allocation,a.component,a.generation,a.nativeId,a.secondaryId).second||
               (pins?!pins->matches(input.pin,a.address):bool(input.pin)))
                throw std::runtime_error("ambiguous or invalid native object identity");
            const auto index=std::make_pair(kind,input.key);
            auto old=entries.find(index);
            auto cookie=old!=entries.end()&&old->second.value.allocation==a?old->second.value.cookie:0;
            if(!cookie) {
                if(counter==UINT64_MAX)throw std::runtime_error("native binding identity exhausted");
                cookie=++counter;
            }
            candidate[index]={{kind,input.key,a,cookie,domain},input.pin};
        }
        for(auto it=candidate.begin();it!=candidate.end();) {
            if(it->first.first==kind&&!seen.contains(it->first.second))it=candidate.erase(it);else ++it;
        }
        if(candidate.size()>Host::maxObjects)throw std::runtime_error("native binding capacity exceeded");
        entries.swap(candidate);nextCookie=counter;++revision;
    }
    // Scene/room teardown retires all bindings. Allocation counters survive
    // resets so a pooled address cannot resurrect a previous object's cookie.
    void clear() {thread();entries.clear();++revision;}
    void retireKind(const std::string& kind) {
        thread();if(!knownKind(kind))throw std::runtime_error("unknown native binding kind");
        for(auto it=entries.begin();it!=entries.end();)if(it->first.first==kind)it=entries.erase(it);else ++it;
        ++revision;
    }
    // Census code may use the opaque cookie to build authority observations.
    // It cannot retrieve a native address without an explicit domain check.
    std::uint64_t cookie(const std::string& kind,const std::string& key) const {
        if(owner==std::thread::id{}||owner!=std::this_thread::get_id())return 0;
        auto* value=find(kind,key);return value?value->cookie:0;
    }
    // Revalidate the actual current native allocation immediately before an
    // operation; holding a GC root alone is not proof that it is still alive.
    const NativeBinding* resolve(NativeDomain expectedDomain,const std::string& kind,const std::string& key,const NativeAllocation& current) const {
        if(owner==std::thread::id{}||owner!=std::this_thread::get_id())return nullptr;
        auto* value=find(kind,key);return value&&value->domain==expectedDomain&&current.valid()&&value->allocation==current?value:nullptr;
    }
    // Synchronous game-thread access for a compiled adapter. The root survives
    // reentrant census changes during the native call; native allocation identity
    // must still be proved by the probe immediately before use. Cookie is private
    // Host ledger evidence, never a handle supplied by a room packet.
    template<class Probe,class Use> bool withResolved(NativeDomain expectedDomain,const std::string& kind,const std::string& key,
                                                    std::uint64_t expectedCookie,Probe probe,Use use) const {
        if(committing||!pins||owner!=std::this_thread::get_id()||!expectedCookie)return false;
        auto found=entries.find({kind,key});
        if(found==entries.end()||found->second.value.domain!=expectedDomain||found->second.value.cookie!=expectedCookie||
           !pins->matches(found->second.pin,found->second.value.allocation.address))return false;
        const auto binding=found->second.value;const auto root=found->second.pin;
        try {if(probe(binding.allocation)!=binding.allocation)return false;}catch(...) {return false;}
        const auto* current=find(kind,key);
        if(committing||!current||current->cookie!=expectedCookie||current->allocation!=binding.allocation)return false;
        return use(binding);
    }
    std::size_t size() const {return entries.size();}
};
}
