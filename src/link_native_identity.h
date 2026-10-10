#pragma once
#include <cstdint>

namespace eflink {
// A GC handle preserves a managed address, not the lifetime of a pooled Entity.
// Compare the allocation UID as well as its server identity before any mutation.
struct NativeIdentity {
    std::uint32_t instance=0,generation=0;
    std::uint64_t server=0;
    bool captured=false;
    bool matches(const NativeIdentity& current) const {
        return captured&&current.captured&&instance==current.instance&&
            generation==current.generation&&server==current.server;
    }
};
}
