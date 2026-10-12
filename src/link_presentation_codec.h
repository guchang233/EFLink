#pragma once
// Stateless compression of complete presentation frames. No critical event is
// coalesced or compressed by this adapter. Decoded size is checked before allocation.
#include <windows.h>
#include <compressapi.h>
#include <string>
#include <optional>
#include <cstring>
#include <cstdint>
#pragma comment(lib,"Cabinet.lib")
namespace eflink {
inline bool packedPresentation(const void* data,std::size_t size) {
    return data&&size>=4&&std::memcmp(data,"EFP1",4)==0;
}
inline std::string packPresentation(const std::string& plain,std::size_t limit) {
    if(plain.size()<4096||plain.size()>limit||plain.size()>UINT32_MAX)return plain;
    COMPRESSOR_HANDLE handle=nullptr;
    if(!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle))return plain;
    struct Close {COMPRESSOR_HANDLE h;~Close(){CloseCompressor(h);}} close{handle};
    SIZE_T needed=0;
    if(Compress(handle,plain.data(),plain.size(),nullptr,0,&needed)||GetLastError()!=ERROR_INSUFFICIENT_BUFFER||needed>limit)return plain;
    std::string result(needed+8,'\0');std::memcpy(result.data(),"EFP1",4);
    const auto length=std::uint32_t(plain.size());
    for(unsigned i=0;i<4;++i)result[4+i]=char(length>>(i*8));
    SIZE_T written=0;
    if(!Compress(handle,plain.data(),plain.size(),result.data()+8,needed,&written)||written>needed)return plain;
    result.resize(written+8);
    return result.size()<plain.size()*9/10?result:plain;
}
inline std::optional<std::string> unpackPresentation(const void* data,std::size_t size,std::size_t limit) {
    if(!packedPresentation(data,size)||size<9||size>limit)return std::nullopt;
    const auto* bytes=static_cast<const unsigned char*>(data);std::uint32_t length=0;
    for(unsigned i=0;i<4;++i)length|=std::uint32_t(bytes[4+i])<<(i*8);
    if(!length||length>limit)return std::nullopt;
    DECOMPRESSOR_HANDLE handle=nullptr;
    if(!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle))return std::nullopt;
    struct Close {DECOMPRESSOR_HANDLE h;~Close(){CloseDecompressor(h);}} close{handle};
    std::string result(length,'\0');SIZE_T written=0;
    if(!Decompress(handle,bytes+8,size-8,result.data(),result.size(),&written)||written!=length)return std::nullopt;
    return result;
}
}
