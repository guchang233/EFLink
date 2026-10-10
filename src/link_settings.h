#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include "nlohmann/json.hpp"

namespace eflink {
// The "=" key (VK_OEM_PLUS on the main keyboard). F1–F12 remain selectable.
constexpr int kDefaultMenuKey=VK_OEM_PLUS;
constexpr int kMenuKeyRevision=2;
inline bool validMenuKey(int key) {return key==VK_OEM_PLUS||(key>=VK_F1&&key<=VK_F12);}
// Configurations written before revision 2 carry the old F8 default; move them
// to the new default once. A player who picks F8 again keeps it (revision 2).
inline void migrateMenuKey(nlohmann::json& j) {
    if(j.value("menu_key_revision",0)<kMenuKeyRevision&&j.value("menu_key",kDefaultMenuKey)==VK_F8) j["menu_key"]=kDefaultMenuKey;
    j["menu_key_revision"]=kMenuKeyRevision;
}
inline std::filesystem::path settingsPath() {
    // Per-instance preferences for isolated launchers and portable test profiles.
    wchar_t profile[32768]{};
    const auto length=GetEnvironmentVariableW(L"EFLINK_PROFILE_DIR",profile,32768);
    if(length) {
        if(length>=32768) throw std::runtime_error("EFLINK_PROFILE_DIR is too long");
        std::filesystem::path root(profile);
        if(!root.is_absolute()) throw std::runtime_error("EFLINK_PROFILE_DIR must be absolute");
        return root/L"settings.json";
    }
    wchar_t local[32768]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768)) throw std::runtime_error("LOCALAPPDATA unavailable");
    return std::filesystem::path(local)/L"EFLink"/L"settings.json";
}
inline void validateSettings(const nlohmann::json& j) {
    if(!j.is_object()) throw std::runtime_error("configuration must be an object");
    for(auto key:{"player_name","room_name"}) if(j.contains(key)&&(!j[key].is_string()||j[key].get<std::string>().size()>64)) throw std::runtime_error(std::string(key)+": max 64 UTF-8 bytes");
    for(auto key:{"show_remote_players","show_player_markers","follow_host_world","sync_combat","gui_enabled","preview_mode"}) if(j.contains(key)&&!j[key].is_boolean()) throw std::runtime_error(std::string(key)+": expected boolean");
    const auto port=j.value("port",47615);if(port<1024||port>65535||port==47616) throw std::runtime_error("端口须为 1024–65535，且不能使用发现端口 47616");
    const auto count=j.value("max_players",8);if(count<2||count>16) throw std::runtime_error("人数上限须为 2–16");
    if(!validMenuKey(j.value("menu_key",kDefaultMenuKey))) throw std::runtime_error("菜单快捷键须为 = 或 F1–F12");
    if(j.contains("saved_servers")) {
        if(!j["saved_servers"].is_array()||j["saved_servers"].size()>64) throw std::runtime_error("最多收藏 64 个地址");
        for(const auto& s:j["saved_servers"]) if(!s.is_object()||!s.contains("address")||!s["address"].is_string()||s["address"].get<std::string>().empty()||s["address"].get<std::string>().size()>256||!s.value("name",nlohmann::json("")).is_string()||s.value("name",std::string{}).size()>128) throw std::runtime_error("invalid saved server");
    }
}
inline nlohmann::json readSettings() {
    std::ifstream in(settingsPath());if(!in) return nlohmann::json::object();
    nlohmann::json j;in>>j;validateSettings(j);migrateMenuKey(j);return j;
}
inline void writeSettings(const nlohmann::json& config) {
    validateSettings(config);
    auto path=settingsPath();std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+=L".tmp";
    // Persist only player preferences; never persist passwords, automatic joins or test options.
    nlohmann::json saved=nlohmann::json::object();
    for(auto key:{"player_name","room_name","max_players","port","show_remote_players","show_player_markers","follow_host_world","sync_combat","menu_key","saved_servers"}) if(config.contains(key)) saved[key]=config[key];
    saved["menu_key_revision"]=kMenuKeyRevision;
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<saved.dump(2);out.flush();if(!out) throw std::runtime_error("无法保存联机设置");}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("无法更新联机设置");
}
}
