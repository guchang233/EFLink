#pragma once
#include <set>
#include <utility>

namespace eflink {
// Native part transfer may enter ApplyModifier again with the same modifier.
// Execute native code normally, but observe/report this stacked application once.
class DamageApplicationScope {
    using Key=std::pair<const void*,const void*>;
    inline static thread_local std::set<Key> active;
    Key key;
    bool owns;
public:
    DamageApplicationScope(const void* ability,const void* modifier):key(ability,modifier),owns(active.insert(key).second) {}
    ~DamageApplicationScope() {if(owns)active.erase(key);}
    DamageApplicationScope(const DamageApplicationScope&)=delete;
    DamageApplicationScope& operator=(const DamageApplicationScope&)=delete;
    bool observe() const {return owns;}
};
}
