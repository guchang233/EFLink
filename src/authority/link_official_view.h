#pragma once
// Split native view notifications without re-encoding unrelated objects or
// unknown fields. Private monsters never hitchhike on character notifications.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <utility>

namespace eflink::authority {
// Commit removal only after native dispatch succeeds. A retry starts at the
// failed message and cannot resend messages already accepted by the handler.
class DeferredOfficialMessages {
    std::deque<std::pair<std::string,std::string>> messages;
    std::size_t bytes=0,countLimit,byteLimit;
public:
    explicit DeferredOfficialMessages(std::size_t count=512,std::size_t capacity=32u*1024*1024):countLimit(count),byteLimit(capacity) {}
    bool append(std::string type,std::string data) {
        if(messages.size()>=countLimit||data.size()>byteLimit-bytes)return false;
        const auto size=data.size();messages.emplace_back(std::move(type),std::move(data));bytes+=size;return true;
    }
    template<class Dispatch> void replay(Dispatch dispatch) {
        while(!messages.empty()) {
            dispatch(messages.front().first,messages.front().second);
            bytes-=messages.front().second.size();messages.pop_front();
        }
    }
    std::size_t size() const {return messages.size();}
    std::size_t byteSize() const {return bytes;}
    bool empty() const {return messages.empty();}
};
struct OfficialViewSplit {
    bool changed=false;
    std::string visible,withheld;
};
namespace view_wire {
struct Field {unsigned number,wire;std::uint64_t value;std::string_view raw,payload;};
inline std::uint64_t varint(std::string_view bytes,std::size_t& at) {
    std::uint64_t value=0;
    for(unsigned shift=0;shift<64;shift+=7) {
        if(at==bytes.size())throw std::runtime_error("truncated native view varint");
        const auto byte=static_cast<unsigned char>(bytes[at++]);
        if(shift==63&&byte>1)throw std::runtime_error("native view varint overflow");
        value|=std::uint64_t(byte&127)<<shift;
        if(!(byte&128))return value;
    }
    throw std::runtime_error("native view varint overflow");
}
inline std::vector<Field> fields(std::string_view bytes) {
    std::vector<Field> result;
    for(std::size_t at=0;at<bytes.size();) {
        const auto start=at;const auto key=varint(bytes,at);
        if(!(key>>3)||(key>>3)>0x1fffffff)throw std::runtime_error("invalid native view field");
        const auto wire=unsigned(key&7);std::uint64_t value=0;std::string_view payload;
        if(wire==0)value=varint(bytes,at);
        else {
            std::uint64_t size=0;
            if(wire==1)size=8;else if(wire==5)size=4;
            else if(wire==2)size=varint(bytes,at);
            else throw std::runtime_error("unsupported native view wire type");
            if(size>bytes.size()-at)throw std::runtime_error("truncated native view field");
            payload=bytes.substr(at,std::size_t(size));at+=std::size_t(size);
        }
        result.push_back({unsigned(key>>3),wire,value,bytes.substr(start,at-start),payload});
    }
    return result;
}
inline void writeVarint(std::string& out,std::uint64_t value) {
    while(value>=128){out.push_back(char((value&127)|128));value>>=7;}out.push_back(char(value));
}
inline void message(std::string& out,unsigned number,const std::string& bytes) {
    writeVarint(out,(std::uint64_t(number)<<3)|2);writeVarint(out,bytes.size());out+=bytes;
}
}
inline OfficialViewSplit splitOfficialView(const std::string& type,const std::string& bytes) {
    OfficialViewSplit result;bool visibleObjects=false;
    if(type!="SC_OBJECT_ENTER_VIEW"&&type!="SC_OBJECT_LEAVE_VIEW")return result;
    for(const auto& field:view_wire::fields(bytes)) {
        if(field.number==1&&field.wire==2) {
            if(type=="SC_OBJECT_ENTER_VIEW") {
                std::string visible,withheld;
                for(const auto& object:view_wire::fields(field.payload)) {
                    if(object.number==2&&object.wire==2) {withheld+=object.raw;result.changed=true;}
                    else {visible+=object.raw;visibleObjects=true;}
                }
                if(!visible.empty())view_wire::message(result.visible,1,visible);
                if(!withheld.empty())view_wire::message(result.withheld,1,withheld);
            } else {
                std::uint64_t objectType=0;
                for(const auto& object:view_wire::fields(field.payload))
                    if(object.number==1&&object.wire==0)objectType=object.value;
                if(objectType==3) {result.withheld+=field.raw;result.changed=true;}
                else {result.visible+=field.raw;visibleObjects=true;}
            }
        } else {
            result.visible+=field.raw;result.withheld+=field.raw;
        }
    }
    if(!result.changed) {result.visible.clear();result.withheld.clear();}
    else if(!visibleObjects)result.visible.clear();
    return result;
}
}
