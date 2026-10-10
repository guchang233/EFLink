// Loopback test of EF Link networking: one host and two clients in one process.
#include "link_net.h"
#include <cstdio>
#include <cstdlib>

using namespace eflink;
namespace {
int failures=0;
void check(bool condition,const char* what) {
    std::printf("%s %s\n",condition?"[ok]  ":"[FAIL]",what);
    if(!condition) ++failures;
}
template<class F> bool waitFor(F condition,int ms=4000) {
    for(int i=0;i<ms/20;++i) {if(condition()) return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    return condition();
}
proto::PlayerInfo info(const char* model,const char* map) {
    proto::PlayerInfo p;p.modelPath=model;p.mapId=map;p.levelId=std::string(map)+"_lv001";p.skeleton=1;p.bones={"Bip001","Bip001_Pelvis","Bip001_Spine"};return p;
}
proto::PlayerState state(float x,std::uint32_t map) {
    proto::PlayerState s;s.flags=proto::StateValid|proto::StateAlive;s.mapHash=map;s.skeleton=1;s.position[0]=x;s.position[1]=100;s.position[2]=-5;
    s.bones.assign(12,0);s.bones[3]=32767;return s;
}
const PeerView* find(const Snapshot& s,std::uint16_t id) {for(const auto& p:s.peers) if(p.id==id) return &p;return nullptr;}
}

int main() {
    constexpr std::uint16_t port=47715,discovery=47716;
    Net host,alice,bob;
    std::atomic<int> hostData=0,bobData=0,bobUnreliable=0,alicePeerEvents=0;
    std::string bobPayload;
    host.onData=[&](std::uint16_t,std::uint32_t channel,const unsigned char*,std::size_t){if(channel==proto::fnv32("test.reliable")) ++hostData;};
    bob.onData=[&](std::uint16_t from,std::uint32_t channel,const unsigned char* data,std::size_t size){
        if(channel==proto::fnv32("test.reliable")&&from==2) {++bobData;bobPayload.assign(reinterpret_cast<const char*>(data),size);}
        if(channel==proto::fnv32("test.fast")) ++bobUnreliable;
    };
    alice.onPeer=[&](std::uint16_t,bool){++alicePeerEvents;};
    host.setName("Host");alice.setName("Alice");bob.setName("Bob");
    host.setLocalInfo(info("chr_host.prefab","map01"));alice.setLocalInfo(info("chr_alice.prefab","map01"));bob.setLocalInfo(info("chr_bob.prefab","map02"));
    check(host.start(discovery)&&alice.start(discovery)&&bob.start(discovery),"network threads start");

    host.host({"测试房间","secret",3,port});
    check(waitFor([&]{return host.currentRole()==Role::Host;}),"host opens a room");
    check(waitFor([&]{auto s=alice.snapshot();for(auto& r:s.rooms) if(r.beacon.roomName=="测试房间"&&r.beacon.password&&r.beacon.hostName=="Host") return true;return false;}),
          "client discovers the room by broadcast with name, host and lock");

    alice.join("127.0.0.1:"+std::to_string(port),"wrong");
    check(waitFor([&]{return alice.currentRole()==Role::Offline&&alice.snapshot().lastError=="密码错误";}),"wrong password is rejected with a reason");

    alice.join("127.0.0.1:"+std::to_string(port),"secret");
    check(waitFor([&]{return alice.currentRole()==Role::Client;}),"alice joins with the password");
    bob.join("localhost:"+std::to_string(port),"secret");
    check(waitFor([&]{return bob.currentRole()==Role::Client;}),"bob joins by host name");
    check(alice.snapshot().localId!=bob.snapshot().localId&&alice.snapshot().localId>=2,"members get distinct ids");

    Net carol;carol.setName("Carol");carol.start(discovery);
    carol.join("127.0.0.1:"+std::to_string(port),"secret");
    check(waitFor([&]{return carol.currentRole()==Role::Offline&&carol.snapshot().lastError=="房间已满";}),"a full room turns newcomers away");
    carol.stop();

    check(waitFor([&]{auto s=bob.snapshot();auto* a=find(s,alice.snapshot().localId);auto* h=find(s,1);
        return a&&a->hasInfo&&a->info.name=="Alice"&&a->info.modelPath=="chr_alice.prefab"&&a->info.bones.size()==3&&h&&h->info.name=="Host";}),
          "player info reaches every member through the host");

    const auto aliceId=alice.snapshot().localId;
    for(int i=0;i<10;++i) {alice.setLocalState(state(float(i),proto::fnv32("map01")));std::this_thread::sleep_for(std::chrono::milliseconds(30));}
    check(waitFor([&]{auto s=bob.snapshot();auto* a=find(s,aliceId);return a&&!a->states.empty()&&a->states.back().state.position[0]==9.f&&a->states.back().state.bones[3]==32767;}),
          "pose updates are relayed client to client over UDP");
    host.setLocalState(state(42,proto::fnv32("map01")));
    check(waitFor([&]{auto s=alice.snapshot();auto* h=find(s,1);return h&&!h->states.empty()&&h->states.back().state.position[0]==42.f;}),"host pose reaches clients");
    check(waitFor([&]{return alice.snapshot().pongs>0;}),"UDP heartbeat receives a real pong");

    const char text[]="hello blocks";
    alice.sendData(proto::fnv32("test.reliable"),proto::kEveryone,text,sizeof text-1,true);
    check(waitFor([&]{return bobData.load()==1&&hostData.load()==1&&bobPayload==text;}),"reliable channel data reaches host and other members");
    alice.sendData(proto::fnv32("test.reliable"),bob.snapshot().localId,text,sizeof text-1,true);
    check(waitFor([&]{return bobData.load()==2;})&&hostData.load()==1,"directed data reaches only its recipient");
    for(int i=0;i<20&&bobUnreliable.load()==0;++i) {host.sendData(proto::fnv32("test.fast"),proto::kEveryone,"x",1,false);std::this_thread::sleep_for(std::chrono::milliseconds(50));}
    check(bobUnreliable.load()>0,"unreliable channel data arrives");

    alice.chat("大家好");
    check(waitFor([&]{for(auto& l:bob.chatSince(0)) if(l.text=="大家好"&&l.name=="Alice") return true;return false;}),"chat is relayed with the sender name");

    host.kick(bob.snapshot().localId);
    check(waitFor([&]{return bob.currentRole()==Role::Offline&&bob.snapshot().lastError=="你被房主移出了房间";}),"host kick disconnects its target with a reason");
    bob.join("127.0.0.1:"+std::to_string(port),"secret");
    check(waitFor([&]{return bob.currentRole()==Role::Client;}),"a disconnected player can rejoin");

    alice.leave();
    check(waitFor([&]{return find(bob.snapshot(),aliceId)==nullptr;}),"a departure reaches the remaining members");
    check(alicePeerEvents.load()>0,"peer callbacks fire");

    host.leave();
    check(waitFor([&]{return bob.currentRole()==Role::Offline;}),"closing the room disconnects members");
    check(bob.snapshot().lastError=="房主关闭了房间","members learn the host closed the room");

    host.stop();alice.stop();bob.stop();
    std::printf("%s\n",failures?"FAILED":"all link network checks passed");
    return failures?1:0;
}
