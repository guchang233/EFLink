#pragma once
// Pure native protobuf serialization. No gameplay dispatch or field writes.
// Game 1.6.3 uses protobuf-net (ProtoBuf.Serializer.NonGeneric.Serialize into a
// MemoryStream); earlier clients used Google.Protobuf ToByteString.
#include "BetterEndfield/ModuleApi.h"
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include <windows.h>

namespace eflink::authority {
class NativeProtoCodec {
    const BE_HostApiV1* api=nullptr;
    BE_ResolvedMethodV1 toBytes{},toBase64{},length{};
    // protobuf-net path
    bool protobufNet=false;BE_ResolvedClassV1 streamClass{};BE_ResolvedMethodV1 streamCtor{},serializeNet{},toArray{};
    static std::string base64(const unsigned char* data,std::size_t size) {
        static const char a[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string t;t.reserve((size+2)/3*4);
        for(std::size_t i=0;i<size;i+=3) {
            unsigned v=unsigned(data[i])<<16;if(i+1<size)v|=unsigned(data[i+1])<<8;if(i+2<size)v|=data[i+2];
            t+=a[v>>18];t+=a[(v>>12)&63];t+=i+1<size?a[(v>>6)&63]:'=';t+=i+2<size?a[v&63]:'=';
        }
        return t;
    }
    bool bindProtobufNet(const BE_HostApiV1* value) {
        BE_MethodDescriptorV1 ctor{"mscorlib.dll","System.IO","MemoryStream",".ctor",nullptr,"System.Void",0};
        BE_MethodDescriptorV1 serialize{"protobuf-net.dll","ProtoBuf","Serializer/NonGeneric","Serialize","System.IO.Stream|System.Object","System.Void",2};
        BE_MethodDescriptorV1 bytes{"mscorlib.dll","System.IO","MemoryStream","ToArray",nullptr,"System.Byte[]",0};
        return value->resolve_class(value->context,"mscorlib.dll","System.IO","MemoryStream",&streamClass)==BE_Result_Ok&&streamClass.class_info&&
            value->resolve_method(value->context,&ctor,&streamCtor)==BE_Result_Ok&&streamCtor.method_info&&
            value->resolve_method(value->context,&serialize,&serializeNet)==BE_Result_Ok&&serializeNet.method_info&&
            value->resolve_method(value->context,&bytes,&toArray)==BE_Result_Ok&&toArray.method_info;
    }
    std::string serializeProtobufNet(void* message,std::size_t maxBytes) const {
        void* exception=nullptr;
        auto* stream=api->object_new(api->context,streamClass.class_info);if(!stream)throw std::runtime_error("MemoryStream allocation failed");
        Root streamRoot(api,stream);
        api->runtime_invoke(api->context,streamCtor.method_info,stream,nullptr,&exception);
        if(exception)throw std::runtime_error("MemoryStream constructor rejected");
        void* args[]={stream,message};
        api->runtime_invoke(api->context,serializeNet.method_info,nullptr,args,&exception);
        if(exception)throw std::runtime_error("protobuf-net serialization rejected");
        auto* array=api->runtime_invoke(api->context,toArray.method_info,stream,nullptr,&exception);
        if(exception||!array)throw std::runtime_error("MemoryStream.ToArray rejected");
        Root arrayRoot(api,array);
        const auto length=reinterpret_cast<std::uintptr_t(*)(void*)>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_array_length"));
        if(!length)throw std::runtime_error("il2cpp_array_length unavailable");
        const auto count=std::size_t(length(array));
        if(count>maxBytes)throw std::runtime_error("protobuf serialized message exceeds limit");
        // Standard Windows x64 IL2CPP array: 32-byte prefix (klass, monitor, bounds, length).
        return base64(static_cast<const unsigned char*>(array)+32,count);
    }
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
        api=value;
        if(bindProtobufNet(value)) {protobufNet=true;return;}
        if(value->resolve_method(value->context,&encode,&toBytes)!=BE_Result_Ok||
           value->resolve_method(value->context,&format,&toBase64)!=BE_Result_Ok||
           value->resolve_method(value->context,&size,&length)!=BE_Result_Ok||!toBytes.method_info||!toBase64.method_info||!length.method_info)
            throw std::runtime_error("protobuf serialization methods unavailable");
        api=value;
    }
    std::string serialize(void* message,std::size_t maxBytes=1024*1024) const {
        if(!api||!message||maxBytes>1024*1024)throw std::runtime_error("protobuf codec not bound or limit invalid");
        if(protobufNet)return serializeProtobufNet(message,maxBytes);
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
