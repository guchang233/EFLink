// Local read-only development inspector. No game calls, hooks, input or memory writes.
// Native code is resolved by current metadata and copied only from executable pages
// in GameAssembly; this tool never takes an arbitrary process address.
#include "BetterEndfield/ThirdPartyModule.h"
#include "runtime_scope.h"
#include "nlohmann/json.hpp"
#include <string>
#include <mutex>
#include <cstring>
#include <stdexcept>
using nlohmann::json;
namespace {
#ifndef EFLINK_PROBE_ID
#define EFLINK_PROBE_ID "eflink.probe1"
#endif
constexpr char id[]=EFLINK_PROBE_ID;
BE_ThirdPartyHostV1 host{};std::mutex mutex;bool initialized=false;
template<class T> T sym(HMODULE dll,const char* name) {
    auto result=reinterpret_cast<T>(GetProcAddress(dll,name));
    if(!result)throw std::runtime_error(std::string("export unavailable: ")+name);return result;
}
const BE_HostApiV1* runtime() {return host.get_runtime?host.get_runtime(host.context):host.runtime;}
BE_Result BE_CALL init(const BE_ThirdPartyHostV1* input,const char*) {
    std::lock_guard lock(mutex);
    if(!input||input->version!=1||!input->module_id||std::string(input->module_id)!=id||!input->reply||
       input->struct_size<offsetof(BE_ThirdPartyHostV1,emit)+sizeof(input->emit))return BE_Result_ContractMismatch;
    host={};std::memcpy(&host,input,(std::min)(std::size_t(input->struct_size),sizeof(host)));initialized=true;return BE_Result_Ok;
}
json inspect(const json& request) {
    RuntimeThreadScope attached(true);if(!attached.ready())throw std::runtime_error("runtime worker unavailable");
    auto* api=runtime();auto dll=GetModuleHandleW(L"GameAssembly.dll");
    if(!api||!dll)throw std::runtime_error("runtime unavailable");
    const auto name=request.at("class").get<std::string>(),space=request.value("namespace",std::string("Beyond.Gameplay.Core"));
    const auto library=request.value("assembly",std::string("Gameplay.Beyond.dll"));
    const bool protocol=library=="Beyond.Network.Proto.dll"&&space=="Proto";
    const bool application=(library=="Gameplay.Beyond.dll"||library=="Common.Beyond.dll"||
        library=="Network.Beyond.dll")&&(space=="Beyond"||space.rfind("Beyond.",0)==0);
    if(name.empty()||name.size()>200||space.size()>200||(!protocol&&!application))
        throw std::runtime_error("invalid class/assembly");
    BE_ResolvedClassV1 type{};
    if(api->resolve_class(api->context,library.c_str(),space.c_str(),name.c_str(),&type)!=BE_Result_Ok)throw std::runtime_error("class unresolved");
    auto methods=sym<const void*(*)(const void*,void**)>(dll,"il2cpp_class_get_methods");
    auto methodName=sym<const char*(*)(const void*)>(dll,"il2cpp_method_get_name");
    auto count=sym<unsigned(*)(const void*)>(dll,"il2cpp_method_get_param_count");
    auto param=sym<const void*(*)(const void*,unsigned)>(dll,"il2cpp_method_get_param");
    auto ret=sym<const void*(*)(const void*)>(dll,"il2cpp_method_get_return_type");
    auto typeName=sym<char*(*)(const void*)>(dll,"il2cpp_type_get_name");
    auto freeMemory=sym<void(*)(void*)>(dll,"il2cpp_free");
    auto text=[&](const void* t){char* p=typeName(t);std::string v=p?p:"";if(p)freeMemory(p);return v;};
    const auto wanted=request.value("method",std::string{});
    const auto bytes=request.value("bytes",0u);if(bytes>8192)throw std::runtime_error("code bound exceeded");
    const auto offset=request.value("offset",0u);
    if(offset>32768||(offset&&(!bytes||wanted.empty())))throw std::runtime_error("invalid named-method code offset");
    auto base=reinterpret_cast<std::uintptr_t>(dll);
    auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(dll);
    auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    const auto imageSize=pe->OptionalHeader.SizeOfImage;
    json output=json::array();void* iterator=nullptr;
    while(auto* method=methods(type.class_info,&iterator)) {
        std::string label=methodName(method);if(!wanted.empty()&&label!=wanted)continue;
        std::string parameters;auto n=count(method);if(n>32)continue;
        for(unsigned i=0;i<n;++i) {if(i)parameters+='|';parameters+=text(param(method,i));}
        auto returns=text(ret(method));
        BE_MethodDescriptorV1 descriptor{library.c_str(),space.c_str(),name.c_str(),label.c_str(),parameters.empty()?nullptr:parameters.c_str(),returns.c_str(),n};
        BE_ResolvedMethodV1 resolved{};auto result=api->resolve_method(api->context,&descriptor,&resolved);
        json item={{"name",label},{"parameters",parameters},{"return",returns},{"resolved",int(result)}};
        const auto address=reinterpret_cast<std::uintptr_t>(resolved.method_pointer);
        if(result==BE_Result_Ok&&address>=base&&address<base+imageSize) {
            item["rva"]=address-base;
            if(bytes&&offset<imageSize-(address-base)) {
                const auto start=address+offset;item["code_rva"]=start-base;
                MEMORY_BASIC_INFORMATION info{};VirtualQuery(reinterpret_cast<void*>(start),&info,sizeof(info));
                const auto protection=info.Protect&0xff;
                if(info.State==MEM_COMMIT&&!(info.Protect&PAGE_GUARD)&&
                   (protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY)) {
                    auto end=(std::min)(reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize,base+imageSize);
                    auto length=(std::min)(std::size_t(bytes),std::size_t(end-start));
                    static const char hex[]="0123456789abcdef";std::string code;code.reserve(length*2);
                    auto* data=reinterpret_cast<const unsigned char*>(start);
                    for(std::size_t i=0;i<length;++i) {code+=hex[data[i]>>4];code+=hex[data[i]&15];}
                    item["code"]=code;
                }
            }
        }
        output.push_back(std::move(item));if(output.size()>=256)break;
    }
    json literals=json::object();
    auto fields=sym<const void*(*)(const void*,void**)>(dll,"il2cpp_class_get_fields");
    auto flags=sym<unsigned(*)(const void*)>(dll,"il2cpp_field_get_flags");
    auto fieldName=sym<const char*(*)(const void*)>(dll,"il2cpp_field_get_name");
    auto fieldType=sym<const void*(*)(const void*)>(dll,"il2cpp_field_get_type");
    auto staticValue=sym<void(*)(const void*,void*)>(dll,"il2cpp_field_static_get_value");
    auto isEnum=sym<bool(*)(const void*)>(dll,"il2cpp_class_is_enum");
    auto enumBase=sym<const void*(*)(const void*)>(dll,"il2cpp_class_enum_basetype");
    const bool intEnum=isEnum(type.class_info)&&text(enumBase(type.class_info))=="System.Int32";
    iterator=nullptr;
    while(auto* field=fields(type.class_info,&iterator)) {
        if(!(flags(field)&0x40))continue;
        if(text(fieldType(field))=="System.Int32"||intEnum) {
            int value=0;staticValue(field,&value);literals[fieldName(field)]=value;
        }
    }
    auto valueType=sym<bool(*)(const void*)>(dll,"il2cpp_class_is_valuetype");
    auto valueSize=sym<int(*)(const void*,unsigned*)>(dll,"il2cpp_class_value_size");
    json layout={{"is_value_type",valueType(type.class_info)}};
    if(valueType(type.class_info)) {unsigned alignment=0;layout["value_size"]=valueSize(type.class_info,&alignment);layout["alignment"]=alignment;}
    return {{"class",name},{"assembly",library},{"namespace",space},{"methods",output},
        {"literals",literals},{"layout",layout},{"gameplay_modified",false}};
}
BE_Result BE_CALL configure(const char*) {return BE_Result_Ok;}
BE_Result BE_CALL message(const char* request,const char* body) {
    std::lock_guard lock(mutex);if(!initialized)return BE_Result_NotReady;
    json result;BE_Result code=BE_Result_Ok;
    try {result=inspect(json::parse(body));}catch(const std::exception& e){result={{"error",e.what()}};code=BE_Result_InvalidArgument;}
    auto serialized=result.dump();return host.reply(host.context,request,code,serialized.c_str());
}
void BE_CALL stop() {std::lock_guard lock(mutex);initialized=false;host={};}
const BE_ThirdPartyModuleV1 module{sizeof(BE_ThirdPartyModuleV1),1,id,init,configure,message,stop};
}
extern "C" __declspec(dllexport) const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1() {return &module;}
