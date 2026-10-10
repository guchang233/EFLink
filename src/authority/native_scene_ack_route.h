#pragma once
// The native send hook uses this route only after server change/enter handlers
// have actually been dispatched. Old local LoadAtPos entry never arms it.
#include <cstdint>
#include <stdexcept>

namespace eflink::authority {
class NativeSceneAckRoute {
public:
    struct Token {
        std::uint64_t world=0,transition=0,scene=0,privateGeneration=0;
        std::uint32_t room=0;std::uint16_t member=0;std::int32_t sceneNum=0;
        bool operator==(const Token&) const=default;
        bool valid() const {return world&&transition&&scene&&privateGeneration&&room&&member&&sceneNum>0;}
    };
    enum class Phase {idle,awaitingChange,awaitingEnter,awaitingLoad,awaitingBootstrap,awaitingNativeApply,active,needsPrivateRestore,returningPrivate};
    enum class Decision {official,native,duplicate,blocked};
private:
    Token token{};Phase state=Phase::idle;
    std::uint64_t deadline=0,restoredGeneration=0,returnGeneration=0;
    std::int32_t privateSceneNum=0;bool acknowledgment=false,privateAcknowledgment=false;
    void same(const Token& value) const {if(value!=token)throw std::runtime_error("native scene transition token mismatch");}
public:
    Phase phase() const {return state;}
    const Token& current() const {return token;}
    bool ownsLoading() const {return state!=Phase::idle;}
    void tick(std::uint64_t now) {if(state!=Phase::idle&&state!=Phase::active&&now>=deadline)state=Phase::needsPrivateRestore;}
    void begin(const Token& value,std::uint64_t now) {
        if(!value.valid()||(state!=Phase::idle&&state!=Phase::active)||now>UINT64_MAX-60000||
            (state==Phase::idle&&value.privateGeneration<=restoredGeneration))
            throw std::runtime_error("native scene cannot begin");
        if(state==Phase::active&&(value.world!=token.world||value.room!=token.room||value.member!=token.member||value.transition<=token.transition||value.privateGeneration!=token.privateGeneration))
            throw std::runtime_error("native follow transition is stale or belongs to another world");
        token=value;state=Phase::awaitingChange;deadline=now+60000;acknowledgment=false;
    }
    void changeDispatched(const Token& value) {
        same(value);if(state!=Phase::awaitingChange)throw std::runtime_error("native change out of order");state=Phase::awaitingEnter;
    }
    void enterDispatched(const Token& value) {
        same(value);if(state!=Phase::awaitingEnter)throw std::runtime_error("native enter out of order");state=Phase::awaitingLoad;
    }
    // Only CS_SCENE_LOAD_FINISH reaches this method. Keep the game's entire
    // OnSceneLoaded callback running; its SendMessage return is replaced at the
    // transport boundary, before calling the original HGNetSession sender.
    Decision loadFinish(std::int32_t sceneNum,std::uint64_t now) {
        if(state==Phase::idle)return Decision::official;
        if(state==Phase::needsPrivateRestore)return Decision::blocked;
        if(state!=Phase::active&&now>=deadline) {state=Phase::needsPrivateRestore;return Decision::blocked;}
        if(state==Phase::returningPrivate) {
            if(sceneNum!=privateSceneNum) {state=Phase::needsPrivateRestore;return Decision::blocked;}
            privateAcknowledgment=true;return Decision::official;
        }
        if(sceneNum!=token.sceneNum) {state=Phase::needsPrivateRestore;return Decision::blocked;}
        if(acknowledgment)return Decision::duplicate;
        if(state!=Phase::awaitingLoad) {state=Phase::needsPrivateRestore;return Decision::blocked;}
        acknowledgment=true;state=Phase::awaitingBootstrap;return Decision::native;
    }
    void bootstrapDispatched(const Token& value,std::uint64_t now) {
        tick(now);
        same(value);if(state!=Phase::awaitingBootstrap)throw std::runtime_error("native bootstrap out of order");state=Phase::awaitingNativeApply;
    }
    // This is a trusted adapter assertion, not a peer/UI acceptance flag.
    void nativeApplied(const Token& value,std::uint64_t now) {
        tick(now);
        same(value);if(state!=Phase::awaitingNativeApply)throw std::runtime_error("native apply out of order");state=Phase::active;
    }
    void fail() {if(state!=Phase::idle)state=Phase::needsPrivateRestore;}
    void disconnected() {fail();}
    // The preserved official scene identity must come from the private-world
    // adapter. Allow only its acknowledgment while the original account's
    // native return pipeline runs; native-room acknowledgments remain blocked.
    void beginPrivateReturn(std::int32_t sceneNum,std::uint64_t generation,std::uint64_t now) {
        if(state!=Phase::needsPrivateRestore||sceneNum<=0||generation<=token.privateGeneration||
            generation<=returnGeneration||now>UINT64_MAX-60000)throw std::runtime_error("private return cannot begin");
        privateSceneNum=sceneNum;returnGeneration=generation;privateAcknowledgment=false;
        deadline=now+60000;state=Phase::returningPrivate;
    }
    // Reset only after the separate private-world adapter has completed return
    // loading, original gameplay replay and native state verification.
    void privateRestored(std::uint64_t generation) {
        if(state!=Phase::returningPrivate||!privateAcknowledgment||generation!=returnGeneration)
            throw std::runtime_error("private world restoration is unproven");
        restoredGeneration=generation;
        token={};state=Phase::idle;deadline=0;acknowledgment=false;
    }
};
}
