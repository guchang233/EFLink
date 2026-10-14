#pragma once
// Bounded protobuf fields for read-only native appearance observations.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace eflink::authority {
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
}
