// Runs the production private-data factory against test-owned metadata/native
// objects. These are not game layouts, official accounts or server responses.
#include "authority/native_character_info.h"
#include "support/native_abi_fixture.h"
#include <iostream>
#include <map>
#include <memory>
#include <set>
using namespace eflink::worldauthority;
static void check(bool ok,const char* label) {if(!ok)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';}
struct Adapter:NativeAdapter {
    bool verified=true;std::string gameVersion() const override {return "1.5.3";}std::string path() const override {return "fixture/private-native-data";}
    bool executionVerified() const override {return verified;}bool isolationVerified() const override {return verified;}
    bool recoveryVerified() const override {return verified;}bool dualClientVerified() const override {return verified;}
};
struct Runtime {
    AbiFixtureClass profileClass{false,0,0,nullptr},protoWeaponClass{false,0,0,nullptr},charClass{false,0,0,nullptr},infoClass{false,0,0,&charClass},
        itemClass{false,0,0,nullptr},weaponClass{false,0,0,&itemClass},gemClass{false,0,0,&itemClass},stringClass{false,0,0,nullptr},
        setClass{false,0,0,nullptr},cardClass{false,0,0,nullptr},arrayClass{false,0,0,nullptr},gameClass{false,0,0,nullptr},
        u64{true,8,8,nullptr},i32{true,4,4,nullptr},u32{true,4,4,nullptr},boolean{true,1,1,nullptr},scopeClass{true,4,4,nullptr};
    AbiFixtureClass cardArrayClass{false,0,0,&arrayClass,nullptr,nullptr,&cardClass,1};
    AbiFixtureField mainScope{"Main",{"Beyond.GEnums.ScopeName",&scopeClass},7};
    AbiFixtureObject profile{&profileClass,{}},sourceWeapon{&protoWeaponClass,{}},templateText{&stringClass,{}},otherText{&stringClass,{}},collection{&setClass,{}},
        scalar64{&u64,{}},scalar32{&i32,{}},unsigned32{&u32,{}},boolBox{&boolean,{}};
    struct Class {const AbiFixtureClass* value;std::string assembly,space,name;};
    std::vector<Class> classes;std::vector<std::unique_ptr<AbiFixtureMethod>> methods;
    std::vector<std::unique_ptr<AbiFixtureField>> fields;std::vector<std::unique_ptr<AbiFixtureType>> types;
    std::map<std::string,const AbiFixtureMethod*> signatures;std::vector<std::unique_ptr<AbiFixtureObject>> objects;
    AbiFixtureMethod* ctor=nullptr,*generate=nullptr,*runtimeId=nullptr,*getWeapon=nullptr,*getLevel=nullptr,*getExp=nullptr,*getPotential=nullptr,*getTemplate=nullptr,
        *unbind=nullptr,*detach=nullptr,*count=nullptr,*copy=nullptr,*getValue=nullptr,*refreshCharacter=nullptr,*refreshWeapon=nullptr,*snapshot=nullptr;
    AbiFixtureObject* created=nullptr,*weapon=nullptr,*gem=nullptr;std::vector<void*> skills;
    std::set<unsigned> roots;std::set<void*> subscriptions;unsigned nextRoot=0,invalid=0,constructors=0,detachCalls=0,unbindCalls=0,refreshes=0;
    std::uint64_t nextId=1024;std::uint32_t level=35,exp=320,potential=2;bool hasGem=true,constructorThrows=false,cleanupThrows=false,replaceTemplate=false,wrongSkillOwner=false,ignoreUnbind=false;
    std::function<void()> duringConstructor,duringGenerate;
    Runtime() {
        scopeClass.field=&mainScope;
        auto addClass=[&](AbiFixtureClass& c,const char* name,const char* space="Beyond.Gameplay",const char* assembly="Gameplay.Beyond.dll") {classes.push_back({&c,assembly,space,name});};
        addClass(profileClass,"SROLE_DATA_CHAR","Proto","Beyond.Network.Proto.dll");addClass(protoWeaponClass,"WEAPON_DATA","Proto","Beyond.Network.Proto.dll");
        addClass(charClass,"CharInfo");addClass(infoClass,"ClientCharInfo");addClass(weaponClass,"InventorySystem/WeaponInstData");addClass(gemClass,"InventorySystem/GemInstData");
        addClass(cardClass,"CardSkill","Beyond.Gameplay.Core");addClass(gameClass,"GameUtil");addClass(arrayClass,"Array","System","mscorlib.dll");
        addClass(setClass,"FixtureSet");
        addClass(scopeClass,"ScopeName","Beyond.GEnums","Common.Beyond.dll");
        field(charClass,"instId","System.UInt64",u64,0);field(charClass,"templateId","System.String",stringClass,8);
        field(charClass,"scope","Beyond.GEnums.ScopeName",scopeClass,16);field(charClass,"level","System.Int32",i32,24);
        field(charClass,"exp","System.Int32",i32,32);field(charClass,"potentialLevel","System.UInt32",u32,40);
        field(charClass,"weaponInstId","System.UInt64",u64,48);field(charClass,"friendship","System.Int32",i32,56);
        field(charClass,"cardSkills","FixtureSet",setClass,64);field(charClass,"m_isDeckAttrChangedNotifySquadInFightEventBound","System.Boolean",boolean,72);
        field(infoClass,"m_weaponData","FixtureWeapon",weaponClass,80);field(infoClass,"m_gemInstData","FixtureGem",gemClass,88);
        field(itemClass,"instId","System.UInt64",u64,0);field(weaponClass,"equippedCharServerId","System.UInt64",u64,8);
        field(weaponClass,"attachedGemInstId","System.UInt64",u64,16);field(gemClass,"weaponInstId","System.UInt64",u64,8);
        field(cardClass,"m_charInfo","FixtureCharInfo",charClass,0);
        ctor=method(infoClass,".ctor",{"System.Void",nullptr},{{"Proto.SROLE_DATA_CHAR",&profileClass},{"Beyond.GEnums.ScopeName",&scopeClass}});
        generate=method(gameClass,"GenerateRuntimeClientId",{"System.UInt64",&u64},{},0x10);
        runtimeId=method(gameClass,"IsRuntimeClientId",{"System.Boolean",&boolean},{{"System.UInt64",&u64}},0x10);
        getWeapon=method(profileClass,"get_Weapon",{"Proto.WEAPON_DATA",&protoWeaponClass});getLevel=method(profileClass,"get_Level",{"System.UInt32",&u32});
        getExp=method(profileClass,"get_Exp",{"System.UInt32",&u32});getPotential=method(profileClass,"get_PotentialLevel",{"System.UInt32",&u32});
        getTemplate=method(profileClass,"get_TemplateId",{"System.String",&stringClass});
        unbind=method(charClass,"_UnbindDeckAttrChangedNotifySquadInFightEvent",{"System.Void",nullptr});detach=method(cardClass,"Detach",{"System.Void",nullptr});
        refreshCharacter=method(charClass,"_RefreshCharAttribute",{"System.Void",nullptr});refreshWeapon=method(charClass,"_RefreshWeaponAttribute",{"System.Void",nullptr});snapshot=method(charClass,"_SnapshotDeckAttr",{"System.Void",nullptr});
        count=method(setClass,"get_Count",{"System.Int32",&i32});copy=method(setClass,"CopyTo",{"System.Void",nullptr},{{"Beyond.Gameplay.Core.CardSkill[]",&cardArrayClass}});
        getValue=method(arrayClass,"GetValue",{"System.Object",nullptr},{{"System.Int32",&i32}});
    }
    static std::string key(const char* assembly,const char* space,const char* type,const char* name,const char* parameters,const char* result) {
        return std::string(assembly)+"|"+space+"|"+type+"|"+name+"|"+(parameters?parameters:"")+"|"+result;
    }
    void field(AbiFixtureClass& c,const char* name,const char* type,AbiFixtureClass& kind,unsigned offset) {
        auto value=std::make_unique<AbiFixtureField>(AbiFixtureField{name,{type,&kind},0,0,c.field,&c,offset});c.field=value.get();fields.push_back(std::move(value));
    }
    AbiFixtureMethod* method(AbiFixtureClass& owner,const char* name,AbiFixtureType returns,std::initializer_list<AbiFixtureType> params={},unsigned flags=0) {
        auto value=std::make_unique<AbiFixtureMethod>(AbiFixtureMethod{&owner,nullptr,flags,unsigned(params.size()),name,returns});unsigned index=0;std::string arguments;
        for(const auto& param:params) {types.push_back(std::make_unique<AbiFixtureType>(param));value->parameters[index++]=types.back().get();if(!arguments.empty())arguments+='|';arguments+=param.name;}
        for(const auto& c:classes)if(c.value==&owner)signatures[key(c.assembly.c_str(),c.space.c_str(),c.name.c_str(),name,arguments.c_str(),returns.name)]=value.get();
        value->next=owner.method;owner.method=value.get();auto* result=value.get();methods.push_back(std::move(value));return result;
    }
    const AbiFixtureField* find(const AbiFixtureObject* object,const char* name) const {
        for(auto* c=object->klass;c;c=c->parent)for(auto* f=c->field;f;f=f->next)if(!std::strcmp(f->name,name))return f;throw std::runtime_error("fixture field missing");
    }
    template<class T> void put(AbiFixtureObject* object,const char* name,T value) {std::memcpy(object->data+find(object,name)->offset,&value,sizeof value);}
    template<class T> T read(const AbiFixtureObject* object,const char* name) const {T result{};std::memcpy(&result,object->data+find(object,name)->offset,sizeof result);return result;}
    AbiFixtureObject* make(const AbiFixtureClass* c) {objects.push_back(std::make_unique<AbiFixtureObject>(AbiFixtureObject{c,{}}));return objects.back().get();}
    template<class T> static void* box(AbiFixtureObject& object,T value) {std::memcpy(object.data,&value,sizeof value);return &object;}
    static BE_Result BE_CALL resolveClass(void* context,const char* assembly,const char* space,const char* name,BE_ResolvedClassV1* out) {
        auto& self=*static_cast<Runtime*>(context);*out={};for(const auto& c:self.classes)if(c.assembly==assembly&&c.space==space&&c.name==name) {out->class_info=c.value;return BE_Result_Ok;}return BE_Result_NotFound;
    }
    static BE_Result BE_CALL resolveMethod(void* context,const BE_MethodDescriptorV1* d,BE_ResolvedMethodV1* out) {
        auto& self=*static_cast<Runtime*>(context);*out={};auto found=self.signatures.find(key(d->assembly_name,d->namespace_name,d->class_name,d->method_name,d->parameter_types,d->return_type));
        if(found==self.signatures.end()||found->second->count!=d->parameter_count) {++self.invalid;return BE_Result_NotFound;}
        out->method_info=found->second;return BE_Result_Ok;
    }
    static void* BE_CALL allocate(void* context,const void* c) {auto& self=*static_cast<Runtime*>(context);if(c!=&self.infoClass) {++self.invalid;return nullptr;}return self.make(static_cast<const AbiFixtureClass*>(c));}
    static unsigned BE_CALL root(void* context,void* object,int) {if(!object)return 0;auto& self=*static_cast<Runtime*>(context);auto handle=++self.nextRoot;self.roots.insert(handle);return handle;}
    static void BE_CALL free(void* context,unsigned handle) {auto& self=*static_cast<Runtime*>(context);if(!self.roots.erase(handle))++self.invalid;}
    static void* BE_CALL unbox(void*,void* object) {return static_cast<AbiFixtureObject*>(object)->data;}
    static void* BE_CALL invoke(void* context,const void* method,void* object,void** args,void** error) {
        auto& v=*static_cast<Runtime*>(context);*error=nullptr;
        if(method==v.getLevel)return box(v.unsigned32,v.level);if(method==v.getExp)return box(v.unsigned32,v.exp);
        if(method==v.getPotential)return box(v.unsigned32,v.potential);if(method==v.getTemplate)return &v.templateText;if(method==v.getWeapon)return &v.sourceWeapon;
        if(method==v.generate) {if(v.duringGenerate)v.duringGenerate();return box(v.scalar64,++v.nextId);}
        if(method==v.runtimeId)return box(v.boolBox,*static_cast<std::uint64_t*>(args[0])>1024);
        if(method==v.refreshCharacter||method==v.refreshWeapon||method==v.snapshot) {
            ++v.refreshes;if(object!=v.created||v.read<int>(v.created,"friendship")!=17||v.read<std::uint64_t>(v.weapon,"instId")<=1024)++v.invalid;return nullptr;
        }
        if(method==v.ctor) {
            ++v.constructors;if(args[0]!=&v.profile) {++v.invalid;*error=&v.profile;return nullptr;}
            v.created=static_cast<AbiFixtureObject*>(object);v.put(v.created,"instId",++v.nextId);
            v.put(v.created,"templateId",v.replaceTemplate?&v.otherText:&v.templateText);v.put(v.created,"scope",*static_cast<int*>(args[1]));
            v.put(v.created,"level",int(v.level));v.put(v.created,"exp",int(v.exp));v.put(v.created,"potentialLevel",v.potential);v.put(v.created,"friendship",999);
            v.put(v.created,"weaponInstId",std::uint64_t(11));v.weapon=v.make(&v.weaponClass);v.put(v.weapon,"instId",std::uint64_t(11));
            v.put(v.weapon,"equippedCharServerId",std::uint64_t(999));v.put(v.weapon,"attachedGemInstId",std::uint64_t(22));
            v.put(v.created,"m_weaponData",v.weapon);v.gem=v.hasGem?v.make(&v.gemClass):nullptr;v.put(v.created,"m_gemInstData",v.gem);
            if(v.gem) {v.put(v.gem,"instId",std::uint64_t(22));v.put(v.gem,"weaponInstId",std::uint64_t(11));}
            v.skills.clear();for(int i=0;i<2;++i) {auto* card=v.make(&v.cardClass);v.put(card,"m_charInfo",v.created);v.skills.push_back(card);}
            if(v.wrongSkillOwner)v.put(static_cast<AbiFixtureObject*>(v.skills.back()),"m_charInfo",v.make(&v.charClass));
            v.put(v.created,"cardSkills",&v.collection);v.put(v.created,"m_isDeckAttrChangedNotifySquadInFightEventBound",true);
            v.subscriptions.insert(v.created);
            if(v.duringConstructor)v.duringConstructor();if(v.constructorThrows)*error=&v.profile;return nullptr;
        }
        if(method==v.count)return box(v.scalar32,int(v.skills.size()));
        if(method==v.copy) {auto* array=reinterpret_cast<AbiFixtureArray*>(args[0]);if(array->elements.size()!=v.skills.size()) {++v.invalid;*error=&v.profile;return nullptr;}
            for(std::size_t i=0;i<v.skills.size();++i)array->elements[i]=v.skills[i];return nullptr;}
        if(method==v.getValue) {auto* array=reinterpret_cast<AbiFixtureArray*>(object);const auto i=*static_cast<int*>(args[0]);if(i<0||std::size_t(i)>=array->elements.size()) {*error=&v.profile;return nullptr;}return array->elements[i];}
        if(method==v.detach) {++v.detachCalls;auto it=std::find(v.skills.begin(),v.skills.end(),object);if(it==v.skills.end())++v.invalid;else v.skills.erase(it);if(v.cleanupThrows)*error=&v.profile;return nullptr;}
        if(method==v.unbind) {++v.unbindCalls;v.put(v.created,"m_isDeckAttrChangedNotifySquadInFightEventBound",false);if(!v.ignoreUnbind)v.subscriptions.erase(v.created);if(v.cleanupThrows)*error=&v.profile;return nullptr;}
        ++v.invalid;*error=&v.profile;return nullptr;
    }
    BE_HostApiV1 api() {BE_HostApiV1 host{};host.context=this;host.resolve_class=resolveClass;host.resolve_method=resolveMethod;host.object_new=allocate;
        host.runtime_invoke=invoke;host.object_unbox=unbox;host.gchandle_new=root;host.gchandle_free=free;return host;}
};
struct Fixture {
    Runtime vm;BE_HostApiV1 runtime=vm.api();NativeObjectPins pins;NativeCharacterInfo data;Adapter adapter;
    Scope scope{17,"0123456789abcdef0123456789abcdef",1};bool current=true;
    Fixture() {pins.bind(&vm,[&](std::uintptr_t object){return Runtime::root(&vm,reinterpret_cast<void*>(object),0);},
        [&](std::uint64_t handle){if(handle>UINT32_MAX)throw std::runtime_error("fixture root overflow");Runtime::free(&vm,std::uint32_t(handle));});data.bind(&runtime,"1.5.3",pins);}
    std::optional<NativeCharacterInfo::Lease> construct() {return data.construct(adapter,scope,NativeDomain::HostWorld,2,99,0,&vm.profile,7,17,[&]{return current;});}
    bool cleanup() {return data.cleanup([&](void* info){return !vm.subscriptions.contains(info)&&vm.skills.empty();});}
};
int run(const wchar_t* path) {
    auto dll=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    check(dll&&dll==GetModuleHandleW(L"GameAssembly.dll"),"private character test uses only its explicit synthetic runtime DLL");
    {
        Fixture f;const auto original=f.vm.sourceWeapon;auto lease=f.construct();check(lease.has_value()&&f.vm.constructors==1,"native private character data constructs from the exact public profile class");
        check(f.vm.refreshes==3&&f.vm.read<std::uint64_t>(f.vm.created,"instId")>1024&&f.vm.read<std::uint64_t>(f.vm.weapon,"instId")>1024&&
            f.vm.read<std::uint64_t>(f.vm.gem,"instId")>1024&&f.vm.read<std::uint64_t>(f.vm.weapon,"attachedGemInstId")==f.vm.read<std::uint64_t>(f.vm.gem,"instId")&&
            f.vm.read<std::uint64_t>(f.vm.gem,"weaponInstId")==f.vm.read<std::uint64_t>(f.vm.weapon,"instId")&&f.vm.read<int>(f.vm.created,"friendship")==17&&
            !std::memcmp(original.data,f.vm.sourceWeapon.data,sizeof original.data),"private native IDs replace source item IDs while source data and observed growth remain unchanged");
        check(!f.cleanup()&&!f.vm.detachCalls,"native character data cannot retire while a room actor still holds its lease");
        bool accessed=false;check(f.data.withInfo(*lease,f.scope,NativeDomain::HostWorld,2,99,0,[&](void* info){accessed=info==f.vm.created;return true;})&&accessed,
            "private data access requires its exact native lease and room ownership");
        auto other=f.scope;++other.epoch;check(!f.data.withInfo(*lease,other,NativeDomain::HostWorld,2,99,0,[](void*){return true;})&&
            !f.data.withInfo(*lease,f.scope,NativeDomain::Personal,2,99,0,[](void*){return true;})&&!f.data.withInfo(*lease,f.scope,NativeDomain::HostWorld,3,99,0,[](void*){return true;})&&
            !f.data.withInfo(*lease,f.scope,NativeDomain::HostWorld,2,100,0,[](void*){return true;}),
            "old scene, personal world, another member or a reconnected incarnation cannot access room character data");
        lease.reset();check(f.cleanup()&&f.vm.detachCalls==2&&f.vm.unbindCalls==1&&f.pins.sweep()&&f.pins.safeToUnload()&&f.vm.roots.empty()&&!f.vm.invalid,
            "private card skills detach through native calls and native event subscription is observed clear before roots retire");
        check(f.cleanup()&&f.vm.unbindCalls==1,"private native character cleanup is idempotent");
    }
    {
        Fixture f;f.vm.hasGem=false;auto lease=f.construct();check(lease&&f.vm.read<std::uint64_t>(f.vm.weapon,"attachedGemInstId")==0,"observed unequipped gem does not inherit a source attachment");lease.reset();f.cleanup();
    }
    {
        Fixture f;auto lease=f.construct();lease.reset();f.vm.ignoreUnbind=true;
        check(!f.data.cleanup()&&f.data.retained()&&f.vm.unbindCalls==1,"native cleanup return and cleared bookkeeping flag cannot replace independent event registry evidence");
        check(!f.cleanup()&&f.vm.unbindCalls==1&&f.data.retained(),"pending delegate removal is observed without repeating native detach or unbind");
        f.vm.subscriptions.clear();check(f.cleanup()&&f.vm.unbindCalls==1&&f.pins.sweep()&&f.pins.safeToUnload(),"observed native subscription retirement releases the exact private data lease");
    }
    {
        Fixture f;check(!f.data.construct(f.adapter,f.scope,NativeDomain::HostWorld,2,99,0,&f.vm.profile,99,17,[]{return true;})&&!f.vm.constructors,
            "unknown native account scope cannot enter the Main-scope construction candidate");
    }
    {
        Fixture f;for(const auto& field:f.vm.fields)if(!std::strcmp(field->name,"friendship"))field->flags=0x20;
        check(!f.construct()&&f.data.retained()&&!f.vm.invalid,"readonly native field rejects private initialization before an unsafe write");f.cleanup();
    }
    {
        Fixture f;for(const auto& field:f.vm.fields)if(!std::strcmp(field->name,"friendship"))field->type.klass=&f.vm.u64;
        check(!f.construct()&&f.data.retained()&&!f.vm.invalid,"changed native scalar width cannot overwrite private construction storage");f.cleanup();
    }
    {
        Fixture f;auto lease=f.construct();bool accessed=true;std::thread worker([&]{accessed=f.data.withInfo(*lease,f.scope,NativeDomain::HostWorld,2,99,0,[](void*){return true;});});worker.join();
        check(!accessed,"native private character data cannot be used by a worker thread");lease.reset();f.cleanup();
    }
    {
        Fixture f;auto lease=f.construct();bool rejected=false;try {f.data.bind(&f.runtime,"unknown",f.pins);}catch(...) {rejected=true;}
        check(rejected&&f.data.retained()&&!f.data.withInfo(*lease,f.scope,NativeDomain::HostWorld,2,99,0,[](void*){return true;}),
            "unknown native version closes previously issued private data leases while retaining recovery ownership");
    }
    {
        Fixture f;auto lease=f.construct();lease.reset();f.vm.copy->owner=&f.vm.arrayClass;
        check(!f.cleanup()&&f.data.retained()&&!f.vm.detachCalls,"closed native collection lookup rejects a method declared by an unrelated class");
    }
    for(unsigned mode=0;mode<4;++mode) {
        Fixture f;if(mode==0)f.adapter.verified=false;if(mode==1)f.current=false;if(mode==2)f.vm.level=0;if(mode==3)f.vm.potential=UINT32_MAX;
        check(!f.construct()&&!f.vm.constructors&&!f.data.retained()&&f.vm.roots.empty(),"uncertified, revoked or malformed public growth cannot enter native construction");
    }
    {
        Fixture f;f.vm.replaceTemplate=true;check(!f.construct()&&f.data.retained()&&f.data.status()["phase"]=="uncertain", "constructor normalization cannot silently replace the guest character template");
        check(f.cleanup()&&f.pins.sweep()&&f.pins.safeToUnload(),"unpublished private construction still performs full native cleanup");
    }
    {
        Fixture f;f.vm.constructorThrows=true;check(!f.construct()&&f.data.retained()&&!f.pins.safeToUnload(),"constructor exception retains recovery ownership before any native callbacks can be lost");
        check(f.cleanup()&&f.pins.sweep()&&f.pins.safeToUnload(),"partially constructed native data is cleaned through its private record");
    }
    {
        Fixture f;f.vm.duringConstructor=[&]{f.current=false;};check(!f.construct()&&f.data.retained(),"scene revocation during construction cannot publish a native data lease");f.cleanup();
    }
    {
        Fixture f;f.vm.duringGenerate=[&]{f.adapter.verified=false;};check(!f.construct()&&f.data.retained()&&f.vm.read<std::uint64_t>(f.vm.weapon,"instId")==11,
            "revoked certification during native ID allocation prevents writes to constructed equipment");f.cleanup();
    }
    {
        Fixture f;auto lease=f.construct();lease.reset();f.vm.cleanupThrows=true;
        check(!f.cleanup()&&f.data.retained()&&!f.pins.safeToUnload()&&!f.cleanup()&&f.vm.detachCalls==1,
            "ambiguous native detach cannot be retried or mistaken for clean data from a changed collection");
    }
    {
        Fixture f;f.vm.wrongSkillOwner=true;auto lease=f.construct();lease.reset();
        check(!f.cleanup()&&f.data.retained()&&f.vm.detachCalls<=1,"foreign card skill ownership prevents cleanup from touching another character");
    }
    {
        Fixture f;bool stable=true;for(unsigned i=0;i<20;++i) {auto lease=f.construct();stable=stable&&bool(lease);lease.reset();stable=stable&&f.cleanup()&&f.pins.sweep()&&f.pins.safeToUnload()&&f.vm.roots.empty();}
        check(stable&&f.vm.constructors==20&&f.vm.detachCalls==40&&f.vm.unbindCalls==20,"twenty private construction/cleanup cycles leave no native root or callback owned by the room");
        check(f.data.status()["native_actor_created"]==false&&f.data.status()["full_sync_verified"]==false,"private data construction never certifies a spawned actor or full synchronization");
    }
    FreeLibrary(dll);return 0;
}
int wmain(int argc,wchar_t** argv) {try {if(argc!=2)throw std::runtime_error("explicit fixture DLL required");return run(argv[1]);}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
