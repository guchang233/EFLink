#pragma once
// Crash diagnostics: a vectored exception handler records faulting exceptions
// (address, registers, call stack as module+offset) together with the EF Link step
// that was running, into %LOCALAPPDATA%\EFLink\crash.log. Observation only: the
// exception always continues to the game's own handlers.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace eflink::crash {
inline std::atomic<const char*> step{"idle"};
inline char detail[192]{};
// Name the step about to run (static string); `what` may add a short detail.
inline void mark(const char* name) {step.store(name,std::memory_order_relaxed);}
inline void mark(const char* name,const std::string& what) {strncpy_s(detail,sizeof detail,what.c_str(),_TRUNCATE);step.store(name,std::memory_order_relaxed);}
inline HANDLE file=INVALID_HANDLE_VALUE;
inline PVOID handler=nullptr;
inline std::atomic<int> written{0};
inline int locate(char* out,std::size_t size,std::uintptr_t address) {
    HMODULE module=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(address),&module)||!module)
        return std::snprintf(out,size,"0x%llX",static_cast<unsigned long long>(address));
    char path[MAX_PATH]{};GetModuleFileNameA(module,path,MAX_PATH);
    const char* name=std::strrchr(path,'\\');name=name?name+1:path;
    return std::snprintf(out,size,"%s+0x%llX",name,static_cast<unsigned long long>(address-reinterpret_cast<std::uintptr_t>(module)));
}
inline LONG CALLBACK onException(EXCEPTION_POINTERS* e) {
    const auto code=e->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_ILLEGAL_INSTRUCTION&&code!=EXCEPTION_STACK_OVERFLOW&&
       code!=EXCEPTION_INT_DIVIDE_BY_ZERO&&code!=EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    if(file==INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    // Each faulting address is written once (handled exceptions can repeat).
    static std::atomic<std::uintptr_t> seen[256]{};
    const auto at=reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    for(auto& slot:seen) {auto v=slot.load();if(v==at) return EXCEPTION_CONTINUE_SEARCH;if(!v) {if(slot.compare_exchange_strong(v,at)) break;}}
    if(written.fetch_add(1)>=200) return EXCEPTION_CONTINUE_SEARCH;
    char text[4096];int n=0;SYSTEMTIME t;GetLocalTime(&t);
    char where[160];locate(where,sizeof where,reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress));
    const auto* c=e->ContextRecord;
    n+=std::snprintf(text+n,sizeof text-n,"%02d:%02d:%02d.%03d tid %lu code 0x%08lX at %s step [%s] %s\r\n",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,
        GetCurrentThreadId(),code,where,step.load(),detail);
    if(code==EXCEPTION_ACCESS_VIOLATION&&e->ExceptionRecord->NumberParameters>=2)
        n+=std::snprintf(text+n,sizeof text-n,"  %s 0x%llX\r\n",e->ExceptionRecord->ExceptionInformation[0]==1?"write":e->ExceptionRecord->ExceptionInformation[0]==8?"execute":"read",
            static_cast<unsigned long long>(e->ExceptionRecord->ExceptionInformation[1]));
    n+=std::snprintf(text+n,sizeof text-n,"  rcx %llX rdx %llX r8 %llX r9 %llX rax %llX rsp %llX\r\n",c->Rcx,c->Rdx,c->R8,c->R9,c->Rax,c->Rsp);
    void* frames[40]{};const auto count=RtlCaptureStackBackTrace(0,40,frames,nullptr);
    for(USHORT i=0;i<count&&n<int(sizeof text)-200;++i) {
        char frame[160];locate(frame,sizeof frame,reinterpret_cast<std::uintptr_t>(frames[i]));
        n+=std::snprintf(text+n,sizeof text-n,"    %s\r\n",frame);
    }
    DWORD out=0;WriteFile(file,text,DWORD(n),&out,nullptr);FlushFileBuffers(file);
    return EXCEPTION_CONTINUE_SEARCH;
}
inline void install(const char* module) {
    if(handler) return;
    char base[MAX_PATH]{};
    if(!GetEnvironmentVariableA("LOCALAPPDATA",base,MAX_PATH)) return;
    std::string dir=std::string(base)+"\\EFLink";CreateDirectoryA(dir.c_str(),nullptr);
    file=CreateFileA((dir+"\\crash.log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    char text[256];SYSTEMTIME t;GetLocalTime(&t);
    const int n=std::snprintf(text,sizeof text,"==== %04d-%02d-%02d %02d:%02d:%02d pid %lu %s\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),module);
    DWORD out=0;WriteFile(file,text,DWORD(n),&out,nullptr);
    written=0;handler=AddVectoredExceptionHandler(1,onException);
}
inline void uninstall() {
    if(handler) {RemoveVectoredExceptionHandler(handler);handler=nullptr;}
    if(file!=INVALID_HANDLE_VALUE) {CloseHandle(file);file=INVALID_HANDLE_VALUE;}
}
}
