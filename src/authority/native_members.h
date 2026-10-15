#pragma once
// Exact runtime metadata access used by native construction, not room JSON.
// No game object or value/array layout is inferred from a file offset.
#include "native_object_pins.h"
#include "../unity_bridge.h"
#include <set>

namespace eflink::worldauthority {
class NativeMembers {
    eflink::unity::Api* bridge;
    template<class T> static T symbol(const char* name) {
        auto fn=reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),name));
        if(!fn)throw std::runtime_error("native construction metadata unavailable");return fn;
    }
    static std::string typeName(const void* type) {
        auto* value=symbol<char*(*)(const void*)>("il2cpp_type_get_name")(type);
        if(!value)throw std::runtime_error("native member type unavailable");
        std::string result(value);symbol<void(*)(void*)>("il2cpp_free")(value);return result;
    }
    const void* field(void* object,const char* name,const char* expected,bool writing) const {
        if(!object)throw std::runtime_error("native member receiver unavailable");
        auto* actual=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(object);
        auto* value=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(actual,name);
        if(!value)throw std::runtime_error(std::string("native field unavailable: ")+name);
        const auto flags=symbol<int(*)(const void*)>("il2cpp_field_get_flags")(value);
        auto* owner=symbol<const void*(*)(const void*)>("il2cpp_field_get_parent")(value);
        auto* type=symbol<const void*(*)(const void*)>("il2cpp_field_get_type")(value);
        if((flags&0x10)||(writing&&(flags&0x60))||!owner||
           !symbol<bool(*)(const void*,const void*)>("il2cpp_class_is_assignable_from")(owner,actual)||(expected&&typeName(type)!=expected))
            throw std::runtime_error("native field contract differs");
        return value;
    }
    static authority::NativeValueShape shape(const void* field) {
        auto* type=symbol<const void*(*)(const void*)>("il2cpp_field_get_type")(field);
        return eflink::unity::Api::valueShape(symbol<const void*(*)(const void*)>("il2cpp_class_from_type")(type));
    }
public:
    explicit NativeMembers(eflink::unity::Api& value):bridge(&value) {}
    static int enumInt(const void* klass,const char* name) {
        authority::requireNativeValue<int>(eflink::unity::Api::valueShape(klass));
        const auto* value=symbol<const void*(*)(const void*,const char*)>("il2cpp_class_get_field_from_name")(klass,name);
        if(!value||!(symbol<int(*)(const void*)>("il2cpp_field_get_flags")(value)&0x10)||
            symbol<const void*(*)(const void*)>("il2cpp_class_from_type")(symbol<const void*(*)(const void*)>("il2cpp_field_get_type")(value))!=klass)
            throw std::runtime_error("native construction enum contract differs");
        int result=0;symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value")(value,&result);return result;
    }
    template<class T> T read(void* object,const char* name,const char* type) const {
        static_assert(std::is_trivially_copyable_v<T>);auto* value=field(object,name,type,false);
        authority::requireNativeField<T>(shape(value),false,true);T result{};
        symbol<void(*)(void*,const void*,void*)>("il2cpp_field_get_value")(object,value,&result);return result;
    }
    void* reference(void* object,const char* name,const void* expectedClass=nullptr) const {
        auto* value=field(object,name,nullptr,false);authority::requireNativeField<void*>(shape(value),false,true);void* result=nullptr;
        symbol<void(*)(void*,const void*,void*)>("il2cpp_field_get_value")(object,value,&result);
        auto* type=symbol<const void*(*)(const void*)>("il2cpp_field_get_type")(value);
        auto* declared=symbol<const void*(*)(const void*)>("il2cpp_class_from_type")(type);
        if(result) {
            auto* actual=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(result);
            auto compatible=symbol<bool(*)(const void*,const void*)>("il2cpp_class_is_assignable_from");
            if(!declared||!compatible(declared,actual)||(expectedClass&&!compatible(expectedClass,actual)))throw std::runtime_error("native reference field receiver differs");
        }
        return result;
    }
    // Caller must own this freshly constructed private object. This helper is
    // internal to the compiled native factory, never an arbitrary-field RPC.
    template<class T> void initialize(void* object,const char* name,const char* type,T input) const {
        static_assert(std::is_trivially_copyable_v<T>);auto* value=field(object,name,type,true);
        authority::requireNativeField<T>(shape(value),false,true);
        symbol<void(*)(void*,const void*,void*)>("il2cpp_field_set_value")(object,value,&input);
    }
    BE_ResolvedMethodV1 method(void* object,const char* label,const std::vector<std::string>& parameters,const char* returns) const {
        if(!object)throw std::runtime_error("native method receiver unavailable");
        auto* klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(object);
        auto each=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
        auto name=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
        auto count=symbol<unsigned(*)(const void*)>("il2cpp_method_get_param_count");
        auto flags=symbol<unsigned(*)(const void*,unsigned*)>("il2cpp_method_get_flags");
        auto parameter=symbol<const void*(*)(const void*,unsigned)>("il2cpp_method_get_param");
        auto result=symbol<const void*(*)(const void*)>("il2cpp_method_get_return_type");
        auto declaring=symbol<const void*(*)(const void*)>("il2cpp_method_get_class");
        auto assignable=symbol<bool(*)(const void*,const void*)>("il2cpp_class_is_assignable_from");
        const void* match=nullptr;void* iterator=nullptr;
        while(auto* candidate=each(klass,&iterator)) {
            const auto* text=name(candidate);unsigned implementation=0;
            if(!text||std::strcmp(text,label)||count(candidate)!=parameters.size()||(flags(candidate,&implementation)&0x410)||
               !declaring(candidate)||!assignable(declaring(candidate),klass)||typeName(result(candidate))!=returns)continue;
            bool valid=true;for(unsigned i=0;i<parameters.size();++i)if(typeName(parameter(candidate,i))!=parameters[i]) {valid=false;break;}
            if(!valid)continue;if(match)throw std::runtime_error("ambiguous native closed member");match=candidate;
        }
        if(!match)throw std::runtime_error(std::string("native closed member unavailable: ")+label);
        BE_ResolvedMethodV1 selected{};selected.method_info=match;return selected;
    }
    // HashSet/MapField implementations may return boxed struct iterators; use
    // their actual runtime shape rather than treating a boxed header as `this`.
    void* self(void* boxed) const {
        auto* klass=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(boxed);
        const auto value=eflink::unity::Api::valueShape(klass);if(!value.isValue)return boxed;
        authority::requireNativeValueShape(value);auto* result=bridge->raw()->object_unbox(bridge->raw()->context,boxed);
        if(!result)throw std::runtime_error("native boxed receiver unavailable");return result;
    }
    int count(void* collection) const {return bridge->value<int>(method(collection,"get_Count",{},"System.Int32"),self(collection));}
    // Let the native collection fill its exact reference array, then use
    // System.Array.GetValue. No array prefix, pointer stride or GC write is used.
    struct RetainedReference {void* object=nullptr;NativePin root;};
    std::vector<RetainedReference> references(void* collection,const char* element,NativeObjectPins& roots,unsigned maximum=128) const {
        const auto n=count(collection);if(n<0||unsigned(n)>maximum)throw std::runtime_error("native reference collection outside bound");
        const auto copy=method(collection,"CopyTo",{std::string(element)+"[]"},"System.Void");
        auto* parameter=symbol<const void*(*)(const void*,unsigned)>("il2cpp_method_get_param")(copy.method_info,0);
        auto* arrayClass=symbol<const void*(*)(const void*)>("il2cpp_class_from_type")(parameter);
        if(!arrayClass||symbol<int(*)(const void*)>("il2cpp_class_get_rank")(arrayClass)!=1)throw std::runtime_error("native CopyTo array rank differs");
        auto* elementClass=symbol<const void*(*)(const void*)>("il2cpp_class_get_element_class")(arrayClass);
        if(!elementClass||eflink::unity::Api::valueShape(elementClass).isValue)throw std::runtime_error("native reference array element differs");
        auto* array=symbol<void*(*)(const void*,std::uintptr_t)>("il2cpp_array_new")(elementClass,std::uintptr_t(n));
        if(!array)throw std::runtime_error("native reference array allocation failed");
        eflink::unity::Api::TemporaryRoot arrayRoot(bridge->raw(),array);
        bridge->call(copy,self(collection),{array});if(count(collection)!=n)throw std::runtime_error("native reference collection changed during capture");
        const auto get=bridge->method("Array","GetValue","System.Int32","System.Object","mscorlib.dll","System");
        std::vector<RetainedReference> out;out.reserve(n);std::set<void*> unique;
        for(int i=0;i<n;++i) {
            auto* object=bridge->call(get,array,{&i});if(!object||!unique.insert(object).second||
                !symbol<bool(*)(const void*,const void*)>("il2cpp_class_is_assignable_from")(elementClass,symbol<const void*(*)(void*)>("il2cpp_object_get_class")(object)))
                throw std::runtime_error("invalid native reference collection element");
            out.push_back({object,roots.retain(reinterpret_cast<std::uintptr_t>(object))});
        }
        return out;
    }
};
}
