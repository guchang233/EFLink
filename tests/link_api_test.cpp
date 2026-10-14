// Loads the built EF Link DLL outside the game, drives it the way Better-Endfield and the
// module page do, and joins its room with a second network stack to check the public API.
#include "link_net.h"
#include "BetterEndfield/ThirdPartyModule.h"
#include "authority/efml_lifecycle.h"
#include "EndfieldLink/EndfieldLink.h"
#include "nlohmann/json.hpp"
#include <cstdio>

using nlohmann::json;
namespace {
int failures=0;
void check(bool condition,const char* what) {std::printf("%s %s\n",condition?"[ok]  ":"[FAIL]",what);if(!condition) ++failures;}
template<class F> bool waitFor(F condition,int ms=4000) {
    for(int i=0;i<ms/20;++i) {if(condition()) return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    return condition();
}
std::mutex replyMutex;json lastReply;int replies=0;
std::string moduleId;
void BE_CALL log(void*,const char*) {}
BE_Result BE_CALL reply(void*,const char*,BE_Result,const char* body) {
    std::lock_guard lock(replyMutex);lastReply=json::parse(body);++replies;return BE_Result_Ok;
}
BE_Result BE_CALL emit(void*,const char*) {return BE_Result_Ok;}
const BE_HostApiV1* BE_CALL noRuntime(void*) {return nullptr;}
BE_Result BE_CALL acceptGameTask(void*,void(BE_CALL*)(void*),void*) {return BE_Result_Ok;}
const BE_ThirdPartyModuleV1* module=nullptr;
json send(const json& body) {
    int before;{std::lock_guard lock(replyMutex);before=replies;}
    module->on_message("test",body.dump().c_str());
    waitFor([&]{std::lock_guard lock(replyMutex);return replies>before;},2000);
    std::lock_guard lock(replyMutex);return lastReply;
}
std::atomic<int> received=0,peerEvents=0;std::string receivedText;std::uint16_t receivedFrom=0;
void EFLINK_CALL onReceive(void*,std::uint16_t from,const void* data,std::uint32_t size) {
    receivedText.assign(static_cast<const char*>(data),size);receivedFrom=from;++received;
}
void EFLINK_CALL onPeer(void*,std::uint16_t,int) {++peerEvents;}
}

int main(int argc,char** argv) {
    if(argc<3) {std::printf("usage: link-api-test <dll> <module id>\n");return 2;}
    moduleId=argv[2];
    auto library=LoadLibraryA(argv[1]);
    check(library!=nullptr,"module DLL loads outside the game");if(!library) return 1;
    auto entry=reinterpret_cast<BE_GetThirdPartyModuleV1Fn>(GetProcAddress(library,BETTER_ENDFIELD_THIRD_PARTY_ENTRY_V1));
    auto getApi=reinterpret_cast<EFLink_GetApiV1Fn>(GetProcAddress(library,EFLINK_ENTRY_V1));
    check(entry&&getApi,"Better-Endfield entry and EFLink_GetApiV1 are exported");if(!entry||!getApi) return 1;
    module=entry();const auto* api=getApi();
    check(module&&module->id==moduleId&&api&&api->struct_size==sizeof(EFLink_ApiV1)&&api->version==EFLINK_API_VERSION_1,"ABI identity matches");
    auto efmlEntry=reinterpret_cast<BE_GetThirdPartyModuleV1Fn>(GetProcAddress(library,"EFML_GetModuleV1"));
    check(efmlEntry&&efmlEntry()==module&&module->struct_size==sizeof(eflink::worldauthority::EfmlModuleWithShutdownBarrierV1)&&
        reinterpret_cast<const eflink::worldauthority::EfmlModuleWithShutdownBarrierV1*>(module)->prepare_shutdown,
        "EFML shutdown barrier extends the unchanged V1 module prefix");
    eflink::worldauthority::EfmlHostExtensionV1 extension{};
    extension.struct_size=sizeof(extension);extension.version=1;
    extension.post_to_game_thread=acceptGameTask;extension.feature_flags=eflink::worldauthority::shutdownBarrierFeature;
    eflink::worldauthority::EfmlHostV1 extendedHost{};extendedHost.base.struct_size=sizeof(extendedHost);extendedHost.efml=&extension;
    check(eflink::worldauthority::hasShutdownBarrier(&extendedHost.base),"EFML host reports its native restoration barrier");
    extension.feature_flags=0;
    check(!eflink::worldauthority::hasShutdownBarrier(&extendedHost.base),"an older host cannot enable native gameplay by size alone");
    extension.feature_flags=eflink::worldauthority::shutdownBarrierFeature;extension.post_to_game_thread=nullptr;
    check(!eflink::worldauthority::hasShutdownBarrier(&extendedHost.base),"shutdown support also requires a working game-thread dispatch entry");
    auto getWorld=reinterpret_cast<EFLink_GetApiV2Fn>(GetProcAddress(library,EFLINK_ENTRY_V2));
    auto* worldApi=getWorld?getWorld():nullptr;
    check(worldApi&&worldApi->version==2&&worldApi->transport==api,"V2 world API preserves the original transport ABI");
    if(worldApi) {
        auto bytes=worldApi->get_world_json(nullptr,0);std::vector<char> copied(bytes);
        auto needed=worldApi->get_world_json(copied.data(),bytes);
        const auto worldJson=json::parse(copied.data());
        check(needed==bytes&&worldJson.contains("session")&&worldJson.at("objects").is_array(),
              "world snapshots use caller-owned buffers and expose only the unified public object ledger");
    }

    BE_ThirdPartyHostV1 host{sizeof(BE_ThirdPartyHostV1),1,nullptr,moduleId.c_str(),"windows-x64","",nullptr,nullptr,log,reply,emit,noRuntime};
    check(module->initialize(&host,R"({"player_name":"测试房主","gui_enabled":false})")==BE_Result_Ok,"module initializes with a configuration");

    auto status=send({{"action","status"}});
    check(status.value("role","")=="offline"&&status.value("name","")=="测试房主","status reports offline and the configured name");

    std::uint32_t channel=0,listener=0;
    check(api->open_channel("test.blocks",onReceive,nullptr,&channel)==EFLink_Ok&&channel,"a module opens a named channel");
    check(api->add_peer_listener(onPeer,nullptr,&listener)==EFLink_Ok,"a module listens for players");
    check(api->send(channel,EFLINK_EVERYONE,"x",1,EFLink_Reliable)==EFLink_NotConnected,"sending offline reports not connected");

    constexpr std::uint16_t port=47725;
    send({{"action","host"},{"room_name","API 测试"},{"port",port},{"max_players",4}});
    check(waitFor([&]{return send({{"action","status"}}).value("role","")=="host";}),"the page opens a room");

    eflink::Net guest;guest.setName("访客");
    eflink::proto::PlayerInfo info;info.modelPath="Assets/chr_test.prefab";info.mapId="map01";guest.setLocalInfo(info);
    std::atomic<int> guestGot=0,echoRequests=0;
    guest.onData=[&](std::uint16_t from,std::uint32_t hash,const unsigned char* data,std::size_t size){
        if(from==1&&hash==eflink::proto::fnv32("test.blocks")) ++guestGot;
        if(from==1&&hash==eflink::proto::fnv32("eflink.example.echo.v1")&&size&&data[0]==0) {
            ++echoRequests;std::vector<unsigned char> response(data,data+size);response[0]=1;
            guest.sendData(hash,from,response.data(),response.size(),true);
        }
    };
    guest.start();
    check(waitFor([&]{for(auto& r:guest.snapshot().rooms) if(r.beacon.roomName=="API 测试"&&r.beacon.hostName=="测试房主") return true;return false;}),"the room is visible on the LAN");
    guest.join("127.0.0.1:"+std::to_string(port),"");
    check(waitFor([&]{return guest.currentRole()==eflink::Role::Client;}),"a second player joins");
    check(waitFor([&]{
        EFLink_PeerInfo peers[4]{};for(auto& p:peers) p.struct_size=sizeof p;
        const auto n=api->list_peers(peers,4);
        return n==2&&std::string(peers[1].name)=="访客"&&std::string(peers[1].model_path)=="Assets/chr_test.prefab";}),"list_peers shows the guest and their operator");
    EFLink_SessionInfo session{};session.struct_size=sizeof session;
    check(api->get_session(&session)==EFLink_Ok&&session.role==EFLink_RoleHost&&session.local_id==1&&session.player_count==2,"get_session reports the hosted room");
    check(peerEvents.load()>=2,"peer listener heard the room open and the guest join");

    const char block[]="stone@1,64,1";
    guest.sendData(eflink::proto::fnv32("test.blocks"),EFLINK_HOST_ID,block,sizeof block-1,true);
    check(waitFor([&]{return received.load()==1&&receivedText==block&&receivedFrom==guest.snapshot().localId;}),"channel data from a guest reaches the module");
    check(api->send(channel,EFLINK_EVERYONE,"ok",2,EFLink_Reliable)==EFLink_Ok,"module sends on its channel");
    check(waitFor([&]{return guestGot.load()==1;}),"the guest receives module data");

    send({{"action","chat"},{"text","你好"}});
    check(waitFor([&]{for(auto& l:guest.chatSince(0)) if(l.text=="你好"&&l.name=="测试房主") return true;return false;}),"chat from the page reaches the guest");
    status=send({{"action","status"}});
    check(status["peers"].size()==2&&!status["addresses"].empty(),"status lists players and this computer's LAN addresses");

    // The shipped SDK consumer links only against the public C ABI.
    if(moduleId=="eflink.core") {
        auto echoDll=LoadLibraryW(L"eflink.example.echo.dll");
        auto echoEntry=echoDll?reinterpret_cast<BE_GetThirdPartyModuleV1Fn>(GetProcAddress(echoDll,BETTER_ENDFIELD_THIRD_PARTY_ENTRY_V1)):nullptr;
        check(echoEntry!=nullptr,"SDK echo example exports the module ABI");
        if(echoEntry) {
            auto* echo=echoEntry();
            check(echo->initialize(&host,"{}")==BE_Result_Ok,"SDK example resolves the public EF Link API");
            echo->on_message("echo-test","{}");
            check(waitFor([&]{return echoRequests.load()>0;}),"SDK example request reaches the remote peer");
            check(waitFor([&]{echo->on_message("echo-status","{}");std::lock_guard lock(replyMutex);return lastReply.value("responses_received",0)>0;}),"SDK example receives a directed response");
            echo->shutdown();
        }
    }

    send({{"action","leave"}});
    check(waitFor([&]{return guest.currentRole()==eflink::Role::Offline;}),"closing the room disconnects the guest");
    guest.stop();
    module->shutdown();
    check(api->send(channel,EFLINK_EVERYONE,"x",1,0)==EFLink_NotFound,"shutdown releases channel registrations");
    check(api->host_room("{}")==EFLink_NotConnected,"room control rejects calls after shutdown");
    check(module->initialize(&host,R"({"player_name":"重启测试","gui_enabled":false})")==BE_Result_Ok,"module can initialize again after shutdown");
    module->shutdown();
    std::printf("%s\n",failures?"FAILED":"all link API checks passed");
    return failures?1:0;
}
