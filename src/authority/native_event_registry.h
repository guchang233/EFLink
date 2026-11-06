#pragma once
// Read-only native event-reference census. It is not a gameplay/isolation
// certificate and never removes another system's handler or processes events.
#include "native_members.h"
#include "world_authority.h"

namespace eflink::worldauthority {
class NativeEventRegistry {
public:
    enum class Result {Unknown,Retained,Clear};
private:
    eflink::unity::Api bridge;const BE_HostApiV1* runtime=nullptr;const void* managerClass=nullptr;
    BE_ResolvedMethodV1 invocationList{},delegateTarget{},arrayLength{},arrayValue{};
    bool bound=false,busy=false;std::thread::id owner;
    std::uint64_t scans=0,retained=0,unknown=0,clear=0;std::string error;
    unsigned bindings=0,handlers=0,delegates=0;
    template<class T> static T symbol(const char* name) {
        auto fn=reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),name));
        if(!fn)throw std::runtime_error("native event registry metadata unavailable");return fn;
    }
    void thread() {
        if(owner==std::thread::id{})owner=std::this_thread::get_id();
        if(owner!=std::this_thread::get_id())throw std::runtime_error("native event census outside game thread");
    }
    bool references(void* handler,void* target) {
        if(!handler)return false;
        if(++handlers>8192)throw std::runtime_error("native event handlers outside census bound");
        eflink::unity::Api::TemporaryRoot handlerRoot(runtime,handler);
        // Multicast Target alone describes only its last invocation. Query the
        // real invocation list and inspect every delegate target instead.
        auto* array=bridge.call(bridge.virtualMethod(invocationList,handler),handler);
        if(!array)throw std::runtime_error("native delegate invocation list unavailable");
        eflink::unity::Api::TemporaryRoot arrayRoot(runtime,array);int dimension=0;
        const auto n=bridge.value<int>(arrayLength,array,{&dimension});
        if(n<1||n>1024||delegates+unsigned(n)>16384)throw std::runtime_error("native multicast delegate outside census bound");
        bool found=false;
        for(int i=0;i<n;++i) {
            ++delegates;auto* item=bridge.call(arrayValue,array,{&i});if(!item)throw std::runtime_error("null native invocation entry");
            eflink::unity::Api::TemporaryRoot itemRoot(runtime,item);
            if(bridge.call(delegateTarget,item)==target)found=true;
        }
        return found;
    }
    bool list(void* collection,const char* delegateField,void* target,NativeObjectPins& pins) {
        NativeMembers members(bridge);
        if(!collection)throw std::runtime_error("native event list unavailable");
        const auto version=members.read<int>(collection,"_version","System.Int32");
        const auto snapshot=members.objects(collection,pins,4096,true);bool found=false;
        for(const auto& value:snapshot) {
            auto* handler=members.reference(value.object,delegateField);
            if(references(handler,target))found=true;
            if(members.reference(value.object,delegateField)!=handler)throw std::runtime_error("native event handler changed during census");
        }
        if(members.read<int>(collection,"_version","System.Int32")!=version)throw std::runtime_error("native event list changed during census");
        return found;
    }
    bool dispatcher(void* value,void* target,NativeObjectPins& pins) {
        if(!value)throw std::runtime_error("native event dispatcher unavailable");
        NativeMembers members(bridge);auto* dictionary=members.reference(value,"m_evtBindings");
        if(!dictionary)throw std::runtime_error("native event binding dictionary unavailable");
        eflink::unity::Api::TemporaryRoot dictionaryRoot(runtime,dictionary);
        const auto version=members.read<int>(dictionary,"_version","System.Int32");
        auto* values=members.referenceGetter(dictionary,"get_Values");if(!values)throw std::runtime_error("native event binding values unavailable");
        eflink::unity::Api::TemporaryRoot valuesRoot(runtime,values);
        const auto snapshot=members.objects(values,pins,4096);bool found=false;
        for(const auto& binding:snapshot) {
            if(++bindings>4096)throw std::runtime_error("native event bindings outside census bound");
            if(members.read<int>(binding.object,"m_iterCounter","System.Int32")!=0)throw std::runtime_error("native event dispatch is still active");
            auto* active=members.reference(binding.object,"m_handlers");auto* pending=members.reference(binding.object,"m_pendingOperator");
            if(list(active,"handler",target,pins)||list(pending,"m_delegate",target,pins))found=true;
            if(members.read<int>(binding.object,"m_iterCounter","System.Int32")!=0||members.reference(binding.object,"m_handlers")!=active||
               members.reference(binding.object,"m_pendingOperator")!=pending)throw std::runtime_error("native event binding changed during census");
        }
        if(members.reference(value,"m_evtBindings")!=dictionary||members.read<int>(dictionary,"_version","System.Int32")!=version)
            throw std::runtime_error("native event dictionary changed during census");
        return found;
    }
public:
    bool boundToRuntime() const {return bound;}
    void bind(const BE_HostApiV1* host,const std::string& version) {
        thread();if(busy)throw std::runtime_error("native event census bind during callback");
        if(version!="1.5.3") {bound=false;throw std::runtime_error("native event registry version unverified");}
        if(bound&&runtime==host)return;bound=false;
        if(!host||!host->resolve_class||!host->resolve_method||!host->runtime_invoke||!host->object_unbox||!host->gchandle_new||!host->gchandle_free)
            throw std::runtime_error("native event registry runtime unavailable");
        bridge=eflink::unity::Api{};bridge.bind(host);
        managerClass=bridge.klass("EventManager","Common.Beyond.dll","Beyond").class_info;
        invocationList=bridge.method("Delegate","GetInvocationList","","System.Delegate[]","mscorlib.dll","System");
        delegateTarget=bridge.method("Delegate","get_Target","","System.Object","mscorlib.dll","System");
        arrayLength=bridge.method("Array","GetLength","System.Int32","System.Int32","mscorlib.dll","System");
        arrayValue=bridge.method("Array","GetValue","System.Int32","System.Object","mscorlib.dll","System");
        runtime=host;bound=true;
    }
    void* existingManager() {
        thread();if(!bound)throw std::runtime_error("native event registry unbound");return NativeMembers(bridge).existingSingleton(managerClass);
    }
    Result observe(void* manager,void* target,NativeObjectPins& pins) {
        thread();if(!bound||busy||!manager||!target||!pins.usable())return Result::Unknown;
        struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(busy);
        ++scans;bindings=handlers=delegates=0;error.clear();
        try {
            if(!symbol<bool(*)(const void*,const void*)>("il2cpp_class_is_assignable_from")(managerClass,symbol<const void*(*)(void*)>("il2cpp_object_get_class")(manager)))
                throw std::runtime_error("native event manager instance differs");
            eflink::unity::Api::TemporaryRoot managerRoot(runtime,manager),targetRoot(runtime,target);
            NativeMembers members(bridge);auto* game=members.reference(manager,"m_gameEvents");auto* global=members.reference(manager,"m_eventDispatcher");
            if(!game||!global)throw std::runtime_error("native event manager incomplete");
            // Scan both, including pending removals and released-but-retained
            // handlers. Bookkeeping flags cannot remove their actual references.
            const bool inGame=dispatcher(game,target,pins),inGlobal=dispatcher(global,target,pins);
            if(members.reference(manager,"m_gameEvents")!=game||members.reference(manager,"m_eventDispatcher")!=global)
                throw std::runtime_error("native event manager changed during census");
            if(inGame||inGlobal) {++retained;return Result::Retained;}
            ++clear;return Result::Clear;
        }catch(const std::exception& e) {error=e.what();}catch(...) {error="native event census fault";}
        ++unknown;return Result::Unknown;
    }
    // One-shot production observation for private-data cleanup. Only an
    // explicit Clear from the real native event manager counts as evidence;
    // every uncertainty returns false so the caller keeps its retained record
    // and the unload barrier stays closed.
    bool observeClear(void* target,NativeObjectPins& pins) {
        try {
            thread();
            if(!bound||busy||!target||!pins.usable())return false;
            auto* manager=existingManager();
            return manager&&observe(manager,target,pins)==Result::Clear;
        }catch(...) {return false;}
    }
    Json status() const {
        return {{"bound",bound},{"scans",scans},{"retained",retained},{"unknown",unknown},{"clear",clear},{"last_bindings",bindings},{"last_handlers",handlers},{"last_delegate_entries",delegates},
            {"error",error},{"read_only",true},{"scope","direct_delegate_targets_in_native_event_manager"},{"isolation_verified",false},{"full_sync_verified",false}};
    }
};
}
