#pragma once
// Read-only IL2CPP metadata helpers (diagnostics): find methods by name and
// map native code addresses back to the methods containing them.
#include "nlohmann/json.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <map>
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
