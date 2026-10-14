// Development interoperability proof. Only parses caller-provided synthetic
// protobuf bytes into newly allocated objects. No gameplay calls or hooks.
#include "BetterEndfield/ThirdPartyModule.h"
#include "../runtime_scope.h"
#include "native_build_policy.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using nlohmann::json;
#ifndef EFLINK_NATIVE_WIRE_ID
#define EFLINK_NATIVE_WIRE_ID "eflink.nativewire1"
#endif
constexpr char id[]=EFLINK_NATIVE_WIRE_ID;
BE_ThirdPartyHostV1 host{};
std::mutex lifecycle;
constexpr std::size_t maximumBytes=65536;
constexpr std::array allowed={"BATTLE_CLIENT_OP_DATA","CS_BATTLE_OP","SC_SELF_SCENE_INFO",
    "SC_ENTER_SCENE_NOTIFY","SC_CHANGE_SCENE_BEGIN_NOTIFY","SC_OBJECT_ENTER_VIEW","SC_ENTITY_PROPERTY_CHANGE"};

template<class T> T symbol(HMODULE module,const char* name) {
    auto value=reinterpret_cast<T>(GetProcAddress(module,name));
    if(!value)throw std::runtime_error(std::string("missing metadata export: ")+name);
    return value;
}
struct Root {
    const BE_HostApiV1* api;std::uint32_t handle;
    Root(const BE_HostApiV1* value,void* object):api(value),handle(object?value->gchandle_new(value->context,object,0):0) {
        if(!handle)throw std::runtime_error("managed object allocation failed");
    }
    ~Root(){api->gchandle_free(api->context,handle);}
    Root(const Root&)=delete;
};
std::vector<unsigned char> unhex(const std::string& text) {
    if(text.size()%2||text.size()>maximumBytes*2)throw std::runtime_error("fixture size invalid");
    auto nibble=[](char c)->unsigned {
        if(c>='0'&&c<='9')return unsigned(c-'0');
        if(c>='a'&&c<='f')return unsigned(c-'a'+10);
        if(c>='A'&&c<='F')return unsigned(c-'A'+10);
        throw std::runtime_error("fixture hex invalid");
    };
    std::vector<unsigned char> bytes;
    bytes.reserve(text.size()/2);
    for(std::size_t i=0;i<text.size();i+=2)bytes.push_back(static_cast<unsigned char>((nibble(text[i])<<4)|nibble(text[i+1])));
    return bytes;
}
std::string base64(const std::vector<unsigned char>& bytes) {
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;text.reserve((bytes.size()+2)/3*4);
    for(std::size_t i=0;i<bytes.size();i+=3) {
        unsigned value=unsigned(bytes[i])<<16;
        if(i+1<bytes.size())value|=unsigned(bytes[i+1])<<8;
        if(i+2<bytes.size())value|=bytes[i+2];
        text+=alphabet[value>>18];text+=alphabet[(value>>12)&63];
        text+=i+1<bytes.size()?alphabet[(value>>6)&63]:'=';
        text+=i+2<bytes.size()?alphabet[value&63]:'=';
    }
    return text;
}
std::string hexFromBase64(const std::string& text) {
    constexpr char digits[]="0123456789abcdef";
    std::string hex;unsigned bits=0,count=0;
    for(char c:text) {
        if(c=='=')break;
        auto pos=std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/").find(c);
        if(pos==std::string_view::npos)throw std::runtime_error("native base64 invalid");
        bits=(bits<<6)|unsigned(pos);count+=6;
        if(count>=8) {count-=8;unsigned byte=(bits>>count)&255;hex+=digits[byte>>4];hex+=digits[byte&15];}
        if(hex.size()>maximumBytes*2)throw std::runtime_error("native result too large");
    }
    return hex;
}

json roundtrip(const json& input) {
    auto type=input.at("type").get<std::string>();
    if(std::find(allowed.begin(),allowed.end(),type)==allowed.end())throw std::runtime_error("type not permitted");
    const auto bytes=unhex(input.at("hex").get<std::string>());
    RuntimeThreadScope attached(true);
    auto* api=host.get_runtime?host.get_runtime(host.context):host.runtime;
    auto module=GetModuleHandleW(L"GameAssembly.dll");
    if(!attached.ready()||!api||!module)throw std::runtime_error("runtime unavailable");
    if(!eflink::worldauthority::readOnlyBuildSupported(eflink::worldauthority::nativeGameVersion(api)))
        throw std::runtime_error("unverified game build; native synthetic parsing disabled");
    auto methods=symbol<const void*(*)(const void*,void**)>(module,"il2cpp_class_get_methods");
    auto methodName=symbol<const char*(*)(const void*)>(module,"il2cpp_method_get_name");
    auto paramCount=symbol<unsigned(*)(const void*)>(module,"il2cpp_method_get_param_count");
    auto parameter=symbol<const void*(*)(const void*,unsigned)>(module,"il2cpp_method_get_param");
    auto typeName=symbol<char*(*)(const void*)>(module,"il2cpp_type_get_name");
    auto freeMemory=symbol<void(*)(void*)>(module,"il2cpp_free");
    auto flags=symbol<unsigned(*)(const void*,unsigned*)>(module,"il2cpp_method_get_flags");
    auto parent=symbol<const void*(*)(const void*)>(module,"il2cpp_class_get_parent");
    auto classOf=symbol<const void*(*)(void*)>(module,"il2cpp_object_get_class");
    auto valueSize=symbol<int(*)(const void*,unsigned*)>(module,"il2cpp_class_value_size");
    auto isValue=symbol<bool(*)(const void*)>(module,"il2cpp_class_is_valuetype");
    auto resolve=[&](const char* assembly,const char* space,const char* name) {
        BE_ResolvedClassV1 result{};
        if(api->resolve_class(api->context,assembly,space,name,&result)!=BE_Result_Ok||!result.class_info)
            throw std::runtime_error(std::string("class unresolved: ")+name);
        return result.class_info;
    };
    auto find=[&](const void* klass,const char* name,const char* param,bool isStatic)->const void* {
        for(unsigned depth=0;klass&&depth<16;++depth,klass=parent(klass)) {
            void* iterator=nullptr;const void* match=nullptr;
            while(auto* method=methods(klass,&iterator)) {
                unsigned implementation=0;
                if(std::strcmp(methodName(method),name)||paramCount(method)!=(param?1u:0u)||bool(flags(method,&implementation)&16)!=isStatic)continue;
                if(param) {
                    char* text=typeName(parameter(method,0));bool same=text&&std::strcmp(text,param)==0;
                    if(text)freeMemory(text);if(!same)continue;
                }
                if(match)throw std::runtime_error(std::string("ambiguous method: ")+name);
                match=method;
            }
            if(match)return match;
        }
        throw std::runtime_error(std::string("method unresolved: ")+name);
    };
    auto invoke=[&](const void* method,void* self,void* argument=nullptr) {
        void* exception=nullptr;void* args[]={argument};
        auto* result=api->runtime_invoke(api->context,method,self,paramCount(method)?args:nullptr,&exception);
        // Do not format arbitrary managed exceptions into logs.
        if(exception)throw std::runtime_error(std::string("native protobuf rejected call: ")+methodName(method));
        if(!result)throw std::runtime_error("native protobuf returned no object");
        return result;
    };
    auto* byteClass=resolve("Google.Protobuf.dll","Google.Protobuf","ByteString");
    auto* extensions=resolve("Google.Protobuf.dll","Google.Protobuf","MessageExtensions");
    auto* messageClass=resolve("Beyond.Network.Proto.dll","Proto",type.c_str());
    auto encoded=base64(bytes);
    auto* string=api->string_new(api->context,encoded.c_str());Root stringRoot(api,string);
    auto* data=invoke(find(byteClass,"FromBase64","System.String",true),nullptr,string);Root dataRoot(api,data);
    auto* parser=invoke(find(messageClass,"get_Parser",nullptr,true),nullptr);Root parserRoot(api,parser);
    auto* parsed=invoke(find(classOf(parser),"ParseFrom","Google.Protobuf.ByteString",false),parser,data);Root parsedRoot(api,parsed);
    if(classOf(parsed)!=messageClass)throw std::runtime_error("native parser type mismatch");
    auto get=[&](void* object,const char* name) {return invoke(find(classOf(object),name,nullptr,false),object);};
    auto scalar=[&]<class T>(void* object,const char* name)->T {
        auto* boxed=get(object,name);Root boxedRoot(api,boxed);unsigned alignment=0;
        if(!isValue(classOf(boxed))||valueSize(classOf(boxed),&alignment)!=sizeof(T))throw std::runtime_error("native scalar ABI mismatch");
        auto* value=api->object_unbox(api->context,boxed);if(!value)throw std::runtime_error("native scalar missing");
        T result;std::memcpy(&result,value,sizeof(T));return result;
    };
    json fields=json::object();
    if(type=="BATTLE_CLIENT_OP_DATA") {
        fields["owner_id"]=scalar.operator()<std::uint64_t>(parsed,"get_OwnerId");
        fields["chapter_map_id"]=scalar.operator()<std::int32_t>(parsed,"get_ChapterMapId");
        auto* damage=get(parsed,"get_EntityValueModifyData");Root damageRoot(api,damage);
        fields["entity_inst_id"]=scalar.operator()<std::uint64_t>(damage,"get_EntityInstId");
        fields["delta_value"]=scalar.operator()<double>(damage,"get_DeltaValue");
        auto* value=get(damage,"get_Value");Root valueRoot(api,value);
        fields["hp"]=scalar.operator()<double>(value,"get_Hp");
    } else if(type=="SC_SELF_SCENE_INFO"||type=="SC_ENTER_SCENE_NOTIFY"||type=="SC_CHANGE_SCENE_BEGIN_NOTIFY") {
        fields["scene_num_id"]=scalar.operator()<std::int32_t>(parsed,"get_SceneNumId");
        if(type!="SC_CHANGE_SCENE_BEGIN_NOTIFY")fields["scene_id"]=scalar.operator()<std::uint64_t>(parsed,"get_SceneId");
    }
    auto* output=invoke(find(extensions,"ToByteString","Google.Protobuf.IMessage",true),nullptr,parsed);Root outputRoot(api,output);
    auto* outputString=invoke(find(byteClass,"ToBase64",nullptr,false),output);Root outputStringRoot(api,outputString);
    std::vector<char> buffer((maximumBytes+2)/3*4+1);
    if(api->copy_managed_string(api->context,outputString,buffer.data(),buffer.size())<0)throw std::runtime_error("native string copy failed");
    const auto hex=hexFromBase64(buffer.data());
    return {{"type",type},{"native_parser_accepted",true},{"exact_wire_roundtrip",hex==hexFromBase64(encoded)},
        {"output_hex",hex},{"native_fields",fields},{"authority_established",false},{"world_state_modified",false}};
}
BE_Result BE_CALL initialize(const BE_ThirdPartyHostV1* input,const char*) {
    std::lock_guard lock(lifecycle);
    if(!input||input->version!=1||!input->module_id||std::string(input->module_id)!=id||!input->reply||
       input->struct_size<offsetof(BE_ThirdPartyHostV1,emit)+sizeof(input->emit))return BE_Result_ContractMismatch;
    host={};std::memcpy(&host,input,(std::min)(std::size_t(input->struct_size),sizeof(host)));return BE_Result_Ok;
}
BE_Result BE_CALL configure(const char*) {return BE_Result_Ok;}
BE_Result BE_CALL message(const char* request,const char* text) {
    std::lock_guard lock(lifecycle);json result;BE_Result code=BE_Result_Ok;
    try {
        const auto input=json::parse(text);auto action=input.value("action",std::string("status"));
        if(action=="roundtrip")result=roundtrip(input);
        else if(action=="status")result={{"synthetic_fixtures_only",true},{"hooks_installed",false},{"authority_established",false},{"allowed_types",allowed}};
        else throw std::runtime_error("unknown action");
    }catch(const std::exception& e){result={{"error",e.what()},{"authority_established",false},{"world_state_modified",false}};code=BE_Result_Failed;}
    auto body=result.dump();return host.reply(host.context,request,code,body.c_str());
}
void BE_CALL shutdown() {}
const BE_ThirdPartyModuleV1 api{sizeof(BE_ThirdPartyModuleV1),1,id,initialize,configure,message,shutdown};
}
extern "C" __declspec(dllexport) const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1(){return &api;}
