#pragma once
#include "BetterEndfield/ThirdPartyModule.h"
#include "authority/native_value_abi.h"
#include <windows.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>

namespace eflink::unity {
struct V2 {float x,y;};
struct V3 {float x,y,z;};
struct Quaternion {float x,y,z,w;};
struct Color {float r,g,b,a;};
struct Matrix {float values[16];};
struct Rect {float x,y,width,height;};
struct RenderTarget {int type,name,instance,pad;std::uintptr_t buffer;int mip,face,slice,pad2;};
class Api {
    const BE_HostApiV1* api=nullptr;
    std::unordered_map<std::string,BE_ResolvedMethodV1> methods;
    std::unordered_map<std::string,BE_ResolvedClassV1> classes;
public:
    std::vector<std::uint32_t> roots;
    struct TemporaryRoot {
        const BE_HostApiV1* api;
        std::uint32_t handle;
        TemporaryRoot(const BE_HostApiV1* a,void* object):api(a),handle(a->gchandle_new(a->context,object,0)) {
            if(!handle) throw std::runtime_error("temporary managed root allocation failed");
        }
        ~TemporaryRoot() {api->gchandle_free(api->context,handle);}
        TemporaryRoot(const TemporaryRoot&)=delete;
        TemporaryRoot& operator=(const TemporaryRoot&)=delete;
    };
    void bind(const BE_HostApiV1* value) {api=value;}
    const BE_HostApiV1* raw() const {return api;}
    static authority::NativeValueShape valueShape(const void* klass) {
        auto dll=GetModuleHandleW(L"GameAssembly.dll");
        auto isValue=reinterpret_cast<bool(*)(const void*)>(GetProcAddress(dll,"il2cpp_class_is_valuetype"));
        auto size=reinterpret_cast<int(*)(const void*,unsigned*)>(GetProcAddress(dll,"il2cpp_class_value_size"));
        if(!klass||!isValue||!size)throw std::runtime_error("native value metadata unavailable");
        authority::NativeValueShape shape;shape.isValue=isValue(klass);
        if(shape.isValue)shape.bytes=size(klass,&shape.alignment);
        return shape;
    }
    BE_ResolvedMethodV1 virtualMethod(BE_ResolvedMethodV1 declaration,void* object) {
        const auto dll=GetModuleHandleW(L"GameAssembly.dll");
        auto lookup=reinterpret_cast<const void*(*)(void*,const void*)>(GetProcAddress(dll,"il2cpp_object_get_virtual_method"));
        auto owner=reinterpret_cast<const void*(*)(const void*)>(GetProcAddress(dll,"il2cpp_method_get_class"));
        auto objectClass=reinterpret_cast<const void*(*)(void*)>(GetProcAddress(dll,"il2cpp_object_get_class"));
        auto assignable=reinterpret_cast<bool(*)(const void*,const void*)>(GetProcAddress(dll,"il2cpp_class_is_assignable_from"));
        auto flags=reinterpret_cast<unsigned(*)(const void*,unsigned*)>(GetProcAddress(dll,"il2cpp_method_get_flags"));
        auto count=reinterpret_cast<unsigned(*)(const void*)>(GetProcAddress(dll,"il2cpp_method_get_param_count"));
        if(!declaration.method_info||!object||!lookup||!owner||!objectClass||!assignable||!flags||!count)
            throw std::runtime_error("native virtual metadata unavailable");
        unsigned implementationFlags=0;
        auto* receiverClass=objectClass(object);auto* declaredClass=owner(declaration.method_info);
        if(!receiverClass||!declaredClass||(flags(declaration.method_info,&implementationFlags)&0x10)||!assignable(declaredClass,receiverClass))
            throw std::runtime_error("native virtual receiver differs");
        const auto* target=lookup(object,declaration.method_info);
        if(!target||(flags(target,&implementationFlags)&0x410)||!owner(target)||!assignable(owner(target),receiverClass)||
           count(target)!=count(declaration.method_info))throw std::runtime_error("native virtual implementation unavailable");
        BE_ResolvedMethodV1 result{};result.method_info=target;return result;
    }
    BE_ResolvedClassV1 klass(const char* type,const char* assembly="UnityEngine.CoreModule.dll",const char* space="UnityEngine") {
        std::string key=std::string(assembly)+space+type;
        if(auto found=classes.find(key);found!=classes.end()) return found->second;
        BE_ResolvedClassV1 result{};
        if(api->resolve_class(api->context,assembly,space,type,&result)!=BE_Result_Ok||!result.class_info)
            throw std::runtime_error("class unavailable: "+key);
        classes.emplace(key,result);return result;
    }
    BE_ResolvedMethodV1 method(const char* type,const char* name,const char* params="",const char* returns="System.Void",
        const char* assembly="UnityEngine.CoreModule.dll",const char* space="UnityEngine") {
        std::string key=std::string(assembly)+space+type+name+params+returns;
        if(auto found=methods.find(key);found!=methods.end()) return found->second;
        unsigned count=params[0]?1:0;for(auto* p=params;*p;++p) if(*p=='|') ++count;
        BE_MethodDescriptorV1 descriptor{assembly,space,type,name,params[0]?params:nullptr,returns,count};
        BE_ResolvedMethodV1 result{};
        if(api->resolve_method(api->context,&descriptor,&result)!=BE_Result_Ok||!result.method_info)
            throw std::runtime_error("method unavailable: "+key);
        methods.emplace(key,result);return result;
    }
    // Host resolve_method intentionally requires a callable entry and therefore
    // rejects abstract declarations. Resolve a zero-argument virtual getter as
    // metadata only, then virtualMethod selects its real receiver implementation.
    BE_ResolvedMethodV1 virtualGetter(const char* type,const char* name,const char* returns,
        const char* assembly="Gameplay.Beyond.dll",const char* space="Beyond.Gameplay") {
        const std::string key=std::string("virtual-getter|")+assembly+space+type+name+returns;
        if(auto found=methods.find(key);found!=methods.end())return found->second;
        auto dll=GetModuleHandleW(L"GameAssembly.dll");
        auto each=reinterpret_cast<const void*(*)(const void*,void**)>(GetProcAddress(dll,"il2cpp_class_get_methods"));
        auto methodName=reinterpret_cast<const char*(*)(const void*)>(GetProcAddress(dll,"il2cpp_method_get_name"));
        auto count=reinterpret_cast<unsigned(*)(const void*)>(GetProcAddress(dll,"il2cpp_method_get_param_count"));
        auto flags=reinterpret_cast<unsigned(*)(const void*,unsigned*)>(GetProcAddress(dll,"il2cpp_method_get_flags"));
        auto resultType=reinterpret_cast<const void*(*)(const void*)>(GetProcAddress(dll,"il2cpp_method_get_return_type"));
        auto typeName=reinterpret_cast<char*(*)(const void*)>(GetProcAddress(dll,"il2cpp_type_get_name"));
        auto release=reinterpret_cast<void(*)(void*)>(GetProcAddress(dll,"il2cpp_free"));
        if(!each||!methodName||!count||!flags||!resultType||!typeName||!release)
            throw std::runtime_error("native virtual getter metadata unavailable");
        auto klassValue=klass(type,assembly,space);void* iterator=nullptr;const void* match=nullptr;
        while(const auto* candidate=each(klassValue.class_info,&iterator)) {
            unsigned implementation=0;const auto attributes=flags(candidate,&implementation);
            const auto* label=methodName(candidate);
            if(!label||std::strcmp(label,name)||count(candidate)||(attributes&0x10)||!(attributes&0x40))continue;
            auto* nativeReturn=typeName(resultType(candidate));const bool same=nativeReturn&&std::strcmp(nativeReturn,returns)==0;
            if(nativeReturn)release(nativeReturn);if(!same)continue;
            if(match)throw std::runtime_error("ambiguous native virtual getter");match=candidate;
        }
        if(!match)throw std::runtime_error("native virtual getter declaration unavailable");
        BE_ResolvedMethodV1 result{};result.method_info=match;methods.emplace(key,result);return result;
    }
    void* call(BE_ResolvedMethodV1 method,void* object=nullptr,std::initializer_list<void*> args={}) {
        void* exception=nullptr;
        std::vector<void*> parameters(args);
        auto* result=api->runtime_invoke(api->context,method.method_info,object,parameters.empty()?nullptr:parameters.data(),&exception);
        if(exception) {
            std::string label="managed exception during bridge call";
            auto dll=GetModuleHandleW(L"GameAssembly.dll");
            auto name=reinterpret_cast<const char*(*)(const void*)>(GetProcAddress(dll,"il2cpp_method_get_name"));
            auto format=reinterpret_cast<void(*)(const void*,char*,int)>(GetProcAddress(dll,"il2cpp_format_exception"));
            if(name&&method.method_info) {auto* text=name(method.method_info);if(text)label+=" ("+std::string(text)+")";}
            if(format) {char message[1024]{};format(exception,message,int(sizeof(message)));label+="; ";label+=message;}
            throw std::runtime_error(label);
        }
        return result;
    }
    // Allocate a zero-initialized boxed value using the exact closed parameter
    // type. In particular, Nullable<T> must not use a guessed native layout.
    void* parameterValue(BE_ResolvedMethodV1 method,unsigned index) {
        auto dll=GetModuleHandleW(L"GameAssembly.dll");
        auto parameter=reinterpret_cast<const void*(*)(const void*,unsigned)>(GetProcAddress(dll,"il2cpp_method_get_param"));
        auto klassFrom=reinterpret_cast<const void*(*)(const void*)>(GetProcAddress(dll,"il2cpp_class_from_type"));
        auto count=reinterpret_cast<unsigned(*)(const void*)>(GetProcAddress(dll,"il2cpp_method_get_param_count"));
        if(!parameter||!klassFrom||!count||!method.method_info||index>=count(method.method_info))throw std::runtime_error("value parameter metadata unavailable");
        auto* type=parameter(method.method_info,index);auto* klass=type?klassFrom(type):nullptr;
        const auto shape=valueShape(klass);authority::requireNativeValueShape(shape);
        auto* boxed=api->object_new(api->context,klass);auto* raw=boxed?api->object_unbox(api->context,boxed):nullptr;
        if(!raw)throw std::runtime_error("value parameter allocation failed");
        std::memset(raw,0,std::size_t(shape.bytes));return boxed;
    }
    template<class T> T value(BE_ResolvedMethodV1 method,void* object=nullptr,std::initializer_list<void*> args={}) {
        auto* result=call(method,object,args);if(!result)throw std::runtime_error("missing boxed bridge result");
        TemporaryRoot root(api,result);
        auto boxedClassOf=reinterpret_cast<const void*(*)(void*)>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_object_get_class"));
        if(!boxedClassOf)throw std::runtime_error("boxed bridge result type unavailable");
        authority::requireNativeValue<T>(valueShape(boxedClassOf(result)));
        auto* raw=api->object_unbox(api->context,result);
        if(!raw) throw std::runtime_error("missing boxed bridge result data");T output;std::memcpy(&output,raw,sizeof(T));return output;
    }
    template<class T> T virtualValue(BE_ResolvedMethodV1 declaration,void* object,std::initializer_list<void*> args={}) {
        if(!object)throw std::runtime_error("native virtual instance unavailable");
        TemporaryRoot instanceRoot(api,object);
        return value<T>(virtualMethod(declaration,object),object,args);
    }
    void* keep(void* object) {
        if(!object) throw std::runtime_error("missing Unity object");
        const auto handle=api->gchandle_new(api->context,object,0);
        if(!handle) throw std::runtime_error("managed root allocation failed");roots.push_back(handle);return object;
    }
    void* object(const char* type) {return keep(api->object_new(api->context,klass(type).class_info));}
    void* string(const std::string& value) {return api->string_new(api->context,value.c_str());}
    void* gameObject(const std::string& name) {
        auto* output=object("GameObject");call(method("GameObject",".ctor","System.String"),output,{string(name)});return output;
    }
    void* transform(void* object,bool component=false) {
        return call(method(component?"Component":"GameObject","get_transform","","UnityEngine.Transform"),object);
    }
    void* add(void* object,const char* type,const char* assembly="UnityEngine.CoreModule.dll",const char* space="UnityEngine") {
        auto klassValue=klass(type,assembly,space);
        return keep(call(method("GameObject","AddComponent","System.Type","UnityEngine.Component"),object,{klassValue.type_object}));
    }
    void setPosition(void* transform,V3 value) {call(method("Transform","set_position","UnityEngine.Vector3"),transform,{&value});}
    void setActive(void* object,bool value) {call(method("GameObject","SetActive","System.Boolean"),object,{&value});}
    void destroy(void* object) {if(object) call(method("Object","Destroy","UnityEngine.Object"),nullptr,{object});}
    void* array(const char* type,const void* bytes,std::size_t count,std::size_t stride,const char* assembly="UnityEngine.CoreModule.dll",const char* space="UnityEngine") {
        // Public IL2CPP allocation exports; standard Windows x64 IL2CPP array ABI.
        // Validate element size and allocated length before accessing the vector.
        const auto module=GetModuleHandleW(L"GameAssembly.dll");
        const auto allocate=reinterpret_cast<void*(*)(const void*,std::uintptr_t)>(GetProcAddress(module,"il2cpp_array_new"));
        const auto length=reinterpret_cast<std::uintptr_t(*)(void*)>(GetProcAddress(module,"il2cpp_array_length"));
        const auto size=reinterpret_cast<int(*)(const void*,unsigned*)>(GetProcAddress(module,"il2cpp_class_value_size"));
        if(!allocate||!length||!size) throw std::runtime_error("IL2CPP array exports unavailable");
        auto k=klass(type,assembly,space);unsigned align=0;
        if(size(k.class_info,&align)!=stride) throw std::runtime_error("IL2CPP element ABI mismatch");
        auto* result=allocate(k.class_info,count);
        if(!result||length(result)!=count) throw std::runtime_error("IL2CPP array allocation failed");
        struct ArrayPrefix {void* klass;void* monitor;void* bounds;std::uintptr_t length;};
        static_assert(sizeof(ArrayPrefix)==32);
        if(count) {
            if(!bytes) throw std::runtime_error("array source missing");
            std::memcpy(static_cast<unsigned char*>(result)+sizeof(ArrayPrefix),bytes,count*stride);
        }
        return result;
    }
    void* texture(unsigned width,unsigned height,const void* bytes) {
        if(!width||!height||width>8192||height>8192) throw std::runtime_error("texture dimensions invalid");
        auto* result=object("Texture2D");int w=int(width),h=int(height),format=4;bool mip=false;
        call(method("Texture2D",".ctor","System.Int32|System.Int32|UnityEngine.TextureFormat|System.Boolean"),result,{&w,&h,&format,&mip});
        int point=0,clamp=1;
        call(method("Texture","set_filterMode","UnityEngine.FilterMode"),result,{&point});
        call(method("Texture","set_wrapMode","UnityEngine.TextureWrapMode"),result,{&clamp});
        upload(result,width,height,bytes);return result;
    }
    void upload(void* texture,unsigned width,unsigned height,const void* bytes) {
        auto pointer=reinterpret_cast<std::uintptr_t>(bytes);int length=int(std::uint64_t(width)*height*4);bool update=false,discard=false;
        call(method("Texture2D","LoadRawTextureData","System.IntPtr|System.Int32"),texture,{&pointer,&length});
        call(method("Texture2D","Apply","System.Boolean|System.Boolean"),texture,{&update,&discard});
    }
};
}
