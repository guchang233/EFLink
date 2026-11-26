// EF Link interface: transparent DirectComposition overlay over the game window.
// HUD (name tags, teammate vitals, floating damage meter, shared ATB sync bar,
// chat, PVP duel invitations) is drawn over the game; the menu key turns the
// overlay interactive and shows the Endfield-styled multiplayer menu.
#include "link_version.h"
#include "link_gui.h"
#include "link_settings.h"
#include "link_combat_stats.h"
#include "link_operator_names.h"
#include "link_ui_theme.h"
#include "link_ui_widgets.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

using Microsoft::WRL::ComPtr;
using nlohmann::json;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
thread_local ImGuiContext* EFLinkImGuiContext = nullptr;

namespace eflink {
namespace {
std::uint64_t now() { return GetTickCount64(); }
template <class T> bool ready(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
std::string str(const json& j, const char* key, const std::string& fallback = {}) {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}
void copyText(char* dst, std::size_t capacity, const std::string& text) {
    strncpy_s(dst, capacity, text.c_str(), _TRUNCATE);
}
const char* fKeyName(int vk) {
    static const char* names[] = {"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    return vk >= VK_F1 && vk <= VK_F12 ? names[vk - VK_F1] : "?";
}
std::string menuKeyName(int vk) {
    return vk == VK_OEM_PLUS ? "=" : std::string(fKeyName(vk));
}
std::string formatPosition(const json& p) {
    if (!p.is_array() || p.size() != 3) return "—";
    char text[96]{};
    std::snprintf(text, sizeof text, "%.1f, %.1f, %.1f", p[0].get<double>(), p[1].get<double>(), p[2].get<double>());
    return text;
}
const char* blockerText(const std::string& b) {
    return b == "host_world_isolation" ? "房主世界隔离" :
           b == "host_logical_players" ? "房主逻辑玩家" :
           b == "host_combat_settlement" ? "房主战斗结算" :
           b == "host_scene_rules" ? "房主地图与副本规则" :
           b == "guest_map_loading" ? "加载房主地图" :
           b == "guest_entity_replicas" ? "重建房主实体" :
           b == "guest_world_isolation" ? "房客世界隔离" :
           b == "guest_logical_players" ? "房客逻辑玩家" :
           b == "host_player_entity" ? "房主侧角色" : b.c_str();
}
ui::Tone roleTone(const std::string& r) {
    if (r == "host") return ui::Tone::Accent;
    if (r == "client") return ui::Tone::Success;
    if (r == "connecting") return ui::Tone::Warning;
    return ui::Tone::Neutral;
}
const char* roleLabel(const std::string& r) {
    if (r == "host") return "房主";
    if (r == "client") return "已连接";
    if (r == "connecting") return "连接中";
    return "离线";
}

// ---- game window -----------------------------------------------------------
HWND findGame() {
    HWND result = nullptr;
    EnumWindows([](HWND w, LPARAM arg) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        wchar_t klass[64]{};
        GetClassNameW(w, klass, 64);
        if (pid == GetCurrentProcessId() && IsWindowVisible(w) && wcscmp(klass, L"UnityWndClass") == 0 && GetWindow(w, GW_OWNER) == nullptr) {
            *reinterpret_cast<HWND*>(arg) = w;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

// ---- demo fixtures for EFLink.Preview.exe ----------------------------------
json demoStatus(const std::string& role) {
    const bool host = role == "host", client = role == "client";
    const bool offline = !(host || client);
    json peers = json::array();
    json vitals = json::object();
    if (host || client) {
        peers.push_back({{"id",1},{"name",host?"管理员":"伊冯"},{"host",host},{"local",true},{"map","map01"},
            {"level","map01_lv001"},{"model","Assets/chr_0003_endminf_postmodel.prefab"},{"address","192.168.2.3:47615"},
            {"rtt_ms",0},{"valid",true},{"same_map",true},{"state_age_ms",35},{"position",{-152.1,104.6,-491.0}},
            {"vitals",{{"id",1},{"hp",16840},{"max_hp",18200},{"usp",100.0},{"max_usp",100.0},{"op","管理员"},{"alive",true}}}});
        peers.push_back({{"id",2},{"name",host?"伊冯":"管理员"},{"host",client},{"local",false},{"map","map01"},
            {"level","map01_lv001"},{"model","Assets/chr_0017_yvonne_postmodel.prefab"},{"address","192.168.2.9:51967"},
            {"rtt_ms",18},{"valid",true},{"same_map",true},{"state_age_ms",95},{"distance",12.4f},{"position",{-153.9,104.7,-493.2}},
            {"vitals",{{"id",2},{"hp",12450},{"max_hp",16800},{"usp",78.5},{"max_usp",100.0},{"op","伊冯"},{"alive",true}}}});
        peers.push_back({{"id",3},{"name","佩丽卡小队"},{"host",false},{"local",false},{"map","map01"},
            {"level","map01_lv001"},{"model","Assets/chr_0004_pelica_postmodel.prefab"},{"address","192.168.2.14:52011"},
            {"rtt_ms",31},{"valid",true},{"same_map",true},{"state_age_ms",110},{"distance",26.8f},{"position",{-140.4,104.2,-502.6}},
            {"vitals",{{"id",3},{"hp",5120},{"max_hp",15400},{"usp",42.0},{"max_usp",100.0},{"op","佩丽卡"},{"alive",true}}}});
        peers.push_back({{"id",4},{"name","莱万汀"},{"host",false},{"local",false},{"map","map02"},
            {"level","map02_lv001"},{"model","Assets/chr_0016_laevat_postmodel.prefab"},{"address","192.168.2.21:52144"},
            {"rtt_ms",58},{"valid",true},{"same_map",false},{"state_age_ms",1200},{"position",{44.0,12.5,82.1}},
            {"vitals",{{"id",4},{"hp",19100},{"max_hp",19100},{"usp",100.0},{"max_usp",100.0},{"op","莱万汀"},{"alive",true}}}});
        for (const auto& p : peers) vitals[std::to_string(p.at("id").get<int>())] = p.at("vitals");
    }
    json rooms = json::array({
        {{"room_id",763792363},{"name","四号谷地开荒 · 共用技力"},{"host","管理员"},{"players",3},{"max_players",4},{"password",false},{"map","map01"},{"address","192.168.2.3:47615"}},
        {{"room_id",918273645},{"name","周本速刷 / 开PVP"},{"host","伊冯"},{"players",2},{"max_players",8},{"password",true},{"map","map01"},{"address","192.168.2.9:47615"}},
        {{"room_id",118273645},{"name","源石矿区探索"},{"host","莱万汀"},{"players",1},{"max_players",4},{"password",false},{"map","map02"},{"address","192.168.2.21:47700"}}
    });
    json chat = json::array();
    if (host || client) {
        chat.push_back({{"serial",1},{"name",""},{"text","伊冯 加入了房间"},{"system",true}});
        chat.push_back({{"serial",2},{"name","管理员"},{"text","共用技力条已经开了，进场先攒两格直接打爆发！"},{"system",false}});
        chat.push_back({{"serial",3},{"name","伊冯"},{"text","收到，我大招快好了，等我破防直接丢战技"},{"system",false}});
        chat.push_back({{"serial",4},{"name","佩丽卡小队"},{"text","我来补感电和连携技"},{"system",false}});
    }
    json timeline = json::array({
        0, 0, 12400, 18900, 26200, 31400, 24100, 19800, 34500, 48200,
        52900, 41200, 33600, 28400, 22100, 39800, 61200, 58400, 47100, 35600,
        29400, 31800, 44200, 53100, 49800, 37200, 41600, 46900, 38400, 42100
    });
    json stats = {
        {"generation", 2}, {"in_combat", true}, {"duration_s", 42.6},
        {"total_damage", 1486200.0}, {"team_dps", 34887.0}, {"total_hits", 418}, {"total_kills", 9},
        {"timeline", timeline},
        {"players", json::array({
            {{"id",1},{"name","管理员"},{"op","管理员"},{"damage",612400.0},{"dps",14376.0},{"share",0.412},
             {"hits",164},{"crits",72},{"crit_rate",0.439},{"max_hit",42850.0},{"kills",4},{"taken",4120.0},
             {"by_type",{{"physical",401200},{"fire",129000},{"ether",82200}}}},
            {{"id",2},{"name","伊冯"},{"op","伊冯"},{"damage",528900.0},{"dps",12415.0},{"share",0.356},
             {"hits",142},{"crits",59},{"crit_rate",0.415},{"max_hit",51200.0},{"kills",3},{"taken",6350.0},
             {"by_type",{{"cryst",392400},{"physical",136500}}}},
            {{"id",3},{"name","佩丽卡小队"},{"op","佩丽卡"},{"damage",344900.0},{"dps",8096.0},{"share",0.232},
             {"hits",112},{"crits",38},{"crit_rate",0.339},{"max_hit",29640.0},{"kills",2},{"taken",9820.0},
             {"by_type",{{"pulse",284100},{"physical",60800}}}}
        })}
    };
    json sharedAtb = {
        {"allowed", true}, {"active", !offline},
        {"value", 216.5}, {"max", 300.0}, {"recover", 8.0},
        {"syncs_sent", 128}, {"syncs_applied", 124}, {"deltas_sent", 19}, {"deltas_applied", 19}, {"error", ""},
        {"events", json::array({
            {{"serial",1},{"peer",2},{"name","伊冯"},{"delta",-100.0},{"reason","cost"},{"age_ms",1200}},
            {{"serial",2},{"peer",1},{"name","管理员"},{"delta",24.5},{"reason","gain"},{"age_ms",450}}
        })}
    };
    return {
        {"module","eflink.core"},{"version",EFLINK_VERSION},{"name",offline?"管理员":(host?"管理员":"伊冯")},
        {"role",offline?"offline":role},{"local_id",1},
        {"room",{{"id",763792363},{"name","四号谷地开荒 · 共用技力"},{"max_players",4},{"password",false},{"port",47615},{"address",host?"0.0.0.0":""}}},
        {"rtt_ms",18},{"peers",peers},{"rooms",rooms},{"chat",chat},{"chat_serial",4},{"last_error",""},
        {"addresses",json::array({{{"ip","192.168.2.3"},{"adapter","以太网"}},{{"ip","10.4.0.7"},{"adapter","ZeroTier"}}})},
        {"vitals",vitals},{"shared_atb",sharedAtb},{"combat_stats",stats},
        {"game",{{"map_id","map01"},{"level_id","map01_lv001"},{"local_valid",true},{"local_bones",64},{"local_hp",16840},{"local_max_hp",18200},{"local_usp",100.0},{"local_max_usp",100.0},{"error",""},
            {"world_replication",{{"matched",14},{"missing",0},{"teleport","idle"},{"error",""},
                {"guest_policy",{{"restricted",client},{"ready",true},{"error",""}}},
                {"official_teleport",json::object()},
                {"host_map_change",{{"pending",false},{"level",""},{"deferred",false},{"token",0}}},
                {"combat",{{"guest_hits_sent",142},{"room_hits_applied",254},{"player_casts_sent",68},{"player_casts_mirrored",65},{"player_casts_rejected",1},{"local_weapon","wpn_sword_0012"},{"weapon_lookups",12},{"squad",{{"poses_sent",2410},{"poses_in",4820},{"remote",json::array({1,2,3})}}}}}}}}},
        {"world_session",{{"ready",true},{"epoch",12},{"error",""},
            {"entry",{{"active",!offline},{"phase",offline?"offline":"active"},{"blockers",json::array()}}}}},
        {"pvp",{{"allowed",true},{"active",!offline},{"scale",1.0},{"revive",true},
            {"duels",json::array({{{"id",2},{"name","伊冯"}}})},{"invites",json::array()},{"notice",""},{"notice_serial",0}}},
        {"hook_ready",true},{"op_filter_ready",true},{"damage_hook_ready",true},{"transferred_hook_ready",true},
        {"atb_cost_hook_ready",true},{"atb_set_hook_ready",true},{"atb_update_hook_ready",true},{"atb_reset_hook_ready",true},
        {"testing_invincible",false},
        {"gui_render",{{"backend","overlay_dcomp"},{"frames",1420}}},
        {"configuration",{{"player_name",offline?"管理员":(host?"管理员":"伊冯")},{"room_name","四号谷地开荒 · 共用技力"},
            {"port",47615},{"max_players",4},{"menu_key",VK_OEM_PLUS},{"pvp_key",VK_F7},{"damage_reset_key",VK_F9},
            {"show_remote_players",true},{"show_remote_squad",true},{"replay_skills",true},{"outline_teammates",true},
            {"show_player_markers",true},{"follow_host_world",true},{"sync_combat",true},
            {"room_pvp",!offline},{"room_pvp_scale",1.0},{"room_pvp_revive",true},{"room_shared_atb",true},
            {"show_damage_hud",true},{"show_team_hud",true},{"show_atb_hud",true},{"hud_compact",false},{"hud_opacity",0.86},{"damage_auto_reset",false},
            {"saved_servers",json::array({{{"address","192.168.2.3:47615"},{"name","宿舍主机"}},{{"address","10.4.0.7:47615"},{"name","异地组网专线"}}})}}},
        {"traffic",{{"bytes_in",184320},{"bytes_out",96256},{"states_in",420},{"states_out",420},{"udp_links",3},{"tcp_only_links",0}}}
    };
}

json demoHud(const json& st) {
    return {
        {"markers", json::array({
            {{"id",1},{"name","管理员"},{"self",true},{"host",true},{"x",.48f},{"y",.55f},{"distance",0.f}},
            {{"id",2},{"name","伊冯"},{"host",false},{"x",.36f},{"y",.40f},{"distance",12.4f}},
            {{"id",3},{"name","佩丽卡小队"},{"host",false},{"x",.66f},{"y",.34f},{"distance",26.8f}},
            {{"id",4},{"name","莱万汀"},{"x",1.35f},{"y",.28f},{"distance",118.7f}}
        })},
        {"vitals", st.value("vitals", json::object())},
        {"shared_atb", st.value("shared_atb", json::object())},
        {"combat_stats", st.value("combat_stats", json::object())}
    };
}

// ---- interface state and drawing -------------------------------------------
struct Ui {
    std::function<std::future<json>(const json&)> post;
    json state = json::object(), hud = json::object();
    std::future<json> statusCall, hudCall;
    std::uint64_t statusSent = 0, hudSent = 0;
    struct Pending { std::future<json> result; std::string action; std::uint64_t sent = 0; };
    std::vector<Pending> actions;

    std::string toast;
    std::uint64_t toastAt = 0;
    bool toastError = false;

    int page = 0;
    bool open = false, closeRequested = false, initialized = false;
    bool leaveConfirm = false, kickPending = false;
    int kickId = -1;
    std::string kickName;

    // Editable fields & preferences
    char name[65]{}, room[65]{}, hostPassword[129]{}, address[257]{}, joinPassword[129]{}, serverName[129]{}, chat[1001]{};
    int port = 47615, limit = 8, key = kDefaultMenuKey, pvpKey = VK_F7, damageResetKey = VK_F9;
    bool remote = true, squad = true, replay = true, outline = true, markers = true, follow = true, combat = true;
    bool pvp = false, pvpRevive = true, sharedAtb = false;
    float pvpScale = 1.f;
    // HUD customization preferences
    bool showDamageHud = true, showTeamHud = true, showAtbHud = true, hudCompact = false, damageAutoReset = false;
    float hudOpacity = 0.86f;
    bool resetHudPosRequested = false;

    std::uint64_t lastChat = 0, unreadChat = 0, lastSeenChatSerial = 0;
    bool nameEdited = false;

    // HUD chat input
    bool chatting = false, chatFocus = false, chatPrimed = false;
    char hudChat[1001]{};
    std::map<std::uint64_t, std::uint64_t> chatSeen;

    // PVP duel invitations
    std::uint64_t pvpNoticeSeen = 0;
    bool pvpNoticePrimed = false;

    // Interactive HUD hit rectangles (duel invite, draggable floating damage meter)
    struct HitRect { ImVec2 min, max; };
    std::vector<HitRect> hudHitRects;

    // Menu window drag state
    bool menuLayoutReady = false, dragActive = false;
    ImVec2 menuPos{}, menuSize{}, lastDisplay{1920.f, 1080.f};

    void say(std::string text, bool error) { toast = std::move(text); toastAt = now(); toastError = error; }
    void act(json input) { const auto a = str(input, "action"); actions.push_back({post(input), a, now()}); }

    void init() {
        if (initialized || state.empty()) return;
        initialized = true;
        copyText(name, sizeof name, str(state, "name"));
        const auto c = state.value("configuration", json::object());
        copyText(room, sizeof room, str(c, "room_name"));
        port = c.value("port", 47615);
        limit = c.value("max_players", 8);
        key = c.value("menu_key", kDefaultMenuKey);
        pvpKey = c.value("pvp_key", int(VK_F7));
        damageResetKey = c.value("damage_reset_key", int(VK_F9));
        remote = c.value("show_remote_players", true);
        squad = c.value("show_remote_squad", true);
        replay = c.value("replay_skills", true);
        outline = c.value("outline_teammates", true);
        markers = c.value("show_player_markers", true);
        follow = c.value("follow_host_world", true);
        combat = c.value("sync_combat", true);
        pvp = c.value("room_pvp", false);
        pvpScale = c.value("room_pvp_scale", 1.f);
        pvpRevive = c.value("room_pvp_revive", true);
        sharedAtb = c.value("room_shared_atb", false);
        showDamageHud = c.value("show_damage_hud", true);
        showTeamHud = c.value("show_team_hud", true);
        showAtbHud = c.value("show_atb_hud", true);
        hudCompact = c.value("hud_compact", false);
        hudOpacity = (std::clamp)(c.value("hud_opacity", 0.86f), 0.25f, 1.0f);
        damageAutoReset = c.value("damage_auto_reset", false);
        lastSeenChatSerial = state.value("chat_serial", std::uint64_t(0));
    }

    void poll(bool menuOpen) {
        const auto t = now();
        if (ready(statusCall)) {
            auto r = statusCall.get();
            if (r.contains("role")) {
                state = std::move(r);
                init();
                const auto cfg = state.value("configuration", json::object());
                key = cfg.value("menu_key", key);
                pvpKey = cfg.value("pvp_key", pvpKey);
                damageResetKey = cfg.value("damage_reset_key", damageResetKey);
                if (!nameEdited) copyText(name, sizeof name, str(state, "name"));
                const auto p = state.value("pvp", json::object());
                const auto serial = p.value("notice_serial", std::uint64_t(0));
                if (pvpNoticePrimed && serial != pvpNoticeSeen && !str(p, "notice").empty()) {
                    say("［PVP］" + str(p, "notice"), false);
                }
                pvpNoticeSeen = serial;
                pvpNoticePrimed = true;
                const auto chatSerial = state.value("chat_serial", std::uint64_t(0));
                if (menuOpen && page == 3) {
                    lastSeenChatSerial = chatSerial;
                    unreadChat = 0;
                } else if (chatSerial > lastSeenChatSerial) {
                    unreadChat = chatSerial - lastSeenChatSerial;
                }
            }
        } else if (statusCall.valid() && t - statusSent > 5000) statusCall = {};
        if (!statusCall.valid() && t - statusSent >= (menuOpen ? 250u : 500u)) {
            statusSent = t;
            statusCall = post({{"action", "status"}});
        }

        if (ready(hudCall)) {
            auto r = hudCall.get();
            if (r.contains("markers")) hud = std::move(r);
        } else if (hudCall.valid() && t - hudSent > 5000) hudCall = {};
        if (!hudCall.valid() && t - hudSent >= 60) {
            hudSent = t;
            hudCall = post({{"action", "hud"}});
        }

        for (auto it = actions.begin(); it != actions.end();) {
            if (ready(it->result)) {
                auto r = it->result.get();
                if (r.contains("error")) say(r["error"].is_string() ? r["error"].get<std::string>() : "操作失败", true);
                else if (r.contains("role")) state = std::move(r);
                it = actions.erase(it);
            } else if (t - it->sent > 15000) it = actions.erase(it);
            else ++it;
        }
    }

    std::string role() const { return str(state, "role", "offline"); }
    bool online() const { const auto r = role(); return r == "host" || r == "client"; }

    const json& liveVitals() const {
        if (hud.contains("vitals") && hud["vitals"].is_object() && !hud["vitals"].empty()) return hud["vitals"];
        static const json empty = json::object();
        auto it = state.find("vitals");
        return it != state.end() && it->is_object() ? *it : empty;
    }
    const json& liveSharedAtb() const {
        if (hud.contains("shared_atb") && hud["shared_atb"].is_object() && !hud["shared_atb"].empty()) return hud["shared_atb"];
        static const json empty = json::object();
        auto it = state.find("shared_atb");
        return it != state.end() && it->is_object() ? *it : empty;
    }
    const json& liveCombatStats() const {
        if (hud.contains("combat_stats") && hud["combat_stats"].is_object() && !hud["combat_stats"].empty()) return hud["combat_stats"];
        static const json empty = json::object();
        auto it = state.find("combat_stats");
        return it != state.end() && it->is_object() ? *it : empty;
    }

    void saveSettings() {
        json patch = {
            {"room_name", room}, {"port", port}, {"max_players", limit},
            {"menu_key", key}, {"pvp_key", pvpKey}, {"damage_reset_key", damageResetKey},
            {"show_remote_players", remote}, {"show_remote_squad", squad}, {"replay_skills", replay},
            {"outline_teammates", outline}, {"show_player_markers", markers},
            {"follow_host_world", follow}, {"sync_combat", combat},
            {"room_shared_atb", sharedAtb},
            {"show_damage_hud", showDamageHud}, {"show_team_hud", showTeamHud},
            {"show_atb_hud", showAtbHud}, {"hud_compact", hudCompact},
            {"hud_opacity", double(hudOpacity)}, {"damage_auto_reset", damageAutoReset}
        };
        if (nameEdited) { patch["player_name"] = name; nameEdited = false; }
        act({{"action", "settings"}, {"configuration", patch}});
        say("设置已保存", false);
    }

    void applySwitches() {
        act({{"action", "settings"}, {"configuration", {
            {"show_remote_players", remote}, {"show_remote_squad", squad}, {"replay_skills", replay},
            {"outline_teammates", outline}, {"show_player_markers", markers},
            {"follow_host_world", follow}, {"sync_combat", combat},
            {"show_damage_hud", showDamageHud}, {"show_team_hud", showTeamHud},
            {"show_atb_hud", showAtbHud}, {"hud_compact", hudCompact},
            {"hud_opacity", double(hudOpacity)}, {"damage_auto_reset", damageAutoReset}
        }}});
    }

    void join() {
        if (*address) act({{"action", "join"}, {"address", address}, {"password", joinPassword}});
        else say("请输入房主地址", true);
    }

    // =========================================================================
    // IN-GAME TRANSPARENT HUD OVERLAYS
    // =========================================================================
    void drawHud(ImVec2 size) {
        hudHitRects.clear();
        auto* d = ImGui::GetBackgroundDrawList();
        const bool showTags = online() && markers;
        std::map<int, float> distances;
        if (showTags) {
            ui::pushNote();
            const auto accent = ui::u32(ui::kAccent);
            const auto white  = ui::u32(ui::kText);
            const auto ink    = ui::u32(ui::kAccentInk);
            for (const auto& m : hud.value("markers", json::array())) {
                if (m.value("other_map", false) || m.value("self", false)) continue;
                const int id = m.value("id", 0);
                const float dist = m.value("distance", 0.f);
                distances[id] = dist;
                float x = m.value("x", .5f) * size.x, y = m.value("y", .5f) * size.y;
                if (!std::isfinite(x) || !std::isfinite(y)) continue;
                const bool behind = m.value("behind", false);
                char idText[8]{}; std::snprintf(idText, sizeof idText, "%02d", id);
                char distText[24]{}; std::snprintf(distText, sizeof distText, "%.0fm", dist);
                if (behind || x < 40 || y < 40 || x > size.x - 40 || y > size.y - 40) {
                    float dx = x - size.x * .5f, dy = y - size.y * .5f;
                    if (behind) { dx = -dx; dy = -dy; }
                    if (std::abs(dx) + std::abs(dy) < .001f) dy = -1;
                    const float len = std::sqrt(dx * dx + dy * dy), ux = dx / len, uy = dy / len;
                    const float k = 1.f / (std::max)(std::abs(dx) / (std::max)(1.f, size.x * .5f - 46), std::abs(dy) / (std::max)(1.f, size.y * .5f - 46));
                    float ex = size.x * .5f + dx * k, ey = size.y * .5f + dy * k;
                    if (showTeamHud) {
                        const float teamLeft = size.x - (hudCompact ? ui::px(256) : ui::px(296)) - ui::px(24);
                        if (ex > teamLeft && ey > size.y * 0.20f && ey < size.y * 0.78f) ex = teamLeft;
                    }
                    d->AddTriangleFilled({ex + ux * 10, ey + uy * 10}, {ex - uy * 7 - ux * 5, ey + ux * 7 - uy * 5}, {ex + uy * 7 - ux * 5, ey - ux * 7 - uy * 5}, accent);
                    char lbl[48]{}; std::snprintf(lbl, sizeof lbl, "%s %s", idText, distText);
                    const auto ts = ImGui::CalcTextSize(lbl);
                    const ImVec2 lp{ex - ux * 28 - ts.x * .5f, ey - uy * 28 - ts.y * .5f};
                    d->AddRectFilled({lp.x - ui::px(5), lp.y - ui::px(2)}, {lp.x + ts.x + ui::px(5), lp.y + ts.y + ui::px(2)}, ui::u32(ui::kCanvas, 0.78f), ui::px(3));
                    ui::shadowText(d, lp, white, lbl);
                    continue;
                }
                const float r = ui::px(5);
                const ImVec2 c{x, y - ui::px(4)};
                d->AddQuadFilled({c.x, c.y - r - 1}, {c.x + r + 1, c.y}, {c.x, c.y + r + 1}, {c.x - r - 1, c.y}, IM_COL32(0, 0, 0, 160));
                d->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}, accent);
                const auto who = str(m, "name");
                const auto is = ImGui::CalcTextSize(idText), ns = ImGui::CalcTextSize(who.c_str()), ds = ImGui::CalcTextSize(distText);
                const float padX = ui::px(7), padY = ui::px(3);
                const float chipW = is.x + ui::px(8) + ns.x + ui::px(8) + ds.x + padX * 2;
                const float chipH = is.y + padY * 2;
                const float left = x - chipW * .5f, top = c.y - r - ui::px(8) - chipH;
                d->AddRectFilled({left, top}, {left + chipW, top + chipH}, ui::u32(ui::kCanvas, 0.78f), ui::px(4));
                d->AddRect({left, top}, {left + chipW, top + chipH}, ui::u32(ui::kAccent, 0.35f), ui::px(4));
                d->AddRectFilled({left + padX - 2, top + padY - 1}, {left + padX + is.x + 4, top + padY + is.y + 1}, accent, ui::px(2));
                d->AddText({left + padX + 1, top + padY}, ink, idText);
                ui::shadowText(d, {left + padX + is.x + ui::px(8), top + padY}, white, who.c_str());
                ui::shadowText(d, {left + padX + is.x + ui::px(8) + ns.x + ui::px(6), top + padY}, accent, distText);
            }
            ui::popNote();
        }
        if (online() && showTeamHud) drawTeamHud(size, distances);
        if (online() && showAtbHud) drawSharedAtbHud(size);
        if (showDamageHud) drawDamageHud(size);
        if (online()) drawChat(size); else chatting = false;

        // Floating toast notification
        if (!toast.empty() && now() - toastAt < 4000 && !open) {
            ui::pushBold();
            const auto ts = ImGui::CalcTextSize(toast.c_str());
            const float alphaF = (std::clamp)(float(4000 - (now() - toastAt)) / 600.f, 0.f, 1.f);
            const ImVec2 p{size.x * .5f - ts.x * .5f - ui::px(18), size.y - ui::px(140)};
            const ImVec2 pMax{p.x + ts.x + ui::px(36), p.y + ts.y + ui::px(20)};
            const ui::Tone tTone = toastError ? ui::Tone::Danger : ui::Tone::Accent;
            d->AddRectFilled(p, pMax, ui::u32(ui::kCanvas, 0.92f * alphaF), ui::px(7));
            d->AddRect(p, pMax, ui::u32(tTone, 0.55f * alphaF), ui::px(7));
            d->AddRectFilled(p, {p.x + ui::px(3), pMax.y}, ui::u32(tTone, alphaF), ui::px(2));
            d->AddText({p.x + ui::px(18), p.y + ui::px(10)}, ui::u32(tTone, alphaF), toast.c_str());
            ui::popBold();
        }

        drawDuelInvite(size);

        // Host map change floating banner
        const auto change = state.value("game", json::object()).value("world_replication", json::object()).value("host_map_change", json::object());
        if (!open && role() == "client" && change.value("pending", false)) {
            ui::pushBold();
            const std::string text = "房主已前往 " + str(change, "level") + "，按 " + menuKeyName(key) + " 选择是否跟随";
            const auto ts = ImGui::CalcTextSize(text.c_str());
            const ImVec2 p{size.x * .5f - ts.x * .5f - ui::px(18), ui::px(82)};
            const ImVec2 pMax{p.x + ts.x + ui::px(36), p.y + ts.y + ui::px(20)};
            d->AddRectFilled(p, pMax, ui::u32(ui::kCanvas, 0.92f), ui::px(7));
            d->AddRect(p, pMax, ui::u32(ui::kAccent, 0.78f), ui::px(7), 0, ui::px(1.5f));
            d->AddText({p.x + ui::px(18), p.y + ui::px(10)}, ui::u32(ui::kAccent), text.c_str());
            ui::popBold();
        }
    }

    // ---- Enhanced Right-Side Teammate Status HUD (HP + Ultimate SP + Op + Dist)
    void drawTeamHud(ImVec2 size, const std::map<int, float>& distances) {
        const auto peers = state.value("peers", json::array());
        if (peers.empty()) return;
        const auto& vitalsMap = liveVitals();
        auto* d = ImGui::GetBackgroundDrawList();
        const float op = hudOpacity;
        const bool compact = hudCompact;
        const float panelW = compact ? ui::px(238) : ui::px(278);
        const float x = size.x - panelW - ui::px(18);
        float y = size.y * 0.25f;

        ui::pushNote();
        const float lh = ImGui::GetTextLineHeight();
        const float rowH = compact ? (lh + ui::px(14)) : (lh * 2.f + ui::px(24));
        const float headerH = lh + ui::px(16);
        const float totalH = headerH + float(peers.size()) * (rowH + ui::px(6)) + ui::px(8);

        // Frosted panel backdrop
        const ImVec2 pMin{x, y}, pMax{x + panelW, y + totalH};
        d->AddRectFilled(pMin, pMax, ui::u32(ui::kCanvas, op * 0.90f), ui::px(8));
        d->AddRect(pMin, pMax, ui::u32(ui::kLine, op * 0.95f), ui::px(8));
        d->AddRectFilledMultiColor({pMin.x + ui::px(6), pMin.y}, {pMax.x - ui::px(6), pMin.y + ui::px(2)},
            ui::u32(ui::kAccent, op), ui::u32(ui::kInfo, op * 0.6f), ui::u32(ui::kInfo, op * 0.6f), ui::u32(ui::kAccent, op));

        // Header line
        auto roomName = str(state.value("room", json::object()), "name");
        if (roomName.empty()) roomName = role() == "host" ? "我的房间" : "房间";
        const std::string titleStr = ui::ellipsize("EF LINK // " + roomName, panelW - ui::px(82));
        d->AddRectFilled({x + ui::px(10), y + ui::px(10)}, {x + ui::px(13), y + ui::px(10) + lh - ui::px(2)}, ui::u32(ui::kAccent, op), ui::px(1));
        ui::shadowText(d, {x + ui::px(18), y + ui::px(9)}, ui::u32(ui::kAccent, op), titleStr.c_str());

        const bool tcpOnly = state.value("traffic", json::object()).value("tcp_only_links", 0u) > 0;
        char netBuf[32]{};
        if (tcpOnly) std::snprintf(netBuf, sizeof netBuf, "TCP");
        else if (role() == "client") std::snprintf(netBuf, sizeof netBuf, "%ums", state.value("rtt_ms", 0u));
        else std::snprintf(netBuf, sizeof netBuf, "%dP", int(peers.size()));
        const auto netSz = ImGui::CalcTextSize(netBuf);
        ui::shadowText(d, {x + panelW - netSz.x - ui::px(10), y + ui::px(9)},
            ui::u32(tcpOnly ? ui::kWarning : ui::kInfo, op), netBuf);

        y += headerH;
        d->AddLine({x + ui::px(10), y - ui::px(3)}, {x + panelW - ui::px(10), y - ui::px(3)}, ui::u32(ui::kLine, op * 0.85f));

        // Member rows
        for (const auto& peer : peers) {
            const int id = peer.value("id", 0);
            const bool self = peer.value("local", false);
            const bool isHost = peer.value("host", false);
            const bool valid = peer.value("valid", false);
            bool dueling = false;
            for (const auto& duel : state.value("pvp", json::object()).value("duels", json::array())) {
                if (duel.value("id", -1) == id) dueling = true;
            }
            const std::string idKey = std::to_string(id);
            json v = vitalsMap.contains(idKey) ? vitalsMap[idKey] : peer.value("vitals", json::object());
            const double hp = v.value("hp", 0.0), maxHp = v.value("max_hp", 0.0);
            const double usp = v.value("usp", 0.0), maxUsp = v.value("max_usp", 0.0);
            std::string opName = str(v, "op");
            if (opName.empty()) opName = operatorDisplayName(str(peer, "model"));
            const bool alive = v.value("alive", true);
            const float hpRatio = maxHp > 0.0 ? float((std::clamp)(hp / maxHp, 0.0, 1.0)) : (valid ? 1.f : 0.f);
            const float spRatio = maxUsp > 0.0 ? float((std::clamp)(usp / maxUsp, 0.0, 1.0)) : 0.f;

            const ImVec2 rMin{x + ui::px(8), y};
            const ImVec2 rMax{x + panelW - ui::px(8), y + rowH};
            d->AddRectFilled(rMin, rMax, ui::u32(self ? ui::kCardHover : ui::kCard, op * 0.82f), ui::px(5));
            d->AddRect(rMin, rMax, ui::u32(dueling ? ui::kDanger : (self ? ui::kAccent : ui::kLine), op * (dueling ? 0.65f : 0.45f)), ui::px(5));

            // ID tile
            char idBuf[8]{}; std::snprintf(idBuf, sizeof idBuf, "%02d", id);
            const auto idSz = ImGui::CalcTextSize(idBuf);
            const float rx = rMin.x + ui::px(7), ry = rMin.y + ui::px(5);
            const ImVec4 tileCol = !valid ? ui::kTextMuted : (self ? ui::kInfo : (isHost ? ui::kAccent : ui::kText));
            d->AddRectFilled({rx, ry}, {rx + idSz.x + ui::px(6), ry + lh}, ui::u32(tileCol, op), ui::px(3));
            d->AddText({rx + ui::px(3), ry}, ui::u32(ui::kAccentInk, op), idBuf);

            // Right distance / self badge
            char rightBuf[24]{};
            if (self) std::snprintf(rightBuf, sizeof rightBuf, "你");
            else if (auto it = distances.find(id); it != distances.end()) std::snprintf(rightBuf, sizeof rightBuf, "%.0fm", it->second);
            else if (peer.contains("distance")) std::snprintf(rightBuf, sizeof rightBuf, "%.0fm", peer.value("distance", 0.f));
            else if (!peer.value("same_map", true)) std::snprintf(rightBuf, sizeof rightBuf, "异图");
            else std::snprintf(rightBuf, sizeof rightBuf, "—");
            const auto rightSz = ImGui::CalcTextSize(rightBuf);
            ui::shadowText(d, {rMax.x - rightSz.x - ui::px(7), ry}, ui::u32(self ? ui::kInfo : ui::kAccent, op), rightBuf);

            // Player name + operator tag
            std::string display = str(peer, "name", "玩家");
            if (isHost) display += "·房主";
            if (dueling) display += " [PVP]";
            const float maxNameW = (rMax.x - rightSz.x - ui::px(14)) - (rx + idSz.x + ui::px(12));
            ui::shadowText(d, {rx + idSz.x + ui::px(11), ry}, ui::u32(alive ? ui::kText : ui::kDanger, op),
                ui::ellipsize(display, maxNameW).c_str());

            // HP + Ultimate SP bars
            const ImVec4 hpCol = !alive ? ui::kDanger : (hpRatio > 0.55f ? ui::kSuccess : (hpRatio > 0.25f ? ui::kWarning : ui::kDanger));
            const bool spFull = maxUsp > 0.0 && spRatio >= 0.995f;
            const ImVec4 spCol = spFull ? ui::kAccent : ui::kInfo;

            if (compact) {
                const float by = ry + lh + ui::px(3);
                const float barW = (rMax.x - rMin.x) - ui::px(14);
                ui::drawProgressBar(d, {rx, by}, {rx + barW * 0.68f, by + ui::px(4)}, hpRatio, ui::u32(hpCol, op), ui::u32(ui::kWell, op * 0.9f));
                ui::drawProgressBar(d, {rx + barW * 0.71f, by}, {rx + barW, by + ui::px(4)}, spRatio, ui::u32(spCol, op), ui::u32(ui::kWell, op * 0.9f));
            } else {
                const float subY = ry + lh + ui::px(3);
                if (!opName.empty()) {
                    d->AddText({rx, subY}, ui::u32(ui::kTextDim, op), opName.c_str());
                }
                char statTxt[48]{};
                if (maxHp > 0.0) {
                    if (spFull) std::snprintf(statTxt, sizeof statTxt, "HP %.0f%% · 终结技 MAX", hpRatio * 100.f);
                    else if (maxUsp > 0.0) std::snprintf(statTxt, sizeof statTxt, "HP %.0f%% · 充能 %.0f%%", hpRatio * 100.f, spRatio * 100.f);
                    else std::snprintf(statTxt, sizeof statTxt, "HP %.0f%%", hpRatio * 100.f);
                } else {
                    std::snprintf(statTxt, sizeof statTxt, "%s", valid ? "状态同步中" : "等待角色");
                }
                const auto stSz = ImGui::CalcTextSize(statTxt);
                d->AddText({rMax.x - stSz.x - ui::px(7), subY}, ui::u32(spFull ? ui::kAccent : ui::kTextDim, op), statTxt);

                const float barY = subY + lh + ui::px(3);
                const float barW = (rMax.x - rMin.x) - ui::px(14);
                ui::drawProgressBar(d, {rx, barY}, {rx + barW, barY + ui::px(4.5f)}, hpRatio, ui::u32(hpCol, op), ui::u32(ui::kWell, op * 0.95f));
                ui::drawProgressBar(d, {rx, barY + ui::px(6.5f)}, {rx + barW, barY + ui::px(9.5f)}, spRatio, ui::u32(spCol, op), ui::u32(ui::kWell, op * 0.95f));
            }
            y += rowH + ui::px(6);
        }
        ui::popNote();
    }

    // ---- Shared ATB Pool Sync Bar & Recent Spend/Gain Event Feed -----------
    void drawSharedAtbHud(ImVec2 size) {
        const auto& atb = liveSharedAtb();
        if (!atb.value("active", false)) return;
        auto* d = ImGui::GetBackgroundDrawList();
        const float op = hudOpacity;
        const float val = float(atb.value("value", 0.0));
        const float mx  = (std::max)(1.f, float(atb.value("max", 300.0)));
        const float rec = float(atb.value("recover", 0.0));
        const int segs  = (std::clamp)(int(std::round(mx / 100.f)), 1, 6);

        ui::pushNote();
        const float lh = ImGui::GetTextLineHeight();
        const float w = hudCompact ? ui::px(260) : ui::px(320);
        const float h = lh + ui::px(22);
        const ImVec2 p0{size.x * 0.5f - w * 0.5f, ui::px(14)};
        const ImVec2 p1{p0.x + w, p0.y + h};

        d->AddRectFilled(p0, p1, ui::u32(ui::kCanvas, op * 0.90f), ui::px(7));
        d->AddRect(p0, p1, ui::u32(ui::kAccent, op * 0.42f), ui::px(7));
        d->AddRectFilled({p0.x, p0.y + ui::px(5)}, {p0.x + ui::px(3), p1.y - ui::px(5)}, ui::u32(ui::kAccent, op), ui::px(1.5f));

        ui::shadowText(d, {p0.x + ui::px(11), p0.y + ui::px(5)}, ui::u32(ui::kAccent, op), "共享技力池");
        char numBuf[64]{};
        if (rec > 0.05f) std::snprintf(numBuf, sizeof numBuf, "%.0f / %.0f  (+%.1f/s)", val, mx, rec);
        else std::snprintf(numBuf, sizeof numBuf, "%.0f / %.0f", val, mx);
        const auto nSz = ImGui::CalcTextSize(numBuf);
        ui::shadowText(d, {p1.x - nSz.x - ui::px(10), p0.y + ui::px(5)}, ui::u32(ui::kText, op), numBuf);

        const float by = p0.y + lh + ui::px(9);
        ui::drawProgressBar(d, {p0.x + ui::px(11), by}, {p1.x - ui::px(11), by + ui::px(7)},
            val / mx, ui::u32(ui::kAccent, op), ui::u32(ui::kWell, op * 0.95f), segs);

        // Recent ATB spend/gain event pills beneath the bar
        const auto events = atb.value("events", json::array());
        float ey = p1.y + ui::px(5);
        int shown = 0;
        for (auto it = events.rbegin(); it != events.rend() && shown < (hudCompact ? 2 : 3); ++it) {
            const auto age = it->value("age_ms", std::uint64_t(9999));
            if (age > 4500) continue;
            const float fade = (std::clamp)(float(4500 - age) / 1000.f, 0.f, 1.f) * op;
            const double delta = it->value("delta", 0.0);
            const bool gain = delta > 0.0;
            char evBuf[128]{};
            std::snprintf(evBuf, sizeof evBuf, "%s  %s%.0f 技力（%s）",
                str(*it, "name", "队友").c_str(), gain ? "+" : "", delta, gain ? "回复" : "战技消耗");
            const auto es = ImGui::CalcTextSize(evBuf);
            const float ew = es.x + ui::px(16), eh = es.y + ui::px(6);
            const ImVec2 e0{size.x * 0.5f - ew * 0.5f, ey};
            const ui::Tone t = gain ? ui::Tone::Success : ui::Tone::Warning;
            d->AddRectFilled(e0, {e0.x + ew, e0.y + eh}, ui::u32(ui::kCanvas, fade * 0.88f), eh * 0.5f);
            d->AddRect(e0, {e0.x + ew, e0.y + eh}, ui::u32(t, fade * 0.55f), eh * 0.5f);
            d->AddText({e0.x + ui::px(8), e0.y + ui::px(3)}, ui::u32(t, fade), evBuf);
            ey += eh + ui::px(4);
            ++shown;
        }
        ui::popNote();
    }

    // ---- Floating Real-Time Damage Meter HUD (Draggable!) ------------------
    void drawDamageHud(ImVec2 size) {
        const auto& stats = liveCombatStats();
        const auto players = stats.value("players", json::array());
        if (!online() && players.empty()) return;

        const float op = hudOpacity;
        const bool compact = hudCompact;
        const float width = compact ? ui::px(262) : ui::px(332);
        const ImVec2 defaultPos{ui::px(20), size.y * 0.25f};
        ImGui::SetNextWindowPos(defaultPos, resetHudPosRequested ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        resetHudPosRequested = false;
        ImGui::SetNextWindowSize({width, 0.f});
        ImGui::SetNextWindowBgAlpha(op * 0.90f);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::alpha(ui::kCanvas, op * 0.92f));
        ImGui::PushStyleColor(ImGuiCol_Border, ui::alpha(ui::kLineStrong, op * 0.85f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(11), ui::px(9)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::px(8));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

        if (ImGui::Begin("##eflink_dmg_hud", nullptr, flags)) {
            const ImVec2 wPos = ImGui::GetWindowPos();
            const ImVec2 wSz  = ImGui::GetWindowSize();
            hudHitRects.push_back({wPos, {wPos.x + wSz.x, wPos.y + wSz.y}});

            ImDrawList* d = ImGui::GetWindowDrawList();
            const bool inCombat = stats.value("in_combat", false);
            d->AddRectFilledMultiColor({wPos.x + ui::px(6), wPos.y}, {wPos.x + wSz.x - ui::px(6), wPos.y + ui::px(2)},
                ui::u32(inCombat ? ui::kAccent : ui::kInfo, op), ui::u32(ui::kInfo, op * 0.4f),
                ui::u32(ui::kInfo, op * 0.4f), ui::u32(inCombat ? ui::kAccent : ui::kInfo, op));

            // Header bar
            ui::pushBold();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kAccent);
            ImGui::TextUnformatted("伤害统计");
            ImGui::PopStyleColor();
            ui::popBold();

            ui::pushNote();
            ImGui::SameLine();
            char metaBuf[48]{};
            std::snprintf(metaBuf, sizeof metaBuf, "%.1fs · %s/s",
                stats.value("duration_s", 0.0), formatDamageNumber(stats.value("team_dps", 0.0)).c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, inCombat ? ui::kText : ui::kTextDim);
            ImGui::TextUnformatted(metaBuf);
            ImGui::PopStyleColor();

            const float resetW = ImGui::CalcTextSize("重置").x + ui::px(14);
            ui::alignRight(resetW);
            if (ui::chipButton("重置##hudrst", ui::Tone::Neutral)) {
                act({{"action", "combat_stats_reset"}});
                say("伤害统计已重置", false);
            }

            ui::vspace(ui::px(4));
            if (players.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextMuted);
                ImGui::TextUnformatted("等待战斗伤害记录…");
                ImGui::PopStyleColor();
            } else {
                const double topDmg = (std::max)(1.0, players[0].value("damage", 1.0));
                const int maxRows = compact ? 4 : 8;
                int idx = 0;
                const float barH = compact ? ui::px(22) : ui::px(26);
                for (const auto& p : players) {
                    if (idx >= maxRows) break;
                    const double dmg = p.value("damage", 0.0);
                    const double dps = p.value("dps", 0.0);
                    const double share = p.value("share", 0.0);
                    const float ratio = float((std::clamp)(dmg / topDmg, 0.0, 1.0));
                    const ImVec4 barCol = ui::rankColor(idx);

                    const ImVec2 o = ImGui::GetCursorScreenPos();
                    const float availW = ImGui::GetContentRegionAvail().x;
                    const ImVec2 bMax{o.x + availW, o.y + barH};
                    d->AddRectFilled(o, bMax, ui::u32(ui::kWell, op * 0.85f), ui::px(4));
                    if (ratio > 0.005f) {
                        d->AddRectFilled(o, {o.x + availW * ratio, bMax.y}, ui::u32(barCol, op * 0.28f), ui::px(4));
                        d->AddRectFilled(o, {o.x + ui::px(3), bMax.y}, ui::u32(barCol, op * 0.95f), ui::px(2));
                    }

                    char rightStr[64]{};
                    if (compact) std::snprintf(rightStr, sizeof rightStr, "%s (%.0f%%)", formatDamageNumber(dmg).c_str(), share * 100.0);
                    else std::snprintf(rightStr, sizeof rightStr, "%s · %s/s (%.0f%%)", formatDamageNumber(dmg).c_str(), formatDamageNumber(dps).c_str(), share * 100.0);
                    const auto rSz = ImGui::CalcTextSize(rightStr);
                    const float ty = o.y + (barH - ImGui::GetTextLineHeight()) * 0.5f;

                    std::string leftStr = "#" + std::to_string(idx + 1) + " " + str(p, "name", "玩家");
                    const std::string opTag = str(p, "op");
                    if (!compact && !opTag.empty() && opTag != str(p, "name")) leftStr += " [" + opTag + "]";
                    const float maxLeft = (std::max)(ui::px(40), availW - rSz.x - ui::px(18));
                    ui::shadowText(d, {o.x + ui::px(8), ty}, ui::u32(ui::kText, op), ui::ellipsize(leftStr, maxLeft).c_str());
                    ui::shadowText(d, {bMax.x - rSz.x - ui::px(6), ty}, ui::u32(idx == 0 ? ui::kAccent : ui::kText, op), rightStr);

                    ImGui::Dummy({availW, barH});
                    ui::vspace(ui::px(3));
                    ++idx;
                }
                if (!compact) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextMuted);
                    ImGui::Text("总伤 %s · 按 %s 重置 · Alt+拖动",
                        formatDamageNumber(stats.value("total_damage", 0.0)).c_str(), fKeyName(damageResetKey));
                    ImGui::PopStyleColor();
                }
            }
            ui::popNote();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    // ---- Top PVP Duel Invitation Card --------------------------------------
    void drawDuelInvite(ImVec2 size) {
        if (open || !online()) return;
        const auto invites = state.value("pvp", json::object()).value("invites", json::array());
        if (invites.empty()) return;
        const auto& invite = invites.back();
        const int from = invite.value("id", 0);
        const float age = float(invite.value("age_ms", std::uint64_t(0)));
        const float slide = (std::clamp)(age / 250.f, 0.f, 1.f);
        const float ease = 1.f - (1.f - slide) * (1.f - slide);
        const float left = (std::clamp)(30000.f - age, 0.f, 30000.f) / 1000.f;
        const float width = (std::min)(ui::px(460), size.x - ui::px(40));

        ImGui::SetNextWindowPos({size.x * .5f - width * .5f, -80.f + ease * (80.f + size.y * .11f)});
        ImGui::SetNextWindowSize({width, 0});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::alpha(ui::kCanvas, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border, ui::alpha(ui::kAccent, 0.85f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, ui::px(1.5f));
        if (ImGui::Begin("##duelinvite", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing)) {
            const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
            hudHitRects.push_back({wp, {wp.x + ws.x, wp.y + ws.y}});
            ui::badge("PVP 对战邀请", ui::Tone::Accent, true);
            ImGui::SameLine();
            ui::dim("剩余 %.0f 秒", left);
            ui::vspace(ui::px(6));
            ImGui::TextWrapped("%s 邀请你进行 PVP 对战。同意后双方可互相锁定与攻击，随时可结束。", str(invite, "name").c_str());
            ui::vspace(ui::px(6));
            const ImVec2 bo = ImGui::GetCursorScreenPos();
            const float bw = ImGui::GetContentRegionAvail().x;
            ui::drawProgressBar(ImGui::GetWindowDrawList(), bo, {bo.x + bw, bo.y + ui::px(3)}, left / 30.f, ui::u32(ui::kAccent), ui::u32(ui::kWell));
            ImGui::Dummy({bw, ui::px(6)});
            const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * .5f;
            const std::string accept = std::string("同意（") + fKeyName(pvpKey) + "）";
            if (ui::primaryButton(accept.c_str(), {btnW, ui::px(32)})) act({{"action", "pvp_duel"}, {"op", "accept"}, {"target", from}});
            ImGui::SameLine();
            if (ui::secondaryButton("拒绝", {btnW, ui::px(32)})) act({{"action", "pvp_duel"}, {"op", "decline"}, {"target", from}});
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    // ---- Bottom-Left In-Game Chat Overlay ----------------------------------
    void drawChat(ImVec2 size) {
        const auto lines = state.value("chat", json::array());
        const auto t = now();
        if (!chatPrimed) {
            for (const auto& l : lines) chatSeen[l.value("serial", std::uint64_t(0))] = 0;
            chatPrimed = true;
        }
        for (const auto& l : lines) {
            const auto s = l.value("serial", std::uint64_t(0));
            if (!chatSeen.contains(s)) chatSeen[s] = t;
        }
        std::vector<const json*> show;
        for (auto it = lines.rbegin(); it != lines.rend() && show.size() < (chatting ? 10u : 6u); ++it) {
            const auto seen = chatSeen[it->value("serial", std::uint64_t(0))];
            if (chatting || (seen && t - seen < 12000)) show.push_back(&*it);
        }
        ui::pushNote();
        auto* d = ImGui::GetBackgroundDrawList();
        const float lineH = ImGui::GetTextLineHeight() + ui::px(5);
        const float left = ui::px(22), width = (std::min)(ui::px(540), size.x * .40f), bottom = size.y * .78f;
        float y = bottom - lineH * float(show.size());
        if (!show.empty()) {
            const float bgA = chatting ? (hudOpacity * 0.86f) : (hudOpacity * 0.42f);
            d->AddRectFilled({left - ui::px(10), y - ui::px(6)}, {left + width, bottom + ui::px(4)}, ui::u32(ui::kCanvas, bgA), ui::px(6));
            if (chatting) d->AddRect({left - ui::px(10), y - ui::px(6)}, {left + width, bottom + ui::px(4)}, ui::u32(ui::kLine, hudOpacity), ui::px(6));
        }
        for (auto it = show.rbegin(); it != show.rend(); ++it, y += lineH) {
            const auto& l = **it;
            const auto seen = chatSeen[l.value("serial", std::uint64_t(0))];
            const float a = chatting ? 1.f : (std::clamp)(float(12000 - (t - seen)) / 2000.f, 0.f, 1.f);
            d->PushClipRect({left - ui::px(6), y - 2}, {left + width - ui::px(6), y + lineH}, true);
            if (l.value("system", false)) {
                ui::shadowText(d, {left, y}, ui::u32(ui::kTextDim, a), ("· " + str(l, "text")).c_str());
            } else {
                const auto who = str(l, "name") + "：";
                ui::shadowText(d, {left, y}, ui::u32(ui::kAccent, a), who.c_str());
                ui::shadowText(d, {left + ImGui::CalcTextSize(who.c_str()).x, y}, ui::u32(ui::kText, a), str(l, "text").c_str());
            }
            d->PopClipRect();
        }
        if (chatting) {
            ImGui::SetNextWindowPos({left - ui::px(10), bottom + ui::px(8)});
            ImGui::SetNextWindowSize({width + ui::px(10), 0});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::alpha(ui::kCanvas, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Border, ui::alpha(ui::kAccent, 0.65f));
            if (ImGui::Begin("##hudchat", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
                if (chatFocus) { ImGui::SetKeyboardFocusHere(); chatFocus = false; }
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputTextWithHint("##hudchatinput", "输入消息，Enter 发送，Esc 取消", hudChat, sizeof hudChat, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    if (*hudChat) act({{"action", "chat"}, {"text", hudChat}});
                    hudChat[0] = 0;
                    chatting = false;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { hudChat[0] = 0; chatting = false; }
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
        }
        ui::popNote();
    }

    // =========================================================================
    // MAIN MENU (ENDFIELD INDUSTRIAL-TACTICAL DESIGN)
    // =========================================================================
    void drawMenu(ImVec2 display) {
        const float maxW = (std::max)(400.f, display.x - ui::px(28));
        const float maxH = (std::max)(320.f, display.y - ui::px(28));
        const float minW = (std::min)(ui::px(900), maxW);
        const float minH = (std::min)(ui::px(560), maxH);
        const bool displayChanged = display.x != lastDisplay.x || display.y != lastDisplay.y;
        if (!menuLayoutReady || displayChanged) {
            if (!menuLayoutReady) {
                menuSize = {(std::min)(ui::px(1130), maxW), (std::min)(ui::px(744), maxH)};
                menuPos = {(display.x - menuSize.x) * 0.5f, (display.y - menuSize.y) * 0.5f};
                menuLayoutReady = true;
            }
            lastDisplay = display;
            menuSize.x = (std::clamp)(menuSize.x, minW, maxW);
            menuSize.y = (std::clamp)(menuSize.y, minH, maxH);
            menuPos.x = (std::clamp)(menuPos.x, 0.f, (std::max)(0.f, display.x - menuSize.x));
            menuPos.y = (std::clamp)(menuPos.y, 0.f, (std::max)(0.f, display.y - menuSize.y));
            ImGui::SetNextWindowPos(menuPos);
            ImGui::SetNextWindowSize(menuSize);
        } else if (dragActive && ImGui::IsMousePosValid()) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            menuPos.x = (std::clamp)(menuPos.x + delta.x, 0.f, (std::max)(0.f, display.x - menuSize.x));
            menuPos.y = (std::clamp)(menuPos.y + delta.y, 0.f, (std::max)(0.f, display.y - menuSize.y));
            ImGui::SetNextWindowPos(menuPos);
        }

        ImGui::SetNextWindowSizeConstraints({minW, minH}, {maxW, maxH});
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin("EF Link###eflink", nullptr, flags)) {
            menuPos = ImGui::GetWindowPos();
            menuSize = ImGui::GetWindowSize();
            dragActive = false;

            ui::drawWindowChrome(ImGui::GetWindowDrawList(), menuPos, {menuPos.x + menuSize.x, menuPos.y + menuSize.y});
            drawMenuHeader();

            const float footerH = ui::px(60);
            drawMenuSidebar(-footerH);
            ImGui::SameLine(0.f, ui::px(12));

            ImGui::BeginChild("##eflink_content", {0.f, -footerH}, ImGuiChildFlags_None);
            switch (page) {
            case 0: pageSession(); break;
            case 1: pagePlayers(); break;
            case 2: pageCombatStats(); break;
            case 3: pageChat(); break;
            case 4: pageSettings(); break;
            default: pageDiagnostics(); break;
            }
            ImGui::EndChild();

            drawMenuFooter();
            popups();
        }
        ImGui::End();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            closeRequested = true;
        }
    }

    void drawMenuHeader() {
        const auto r = role();
        const auto roomInfo = state.value("room", json::object());
        const auto pvpInfo = state.value("pvp", json::object());
        const auto& atbInfo = liveSharedAtb();

        constexpr const char* closeLbl = "返回游戏  ×";
        const float closeW = ui::buttonWidth(closeLbl) + ui::px(6);

        // Top row: draggable brand strip + live status pills + Close button
        ui::pushTitle();
        const float titleH = ImGui::GetTextLineHeight();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float dragW = (std::max)(ui::px(220), availW - closeW - ui::px(12));
        ImGui::InvisibleButton("##header_drag", {dragW, titleH + ui::px(4)});
        const bool dragHov = ImGui::IsItemHovered();
        dragActive = ImGui::IsItemActive();
        if (dragHov || dragActive) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        ImDrawList* d = ImGui::GetWindowDrawList();
        ui::drawEndfieldLogo(d, {origin.x, origin.y + ui::px(1)}, titleH);
        d->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {origin.x + ui::px(22), origin.y}, ui::u32(ui::kText), "EF LINK");
        const float brandEnd = origin.x + ui::px(22) + ImGui::CalcTextSize("EF LINK").x + ui::px(10);
        ui::popTitle();

        // Version badge & subtitle right on the title line
        ui::pushNote();
        const std::string verStr = std::string("v") + EFLINK_VERSION;
        const auto vs = ImGui::CalcTextSize(verStr.c_str());
        const ImVec2 v0{brandEnd, origin.y + (titleH - vs.y - ui::px(4)) * 0.5f};
        d->AddRectFilled(v0, {v0.x + vs.x + ui::px(10), v0.y + vs.y + ui::px(4)}, ui::u32(ui::kAccent, 0.14f), ui::px(4));
        d->AddRect(v0, {v0.x + vs.x + ui::px(10), v0.y + vs.y + ui::px(4)}, ui::u32(ui::kAccent, 0.42f), ui::px(4));
        d->AddText({v0.x + ui::px(5), v0.y + ui::px(2)}, ui::u32(ui::kAccent), verStr.c_str());

        const std::string sub = online()
            ? ("终末地联机  ·  " + str(roomInfo, "name", "未命名房间") + "  ·  " + str(state, "name", "管理员"))
            : ("终末地联机  ·  " + str(state, "name", "管理员") + "  ·  拖动标题栏移动");
        const float subX = v0.x + vs.x + ui::px(22);
        const float subMaxW = (origin.x + dragW) - subX - ui::px(12);
        if (subMaxW > ui::px(80)) {
            d->AddText({subX, origin.y + (titleH - vs.y) * 0.5f}, ui::u32(ui::kTextDim), ui::ellipsize(sub, subMaxW).c_str());
        }
        ui::popNote();

        ui::alignRight(closeW);
        if (ui::ghostButton(closeLbl, {closeW, ui::px(30)})) closeRequested = true;

        // Second strip: live telemetry pills
        ui::vspace(ui::px(6));
        ui::statusPill(roleLabel(r), roleTone(r), true);
        if (online()) {
            char pBuf[32]{};
            std::snprintf(pBuf, sizeof pBuf, "%d / %d 人", int(state.value("peers", json::array()).size()), roomInfo.value("max_players", 8));
            ui::hspace(ui::px(6)); ui::statusPill(pBuf, ui::Tone::Neutral, false);
            const bool tcpOnly = state.value("traffic", json::object()).value("tcp_only_links", 0u) > 0;
            if (tcpOnly) { ui::hspace(ui::px(6)); ui::statusPill("TCP 备用通道", ui::Tone::Warning, true); }
            else if (r == "client") {
                char rttBuf[24]{}; std::snprintf(rttBuf, sizeof rttBuf, "%u ms", state.value("rtt_ms", 0u));
                ui::hspace(ui::px(6)); ui::statusPill(rttBuf, ui::Tone::Info, false);
            }
            if (pvpInfo.value("active", false)) { ui::hspace(ui::px(6)); ui::statusPill("PVP 已开放", ui::Tone::Danger, true); }
            if (atbInfo.value("active", false)) { ui::hspace(ui::px(6)); ui::statusPill("共享技力池", ui::Tone::Accent, true); }
        } else {
            ui::hspace(ui::px(8));
            ui::pushNote();
            ui::muted("按 %s 开关菜单 · Esc 关闭 · 进房后按 Enter 快速聊天", menuKeyName(key).c_str());
            ui::popNote();
        }

        // Inline toast inside menu if active
        if (!toast.empty() && now() - toastAt < 4000) {
            const float tw = ui::pillWidth(toast.c_str(), false);
            ui::alignRight(tw);
            ui::statusPill(toast.c_str(), toastError ? ui::Tone::Danger : ui::Tone::Success, false);
        }

        ui::vspace(ui::px(8));
        ImGui::Separator();
        ui::vspace(ui::px(8));
    }

    void drawMenuSidebar(float height) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kSidebar);
        ImGui::PushStyleColor(ImGuiCol_Border, ui::kLine);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(8), ui::px(10)});
        ImGui::BeginChild("##eflink_sidebar", {ui::px(196), height}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);

        char roomBadge[16]{}, peerBadge[16]{}, chatBadge[16]{};
        const int roomCnt = int(state.value("rooms", json::array()).size());
        const int peerCnt = int(state.value("peers", json::array()).size());
        if (!online() && roomCnt > 0) std::snprintf(roomBadge, sizeof roomBadge, "%d", roomCnt);
        if (online() && peerCnt > 0) std::snprintf(peerBadge, sizeof peerBadge, "%d", peerCnt);
        if (unreadChat > 0) std::snprintf(chatBadge, sizeof chatBadge, "%llu", static_cast<unsigned long long>(unreadChat));

        const auto& cStats = liveCombatStats();
        const bool inCombat = cStats.value("in_combat", false);
        const char* combatBadge = inCombat ? "战斗" : (!cStats.value("players", json::array()).empty() ? "数据" : nullptr);

        const float rowH = ui::px(38);
        if (ui::navItem(ui::NavIcon::Lobby,       "联机大厅",   page == 0, *roomBadge ? roomBadge : nullptr, ui::Tone::Accent, rowH)) page = 0;
        ui::vspace(ui::px(3));
        if (ui::navItem(ui::NavIcon::Players,     "房间玩家",   page == 1, *peerBadge ? peerBadge : nullptr, ui::Tone::Info, rowH)) page = 1;
        ui::vspace(ui::px(3));
        if (ui::navItem(ui::NavIcon::Combat,      "战斗统计",   page == 2, combatBadge, inCombat ? ui::Tone::Accent : ui::Tone::Neutral, rowH)) page = 2;
        ui::vspace(ui::px(3));
        if (ui::navItem(ui::NavIcon::Chat,        "房间聊天",   page == 3, *chatBadge ? chatBadge : nullptr, ui::Tone::Warning, rowH)) {
            page = 3; unreadChat = 0; lastSeenChatSerial = state.value("chat_serial", std::uint64_t(0));
        }
        ui::vspace(ui::px(3));
        if (ui::navItem(ui::NavIcon::Settings,    "外观与设置", page == 4, nullptr, ui::Tone::Neutral, rowH)) page = 4;
        ui::vspace(ui::px(3));
        if (ui::navItem(ui::NavIcon::Diagnostics, "运行诊断",   page == 5, nullptr, ui::Tone::Neutral, rowH)) page = 5;

        // Bottom pinned compact hotkey legend (measured so it never triggers a scrollbar)
        const float legendH = ui::px(134);
        const float slack = ImGui::GetContentRegionAvail().y - legendH;
        if (slack > ui::px(8)) {
            ImGui::Dummy({0.f, slack});
            ImGui::Separator();
            ui::vspace(ui::px(6));
            ui::pushNote();
            ui::muted("快捷操作");
            ui::vspace(ui::px(2));
            ui::dim(" %s  开关菜单", menuKeyName(key).c_str());
            ui::dim(" %s  PVP 邀请", fKeyName(pvpKey));
            ui::dim(" %s  重置伤害统计", fKeyName(damageResetKey));
            ui::dim(" Enter  快捷聊天");
            ui::popNote();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    void drawMenuFooter() {
        ui::vspace(ui::px(6));
        ImGui::Separator();
        ui::vspace(ui::px(8));
        const auto r = role();
        if (online() || r == "connecting") {
            const char* lbl = r == "host" ? "关闭房间" : (r == "client" ? "离开房间" : "取消连接");
            if (ui::chipButton(lbl, ui::Tone::Danger)) leaveConfirm = true;
            ui::hspace(ui::px(10));
        }
        // Quick HUD visibility toggle chips right in footer
        if (ui::chipButton(showDamageHud ? "悬浮伤害: 开" : "悬浮伤害: 关", showDamageHud ? ui::Tone::Accent : ui::Tone::Neutral)) {
            showDamageHud = !showDamageHud; applySwitches();
        }
        ui::hspace(ui::px(6));
        if (ui::chipButton(showTeamHud ? "队友HUD: 开" : "队友HUD: 关", showTeamHud ? ui::Tone::Info : ui::Tone::Neutral)) {
            showTeamHud = !showTeamHud; applySwitches();
        }
        ui::hspace(ui::px(6));
        if (ui::chipButton(showAtbHud ? "技力提示: 开" : "技力提示: 关", showAtbHud ? ui::Tone::Success : ui::Tone::Neutral)) {
            showAtbHud = !showAtbHud; applySwitches();
        }

        char rightMeta[96]{};
        if (online()) std::snprintf(rightMeta, sizeof rightMeta, "延迟 %u ms  ·  EF Link %s", state.value("rtt_ms", 0u), EFLINK_VERSION);
        else std::snprintf(rightMeta, sizeof rightMeta, "就绪  ·  EF Link %s", EFLINK_VERSION);
        ui::pushNote();
        ui::alignRight(ImGui::CalcTextSize(rightMeta).x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui::px(4));
        ui::muted("%s", rightMeta);
        ui::popNote();
    }

    // ---- PAGE 0: 联机大厅 --------------------------------------------------
    void pageSession() {
        const auto r = role();
        if (!online() && r != "connecting") {
            const float half = (ImGui::GetContentRegionAvail().x - ui::px(12)) * 0.5f;

            // Left column: Host Room
            ImGui::BeginGroup();
            ui::cardBegin("##host_card", "开放房间", {half, 0.f}, ui::Tone::Accent);
            ui::fieldLabel("房间名称", "好友在列表里看到的名字");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##room", "例如：四号谷地一起刷本", room, sizeof room);
            ui::vspace(ui::px(6));
            ui::fieldLabel("房间密码", "可留空");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##hostpw", "留空为公开房间", hostPassword, sizeof hostPassword, ImGuiInputTextFlags_Password);
            ui::vspace(ui::px(6));
            ui::fieldLabel("人数上限与端口", "2–16 人 · 默认 47615");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.56f);
            ImGui::SliderInt("##limit", &limit, 2, 16, "%d 人");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt("##port", &port, 0);
            port = (std::clamp)(port, 1024, 65535);
            ui::vspace(ui::px(8));

            ui::toggleSwitch("host_shared_atb", &sharedAtb, "所有人共用技力条（ATB 共享池）",
                "全队共用房主技力条与自然回复，任意玩家释放战技或回复技力实时同步。");
            ui::toggleSwitch("host_pvp", &pvp, "允许 PVP 对战邀请",
                "房内玩家可互相邀请进行 PVP 切磋，不会误伤未对战队友。");
            if (pvp) {
                ImGui::Indent(ui::px(10));
                ui::fieldLabel("PVP 伤害倍率");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##pvpscale", &pvpScale, 0.1f, 5.0f, "%.1f 倍");
                int mode = pvpRevive ? 0 : 1;
                if (ImGui::RadioButton("击败后回满生命", mode == 0)) pvpRevive = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("真正倒下", mode == 1)) pvpRevive = false;
                ImGui::Unindent(ui::px(10));
                ui::vspace(ui::px(4));
            }
            ui::vspace(ui::px(6));
            if (ui::primaryButton("开放房间", {-1.f, ui::px(36)})) {
                saveSettings();
                act({{"action", "host"}, {"room_name", room}, {"password", hostPassword},
                     {"port", port}, {"max_players", limit}, {"pvp", pvp},
                     {"pvp_scale", pvpScale}, {"pvp_revive", pvpRevive}, {"shared_atb", sharedAtb}});
                hostPassword[0] = 0;
            }
            ui::cardEnd();
            ImGui::EndGroup();

            ImGui::SameLine(0.f, ui::px(12));

            // Right column: Direct Join + Saved Servers
            ImGui::BeginGroup();
            ui::cardBegin("##join_card", "直接连接好友", {half, 0.f}, ui::Tone::Info);
            ui::fieldLabel("房主地址", "例：192.168.1.20:47615");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##address", "输入 IP 或 IP:端口", address, sizeof address);
            ui::vspace(ui::px(6));
            ui::fieldLabel("房间密码", "无密码留空");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##joinpw", "房间密码（可留空）", joinPassword, sizeof joinPassword, ImGuiInputTextFlags_Password);
            ui::vspace(ui::px(6));
            ui::fieldLabel("收藏备注名", "可选，便于下次识别");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##srvname", "例如：好友的电脑", serverName, sizeof serverName);
            ui::vspace(ui::px(10));
            const float btnHalf = (ImGui::GetContentRegionAvail().x - ui::px(8)) * 0.5f;
            if (ui::primaryButton("连接房间", {btnHalf, ui::px(36)})) join();
            ImGui::SameLine(0.f, ui::px(8));
            if (ui::secondaryButton("收藏此地址", {-1.f, ui::px(36)})) {
                if (*address) {
                    act({{"action", "save_server"}, {"address", address}, {"name", *serverName ? serverName : address}});
                    say("已添加到收藏", false);
                } else say("请先输入地址", true);
            }
            ui::cardEnd();

            ui::vspace(ui::px(10));
            const auto saved = state.value("configuration", json::object()).value("saved_servers", json::array());
            ui::cardBegin("##saved_card", "收藏服务器", {half, 0.f}, ui::Tone::Neutral);
            if (saved.empty()) {
                ui::hint("暂无收藏地址。填入好友地址后点击“收藏此地址”即可一键填入。");
            } else {
                for (const auto& s : saved) {
                    const auto ep = str(s, "address");
                    const auto lb = str(s, "name", ep);
                    ImGui::PushID(ep.c_str());
                    ui::pushBold();
                    ImGui::TextUnformatted(ui::ellipsize(lb, half - ui::px(155)).c_str());
                    ui::popBold();
                    const float fillW = ui::px(58), remW = ui::px(48);
                    ui::alignRight(fillW + remW + ui::px(6));
                    if (ui::chipButton("填入", ui::Tone::Accent)) { copyText(address, sizeof address, ep); joinPassword[0] = 0; }
                    ImGui::SameLine(0.f, ui::px(6));
                    if (ui::chipButton("移除", ui::Tone::Danger)) act({{"action", "remove_server"}, {"address", ep}});
                    ui::pushMono();
                    ui::muted("%s", ep.c_str());
                    ui::popMono();
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
            ui::cardEnd();
            ImGui::EndGroup();

            ui::vspace(ui::px(12));

            // Bottom full-width LAN Discovery card
            ui::cardBegin("##lan_card", "局域网房间自动发现", {0.f, 0.f}, ui::Tone::Success);
            if (ui::secondaryButton("刷新列表", {ui::px(110), ui::px(30)})) act({{"action", "refresh"}});
            ImGui::SameLine();
            ui::dim(" 自动扫描同一局域网内开放的终末地联机房间");
            ui::vspace(ui::px(8));
            const auto rooms = state.value("rooms", json::array());
            if (rooms.empty()) {
                ui::emptyState("暂未发现局域网房间", "让好友先开放房间，或在上方直接输入 IP:端口 加入。");
            } else {
                for (const auto& item : rooms) {
                    const auto ep = str(item, "address");
                    const bool locked = item.value("password", false);
                    ImGui::PushID(ep.c_str());
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kWell);
                    ImGui::BeginChild("##lanrow", {0.f, 0.f}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
                    ui::pushBold();
                    ImGui::TextUnformatted(str(item, "name", "未命名房间").c_str());
                    ui::popBold();
                    if (locked) { ImGui::SameLine(); ui::badge("需密码", ui::Tone::Warning); }
                    const float joinW = ui::px(88);
                    ui::alignRight(joinW);
                    if (ui::primaryButton("加入房间", {joinW, ui::px(30)})) {
                        copyText(address, sizeof address, ep);
                        if (locked) say("该房间需要密码，请在上方输入密码后连接", false);
                        else join();
                    }
                    ui::pushNote();
                    ui::muted("房主 %s  ·  %d/%d 人  ·  地图 %s  ·  %s",
                        str(item, "host").c_str(), item.value("players", 0), item.value("max_players", 0),
                        str(item, "map", "—").c_str(), ep.c_str());
                    ui::popNote();
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ui::vspace(ui::px(6));
                    ImGui::PopID();
                }
            }
            ui::cardEnd();
            return;
        }

        // ---- ONLINE ROOM VIEW ----
        const auto roomInfo = state.value("room", json::object());
        const auto pvpState = state.value("pvp", json::object());
        const auto& atbState = liveSharedAtb();

        // Top Overview + Live Shared ATB & World Sync
        ui::cardBegin("##room_overview", r == "host" ? "我的房间（房主控制）" : (r == "client" ? "已加入房间" : "正在连接房间"), {0.f, 0.f}, roleTone(r));
        ui::pushTitle();
        ImGui::TextUnformatted(str(roomInfo, "name", r == "connecting" ? "连接中…" : "未命名房间").c_str());
        ui::popTitle();
        ui::vspace(ui::px(4));
        ui::statusPill(roleLabel(r), roleTone(r), true);
        char pCnt[32]{}; std::snprintf(pCnt, sizeof pCnt, "%d / %d 人", int(state.value("peers", json::array()).size()), roomInfo.value("max_players", 8));
        ui::hspace(ui::px(6)); ui::statusPill(pCnt, ui::Tone::Neutral, false);
        if (pvpState.value("active", false)) {
            char pvpRule[64]{}; std::snprintf(pvpRule, sizeof pvpRule, "PVP %.1fx · %s", pvpState.value("scale", 1.0), pvpState.value("revive", true) ? "回满" : "倒下");
            ui::hspace(ui::px(6)); ui::statusPill(pvpRule, ui::Tone::Danger, true);
        }
        if (atbState.value("active", false)) {
            ui::hspace(ui::px(6)); ui::statusPill("共用技力池已启用", ui::Tone::Accent, true);
        }

        // Live Shared ATB gauge bar inside room card when enabled
        if (atbState.value("active", false)) {
            ui::vspace(ui::px(10));
            const float val = float(atbState.value("value", 0.0));
            const float mx = (std::max)(1.f, float(atbState.value("max", 300.0)));
            const float rec = float(atbState.value("recover", 0.0));
            const int segs = (std::clamp)(int(std::round(mx / 100.f)), 1, 6);
            ui::dim("共享技力池实时状态：%.0f / %.0f  （自然回复 +%.1f/s）", val, mx, rec);
            const ImVec2 bo = ImGui::GetCursorScreenPos();
            const float bw = ImGui::GetContentRegionAvail().x;
            ui::drawProgressBar(ImGui::GetWindowDrawList(), bo, {bo.x + bw, bo.y + ui::px(10)},
                val / mx, ui::u32(ui::kAccent), ui::u32(ui::kWell), segs);
            ImGui::Dummy({bw, ui::px(12)});
        }

        if (r == "host") {
            ui::vspace(ui::px(8));
            ImGui::Separator();
            ui::vspace(ui::px(8));
            const float half = (ImGui::GetContentRegionAvail().x - ui::px(14)) * 0.5f;

            // Left: Room Gameplay Rules (Shared ATB + PVP)
            ImGui::BeginGroup();
            ImGui::BeginChild("##host_rules", {half, 0.f}, ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
            bool roomAtb = atbState.value("allowed", false);
            if (ui::toggleSwitch("live_shared_atb", &roomAtb, "所有人共用技力条（ATB）",
                "房主权威共享池：任何队友消耗或回复技力实时同步给所有人。")) {
                sharedAtb = roomAtb;
                act({{"action", "room_shared_atb"}, {"enabled", roomAtb}});
            }
            bool roomPvp = pvpState.value("allowed", false);
            if (ui::toggleSwitch("live_pvp", &roomPvp, "允许 PVP 切磋邀请",
                "开启后按 PVP 键向附近玩家发出邀请，对方同意即可对战。")) {
                pvp = roomPvp;
                act({{"action", "room_pvp"}, {"enabled", roomPvp}});
            }
            if (roomPvp) {
                ImGui::Indent(ui::px(8));
                ui::fieldLabel("PVP 伤害倍率");
                ImGui::SetNextItemWidth(-ui::px(8));
                if (!ImGui::IsAnyItemActive()) pvpScale = float(pvpState.value("scale", 1.0));
                ImGui::SliderFloat("##roompvpscale", &pvpScale, 0.1f, 5.0f, "%.1f 倍");
                if (ImGui::IsItemDeactivatedAfterEdit()) act({{"action", "room_pvp"}, {"enabled", roomPvp}, {"scale", pvpScale}});
                const int mode = pvpState.value("revive", true) ? 0 : 1;
                if (ImGui::RadioButton("回满生命（3秒无敌后回满）", mode == 0)) act({{"action", "room_pvp"}, {"enabled", roomPvp}, {"revive", true}});
                if (ImGui::RadioButton("真正倒下（按游戏规则倒下）", mode == 1)) act({{"action", "room_pvp"}, {"enabled", roomPvp}, {"revive", false}});
                ImGui::Unindent(ui::px(8));
            }
            ImGui::EndChild();
            ImGui::EndGroup();

            ImGui::SameLine(0.f, ui::px(14));

            // Right: Shareable Host IP Addresses
            ImGui::BeginGroup();
            ImGui::BeginChild("##host_addrs", {half, 0.f}, ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
            ui::fieldLabel("房间地址", "发给好友直接连接");
            const auto addrs = state.value("addresses", json::array());
            if (addrs.empty()) ui::hint("正在读取本机网卡地址…");
            for (const auto& a : addrs) {
                const auto ep = str(a, "ip") + ":" + std::to_string(roomInfo.value("port", 47615));
                ImGui::PushID(ep.c_str());
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kWell);
                ImGui::BeginChild("##addrrow", {0.f, 0.f}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
                ui::pushMono();
                ImGui::TextUnformatted(ep.c_str());
                ui::popMono();
                ui::alignRight(ui::px(60));
                if (ui::chipButton("复制", ui::Tone::Accent)) { ImGui::SetClipboardText(ep.c_str()); say("地址已复制：" + ep, false); }
                ui::pushNote();
                ui::muted("%s", str(a, "adapter").c_str());
                ui::popNote();
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ui::vspace(ui::px(5));
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndGroup();
        }
        ui::cardEnd();

        ui::vspace(ui::px(12));
        drawWorldSyncCard();
    }

    void drawWorldSyncCard() {
        const auto world = state.value("world_session", json::object());
        const auto entry = world.value("entry", json::object());
        const auto native = state.value("game", json::object()).value("world_replication", json::object());
        const bool active = entry.value("active", false);

        ui::cardBegin("##sync_card", "世界同步与传送", {0.f, 0.f}, active ? ui::Tone::Success : ui::Tone::Warning);
        ui::statusPill(active ? "已进入共享世界" : "正在准备共享世界", active ? ui::Tone::Success : ui::Tone::Warning, true);
        ui::hspace(ui::px(8));
        ui::statusPill(("已同步敌人 " + std::to_string(native.value("matched", 0u))).c_str(), ui::Tone::Info, false);
        if (native.value("missing", 0u) > 0) {
            ui::hspace(ui::px(8));
            ui::statusPill(("待补全 " + std::to_string(native.value("missing", 0u))).c_str(), ui::Tone::Danger, false);
        }
        const auto blockers = entry.value("blockers", json::array());
        if (!blockers.empty()) {
            ui::vspace(ui::px(8));
            for (const auto& b : blockers) {
                if (b.is_string()) { ui::badge(blockerText(b.get<std::string>()), ui::Tone::Warning); ui::hspace(ui::px(6)); }
            }
            ImGui::NewLine();
        }
        const auto travel = str(native, "teleport");
        if (travel == "failed") {
            ui::vspace(ui::px(6));
            ui::noticeBanner(ui::Tone::Danger, ("传送失败：" + str(native, "error")).c_str());
        } else if (!travel.empty() && travel != "idle" && travel != "complete") {
            ui::vspace(ui::px(6));
            ui::dim("传送进度：%s", travel.c_str());
        }
        const auto change = native.value("host_map_change", json::object());
        if (role() == "client" && change.value("pending", false)) {
            ui::vspace(ui::px(10));
            ui::noticeBanner(ui::Tone::Accent, ("房主已切换地图至：" + str(change, "level")).c_str());
            ui::vspace(ui::px(8));
            const bool supported = std::find(blockers.begin(), blockers.end(), json("host_scene_rules")) == blockers.end();
            const auto token = change.value("token", std::uint64_t(0));
            ImGui::BeginDisabled(!supported);
            if (ui::primaryButton("跟随房主", {ui::px(130), ui::px(32)})) act({{"action", "host_map_choice"}, {"token", token}, {"follow", true}});
            ImGui::EndDisabled();
            if (!change.value("deferred", false)) {
                ui::hspace(ui::px(8));
                if (ui::secondaryButton("暂不跟随", {ui::px(110), ui::px(32)})) act({{"action", "host_map_choice"}, {"token", token}, {"follow", false}});
            }
            if (!supported) { ui::vspace(ui::px(4)); ui::hint("房主当前场景尚未开放联机入场，请等待其回到大世界。"); }
        }
        ui::cardEnd();
    }

    // ---- PAGE 1: 房间玩家 --------------------------------------------------
    void pagePlayers() {
        const auto peers = state.value("peers", json::array());
        if (peers.empty()) {
            ui::emptyState("房间内暂无玩家", "开放或加入房间后，这里会实时展示队友干员、血量、终结技充能、距离与快捷操作。");
            return;
        }
        const auto& vitalsMap = liveVitals();
        const auto pvpInfo = state.value("pvp", json::object());
        const bool isHost = role() == "host";
        const bool pvpAllowed = pvpInfo.value("active", false);

        ui::sectionHeader("房间成员与实时状态", (std::to_string(peers.size()) + " 位在线").c_str(), ui::Tone::Info);
        for (const auto& p : peers) {
            const int id = p.value("id", 0);
            const bool self = p.value("local", false);
            const bool peerHost = p.value("host", false);
            const bool valid = p.value("valid", false);
            const bool stale = p.value("state_age_ms", 4000) > 3000;
            bool dueling = false;
            for (const auto& duel : pvpInfo.value("duels", json::array())) if (duel.value("id", -1) == id) dueling = true;

            const std::string idKey = std::to_string(id);
            const json v = vitalsMap.contains(idKey) ? vitalsMap[idKey] : p.value("vitals", json::object());
            const double hp = v.value("hp", 0.0), maxHp = v.value("max_hp", 0.0);
            const double usp = v.value("usp", 0.0), maxUsp = v.value("max_usp", 0.0);
            std::string opName = str(v, "op");
            if (opName.empty()) opName = operatorDisplayName(str(p, "model"));
            const float hpRatio = maxHp > 0.0 ? float((std::clamp)(hp / maxHp, 0.0, 1.0)) : 0.f;
            const float spRatio = maxUsp > 0.0 ? float((std::clamp)(usp / maxUsp, 0.0, 1.0)) : 0.f;

            ImGui::PushID(id);
            ui::cardBegin("##peer_card", nullptr, {0.f, 0.f});

            // Top row: ID tile + Name + Badges + Action buttons
            char idBuf[8]{}; std::snprintf(idBuf, sizeof idBuf, "#%02d", id);
            ui::badge(idBuf, self ? ui::Tone::Info : (peerHost ? ui::Tone::Accent : ui::Tone::Neutral), true);
            ImGui::SameLine();
            ui::pushBold();
            ImGui::TextUnformatted(str(p, "name", "未命名玩家").c_str());
            ui::popBold();
            if (peerHost) { ImGui::SameLine(); ui::badge("房主", ui::Tone::Accent); }
            if (self) { ImGui::SameLine(); ui::badge("你", ui::Tone::Info); }
            if (!opName.empty()) { ImGui::SameLine(); ui::badge(opName.c_str(), ui::Tone::Violet); }
            if (dueling) { ImGui::SameLine(); ui::badge("PVP 对战中", ui::Tone::Danger, true); }

            if (!self && online()) {
                const float tpW = ui::buttonWidth("传送到TA");
                const float pvpW = pvpAllowed ? (ui::buttonWidth(dueling ? "结束PVP" : "邀请PVP") + ui::px(6)) : 0.f;
                const float kickW = isHost ? (ui::buttonWidth("移出") + ui::px(6)) : 0.f;
                ui::alignRight(tpW + pvpW + kickW);
                ImGui::BeginDisabled(!valid || stale);
                if (ui::secondaryButton("传送到TA", {tpW, ui::px(28)})) act({{"action", "teleport_to_player"}, {"id", id}});
                ImGui::EndDisabled();
                if (pvpAllowed) {
                    ImGui::SameLine(0.f, ui::px(6));
                    if (dueling) {
                        if (ui::dangerButton("结束PVP", {0.f, ui::px(28)})) act({{"action", "pvp_duel"}, {"op", "end"}, {"target", id}});
                    } else {
                        if (ui::secondaryButton("邀请PVP", {0.f, ui::px(28)})) act({{"action", "pvp_duel"}, {"op", "invite"}, {"target", id}});
                    }
                }
                if (isHost) {
                    ImGui::SameLine(0.f, ui::px(6));
                    if (ui::dangerButton("移出", {0.f, ui::px(28)})) { kickId = id; kickName = str(p, "name", "玩家"); kickPending = true; }
                }
            }

            // Middle row: HP & Ultimate SP live bars
            ui::vspace(ui::px(6));
            const float halfBar = (ImGui::GetContentRegionAvail().x - ui::px(16)) * 0.5f;
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 barStart = ImGui::GetCursorScreenPos();
            ui::pushNote();
            char hpLabel[64]{}, spLabel[64]{};
            if (maxHp > 0.0) std::snprintf(hpLabel, sizeof hpLabel, "生命  %.0f / %.0f (%.0f%%)", hp, maxHp, hpRatio * 100.f);
            else std::snprintf(hpLabel, sizeof hpLabel, "生命  等待同步");
            if (maxUsp > 0.0) std::snprintf(spLabel, sizeof spLabel, "终结技能量  %.0f / %.0f%s", usp, maxUsp, spRatio >= 0.995f ? "  [就绪]" : "");
            else std::snprintf(spLabel, sizeof spLabel, "终结技能量  —");

            const ImVec4 hpCol = hpRatio > 0.55f ? ui::kSuccess : (hpRatio > 0.25f ? ui::kWarning : ui::kDanger);
            const ImVec4 spCol = spRatio >= 0.995f ? ui::kAccent : ui::kInfo;
            d->AddText(barStart, ui::u32(ui::kTextDim), hpLabel);
            d->AddText({barStart.x + halfBar + ui::px(16), barStart.y}, ui::u32(spRatio >= 0.995f ? ui::kAccent : ui::kTextDim), spLabel);
            const float by = barStart.y + ImGui::GetTextLineHeight() + ui::px(4);
            ui::drawProgressBar(d, {barStart.x, by}, {barStart.x + halfBar, by + ui::px(6)}, hpRatio, ui::u32(hpCol), ui::u32(ui::kWell));
            ui::drawProgressBar(d, {barStart.x + halfBar + ui::px(16), by}, {barStart.x + halfBar * 2.f + ui::px(16), by + ui::px(6)}, spRatio, ui::u32(spCol), ui::u32(ui::kWell));
            ImGui::Dummy({ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() + ui::px(12)});

            // Bottom metadata line
            const char* syncTxt = self ? "本机" : ((!valid || stale) ? "等待状态" : (p.value("same_map", false) ? "同地图" : "其他地图"));
            ui::muted("地图 %s  ·  %s  ·  坐标 %s",
                str(p, "level", str(p, "map", "—")).c_str(), syncTxt,
                valid ? formatPosition(p.value("position", json::array())).c_str() : "—");
            if (!self) {
                ImGui::SameLine();
                if (p.contains("distance")) ui::muted("  ·  距离 %.1f m  ·  延迟 %u ms", p.value("distance", 0.f), p.value("rtt_ms", 0u));
                else ui::muted("  ·  延迟 %u ms", p.value("rtt_ms", 0u));
            }
            ui::popNote();
            ui::cardEnd();
            ui::vspace(ui::px(8));
            ImGui::PopID();
        }
    }

    // ---- PAGE 2: 战斗统计 (DAMAGE METER & ANALYTICS) -----------------------
    void pageCombatStats() {
        const auto& stats = liveCombatStats();
        const bool inCombat = stats.value("in_combat", false);
        const auto players = stats.value("players", json::array());

        // Toolbar row
        ui::statusPill(inCombat ? "战斗统计进行中" : (players.empty() ? "待机" : "战斗已结算（DPS 已冻结）"),
            inCombat ? ui::Tone::Accent : (players.empty() ? ui::Tone::Neutral : ui::Tone::Info), true);

        const float shareW = ui::buttonWidth("发送到聊天");
        const float rstW = ui::buttonWidth("重置统计 (F9)");
        ui::alignRight(shareW + rstW + ui::px(10));
        ImGui::BeginDisabled(!online() || players.empty());
        if (ui::secondaryButton("发送到聊天", {shareW, ui::px(30)})) {
            act({{"action", "combat_stats_share"}});
            say("已将伤害统计发送到房间聊天", false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.f, ui::px(10));
        char rstLabel[32]{}; std::snprintf(rstLabel, sizeof rstLabel, "重置统计 (%s)", fKeyName(damageResetKey));
        if (ui::dangerButton(rstLabel, {rstW, ui::px(30)})) {
            act({{"action", "combat_stats_reset"}});
            say("伤害统计已重置", false);
        }

        ui::vspace(ui::px(10));

        // 4 Hero KPI Cards
        const float gap = ui::px(10);
        const float cardW = (ImGui::GetContentRegionAvail().x - gap * 3.f) * 0.25f;
        char durBuf[32]{}, hkBuf[48]{}, hitSub[48]{};
        std::snprintf(durBuf, sizeof durBuf, "%.1f s", stats.value("duration_s", 0.0));
        std::snprintf(hkBuf, sizeof hkBuf, "%llu", static_cast<unsigned long long>(stats.value("total_hits", std::uint64_t(0))));
        std::snprintf(hitSub, sizeof hitSub, "击杀 %llu 名敌人", static_cast<unsigned long long>(stats.value("total_kills", std::uint64_t(0))));

        ui::kpiCard("##kpi_total", "团队总伤害", formatDamageNumber(stats.value("total_damage", 0.0)).c_str(),
            (std::to_string(players.size()) + " 位参战玩家").c_str(), ui::Tone::Accent, cardW);
        ImGui::SameLine(0.f, gap);
        ui::kpiCard("##kpi_dps", "团队平均 DPS", (formatDamageNumber(stats.value("team_dps", 0.0)) + "/s").c_str(),
            inCombat ? "实时采样中" : "已停表保留峰值", ui::Tone::Info, cardW);
        ImGui::SameLine(0.f, gap);
        ui::kpiCard("##kpi_dur", "有效战斗时长", durBuf,
            damageAutoReset ? "脱战15秒自动新开" : "手动重置分段", ui::Tone::Success, cardW);
        ImGui::SameLine(0.f, gap);
        ui::kpiCard("##kpi_hits", "累计命中次数", hkBuf, hitSub, ui::Tone::Violet, cardW);

        ui::vspace(ui::px(10));

        // 30s Real-Time Team DPS Sparkline
        ui::cardBegin("##dps_graph", "最近 30 秒团队秒伤曲线", {0.f, 0.f}, ui::Tone::Info);
        std::vector<float> curve;
        for (const auto& v : stats.value("timeline", json::array())) {
            curve.push_back(v.is_number() ? v.get<float>() : 0.f);
        }
        if (curve.empty()) curve.assign(30, 0.f);
        ui::dpsSparkline(curve, {ImGui::GetContentRegionAvail().x, ui::px(76)}, inCombat ? ui::Tone::Accent : ui::Tone::Info);
        ui::cardEnd();

        ui::vspace(ui::px(10));

        // Ranked Player Breakdown
        ui::cardBegin("##dmg_rank", "全队伤害贡献排行", {0.f, 0.f}, ui::Tone::Accent);
        if (players.empty()) {
            ui::emptyState("暂无战斗伤害数据", "攻击敌人后将自动统计房主与全队成员的总伤、DPS、暴击率、最高单次伤害及元素占比。");
        } else {
            const double topDmg = (std::max)(1.0, players[0].value("damage", 1.0));
            int idx = 0;
            for (const auto& p : players) {
                const double dmg = p.value("damage", 0.0);
                const double dps = p.value("dps", 0.0);
                const double share = p.value("share", 0.0);
                const double critRate = p.value("crit_rate", 0.0);
                const double maxHit = p.value("max_hit", 0.0);
                const double taken = p.value("taken", 0.0);
                const auto hits = p.value("hits", std::uint64_t(0));
                const auto kills = p.value("kills", std::uint64_t(0));
                const float ratio = float((std::clamp)(dmg / topDmg, 0.0, 1.0));
                const ImVec4 rCol = ui::rankColor(idx);

                ImGui::PushID(p.value("id", idx));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kWell);
                ImGui::BeginChild("##dmg_player", {0.f, 0.f}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);

                // Bar background behind header row
                ImDrawList* d = ImGui::GetWindowDrawList();
                const ImVec2 barMin = ImGui::GetCursorScreenPos();
                const float barW = ImGui::GetContentRegionAvail().x;
                const float barH = ui::px(30);
                d->AddRectFilled(barMin, {barMin.x + barW, barMin.y + barH}, ui::u32(ui::kCard, 0.85f), ui::px(5));
                if (ratio > 0.005f) {
                    d->AddRectFilled(barMin, {barMin.x + barW * ratio, barMin.y + barH}, ui::u32(rCol, 0.24f), ui::px(5));
                    d->AddRectFilled(barMin, {barMin.x + ui::px(3.5f), barMin.y + barH}, ui::u32(rCol, 0.95f), ui::px(2));
                }

                // Rank badge + Name + Operator
                char rankBuf[16]{}; std::snprintf(rankBuf, sizeof rankBuf, idx == 0 ? "MVP #1" : "#%d", idx + 1);
                ImGui::SetCursorScreenPos({barMin.x + ui::px(10), barMin.y + ui::px(5)});
                ui::badge(rankBuf, idx == 0 ? ui::Tone::Accent : (idx == 1 ? ui::Tone::Info : ui::Tone::Neutral), idx == 0);
                ImGui::SameLine();
                ui::pushBold();
                ImGui::TextUnformatted(str(p, "name", "玩家").c_str());
                ui::popBold();
                const std::string opTag = str(p, "op");
                if (!opTag.empty()) { ImGui::SameLine(); ui::badge(opTag.c_str(), ui::Tone::Violet); }

                char rightMetrics[96]{};
                std::snprintf(rightMetrics, sizeof rightMetrics, "%s  ·  %s/s  ·  %.1f%%",
                    formatDamageNumber(dmg).c_str(), formatDamageNumber(dps).c_str(), share * 100.0);
                ui::pushBold();
                const float rmW = ImGui::CalcTextSize(rightMetrics).x;
                ImGui::SetCursorScreenPos({barMin.x + barW - rmW - ui::px(10), barMin.y + (barH - ImGui::GetTextLineHeight()) * 0.5f});
                ImGui::PushStyleColor(ImGuiCol_Text, idx == 0 ? ui::kAccent : ui::kText);
                ImGui::TextUnformatted(rightMetrics);
                ImGui::PopStyleColor();
                ui::popBold();

                ImGui::SetCursorScreenPos({barMin.x, barMin.y + barH + ui::px(6)});
                ui::pushNote();
                ui::dim("命中 %llu  ·  暴击率 %.1f%%  ·  最高单次 %s  ·  击杀 %llu  ·  承伤 %s",
                    static_cast<unsigned long long>(hits), critRate * 100.0,
                    formatDamageNumber(maxHit).c_str(), static_cast<unsigned long long>(kills),
                    formatDamageNumber(taken).c_str());

                // Element damage breakdown pills
                const auto byType = p.value("by_type", json::object());
                if (byType.is_object() && !byType.empty() && dmg > 0.0) {
                    ui::vspace(ui::px(4));
                    std::vector<std::pair<std::string, double>> elems;
                    for (auto it = byType.begin(); it != byType.end(); ++it) {
                        if (it.value().is_number() && it.value().get<double>() > 0.0) elems.emplace_back(it.key(), it.value().get<double>());
                    }
                    std::sort(elems.begin(), elems.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                    bool first = true;
                    for (const auto& [ek, ev] : elems) {
                        if (!first) ImGui::SameLine(0.f, ui::px(6));
                        first = false;
                        char elBuf[48]{};
                        std::snprintf(elBuf, sizeof elBuf, "%s %.0f%%", damageTypeLabel(ek), (ev / dmg) * 100.0);
                        const ImVec4 ec = ui::damageElementColor(ek);
                        const ImVec2 eo = ImGui::GetCursorScreenPos();
                        const ImVec2 es = ImGui::CalcTextSize(elBuf);
                        const ImVec2 em{eo.x + es.x + ui::px(12), eo.y + es.y + ui::px(4)};
                        d->AddRectFilled(eo, em, ui::u32(ec, 0.14f), ui::px(4));
                        d->AddRect(eo, em, ui::u32(ec, 0.40f), ui::px(4));
                        d->AddText({eo.x + ui::px(6), eo.y + ui::px(2)}, ui::u32(ec), elBuf);
                        ImGui::Dummy({em.x - eo.x, em.y - eo.y});
                    }
                }
                ui::popNote();
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ui::vspace(ui::px(7));
                ImGui::PopID();
                ++idx;
            }
        }
        ui::cardEnd();
    }

    // ---- PAGE 3: 房间聊天 --------------------------------------------------
    void pageChat() {
        ui::sectionHeader("房间聊天", online() ? "Enter 快捷发送" : "离线不可发送", ui::Tone::Accent);
        const float bottomReserve = ui::px(98);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kWell);
        ImGui::PushStyleColor(ImGuiCol_Border, ui::kLine);
        ImGui::BeginChild("##messages", {0.f, -bottomReserve}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        const auto lines = state.value("chat", json::array());
        if (lines.empty()) {
            ui::emptyState("还没有聊天记录", "加入房间后可以和队友沟通，或在游戏内直接按 Enter 打字。");
        }
        std::uint64_t newest = 0;
        for (const auto& line : lines) {
            newest = (std::max)(newest, line.value("serial", std::uint64_t(0)));
            if (line.value("system", false)) {
                ui::pushNote();
                ui::muted("·  %s", str(line, "text").c_str());
                ui::popNote();
                ui::vspace(ui::px(2));
                continue;
            }
            ui::pushBold();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kAccent);
            ImGui::TextUnformatted((str(line, "name", "玩家") + "：").c_str());
            ImGui::PopStyleColor();
            ui::popBold();
            ImGui::SameLine(0.f, ui::px(2));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextWrapped("%s", str(line, "text").c_str());
            ImGui::PopTextWrapPos();
            ui::vspace(ui::px(3));
        }
        if (newest != lastChat) { ImGui::SetScrollHereY(1.0f); lastChat = newest; }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        // Quick tactical phrases
        ui::vspace(ui::px(6));
        ImGui::BeginDisabled(!online());
        static const char* phrases[] = {"准备开怪", "等一下技力", "终结技已就绪", "传送过来集合"};
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine(0.f, ui::px(6));
            if (ui::chipButton(phrases[i], ui::Tone::Neutral)) act({{"action", "chat"}, {"text", phrases[i]}});
        }
        ImGui::SameLine(0.f, ui::px(6));
        if (ui::chipButton("播报伤害统计", ui::Tone::Accent)) act({{"action", "combat_stats_share"}});

        ui::vspace(ui::px(6));
        ImGui::SetNextItemWidth(-ui::px(98));
        bool send = ImGui::InputTextWithHint("##chat", "输入消息，按 Enter 发送…", chat, sizeof chat, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(0.f, ui::px(8));
        send |= ui::primaryButton("发送", {-1.f, ui::px(32)});
        if (send && *chat) {
            act({{"action", "chat"}, {"text", chat}});
            chat[0] = 0;
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::EndDisabled();
    }

    // ---- PAGE 4: 外观与设置 ------------------------------------------------
    void pageSettings() {
        const float half = (ImGui::GetContentRegionAvail().x - ui::px(12)) * 0.5f;

        // Top 2-column row: Identity/Keys + HUD Appearance
        ImGui::BeginGroup();
        ui::cardBegin("##set_player", "玩家身份与快捷键", {half, 0.f}, ui::Tone::Accent);
        ui::fieldLabel("显示名称", "默认读取游戏昵称");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##name", name, sizeof name)) nameEdited = true;
        ui::vspace(ui::px(8));

        static const char* keys[] = {"=","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
        int menuIdx = key == VK_OEM_PLUS ? 0 : (std::clamp)(key - VK_F1 + 1, 1, 12);
        ui::fieldLabel("联机菜单快捷键");
        ImGui::SetNextItemWidth(ui::px(150));
        if (ImGui::Combo("##key", &menuIdx, keys, 13)) key = menuIdx == 0 ? VK_OEM_PLUS : VK_F1 + menuIdx - 1;
        ui::vspace(ui::px(6));

        int pvpIdx = (std::clamp)(pvpKey - VK_F1, 0, 11);
        ui::fieldLabel("PVP 邀请 / 同意快捷键");
        ImGui::SetNextItemWidth(ui::px(150));
        if (ImGui::Combo("##pvpkey", &pvpIdx, keys + 1, 12)) pvpKey = VK_F1 + pvpIdx;
        ui::vspace(ui::px(6));

        int rstIdx = (std::clamp)(damageResetKey - VK_F1, 0, 11);
        ui::fieldLabel("重置伤害统计快捷键");
        ImGui::SetNextItemWidth(ui::px(150));
        if (ImGui::Combo("##dmgkey", &rstIdx, keys + 1, 12)) damageResetKey = VK_F1 + rstIdx;

        ui::vspace(ui::px(10));
        if (ui::primaryButton("保存名称与快捷键", {-1.f, ui::px(34)})) saveSettings();
        ui::cardEnd();
        ImGui::EndGroup();

        ImGui::SameLine(0.f, ui::px(12));

        ImGui::BeginGroup();
        ui::cardBegin("##set_hud", "悬浮窗与 HUD 外观", {half, 0.f}, ui::Tone::Info);
        bool changed = false;
        changed |= ui::toggleSwitch("sw_markers", &markers, "显示玩家头顶标记与边缘箭头");
        changed |= ui::toggleSwitch("sw_teamhud", &showTeamHud, "显示右侧队友状态 HUD", "展示队友血量条、终结技能量、当前干员与距离。");
        changed |= ui::toggleSwitch("sw_dmghud", &showDamageHud, "显示实时悬浮伤害小窗", "按住 Alt 可拖动窗口位置或点击重置。");
        changed |= ui::toggleSwitch("sw_atbhud", &showAtbHud, "显示共享技力条同步提示", "开启共用技力池时在顶部展示同步条与消耗/回复播报。");
        changed |= ui::toggleSwitch("sw_compact", &hudCompact, "HUD 紧凑模式", "缩小悬浮窗与队友栏尺寸，减少画面遮挡。");
        changed |= ui::toggleSwitch("sw_autorst", &damageAutoReset, "脱战 15 秒后自动开启新一轮伤害统计");
        ui::fieldLabel("HUD 背景透明度");
        ImGui::SetNextItemWidth(-1);
        int opPct = int(std::round(hudOpacity * 100.f));
        if (ImGui::SliderInt("##hudop", &opPct, 30, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            hudOpacity = float(opPct) / 100.f;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        ui::vspace(ui::px(4));
        if (ui::secondaryButton("重置悬浮伤害小窗位置", {-1.f, ui::px(30)})) {
            resetHudPosRequested = true;
            say("已重置悬浮窗位置", false);
        }
        ui::cardEnd();
        ImGui::EndGroup();

        ui::vspace(ui::px(12));

        // Bottom 2-column row: Coop Sync Switches + Emergency/Testing Tools
        ImGui::BeginGroup();
        ui::cardBegin("##set_coop", "联机画面与战斗同步（即时生效）", {half, 0.f}, ui::Tone::Success);
        changed |= ui::toggleSwitch("sw_remote", &remote, "显示其他玩家的角色");
        ImGui::BeginDisabled(!remote);
        changed |= ui::toggleSwitch("sw_squad", &squad, "显示其他玩家队伍中的其余三名干员");
        changed |= ui::toggleSwitch("sw_replay", &replay, "重放其他玩家的技能动作、武器与特效");
        changed |= ui::toggleSwitch("sw_outline", &outline, "给队友角色添加高亮描边");
        ImGui::EndDisabled();
        changed |= ui::toggleSwitch("sw_follow", &follow, "作为房客时跟随房主地图");
        changed |= ui::toggleSwitch("sw_combat", &combat, "同步战斗（共同攻击房主世界敌人）");
        ui::cardEnd();
        ImGui::EndGroup();

        ImGui::SameLine(0.f, ui::px(12));

        ImGui::BeginGroup();
        ui::cardBegin("##set_tools", "应急脱离与测试工具", {half, 0.f}, ui::Tone::Warning);
        if (ui::secondaryButton("应急回到四号谷地", {-1.f, ui::px(34)})) act({{"action", "emergency_home"}});
        ui::vspace(ui::px(4));
        ui::hint("先离开当前房间，再通过游戏官方传送回到四号谷地枢纽。");
        const auto teleport = state.value("game", json::object()).value("world_replication", json::object()).value("official_teleport", json::object());
        if (teleport.contains("error")) {
            ui::vspace(ui::px(6));
            ui::noticeBanner(ui::Tone::Danger, ("官方传送失败：" + str(teleport, "error")).c_str());
        }
        ui::vspace(ui::px(10));
        ImGui::Separator();
        ui::vspace(ui::px(8));
        bool invincible = state.value("testing_invincible", false);
        if (ui::toggleSwitch("sw_invincible", &invincible, "测试：本机角色无敌",
            "会保存并在重启后保持。作废本机干员受到的伤害，仅供联机测试。")) {
            act({{"action", "testing_invincible"}, {"enabled", invincible}});
        }
        ui::cardEnd();
        ImGui::EndGroup();

        if (changed) applySwitches();
    }

    // ---- PAGE 5: 运行诊断 --------------------------------------------------
    void pageDiagnostics() {
        const auto g = state.value("game", json::object());
        const auto native = g.value("world_replication", json::object());
        const auto world = state.value("world_session", json::object());
        const auto entry = world.value("entry", json::object());
        const auto combatState = native.value("combat", json::object());
        const auto squadState = combatState.value("squad", json::object());
        const auto& atbState = liveSharedAtb();
        const auto traffic = state.value("traffic", json::object());
        auto num = [](const json& j, const char* k) { return std::to_string(j.value(k, std::uint64_t(0))); };

        const float half = (ImGui::GetContentRegionAvail().x - ui::px(12)) * 0.5f;

        ImGui::BeginGroup();
        ui::cardBegin("##diag_game", "游戏接入与钩子状态", {half, 0.f}, ui::Tone::Accent);
        if (ImGui::BeginTable("##dg", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("k", 0, 1.0f); ImGui::TableSetupColumn("v", 0, 1.8f);
            ui::kvRow("游戏接入", state.value("hook_ready", false) ? "已连接" : "等待场景", state.value("hook_ready", false) ? ui::Tone::Success : ui::Tone::Warning);
            ui::kvRow("地图 / 关卡", str(g, "map_id", "—") + " / " + str(g, "level_id", "—"));
            ui::kvRow("本机角色", g.value("local_valid", false) ? ("已读取（骨骼 " + std::to_string(g.value("local_bones", 0)) + "）") : "未读取", g.value("local_valid", false) ? ui::Tone::Success : ui::Tone::Warning);
            ui::kvRow("共享世界", str(entry, "phase", "offline"), entry.value("active", false) ? ui::Tone::Success : ui::Tone::Warning);
            ui::kvRow("房客隔离", native.value("guest_policy", json::object()).value("ready", false) ? "就绪" : str(native.value("guest_policy", json::object()), "error", "—"));
            ui::kvRow("战斗过滤", state.value("op_filter_ready", false) ? "就绪" : str(state, "op_filter_note", "—"), state.value("op_filter_ready", false) ? ui::Tone::Success : ui::Tone::Warning);
            const bool atbHooks = state.value("atb_cost_hook_ready", false) && state.value("atb_set_hook_ready", false);
            ui::kvRow("技力条钩子", atbHooks ? "就绪 (Cost/Set/Update/Reset)" : "等待游戏初始化", atbHooks ? ui::Tone::Success : ui::Tone::Warning);
            ui::kvRow("版本", "EF Link " EFLINK_VERSION " · Windows x64");
            ImGui::EndTable();
        }
        ui::cardEnd();
        ImGui::EndGroup();

        ImGui::SameLine(0.f, ui::px(12));

        ImGui::BeginGroup();
        ui::cardBegin("##diag_sync", "战斗、技力与网络同步", {half, 0.f}, ui::Tone::Info);
        if (ImGui::BeginTable("##ds", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("k", 0, 1.0f); ImGui::TableSetupColumn("v", 0, 1.8f);
            ui::kvRow("敌人同步", num(native, "matched") + " 已同步 / " + num(native, "missing") + " 缺失");
            ui::kvRow("命中结算", num(combatState, "guest_hits_sent") + " 发出 / " + num(combatState, "room_hits_applied") + " 结算");
            ui::kvRow("技能重放", num(combatState, "player_casts_sent") + " 发 / " + num(combatState, "player_casts_mirrored") + " 播 / " + num(combatState, "player_casts_rejected") + " 拒");
            ui::kvRow("共享技力", (atbState.value("active", false) ? std::string("启用 · ") : std::string("关闭 · ")) +
                "同步 " + num(atbState, "syncs_sent") + "/" + num(atbState, "syncs_applied") + " · 增量 " + num(atbState, "deltas_sent") + "/" + num(atbState, "deltas_applied"));
            ui::kvRow("队伍成员", num(squadState, "poses_sent") + " 发出 / " + num(squadState, "poses_in") + " 收到 / " + std::to_string(squadState.value("remote", json::array()).size()) + " 远端");
            ui::kvRow("网络延迟", std::to_string(state.value("rtt_ms", 0u)) + " ms");
            if (traffic.value("tcp_only_links", 0u) > 0)
                ui::kvRow("UDP 状态", std::to_string(traffic.value("tcp_only_links", 0u)) + " 条走 TCP 备用", ui::Tone::Warning);
            else
                ui::kvRow("UDP 状态", traffic.value("udp_links", 0u) > 0 ? "已连通" : "—", traffic.value("udp_links", 0u) > 0 ? ui::Tone::Success : ui::Tone::Neutral);
            const auto gui = state.value("gui_render", json::object());
            ui::kvRow("界面后端", str(gui, "backend") + " · " + num(gui, "frames") + " 帧");
            ImGui::EndTable();
        }
        ui::cardEnd();
        ImGui::EndGroup();

        // Any active error banners
        bool hasErrors = false;
        for (const auto& e : {str(world, "error"), str(native, "error"), str(g, "error"), str(state, "last_error"),
                              str(combatState, "player_cast_error"), str(squadState, "error"), str(atbState, "error")}) {
            if (!e.empty()) {
                if (!hasErrors) { ui::vspace(ui::px(10)); hasErrors = true; }
                ui::noticeBanner(ui::Tone::Danger, e.c_str());
                ui::vspace(ui::px(5));
            }
        }
    }

    // ---- Modal Confirmation Popups -----------------------------------------
    void popups() {
        if (leaveConfirm) { ImGui::OpenPopup("##leave"); leaveConfirm = false; }
        if (kickPending)  { ImGui::OpenPopup("##kick");  kickPending = false; }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(22), ui::px(18)});
        if (ImGui::BeginPopupModal("##leave", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ui::pushTitle();
            ImGui::TextUnformatted(role() == "host" ? "关闭房间？" : "离开房间？");
            ui::popTitle();
            ui::vspace(ui::px(6));
            ui::dim("%s", role() == "host" ? "所有玩家都会断开连接并结束本次联机。" : "将断开连接并自动回到进入房间前的位置。");
            ui::vspace(ui::px(14));
            if (ui::dangerButton("确认", {ui::px(120), ui::px(34)})) { act({{"action", "leave"}}); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(0.f, ui::px(8));
            if (ui::secondaryButton("取消", {ui::px(120), ui::px(34)})) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("##kick", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ui::pushTitle();
            ImGui::Text("移出 %s (#%d)？", kickName.c_str(), kickId);
            ui::popTitle();
            ui::vspace(ui::px(14));
            if (ui::dangerButton("移出", {ui::px(120), ui::px(34)})) { act({{"action", "kick"}, {"id", kickId}}); kickId = -1; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(0.f, ui::px(8));
            if (ui::secondaryButton("取消", {ui::px(120), ui::px(34)})) { kickId = -1; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
    }
};

// ---- renderer: DirectComposition swap chain on a popup window --------------
struct Surface {
    HWND window = nullptr;
    bool interactive = false, shown = false, transparent = true;
    unsigned width = 0, height = 0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<IDCompositionDevice> dcomp;
    ComPtr<IDCompositionTarget> dtarget;
    ComPtr<IDCompositionVisual> visual;
    bool closeRequested = false;
    unsigned pendingWidth = 0, pendingHeight = 0;

    static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) {
        auto* self = reinterpret_cast<Surface*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);
            SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && self->transparent && !self->interactive) {
            if (m == WM_NCHITTEST) return HTTRANSPARENT;
            if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        }
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(w, m, a, b)) return 1;
        if (m == WM_CLOSE) { if (self) self->closeRequested = true; return 0; }
        if (m == WM_SIZE && self && a != SIZE_MINIMIZED) { self->pendingWidth = LOWORD(b); self->pendingHeight = HIWORD(b); }
        return DefWindowProcW(w, m, a, b);
    }

    bool create(const wchar_t* className, HWND owner, bool overlay, int x, int y, unsigned w, unsigned h) {
        transparent = overlay;
        width = (std::max)(1u, w);
        height = (std::max)(1u, h);
        const DWORD ex = overlay ? (WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) : WS_EX_NOREDIRECTIONBITMAP;
        window = CreateWindowExW(ex, className, L"EF Link", overlay ? WS_POPUP : WS_OVERLAPPEDWINDOW, x, y, int(width), int(height), owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!window) return false;
        if (overlay) SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
        D3D_FEATURE_LEVEL level;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context)) &&
            FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context))) return false;
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
        if (FAILED(device.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = width; d.Height = height; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2; d.Scaling = DXGI_SCALING_STRETCH;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(factory->CreateSwapChainForComposition(device.Get(), &d, nullptr, &swap))) return false;
        if (FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&dcomp))) || FAILED(dcomp->CreateTargetForHwnd(window, TRUE, &dtarget)) ||
            FAILED(dcomp->CreateVisual(&visual)) || FAILED(visual->SetContent(swap.Get())) || FAILED(dtarget->SetRoot(visual.Get())) || FAILED(dcomp->Commit())) return false;
        makeTarget();
        return target != nullptr;
    }

    void makeTarget() {
        target.Reset();
        ComPtr<ID3D11Texture2D> back;
        if (SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&back)))) device->CreateRenderTargetView(back.Get(), nullptr, &target);
    }

    void resize(unsigned w, unsigned h) {
        w = (std::max)(1u, w); h = (std::max)(1u, h);
        if (w == width && h == height) return;
        width = w; height = h;
        target.Reset(); context->ClearState(); context->Flush();
        swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        makeTarget();
    }

    void setInteractive(bool value) {
        if (interactive == value) return;
        interactive = value;
        if (transparent) {
            auto ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
            ex = value ? (ex & ~LONG_PTR(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) : (ex | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
            SetWindowLongPtrW(window, GWL_EXSTYLE, ex);
            SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

    void show(bool value) {
        if (shown == value) return;
        shown = value;
        ShowWindow(window, value ? SW_SHOWNA : SW_HIDE);
    }

    void render(const float clear[4], const std::filesystem::path* capturePath = nullptr) {
        auto* rtv = target.Get();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (capturePath && !capturePath->empty()) capture(*capturePath);
        swap->Present(1, 0);
    }

    void capture(const std::filesystem::path& path) {
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return;
        D3D11_TEXTURE2D_DESC d{}; back->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &staging))) return;
        context->CopyResource(staging.Get(), back.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return;
        std::vector<unsigned char> pixels(std::size_t(d.Width) * d.Height * 4);
        for (unsigned y = 0; y < d.Height; ++y) {
            auto* dst = pixels.data() + std::size_t(y) * d.Width * 4;
            std::memcpy(dst, static_cast<unsigned char*>(map.pData) + y * map.RowPitch, d.Width * 4);
            for (unsigned x = 0; x < d.Width; ++x) dst[x * 4 + 3] = 255;
        }
        context->Unmap(staging.Get(), 0);
        BITMAPFILEHEADER f{}; BITMAPINFOHEADER info{};
        f.bfType = 0x4D42; f.bfOffBits = sizeof(f) + sizeof(info); f.bfSize = f.bfOffBits + DWORD(pixels.size());
        info.biSize = sizeof(info); info.biWidth = LONG(d.Width); info.biHeight = -LONG(d.Height);
        info.biPlanes = 1; info.biBitCount = 32; info.biSizeImage = DWORD(pixels.size());
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<char*>(&f), sizeof(f));
        out.write(reinterpret_cast<char*>(&info), sizeof(info));
        out.write(reinterpret_cast<char*>(pixels.data()), std::streamsize(pixels.size()));
    }

    void destroy() {
        if (ImGui::GetCurrentContext()) { ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); }
        visual.Reset(); dtarget.Reset(); dcomp.Reset(); target.Reset(); swap.Reset(); context.Reset(); device.Reset();
        if (window) DestroyWindow(window);
        window = nullptr;
    }
};

} // namespace

// ================= Gui =================
Gui::Gui() = default;
Gui::~Gui() { stop(); }

std::future<json> Gui::post(const json& input) {
    auto call = std::make_shared<Call>();
    call->input = input;
    auto future = call->result.get_future();
    std::lock_guard lock(callsMutex);
    if (calls.size() >= 64) {
        std::promise<json> busy;
        busy.set_value({{"error", "界面请求过多"}});
        return busy.get_future();
    }
    calls.push_back(std::move(call));
    return future;
}

void Gui::tickGame(const BE_HostApiV1*) {
    gameThreadSeen = true;
    uiReady = overlayReady.load();
    std::deque<std::shared_ptr<Call>> pending;
    { std::lock_guard lock(callsMutex); pending.swap(calls); }
    for (auto& call : pending) {
        try { call->result.set_value(request ? request(call->input) : json{{"error", "模块未就绪"}}); }
        catch (const std::exception& e) { try { call->result.set_value({{"error", e.what()}}); } catch (...) {} }
        catch (...) { try { call->result.set_value({{"error", "请求失败"}}); } catch (...) {} }
        ++callsServed;
    }
}

void Gui::shutdownGame(const BE_HostApiV1*) { menuVisible = false; }

nlohmann::json Gui::diagnostics() const {
    std::string error;
    { std::lock_guard lock(errorMutex); error = lastError; }
    return {{"backend", "overlay_dcomp"}, {"overlay_ready", overlayReady.load()},
            {"frames", overlayFrames.load()}, {"calls_served", callsServed.load()}, {"error", error}};
}

void Gui::start(Request handler, const json& options) {
    stop();
    stopping = false;
    overlayFrames = 0;
    request = std::move(handler);
    if (options.value("preview_mode", false)) {
        thread = std::jthread([this, options] {
            try { runPreview(request, options); } catch (const std::exception& e) { setError(e.what()); }
        });
        return;
    }
    thread = std::jthread([this, options] {
        try { runOverlay(options); } catch (const std::exception& e) { setError(e.what()); }
        overlayReady = false;
    });
}

void Gui::stop() {
    stopping = true;
    if (thread.joinable()) thread.join();
    std::deque<std::shared_ptr<Call>> pending;
    { std::lock_guard lock(callsMutex); pending.swap(calls); }
    for (auto& call : pending) try { call->result.set_value({{"error", "界面已关闭"}}); } catch (...) {}
    menuVisible = false;
    uiReady = false;
    overlayReady = false;
}

void Gui::runOverlay(json) {
    const auto instance = GetModuleHandleW(nullptr);
    const std::wstring className = L"EFLink.Overlay." + std::to_wstring(GetCurrentProcessId());
    WNDCLASSEXW wc{sizeof(wc), 0, Surface::proc, 0, 0, instance, nullptr, LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, className.c_str(), nullptr};
    if (!RegisterClassExW(&wc)) throw std::runtime_error("界面窗口注册失败");
    struct Unregister { const wchar_t* name; HINSTANCE instance; ~Unregister() { UnregisterClassW(name, instance); } } unregister{className.c_str(), instance};

    while (!stopping) {
        HWND game = nullptr;
        while (!stopping && !(game = findGame())) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (stopping) break;
        RECT client{}; GetClientRect(game, &client);
        POINT origin{0, 0}; ClientToScreen(game, &origin);
        Surface surface;
        if (!surface.create(className.c_str(), game, true, origin.x, origin.y, unsigned(client.right), unsigned(client.bottom))) {
            surface.destroy();
            throw std::runtime_error("界面渲染器创建失败（DirectComposition）");
        }
        IMGUI_CHECKVERSION();
        auto* context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        const float displayScale = ui::scaleForHeight(unsigned(client.bottom));
        ui::applyTheme(displayScale);
        ui::fonts = ui::loadFonts(io.Fonts, displayScale);
        io.FontDefault = ui::fonts.body;

        ImGui_ImplWin32_Init(surface.window);
        ImGui_ImplDX11_Init(surface.device.Get(), surface.context.Get());

        Ui ui;
        ui.post = [this](const json& input) { return post(input); };
        overlayReady = true;
        bool keyDown = false, enterDown = false, pvpKeyDown = false, dmgResetDown = false;

        while (!stopping && IsWindow(game)) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            const bool minimized = IsIconic(game) || !IsWindowVisible(game);
            const HWND foreground = GetForegroundWindow();
            const bool focused = foreground == game || foreground == surface.window;

            const bool down = (GetAsyncKeyState(ui.key) & 0x8000) != 0;
            if (down && !keyDown && focused && !(ui.open && io.WantTextInput)) ui.open = !ui.open;
            keyDown = down;
            if (openRequested.exchange(false)) ui.open = true;
            if (ui.closeRequested) { ui.open = false; ui.closeRequested = false; }

            const bool pvpDown = (GetAsyncKeyState(ui.pvpKey) & 0x8000) != 0;
            if (pvpDown && !pvpKeyDown && foreground == game && !ui.open && !ui.chatting && ui.online()) ui.act({{"action", "pvp_key"}});
            pvpKeyDown = pvpDown;

            const bool rstDown = (GetAsyncKeyState(ui.damageResetKey) & 0x8000) != 0;
            if (rstDown && !dmgResetDown && focused && !(ui.open && io.WantTextInput) && !ui.chatting) {
                ui.act({{"action", "combat_stats_reset"}});
                ui.say("伤害统计已重置", false);
            }
            dmgResetDown = rstDown;

            const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
            if (enter && !enterDown && foreground == game && !ui.open && !ui.chatting && ui.online()) { ui.chatting = true; ui.chatFocus = true; }
            enterDown = enter;

            if (ui.open) ui.chatting = false;
            if (minimized || !focused) { ui.open = false; ui.chatting = false; }

            const bool wantInput = ui.open || ui.chatting;
            bool overHudWidget = false;
            if (!ui.hudHitRects.empty() && !wantInput) {
                CURSORINFO cursor{sizeof cursor}; POINT p{};
                if (GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) && GetCursorPos(&p)) {
                    ScreenToClient(game, &p);
                    for (const auto& r : ui.hudHitRects) {
                        if (p.x >= r.min.x && p.x <= r.max.x && p.y >= r.min.y && p.y <= r.max.y) { overHudWidget = true; break; }
                    }
                }
            }

            const bool wasInteractive = surface.interactive;
            surface.setInteractive(wantInput || overHudWidget);
            if (wantInput && !wasInteractive) SetForegroundWindow(surface.window);
            if (!wantInput && !overHudWidget && wasInteractive && GetForegroundWindow() == surface.window) SetForegroundWindow(game);
            menuVisible = ui.open;

            if (minimized) { surface.show(false); ui.poll(false); std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

            RECT area{}; GetClientRect(game, &area);
            POINT at{0, 0}; ClientToScreen(game, &at);
            if (at.x != origin.x || at.y != origin.y || area.right != client.right || area.bottom != client.bottom || !surface.shown) {
                client = area; origin = at;
                SetWindowPos(surface.window, nullptr, origin.x, origin.y, client.right, client.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            surface.resize(unsigned(client.right), unsigned(client.bottom));
            surface.show(true);
            ui.poll(ui.open || ui.chatting);

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            const ImVec2 size{float(surface.width), float(surface.height)};
            ui.drawHud(size);
            if (ui.open) ui.drawMenu(size);
            ImGui::Render();
            const float clear[4]{0, 0, 0, 0};
            surface.render(clear);
            ++overlayFrames;
            if (!ui.open && !ui.chatting) std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        overlayReady = false;
        menuVisible = false;
        surface.destroy();
        ImGui::DestroyContext(context);
    }
}

void Gui::runPreview(Request handler, json options) {
    const auto instance = GetModuleHandleW(nullptr);
    const std::wstring className = L"EFLink.NativeMenu." + std::to_wstring(GetCurrentProcessId());
    WNDCLASSEXW wc{sizeof(wc), 0, Surface::proc, 0, 0, instance, nullptr, LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, className.c_str(), nullptr};
    if (!RegisterClassExW(&wc)) throw std::runtime_error("预览窗口注册失败");
    Surface surface;
    if (!surface.create(className.c_str(), nullptr, false, 80, 60, 1440, 860)) {
        surface.destroy();
        UnregisterClassW(className.c_str(), instance);
        throw std::runtime_error("预览渲染器创建失败");
    }

    IMGUI_CHECKVERSION();
    auto* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    ui::applyTheme(1.0f);
    ui::fonts = ui::loadFonts(io.Fonts, 1.0f);
    io.FontDefault = ui::fonts.body;

    ImGui_ImplWin32_Init(surface.window);
    ImGui_ImplDX11_Init(surface.device.Get(), surface.context.Get());

    std::string demoRole = str(options, "preview_demo");
    if (options.value("preview_hud", false) && demoRole.empty()) demoRole = "host";
    json demoState = demoRole.empty() ? json() : demoStatus(demoRole);
    json demoHudData = demoRole.empty() ? json() : demoHud(demoState);

    Ui ui;
    ui.post = [handler, demoRole, demoState, demoHudData](const json& input) {
        std::promise<json> p;
        try {
            if (!demoRole.empty()) {
                const auto act = str(input, "action");
                p.set_value(act == "hud" ? demoHudData : demoState);
            } else {
                p.set_value(handler(input));
            }
        } catch (const std::exception& e) { p.set_value({{"error", e.what()}}); }
        return p.get_future();
    };
    ui.page = (std::clamp)(options.value("preview_tab", 0), 0, 5);
    char envTab[16]{};
    if (GetEnvironmentVariableA("EFLINK_PREVIEW_TAB", envTab, sizeof envTab) > 0) {
        ui.page = (std::clamp)(std::atoi(envTab), 0, 5);
    }
    ui.open = !options.value("preview_hud", false);

    const auto capture = str(options, "preview_capture");
    if (capture.empty()) ShowWindow(surface.window, SW_SHOW);
    else surface.show(true);

    uiReady = true;
    overlayReady = true;
    const auto started = now();
    while (!stopping && !surface.closeRequested) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (surface.pendingWidth) { surface.resize(surface.pendingWidth, surface.pendingHeight); surface.pendingWidth = 0; }
        ui.poll(true);
        if (!demoHudData.is_null()) ui.hud = demoHudData;
        if (!demoState.is_null()) { ui.state = demoState; ui.init(); }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        const ImVec2 size{float(surface.width), float(surface.height)};
        // In HUD-only preview or when menu is closed, show full HUD; when menu is open in preview, show clean backdrop + menu.
        if (!ui.open) ui.drawHud(size);
        if (ui.open) ui.drawMenu(size);
        if (ui.closeRequested) ui.closeRequested = false;
        ImGui::Render();
        const float clear[4]{0.065f, 0.080f, 0.100f, 1.0f};
        const bool doCapture = !capture.empty() && overlayFrames >= 8 && now() - started > 600;
        const std::filesystem::path capturePath = doCapture
            ? std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(capture.data()), capture.size()))
            : std::filesystem::path{};
        surface.render(clear, doCapture ? &capturePath : nullptr);
        ++overlayFrames;
        if (doCapture) break;
    }
    surface.destroy();
    ImGui::DestroyContext(context);
    UnregisterClassW(className.c_str(), instance);
    uiReady = false;
    overlayReady = false;
}

} // namespace eflink
