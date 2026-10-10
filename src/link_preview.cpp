// A real native menu using the shipping DLL and transport, without loading game assets.
#include "BetterEndfield/ThirdPartyModule.h"
#include "nlohmann/json.hpp"
#include <windows.h>
#include <filesystem>
#include <cstdio>
using nlohmann::json;
void BE_CALL log(void*,const char* s) {std::puts(s);}
BE_Result BE_CALL reply(void*,const char*,BE_Result,const char*) {return BE_Result_Ok;}
int wmain(int argc,wchar_t** argv) {
    std::wstring moduleId=L"eflink.core";
    for(int i=1;i<argc;++i) if(std::wstring(argv[i])==L"--module"&&i+1<argc) moduleId=argv[++i];
    if(moduleId.rfind(L"eflink.",0)!=0||moduleId.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789._-")!=std::wstring::npos) return 2;
    std::string moduleName;for(auto value:moduleId) moduleName.push_back(static_cast<char>(value));
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto library=LoadLibraryW((std::filesystem::path(exe).parent_path()/(moduleId+L".dll")).c_str());
    if(!library) {std::puts("Build the module first.");return 1;}
    auto entry=reinterpret_cast<BE_GetThirdPartyModuleV1Fn>(GetProcAddress(library,BETTER_ENDFIELD_THIRD_PARTY_ENTRY_V1));if(!entry) return 1;
    auto* module=entry();BE_ThirdPartyHostV1 host{};host.struct_size=sizeof(host);host.version=1;host.module_id=moduleName.c_str();host.platform="windows-x64";host.package_root="";host.log=log;host.reply=reply;
    json config={{"preview_mode",true},{"player_name","管理员"}};bool capture=false,autoHost=false;int tab=0;
    for(int i=1;i<argc;++i) {
        std::wstring arg=argv[i];
        if(arg==L"--capture"&&i+1<argc) {auto p=std::filesystem::absolute(argv[++i]).u8string();config["preview_capture"]=std::string(p.begin(),p.end());capture=true;}
        else if(arg==L"--tab"&&i+1<argc) tab=_wtoi(argv[++i]);
        else if(arg==L"--host") autoHost=true;
        else if(arg==L"--hud") config["preview_hud"]=true;
        else if(arg==L"--module"&&i+1<argc) ++i;
        else {std::puts("EFLink.Preview.exe [--module eflink.core] [--capture path.bmp] [--tab 0..5] [--host] [--hud]");return 2;}
    }
    config["preview_tab"]=tab;if(autoHost) {config["auto_host"]=true;config["room_name"]="EF Link 测试房间";config["port"]=47735;}
    if(module->initialize(&host,config.dump().c_str())!=BE_Result_Ok) return 1;
    if(capture) {
        const auto utf8=config["preview_capture"].get<std::string>();
        const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()),utf8.size()));
        const auto deadline=GetTickCount64()+30000;
        while(!std::filesystem::exists(path)&&GetTickCount64()<deadline) Sleep(100);
        if(!std::filesystem::exists(path)) {module->shutdown();std::puts("Native menu capture timed out.");return 1;}
    }
    else {std::puts("Native menu active. Close the window to finish; Ctrl+C also exits.");
        while(!FindWindowW((L"EFLink.NativeMenu."+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr)) Sleep(50);
        while(FindWindowW((L"EFLink.NativeMenu."+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr)) Sleep(100);}
    module->shutdown();return 0;
}
