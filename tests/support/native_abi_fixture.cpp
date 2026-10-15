// A synthetic export fixture for unit tests. Never installed beside the game.
#include "native_abi_fixture.h"
#include <cstdlib>
#include <cstring>
#define FIXTURE_EXPORT extern "C" __declspec(dllexport)
FIXTURE_EXPORT const void* il2cpp_object_get_class(void* object) {return object?static_cast<AbiFixtureObject*>(object)->klass:nullptr;}
FIXTURE_EXPORT bool il2cpp_class_is_valuetype(const void* klass) {return static_cast<const AbiFixtureClass*>(klass)->value;}
FIXTURE_EXPORT int il2cpp_class_value_size(const void* klass,unsigned* alignment) {
    auto* value=static_cast<const AbiFixtureClass*>(klass);*alignment=value->alignment;return value->bytes;
}
FIXTURE_EXPORT const void* il2cpp_method_get_class(const void* method) {return static_cast<const AbiFixtureMethod*>(method)->owner;}
FIXTURE_EXPORT unsigned il2cpp_method_get_flags(const void* method,unsigned* implementation) {*implementation=0;return static_cast<const AbiFixtureMethod*>(method)->flags;}
FIXTURE_EXPORT unsigned il2cpp_method_get_param_count(const void* method) {return static_cast<const AbiFixtureMethod*>(method)->count;}
FIXTURE_EXPORT bool il2cpp_class_is_assignable_from(const void* expected,const void* actual) {
    for(auto* value=static_cast<const AbiFixtureClass*>(actual);value;value=value->parent)if(value==expected)return true;return false;
}
FIXTURE_EXPORT const void* il2cpp_object_get_virtual_method(void*,const void* method) {return static_cast<const AbiFixtureMethod*>(method)->implementation;}
FIXTURE_EXPORT const void* il2cpp_class_get_methods(const void* klass,void** iterator) {
    if(*iterator)return nullptr;*iterator=reinterpret_cast<void*>(1);return static_cast<const AbiFixtureClass*>(klass)->method;
}
FIXTURE_EXPORT const char* il2cpp_method_get_name(const void* method) {return static_cast<const AbiFixtureMethod*>(method)->name;}
FIXTURE_EXPORT const void* il2cpp_method_get_return_type(const void* method) {return static_cast<const AbiFixtureMethod*>(method)->returns;}
FIXTURE_EXPORT char* il2cpp_type_get_name(const void* type) {
    const auto* name=static_cast<const char*>(type);const auto bytes=std::strlen(name)+1;
    auto* copy=static_cast<char*>(std::malloc(bytes));if(copy)std::memcpy(copy,name,bytes);return copy;
}
FIXTURE_EXPORT void il2cpp_free(void* value) {std::free(value);}
