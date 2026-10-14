#pragma once
// Private native identities for every shared kind. No address or account-side
// identifier in this table is serialized into the room protocol.
#include "world_authority.h"

namespace eflink::worldauthority {
struct NativeAllocation {
    std::uintptr_t address=0;
    std::uint32_t allocation=0,component=0,generation=0;
    std::uint64_t nativeId=0,secondaryId=0;
    bool operator==(const NativeAllocation&) const=default;
    bool valid() const {return address&&allocation;}
};
struct NativeBindingInput {std::string key;NativeAllocation allocation;};
struct NativeBinding {std::string kind,key;NativeAllocation allocation;std::uint64_t cookie=0;};
class NativeObjectBindings {
    std::map<std::pair<std::string,std::string>,NativeBinding> entries;
    std::uint64_t nextCookie=0;
    std::thread::id owner;
    void thread() {
        if(owner==std::thread::id{})owner=std::this_thread::get_id();
        if(owner!=std::this_thread::get_id())throw std::runtime_error("native binding mutation outside game thread");
    }
public:
    NativeObjectBindings()=default;
    NativeObjectBindings(const NativeObjectBindings&)=delete;
    NativeObjectBindings& operator=(const NativeObjectBindings&)=delete;
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
               !allocations.emplace(a.address,a.allocation,a.component,a.generation,a.nativeId,a.secondaryId).second)
                throw std::runtime_error("ambiguous or invalid native object identity");
            const auto index=std::make_pair(kind,input.key);
            auto old=entries.find(index);
            auto cookie=old!=entries.end()&&old->second.allocation==a?old->second.cookie:0;
            if(!cookie) {
                if(counter==UINT64_MAX)throw std::runtime_error("native binding identity exhausted");
                cookie=++counter;
            }
            candidate[index]={kind,input.key,a,cookie};
        }
        for(auto it=candidate.begin();it!=candidate.end();) {
            if(it->first.first==kind&&!seen.contains(it->first.second))it=candidate.erase(it);else ++it;
        }
        if(candidate.size()>Host::maxObjects)throw std::runtime_error("native binding capacity exceeded");
        entries.swap(candidate);nextCookie=counter;
    }
    // Scene/room teardown retires all bindings. Allocation counters survive
    // resets so a pooled address cannot resurrect a previous object's cookie.
    void clear() {thread();entries.clear();}
    void retireKind(const std::string& kind) {reconcile(kind,{});}
    const NativeBinding* find(const std::string& kind,const std::string& key) const {
        auto it=entries.find({kind,key});return it==entries.end()?nullptr:&it->second;
    }
    // Revalidate the actual current native allocation immediately before an
    // operation; holding a GC root alone is not proof that it is still alive.
    const NativeBinding* resolve(const std::string& kind,const std::string& key,const NativeAllocation& current) const {
        auto* value=find(kind,key);return value&&current.valid()&&value->allocation==current?value:nullptr;
    }
    std::size_t size() const {return entries.size();}
};
}
