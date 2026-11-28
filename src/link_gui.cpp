// EF Link interface: transparent DirectComposition overlay over the game window.
// - In-game HUD (markers, teammate roster + micro HP/SP bars, shared ATB bar,
//   floating damage meter, chat) preserves the original clean borderless Endfield style.
// - Main menu follows Google's Android Material Design 3 (Material You) design language,
//   supporting both Light (default) and Dark Material 3 themes with 1-click switching.
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
    if (r == "host") return ui::Tone::Primary;
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
json demoStatus(const std::string& role, bool darkDefault = false, const std::string& hudStyleDefault = "telemetry") {
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
        })},
        {"pvp", {
            {"generation", 1}, {"in_combat", false}, {"duration_s", 18.4},
            {"total_damage", 32600.0}, {"team_dps", 1772.0}, {"total_hits", 29}, {"total_kills", 2},
            {"timeline", json::array({0,0,0,0,0,0,0,0,0,0,0,0,1200,2400,3100,4800,3900,2600,4100,5200,3300,2000,0,0,0,0,0,0,0,0})},
            {"players", json::array({
                {{"id",2},{"name","伊冯"},{"op","伊冯"},{"damage",18900.0},{"dps",1027.0},{"share",0.580},
                 {"hits",16},{"crits",7},{"crit_rate",0.438},{"max_hit",3420.0},{"kills",1},{"taken",13700.0},
                 {"by_type",{{"cryst",14100},{"physical",4800}}}},
                {{"id",1},{"name","管理员"},{"op","管理员"},{"damage",13700.0},{"dps",745.0},{"share",0.420},
                 {"hits",13},{"crits",5},{"crit_rate",0.385},{"max_hit",2680.0},{"kills",1},{"taken",18900.0},
                 {"by_type",{{"physical",10200},{"fire",3500}}}}
            })}
        }}
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
        {"pvp",{{"allowed",!offline},{"active",!offline},{"scale",1.0},{"revive",true},
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
            {"show_damage_hud",true},{"show_team_hud",true},{"show_atb_hud",true},{"hud_compact",false},{"hud_opacity",0.86},{"hud_style",hudStyleDefault},{"damage_auto_reset",false},
            {"menu_dark_mode",darkDefault},
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
    bool remote = true, squad = true, replay = true, outline = true, markers = true, follow = true, autoFollowHostMap = false, combat = true;
    bool pvp = false, pvpRevive = true, sharedAtb = false;
    float pvpScale = 1.f;
    // HUD & theme preferences
    bool showDamageHud = true, showTeamHud = true, showAtbHud = true, hudCompact = false, damageAutoReset = false;
    bool darkMode = false, appliedDarkMode = false;
    float hudOpacity = 0.86f;
    ui::HudStyle hudStyle = ui::HudStyle::Telemetry;
    int combatStatsTab = 0; // 0 = PvE (对怪物), 1 = PvP (对玩家)
    int hudDamageTab = 0;   // 0 = PvE (对怪物), 1 = PvP (对玩家)
    float displayScale = 1.0f;
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

    void syncThemeIfNeeded() {
        if (darkMode != appliedDarkMode) {
            ui::applyTheme(displayScale, darkMode);
            appliedDarkMode = darkMode;
        }
    }

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
        autoFollowHostMap = c.value("auto_follow_host_map", false);
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
        hudStyle = ui::hudStyleFromKey(c.value("hud_style", std::string("telemetry")));
        damageAutoReset = c.value("damage_auto_reset", false);
        darkMode = c.value("menu_dark_mode", false);
        syncThemeIfNeeded();
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
                const auto chatSer = state.value("chat_serial", std::uint64_t(0));
                if (page == 3 && open) { lastSeenChatSerial = chatSer; unreadChat = 0; }
                else if (chatSer > lastSeenChatSerial) unreadChat = chatSer - lastSeenChatSerial;
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
        const std::uint64_t hudInterval = online() ? 80u : (menuOpen ? 200u : 500u);
        if (!hudCall.valid() && t - hudSent >= hudInterval) {
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

    const json& liveCombatStats() const {
        if (hud.contains("combat_stats") && hud["combat_stats"].is_object()) return hud["combat_stats"];
        if (state.contains("combat_stats") && state["combat_stats"].is_object()) return state["combat_stats"];
        static const json empty = json::object();
        return empty;
    }
    const json& liveCombatStatsFor(bool pvpMode) const {
        const auto& root = liveCombatStats();
        if (!pvpMode) return root;
        if (root.contains("pvp") && root["pvp"].is_object()) return root["pvp"];
        static const json empty = json::object();
        return empty;
    }
    const json& liveSharedAtb() const {
        if (hud.contains("shared_atb") && hud["shared_atb"].is_object()) return hud["shared_atb"];
        if (state.contains("shared_atb") && state["shared_atb"].is_object()) return state["shared_atb"];
        static const json empty = json::object();
        return empty;
    }
    const json& liveVitals() const {
        if (hud.contains("vitals") && hud["vitals"].is_object()) return hud["vitals"];
        if (state.contains("vitals") && state["vitals"].is_object()) return state["vitals"];
        static const json empty = json::object();
        return empty;
    }
    json liveHostMapChange() const {
        if (hud.contains("host_map_change") && hud["host_map_change"].is_object()) return hud["host_map_change"];
        return state.value("game", json::object()).value("world_replication", json::object()).value("host_map_change", json::object());
    }
    static std::string hostMapDestLabel(const json& change) {
        const auto dungeon = str(change, "dungeon");
        const auto level = str(change, "level");
        const auto map = str(change, "map");
        if (!dungeon.empty()) return "副本 " + dungeon + (level.empty() ? "" : "（" + level + "）");
        if (!level.empty()) return level;
        return map.empty() ? "新区域" : map;
    }

    void saveSettings() {
        json patch = {
            {"room_name", room}, {"port", port}, {"max_players", limit},
            {"menu_key", key}, {"pvp_key", pvpKey}, {"damage_reset_key", damageResetKey},
            {"show_remote_players", remote}, {"show_remote_squad", squad},
            {"replay_skills", replay}, {"outline_teammates", outline},
            {"show_player_markers", markers}, {"follow_host_world", follow}, {"auto_follow_host_map", autoFollowHostMap}, {"sync_combat", combat},
            {"show_damage_hud", showDamageHud}, {"show_team_hud", showTeamHud},
            {"show_atb_hud", showAtbHud}, {"hud_compact", hudCompact},
            {"hud_opacity", double(hudOpacity)}, {"hud_style", ui::hudStyleKey(hudStyle)}, {"damage_auto_reset", damageAutoReset},
            {"menu_dark_mode", darkMode}
        };
        if (nameEdited) { patch["player_name"] = name; nameEdited = false; }
        act({{"action", "settings"}, {"configuration", patch}});
        say("设置已保存", false);
    }

    void applySwitches() {
        syncThemeIfNeeded();
        act({{"action", "settings"}, {"configuration", {
            {"show_remote_players", remote}, {"show_remote_squad", squad},
            {"replay_skills", replay}, {"outline_teammates", outline},
            {"show_player_markers", markers}, {"follow_host_world", follow}, {"auto_follow_host_map", autoFollowHostMap}, {"sync_combat", combat},
            {"show_damage_hud", showDamageHud}, {"show_team_hud", showTeamHud},
            {"show_atb_hud", showAtbHud}, {"hud_compact", hudCompact},
            {"hud_opacity", double(hudOpacity)}, {"hud_style", ui::hudStyleKey(hudStyle)}, {"damage_auto_reset", damageAutoReset},
            {"menu_dark_mode", darkMode}
        }}});
    }

    void join() {
        if (*address) act({{"action", "join"}, {"address", address}, {"password", joinPassword}});
        else say("请输入房主地址", true);
    }

    // =========================================================================
    // IN-GAME TRANSPARENT HUD (4 PRESETS: TELEMETRY / CHAMFER / HAZARD / MATERIAL)
    // =========================================================================
    void drawHud(ImVec2 size) {
        hudHitRects.clear();
        auto* d = ImGui::GetBackgroundDrawList();
        const bool showTags = online() && markers;
        std::map<int, float> distances;
        if (showTags) {
            ui::pushNote();
            const auto accent = ui::u32(ui::hud_orig::accent);
            const auto white  = ui::u32(ui::hud_orig::text);
            const auto ink    = ui::u32(ui::hud_orig::ink);
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
                    const float k = 1.f / (std::max)(std::abs(dx) / (std::max)(1.f, size.x * .5f - 44), std::abs(dy) / (std::max)(1.f, size.y * .5f - 44));
                    float ex = size.x * .5f + dx * k, ey = size.y * .5f + dy * k;
                    if (showTeamHud) {
                        const float rosterLeft = size.x - ui::px(320);
                        if (ex > rosterLeft && ey > size.y * 0.24f && ey < size.y * 0.72f) ex = rosterLeft;
                    }
                    d->AddTriangleFilled({ex + ux * 9, ey + uy * 9}, {ex - uy * 7 - ux * 5, ey + ux * 7 - uy * 5}, {ex + uy * 7 - ux * 5, ey - ux * 7 - uy * 5}, accent);
                    char lbl[48]{}; std::snprintf(lbl, sizeof lbl, "%s %s", idText, distText);
                    const auto ts = ImGui::CalcTextSize(lbl);
                    const ImVec2 tp{ex - ux * 28 - ts.x * .5f, ey - uy * 28 - ts.y * .5f};
                    const ImVec2 bMin{tp.x - ui::px(6), tp.y - ui::px(2.5f)}, bMax{tp.x + ts.x + ui::px(6), tp.y + ts.y + ui::px(2.5f)};
                    if (hudStyle == ui::HudStyle::Material) {
                        const float pr = (bMax.y - bMin.y) * 0.5f;
                        d->AddRectFilled(bMin, bMax, ui::u32(ui::hud_orig::base, 0.72f * hudOpacity), pr);
                        d->AddRect(bMin, bMax, ui::u32(ui::hud_orig::rim, hudOpacity), pr);
                    } else if (hudStyle == ui::HudStyle::Chamfer) {
                        ui::addChamferRectFilled(d, bMin, bMax, ui::px(5), ui::px(5), ui::u32(ui::hud_orig::base, 0.82f * hudOpacity));
                        ui::addChamferRect(d, bMin, bMax, ui::px(5), ui::px(5), ui::u32(ui::hud_orig::accent, 0.45f * hudOpacity));
                    } else if (hudStyle == ui::HudStyle::Hazard) {
                        d->AddRectFilled(bMin, bMax, IM_COL32(20, 22, 26, int(230 * hudOpacity)));
                        d->AddRect(bMin, bMax, ui::u32(ui::hud_orig::accent, 0.85f * hudOpacity));
                    } else {
                        d->AddLine({bMin.x, bMax.y}, {bMax.x, bMax.y}, ui::u32(ui::hud_orig::accent, 0.70f * hudOpacity), ui::px(1.2f));
                    }
                    ui::shadowText(d, tp, white, lbl);
                    continue;
                }
                const float r = 4.5f;
                const ImVec2 c{x, y - 4};
                const auto who = str(m, "name");
                const auto is = ImGui::CalcTextSize(idText), ns = ImGui::CalcTextSize(who.c_str()), ds = ImGui::CalcTextSize(distText);
                const float idW = is.x + ui::px(8);

                if (hudStyle == ui::HudStyle::Telemetry) {
                    // Style B: borderless minimal tactical diamond + sharp square ID block + shadowed text
                    d->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}, ui::u32(ui::hud_orig::accent, 0.35f));
                    const ImVec2 dp[4] = {{c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}};
                    d->AddPolyline(dp, 4, accent, ImDrawFlags_Closed, ui::px(1.5f));
                    const float w = idW + ui::px(6) + ns.x + ui::px(7) + ds.x;
                    const float left = x - w * .5f, top = c.y - r - ui::px(8) - is.y;
                    d->AddRectFilled({left, top}, {left + idW, top + is.y}, accent);
                    d->AddText({left + ui::px(4), top}, ink, idText);
                    ui::shadowText(d, {left + idW + ui::px(6), top}, white, who.c_str());
                    ui::shadowText(d, {left + idW + ui::px(6) + ns.x + ui::px(7), top}, accent, distText);
                } else if (hudStyle == ui::HudStyle::Chamfer) {
                    // Style A: 45-deg chamfered dark badge + vertical tick + filled yellow diamond
                    d->AddQuadFilled({c.x, c.y - r - 1.f}, {c.x + r + 1.f, c.y}, {c.x, c.y + r + 1.f}, {c.x - r - 1.f, c.y}, IM_COL32(0, 0, 0, 175));
                    d->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}, accent);
                    const float w = idW + ui::px(7) + ns.x + ui::px(8) + ds.x;
                    const float left = x - w * .5f, top = c.y - r - ui::px(13) - is.y;
                    const ImVec2 pMin{left - ui::px(4), top - ui::px(3)}, pMax{left + w + ui::px(7), top + is.y + ui::px(3)};
                    d->AddLine({x, pMax.y}, {x, c.y - r - 1.f}, ui::u32(ui::hud_orig::accent, 0.65f * hudOpacity), ui::px(1));
                    ui::addChamferRectFilled(d, pMin, pMax, ui::px(6), ui::px(6), ui::u32(ui::hud_orig::base, 0.84f * hudOpacity));
                    ui::addChamferRect(d, pMin, pMax, ui::px(6), ui::px(6), ui::u32(ui::hud_orig::accent, 0.45f * hudOpacity), ui::px(1));
                    d->AddRectFilled({left, top}, {left + idW, top + is.y}, accent);
                    d->AddText({left + ui::px(4), top}, ink, idText);
                    ui::shadowText(d, {left + idW + ui::px(7), top}, white, who.c_str());
                    ui::shadowText(d, {left + idW + ui::px(7) + ns.x + ui::px(8), top}, accent, distText);
                } else if (hudStyle == ui::HudStyle::Hazard) {
                    // Style C: 3-block segmented high-contrast flag + downward pointer triangle
                    const float padH = ui::px(2.5f);
                    const float idBoxW = is.x + ui::px(10), nameBoxW = ns.x + ui::px(12), distBoxW = ds.x + ui::px(10);
                    const float totalW = idBoxW + nameBoxW + distBoxW;
                    const float left = x - totalW * .5f, top = c.y - ui::px(12) - (is.y + padH * 2.f);
                    const float bot = top + is.y + padH * 2.f;
                    d->AddRectFilled({left, top}, {left + idBoxW, bot}, accent);
                    d->AddRectFilled({left + idBoxW, top}, {left + idBoxW + nameBoxW, bot}, IM_COL32(20, 22, 26, int(235 * hudOpacity)));
                    d->AddRectFilled({left + idBoxW + nameBoxW, top}, {left + totalW, bot}, IM_COL32(36, 40, 46, int(235 * hudOpacity)));
                    d->AddRect({left, top}, {left + totalW, bot}, accent, 0.f, 0, ui::px(1));
                    d->AddText({left + ui::px(5), top + padH}, ink, idText);
                    d->AddText({left + idBoxW + ui::px(6), top + padH}, white, who.c_str());
                    d->AddText({left + idBoxW + nameBoxW + ui::px(5), top + padH}, accent, distText);
                    d->AddTriangleFilled({x - ui::px(5), bot + ui::px(2)}, {x + ui::px(5), bot + ui::px(2)}, {x, bot + ui::px(8)}, accent);
                } else {
                    // Style D: Material 3 pill scrim + rounded ID chip + diamond
                    d->AddQuadFilled({c.x, c.y - r - 1.f}, {c.x + r + 1.f, c.y}, {c.x, c.y + r + 1.f}, {c.x - r - 1.f, c.y}, IM_COL32(0, 0, 0, 150));
                    d->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}, accent);
                    const float w = idW + ui::px(7) + ns.x + ui::px(8) + ds.x;
                    const float left = x - w * .5f, top = c.y - r - ui::px(11) - is.y;
                    const float padX = ui::px(8), padY = ui::px(3.5f);
                    const ImVec2 pMin{left - padX, top - padY}, pMax{left + w + padX, top + is.y + padY};
                    const float pillR = (pMax.y - pMin.y) * 0.5f;
                    d->AddRectFilled(pMin, pMax, ui::u32(ui::hud_orig::base, 0.72f * hudOpacity), pillR);
                    d->AddRect(pMin, pMax, ui::u32(ui::hud_orig::rim, hudOpacity), pillR);
                    d->AddRectFilled({left, top - 0.5f}, {left + idW, top + is.y + 0.5f}, accent, ui::px(4));
                    d->AddText({left + ui::px(4), top}, ink, idText);
                    ui::shadowText(d, {left + idW + ui::px(7), top}, white, who.c_str());
                    ui::shadowText(d, {left + idW + ui::px(7) + ns.x + ui::px(8), top}, accent, distText);
                }
            }
            ui::popNote();
        }

        if (online() && showTeamHud) drawRoster(size, distances);
        if (online() && showAtbHud) drawSharedAtbHud(size);
        if (showDamageHud) drawDamageHud(size);
        if (online()) drawChat(size); else chatting = false;

        if (!toast.empty() && now() - toastAt < 4000 && !open) {
            const auto ts = ImGui::CalcTextSize(toast.c_str());
            const float a = (std::clamp)(float(4000 - (now() - toastAt)) / 600.f, 0.f, 1.f);
            const ImVec4 tint = toastError ? ui::hud_orig::bad : ui::hud_orig::accent;
            if (hudStyle == ui::HudStyle::Material) {
                const float padX = ui::px(16), padY = ui::px(9), dotR = ui::px(3.5f);
                const float boxW = ts.x + padX * 2.f + ui::px(14), boxH = ts.y + padY * 2.f;
                const ImVec2 p{size.x * .5f - boxW * .5f, size.y - ui::px(138)};
                ui::hudGlassPanel(d, p, {p.x + boxW, p.y + boxH}, a, boxH * 0.5f / ui::gScale, ui::alpha(tint, 0.45f), 0.88f);
                d->AddCircleFilled({p.x + padX + dotR, p.y + boxH * 0.5f}, dotR, ui::u32(tint, a), 12);
                ui::shadowText(d, {p.x + padX + ui::px(14), p.y + padY}, ui::u32(ui::hud_orig::text, a), toast.c_str());
            } else if (hudStyle == ui::HudStyle::Chamfer) {
                const float padX = ui::px(14), padY = ui::px(7);
                const float boxW = ts.x + padX * 2.f + ui::px(6), boxH = ts.y + padY * 2.f;
                const ImVec2 p{size.x * .5f - boxW * .5f, size.y - ui::px(138)};
                ui::addChamferRectFilled(d, p, {p.x + boxW, p.y + boxH}, ui::px(7), ui::px(7), ui::u32(ui::hud_orig::base, 0.86f * a));
                ui::addChamferRect(d, p, {p.x + boxW, p.y + boxH}, ui::px(7), ui::px(7), ui::u32(tint, 0.50f * a));
                d->AddRectFilled(p, {p.x + ui::px(3), p.y + boxH - ui::px(7)}, ui::u32(tint, a));
                ui::shadowText(d, {p.x + padX + ui::px(4), p.y + padY}, ui::u32(ui::hud_orig::text, a), toast.c_str());
            } else if (hudStyle == ui::HudStyle::Hazard) {
                const float stripeW = ui::px(8), padX = ui::px(13), padY = ui::px(7);
                const float boxW = stripeW + ts.x + padX * 2.f, boxH = ts.y + padY * 2.f;
                const ImVec2 p{size.x * .5f - boxW * .5f, size.y - ui::px(138)};
                d->AddRectFilled(p, {p.x + boxW, p.y + boxH}, IM_COL32(16, 18, 21, int(235 * a)));
                ui::addHazardStripe(d, p, {p.x + stripeW, p.y + boxH}, a);
                d->AddRect(p, {p.x + boxW, p.y + boxH}, ui::u32(tint, a));
                ui::shadowText(d, {p.x + stripeW + padX, p.y + padY}, ui::u32(ui::hud_orig::text, a), toast.c_str());
            } else {
                // Telemetry: centered fade scrim + bottom accent underline + diamond glyph
                const float padX = ui::px(24), padY = ui::px(7);
                const float boxW = ts.x + padX * 2.f + ui::px(14), boxH = ts.y + padY * 2.f;
                const ImVec2 p{size.x * .5f - boxW * .5f, size.y - ui::px(138)};
                ui::hudStyledPanel(d, p, {p.x + boxW, p.y + boxH}, a, ui::HudStyle::Telemetry, tint, 0.78f, 0);
                d->AddLine({p.x + ui::px(16), p.y + boxH}, {p.x + boxW - ui::px(16), p.y + boxH}, ui::u32(tint, 0.65f * a), ui::px(1.2f));
                const float cy = p.y + boxH * 0.5f, cx = p.x + padX + ui::px(3);
                d->AddQuadFilled({cx, cy - ui::px(3.5f)}, {cx + ui::px(3.5f), cy}, {cx, cy + ui::px(3.5f)}, {cx - ui::px(3.5f), cy}, ui::u32(tint, a));
                ui::shadowText(d, {p.x + padX + ui::px(12), p.y + padY}, ui::u32(ui::hud_orig::text, a), toast.c_str());
            }
        }

        drawDuelInvite(size);
        drawHostMapHudCard(size);
    }

    // ---- Floating Roster + HP/SP (4 HUD Presets) ---------------------------
    void drawRoster(ImVec2 size, const std::map<int, float>& distances) {
        const auto peers = state.value("peers", json::array());
        if (peers.empty()) return;
        const auto& vitalsMap = liveVitals();
        auto* d = ImGui::GetBackgroundDrawList();
        ui::pushNote();

        const float op = hudOpacity;
        const float line = ImGui::GetTextLineHeight();
        const bool isHazard = hudStyle == ui::HudStyle::Hazard;
        const bool isMat    = hudStyle == ui::HudStyle::Material;
        const bool isTel    = hudStyle == ui::HudStyle::Telemetry;
        const float padX = isHazard ? ui::px(10) : ui::px(13);
        const float padY = isHazard ? ui::px(9)  : ui::px(10);
        const float innerW = hudCompact ? ui::px(226) : ui::px(252);
        const float boxW = innerW + padX * 2.f;

        const float headerBarH = isHazard ? (line + ui::px(8)) : (line + ui::px(12));
        float rowsH = 0.f;
        for (const auto& peer : peers) {
            const std::string idKey = std::to_string(peer.value("id", 0));
            json v = vitalsMap.contains(idKey) ? vitalsMap[idKey] : peer.value("vitals", json::object());
            const bool hasBars = !hudCompact && v.value("max_hp", 0.0) > 0.0;
            rowsH += hasBars ? (line + ui::px(15)) : (line + ui::px(8));
        }
        rowsH = (std::max)(0.f, rowsH - ui::px(4));

        const float boxH = isHazard ? (headerBarH + padY * 2.f + rowsH) : (headerBarH + rowsH + padY * 2.f);
        const float boxX = size.x - boxW - ui::px(18);
        const float boxY = size.y * 0.27f;
        ui::hudStyledPanel(d, {boxX, boxY}, {boxX + boxW, boxY + boxH}, op, hudStyle, ui::hud_orig::rim, 0.64f, +1, 15.f);

        const float x = boxX + padX;
        float y = boxY + padY;
        const float width = innerW;

        const auto accent = ui::u32(ui::hud_orig::accent, op);
        const auto white  = ui::u32(ui::hud_orig::text, op);
        const auto dim    = ui::u32(ui::hud_orig::dim, op);
        const auto ink    = ui::u32(ui::hud_orig::ink, op);

        auto roomName = str(state.value("room", json::object()), "name");
        if (roomName.empty()) roomName = role() == "host" ? "我的房间" : "房间";
        const std::string head = ui::ellipsize(
            "EF LINK // " + roomName + (state.value("pvp", json::object()).value("active", false) ? " // PVP" : ""),
            width - ui::px(56));

        const bool tcpOnly = state.value("traffic", json::object()).value("tcp_only_links", 0u) > 0;
        char rtt[32]{};
        const bool showRtt = role() == "client" || tcpOnly;
        if (showRtt) {
            if (tcpOnly) std::snprintf(rtt, sizeof rtt, "TCP");
            else std::snprintf(rtt, sizeof rtt, "%ums", state.value("rtt_ms", 0u));
        }

        if (isHazard) {
            // Solid crisp light-white industrial strip header with bold black text + inverted dark/yellow RTT chip
            d->AddRectFilled({boxX, boxY}, {boxX + boxW, boxY + headerBarH}, IM_COL32(242, 244, 247, int(245 * op)));
            const float ty = boxY + (headerBarH - line) * 0.5f;
            d->AddText({x, ty}, ink, head.c_str());
            if (showRtt) {
                const float rw = ImGui::CalcTextSize(rtt).x + ui::px(10);
                d->AddRectFilled({x + width - rw, boxY + ui::px(3)}, {x + width, boxY + headerBarH - ui::px(3)}, IM_COL32(18, 20, 24, int(245 * op)));
                d->AddText({x + width - rw + ui::px(5), ty}, tcpOnly ? ui::u32(ui::hud_orig::warn, op) : accent, rtt);
            }
            y = boxY + headerBarH + padY;
        } else {
            const float barR = isMat ? ui::px(1.5f) : 0.f;
            d->AddRectFilled({x, y + 2}, {x + ui::px(3), y + line - 2}, accent, barR);
            ui::shadowText(d, {x + ui::px(9), y}, accent, head.c_str());
            if (showRtt) {
                const float rw = ui::hudChipWidth(rtt);
                ui::hudChip(d, {x + width - rw, y - 1}, rtt,
                    tcpOnly ? ui::hud_orig::warn : ui::hud_orig::dim,
                    tcpOnly ? ui::alpha(ui::hud_orig::warn, 0.18f) : ui::alpha(ui::hud_orig::well, 0.85f),
                    op, line + 2, hudStyle);
            }
            y += line + ui::px(6);
            d->AddLine({x, y}, {x + width, y}, ui::u32(ui::hud_orig::accent, (isTel ? 0.48f : 0.36f) * op));
            y += ui::px(7);
        }

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
            const bool alive = v.value("alive", true);

            char idText[8]{}; std::snprintf(idText, sizeof idText, "%02d", id);
            const auto is = ImGui::CalcTextSize(idText);
            const float idW = is.x + ui::px(8);
            d->AddRectFilled({x, y}, {x + idW, y + line},
                !valid ? ui::u32(ui::hud_orig::faint, op) : (self ? white : accent), isMat ? ui::px(4.5f) : 0.f);
            d->AddText({x + ui::px(4), y}, ink, idText);

            char right[24]{};
            if (self) std::snprintf(right, sizeof right, "你");
            else if (auto it = distances.find(id); it != distances.end()) std::snprintf(right, sizeof right, "%.0fm", it->second);
            else if (peer.contains("distance")) std::snprintf(right, sizeof right, "%.0fm", peer.value("distance", 0.f));
            else if (!peer.value("same_map", true)) std::snprintf(right, sizeof right, "异图");
            else std::snprintf(right, sizeof right, "—");
            const float rightW = ImGui::CalcTextSize(right).x;
            ui::shadowText(d, {x + width - rightW, y}, self ? dim : accent, right);

            const float hostChipW = isHost ? (ui::hudChipWidth("房主") + ui::px(5)) : 0.f;
            const float pvpChipW  = dueling ? (ui::hudChipWidth("PVP") + ui::px(5)) : 0.f;
            const std::string who = str(peer, "name", "玩家");
            const float maxWhoW = (std::max)(ui::px(44), width - (idW + ui::px(10)) - rightW - hostChipW - pvpChipW - ui::px(8));
            const std::string shownWho = ui::ellipsize(who, maxWhoW);
            const float nameX = x + idW + ui::px(9);
            ui::shadowText(d, {nameX, y}, !alive ? ui::u32(ui::hud_orig::bad, op) : (valid ? white : dim), shownWho.c_str());

            float tagX = nameX + ImGui::CalcTextSize(shownWho.c_str()).x + ui::px(6);
            if (isHost) {
                ui::hudChip(d, {tagX, y}, "房主", ui::hud_orig::info,
                    isHazard ? ui::hex(0x1D3A4D) : ui::alpha(ui::hud_orig::info, 0.20f), op, line, hudStyle);
                tagX += ui::hudChipWidth("房主") + ui::px(4);
            }
            if (dueling) {
                ui::hudChip(d, {tagX, y}, "PVP", isHazard ? ui::hud_orig::text : ui::hud_orig::bad,
                    isHazard ? ui::hex(0xE03E3E) : ui::alpha(ui::hud_orig::bad, 0.24f), op, line, hudStyle);
            }

            if (!hudCompact && maxHp > 0.0) {
                const float hpRatio = float((std::clamp)(hp / maxHp, 0.0, 1.0));
                const float spRatio = maxUsp > 0.0 ? float((std::clamp)(usp / maxUsp, 0.0, 1.0)) : 0.f;
                const bool spFull = maxUsp > 0.0 && spRatio >= 0.995f;
                const ImVec4 hpCol = !alive ? ui::hud_orig::bad : (hpRatio > 0.55f ? ui::hud_orig::ok : (hpRatio > 0.25f ? ui::hud_orig::warn : ui::hud_orig::bad));
                const ImVec4 spCol = spFull ? ui::hud_orig::accent : ui::hud_orig::info;
                const float bx = isHazard ? x : nameX;
                const float bw = isHazard ? width : (width - (idW + ui::px(9)));
                const float by = y + line + ui::px(4);
                const float barR = isMat ? ui::px(2) : 0.f;
                const float hpH = isTel ? ui::px(2.6f) : ui::px(3.2f);
                const float spH = isTel ? ui::px(2.0f) : ui::px(2.4f);
                if (isHazard && maxUsp > 0.0) {
                    // Side-by-side 3:1 split bar in Hazard style
                    const float gap = ui::px(3);
                    const float hpW = (bw - gap) * 0.75f;
                    const float spW = (bw - gap) - hpW;
                    d->AddRectFilled({bx, by}, {bx + hpW, by + ui::px(3.5f)}, ui::u32(ui::hud_orig::well, 0.90f * op));
                    if (hpRatio > 0.005f) d->AddRectFilled({bx, by}, {bx + hpW * hpRatio, by + ui::px(3.5f)}, ui::u32(hpCol, op));
                    d->AddRectFilled({bx + hpW + gap, by}, {bx + bw, by + ui::px(3.5f)}, ui::u32(ui::hud_orig::well, 0.90f * op));
                    if (spRatio > 0.005f) d->AddRectFilled({bx + hpW + gap, by}, {bx + hpW + gap + spW * spRatio, by + ui::px(3.5f)}, ui::u32(spCol, op));
                } else {
                    d->AddRectFilled({bx, by}, {bx + bw, by + hpH}, ui::u32(ui::hud_orig::well, 0.85f * op), barR);
                    if (hpRatio > 0.005f) d->AddRectFilled({bx, by}, {bx + bw * hpRatio, by + hpH}, ui::u32(hpCol, op), barR);
                    if (maxUsp > 0.0) {
                        const float sy = by + hpH + ui::px(2.2f);
                        d->AddRectFilled({bx, sy}, {bx + bw, sy + spH}, ui::u32(ui::hud_orig::well, 0.80f * op), isMat ? ui::px(1.5f) : 0.f);
                        if (spRatio > 0.005f) d->AddRectFilled({bx, sy}, {bx + bw * spRatio, sy + spH}, ui::u32(spCol, op), isMat ? ui::px(1.5f) : 0.f);
                    }
                }
                y += line + ui::px(15);
            } else {
                y += line + ui::px(8);
            }
        }
        ui::popNote();
    }

    // ---- Shared ATB Pool Top Sync Bar (4 HUD Presets) ----------------------
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
        const bool isHazard = hudStyle == ui::HudStyle::Hazard;
        const bool isChamf  = hudStyle == ui::HudStyle::Chamfer;
        const bool isMat    = hudStyle == ui::HudStyle::Material;
        const float padX = ui::px(14), padY = ui::px(9);
        const float w = hudCompact ? ui::px(244) : ui::px(308);
        const float leftRail = isHazard ? ui::px(34) : 0.f;
        const float rightStripe = isHazard ? ui::px(8) : 0.f;
        const float boxW = leftRail + w + padX * 2.f + rightStripe;
        const float boxH = lh + ui::px(14) + padY * 2.f;
        const float boxX = size.x * 0.5f - boxW * 0.5f;
        const float boxY = ui::px(14);
        ui::hudStyledPanel(d, {boxX, boxY}, {boxX + boxW, boxY + boxH}, op, hudStyle, ui::hud_orig::rim, 0.66f, 0, 14.f);

        const float x0 = boxX + leftRail + padX;
        const float y0 = boxY + padY;

        if (isHazard) {
            d->AddRectFilled({boxX, boxY}, {boxX + leftRail, boxY + boxH}, ui::u32(ui::hud_orig::accent, op));
            const auto atbSz = ImGui::CalcTextSize("ATB");
            d->AddText({boxX + (leftRail - atbSz.x) * 0.5f, boxY + (boxH - atbSz.y) * 0.5f}, ui::u32(ui::hud_orig::ink, op), "ATB");
            ui::addHazardStripe(d, {boxX + boxW - rightStripe, boxY}, {boxX + boxW, boxY + boxH}, op);
            ui::shadowText(d, {x0, y0}, ui::u32(ui::hud_orig::text, op), "共享技力池");
        } else if (isChamf) {
            d->AddRectFilled({boxX, boxY}, {boxX + ui::px(3), boxY + boxH - ui::px(10)}, ui::u32(ui::hud_orig::accent, op));
            const auto atbSz = ImGui::CalcTextSize("ATB");
            const float tagW = atbSz.x + ui::px(8);
            d->AddRectFilled({x0, y0 + 1}, {x0 + tagW, y0 + lh - 1}, ui::u32(ui::hud_orig::accent, op));
            d->AddText({x0 + ui::px(4), y0}, ui::u32(ui::hud_orig::ink, op), "ATB");
            ui::shadowText(d, {x0 + tagW + ui::px(7), y0}, ui::u32(ui::hud_orig::text, op), "共享技力池");
        } else if (hudStyle == ui::HudStyle::Telemetry) {
            ui::shadowText(d, {x0, y0}, ui::u32(ui::hud_orig::accent, op), "[ 共享技力池 ]");
        } else {
            d->AddRectFilled({x0, y0 + 2}, {x0 + ui::px(3), y0 + lh - 2}, ui::u32(ui::hud_orig::accent, op), ui::px(1.5f));
            ui::shadowText(d, {x0 + ui::px(9), y0}, ui::u32(ui::hud_orig::accent, op), "共享技力池");
        }

        char numBuf[64]{};
        if (rec > 0.05f) std::snprintf(numBuf, sizeof numBuf, "%.0f / %.0f  +%.1f/s", val, mx, rec);
        else std::snprintf(numBuf, sizeof numBuf, "%.0f / %.0f", val, mx);
        const auto nSz = ImGui::CalcTextSize(numBuf);
        ui::shadowText(d, {x0 + w - nSz.x, y0}, ui::u32((isChamf || isHazard) ? ui::hud_orig::accent : ui::hud_orig::text, op), numBuf);

        const float by = y0 + lh + ui::px(6);
        const float barH = hudStyle == ui::HudStyle::Telemetry ? ui::px(5.5f) : ui::px(6.5f);
        ui::drawHudProgressBar(d, {x0, by}, {x0 + w, by + barH}, val / mx,
            ui::u32(ui::hud_orig::accent, op), ui::u32(ui::hud_orig::well, 0.90f * op), segs, hudStyle);

        // Spend/gain event feed underneath
        const auto events = atb.value("events", json::array());
        float ey = boxY + boxH + ui::px(7);
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
            const ImVec4 evCol = gain ? ui::hud_orig::ok : ui::hud_orig::warn;
            if (isMat) {
                const float chipPadX = ui::px(11), chipPadY = ui::px(3);
                const float chipW = es.x + chipPadX * 2.f + ui::px(10), chipH = es.y + chipPadY * 2.f;
                const ImVec2 cp{size.x * 0.5f - chipW * 0.5f, ey};
                d->AddRectFilled(cp, {cp.x + chipW, cp.y + chipH}, ui::u32(ui::hud_orig::base, 0.72f * fade), chipH * 0.5f);
                d->AddRect(cp, {cp.x + chipW, cp.y + chipH}, ui::u32(evCol, 0.38f * fade), chipH * 0.5f);
                d->AddCircleFilled({cp.x + chipPadX + ui::px(2.5f), cp.y + chipH * 0.5f}, ui::px(2.8f), ui::u32(evCol, fade), 10);
                ui::shadowText(d, {cp.x + chipPadX + ui::px(10), cp.y + chipPadY}, ui::u32(evCol, fade), evBuf);
                ey += chipH + ui::px(4);
            } else if (hudStyle == ui::HudStyle::Telemetry) {
                const float cx = size.x * 0.5f - (es.x + ui::px(8)) * 0.5f;
                d->AddRectFilled({cx, ey + ui::px(2)}, {cx + ui::px(2), ey + es.y - ui::px(2)}, ui::u32(evCol, fade));
                ui::shadowText(d, {cx + ui::px(7), ey}, ui::u32(evCol, fade), evBuf);
                ey += es.y + ui::px(4);
            } else if (isChamf) {
                const float chipPadX = ui::px(10), chipPadY = ui::px(2.5f);
                const float chipW = es.x + chipPadX * 2.f + ui::px(4), chipH = es.y + chipPadY * 2.f;
                const ImVec2 cp{size.x * 0.5f - chipW * 0.5f, ey};
                ui::addChamferRectFilled(d, cp, {cp.x + chipW, cp.y + chipH}, ui::px(5), ui::px(5), ui::u32(ui::hud_orig::base, 0.78f * fade));
                d->AddRectFilled(cp, {cp.x + ui::px(2.5f), cp.y + chipH - ui::px(5)}, ui::u32(evCol, fade));
                ui::shadowText(d, {cp.x + chipPadX + ui::px(2), cp.y + chipPadY}, ui::u32(evCol, fade), evBuf);
                ey += chipH + ui::px(4);
            } else {
                const float chipPadX = ui::px(8), chipPadY = ui::px(2);
                const float chipW = es.x + chipPadX * 2.f, chipH = es.y + chipPadY * 2.f;
                const ImVec2 cp{size.x * 0.5f - chipW * 0.5f, ey};
                d->AddRectFilled(cp, {cp.x + chipW, cp.y + chipH}, IM_COL32(18, 20, 24, int(230 * fade)));
                d->AddRect(cp, {cp.x + chipW, cp.y + chipH}, ui::u32(evCol, 0.85f * fade));
                ui::shadowText(d, {cp.x + chipPadX, cp.y + chipPadY}, ui::u32(evCol, fade), evBuf);
                ey += chipH + ui::px(4);
            }
            ++shown;
        }
        ui::popNote();
    }

    // ---- Floating Real-Time Damage Meter (4 HUD Presets, PvE / PvP Switch) -
    void drawDamageHud(ImVec2 size) {
        const bool pvpMode = (hudDamageTab == 1);
        const auto& stats = liveCombatStatsFor(pvpMode);
        const auto players = stats.value("players", json::array());
        const bool anyData = !liveCombatStatsFor(false).value("players", json::array()).empty() ||
                             !liveCombatStatsFor(true).value("players", json::array()).empty();
        if (!online() && !anyData) return;

        const float op = hudOpacity;
        const bool compact = hudCompact;
        const bool isMat    = hudStyle == ui::HudStyle::Material;
        const bool isTel    = hudStyle == ui::HudStyle::Telemetry;
        const bool isChamf  = hudStyle == ui::HudStyle::Chamfer;
        const bool isHazard = hudStyle == ui::HudStyle::Hazard;
        const float width = compact ? ui::px(268) : ui::px(332);
        const ImVec2 defaultPos{ui::px(20), size.y * 0.27f};
        ImGui::SetNextWindowPos(defaultPos, resetHudPosRequested ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        resetHudPosRequested = false;
        ImGui::SetNextWindowSize({width, 0.f});

        ImGui::PushStyleColor(ImGuiCol_WindowBg, isMat ? ui::alpha(ui::hud_orig::base, op * 0.68f) : ImVec4{0, 0, 0, 0});
        ImGui::PushStyleColor(ImGuiCol_Border, isMat ? ui::alpha(ui::hud_orig::rim, op) : ImVec4{0, 0, 0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(isHazard ? 10 : 13), ui::px(isHazard ? 9 : 11)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, isMat ? ui::px(15) : 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, isMat ? ui::px(1) : 0.f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

        if (ImGui::Begin("##eflink_dmg_hud", nullptr, flags)) {
            const ImVec2 wPos = ImGui::GetWindowPos();
            const ImVec2 wSz  = ImGui::GetWindowSize();
            hudHitRects.push_back({wPos, {wPos.x + wSz.x, wPos.y + wSz.y}});

            ImDrawList* d = ImGui::GetWindowDrawList();
            if (!isMat) {
                ui::hudStyledPanel(d, wPos, {wPos.x + wSz.x, wPos.y + wSz.y}, op, hudStyle, ui::hud_orig::rim, 0.68f, -1);
            }

            ui::pushNote();
            const float lh = ImGui::GetTextLineHeight();
            const ImVec2 ho = ImGui::GetCursorScreenPos();
            const float availW = ImGui::GetContentRegionAvail().x;
            char metaBuf[48]{};
            std::snprintf(metaBuf, sizeof metaBuf, "%.1fs · %s/s",
                stats.value("duration_s", 0.0), formatDamageNumber(stats.value("team_dps", 0.0)).c_str());

            const ImVec4 headerAccent = pvpMode ? ui::hud_orig::bad : ui::hud_orig::accent;
            if (isHazard) {
                const float stripH = lh + ui::px(8);
                d->AddRectFilled(wPos, {wPos.x + wSz.x, wPos.y + stripH}, ui::u32(headerAccent, op));
                const float ty = wPos.y + (stripH - lh) * 0.5f;
                const char* hazTitle = pvpMode ? "PVP // 玩家伤害" : "PVE // 怪物伤害";
                const ImU32 hazInk = pvpMode ? IM_COL32(255, 255, 255, int(255 * op)) : ui::u32(ui::hud_orig::ink, op);
                d->AddText({ho.x, ty}, hazInk, hazTitle);
                const float tw = ImGui::CalcTextSize(hazTitle).x;
                char tSec[24]{}; std::snprintf(tSec, sizeof tSec, "%.1fs", stats.value("duration_s", 0.0));
                d->AddText({ho.x + tw + ui::px(7), ty}, pvpMode ? IM_COL32(255, 255, 255, int(210 * op)) : IM_COL32(20, 22, 26, int(190 * op)), tSec);
            } else if (isTel) {
                const char* telTitle = pvpMode ? "// PVP" : "// 怪物";
                ui::shadowText(d, {ho.x, ho.y}, ui::u32(headerAccent, op), telTitle);
                const float titleW = ImGui::CalcTextSize(telTitle).x;
                ui::shadowText(d, {ho.x + titleW + ui::px(7), ho.y}, ui::u32(ui::hud_orig::dim, op), metaBuf);
            } else {
                const char* stdTitle = pvpMode ? "PVP伤害" : "怪物伤害";
                d->AddRectFilled({ho.x, ho.y + 2}, {ho.x + ui::px(3), ho.y + lh - 2}, ui::u32(headerAccent, op), isMat ? ui::px(1.5f) : 0.f);
                ui::shadowText(d, {ho.x + ui::px(9), ho.y}, ui::u32(headerAccent, op), stdTitle);
                const float titleW = ImGui::CalcTextSize(stdTitle).x;
                ui::shadowText(d, {ho.x + ui::px(15) + titleW, ho.y}, ui::u32(ui::hud_orig::dim, op), metaBuf);
            }

            // Styled Mode Toggle + Reset buttons on right
            const char* modeLbl = isTel ? (pvpMode ? "[切怪物]" : "[切PVP]") : (pvpMode ? "切怪物" : "切PVP");
            const char* rstLbl  = isTel ? "[重置]" : "重置";
            const float modeW = ImGui::CalcTextSize(modeLbl).x + (isTel ? ui::px(6) : ui::px(12));
            const float rstW  = ImGui::CalcTextSize(rstLbl).x  + (isTel ? ui::px(6) : ui::px(12));
            const float btnGap = ui::px(4);
            const float rstH = lh + ui::px(2);
            const float rstY = isHazard ? (wPos.y + ui::px(3)) : (ho.y - 1.f);
            auto pushHudBtnStyle = [&](bool emphasize) {
                if (isHazard) {
                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(18, 20, 24, int(245 * op)));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(42, 46, 54, 255));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(60, 64, 74, 255));
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::alpha(emphasize ? headerAccent : ui::hud_orig::text, op));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui::px(5), 1.f});
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
                } else if (isTel) {
                    ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::alpha(headerAccent, 0.18f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::alpha(headerAccent, 0.30f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::alpha(emphasize ? headerAccent : ui::hud_orig::text, 0.88f * op));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui::px(3), 0.f});
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
                } else if (isChamf) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ui::alpha(ui::hud_orig::well, 0.85f * op));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::alpha(headerAccent, 0.24f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::alpha(headerAccent, 0.38f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::alpha(emphasize ? headerAccent : ui::hud_orig::text, op));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui::px(6), 1.f});
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ui::px(1));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ui::alpha(ui::hud_orig::well, 0.85f * op));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::alpha(headerAccent, 0.22f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::alpha(headerAccent, 0.34f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::alpha(emphasize ? headerAccent : ui::hud_orig::text, op));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui::px(6), 1.f});
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rstH * 0.5f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
                }
            };
            ImGui::SetCursorScreenPos({ho.x + availW - rstW - btnGap - modeW, rstY});
            pushHudBtnStyle(true);
            if (ImGui::Button((std::string(modeLbl) + "##hudmode").c_str(), {modeW, rstH})) {
                hudDamageTab = pvpMode ? 0 : 1;
            }
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);

            ImGui::SetCursorScreenPos({ho.x + availW - rstW, rstY});
            pushHudBtnStyle(false);
            if (ImGui::Button((std::string(rstLbl) + "##hudrst").c_str(), {rstW, rstH})) {
                act({{"action", "combat_stats_reset"}, {"scope", pvpMode ? "pvp" : "pve"}});
                say(pvpMode ? "PVP 伤害统计已重置" : "对怪物伤害统计已重置", false);
            }
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);

            if (isHazard) {
                ImGui::SetCursorScreenPos({ho.x, wPos.y + lh + ui::px(15)});
            } else {
                ImGui::SetCursorScreenPos({ho.x, ho.y + lh + ui::px(6)});
                d->AddLine({ho.x, ho.y + lh + ui::px(5)}, {ho.x + availW, ho.y + lh + ui::px(5)},
                    isTel ? IM_COL32(255, 255, 255, int(64 * op)) : ui::u32(headerAccent, 0.36f * op));
                ui::vspace(ui::px(6));
            }

            if (players.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::alpha(ui::hud_orig::dim, op));
                ImGui::TextUnformatted(pvpMode ? "等待 PVP 对战伤害记录…" : "等待对怪物伤害记录…");
                ImGui::PopStyleColor();
            } else {
                const double topDmg = (std::max)(1.0, players[0].value("damage", 1.0));
                const int maxRows = compact ? 4 : 8;
                int idx = 0;
                for (const auto& p : players) {
                    if (idx >= maxRows) break;
                    const double dmg = p.value("damage", 0.0);
                    const double dps = p.value("dps", 0.0);
                    const double share = p.value("share", 0.0);
                    const float ratio = float((std::clamp)(dmg / topDmg, 0.0, 1.0));
                    const ImVec4 barCol = ui::hudRankColor(idx);

                    char rightStr[64]{};
                    if (compact) std::snprintf(rightStr, sizeof rightStr, "%s (%.0f%%)", formatDamageNumber(dmg).c_str(), share * 100.0);
                    else std::snprintf(rightStr, sizeof rightStr, "%s · %s/s (%.0f%%)", formatDamageNumber(dmg).c_str(), formatDamageNumber(dps).c_str(), share * 100.0);
                    const auto rSz = ImGui::CalcTextSize(rightStr);

                    std::string opTag = str(p, "op");
                    std::string leftStr;
                    if (isTel || isChamf) {
                        char rankBuf[8]{}; std::snprintf(rankBuf, sizeof rankBuf, "%02d ", idx + 1);
                        leftStr = std::string(rankBuf) + str(p, "name", "玩家");
                    } else {
                        leftStr = "#" + std::to_string(idx + 1) + " " + str(p, "name", "玩家");
                    }
                    if (!compact && !opTag.empty() && opTag != str(p, "name")) leftStr += " [" + opTag + "]";

                    const ImVec2 o = ImGui::GetCursorScreenPos();
                    const float rw = ImGui::GetContentRegionAvail().x;
                    if (isTel) {
                        // Style B: clean borderless text row + razor-sharp 2.5px bottom progress line
                        const float rowH = lh + ui::px(6);
                        const float maxLeft = (std::max)(ui::px(40), rw - rSz.x - ui::px(12));
                        ui::shadowText(d, {o.x, o.y}, ui::u32(ui::hud_orig::text, op), ui::ellipsize(leftStr, maxLeft).c_str());
                        ui::shadowText(d, {o.x + rw - rSz.x, o.y}, ui::u32(idx == 0 ? ui::hud_orig::accent : ui::hud_orig::text, op), rightStr);
                        const float lineY = o.y + lh + ui::px(3);
                        d->AddRectFilled({o.x, lineY}, {o.x + rw, lineY + ui::px(2.5f)}, IM_COL32(255, 255, 255, int(42 * op)));
                        if (ratio > 0.005f) d->AddRectFilled({o.x, lineY}, {o.x + rw * ratio, lineY + ui::px(2.5f)}, ui::u32(barCol, op));
                        ImGui::Dummy({rw, rowH});
                        ui::vspace(ui::px(4));
                    } else {
                        const float barH = compact ? (lh + ui::px(6)) : (lh + ui::px(8));
                        const float barR = isMat ? ui::px(8) : 0.f;
                        d->AddRectFilled(o, {o.x + rw, o.y + barH}, ui::u32(ui::hud_orig::well, (isHazard ? 0.50f : 0.56f) * op), barR);
                        if (ratio > 0.005f) {
                            d->AddRectFilled(o, {o.x + rw * ratio, o.y + barH}, ui::u32(barCol, op * (isHazard ? 0.25f : 0.28f)), barR);
                            if (isMat) {
                                d->AddRectFilled({o.x + ui::px(2), o.y + ui::px(3)}, {o.x + ui::px(5), o.y + barH - ui::px(3)}, ui::u32(barCol, op * 0.95f), ui::px(1.5f));
                            } else if (isChamf) {
                                d->AddRectFilled(o, {o.x + ui::px(2.5f), o.y + barH}, ui::u32(barCol, op));
                            }
                        }
                        const float ty = o.y + (barH - lh) * 0.5f;
                        const float maxLeft = (std::max)(ui::px(40), rw - rSz.x - ui::px(20));
                        ui::shadowText(d, {o.x + ui::px(isHazard ? 7 : 10), ty}, ui::u32(ui::hud_orig::text, op), ui::ellipsize(leftStr, maxLeft).c_str());
                        if (isHazard && idx == 0) {
                            d->AddRectFilled({o.x + rw - rSz.x - ui::px(11), o.y + ui::px(2)}, {o.x + rw - ui::px(3), o.y + barH - ui::px(2)}, IM_COL32(16, 18, 21, int(235 * op)));
                            d->AddText({o.x + rw - rSz.x - ui::px(7), ty}, ui::u32(ui::hud_orig::accent, op), rightStr);
                        } else {
                            ui::shadowText(d, {o.x + rw - rSz.x - ui::px(7), ty}, ui::u32(idx == 0 ? ui::hud_orig::accent : ui::hud_orig::text, op), rightStr);
                        }
                        ImGui::Dummy({rw, barH});
                        ui::vspace(ui::px(4));
                    }
                    ++idx;
                }
                if (!compact) {
                    ui::vspace(ui::px(1));
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::alpha(ui::hud_orig::dim, op));
                    if (isHazard) {
                        ImGui::Text("总伤 %s · %s/s · %s 重置",
                            formatDamageNumber(stats.value("total_damage", 0.0)).c_str(),
                            formatDamageNumber(stats.value("team_dps", 0.0)).c_str(), fKeyName(damageResetKey));
                    } else {
                        ImGui::Text("总伤 %s · %s 重置 · Alt+拖动",
                            formatDamageNumber(stats.value("total_damage", 0.0)).c_str(), fKeyName(damageResetKey));
                    }
                    ImGui::PopStyleColor();
                }
            }
            ui::popNote();
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    // ---- PVP Duel Invitation Banner ----------------------------------------
    void drawDuelInvite(ImVec2 size) {
        if (open || !online()) return;
        const auto invites = state.value("pvp", json::object()).value("invites", json::array());
        if (invites.empty()) return;
        const auto& invite = invites.back();
        const int from = invite.value("id", 0);
        const float age = float(invite.value("age_ms", std::uint64_t(0)));
        const float slide = (std::clamp)(age / 240.f, 0.f, 1.f);
        const float ease = 1.f - (1.f - slide) * (1.f - slide);
        const float left = (std::clamp)(30000.f - age, 0.f, 30000.f) / 1000.f;
        const float width = (std::min)(ui::px(440), size.x - ui::px(40));

        ImGui::SetNextWindowPos({size.x * 0.5f - width * 0.5f, -ui::px(90) + ease * (ui::px(90) + size.y * 0.12f)});
        ImGui::SetNextWindowSize({width, 0.f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::md.surfaceContainerLowest);
        ImGui::PushStyleColor(ImGuiCol_Border, ui::md.outlineVariant);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(20), ui::px(16)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::px(20));
        if (ImGui::Begin("##duelinvite", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing)) {
            ui::pushBold();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::md.primary);
            ImGui::TextUnformatted("PVP 对战邀请");
            ImGui::PopStyleColor();
            ui::popBold();
            ui::pushNote();
            ui::alignRight(ui::px(46));
            ui::muted("%.0f 秒", left);
            ui::popNote();
            ui::vspace(ui::px(6));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextWrapped("%s 邀请你进行 PVP 切磋。同意后可互相锁定和攻击，随时可结束对战。", str(invite, "name").c_str());
            ImGui::PopTextWrapPos();
            ui::vspace(ui::px(12));
            const float bw = (ImGui::GetContentRegionAvail().x - ui::px(10)) * 0.5f;
            const std::string acc = std::string("同意（") + fKeyName(pvpKey) + "）";
            if (ui::filledButton(acc.c_str(), {bw, ui::px(36)})) act({{"action", "pvp_duel"}, {"op", "accept"}, {"target", from}});
            ImGui::SameLine(0.f, ui::px(10));
            if (ui::outlinedButton("拒绝", {bw, ui::px(36)})) act({{"action", "pvp_duel"}, {"op", "decline"}, {"target", from}});
            const ImVec2 p0 = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
            hudHitRects.push_back({p0, {p0.x + sz.x, p0.y + sz.y}});
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    // ---- Floating HUD Prompt for Host Map Change (clickable when closed) ----
    void drawHostMapHudCard(ImVec2 size) {
        if (open || role() != "client") return;
        const auto change = liveHostMapChange();
        if (!change.value("pending", false)) return;
        const bool deferred = change.value("deferred", false);
        const bool supported = change.value("supported", true);
        const auto token = change.value("token", std::uint64_t(0));
        const std::string dest = hostMapDestLabel(change);

        if (deferred) {
            auto* d = ImGui::GetBackgroundDrawList();
            const std::string text = "房主位于 " + dest + " · 按 " + menuKeyName(key) + " 可随时跟随";
            ui::pushNote();
            const auto ts = ImGui::CalcTextSize(text.c_str());
            const float padX = ui::px(14), padY = ui::px(6), dotR = ui::px(3);
            const float boxW = ts.x + padX * 2.f + ui::px(12), boxH = ts.y + padY * 2.f;
            const ImVec2 p{size.x * 0.5f - boxW * 0.5f, ui::px(76)};
            ui::hudStyledPanel(d, p, {p.x + boxW, p.y + boxH}, hudOpacity, hudStyle, ui::alpha(ui::hud_orig::accent, 0.50f), 0.78f, 0, boxH * 0.5f / ui::gScale);
            if (hudStyle == ui::HudStyle::Material) {
                d->AddCircleFilled({p.x + padX + dotR, p.y + boxH * 0.5f}, dotR, ui::u32(ui::hud_orig::accent, hudOpacity), 12);
            } else {
                const float cx = p.x + padX + dotR, cy = p.y + boxH * 0.5f;
                d->AddQuadFilled({cx, cy - dotR}, {cx + dotR, cy}, {cx, cy + dotR}, {cx - dotR, cy}, ui::u32(ui::hud_orig::accent, hudOpacity));
            }
            ui::shadowText(d, {p.x + padX + ui::px(12), p.y + padY}, ui::u32(ui::hud_orig::text, hudOpacity), text.c_str());
            ui::popNote();
            return;
        }

        const bool duelVisible = !state.value("pvp", json::object()).value("invites", json::array()).empty();
        const float topY = duelVisible ? size.y * 0.27f : ui::px(82);
        const float width = (std::min)(ui::px(470), size.x - ui::px(40));

        ImGui::SetNextWindowPos({size.x * 0.5f - width * 0.5f, topY});
        ImGui::SetNextWindowSize({width, 0.f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::md.surfaceContainerLowest);
        ImGui::PushStyleColor(ImGuiCol_Border, ui::md.outlineVariant);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(20), ui::px(16)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::px(20));
        if (ImGui::Begin("##hostmapinvite", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing)) {
            ui::pushBold();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::md.primary);
            ImGui::TextUnformatted("房主切换了地图");
            ImGui::PopStyleColor();
            ui::popBold();
            ui::pushNote();
            ui::alignRight(ui::px(108));
            ui::muted("按 %s 也可操作", menuKeyName(key).c_str());
            ui::popNote();
            ui::vspace(ui::px(6));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextWrapped("房主已前往 %s。是否跟随前往该区域？", dest.c_str());
            if (!supported) {
                ui::vspace(ui::px(4));
                ImGui::PushStyleColor(ImGuiCol_Text, ui::md.error);
                ImGui::TextWrapped("房主当前场景尚未开放联机入场，请等待其回到大世界。");
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
            ui::vspace(ui::px(12));
            const float avail = ImGui::GetContentRegionAvail().x;
            const float gap = ui::px(8);
            const float bw1 = (avail - gap * 2.f) * 0.36f;
            const float bw2 = (avail - gap * 2.f) * 0.36f;
            const float bw3 = avail - bw1 - bw2 - gap * 2.f;
            ImGui::BeginDisabled(!supported);
            if (ui::filledButton("跟随房主", {bw1, ui::px(36)})) {
                act({{"action", "host_map_choice"}, {"token", token}, {"follow", true}});
            }
            ImGui::SameLine(0.f, gap);
            if (ui::tonalButton("总是自动跟随", {bw2, ui::px(36)})) {
                autoFollowHostMap = true;
                applySwitches();
                act({{"action", "host_map_choice"}, {"token", token}, {"follow", true}});
                say("已开启房主切图自动跟随", false);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0.f, gap);
            if (ui::outlinedButton("暂不跟随", {bw3, ui::px(36)})) {
                act({{"action", "host_map_choice"}, {"token", token}, {"follow", false}});
            }
            const ImVec2 p0 = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
            hudHitRects.push_back({p0, {p0.x + sz.x, p0.y + sz.y}});
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    // ---- Cross-Page Menu Banner for Pending Host Map Change -----------------
    void drawHostMapMenuBanner() {
        if (role() != "client") return;
        const auto change = liveHostMapChange();
        if (!change.value("pending", false)) return;
        const auto entry = state.value("world_session", json::object()).value("entry", json::object());
        const auto blockers = entry.value("blockers", json::array());
        const bool supported = change.value("supported", true) &&
            std::find(blockers.begin(), blockers.end(), json("host_scene_rules")) == blockers.end();
        const auto token = change.value("token", std::uint64_t(0));
        const std::string dest = hostMapDestLabel(change);

        ui::cardBegin("##host_map_banner", nullptr, {0.f, 0.f});
        ui::pushBold();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::md.primary);
        ImGui::Text("房主已前往：%s", dest.c_str());
        ImGui::PopStyleColor();
        ui::popBold();
        if (!supported) {
            ui::vspace(ui::px(4));
            ui::hint("房主当前场景尚未开放联机入场，请等待其回到大世界。");
        }
        ui::vspace(ui::px(8));
        ImGui::BeginDisabled(!supported);
        if (ui::filledButton("跟随房主", {ui::px(116), ui::px(36)})) {
            act({{"action", "host_map_choice"}, {"token", token}, {"follow", true}});
        }
        ImGui::SameLine(0.f, ui::px(8));
        if (ui::tonalButton("总是自动跟随", {ui::px(128), ui::px(36)})) {
            autoFollowHostMap = true;
            applySwitches();
            act({{"action", "host_map_choice"}, {"token", token}, {"follow", true}});
            say("已开启房主切图自动跟随", false);
        }
        ImGui::EndDisabled();
        if (!change.value("deferred", false)) {
            ImGui::SameLine(0.f, ui::px(8));
            if (ui::outlinedButton("暂不跟随", {ui::px(104), ui::px(36)})) {
                act({{"action", "host_map_choice"}, {"token", token}, {"follow", false}});
            }
        }
        ui::cardEnd();
        ui::vspace(ui::px(12));
    }

    // ---- In-Game HUD Chat (Hybrid Endfield × Material 3 Tonal Glass) -------
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
        float maxAlpha = 0.f;
        for (auto it = lines.rbegin(); it != lines.rend() && show.size() < (chatting ? 10u : 6u); ++it) {
            const auto seen = chatSeen[it->value("serial", std::uint64_t(0))];
            if (chatting || (seen && t - seen < 12000)) {
                const float a = chatting ? 1.f : (std::clamp)(float(12000 - (t - seen)) / 2000.f, 0.f, 1.f);
                if (a > maxAlpha) maxAlpha = a;
                show.push_back(&*it);
            }
        }
        ui::pushNote();
        auto* d = ImGui::GetBackgroundDrawList();
        const float op = hudOpacity;
        const float lh = ImGui::GetTextLineHeight();
        const float line = lh + ui::px(7);
        const float padX = ui::px(14), padY = ui::px(10);
        const float boxX = ui::px(20);
        const float boxW = (std::min)(ui::px(500), size.x * .38f);
        const float left = boxX + padX;
        const float innerW = boxW - padX * 2.f;
        const float bottom = size.y * .78f;

        if (!show.empty()) {
            const float headerH = chatting ? (lh + ui::px(11)) : 0.f;
            const float msgH = line * float(show.size());
            const float boxTop = bottom - msgH - headerH - padY;
            const float boxBot = bottom + padY * 0.6f;
            const float panelOp = chatting ? op : (op * maxAlpha);
            const bool isMat = hudStyle == ui::HudStyle::Material;
            ui::hudStyledPanel(d, {boxX, boxTop}, {boxX + boxW, boxBot}, panelOp, hudStyle,
                chatting ? ui::alpha(ui::hud_orig::accent, 0.42f) : ui::hud_orig::rim,
                chatting ? 0.78f : 0.58f, -1, chatting ? 16.f : 14.f);

            float y = boxTop + padY;
            if (chatting) {
                d->AddRectFilled({left, y + 2}, {left + ui::px(3), y + lh - 2}, ui::u32(ui::hud_orig::accent, op), isMat ? ui::px(1.5f) : 0.f);
                ui::shadowText(d, {left + ui::px(9), y}, ui::u32(ui::hud_orig::accent, op), "队伍聊天");
                const char* hintStr = "Enter 发送 · Esc 取消";
                const auto hs = ImGui::CalcTextSize(hintStr);
                ui::shadowText(d, {left + innerW - hs.x, y}, ui::u32(ui::hud_orig::dim, op * 0.9f), hintStr);
                y += lh + ui::px(5);
                d->AddLine({left, y}, {left + innerW, y}, ui::u32(ui::hud_orig::accent, 0.34f * op));
                y += ui::px(6);
            }

            for (auto it = show.rbegin(); it != show.rend(); ++it, y += line) {
                const auto& l = **it;
                const auto seen = chatSeen[l.value("serial", std::uint64_t(0))];
                const float a = (chatting ? 1.f : (std::clamp)(float(12000 - (t - seen)) / 2000.f, 0.f, 1.f)) * op;
                d->PushClipRect({left - ui::px(2), y - ui::px(2)}, {left + innerW + ui::px(2), y + line}, true);
                if (l.value("system", false)) {
                    const float dotY = y + lh * 0.5f;
                    if (isMat) d->AddCircleFilled({left + ui::px(4), dotY}, ui::px(2.5f), ui::u32(ui::hud_orig::info, a * 0.9f), 10);
                    else d->AddRectFilled({left + ui::px(2), dotY - ui::px(2.5f)}, {left + ui::px(6), dotY + ui::px(2.5f)}, ui::u32(ui::hud_orig::info, a * 0.9f));
                    ui::shadowText(d, {left + ui::px(12), y}, ui::u32(ui::hud_orig::dim, a), str(l, "text").c_str());
                } else {
                    const auto sender = str(l, "name");
                    const auto msg    = str(l, "text");
                    const float chipW = ui::hudChipWidth(sender.c_str());
                    ui::hudChip(d, {left, y - 0.5f}, sender.c_str(), ui::hud_orig::accent, ui::alpha(ui::hud_orig::accent, 0.16f), a, lh + 1.f, hudStyle);
                    ui::shadowText(d, {left + chipW + ui::px(8), y}, ui::u32(ui::hud_orig::text, a), msg.c_str());
                }
                d->PopClipRect();
            }
        }
        if (chatting) {
            const float inputRound = (hudStyle == ui::HudStyle::Material) ? ui::px(18) : 0.f;
            ImGui::SetNextWindowPos({boxX, bottom + padY * 0.6f + ui::px(6)});
            ImGui::SetNextWindowSize({boxW, 0});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, {0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_Text, ui::hud_orig::text);
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ui::hud_orig::dim);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ui::alpha(ui::hud_orig::base, 0.88f * op));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui::alpha(ui::hud_orig::surface, 0.92f * op));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui::alpha(ui::hud_orig::surface, 0.95f * op));
            ImGui::PushStyleColor(ImGuiCol_Border, ui::alpha(ui::hud_orig::accent, 0.75f * op));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, inputRound);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui::px(14), ui::px(9)});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, inputRound);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ui::px(1.2f));
            if (ImGui::Begin("##hudchat", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
                if (chatFocus) { ImGui::SetKeyboardFocusHere(); chatFocus = false; }
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputTextWithHint("##hudchatinput", "输入消息，Enter 发送，Esc 取消", hudChat, sizeof hudChat, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    if (*hudChat) act({{"action", "chat"}, {"text", hudChat}});
                    hudChat[0] = 0; chatting = false;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { hudChat[0] = 0; chatting = false; }
            }
            ImGui::End();
            ImGui::PopStyleVar(6);
            ImGui::PopStyleColor(7);
        }
        ui::popNote();
    }

    // =========================================================================
    // MAIN MENU — GOOGLE ANDROID MATERIAL DESIGN 3 (MATERIAL YOU)
    // =========================================================================
    void drawMenu(ImVec2 display) {
        syncThemeIfNeeded();

        const float maxW = (std::max)(480.f, display.x - ui::px(32));
        const float maxH = (std::max)(360.f, display.y - ui::px(32));
        const float minW = (std::min)(ui::px(920), maxW);
        const float minH = (std::min)(ui::px(580), maxH);
        const bool displayChanged = display.x != lastDisplay.x || display.y != lastDisplay.y;
        if (!menuLayoutReady || displayChanged) {
            if (!menuLayoutReady) {
                menuSize = {(std::min)(ui::px(1136), maxW), (std::min)(ui::px(748), maxH)};
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

            drawTopAppBar();

            const float footerH = ui::px(64);
            drawNavDrawer(-footerH);
            ImGui::SameLine(0.f, ui::px(14));

            ImGui::BeginChild("##eflink_content", {0.f, -footerH}, ImGuiChildFlags_None);
            drawHostMapMenuBanner();
            switch (page) {
            case 0: pageSession(); break;
            case 1: pagePlayers(); break;
            case 2: pageCombatStats(); break;
            case 3: pageChat(); break;
            case 4: pageSettings(); break;
            default: pageDiagnostics(); break;
            }
            ImGui::EndChild();

            drawBottomBar();
            popups();
        }
        ImGui::End();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            closeRequested = true;
        }
    }

    // ---- Material 3 Top App Bar (flat, clean, circular icon actions) -------
    void drawTopAppBar() {
        const auto r = role();
        const auto roomInfo = state.value("room", json::object());
        const auto pvpInfo = state.value("pvp", json::object());
        const auto& atbInfo = liveSharedAtb();

        const float barH = ui::px(42);
        const float rightClusterW = ui::px(38) * 2 + ui::px(10);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float dragW = (std::max)(ui::px(220), availW - rightClusterW - ui::px(12));

        ImGui::InvisibleButton("##topbar_drag", {dragW, barH});
        const bool dragHov = ImGui::IsItemHovered();
        dragActive = ImGui::IsItemActive();
        if (dragHov || dragActive) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        ImDrawList* d = ImGui::GetWindowDrawList();
        const float midY = origin.y + barH * 0.5f;
        ui::drawAppMark(d, {origin.x + ui::px(18), midY}, ui::px(16));

        ui::pushTitle();
        const float titleH = ImGui::GetTextLineHeight();
        d->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            {origin.x + ui::px(44), midY - titleH * 0.5f}, ui::u32(ui::md.onSurface), "EF Link");
        const float titleEnd = origin.x + ui::px(44) + ImGui::CalcTextSize("EF Link").x;
        ui::popTitle();

        // Compact Material 3 status chips right on the Top App Bar
        float cx = titleEnd + ui::px(14);
        auto drawChipInline = [&](const char* text, ui::Tone tone, bool dot) {
            ui::pushNote();
            const ImVec2 ts = ImGui::CalcTextSize(text);
            const float dotS = dot ? ui::px(12) : 0.f;
            const float ch = ts.y + ui::px(8), cw = ts.x + ui::px(20) + dotS;
            if (cx + cw < origin.x + dragW - ui::px(8)) {
                const ImVec2 p0{cx, midY - ch * 0.5f}, p1{cx + cw, midY + ch * 0.5f};
                d->AddRectFilled(p0, p1, ui::u32(ui::toneContainer(tone)), ch * 0.5f);
                if (dot) d->AddCircleFilled({p0.x + ui::px(11), midY}, ui::px(3.2f), ui::u32(ui::toneFg(tone)), 12);
                d->AddText({p0.x + ui::px(10) + dotS, midY - ts.y * 0.5f}, ui::u32(ui::toneOnContainer(tone)), text);
                cx += cw + ui::px(8);
            }
            ui::popNote();
        };

        drawChipInline(roleLabel(r), roleTone(r), true);
        if (online()) {
            char pBuf[32]{};
            std::snprintf(pBuf, sizeof pBuf, "%d / %d 人", int(state.value("peers", json::array()).size()), roomInfo.value("max_players", 8));
            drawChipInline(pBuf, ui::Tone::Neutral, false);
            if (atbInfo.value("active", false)) drawChipInline("共享技力池", ui::Tone::Primary, false);
            if (pvpInfo.value("active", false)) drawChipInline("PVP 已开启", ui::Tone::Error, false);
        } else {
            ui::pushNote();
            const std::string sub = str(state, "name", "管理员") + " · 按 " + menuKeyName(key) + " 开关菜单";
            const float subMax = (origin.x + dragW) - cx - ui::px(8);
            if (subMax > ui::px(80)) {
                const ImVec2 ss = ImGui::CalcTextSize(sub.c_str());
                d->AddText({cx + ui::px(4), midY - ss.y * 0.5f}, ui::u32(ui::md.onSurfaceVariant), ui::ellipsize(sub, subMax).c_str());
            }
            ui::popNote();
        }

        // Inline toast snackbar pill (if active)
        if (!toast.empty() && now() - toastAt < 4000) {
            drawChipInline(toast.c_str(), toastError ? ui::Tone::Error : ui::Tone::Success, false);
        }

        // Trailing Material 3 Circular Icon Buttons: Theme Toggle (Light/Dark) + Close
        ui::alignRight(rightClusterW);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (barH - ui::px(38)) * 0.5f);
        if (ui::iconButton("theme_toggle", darkMode ? ui::Icon::Sun : ui::Icon::Moon,
            darkMode ? "切换浅色主题（Material 3 Light）" : "切换深色主题（Material 3 Dark）")) {
            darkMode = !darkMode;
            applySwitches();
        }
        ImGui::SameLine(0.f, ui::px(6));
        if (ui::iconButton("close_menu", ui::Icon::Close, "返回游戏（Esc）")) {
            closeRequested = true;
        }
        ui::vspace(ui::px(8));
    }

    // ---- Material 3 Permanent Navigation Drawer (borderless, full-pill) ----
    void drawNavDrawer(float height) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0, 0, 0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(4), ui::px(4)});
        ImGui::BeginChild("##m3_drawer", {ui::px(214), height}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);

        // Iconic Material 3 Extended FAB (like Gmail Compose / Drive New)
        if (!online()) {
            if (ui::extendedFab("开放房间", ui::Icon::Plus, ui::Tone::Primary)) {
                page = 0;
            }
        } else {
            if (ui::extendedFab("重置伤害统计", ui::Icon::Reset, ui::Tone::Primary)) {
                act({{"action", "combat_stats_reset"}});
                say("伤害统计已重置", false);
            }
        }
        ui::vspace(ui::px(10));

        char roomBadge[16]{}, peerBadge[16]{}, chatBadge[16]{};
        const int roomCnt = int(state.value("rooms", json::array()).size());
        const int peerCnt = int(state.value("peers", json::array()).size());
        if (!online() && roomCnt > 0) std::snprintf(roomBadge, sizeof roomBadge, "%d", roomCnt);
        if (online() && peerCnt > 0) std::snprintf(peerBadge, sizeof peerBadge, "%d", peerCnt);
        if (unreadChat > 0) std::snprintf(chatBadge, sizeof chatBadge, "%llu", static_cast<unsigned long long>(unreadChat));

        const bool anyCombat = liveCombatStatsFor(false).value("in_combat", false) || liveCombatStatsFor(true).value("in_combat", false);
        const char* combatBadge = anyCombat ? "进行中" : nullptr;

        if (ui::navDrawerItem(ui::Icon::Lobby,       "联机大厅",   page == 0, *roomBadge ? roomBadge : nullptr)) page = 0;
        ui::vspace(ui::px(2));
        if (ui::navDrawerItem(ui::Icon::Players,     "房间玩家",   page == 1, *peerBadge ? peerBadge : nullptr)) page = 1;
        ui::vspace(ui::px(2));
        if (ui::navDrawerItem(ui::Icon::Combat,      "战斗统计",   page == 2, combatBadge)) page = 2;
        ui::vspace(ui::px(2));
        if (ui::navDrawerItem(ui::Icon::Chat,        "房间聊天",   page == 3, *chatBadge ? chatBadge : nullptr)) {
            page = 3; unreadChat = 0; lastSeenChatSerial = state.value("chat_serial", std::uint64_t(0));
        }
        ui::vspace(ui::px(2));
        if (ui::navDrawerItem(ui::Icon::Settings,    "外观与设置", page == 4, nullptr)) page = 4;
        ui::vspace(ui::px(2));
        if (ui::navDrawerItem(ui::Icon::Diagnostics, "运行诊断",   page == 5, nullptr)) page = 5;

        // Subtle pinned hotkey reference at bottom of drawer
        const float legendH = ui::px(154);
        const float slack = ImGui::GetContentRegionAvail().y - legendH;
        if (slack > ui::px(4)) {
            ImGui::Dummy({0.f, slack});
            ImGui::Separator();
            ui::vspace(ui::px(6));
            ImGui::Indent(ui::px(12));
            ui::pushNote();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ui::px(6), ui::px(4)});
            ui::muted("快捷键");
            ui::muted("%s  开关菜单", menuKeyName(key).c_str());
            ui::muted("%s  PVP 邀请", fKeyName(pvpKey));
            ui::muted("%s  重置伤害统计", fKeyName(damageResetKey));
            ui::muted("Enter  快捷聊天");
            ImGui::PopStyleVar();
            ui::popNote();
            ImGui::Unindent(ui::px(12));
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // ---- Material 3 Bottom Filter-Chip Quick Bar ---------------------------
    void drawBottomBar() {
        ui::vspace(ui::px(4));
        ImGui::Separator();
        ui::vspace(ui::px(9));
        const auto r = role();
        if (online() || r == "connecting") {
            const char* lbl = r == "host" ? "关闭房间" : (r == "client" ? "离开房间" : "取消连接");
            if (ui::dangerButton(lbl, {0.f, ui::px(30)})) leaveConfirm = true;
            ui::hspace(ui::px(10));
        }
        if (ui::filterChip("悬浮伤害", showDamageHud, ui::Tone::Secondary)) {
            showDamageHud = !showDamageHud; applySwitches();
        }
        ui::hspace(ui::px(8));
        if (ui::filterChip("队友状态", showTeamHud, ui::Tone::Secondary)) {
            showTeamHud = !showTeamHud; applySwitches();
        }
        ui::hspace(ui::px(8));
        if (ui::filterChip("共享技力提示", showAtbHud, ui::Tone::Secondary)) {
            showAtbHud = !showAtbHud; applySwitches();
        }

        char rightMeta[96]{};
        if (online()) std::snprintf(rightMeta, sizeof rightMeta, "延迟 %u ms · EF Link %s", state.value("rtt_ms", 0u), EFLINK_VERSION);
        else std::snprintf(rightMeta, sizeof rightMeta, "就绪 · EF Link %s", EFLINK_VERSION);
        ui::pushNote();
        ui::alignRight(ImGui::CalcTextSize(rightMeta).x + ui::px(4));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui::px(6));
        ui::muted("%s", rightMeta);
        ui::popNote();
    }

    // ---- PAGE 0: 联机大厅 --------------------------------------------------
    void pageSession() {
        const auto r = role();
        if (!online() && r != "connecting") {
            const float half = (ImGui::GetContentRegionAvail().x - ui::px(14)) * 0.5f;

            // Left Column: Host Room
            ImGui::BeginGroup();
            ui::cardBegin("##host_card", "开放房间", {half, 0.f}, "创建主机房间并邀请好友加入");
            ui::fieldLabel("房间名称", "好友在列表里看到的名字");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##room", "例如：四号谷地一起刷本", room, sizeof room);
            ui::vspace(ui::px(6));
            ui::fieldLabel("房间密码", "留空为公开房间");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##hostpw", "输入密码（可选）", hostPassword, sizeof hostPassword, ImGuiInputTextFlags_Password);
            ui::vspace(ui::px(6));
            ui::fieldLabel("人数上限与端口", "2–16 人 · 默认 47615");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.56f);
            ImGui::SliderInt("##limit", &limit, 2, 16, "%d 人");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt("##port", &port, 0);
            port = (std::clamp)(port, 1024, 65535);
            ui::vspace(ui::px(10));
            ImGui::Separator();
            ui::vspace(ui::px(8));

            ui::toggleSwitch("host_shared_atb", &sharedAtb, "所有人共用技力条（ATB 共享池）",
                "全队共用房主技力条与自然回复，任意玩家消耗或回复技力实时同步。");
            ui::toggleSwitch("host_pvp", &pvp, "允许 PVP 对战邀请",
                "房内玩家可互相邀请切磋，不会误伤未对战队友。");
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
                ui::vspace(ui::px(6));
            }
            ui::vspace(ui::px(4));
            if (ui::filledButton("开放房间", {-1.f, ui::px(40)})) {
                saveSettings();
                act({{"action", "host"}, {"room_name", room}, {"password", hostPassword},
                     {"port", port}, {"max_players", limit}, {"pvp", pvp},
                     {"pvp_scale", pvpScale}, {"pvp_revive", pvpRevive}, {"shared_atb", sharedAtb}});
                hostPassword[0] = 0;
            }
            ui::cardEnd();
            ImGui::EndGroup();

            ImGui::SameLine(0.f, ui::px(14));

            // Right Column: Direct Join + Saved Servers
            ImGui::BeginGroup();
            ui::cardBegin("##join_card", "直接连接好友", {half, 0.f}, "输入房主局域网或异地组网 IP");
            ui::fieldLabel("房主地址", "例：192.168.1.20:47615");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##address", "输入 IP 或 IP:端口", address, sizeof address);
            ui::vspace(ui::px(6));
            ui::fieldLabel("房间密码", "无密码留空");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##joinpw", "房间密码（可选）", joinPassword, sizeof joinPassword, ImGuiInputTextFlags_Password);
            ui::vspace(ui::px(6));
            ui::fieldLabel("收藏备注名", "可选，便于下次识别");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##srvname", "例如：好友的电脑", serverName, sizeof serverName);
            ui::vspace(ui::px(10));
            const float btnHalf = (ImGui::GetContentRegionAvail().x - ui::px(10)) * 0.5f;
            if (ui::filledButton("连接房间", {btnHalf, ui::px(40)})) join();
            ImGui::SameLine(0.f, ui::px(10));
            if (ui::tonalButton("收藏此地址", {-1.f, ui::px(40)})) {
                if (*address) {
                    act({{"action", "save_server"}, {"address", address}, {"name", *serverName ? serverName : address}});
                    say("已添加到收藏", false);
                } else say("请先输入地址", true);
            }
            ui::cardEnd();

            ui::vspace(ui::px(12));
            const auto saved = state.value("configuration", json::object()).value("saved_servers", json::array());
            ui::cardBegin("##saved_card", "收藏地址", {half, 0.f});
            if (saved.empty()) {
                ui::hint("暂无收藏地址。填入好友地址后点击“收藏此地址”即可保存。");
            } else {
                for (std::size_t i = 0; i < saved.size(); ++i) {
                    const auto& s = saved[i];
                    const auto ep = str(s, "address");
                    const auto lb = str(s, "name", ep);
                    ImGui::PushID(ep.c_str());
                    if (i > 0) { ui::vspace(ui::px(4)); ImGui::Separator(); ui::vspace(ui::px(6)); }
                    ui::pushBold();
                    ImGui::TextUnformatted(ui::ellipsize(lb, half - ui::px(170)).c_str());
                    ui::popBold();
                    ui::alignRight(ui::px(116));
                    if (ui::actionChip("填入", ui::Tone::Secondary)) { copyText(address, sizeof address, ep); joinPassword[0] = 0; }
                    ImGui::SameLine(0.f, ui::px(6));
                    if (ui::actionChip("移除", ui::Tone::Error)) act({{"action", "remove_server"}, {"address", ep}});
                    ui::pushMono();
                    ui::muted("%s", ep.c_str());
                    ui::popMono();
                    ImGui::PopID();
                }
            }
            ui::cardEnd();
            ImGui::EndGroup();

            ui::vspace(ui::px(14));

            // Bottom LAN Discovery Card
            ui::cardBegin("##lan_card", "局域网房间发现", {0.f, 0.f}, "自动扫描同一局域网内开放的终末地房间");
            if (ui::outlinedButton("刷新列表", {ui::px(108), ui::px(34)})) act({{"action", "refresh"}});
            ui::vspace(ui::px(8));
            const auto rooms = state.value("rooms", json::array());
            if (rooms.empty()) {
                ui::emptyState("暂未发现局域网房间", "让好友先开放房间，或在上方直接输入 IP:端口 加入。");
            } else {
                for (std::size_t i = 0; i < rooms.size(); ++i) {
                    const auto& item = rooms[i];
                    const auto ep = str(item, "address");
                    const bool locked = item.value("password", false);
                    ImGui::PushID(ep.c_str());
                    if (i > 0) { ui::vspace(ui::px(6)); ImGui::Separator(); ui::vspace(ui::px(8)); }
                    ui::pushBold();
                    ImGui::TextUnformatted(str(item, "name", "未命名房间").c_str());
                    ui::popBold();
                    if (locked) { ImGui::SameLine(); ui::badge("需密码", ui::Tone::Warning); }
                    ui::alignRight(ui::px(94));
                    if (ui::tonalButton("加入房间", {ui::px(94), ui::px(34)})) {
                        copyText(address, sizeof address, ep);
                        if (locked) say("该房间需要密码，请在上方输入密码后连接", false);
                        else join();
                    }
                    ui::pushNote();
                    ui::muted("房主 %s · %d/%d 人 · 地图 %s · %s",
                        str(item, "host").c_str(), item.value("players", 0), item.value("max_players", 0),
                        str(item, "map", "—").c_str(), ep.c_str());
                    ui::popNote();
                    ImGui::PopID();
                }
            }
            ui::cardEnd();
            return;
        }

        // ---- ONLINE ROOM VIEW (MATERIAL 3) ----
        const auto roomInfo = state.value("room", json::object());
        const auto pvpState = state.value("pvp", json::object());
        const auto& atbState = liveSharedAtb();

        ui::cardBegin("##room_overview", nullptr, {0.f, 0.f});
        ui::pushTitle();
        ImGui::TextUnformatted(str(roomInfo, "name", r == "connecting" ? "正在连接…" : "未命名房间").c_str());
        ui::popTitle();
        ui::vspace(ui::px(6));
        ui::statusPill(roleLabel(r), roleTone(r), true);
        char pCnt[32]{}; std::snprintf(pCnt, sizeof pCnt, "%d / %d 人", int(state.value("peers", json::array()).size()), roomInfo.value("max_players", 8));
        ui::hspace(ui::px(8)); ui::statusPill(pCnt, ui::Tone::Neutral, false);
        if (atbState.value("active", false)) {
            ui::hspace(ui::px(8)); ui::statusPill("共用技力池已启用", ui::Tone::Primary, false);
        }
        if (pvpState.value("active", false)) {
            char pvpRule[64]{}; std::snprintf(pvpRule, sizeof pvpRule, "PVP %.1fx · %s", pvpState.value("scale", 1.0), pvpState.value("revive", true) ? "回满" : "倒下");
            ui::hspace(ui::px(8)); ui::statusPill(pvpRule, ui::Tone::Error, false);
        }

        // Live Shared ATB Segmented Material 3 Progress Indicator
        if (atbState.value("active", false)) {
            ui::vspace(ui::px(14));
            const float val = float(atbState.value("value", 0.0));
            const float mx = (std::max)(1.f, float(atbState.value("max", 300.0)));
            const float rec = float(atbState.value("recover", 0.0));
            const int segs = (std::clamp)(int(std::round(mx / 100.f)), 1, 6);
            ui::pushNote();
            ui::muted("共享技力池实时状态：%.0f / %.0f  （自然回复 +%.1f/s）", val, mx, rec);
            ui::popNote();
            ui::vspace(ui::px(4));
            const ImVec2 bo = ImGui::GetCursorScreenPos();
            const float bw = ImGui::GetContentRegionAvail().x;
            ui::drawProgressBar(ImGui::GetWindowDrawList(), bo, {bo.x + bw, bo.y + ui::px(10)},
                val / mx, ui::u32(ui::md.primary), ui::u32(ui::md.surfaceContainerHighest), segs);
            ImGui::Dummy({bw, ui::px(12)});
        }

        if (r == "host") {
            ui::vspace(ui::px(10));
            ImGui::Separator();
            ui::vspace(ui::px(12));
            const float half = (ImGui::GetContentRegionAvail().x - ui::px(18)) * 0.5f;

            // Left: Room Gameplay Switches
            ImGui::BeginGroup();
            ImGui::BeginChild("##host_rules", {half, 0.f}, ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
            ui::sectionLabel("房间规则");
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

            ImGui::SameLine(0.f, ui::px(18));

            // Right: Shareable Host IP Addresses
            ImGui::BeginGroup();
            ImGui::BeginChild("##host_addrs", {half, 0.f}, ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
            ui::sectionLabel("房间地址", "发给好友直接连接");
            const auto addrs = state.value("addresses", json::array());
            if (addrs.empty()) ui::hint("正在读取本机网卡地址…");
            for (const auto& a : addrs) {
                const auto ep = str(a, "ip") + ":" + std::to_string(roomInfo.value("port", 47615));
                ImGui::PushID(ep.c_str());
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::md.surfaceContainer);
                ImGui::PushStyleColor(ImGuiCol_Border, {0, 0, 0, 0});
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ui::px(14));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(14), ui::px(10)});
                ImGui::BeginChild("##addrrow", {0.f, 0.f}, ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
                ui::pushMono();
                ImGui::TextUnformatted(ep.c_str());
                ui::popMono();
                ui::alignRight(ui::px(58));
                if (ui::actionChip("复制", ui::Tone::Secondary)) { ImGui::SetClipboardText(ep.c_str()); say("地址已复制：" + ep, false); }
                ui::pushNote();
                ui::muted("%s", str(a, "adapter").c_str());
                ui::popNote();
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
                ui::vspace(ui::px(8));
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndGroup();
        }
        ui::cardEnd();

        ui::vspace(ui::px(14));
        drawSyncCard();
    }

    void drawSyncCard() {
        const auto world = state.value("world_session", json::object());
        const auto entry = world.value("entry", json::object());
        const auto native = state.value("game", json::object()).value("world_replication", json::object());
        ui::cardBegin("##sync_card", "世界同步与传送", {0.f, 0.f});
        const bool active = entry.value("active", false);
        ui::statusPill(active ? "已进入共享世界" : "正在准备共享世界", active ? ui::Tone::Success : ui::Tone::Warning, true);
        char enBuf[48]{}; std::snprintf(enBuf, sizeof enBuf, "敌人 %u 已同步", native.value("matched", 0u));
        ui::hspace(ui::px(8)); ui::statusPill(enBuf, ui::Tone::Neutral, false);
        if (native.value("missing", 0u) > 0) {
            char msBuf[32]{}; std::snprintf(msBuf, sizeof msBuf, "缺失 %u", native.value("missing", 0u));
            ui::hspace(ui::px(8)); ui::statusPill(msBuf, ui::Tone::Error, false);
        }
        for (const auto& b : entry.value("blockers", json::array())) {
            if (b.is_string()) { ui::vspace(ui::px(4)); ui::hint("· 等待：%s", blockerText(b.get<std::string>())); }
        }
        const auto travel = str(native, "teleport");
        if (travel == "failed") { ui::vspace(ui::px(6)); ui::noticeBanner(ui::Tone::Error, ("传送失败：" + str(native, "error")).c_str()); }
        else if (!travel.empty() && travel != "idle" && travel != "complete") { ui::vspace(ui::px(4)); ui::hint("传送状态：%s", travel.c_str()); }

        const auto change = liveHostMapChange();
        if (role() == "client" && change.value("pending", false)) {
            ui::vspace(ui::px(8));
            ui::hint("待确认：房主已前往 %s（可在上方横幅点击“跟随房主”或开启“总是自动跟随”）。", hostMapDestLabel(change).c_str());
        }
        ui::cardEnd();
    }

    // ---- PAGE 1: 房间玩家 (Google Contacts / Devices Material 3 Cards) -----
    void pagePlayers() {
        const auto peers = state.value("peers", json::array());
        if (peers.empty()) {
            ui::cardBegin("##players_empty", "房间成员", {0.f, 0.f});
            ui::emptyState("当前还没有在线队友", "在“联机大厅”开放房间或加入好友房间后，这里将展示每位成员的干员、实时血量、终结技充能和传送入口。");
            ui::cardEnd();
            return;
        }
        const bool isRoomHost = role() == "host";
        const auto& vitalsMap = liveVitals();
        const auto pvpInfo = state.value("pvp", json::object());
        const bool pvpOn = pvpInfo.value("active", false);

        char cntBuf[32]{}; std::snprintf(cntBuf, sizeof cntBuf, "共 %d 位在线", int(peers.size()));
        ui::sectionLabel("房间成员", cntBuf);
        ui::vspace(ui::px(4));

        for (const auto& p : peers) {
            const int id = p.value("id", 0);
            const bool self = p.value("local", false);
            const bool isHost = p.value("host", false);
            const bool valid = p.value("valid", false);
            const bool stale = p.value("state_age_ms", 4000u) > 3000u;
            bool dueling = false;
            for (const auto& duel : pvpInfo.value("duels", json::array())) {
                if (duel.value("id", -1) == id) dueling = true;
            }
            const std::string idKey = std::to_string(id);
            json v = vitalsMap.contains(idKey) ? vitalsMap[idKey] : p.value("vitals", json::object());
            const double hp = v.value("hp", 0.0), maxHp = v.value("max_hp", 0.0);
            const double usp = v.value("usp", 0.0), maxUsp = v.value("max_usp", 0.0);
            std::string opName = str(v, "op");
            if (opName.empty()) opName = operatorDisplayName(str(p, "model"));
            const bool alive = v.value("alive", true);

            ImGui::PushID(id);
            ui::cardBegin("##pcard", nullptr, {0.f, 0.f});

            // Header row: circular M3 tonal avatar + name + badges + right pill actions
            char idStr[8]{}; std::snprintf(idStr, sizeof idStr, "%02d", id);
            ui::avatarCircle(idStr, self ? ui::Tone::Primary : (isHost ? ui::Tone::Secondary : ui::Tone::Neutral), ui::px(36));
            ImGui::SameLine(0.f, ui::px(12));

            ImGui::BeginGroup();
            ui::pushBold();
            ImGui::TextUnformatted(str(p, "name", "玩家").c_str());
            ui::popBold();
            if (isHost) { ImGui::SameLine(); ui::badge("房主", ui::Tone::Primary); }
            if (self) { ImGui::SameLine(); ui::badge("本机", ui::Tone::Secondary); }
            if (!opName.empty()) { ImGui::SameLine(); ui::badge(opName.c_str(), ui::Tone::Violet); }
            if (dueling) { ImGui::SameLine(); ui::badge("PVP 对战中", ui::Tone::Error, true); }

            ui::pushNote();
            const std::string loc = str(p, "level", str(p, "map", "—"));
            const std::string posStr = valid ? formatPosition(p.value("position", json::array())) : "—";
            const char* mapRel = self ? "本机位置" : ((!valid || stale) ? "等待同步" : (p.value("same_map", false) ? "同地图" : "其他地图"));
            char subLine[192]{};
            if (!self && p.contains("distance")) {
                std::snprintf(subLine, sizeof subLine, "%s · %s · 坐标 %s · 距离 %.1f m · %u ms",
                    loc.c_str(), mapRel, posStr.c_str(), p.value("distance", 0.f), p.value("rtt_ms", 0u));
            } else {
                std::snprintf(subLine, sizeof subLine, "%s · %s · 坐标 %s", loc.c_str(), mapRel, posStr.c_str());
            }
            ui::muted("%s", subLine);
            ui::popNote();
            ImGui::EndGroup();

            if (!self) {
                const float tpW = ui::px(88), pvpW = pvpOn ? ui::px(82) : 0.f, kickW = isRoomHost ? ui::px(64) : 0.f;
                const float totalBtnW = tpW + (pvpOn ? pvpW + ui::px(8) : 0.f) + (isRoomHost ? kickW + ui::px(8) : 0.f);
                ui::alignRight(totalBtnW);
                ImGui::BeginDisabled(!online() || !valid || stale);
                if (ui::tonalButton("传送到TA", {tpW, ui::px(32)})) act({{"action", "teleport_to_player"}, {"id", id}});
                ImGui::EndDisabled();
                if (pvpOn) {
                    ImGui::SameLine(0.f, ui::px(8));
                    if (dueling) {
                        if (ui::dangerButton("结束PVP", {pvpW, ui::px(32)})) act({{"action", "pvp_duel"}, {"op", "end"}, {"target", id}});
                    } else {
                        if (ui::outlinedButton("邀请PVP", {pvpW, ui::px(32)})) act({{"action", "pvp_duel"}, {"op", "invite"}, {"target", id}});
                    }
                }
                if (isRoomHost) {
                    ImGui::SameLine(0.f, ui::px(8));
                    if (ui::dangerButton("移出", {kickW, ui::px(32)})) { kickId = id; kickName = str(p, "name"); kickPending = true; }
                }
            }

            // Vitals bars (HP + Ultimate SP) in clean Material 3 Linear Progress style
            if (maxHp > 0.0) {
                ui::vspace(ui::px(10));
                const float hpFrac = float((std::clamp)(hp / maxHp, 0.0, 1.0));
                const float spFrac = maxUsp > 0.0 ? float((std::clamp)(usp / maxUsp, 0.0, 1.0)) : 0.f;
                const bool spReady = maxUsp > 0.0 && spFrac >= 0.995f;
                const ImVec4 hpCol = !alive ? ui::md.error : (hpFrac > 0.55f ? ui::md.tertiary : (hpFrac > 0.25f ? ui::md.warning : ui::md.error));
                const ImVec4 spCol = spReady ? ui::md.primary : ui::md.secondary;

                const float barPairW = (ImGui::GetContentRegionAvail().x - ui::px(18)) * 0.5f;
                ImDrawList* d = ImGui::GetWindowDrawList();
                const ImVec2 bo = ImGui::GetCursorScreenPos();
                ui::pushNote();
                char hpLbl[64]{}, spLbl[64]{};
                std::snprintf(hpLbl, sizeof hpLbl, "生命  %.0f / %.0f  (%.0f%%)", hp, maxHp, hpFrac * 100.f);
                if (maxUsp > 0.0) std::snprintf(spLbl, sizeof spLbl, "终结技能量  %.0f / %.0f%s", usp, maxUsp, spReady ? "  已就绪" : "");
                else std::snprintf(spLbl, sizeof spLbl, "终结技能量  —");
                d->AddText(bo, ui::u32(ui::md.onSurfaceVariant), hpLbl);
                d->AddText({bo.x + barPairW + ui::px(18), bo.y}, ui::u32(spReady ? ui::md.primary : ui::md.onSurfaceVariant), spLbl);
                const float barY = bo.y + ImGui::GetTextLineHeight() + ui::px(5);
                ui::drawProgressBar(d, {bo.x, barY}, {bo.x + barPairW, barY + ui::px(7)}, hpFrac,
                    ui::u32(hpCol), ui::u32(ui::md.surfaceContainerHighest));
                ui::drawProgressBar(d, {bo.x + barPairW + ui::px(18), barY}, {bo.x + barPairW * 2.f + ui::px(18), barY + ui::px(7)}, spFrac,
                    ui::u32(spCol), ui::u32(ui::md.surfaceContainerHighest));
                ImGui::Dummy({ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() + ui::px(13)});
                ui::popNote();
            }

            ui::cardEnd();
            ui::vspace(ui::px(10));
            ImGui::PopID();
        }
    }

    // ---- PAGE 2: 战斗统计 (Google Fit / Material You Analytics, PvE & PvP) --
    void pageCombatStats() {
        const bool pvpMode = (combatStatsTab == 1);
        const auto& stats = liveCombatStatsFor(pvpMode);
        const auto players = stats.value("players", json::array());
        const bool inCombat = stats.value("in_combat", false);
        const double totalDmg = stats.value("total_damage", 0.0);
        const double teamDps  = stats.value("team_dps", 0.0);
        const double durS     = stats.value("duration_s", 0.0);
        const auto totalHits  = stats.value("total_hits", std::uint64_t(0));
        const auto totalKills = stats.value("total_kills", std::uint64_t(0));

        // Top mode selector (PvE vs PvP) + status + actions
        if (pvpMode) {
            if (ui::tonalButton("对怪物 (PvE)", {ui::px(118), ui::px(34)})) combatStatsTab = 0;
            ImGui::SameLine(0.f, ui::px(8));
            ui::dangerButton("对玩家 (PVP)", {ui::px(118), ui::px(34)});
        } else {
            ui::filledButton("对怪物 (PvE)", {ui::px(118), ui::px(34)});
            ImGui::SameLine(0.f, ui::px(8));
            if (ui::tonalButton("对玩家 (PVP)", {ui::px(118), ui::px(34)})) combatStatsTab = 1;
        }
        ImGui::SameLine(0.f, ui::px(12));
        ui::statusPill(inCombat ? (pvpMode ? "PVP 统计进行中" : "战斗统计进行中") : (players.empty() ? "等待战斗记录" : "统计已暂停（脱战）"),
            inCombat ? (pvpMode ? ui::Tone::Error : ui::Tone::Primary) : (players.empty() ? ui::Tone::Neutral : ui::Tone::Success), true);

        const std::string rstLbl = std::string(pvpMode ? "重置 PVP" : "重置怪物") + " (" + fKeyName(damageResetKey) + ")";
        const float shareW = ui::px(108), rstW = ui::buttonWidth(rstLbl.c_str());
        ui::alignRight(shareW + rstW + ui::px(10));
        ImGui::BeginDisabled(!online() || players.empty());
        if (ui::outlinedButton("发送到聊天", {shareW, ui::px(34)})) {
            act({{"action", "combat_stats_share"}, {"scope", pvpMode ? "pvp" : "pve"}});
            say(pvpMode ? "已将 PVP 伤害统计发送到房间聊天" : "已将对怪物伤害统计发送到房间聊天", false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.f, ui::px(10));
        if (ui::dangerButton(rstLbl.c_str(), {rstW, ui::px(34)})) {
            act({{"action", "combat_stats_reset"}, {"scope", pvpMode ? "pvp" : "pve"}});
            say(pvpMode ? "PVP 伤害统计已重置" : "对怪物伤害统计已重置", false);
        }

        ui::vspace(ui::px(12));

        // 4 Harmonized Material You Tonal KPI Cards
        const float kpiW = (ImGui::GetContentRegionAvail().x - ui::px(12) * 3.f) * 0.25f;
        char v1[32]{}, v2[32]{}, v3[32]{}, v4[32]{}, s1[48]{}, s4[48]{};
        std::snprintf(v1, sizeof v1, "%s", formatDamageNumber(totalDmg).c_str());
        std::snprintf(s1, sizeof s1, "%d 位参战玩家", int(players.size()));
        std::snprintf(v2, sizeof v2, "%s/s", formatDamageNumber(teamDps).c_str());
        std::snprintf(v3, sizeof v3, "%.1f s", durS);
        std::snprintf(v4, sizeof v4, "%llu", static_cast<unsigned long long>(totalHits));
        std::snprintf(s4, sizeof s4, pvpMode ? "击败 %llu 次对手" : "击杀 %llu 名敌人", static_cast<unsigned long long>(totalKills));

        ui::kpiCard("##kpi1", pvpMode ? "PVP 总伤害" : "对怪物总伤害", v1, s1, pvpMode ? ui::Tone::Error : ui::Tone::Primary, kpiW);
        ImGui::SameLine(0.f, ui::px(12));
        ui::kpiCard("##kpi2", pvpMode ? "PVP 平均 DPS" : "团队平均 DPS", v2, inCombat ? "实时采样中" : "已按有效时长结算", ui::Tone::Secondary, kpiW);
        ImGui::SameLine(0.f, ui::px(12));
        ui::kpiCard("##kpi3", "有效战斗时长", v3, damageAutoReset ? "脱战 15s 自动重置" : "手动重置分段", ui::Tone::Success, kpiW);
        ImGui::SameLine(0.f, ui::px(12));
        ui::kpiCard("##kpi4", "累计命中次数", v4, s4, ui::Tone::Violet, kpiW);

        ui::vspace(ui::px(14));

        // 30-second Rolling DPS Bar Chart (Google Fit style)
        ui::cardBegin("##dps_curve", pvpMode ? "最近 30 秒 PVP 秒伤趋势" : "最近 30 秒对怪物团队秒伤趋势", {0.f, 0.f});
        std::vector<double> tl;
        if (stats.contains("timeline") && stats["timeline"].is_array()) {
            for (const auto& pt : stats["timeline"]) if (pt.is_number()) tl.push_back(pt.get<double>());
        }
        if (tl.empty()) tl.assign(30, 0.0);
        ui::dpsSparkline(tl, ui::px(82));
        ui::cardEnd();

        ui::vspace(ui::px(14));

        // Player Breakdown Table / Cards
        ui::cardBegin("##dmg_rank", pvpMode ? "PVP 对玩家伤害排行" : "全队对怪物伤害贡献排行", {0.f, 0.f});
        if (players.empty()) {
            ui::emptyState(pvpMode ? "暂无 PVP 伤害数据" : "暂无对怪物伤害数据",
                pvpMode ? "开启房间 PVP 并同意切磋后，将自动统计双方互相造成的实际伤害、DPS、暴击率、击败次数及承伤。"
                        : "攻击怪物后将自动统计房主与所有队友的总伤、DPS、暴击率、最高单次伤害及元素占比。");
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
                if (idx > 0) { ui::vspace(ui::px(8)); ImGui::Separator(); ui::vspace(ui::px(10)); }

                // Top header strip: rank pill + name + op + right metrics
                char rankBuf[16]{}; std::snprintf(rankBuf, sizeof rankBuf, idx == 0 ? "MVP #1" : "#%d", idx + 1);
                ui::badge(rankBuf, idx == 0 ? ui::Tone::Primary : (idx == 1 ? ui::Tone::Secondary : ui::Tone::Neutral), idx == 0);
                ImGui::SameLine(0.f, ui::px(8));
                ui::pushBold();
                ImGui::TextUnformatted(str(p, "name", "玩家").c_str());
                ui::popBold();
                const std::string opTag = str(p, "op");
                if (!opTag.empty()) { ImGui::SameLine(0.f, ui::px(6)); ui::badge(opTag.c_str(), ui::Tone::Violet); }

                char rightMetrics[96]{};
                std::snprintf(rightMetrics, sizeof rightMetrics, "%s · %s/s · %.1f%%",
                    formatDamageNumber(dmg).c_str(), formatDamageNumber(dps).c_str(), share * 100.0);
                ui::pushBold();
                ui::alignRight(ImGui::CalcTextSize(rightMetrics).x);
                ImGui::PushStyleColor(ImGuiCol_Text, idx == 0 ? ui::md.primary : ui::md.onSurface);
                ImGui::TextUnformatted(rightMetrics);
                ImGui::PopStyleColor();
                ui::popBold();

                // Clean Material 3 linear contribution bar
                ui::vspace(ui::px(5));
                const ImVec2 bo = ImGui::GetCursorScreenPos();
                const float bw = ImGui::GetContentRegionAvail().x;
                ui::drawProgressBar(ImGui::GetWindowDrawList(), bo, {bo.x + bw, bo.y + ui::px(7)},
                    ratio, ui::u32(rCol), ui::u32(ui::md.surfaceContainerHighest));
                ImGui::Dummy({bw, ui::px(9)});

                ui::pushNote();
                ui::muted(pvpMode ? "命中 %llu · 暴击率 %.1f%% · 最高单次 %s · 击败 %llu · 承伤 %s"
                                  : "命中 %llu · 暴击率 %.1f%% · 最高单次 %s · 击杀 %llu · 承伤 %s",
                    static_cast<unsigned long long>(hits), critRate * 100.0,
                    formatDamageNumber(maxHit).c_str(), static_cast<unsigned long long>(kills),
                    formatDamageNumber(taken).c_str());

                // Clean Material 3 element breakdown pills
                const auto byType = p.value("by_type", json::object());
                if (byType.is_object() && !byType.empty() && dmg > 0.0) {
                    ui::vspace(ui::px(5));
                    std::vector<std::pair<std::string, double>> elems;
                    for (auto it = byType.begin(); it != byType.end(); ++it) {
                        if (it.value().is_number() && it.value().get<double>() > 0.0) elems.emplace_back(it.key(), it.value().get<double>());
                    }
                    std::sort(elems.begin(), elems.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                    ImDrawList* d = ImGui::GetWindowDrawList();
                    bool first = true;
                    for (const auto& [ek, ev] : elems) {
                        if (!first) ImGui::SameLine(0.f, ui::px(6));
                        first = false;
                        char elBuf[48]{};
                        std::snprintf(elBuf, sizeof elBuf, "%s %.0f%%", damageTypeLabel(ek), (ev / dmg) * 100.0);
                        const ImVec4 ec = ui::damageElementColor(ek);
                        const ImVec2 eo = ImGui::GetCursorScreenPos();
                        const ImVec2 es = ImGui::CalcTextSize(elBuf);
                        const float eh = es.y + ui::px(5);
                        const ImVec2 em{eo.x + es.x + ui::px(14), eo.y + eh};
                        d->AddRectFilled(eo, em, ui::u32(ec, ui::md.dark ? 0.18f : 0.11f), eh * 0.5f);
                        d->AddText({eo.x + ui::px(7), eo.y + ui::px(2.5f)}, ui::u32(ec), elBuf);
                        ImGui::Dummy({em.x - eo.x, eh});
                    }
                }
                ui::popNote();
                ImGui::PopID();
                ++idx;
            }
        }
        ui::cardEnd();
    }

    // ---- PAGE 3: 房间聊天 (Google Messages / Chat Material 3 Style) --------
    void pageChat() {
        const float bottomReserve = ui::px(116);
        ui::cardBegin("##chat_surface", "房间聊天", {0.f, -bottomReserve}, online() ? "Enter 快捷发送" : "离线时不可发送消息");
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
                ui::vspace(ui::px(4));
                continue;
            }
            ui::pushBold();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::md.primary);
            ImGui::TextUnformatted((str(line, "name", "玩家") + "：").c_str());
            ImGui::PopStyleColor();
            ui::popBold();
            ImGui::SameLine(0.f, ui::px(4));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextWrapped("%s", str(line, "text").c_str());
            ImGui::PopTextWrapPos();
            ui::vspace(ui::px(5));
        }
        if (newest != lastChat) { ImGui::SetScrollHereY(1.0f); lastChat = newest; }
        ui::cardEnd();

        // Quick tactical phrases (Material 3 Assist Chips)
        ui::vspace(ui::px(10));
        ImGui::BeginDisabled(!online());
        static const char* phrases[] = {"准备开怪", "等一下技力", "终结技已就绪", "传送过来集合"};
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine(0.f, ui::px(8));
            if (ui::actionChip(phrases[i], ui::Tone::Neutral)) act({{"action", "chat"}, {"text", phrases[i]}});
        }
        ImGui::SameLine(0.f, ui::px(8));
        if (ui::actionChip("播报伤害统计", ui::Tone::Primary)) act({{"action", "combat_stats_share"}});

        ui::vspace(ui::px(8));
        ImGui::SetNextItemWidth(-ui::px(104));
        bool send = ImGui::InputTextWithHint("##chat", "输入消息，按 Enter 发送…", chat, sizeof chat, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(0.f, ui::px(10));
        send |= ui::filledButton("发送", {-1.f, ui::px(36)});
        if (send && *chat) {
            act({{"action", "chat"}, {"text", chat}});
            chat[0] = 0;
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::EndDisabled();
    }

    // ---- PAGE 4: 外观与设置 (Pixel / Android Settings Material 3 Cards) ----
    void pageSettings() {
        const float half = (ImGui::GetContentRegionAvail().x - ui::px(14)) * 0.5f;

        // Top 2-column row: Identity/Keys + Appearance & HUD
        ImGui::BeginGroup();
        ui::cardBegin("##set_player", "玩家身份与快捷键", {half, 0.f});
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

        ui::vspace(ui::px(12));
        if (ui::filledButton("保存名称与快捷键", {-1.f, ui::px(38)})) saveSettings();
        ui::cardEnd();
        ImGui::EndGroup();

        ImGui::SameLine(0.f, ui::px(14));

        ImGui::BeginGroup();
        ui::cardBegin("##set_hud", "外观与悬浮 HUD", {half, 0.f});
        bool changed = false;
        changed |= ui::toggleSwitch("sw_darkmode", &darkMode, "菜单深色主题（Material 3 Dark）", "也可以点击右上角月亮/太阳图标一键切换。");
        ui::fieldLabel("HUD 风格预设");
        ImGui::SetNextItemWidth(-1);
        static const char* kHudStyles[] = {
            "无框战术准星（终末地 · 默认）",
            "工业切角框（终末地）",
            "塔卫二高反差条（终末地）",
            "圆角 Material 玻璃"
        };
        static const char* kHudStyleHints[] = {
            "去大黑框，保留 L 型战术角标、渐隐托底与极细直角光带，最贴近实机画面。",
            "45° 工业斜切角 + 炭黑半透底 + 斜切分段技力条，亮暗场景均清晰稳重。",
            "高反差黑白黄撞色 + 实心标题带 + 斜纹警戒角标 ///，扫视识别度最强。",
            "圆角半透明 Material 3 磨砂玻璃与胶囊进度条，与菜单风格统一。"
        };
        int styleIdx = (std::clamp)(int(hudStyle), 0, 3);
        if (ImGui::Combo("##hudstyle", &styleIdx, kHudStyles, 4)) {
            hudStyle = static_cast<ui::HudStyle>(styleIdx);
            changed = true;
        }
        ui::hint(kHudStyleHints[styleIdx]);
        ui::vspace(ui::px(6));
        changed |= ui::toggleSwitch("sw_markers", &markers, "显示玩家头顶标记与边缘箭头");
        changed |= ui::toggleSwitch("sw_teamhud", &showTeamHud, "显示右侧队友信息列表", "显示队友编号、距离、血量与终结技能量微条。");
        changed |= ui::toggleSwitch("sw_dmghud", &showDamageHud, "显示实时悬浮伤害小窗", "按住 Alt 可拖动窗口位置或点击重置。");
        changed |= ui::toggleSwitch("sw_atbhud", &showAtbHud, "显示共享技力条同步提示", "开启共用技力池时在顶部展示分段技力条与消耗/回复播报。");
        changed |= ui::toggleSwitch("sw_compact", &hudCompact, "HUD 紧凑模式", "隐藏附加微条、缩窄悬浮小窗以减少遮挡。");
        changed |= ui::toggleSwitch("sw_autorst", &damageAutoReset, "脱战 15 秒后自动开启新一轮伤害统计");
        ui::fieldLabel("HUD 透明度");
        ImGui::SetNextItemWidth(-1);
        int opPct = int(std::round(hudOpacity * 100.f));
        if (ImGui::SliderInt("##hudop", &opPct, 30, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            hudOpacity = float(opPct) / 100.f;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        ui::vspace(ui::px(8));
        if (ui::outlinedButton("重置悬浮伤害小窗位置", {-1.f, ui::px(34)})) {
            resetHudPosRequested = true;
            say("已重置悬浮窗位置", false);
        }
        ui::cardEnd();
        ImGui::EndGroup();

        ui::vspace(ui::px(14));

        // Bottom 2-column row: Coop Sync Switches + Emergency/Testing Tools
        ImGui::BeginGroup();
        ui::cardBegin("##set_coop", "联机画面与战斗同步", {half, 0.f}, "勾选项修改后立即生效");
        changed |= ui::toggleSwitch("sw_remote", &remote, "显示其他玩家的角色");
        ImGui::BeginDisabled(!remote);
        changed |= ui::toggleSwitch("sw_squad", &squad, "同时显示队友队伍中的另外三名干员");
        changed |= ui::toggleSwitch("sw_replay", &replay, "重放其他玩家的技能动作、武器与特效");
        changed |= ui::toggleSwitch("sw_outline", &outline, "给队友角色添加轮廓描边");
        ImGui::EndDisabled();
        ImGui::Separator();
        ui::vspace(ui::px(6));
        changed |= ui::toggleSwitch("sw_follow", &follow, "作为房客时跟随房主地图");
        ImGui::BeginDisabled(!follow);
        changed |= ui::toggleSwitch("sw_autofollow", &autoFollowHostMap, "房主切换地图时自动跟随（免弹窗确认）", "开启后房主切图或进副本时自动跟随，不再弹出确认提示。");
        ImGui::EndDisabled();
        changed |= ui::toggleSwitch("sw_combat", &combat, "同步战斗（共同攻击房主世界敌人）");
        ui::cardEnd();
        ImGui::EndGroup();

        ImGui::SameLine(0.f, ui::px(14));

        ImGui::BeginGroup();
        ui::cardBegin("##set_tools", "应急脱离与测试工具", {half, 0.f});
        ui::hint("若房客场景异常，可先安全退出房间并通过官方传送回到四号谷地枢纽。");
        ui::vspace(ui::px(8));
        if (ui::tonalButton("应急回到四号谷地", {-1.f, ui::px(36)})) act({{"action", "emergency_home"}});
        const auto tp = state.value("game", json::object()).value("world_replication", json::object()).value("official_teleport", json::object());
        if (tp.contains("error")) {
            ui::vspace(ui::px(6));
            ui::noticeBanner(ui::Tone::Error, ("官方传送失败：" + str(tp, "error")).c_str());
        }
        ui::vspace(ui::px(12));
        ImGui::Separator();
        ui::vspace(ui::px(8));
        bool invincible = state.value("testing_invincible", false);
        if (ui::toggleSwitch("sw_invincible", &invincible, "测试：本机角色无敌", "仅作用于本机干员，不影响队友替身或怪物；重启后保持。")) {
            act({{"action", "testing_invincible"}, {"enabled", invincible}});
        }
        ui::cardEnd();
        ImGui::EndGroup();

        if (changed) applySwitches();
    }

    // ---- PAGE 5: 运行诊断 (Android About / Diagnostics Card Layout) --------
    void pageDiagnostics() {
        const auto g = state.value("game", json::object());
        const auto native = g.value("world_replication", json::object());
        const auto world = state.value("world_session", json::object());
        const auto entry = world.value("entry", json::object());
        const auto combatState = native.value("combat", json::object());
        const auto squadState = combatState.value("squad", native.value("squad", json::object()));
        const auto& atb = liveSharedAtb();
        auto num = [](const json& j, const char* k) { return std::to_string(j.value(k, std::uint64_t(0))); };

        const float half = (ImGui::GetContentRegionAvail().x - ui::px(14)) * 0.5f;

        ImGui::BeginGroup();
        ui::cardBegin("##diag_hooks", "游戏接入与钩子状态", {half, 0.f});
        if (ImGui::BeginTable("##dh", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("k", 0, 1.0f); ImGui::TableSetupColumn("v", 0, 1.8f);
            ui::kvRow("游戏接入", state.value("hook_ready", false) ? "已连接" : "等待场景", state.value("hook_ready", false) ? ui::Tone::Success : ui::Tone::Warning);
            ui::kvRow("地图 / 关卡", str(g, "map_id", "—") + " / " + str(g, "level_id", "—"));
            ui::kvRow("本机角色", g.value("local_valid", false) ? ("已读取（骨骼 " + std::to_string(g.value("local_bones", 0)) + "）") : "未读取",
                g.value("local_valid", false) ? ui::Tone::Success : ui::Tone::Warning);
            ui::kvRow("共享世界", str(entry, "phase", "offline"), entry.value("active", false) ? ui::Tone::Success : ui::Tone::Warning);
            ui::kvRow("房客隔离", native.value("guest_policy", json::object()).value("ready", false) ? "就绪" : str(native.value("guest_policy", json::object()), "error", "—"));
            ui::kvRow("战斗过滤", state.value("op_filter_ready", false) ? "就绪" : str(state, "op_filter_note", "—"),
                state.value("op_filter_ready", false) ? ui::Tone::Success : ui::Tone::Warning);
            const bool atbReady = state.value("atb_cost_hook_ready", false) && state.value("atb_set_hook_ready", false);
            ui::kvRow("技力条钩子", atbReady ? "就绪 (Cost/Set/Update/Reset)" : "等待挂接", atbReady ? ui::Tone::Success : ui::Tone::Warning);
            ui::kvRow("版本", "EF Link " EFLINK_VERSION " · Windows x64");
            ImGui::EndTable();
        }
        ui::cardEnd();
        ImGui::EndGroup();

        ImGui::SameLine(0.f, ui::px(14));

        ImGui::BeginGroup();
        ui::cardBegin("##diag_net", "战斗、技力与网络同步", {half, 0.f});
        if (ImGui::BeginTable("##dn", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("k", 0, 1.0f); ImGui::TableSetupColumn("v", 0, 1.8f);
            ui::kvRow("敌人同步", num(native, "matched") + " 已同步 / " + num(native, "missing") + " 缺失");
            ui::kvRow("命中结算", num(combatState, "guest_hits_sent") + " 发出 / " + num(combatState, "room_hits_applied") + " 结算");
            ui::kvRow("技能重放", num(combatState, "player_casts_sent") + " 发 / " + num(combatState, "player_casts_mirrored") + " 播 / " + num(combatState, "player_casts_rejected") + " 拒");
            ui::kvRow("共享技力", std::string(atb.value("active", false) ? "启用" : "未启用") + " · 同步 " +
                num(atb, "syncs_sent") + "/" + num(atb, "syncs_applied") + " · 增量 " + num(atb, "deltas_sent") + "/" + num(atb, "deltas_applied"));
            ui::kvRow("队伍成员", num(squadState, "poses_sent") + " 发出 / " + num(squadState, "poses_in") + " 收到 / " + std::to_string(squadState.value("remote", json::array()).size()) + " 远端");
            ui::kvRow("网络延迟", std::to_string(state.value("rtt_ms", 0u)) + " ms");
            const auto traffic = state.value("traffic", json::object());
            if (traffic.value("tcp_only_links", 0u) > 0) {
                ui::kvRow("UDP 状态", std::to_string(traffic.value("tcp_only_links", 0u)) + " 条连接走 TCP 备用通道", ui::Tone::Warning);
            } else {
                ui::kvRow("UDP 状态", traffic.value("udp_links", 0u) > 0 ? "已连通" : "—", traffic.value("udp_links", 0u) > 0 ? ui::Tone::Success : ui::Tone::Neutral);
            }
            const auto guiInfo = state.value("gui_render", json::object());
            ui::kvRow("界面后端", str(guiInfo, "backend", "overlay_dcomp") + " · " + num(guiInfo, "frames") + " 帧");
            ImGui::EndTable();
        }
        ui::cardEnd();
        ImGui::EndGroup();

        std::vector<std::string> errors;
        for (const auto& e : {str(world, "error"), str(native, "error"), str(g, "error"), str(state, "last_error"), str(atb, "error"), str(combatState, "player_cast_error")}) {
            if (!e.empty()) errors.push_back(e);
        }
        if (!errors.empty()) {
            ui::vspace(ui::px(14));
            ui::cardBegin("##diag_err", "最近异常日志", {0.f, 0.f});
            for (const auto& e : errors) { ui::noticeBanner(ui::Tone::Error, e.c_str()); ui::vspace(ui::px(6)); }
            ui::cardEnd();
        }
    }

    // ---- Material 3 Alert Dialogs (28px rounded, right-aligned buttons) ----
    void popups() {
        if (leaveConfirm) { ImGui::OpenPopup("##leave"); leaveConfirm = false; }
        if (kickPending)  { ImGui::OpenPopup("##kick");  kickPending = false; }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui::px(24), ui::px(22)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::px(26));
        if (ImGui::BeginPopupModal("##leave", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ui::pushTitle();
            ImGui::TextUnformatted(role() == "host" ? "关闭房间？" : "离开房间？");
            ui::popTitle();
            ui::vspace(ui::px(8));
            ui::muted("%s", role() == "host" ? "关闭后房间内所有玩家都会断开连接。" : "离开后将自动返回进入房间前的位置。");
            ui::vspace(ui::px(20));
            ui::alignRight(ui::px(176));
            if (ui::textButton("取消", {ui::px(76), ui::px(36)})) ImGui::CloseCurrentPopup();
            ImGui::SameLine(0.f, ui::px(8));
            if (ui::dangerButton("确认", {ui::px(92), ui::px(36)})) { act({{"action", "leave"}}); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("##kick", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ui::pushTitle();
            ImGui::Text("移出玩家 #%d？", kickId);
            ui::popTitle();
            if (!kickName.empty()) { ui::vspace(ui::px(6)); ui::muted("将断开与 %s 的联机连接。", kickName.c_str()); }
            ui::vspace(ui::px(20));
            ui::alignRight(ui::px(176));
            if (ui::textButton("取消", {ui::px(76), ui::px(36)})) { kickId = -1; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(0.f, ui::px(8));
            if (ui::dangerButton("移出", {ui::px(92), ui::px(36)})) { act({{"action", "kick"}, {"id", kickId}}); kickId = -1; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
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
        ui::applyTheme(displayScale, false);
        ui::fonts = ui::loadFonts(io.Fonts, displayScale);
        io.FontDefault = ui::fonts.body;

        ImGui_ImplWin32_Init(surface.window);
        ImGui_ImplDX11_Init(surface.device.Get(), surface.context.Get());

        Ui ui;
        ui.displayScale = displayScale;
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
            if (openLobbyRequested.exchange(false)) ui.page = 0;
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

    const bool previewDark = options.value("preview_dark", false);
    IMGUI_CHECKVERSION();
    auto* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    ui::applyTheme(1.0f, previewDark);
    ui::fonts = ui::loadFonts(io.Fonts, 1.0f);
    io.FontDefault = ui::fonts.body;

    ImGui_ImplWin32_Init(surface.window);
    ImGui_ImplDX11_Init(surface.device.Get(), surface.context.Get());

    std::string demoRole = str(options, "preview_demo");
    if (options.value("preview_hud", false) && demoRole.empty()) demoRole = "host";
    const std::string previewHudStyle = options.value("preview_hud_style", std::string("telemetry"));
    json demoState = demoRole.empty() ? json() : demoStatus(demoRole, previewDark, previewHudStyle);
    json demoHudData = demoRole.empty() ? json() : demoHud(demoState);

    Ui ui;
    ui.displayScale = 1.0f;
    ui.darkMode = previewDark;
    ui.appliedDarkMode = previewDark;
    ui.hudStyle = ui::hudStyleFromKey(previewHudStyle);
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
    if (options.value("preview_hud", false)) {
        // Ensure demo chat messages are visible in HUD preview captures
        ui.chatPrimed = true;
        if (options.value("preview_chat", false)) {
            ui.chatting = true;
            ui.chatFocus = true;
        }
    }

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
        if (!ui.open) ui.drawHud(size);
        if (ui.open) ui.drawMenu(size);
        if (ui.closeRequested) ui.closeRequested = false;
        ImGui::Render();
        // Neutral game-like dark backdrop so transparent HUD and Material 3 floating sheet read clearly.
        const float clear[4]{0.09f, 0.11f, 0.13f, 1.0f};
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
