#pragma once
// Read-only IL2CPP metadata helpers (diagnostics): find methods by name and
// map native code addresses back to the methods containing them.
#include "nlohmann/json.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace eflink::authority {
class MetadataIndex {
    template<class T> static T symbol(const char* name) {
        auto p=reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),name));
        if(!p)throw std::runtime_error(std::string("missing metadata export: ")+name);
        return p;
    }
    struct Method {std::uintptr_t address;std::string name;};
    std::vector<Method> byAddress; // sorted by entry address
    std::string typeText(const void* type) const {
        static auto typeName=symbol<char*(*)(const void*)>("il2cpp_type_get_name");
        static auto release=symbol<void(*)(void*)>("il2cpp_free");
        char* value=typeName(type);std::string out=value?value:"";if(value)release(value);return out;
    }
    template<class Visit> void each(Visit visit) const {
        auto domainGet=symbol<void*(*)()>("il2cpp_domain_get");
        auto assemblies=symbol<void**(*)(void*,std::size_t*)>("il2cpp_domain_get_assemblies");
        auto imageOf=symbol<const void*(*)(const void*)>("il2cpp_assembly_get_image");
        auto classCount=symbol<std::size_t(*)(const void*)>("il2cpp_image_get_class_count");
        auto classAt=symbol<const void*(*)(const void*,std::size_t)>("il2cpp_image_get_class");
        auto methods=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
        std::size_t count=0;auto** list=assemblies(domainGet(),&count);
        for(std::size_t a=0;a<count;++a) {
            const auto* image=imageOf(list[a]);if(!image)continue;
            for(std::size_t c=0,n=classCount(image);c<n;++c) {
                const auto* klass=classAt(image,c);if(!klass)continue;
                void* it=nullptr;while(const auto* method=methods(klass,&it))visit(klass,method);
            }
        }
    }
    std::string qualified(const void* klass,const void* method,bool signature) const {
        static auto klassName=symbol<const char*(*)(const void*)>("il2cpp_class_get_name");
        static auto klassSpace=symbol<const char*(*)(const void*)>("il2cpp_class_get_namespace");
        static auto declaring=symbol<const void*(*)(const void*)>("il2cpp_class_get_declaring_type");
        static auto methodName=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
        static auto count=symbol<unsigned(*)(const void*)>("il2cpp_method_get_param_count");
        static auto param=symbol<const void*(*)(const void*,unsigned)>("il2cpp_method_get_param");
        static auto result=symbol<const void*(*)(const void*)>("il2cpp_method_get_return_type");
        std::string type=klassName(klass);
        for(auto* outer=declaring(klass);outer;outer=declaring(outer))type=std::string(klassName(outer))+"/"+type;
        std::string out=std::string(klassSpace(klass))+"."+type+"::"+methodName(method);
        if(signature) {
            out+="(";for(unsigned i=0,n=count(method);i<n;++i){if(i)out+=",";out+=typeText(param(method,i));}
            out+=")->"+typeText(result(method));
        }
        return out;
    }
public:
    // Methods whose name contains `method` in classes whose name contains `klass`.
    nlohmann::json search(const std::string& method,const std::string& klass,std::size_t limit) const {
        static auto methodName=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
        static auto klassName=symbol<const char*(*)(const void*)>("il2cpp_class_get_name");
        nlohmann::json out=nlohmann::json::array();
        if(method.size()<3)throw std::runtime_error("method search needs at least 3 characters");
        each([&](const void* c,const void* m){
            if(out.size()>=limit)return;
            const std::string name=methodName(m);if(name.find(method)==std::string::npos)return;
            if(!klass.empty()&&std::string(klassName(c)).find(klass)==std::string::npos)return;
            out.push_back(qualified(c,m,true));
        });
        return out;
    }
    // Classes named `name` (any namespace): parents, static fields (with the
    // class of the object they hold), instance fields.
    nlohmann::json classInfo(const std::string& name,std::size_t limit=6,bool readStaticObjects=true) const {
        static auto klassName=symbol<const char*(*)(const void*)>("il2cpp_class_get_name");
        static auto klassSpace=symbol<const char*(*)(const void*)>("il2cpp_class_get_namespace");
        static auto parent=symbol<const void*(*)(const void*)>("il2cpp_class_get_parent");
        static auto declaring=symbol<const void*(*)(const void*)>("il2cpp_class_get_declaring_type");
        static auto fieldsOf=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_fields");
        static auto fieldName=symbol<const char*(*)(const void*)>("il2cpp_field_get_name");
        static auto fieldType=symbol<const void*(*)(const void*)>("il2cpp_field_get_type");
        static auto fieldFlags=symbol<int(*)(const void*)>("il2cpp_field_get_flags");
        static auto staticValue=symbol<void(*)(const void*,void*)>("il2cpp_field_static_get_value");
        static auto typeKind=symbol<int(*)(const void*)>("il2cpp_type_get_type");
        static auto objectClass=symbol<const void*(*)(void*)>("il2cpp_object_get_class");
        static auto classInit=symbol<void(*)(const void*)>("il2cpp_runtime_class_init");
        nlohmann::json out=nlohmann::json::array();std::set<const void*> seen;
        auto domainGet=symbol<void*(*)()>("il2cpp_domain_get");
        auto assemblies=symbol<void**(*)(void*,std::size_t*)>("il2cpp_domain_get_assemblies");
        auto imageOf=symbol<const void*(*)(const void*)>("il2cpp_assembly_get_image");
        auto imageName=symbol<const char*(*)(const void*)>("il2cpp_image_get_name");
        auto classCount=symbol<std::size_t(*)(const void*)>("il2cpp_image_get_class_count");
        auto classAt=symbol<const void*(*)(const void*,std::size_t)>("il2cpp_image_get_class");
        std::size_t count=0;auto** list=assemblies(domainGet(),&count);
        for(std::size_t a=0;a<count&&out.size()<limit;++a) {
            const auto* image=imageOf(list[a]);if(!image)continue;
            for(std::size_t c=0,n=classCount(image);c<n&&out.size()<limit;++c) {
                const auto* klass=classAt(image,c);if(!klass||name!=klassName(klass)||!seen.insert(klass).second)continue;
                std::string fullName=klassName(klass);const auto* top=klass;
                for(auto* outer=declaring(klass);outer;outer=declaring(outer)) {fullName=std::string(klassName(outer))+"/"+fullName;top=outer;}
                nlohmann::json info;info["class"]=std::string(klassSpace(top))+"."+fullName;
                info["assembly"]=imageName(image);
                nlohmann::json parents=nlohmann::json::array();for(auto* p=parent(klass);p;p=parent(p))parents.push_back(std::string(klassSpace(p))+"."+klassName(p));
                info["parents"]=parents;info["static"]=nlohmann::json::array();info["instance"]=nlohmann::json::array();
                for(const void* k=klass;k;k=parent(k)) {
                    void* it=nullptr;
                    while(const auto* field=fieldsOf(k,&it)) {
                        const auto* type=fieldType(field);nlohmann::json row={{"name",fieldName(field)},{"type",typeText(type)},{"declared",klassName(k)}};
                        if(fieldFlags(field)&0x10) {
                            const int kind=typeKind(type);
                            if(readStaticObjects&&(kind==0x12||kind==0x15||kind==0x1c)) { // class, generic instance, object
                                classInit(k);void* value=nullptr;staticValue(field,&value);
                                row["holds"]=value?std::string(klassSpace(objectClass(value)))+"."+klassName(objectClass(value)):"null";
                            }
                            info["static"].push_back(row);
                        } else if(info["instance"].size()<256) info["instance"].push_back(row);
                    }
                }
                out.push_back(std::move(info));
            }
        }
        return out;
    }
    // Instance field values of an object (primitives and strings; other types by name).
    nlohmann::json fields(void* object,std::size_t limit=48) const {
        static auto classOf=symbol<const void*(*)(void*)>("il2cpp_object_get_class");
        static auto parent=symbol<const void*(*)(const void*)>("il2cpp_class_get_parent");
        static auto fieldsOf=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_fields");
        static auto fieldName=symbol<const char*(*)(const void*)>("il2cpp_field_get_name");
        static auto fieldType=symbol<const void*(*)(const void*)>("il2cpp_field_get_type");
        static auto fieldOffset=symbol<std::size_t(*)(const void*)>("il2cpp_field_get_offset");
        static auto fieldFlags=symbol<int(*)(const void*)>("il2cpp_field_get_flags");
        static auto typeKind=symbol<int(*)(const void*)>("il2cpp_type_get_type");
        static auto stringLength=symbol<int(*)(void*)>("il2cpp_string_length");
        static auto stringChars=symbol<const char16_t*(*)(void*)>("il2cpp_string_chars");
        nlohmann::json out=nlohmann::json::object();if(!object)return out;
        for(const void* klass=classOf(object);klass&&out.size()<limit;klass=parent(klass)) {
            void* it=nullptr;
            while(const auto* field=fieldsOf(klass,&it)) {
                if(out.size()>=limit)break;
                if(fieldFlags(field)&0x10)continue; // static
                const auto* type=fieldType(field);const auto* at=static_cast<const unsigned char*>(object)+fieldOffset(field);
                nlohmann::json value;
                switch(typeKind(type)) {
                    case 0x02:value=*reinterpret_cast<const bool*>(at);break;
                    case 0x04:case 0x05:value=int(*reinterpret_cast<const std::int8_t*>(at));break;
                    case 0x06:case 0x07:value=int(*reinterpret_cast<const std::int16_t*>(at));break;
                    case 0x08:value=*reinterpret_cast<const std::int32_t*>(at);break;
                    case 0x09:value=*reinterpret_cast<const std::uint32_t*>(at);break;
                    case 0x0a:value=*reinterpret_cast<const std::int64_t*>(at);break;
                    case 0x0b:value=*reinterpret_cast<const std::uint64_t*>(at);break;
                    case 0x0c:value=*reinterpret_cast<const float*>(at);break;
                    case 0x0d:value=*reinterpret_cast<const double*>(at);break;
                    case 0x0e: {
                        void* text=*reinterpret_cast<void* const*>(at);std::string utf8;
                        if(text){const auto n=stringLength(text);const auto* c=stringChars(text);for(int i=0;i<n&&i<200;++i){const auto ch=c[i];if(ch<0x80)utf8+=char(ch);else if(ch<0x800){utf8+=char(0xC0|(ch>>6));utf8+=char(0x80|(ch&0x3F));}else{utf8+=char(0xE0|(ch>>12));utf8+=char(0x80|((ch>>6)&0x3F));utf8+=char(0x80|(ch&0x3F));}}}
                        value=text?nlohmann::json(utf8):nlohmann::json(nullptr);break;
                    }
                    case 0x11:value="<"+typeText(type)+">";if(typeText(type)=="UnityEngine.Vector3"){const auto* v=reinterpret_cast<const float*>(at);value={v[0],v[1],v[2]};}break;
                    default:value="<"+typeText(type)+">";
                }
                out[fieldName(field)]=value;
            }
        }
        return out;
    }
    // Resolve return addresses inside GameAssembly to "Class::Method+offset".
    nlohmann::json resolve(const std::vector<std::uintptr_t>& addresses) {
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"GameAssembly.dll"));
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        const auto limit=base+pe->OptionalHeader.SizeOfImage;
        if(byAddress.empty()) {
            each([&](const void* c,const void* m){
                const auto address=*reinterpret_cast<const std::uintptr_t*>(m);
                if(address>=base&&address<limit)byAddress.push_back({address,qualified(c,m,false)});
            });
            std::sort(byAddress.begin(),byAddress.end(),[](const Method& a,const Method& b){return a.address<b.address;});
        }
        nlohmann::json out=nlohmann::json::array();
        for(auto address:addresses) {
            if(address<base||address>=limit) {out.push_back("<outside GameAssembly>");continue;}
            auto it=std::upper_bound(byAddress.begin(),byAddress.end(),address,[](std::uintptr_t a,const Method& m){return a<m.address;});
            if(it==byAddress.begin()) {out.push_back("rva "+std::to_string(address-base));continue;}
            --it;out.push_back(it->name+"+"+std::to_string(address-it->address));
        }
        return out;
    }
};
}
