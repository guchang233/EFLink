#pragma once
// Metadata shape checks shared by native reads. File RVAs never establish an
// ABI: the running runtime must describe the actual object before bytes move.
#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace eflink::authority {
struct NativeValueShape {
    bool isValue=false;
    int bytes=0;
    unsigned alignment=0;
};
inline void requireNativeValueShape(const NativeValueShape& shape) {
    if(!shape.isValue||shape.bytes<=0||shape.bytes>4096||!shape.alignment||
       shape.alignment>16||(shape.alignment&(shape.alignment-1)))
        throw std::runtime_error("native value layout unavailable");
}
template<class T> void requireNativeValue(const NativeValueShape& shape) {
    static_assert(std::is_trivially_copyable_v<T>&&!std::is_pointer_v<T>);
    requireNativeValueShape(shape);
    if(shape.bytes!=sizeof(T))throw std::runtime_error("native value size differs from requested read");
}
template<class T> void requireNativeField(const NativeValueShape& shape,bool isStatic,bool ownerMatches) {
    static_assert(std::is_trivially_copyable_v<T>);
    if(isStatic||!ownerMatches)throw std::runtime_error("native instance field ownership differs");
    if constexpr(std::is_same_v<T,void*>) {
        if(shape.isValue)throw std::runtime_error("native value field cannot be read as a managed reference");
    } else requireNativeValue<T>(shape);
}
}
