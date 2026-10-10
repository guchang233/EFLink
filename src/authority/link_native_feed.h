#pragma once
// Native world feed (game thread only). Builds standard server messages and hands
// them to the client's own GameplayNetwork handlers, the same code the network
// layer dispatches to. The game's own pipeline then creates, spawns and removes
// entities; nothing is hidden, cloned or written into objects directly, and
// nothing is sent to any server.
#include "../unity_bridge.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace eflink::authority {
// Minimal protobuf writer for the handful of server messages used here.
class ProtoWriter {
    std::string out;
public:
    void varint(std::uint64_t v) {while(v>=0x80){out.push_back(char((v&0x7f)|0x80));v>>=7;}out.push_back(char(v));}
    void key(int field,int wire) {varint(std::uint64_t(field)<<3|unsigned(wire));}
    void u64(int field,std::uint64_t v) {key(field,0);varint(v);}
    void i32(int field,std::int32_t v) {key(field,0);varint(std::uint64_t(std::int64_t(v)));}
    void f32(int field,float v) {key(field,5);char b[4];std::memcpy(b,&v,4);out.append(b,4);}
    void f64(int field,double v) {key(field,1);char b[8];std::memcpy(b,&v,8);out.append(b,8);}
    void str(int field,const std::string& v) {key(field,2);varint(v.size());out+=v;}
    void msg(int field,const ProtoWriter& v) {key(field,2);varint(v.out.size());out+=v.out;}
    const std::string& bytes() const {return out;}
};
struct FeedMonster {
    std::uint64_t id=0;std::string templateId;
    float position[3]{},rotation[3]{}; // rotation: Euler degrees, as the server sends
    std::int32_t scene=0;double hp=0;std::int32_t level=1;
    // 0 for host monsters shown to a guest (not battle entities of the guest's
    // account); the original value when restoring the guest's own monsters.
    std::uint64_t battleInstance=0;std::uint32_t generation=1;
};
inline ProtoWriter vector3(const float v[3]) {ProtoWriter w;w.f32(1,v[0]);w.f32(2,v[1]);w.f32(3,v[2]);return w;}
// SC_OBJECT_ENTER_VIEW{detail{monster_list[...]}} in the server's own format.
inline std::string enterViewMonsters(const std::vector<FeedMonster>& monsters) {
    ProtoWriter container;
    for(const auto& m:monsters) {
        ProtoWriter common;
        common.u64(1,m.id);common.i32(2,3);common.str(3,m.templateId);
        common.msg(4,vector3(m.position));common.msg(5,vector3(m.rotation));
        common.i32(7,m.scene);common.f64(8,m.hp);
        // No battle_inst_id: a host monster shown to a guest must not become a
        // battle entity of the guest's own account (its client would report
        // that monster's battle operations to the guest's official server).
        ProtoWriter battle;battle.u64(3,m.generation);if(m.battleInstance)battle.u64(4,m.battleInstance);battle.msg(6,ProtoWriter{});
        ProtoWriter monster;monster.msg(1,common);monster.i32(3,m.level);monster.msg(5,battle);
        container.msg(2,monster);
    }
    ProtoWriter message;message.msg(1,container);return message.bytes();
}
// SC_OBJECT_ENTER_VIEW built from SCENE_MONSTER entries exactly as the official
// server sent them (restoring the player's own world after a room).
inline std::string enterViewRawMonsters(const std::vector<std::string>& monsters) {
    std::string container;
    for(const auto& raw:monsters) {
        ProtoWriter entry;entry.key(2,2);entry.varint(raw.size());container+=entry.bytes()+raw;
    }
    ProtoWriter message;message.key(1,2);message.varint(container.size());
    return message.bytes()+container;
}
// SC_OBJECT_LEAVE_VIEW{obj_list[{obj_type=3 (monster), obj_id}]}
inline std::string leaveViewMonsters(const std::vector<std::uint64_t>& ids) {
    ProtoWriter message;
    for(auto id:ids) {ProtoWriter item;item.i32(1,3);item.u64(2,id);message.msg(1,item);}
    return message.bytes();
}
// Unity quaternion -> Euler degrees (Z, X, Y order used by Transform.eulerAngles).
inline void eulerDegrees(const float q[4],float out[3]) {
    const double x=q[0],y=q[1],z=q[2],w=q[3];
    const double sinp=2*(w*x-y*z);
    const double pitch=std::abs(sinp)>=1?std::copysign(3.14159265358979/2,sinp):std::asin(sinp);
    const double yaw=std::atan2(2*(w*y+x*z),1-2*(x*x+y*y));
    const double roll=std::atan2(2*(w*z+x*y),1-2*(x*x+z*z));
    auto deg=[](double r){double d=r*180.0/3.14159265358979;return float(d<0?d+360:d);};
    out[0]=deg(pitch);out[1]=deg(yaw);out[2]=deg(roll);
}

// A SCENE_MONSTER exactly as the host's official server sent it, adapted for
// a guest: current host position and HP, and no battle_inst_id (the monster is
// never a battle entity of the guest's account). Every other field (script
// ownership, attributes, skills, buffs, spawner library) stays verbatim.
struct RawField {int field=0,wire=0;std::string_view whole,payload;};
inline bool rawFields(std::string_view b,std::vector<RawField>& out) {
    std::size_t at=0;
    auto varint=[&](std::uint64_t& v){v=0;for(int shift=0;shift<64;shift+=7){if(at>=b.size())return false;const auto c=static_cast<unsigned char>(b[at++]);v|=std::uint64_t(c&0x7f)<<shift;if(!(c&0x80))return true;}return false;};
    while(at<b.size()) {
        const auto start=at;std::uint64_t key=0;if(!varint(key))return false;
        RawField f;f.field=int(key>>3);f.wire=int(key&7);const auto body=at;std::uint64_t v=0;
        switch(f.wire) {
            case 0:if(!varint(v))return false;break;
            case 1:if(b.size()-at<8)return false;at+=8;break;
            case 5:if(b.size()-at<4)return false;at+=4;break;
            case 2:{if(!varint(v)||v>b.size()-at)return false;f.payload=b.substr(at,std::size_t(v));at+=std::size_t(v);break;}
            default:return false;
        }
        (void)body;f.whole=b.substr(start,at-start);out.push_back(f);
    }
    return true;
}
inline std::string officialMonsterForGuest(const std::string& raw,const float position[3],double hp) {
    std::vector<RawField> top;if(!rawFields(raw,top))throw std::runtime_error("official monster malformed");
    auto wrap=[](int field,const std::string& body){ProtoWriter w;w.key(field,2);w.varint(body.size());return w.bytes()+body;};
    std::string out;bool common=false;
    for(const auto& f:top) {
        if(f.field==1&&f.wire==2) {
            std::vector<RawField> c;if(!rawFields(f.payload,c))throw std::runtime_error("official common info malformed");
            std::string body;for(const auto& g:c)if(g.field!=4&&g.field!=8)body.append(g.whole);
            ProtoWriter now;now.msg(4,vector3(position));now.f64(8,hp);body+=now.bytes();
            out+=wrap(1,body);common=true;
        } else if(f.field==5&&f.wire==2) {
            std::vector<RawField> c;if(!rawFields(f.payload,c))throw std::runtime_error("official battle info malformed");
            std::string body;for(const auto& g:c)if(g.field!=4)body.append(g.whole);
            out+=wrap(5,body);
        } else out.append(f.whole);
    }
    if(!common)throw std::runtime_error("official monster has no common info");
    return out;
}
class NativeFeed {
    unity::Api api;const BE_HostApiV1* runtime=nullptr;
    template<class T> static T symbol(const char* name) {
        auto p=reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),name));
        if(!p)throw std::runtime_error(std::string("missing metadata export: ")+name);
        return p;
    }
    static std::string base64(const std::string& bytes) {
        static const char a[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string t;t.reserve((bytes.size()+2)/3*4);
        for(std::size_t i=0;i<bytes.size();i+=3) {
            unsigned v=unsigned(static_cast<unsigned char>(bytes[i]))<<16;
            if(i+1<bytes.size())v|=unsigned(static_cast<unsigned char>(bytes[i+1]))<<8;
            if(i+2<bytes.size())v|=static_cast<unsigned char>(bytes[i+2]);
            t+=a[v>>18];t+=a[(v>>12)&63];t+=i+1<bytes.size()?a[(v>>6)&63]:'=';t+=i+2<bytes.size()?a[v&63]:'=';
        }
        return t;
    }
    void* parse(const char* type,const std::string& bytes) {
        auto* string=api.string(base64(bytes));unity::Api::TemporaryRoot stringRoot(runtime,string);
        auto* data=api.call(api.method("ByteString","FromBase64","System.String","Google.Protobuf.ByteString","Google.Protobuf.dll","Google.Protobuf"),nullptr,{string});
        unity::Api::TemporaryRoot dataRoot(runtime,data);
        auto klass=api.klass(type,"Beyond.Network.Proto.dll","Proto");
        auto* getParser=symbol<const void*(*)(const void*,const char*,int)>("il2cpp_class_get_method_from_name")(klass.class_info,"get_Parser",0);
        if(!getParser)throw std::runtime_error("protobuf parser unavailable");
        BE_ResolvedMethodV1 parserMethod{};parserMethod.method_info=getParser;
        auto* parser=api.call(parserMethod);unity::Api::TemporaryRoot parserRoot(runtime,parser);
        auto methods=symbol<const void*(*)(const void*,void**)>("il2cpp_class_get_methods");
        auto name=symbol<const char*(*)(const void*)>("il2cpp_method_get_name");
        auto count=symbol<unsigned(*)(const void*)>("il2cpp_method_get_param_count");
        auto param=symbol<const void*(*)(const void*,unsigned)>("il2cpp_method_get_param");
        auto typeName=symbol<char*(*)(const void*)>("il2cpp_type_get_name");
        auto release=symbol<void(*)(void*)>("il2cpp_free");
        auto parent=symbol<const void*(*)(const void*)>("il2cpp_class_get_parent");
        const void* parseFrom=nullptr;
        for(auto* k=symbol<const void*(*)(void*)>("il2cpp_object_get_class")(parser);k&&!parseFrom;k=parent(k)) {
            void* iterator=nullptr;
            while(auto* method=methods(k,&iterator)) {
                if(std::strcmp(name(method),"ParseFrom")||count(method)!=1)continue;
                char* t=typeName(param(method,0));const bool match=t&&std::strcmp(t,"Google.Protobuf.ByteString")==0;if(t)release(t);
                if(match){parseFrom=method;break;}
            }
        }
        if(!parseFrom)throw std::runtime_error("ParseFrom(ByteString) unavailable");
        BE_ResolvedMethodV1 parseMethod{};parseMethod.method_info=parseFrom;
        auto* message=api.call(parseMethod,parser,{data});
        if(!message)throw std::runtime_error(std::string("native parser rejected ")+type);
        return message;
    }
    void handle(const char* type,const char* handler,const std::string& bytes) {
        auto* message=parse(type,bytes);unity::Api::TemporaryRoot messageRoot(runtime,message);
        auto* instance=api.call(api.method("GameInstance","get_instance","","Beyond.Gameplay.GameInstance","Gameplay.Beyond.dll","Beyond.Gameplay"));
        auto* network=instance?api.call(api.method("GameInstance","get_gameplayNetwork","","Beyond.Gameplay.GameplayNetwork","Gameplay.Beyond.dll","Beyond.Gameplay"),instance):nullptr;
        if(!network)throw std::runtime_error("GameplayNetwork unavailable");
        const auto parameter=std::string("Proto.")+type;
        api.call(api.method("GameplayNetwork",handler,parameter.c_str(),"System.Void","Gameplay.Beyond.dll","Beyond.Gameplay"),network,{message});
    }
public:
    static std::string encode64(const std::string& bytes) {return base64(bytes);}
    std::uint64_t entered=0,left=0;
    static std::string decode64(const std::string& text) {
        std::string out;unsigned bits=0;int count=0;
        for(char c:text) {
            int v;
            if(c>='A'&&c<='Z')v=c-'A';else if(c>='a'&&c<='z')v=c-'a'+26;else if(c>='0'&&c<='9')v=c-'0'+52;
            else if(c=='+')v=62;else if(c=='/')v=63;else if(c=='=')break;else throw std::runtime_error("base64 invalid");
            bits=(bits<<6)|unsigned(v);count+=6;
            if(count>=8){count-=8;out.push_back(char((bits>>count)&0xff));}
        }
        return out;
    }
    void bind(const BE_HostApiV1* host) {runtime=host;api.bind(host);}
    // The receive filter preserves the original NetResponse envelope while
    // replacing just a mixed view body through the game's protobuf parser.
    void* parseView(const std::string& type,const std::string& bytes) {
        if(type!="SC_OBJECT_ENTER_VIEW"&&type!="SC_OBJECT_LEAVE_VIEW")
            throw std::runtime_error("view parser type not permitted");
        return parse(type.c_str(),bytes);
    }
    void enter(const std::vector<FeedMonster>& monsters) {
        for(std::size_t i=0;i<monsters.size();i+=64) {
            std::vector<FeedMonster> part(monsters.begin()+std::ptrdiff_t(i),monsters.begin()+std::ptrdiff_t((std::min)(monsters.size(),i+64)));
            handle("SC_OBJECT_ENTER_VIEW","_Handle_ObjectEnterView",enterViewMonsters(part));entered+=part.size();
        }
    }
    void leave(const std::vector<std::uint64_t>& ids) {
        for(std::size_t i=0;i<ids.size();i+=128) {
            std::vector<std::uint64_t> part(ids.begin()+std::ptrdiff_t(i),ids.begin()+std::ptrdiff_t((std::min)(ids.size(),i+128)));
            handle("SC_OBJECT_LEAVE_VIEW","_Handle_ObjectLeaveView",leaveViewMonsters(part));left+=part.size();
        }
    }
    void enterRaw(const std::vector<std::string>& monsters) {
        for(std::size_t i=0;i<monsters.size();i+=64) {
            std::vector<std::string> part(monsters.begin()+std::ptrdiff_t(i),monsters.begin()+std::ptrdiff_t((std::min)(monsters.size(),i+64)));
            handle("SC_OBJECT_ENTER_VIEW","_Handle_ObjectEnterView",enterViewRawMonsters(part));entered+=part.size();
        }
    }
    // Replay a message exactly as the official server sent it (restoring a
    // private world after leaving a room).
    void replay(const std::string& type,const std::string& bytes) {
        if(type=="SC_OBJECT_ENTER_VIEW")handle("SC_OBJECT_ENTER_VIEW","_Handle_ObjectEnterView",bytes);
        else if(type=="SC_OBJECT_LEAVE_VIEW")handle("SC_OBJECT_LEAVE_VIEW","_Handle_ObjectLeaveView",bytes);
        else throw std::runtime_error("replay type not permitted: "+type);
    }
};
}
