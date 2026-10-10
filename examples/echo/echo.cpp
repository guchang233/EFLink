#include <windows.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <BetterEndfield/ThirdPartyModule.h>
#include <EndfieldLink/EndfieldLink.h>

namespace {
const EFLink_ApiV1* api=nullptr;
BE_ThirdPartyHostV1 host{};
uint32_t channel=0;
std::atomic<unsigned> replies{0};
void EFLINK_CALL receive(void*,uint16_t from,const void* bytes,uint32_t size) {
    if(!bytes||size<1||size>1024) return;
    const auto* data=static_cast<const unsigned char*>(bytes);
    if(data[0]==0) {
        unsigned char answer[1024];std::memcpy(answer,data,size);answer[0]=1;
        api->send(channel,from,answer,size,EFLink_Reliable);
    } else if(data[0]==1) {
        ++replies;
        if(host.log) host.log(host.context,"EF Link echo response received");
    }
}
BE_Result BE_CALL initialize(const BE_ThirdPartyHostV1* input,const char*) {
    if(!input||input->version!=1||input->struct_size<sizeof(*input)) return BE_Result_InvalidArgument;
    auto dll=GetModuleHandleW(L"eflink.core.dll");
    auto get=dll?reinterpret_cast<EFLink_GetApiV1Fn>(GetProcAddress(dll,EFLINK_ENTRY_V1)):nullptr;
    api=get?get():nullptr;
    if(!api||api->version!=1||api->struct_size<sizeof(*api)) return BE_Result_NotReady;
    host=*input;replies=0;
    return api->open_channel("eflink.example.echo.v1",receive,nullptr,&channel)==EFLink_Ok?BE_Result_Ok:BE_Result_Failed;
}
BE_Result BE_CALL configure(const char*) {return BE_Result_Ok;}
BE_Result BE_CALL message(const char* request,const char* body) {
    if(!api) return BE_Result_NotReady;
    auto size=body?std::strlen(body):0;
    if(size>1023) return BE_Result_InvalidArgument;
    unsigned char packet[1024]{};
    if(size) std::memcpy(packet+1,body,size);
    const auto sent=api->send(channel,EFLINK_EVERYONE,packet,uint32_t(size+1),EFLink_Reliable);
    char result[128];std::snprintf(result,sizeof(result),"{\"send_result\":%u,\"responses_received\":%u}",unsigned(sent),replies.load());
    return host.reply?host.reply(host.context,request,BE_Result_Ok,result):BE_Result_Ok;
}
void BE_CALL shutdown() {
    if(api&&channel) api->close_channel(channel); // Waits for in-flight receive calls.
    channel=0;api=nullptr;host={};
}
const BE_ThirdPartyModuleV1 module{sizeof(module),1,"eflink.example.echo",initialize,configure,message,shutdown};
}
extern "C" __declspec(dllexport) const BE_ThirdPartyModuleV1* BE_CALL BetterEndfield_GetThirdPartyModuleV1() {return &module;}
