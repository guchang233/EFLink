#pragma once
// EF Link wire format. Little-endian, bounds-checked, no game dependencies.
//
// Transport:
//   UDP discovery port  BEACON / DISCOVER broadcasts between all instances on the LAN.
//   TCP game port       reliable, ordered frames: join handshake, player info, chat,
//                       reliable module data. Frame = u32 length | u8 type | payload.
//   UDP game port       unreliable datagrams: player state and unreliable module data.
// The room host is the hub: every client talks only to the host, which relays.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace eflink::proto {
constexpr std::uint32_t kMagic=0x4B4C4645u; // "EFLK"
constexpr std::uint16_t kProtocol=1;
constexpr std::uint16_t kDefaultPort=47615;
constexpr std::uint16_t kDiscoveryPort=47616;
constexpr std::size_t kMaxDatagram=1200;
constexpr std::size_t kMaxFrame=1u<<20;
constexpr std::size_t kMaxReliableData=256u*1024;
constexpr std::size_t kMaxUnreliableData=1024;
constexpr unsigned kMaxBones=96;
constexpr unsigned kMaxPlayers=16;
constexpr std::uint16_t kHostId=1;
constexpr std::uint16_t kEveryone=0xFFFF;

enum class Udp:std::uint8_t {Beacon=1,Discover=2,Hello=3,Welcome=4,State=5,Data=6,Ping=7,Pong=8};
// State: a player state over TCP when UDP cannot get through (TCP-only port forwarding).
enum class Tcp:std::uint8_t {Hello=1,Welcome=2,Reject=3,PlayerInfo=4,PlayerLeave=5,Chat=6,Data=7,Bye=8,KeepAlive=9,State=10};

inline std::uint32_t fnv32(std::string_view text) {
    std::uint32_t hash=2166136261u;for(unsigned char c:text) {hash^=c;hash*=16777619u;}return hash;
}
inline std::uint64_t fnv64(std::string_view text) {
    std::uint64_t hash=14695981039346656037ull;for(unsigned char c:text) {hash^=c;hash*=1099511628211ull;}return hash;
}
// Not a secure credential, only a LAN room lock: the password never travels in clear text.
inline std::uint64_t passwordProof(std::uint32_t roomId,std::string_view password) {
    return password.empty()?0:fnv64(std::to_string(roomId)+":"+std::string(password));
}

class Writer {
public:
    std::vector<unsigned char> bytes;
    template<class T> Writer& put(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* raw=reinterpret_cast<const unsigned char*>(&value);bytes.insert(bytes.end(),raw,raw+sizeof(T));return *this;
    }
    Writer& u8(std::uint8_t v) {return put(v);}
    Writer& u16(std::uint16_t v) {return put(v);}
    Writer& u32(std::uint32_t v) {return put(v);}
    Writer& u64(std::uint64_t v) {return put(v);}
    Writer& f32(float v) {return put(v);}
    Writer& str(std::string_view text,std::size_t limit=255) {
        if(text.size()>limit) {
            // Never cut a UTF-8 sequence in half: step back to the start of the cut sequence.
            std::size_t n=limit;while(n>0&&(static_cast<unsigned char>(text[n])&0xC0)==0x80) --n;
            text=text.substr(0,n);
        }
        if(limit<=255) u8(std::uint8_t(text.size()));else u16(std::uint16_t(text.size()));
        bytes.insert(bytes.end(),text.begin(),text.end());return *this;
    }
    Writer& raw(const void* data,std::size_t size) {
        if(!size) return *this;
        const auto* p=static_cast<const unsigned char*>(data);bytes.insert(bytes.end(),p,p+size);return *this;
    }
};
class Reader {
    const unsigned char* data;std::size_t size,at=0;
public:
    Reader(const void* d,std::size_t n):data(static_cast<const unsigned char*>(d)),size(n) {}
    template<class T> T get() {
        if(size-at<sizeof(T)) throw std::runtime_error("truncated message");
        T value;std::memcpy(&value,data+at,sizeof(T));at+=sizeof(T);return value;
    }
    std::uint8_t u8() {return get<std::uint8_t>();}
    std::uint16_t u16() {return get<std::uint16_t>();}
    std::uint32_t u32() {return get<std::uint32_t>();}
    std::uint64_t u64() {return get<std::uint64_t>();}
    float f32() {return get<float>();}
    std::string str(std::size_t limit=255) {
        std::size_t n=limit<=255?u8():u16();
        if(n>limit||size-at<n) throw std::runtime_error("truncated string");
        std::string text(reinterpret_cast<const char*>(data+at),n);at+=n;return text;
    }
    const unsigned char* take(std::size_t n) {
        if(size-at<n) throw std::runtime_error("truncated payload");
        const auto* p=data+at;at+=n;return p;
    }
    std::size_t remaining() const {return size-at;}
};

inline Writer udpHeader(Udp type,std::uint32_t session) {
    Writer w;w.u32(kMagic).u16(kProtocol).u8(std::uint8_t(type)).u8(0).u32(session);return w;
}
struct UdpHeader {Udp type;std::uint32_t session;};
inline UdpHeader readUdpHeader(Reader& r) {
    if(r.u32()!=kMagic) throw std::runtime_error("foreign datagram");
    if(r.u16()!=kProtocol) throw std::runtime_error("protocol mismatch");
    UdpHeader h{Udp(r.u8()),0};r.u8();h.session=r.u32();return h;
}

struct Beacon {
    std::uint32_t roomId=0;
    std::uint16_t gamePort=kDefaultPort;
    std::uint8_t players=0,maxPlayers=0;
    bool password=false;
    std::string roomName,hostName,mapId,version;
};
inline std::vector<unsigned char> encode(const Beacon& b) {
    auto w=udpHeader(Udp::Beacon,b.roomId);
    w.u16(b.gamePort).u8(b.players).u8(b.maxPlayers).u8(b.password?1:0).str(b.roomName,64).str(b.hostName,64).str(b.mapId,64).str(b.version,32);
    return std::move(w.bytes);
}
inline Beacon decodeBeacon(Reader& r,std::uint32_t session) {
    Beacon b;b.roomId=session;b.gamePort=r.u16();b.players=r.u8();b.maxPlayers=r.u8();b.password=r.u8()!=0;
    b.roomName=r.str(64);b.hostName=r.str(64);b.mapId=r.str(64);b.version=r.str(32);return b;
}

// Everything another client needs to draw this player. Sent when it changes.
struct PlayerInfo {
    std::uint16_t id=0;
    std::string name,modelPath,mapId,levelId;
    std::uint16_t skeleton=0;              // increments whenever bones changes
    std::vector<std::string> bones;        // names, in the order of PlayerState::bones
};
inline void write(Writer& w,const PlayerInfo& p) {
    w.u16(p.id).str(p.name,64).str(p.modelPath,512).str(p.mapId,64).str(p.levelId,64).u16(p.skeleton);
    const auto count=std::uint8_t(std::min<std::size_t>(p.bones.size(),kMaxBones));w.u8(count);
    for(unsigned i=0;i<count;++i) w.str(p.bones[i],96);
}
inline PlayerInfo readPlayerInfo(Reader& r) {
    PlayerInfo p;p.id=r.u16();p.name=r.str(64);p.modelPath=r.str(512);p.mapId=r.str(64);p.levelId=r.str(64);p.skeleton=r.u16();
    const auto count=r.u8();if(count>kMaxBones) throw std::runtime_error("too many bones");
    for(unsigned i=0;i<count;++i) p.bones.push_back(r.str(96));
    return p;
}

enum StateFlags:std::uint8_t {StateValid=1,StateAlive=2,StateCinematic=4,StateHidden=8,StateWeaponValid=16};
// Optional view state occupies the three reserved high bits; wire and C ABI
// layouts are unchanged. Native adapters still require room negotiation.
inline unsigned weaponState(std::uint8_t flags) {return (flags>>5)&7;}
// One sampled pose of a player's model, sent unreliably about twenty times a second.
struct PlayerState {
    std::uint16_t id=0;
    std::uint32_t sequence=0,timeMs=0;
    std::uint8_t flags=0;
    std::uint32_t mapHash=0;
    std::uint16_t skeleton=0;
    float position[3]{},rotation[4]{0,0,0,1};
    float rootOffset[3]{};                 // local position of the first bone (hips)
    std::vector<std::int16_t> bones;       // 4 per bone: quantized local rotation
};
inline std::int16_t quantize(float v) {
    if(!(v==v)) return 0;
    v=v<-1?-1:v>1?1:v;return std::int16_t(v*32767.f);
}
inline float dequantize(std::int16_t v) {return float(v)/32767.f;}
inline void write(Writer& w,const PlayerState& s) {
    w.u16(s.id).u32(s.sequence).u32(s.timeMs).u8(s.flags).u32(s.mapHash).u16(s.skeleton);
    for(float v:s.position) w.f32(v);
    for(float v:s.rotation) w.f32(v);
    for(float v:s.rootOffset) w.f32(v);
    const auto count=std::uint8_t(std::min<std::size_t>(s.bones.size()/4,kMaxBones));w.u8(count);
    w.raw(s.bones.data(),std::size_t(count)*4*sizeof(std::int16_t));
}
inline PlayerState readPlayerState(Reader& r) {
    PlayerState s;s.id=r.u16();s.sequence=r.u32();s.timeMs=r.u32();s.flags=r.u8();s.mapHash=r.u32();s.skeleton=r.u16();
    for(float& v:s.position) v=r.f32();
    for(float& v:s.rotation) v=r.f32();
    for(float& v:s.rootOffset) v=r.f32();
    for(float v:s.position) if(!std::isfinite(v)||std::abs(v)>1000000.f) throw std::runtime_error("invalid position");
    for(float v:s.rotation) if(!std::isfinite(v)||std::abs(v)>1.01f) throw std::runtime_error("invalid rotation");
    for(float v:s.rootOffset) if(!std::isfinite(v)||std::abs(v)>1000.f) throw std::runtime_error("invalid root offset");
    const auto count=r.u8();if(count>kMaxBones) throw std::runtime_error("too many bones");
    s.bones.resize(std::size_t(count)*4);std::memcpy(s.bones.data(),r.take(s.bones.size()*sizeof(std::int16_t)),s.bones.size()*sizeof(std::int16_t));
    return s;
}

inline std::vector<unsigned char> frame(Tcp type,const Writer& payload) {
    Writer w;w.u32(std::uint32_t(payload.bytes.size()+1)).u8(std::uint8_t(type));
    w.bytes.insert(w.bytes.end(),payload.bytes.begin(),payload.bytes.end());return std::move(w.bytes);
}
}
