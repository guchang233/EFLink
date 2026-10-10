// Actual C++ -> Zig ABI and load-ack route tests. No game or account is touched.
#include "authority/native_authority_client.h"
#include "authority/native_scene_ack_route.h"
#include "authority/link_native_feed.h"
#include <iostream>

using namespace eflink::authority;
std::span<const unsigned char> bytes(const std::string& value) {
    return {reinterpret_cast<const unsigned char*>(value.data()),value.size()};
}
void require(bool value,const char* description) {
    if(!value)throw std::runtime_error(description);std::cout<<"PASS "<<description<<'\n';
}
template<class Fn> bool rejected(Fn&& action) {try {action();return false;}catch(const std::runtime_error&) {return true;}}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    try {
        NativeAuthorityClient kernel(std::filesystem::absolute(argv[1]),9001,7001,103);
        kernel.addMember(1);kernel.addMember(2);
        ProtoWriter common;common.u64(1,1001);common.str(3,"synthetic_host");common.i32(7,103);common.f64(8,300);
        ProtoWriter battle;battle.u64(3,7);battle.u64(4,90001);
        ProtoWriter character;character.msg(1,common);character.msg(5,battle);
        ProtoWriter detail;detail.msg(1,character);
        ProtoWriter team;team.u64(4,1001);
        ProtoWriter scene;scene.i32(1,103);scene.u64(2,7001);scene.msg(3,detail);scene.msg(12,{});scene.msg(16,team);
        kernel.importHostScene(1,bytes(scene.bytes()));
        const auto leader=kernel.allocateApprovedOperator(2,bytes(character.bytes()));
        const auto transition=kernel.beginScene(2,22,9);
        NativeSceneAckRoute route;
        using Decision=NativeSceneAckRoute::Decision;
        using Phase=NativeSceneAckRoute::Phase;
        require(route.loadFinish(103,100)==Decision::official,"old official loading remains untouched before native entry");
        const NativeSceneAckRoute::Token token{9001,transition,7001,9,12345,2,103};
        route.begin(token,100);
        auto packet=kernel.poll();
        require(packet&&packet->head.message_id==838&&packet->head.recipient==2&&!packet->body.empty(),"C++ consumes actual Zig native change body");
        route.changeDispatched(token);
        packet=kernel.poll();
        require(packet&&packet->head.message_id==24&&packet->head.transition_id==transition,"native enter shares the exact transition token");
        route.enterDispatched(token);
        require(route.loadFinish(103,110)==Decision::native,"armed native load acknowledgment routes to the shared session");
        require(route.loadFinish(103,111)==Decision::duplicate,"duplicate native acknowledgment is not sent to the official world");
        ProtoWriter loaded;loaded.i32(1,103);
        kernel.sceneLoaded(2,9001,transition,bytes(loaded.bytes()));
        packet=kernel.poll();
        require(packet&&packet->head.message_id==25&&packet->head.world_id==9001,"shared core produces a standard native bootstrap");
        route.bootstrapDispatched(token,120);
        require(route.phase()==Phase::awaitingNativeApply,"loading acknowledgment alone does not activate the route");
        kernel.nativeApplied(2,{transition,9001,7001,leader,9});
        route.nativeApplied(token,130);
        require(route.phase()==Phase::active,"only the trusted adapter proof completes the core route");
        auto stale=token;
        require(rejected([&]{route.begin(stale,140);}),"current transition cannot be replayed as a host map change");
        route.disconnected();
        require(route.loadFinish(103,150)==Decision::blocked,"disconnect retains account protection until private return is proven");
        require(rejected([&]{route.privateRestored(9);}),"old private-feed generation cannot release native routing");
        route.beginPrivateReturn(21,10,151);
        require(rejected([&]{route.privateRestored(10);}),"private return cannot complete before its native loading callback");
        require(route.loadFinish(21,152)==Decision::official,"only the preserved private scene can acknowledge the original account");
        route.privateRestored(10);
        require(route.loadFinish(103,160)==Decision::official,"completed private return restores the original sender route");
        kernel.removeMember(2);
        require(!kernel.poll(),"disconnected guest has no queued native bodies");
        kernel.addMember(2);kernel.allocateApprovedOperator(2,bytes(character.bytes()));
        const auto next=kernel.beginScene(2,22,11);
        require(next>transition,"reused member ID receives a fresh room transition");
        require(rejected([&]{kernel.sceneLoaded(2,9001,transition,bytes(loaded.bytes()));}),"old connection cannot finish the new native load");
        auto wrong=token;wrong.transition=next;wrong.privateGeneration=11;route.begin(wrong,200);
        route.changeDispatched(wrong);route.enterDispatched(wrong);
        require(route.loadFinish(104,201)==Decision::blocked&&route.phase()==Phase::needsPrivateRestore,"wrong-scene acknowledgment fails without escaping to official traffic");
        route.beginPrivateReturn(21,12,202);route.loadFinish(21,203);route.privateRestored(12);
        wrong.privateGeneration=13;route.begin(wrong,300);route.changeDispatched(wrong);route.enterDispatched(wrong);
        route.tick(60300);
        require(route.phase()==Phase::needsPrivateRestore&&route.loadFinish(103,60301)==Decision::blocked,"timed-out native loading cannot resume official traffic automatically");
        std::cout<<"C++/Zig route checks passed; no real-client acceptance claimed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
