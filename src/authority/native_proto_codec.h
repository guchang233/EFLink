#pragma once
// Pure native Google.Protobuf serialization. No gameplay dispatch or field writes.
#include "BetterEndfield/ModuleApi.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace eflink::authority {
class NativeProtoCodec {
    const BE_HostApiV1* api=nullptr;
    BE_ResolvedMethodV1 toBytes{},toBase64{},length{};
    struct Root {
        const BE_HostApiV1* api;std::uint32_t handle;
        Root(const BE_HostApiV1* a,void* object):api(a),handle(object?a->gchandle_new(a->context,object,0):0) {
            if(!handle)throw std::runtime_error("protobuf temporary root unavailable");
        }
        ~Root(){api->gchandle_free(api->context,handle);}
        Root(const Root&)=delete;
    };
public:
    void bind(const BE_HostApiV1* value) {
        if(!value||!value->runtime_invoke||!value->gchandle_new||!value->gchandle_free||!value->copy_managed_string)
            throw std::runtime_error("protobuf runtime unavailable");
        BE_MethodDescriptorV1 encode{"Google.Protobuf.dll","Google.Protobuf","MessageExtensions","ToByteString","Google.Protobuf.IMessage","Google.Protobuf.ByteString",1};
        BE_MethodDescriptorV1 format{"Google.Protobuf.dll","Google.Protobuf","ByteString","ToBase64",nullptr,"System.String",0};
        BE_MethodDescriptorV1 size{"Google.Protobuf.dll","Google.Protobuf","ByteString","get_Length",nullptr,"System.Int32",0};
        if(value->resolve_method(value->context,&encode,&toBytes)!=BE_Result_Ok||
           value->resolve_method(value->context,&format,&toBase64)!=BE_Result_Ok||
           value->resolve_method(value->context,&size,&length)!=BE_Result_Ok||!toBytes.method_info||!toBase64.method_info||!length.method_info)
            throw std::runtime_error("protobuf serialization methods unavailable");
        api=value;
    }
    std::string serialize(void* message,std::size_t maxBytes=1024*1024) const {
        if(!api||!message||maxBytes>1024*1024)throw std::runtime_error("protobuf codec not bound or limit invalid");
        void* exception=nullptr;void* args[]={message};
        auto* bytes=api->runtime_invoke(api->context,toBytes.method_info,nullptr,args,&exception);
        if(exception||!bytes)throw std::runtime_error("protobuf serialization rejected");
        Root bytesRoot(api,bytes);
        auto* size=api->runtime_invoke(api->context,length.method_info,bytes,nullptr,&exception);
        auto* raw=size&&!exception?api->object_unbox(api->context,size):nullptr;
        if(!raw)throw std::runtime_error("protobuf length unavailable");
        const auto byteCount=*static_cast<const std::int32_t*>(raw);
        if(byteCount<0||std::size_t(byteCount)>maxBytes)throw std::runtime_error("protobuf serialized message exceeds limit");
        auto* text=api->runtime_invoke(api->context,toBase64.method_info,bytes,nullptr,&exception);
        if(exception||!text)throw std::runtime_error("protobuf formatting rejected");
        Root textRoot(api,text);
        std::vector<char> buffer((std::size_t(byteCount)+2)/3*4+1);
        const int required=api->copy_managed_string(api->context,text,buffer.data(),buffer.size());
        if(required<0||std::size_t(required)>=buffer.size())throw std::runtime_error("protobuf serialized message exceeds limit");
        return buffer.data();
    }
};
}
