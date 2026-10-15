#include "authority/native_value_abi.h"
#include <cstdint>
#include <iostream>
using namespace eflink::authority;
static void check(bool value,const char* name) {if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool rejects(F f) {try {f();return false;}catch(const std::runtime_error&) {return true;}}
int main() {
    requireNativeValue<bool>({true,1,1});requireNativeValue<std::uint32_t>({true,4,4});
    requireNativeValue<double>({true,8,8});
    struct Vector3 {float x,y,z;};requireNativeValue<Vector3>({true,12,4});
    check(true,"scalar and vector reads accept their actual boxed metadata sizes");
    check(rejects([]{requireNativeValue<std::uint64_t>({true,4,4});}),"32-bit boxed result cannot be copied as 64-bit data");
    check(rejects([]{requireNativeValue<bool>({true,4,4});}),"integer result cannot be silently interpreted as Boolean");
    check(rejects([]{requireNativeValue<std::uint64_t>({false,8,8});}),"managed reference cannot be unboxed as a scalar");
    for(const auto shape:{NativeValueShape{true,4,0},NativeValueShape{true,4,3},NativeValueShape{true,4,32},
        NativeValueShape{true,-1,4},NativeValueShape{true,4097,4}})
        check(rejects([&]{requireNativeValueShape(shape);}),"unknown or invalid native layouts reject before memory access");
    requireNativeField<void*>({false,0,0},false,true);
    requireNativeField<std::uint32_t>({true,4,4},false,true);
    check(true,"reference and scalar instance fields use separate read contracts");
    check(rejects([]{requireNativeField<void*>({true,8,8},false,true);}),"same-size value cannot be treated as a managed object pointer");
    check(rejects([]{requireNativeField<std::uint64_t>({false,0,0},false,true);}),"managed field cannot be read into an integer buffer");
    check(rejects([]{requireNativeField<std::uint64_t>({true,8,8},true,true);}),"static field requires a different native accessor");
    check(rejects([]{requireNativeField<std::uint64_t>({true,8,8},false,false);}),"a field declared by another native class cannot read the instance");
    check(rejects([]{requireNativeField<std::uint32_t>({true,8,8},false,true);}),"field width changes cannot overwrite a smaller C++ destination");
    return 0;
}
