#pragma once
// Thread-local re-entrancy guard: native part damage transfer may re-enter
// ApplyModifier with the same (ability, modifier) pair. Execute native code
// normally, but record damage statistics once.
#include <set>
#include <utility>

namespace eflink {
class DamageApplicationScope {
    using Key = std::pair<const void*, const void*>;
    inline static thread_local std::set<Key> active;
    Key key;
    bool owns;
public:
    DamageApplicationScope(const void* ability, const void* modifier)
        : key(ability, modifier), owns(active.insert(key).second) {}
    ~DamageApplicationScope() { if (owns) active.erase(key); }
    DamageApplicationScope(const DamageApplicationScope&) = delete;
    DamageApplicationScope& operator=(const DamageApplicationScope&) = delete;
    bool observe() const { return owns; }
};
} // namespace eflink
