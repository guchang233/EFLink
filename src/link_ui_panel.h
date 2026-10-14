#pragma once
// The EF Link menu. The in-game Unity canvas, the standalone preview window and
// the headless UI test all draw this, so layout, copy and the action contract
// live here while input capture and rendering stay in the back ends.
#include "link_settings.h"
#include "link_version.h"
#include "link_ui_theme.h"
#include "link_ui_widgets.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace eflink {
using nlohmann::json;

// ---- type-safe readers -----------------------------------------------------
// The menu is the only thing between a bad status payload and the player, and a
// throw here used to cost the whole session its menu. Every read therefore goes
// through an accessor that falls back instead of throwing.
namespace uid {
inline const json& emptyArray() {static const json value=json::array();return value;}
inline const json& emptyObject() {static const json value=json::object();return value;}
inline const json& arrayAt(const json& node,const char* key) {
    const auto found=node.is_object()?node.find(key):node.end();
    return found==node.end()||!found->is_array()?emptyArray():*found;
}
inline const json& objectAt(const json& node,const char* key) {
    const auto found=node.is_object()?node.find(key):node.end();
    return found==node.end()||!found->is_object()?emptyObject():*found;
}
inline bool hasAt(const json& node,const char* key) {return node.is_object()&&node.contains(key);}
inline std::string stringAt(const json& node,const char* key,const char* fallback="") {
    const auto found=node.is_object()?node.find(key):node.end();
    return found==node.end()||!found->is_string()?fallback:found->get<std::string>();
}
inline bool boolAt(const json& node,const char* key,bool fallback) {
    const auto found=node.is_object()?node.find(key):node.end();
    return found==node.end()||!found->is_boolean()?fallback:found->get<bool>();
}
inline double numberAt(const json& node,const char* key,double fallback) {
    const auto found=node.is_object()?node.find(key):node.end();
    return found==node.end()||!found->is_number()?fallback:found->get<double>();
}
inline int intAt(const json& node,const char* key,int fallback) {return int(numberAt(node,key,fallback));}
inline unsigned uintAt(const json& node,const char* key,unsigned fallback) {return unsigned(numberAt(node,key,fallback));}
inline float floatAt(const json& node,const char* key,float fallback) {return float(numberAt(node,key,fallback));}
inline std::uint64_t serialAt(const json& node,const char* key,std::uint64_t fallback) {
    return static_cast<std::uint64_t>(numberAt(node,key,double(fallback)));
}
inline std::vector<json> objectsAt(const json& node,const char* key) {
    std::vector<json> result;
    for(const auto& element:arrayAt(node,key)) if(element.is_object()) result.push_back(element);
    return result;
}
inline std::vector<std::string> stringsAt(const json& node,const char* key) {
    std::vector<std::string> result;
    for(const auto& element:arrayAt(node,key)) if(element.is_string()) result.push_back(element.get<std::string>());
    return result;
}
}

inline std::string roleText(const std::string& role) {
    if(role=="host") return "房主";
    if(role=="client") return "已连接";
    if(role=="connecting") return "连接中";
    return "离线";
}
inline ui::Tone roleTone(const std::string& role) {
    if(role=="host") return ui::Tone::Accent;
    if(role=="client") return ui::Tone::Success;
    if(role=="connecting") return ui::Tone::Warning;
    return ui::Tone::Neutral;
}

struct UiPanel {
    using Request=std::function<json(const json&)>;
    Request request;
    json state;
    std::string feedback;
    int tab=0;
    bool close=false;
    // Diagnostics for the layout test: rail() sets this when the navigation
    // column needs a scrollbar, which means the bottom-pinned hint no longer
    // fits. A bad metric then fails the test instead of silently clipping text.
    // railHeight/railSlack are kept so a failure reports the numbers instead of
    // just "it broke".
    bool railOverflow=false;
    float railHeight=0.f,railSlack=0.f;
    std::uint64_t lastMapPrompt=0;
    char name[65]{},room[65]{},hostPassword[129]{},address[257]{},joinPassword[129]{},serverName[129]{},chat[1001]{};
    int port=47615,limit=8,key=kDefaultMenuKey;
    bool remote=true,markers=true,follow=true,combat=true;
    std::uint64_t lastRefresh=0,lastChat=0;
    // ---- window geometry ---------------------------------------------------
    // The menu is a normal ImGui window: the player drags it by the title strip
    // and resizes it from its edges. ImGui owns the rectangle between frames, so
    // the panel only seeds it (first frame, display change, or after the menu was
    // closed for a while) and otherwise reads back whatever the player settled
    // on. Remembering those numbers is what makes a re-opened menu come back
    // where it was left instead of jumping back to the middle of the screen.
    bool layoutReady=false,dragHovered=false,dragActive=false;
    ImVec2 layoutPos{},layoutSize{},layoutDisplay{1920.f,1080.f};
    std::uint64_t layoutFrame=0;
    // Hit rectangle of the drag strip, in screen space. Exposed so the layout
    // test can press exactly where the player would instead of re-deriving the
    // window padding by hand, and so a failure reports real numbers.
    ImVec2 dragMin{},dragMax{};

    json call(const json& input) {
        try {
            auto result=request(input);
            if(result.contains("error")) feedback=result["error"].get<std::string>();
            else {state=std::move(result);if(input.value("action",std::string{})!="status") feedback.clear();}
        } catch(const std::exception& e) {feedback=e.what();}
        return state;
    }
    void refresh() {
        if(GetTickCount64()-lastRefresh<250) return;
        lastRefresh=GetTickCount64();call({{"action","status"}});
        if(initialized||state.empty()) return;
        initialized=true;
        copyText(name,sizeof name,uid::stringAt(state,"name","管理员"));
        const auto& config=configuration();
        copyText(room,sizeof room,uid::stringAt(config,"room_name"));
        port=uid::intAt(config,"port",47615);limit=uid::intAt(config,"max_players",8);
        key=uid::intAt(config,"menu_key",kDefaultMenuKey);
        remote=uid::boolAt(config,"show_remote_players",true);markers=uid::boolAt(config,"show_player_markers",true);
        follow=uid::boolAt(config,"follow_host_world",true);combat=uid::boolAt(config,"sync_combat",true);
    }
    void join() {if(*address) call({{"action","join"},{"address",address},{"password",joinPassword}});else feedback="请输入房主地址";}
    void save() {
        // save() can be reached before the first status payload has loaded the
        // configuration (the buffers are zero-initialised). Pushing an empty
        // player name would wipe the module's identity, so refuse and explain.
        if(!*name) {feedback="请输入玩家名";return;}
        call({{"action","settings"},{"configuration",{{"player_name",name},{"room_name",room},{"port",port},{"max_players",limit},{"menu_key",key},
            {"show_remote_players",remote},{"show_player_markers",markers},{"follow_host_world",follow},{"sync_combat",combat}}}});
    }

    static void copyText(char* destination,std::size_t capacity,const std::string& text) {
        strncpy_s(destination,capacity,text.c_str(),_TRUNCATE);
    }

    // ---- state ------------------------------------------------------------
    std::string role() const {return uid::stringAt(state,"role","offline");}
    bool online() const {const auto value=role();return value=="host"||value=="client";}
    bool offline() const {return role()=="offline";}
    const json& configuration() const {return uid::objectAt(state,"configuration");}
    const json& roomInfo() const {return uid::objectAt(state,"room");}
    const json& worldSession() const {return uid::objectAt(state,"world_session");}
    const json& entry() const {return uid::objectAt(worldSession(),"entry");}
    const json& replication() const {return uid::objectAt(uid::objectAt(state,"game"),"world_replication");}
    const json& mapChange() const {return uid::objectAt(replication(),"host_map_change");}
    std::vector<json> peers() const {return uid::objectsAt(state,"peers");}
    std::string roomTitle() const {const auto text=uid::stringAt(roomInfo(),"name");return text.empty()?"未加入房间":text;}
    bool mapChangePending() const {return role()=="client"&&uid::boolAt(mapChange(),"pending",false);}
    bool hostSceneSupported() const {
        for(const auto& blocker:uid::stringsAt(entry(),"blockers")) if(blocker=="host_scene_rules") return false;
        return true;
    }

    // ---- chrome -----------------------------------------------------------
    void header() {
        constexpr const char* closeLabel="返回游戏  ×";
        const float closeWidth=ui::buttonWidth(closeLabel);
        // The title doubles as the window's drag handle. The label goes straight
        // into the draw list and the hit target is an invisible button, because
        // ImGui only lets the *first* item under the cursor claim the hover id --
        // a Text() here would keep the strip from ever being hoverable. The strip
        // stops short of the close button so that stays clickable.
        ui::pushTitle();
        const float rowHeight=ImGui::GetTextLineHeight();
        const float titleWidth=ImGui::CalcTextSize("EF LINK").x;
        const ImVec2 stripOrigin=ImGui::GetCursorScreenPos();
        const float stripWidth=(std::max)(titleWidth+ui::px(16),ImGui::GetContentRegionAvail().x-closeWidth-ui::px(12));
        ImGui::InvisibleButton("##window_drag",{stripWidth,rowHeight});
        dragHovered=ImGui::IsItemHovered();
        dragActive=ImGui::IsItemActive();
        dragMin=ImGui::GetItemRectMin();dragMax=ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),stripOrigin,ui::u32(ui::kAccent),"EF LINK");
        if(dragHovered||dragActive) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            // Two rows of grip dots: the only hint that this line moves the window.
            ImDrawList* list=ImGui::GetWindowDrawList();
            const float x=stripOrigin.x+titleWidth+ui::px(14);
            const ImU32 color=ui::u32(ui::kTextMuted,dragActive?.95f:.6f);
            for(int row=0;row<2;++row) for(int column=0;column<3;++column)
                list->AddCircleFilled({x+column*ui::px(5),stripOrigin.y+rowHeight*.5f+(row?ui::px(3):-ui::px(3))},ui::px(1.1f),color,8);
        }
        ui::popTitle();
        ui::alignRight(closeWidth);
        if(ui::ghostButton(closeLabel)) close=true;

        ui::pushSmall();
        ui::muted("终末地联机 · %s",roomTitle().c_str());
        // A quiet affordance for the one thing the menu cannot show by itself: that
        // the window can be moved and resized at all. It rides on the caption line
        // instead of taking a row of its own, because the navigation rail cannot
        // spare the height on a 600px-tall screen.
        ui::hspace(ui::px(16));
        ui::muted("%s","拖动标题移动 · 拖边缘缩放");
        ui::popSmall();
        const std::string current=roleText(role());
        char players[64]{};
        if(online()) std::snprintf(players,sizeof players,"%d / %d 人",int(peers().size()),uid::intAt(roomInfo(),"max_players",8));
        else std::snprintf(players,sizeof players,"%s","未加入房间");
        ui::alignRight(ui::pillWidth(current.c_str())+ui::px(8)+ui::pillWidth(players));
        ui::statusPill(current.c_str(),roleTone(role()));
        ui::hspace(ui::px(8));
        ui::statusPill(players,ui::Tone::Neutral);
        ui::vspace(ui::px(12));
        ImGui::Separator();
        ui::vspace(ui::px(10));
    }
    void rail(float height) {
        static const char* labels[]={"局域网房间","直接连接 / 收藏","开放房间","房间玩家","房间聊天","设置 / 诊断"};
        char roomCount[16]{},favoriteCount[16]{},playerCount[16]{};
        std::snprintf(roomCount,sizeof roomCount,"%d",int(uid::arrayAt(state,"rooms").size()));
        std::snprintf(favoriteCount,sizeof favoriteCount,"%d",int(uid::arrayAt(configuration(),"saved_servers").size()));
        std::snprintf(playerCount,sizeof playerCount,"%d",int(peers().size()));
        const bool hasRooms=!uid::arrayAt(state,"rooms").empty();
        const bool hasFavorites=!uid::arrayAt(configuration(),"saved_servers").empty();
        const char* notes[]={hasRooms?roomCount:nullptr,hasFavorites?favoriteCount:nullptr,
            nullptr,online()?playerCount:nullptr,nullptr,nullptr};

        ImGui::PushStyleColor(ImGuiCol_ChildBg,ui::kRaised);
        ImGui::BeginChild("navigation",{ui::px(196),height},ImGuiChildFlags_Borders);
        static const char* hintText="关闭菜单后联机继续运行，屏幕上会保留玩家位置标记。";
        const float gap=ui::px(8);
        char keyLine[32]{};
        if(key==VK_OEM_PLUS) std::snprintf(keyLine,sizeof keyLine,"菜单键：=");
        else std::snprintf(keyLine,sizeof keyLine,"菜单键：F%d",key-VK_F1+1);
        ui::pushSmall();
        // Deliberately an over-estimate: Separator() contributes no layout height,
        // but every item advances the cursor by its height *plus* ItemSpacing.y,
        // and ImGui enlarges text rows to match the font baseline.
        const float spacing=ImGui::GetStyle().ItemSpacing.y;
        const float blockHeight=spacing*5.f+gap+ImGui::GetTextLineHeightWithSpacing()
            +ImGui::CalcTextSize(hintText,nullptr,false,ImGui::GetContentRegionAvail().x).y;
        ui::popSmall();

        // Fit the six rows to the column rather than overflowing it: on a short
        // window the header and footer leave the rail too little room for fixed
        // 36px rows, and the column would grow a scrollbar. The hint block is
        // reserved up front so the rows and the hint never compete.
        const float rowHeight=(std::clamp)((ImGui::GetContentRegionAvail().y-blockHeight-spacing*7.f)/6.f,ui::px(26),ui::px(36));
        for(int index=0;index<6;++index) if(ui::navItem(labels[index],tab==index,notes[index],rowHeight)) tab=index;

        // The estimate above is what puts the hint on the bottom edge. The Dummy
        // itself also consumes one ItemSpacing.y, so an exact fit would land right
        // on the scrollbar threshold; keep a real cushion, and drop the hint on
        // columns that are too short rather than clip it -- the binding is also
        // shown on the settings page, so losing it costs nothing.
        ui::pushSmall();
        const float slack=ImGui::GetContentRegionAvail().y-blockHeight;
        railHeight=ImGui::GetWindowHeight();railSlack=slack;
        if(slack>ui::px(12)) {
            ImGui::Dummy({0,slack-ui::px(12)});
            ImGui::Separator();
            ui::vspace(gap);
            ui::muted("%s",keyLine);
            ui::hint("%s",hintText);
        }
        railOverflow=ImGui::GetScrollMaxY()>0.f;
        ui::popSmall();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    void footerStatus() {
        ui::hspace(ui::px(18));
        char buffer[80]{};
        if(online()) {
            std::snprintf(buffer,sizeof buffer,"延迟 %u ms",uid::uintAt(state,"rtt_ms",0));
            ui::dim("%s",buffer);
            ui::hspace(ui::px(14));
            std::snprintf(buffer,sizeof buffer,"玩家 %d",int(peers().size()));
            ui::dim("%s",buffer);
            ui::hspace(ui::px(14));
            const auto phase=uid::stringAt(entry(),"phase","offline");
            std::snprintf(buffer,sizeof buffer,"世界 %s",uid::boolAt(entry(),"active",false)?"已入场":phase.c_str());
            ui::dim("%s",buffer);
        } else ui::dim("%s","房间连接与只读状态 · 共享玩法未支持");
        const std::string version=std::string("EF Link ")+EFLINK_VERSION;
        ui::alignRight(ImGui::CalcTextSize(version.c_str()).x);
        ui::muted("%s",version.c_str());
    }
    void footer() {
        const auto& teleportError=uid::stringAt(replication(),"error");
        if(uid::stringAt(replication(),"teleport")=="failed"&&!teleportError.empty()) {
            ui::notice(ui::Tone::Danger,(std::string("传送失败：")+teleportError).c_str());
            ui::vspace(ui::px(8));
        }
        if(mapChangePending()) {
            const auto& change=mapChange();
            const auto token=uid::serialAt(change,"token",0);
            const bool deferred=uid::boolAt(change,"deferred",false);
            std::string text=std::string("房主已切换地图：")+uid::stringAt(change,"level");
            if(deferred) text+="，已暂不跟随，联机战斗暂停";
            ui::notice(ui::Tone::Accent,text.c_str());
            ui::vspace(ui::px(8));
            if(ui::primaryButton(deferred?"现在跟随房主":"跟随房主",{ui::px(140),ui::px(30)}))
                call({{"action","host_map_choice"},{"token",token},{"follow",true}});
            ui::hspace(ui::px(8));
            if(!deferred&&ui::secondaryButton("暂不跟随",{ui::px(120),ui::px(30)}))
                call({{"action","host_map_choice"},{"token",token},{"follow",false}});
            if(!hostSceneSupported()) {ui::hspace(ui::px(10));ui::dim("%s","房主当前场景尚未开放联机入场");}
            if(!deferred&&token!=lastMapPrompt) {lastMapPrompt=token;openMapPrompt=true;}
            ui::vspace(ui::px(10));
        }
        if(!offline()) {
            const auto current=role();
            const char* label=current=="connecting"?"取消连接":current=="host"?"关闭房间":"断开连接";
            if(ui::dangerButton(label,{0,ui::px(32)})) openLeave=true;
        }
        footerStatus();
    }
    void popups() {
        if(openLeave) {ImGui::OpenPopup("离开房间");openLeave=false;}
        if(ImGui::BeginPopupModal("离开房间",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(role()=="host"?"关闭房间会断开所有玩家，并结束当前联机。":"确定结束当前连接？");
            ui::vspace(ui::px(14));
            if(ui::dangerButton("确认",{ui::px(120),ui::px(32)})) {call({{"action","leave"}});joinPassword[0]=0;ImGui::CloseCurrentPopup();}
            ui::hspace(ui::px(8));
            if(ui::secondaryButton("返回",{ui::px(120),ui::px(32)})) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(openKick) {ImGui::OpenPopup("确认移出");openKick=false;}
        if(ImGui::BeginPopupModal("确认移出",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("将 %s 移出当前房间？",pendingKickName.c_str());
            ui::vspace(ui::px(14));
            if(ui::dangerButton("移出",{ui::px(120),ui::px(32)})) {call({{"action","kick"},{"id",pendingKick}});ImGui::CloseCurrentPopup();}
            ui::hspace(ui::px(8));
            if(ui::secondaryButton("取消",{ui::px(120),ui::px(32)})) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(openMapPrompt) {ImGui::OpenPopup("房主切换地图");openMapPrompt=false;}
        if(ImGui::BeginPopupModal("房主切换地图",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            if(!mapChangePending()) ImGui::CloseCurrentPopup();
            else {
                const auto& change=mapChange();
                const auto token=uid::serialAt(change,"token",0);
                ImGui::Text("房主已前往：%s",uid::stringAt(change,"level").c_str());
                ImGui::TextUnformatted("是否跟随房主切换地图？");
                ui::pushSmall();
                ui::hint("暂不跟随会留在当前地图，并暂停联机战斗。");
                ui::popSmall();
                if(!hostSceneSupported()) ui::notice(ui::Tone::Warning,"房主当前场景尚未开放联机入场，请等待其回到大世界。");
                ui::vspace(ui::px(14));
                ImGui::BeginDisabled(!hostSceneSupported());
                if(ui::primaryButton("跟随切换",{ui::px(140),ui::px(32)})) {
                    call({{"action","host_map_choice"},{"token",token},{"follow",true}});
                    if(feedback.empty()) ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ui::hspace(ui::px(8));
                if(ui::secondaryButton("暂不跟随",{ui::px(140),ui::px(32)})) {
                    call({{"action","host_map_choice"},{"token",token},{"follow",false}});
                    if(feedback.empty()) ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // ---- pages -----------------------------------------------------------
    void pageRooms() {
        ui::sectionHeader("局域网房间","自动发现");
        if(ui::primaryButton("刷新列表",{ui::px(120),ui::px(32)})) call({{"action","refresh"}});
        ui::hspace(ui::px(12));
        ui::dim("%s","自动寻找同一局域网中的房间；也可以直接输入好友地址。");
        ui::vspace(ui::px(12));
        const auto rooms=uid::objectsAt(state,"rooms");
        ImGui::BeginDisabled(!offline());
        if(rooms.empty()) ui::emptyState("尚未发现房间","让好友先开放房间，或在“直接连接”中填写 IP:端口。");
        for(const auto& roomEntry:rooms) {
            const auto roomName=uid::stringAt(roomEntry,"name");
            const auto endpoint=uid::stringAt(roomEntry,"address");
            const bool locked=uid::boolAt(roomEntry,"password",false);
            const float joinWidth=ui::px(104);
            ImGui::PushID(endpoint.c_str());
            ui::cardBegin({ui::px(14),ui::px(11)});
            ImGui::PushStyleColor(ImGuiCol_Text,ui::kText);
            ImGui::TextUnformatted(ui::ellipsize(roomName.empty()?endpoint:roomName,ImGui::GetContentRegionAvail().x-joinWidth-ui::px(90)).c_str());
            ImGui::PopStyleColor();
            if(locked) {ui::hspace(ui::px(8));ui::badge("需要密码",ui::Tone::Warning);}
            ui::alignRight(joinWidth);
            if(ui::primaryButton("加入房间",{joinWidth,ui::px(30)})) {
                copyText(address,sizeof address,endpoint);joinPassword[0]=0;
                if(locked) tab=1;else join();
            }
            ui::pushSmall();
            ui::muted("%s  ·  %d/%d 人  ·  %s",uid::stringAt(roomEntry,"host").c_str(),uid::intAt(roomEntry,"players",0),
                uid::intAt(roomEntry,"max_players",0),endpoint.c_str());
            const auto mapName=uid::stringAt(roomEntry,"map");
            if(!mapName.empty()) ui::muted("地图 %s",mapName.c_str());
            ui::popSmall();
            ui::cardEnd();
            ui::vspace(ui::px(8));
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }
    void pageConnect() {
        ui::sectionHeader("连接好友","直接地址");
        ui::cardBegin();
        ui::fieldLabel("房主地址","例：192.168.1.20:47615");
        ui::field("##address","房主的 IP 或 IP:端口",address,sizeof address);
        ui::vspace(ui::px(10));
        ui::fieldLabel("房间密码","可留空");
        ui::field("##password","房间密码（可留空）",joinPassword,sizeof joinPassword,ImGuiInputTextFlags_Password);
        ui::vspace(ui::px(14));
        ImGui::BeginDisabled(!offline());
        if(ui::primaryButton("连接",{ui::px(140),ui::px(32)})) join();
        ImGui::EndDisabled();
        if(!offline()) {ui::hspace(ui::px(10));ui::dim("%s","已在房间中，断开后才能连接其他房间。");}
        ui::vspace(ui::px(14));
        ui::fieldLabel("收藏名称","可留空，默认使用地址");
        ui::field("##serverName","收藏名称",serverName,sizeof serverName);
        ui::vspace(ui::px(10));
        if(ui::chipButton("收藏此地址",ui::Tone::Accent)) call({{"action","save_server"},{"address",address},{"name",*serverName?serverName:address}});
        ui::cardEnd();
        ui::vspace(ui::px(14));
        const auto servers=uid::objectsAt(configuration(),"saved_servers");
        char count[24]{};
        std::snprintf(count,sizeof count,"%d 个",int(servers.size()));
        ui::sectionHeader("收藏的服务器",servers.empty()?nullptr:count);
        if(servers.empty()) {ui::emptyState("还没有收藏","填写地址后点“收藏此地址”，下次就能一键填入。");return;}
        for(const auto& server:servers) {
            const auto endpoint=uid::stringAt(server,"address");
            const auto label=uid::stringAt(server,"name",endpoint.c_str());
            ImGui::PushID(endpoint.c_str());
            ui::cardBegin({ui::px(14),ui::px(10)});
            ui::pushSmall();
            ImGui::PushStyleColor(ImGuiCol_Text,ui::kText);
            ImGui::TextUnformatted(ui::ellipsize(label,(std::max)(ui::px(60),ImGui::GetContentRegionAvail().x-ui::px(200))).c_str());
            ImGui::PopStyleColor();
            ui::muted("%s",endpoint.c_str());
            ui::popSmall();
            const float fillWidth=ui::chipWidth("填入地址");
            const float removeWidth=ui::chipWidth("移除");
            ui::alignRight(fillWidth+removeWidth+ui::px(8));
            if(ui::chipButton("填入地址",ui::Tone::Accent)) {copyText(address,sizeof address,endpoint);joinPassword[0]=0;}
            ui::hspace(ui::px(8));
            if(ui::chipButton("移除",ui::Tone::Danger)) call({{"action","remove_server"},{"address",endpoint}});
            ui::cardEnd();
            ui::vspace(ui::px(8));
            ImGui::PopID();
        }
    }
    void pageHost() {
        const auto info=roomInfo();
        const auto current=role();
        ui::sectionHeader("开放房间","房主");
        if(!offline()) {
            if(current=="host") ui::notice(ui::Tone::Success,(std::string("当前房间已开放：")+uid::stringAt(info,"name")).c_str());
            else if(current=="connecting") ui::notice(ui::Tone::Warning,"正在连接房间。请先取消连接，再设置自己的房间。");
            else ui::notice(ui::Tone::Warning,"你已加入其他房间。请先断开连接，再设置自己的房间。");
            ui::vspace(ui::px(8));
        }
        ImGui::BeginDisabled(!offline());
        ui::cardBegin();
        ui::fieldLabel("房间名称","好友在列表里看到的名字");
        ui::field("##room","房间名称",room,sizeof room);
        ui::vspace(ui::px(10));
        ui::fieldLabel("房间密码","可留空");
        ui::field("##roomPassword","房间密码（可留空）",hostPassword,sizeof hostPassword,ImGuiInputTextFlags_Password);
        ui::vspace(ui::px(14));
        ui::sliderField("人数上限",&limit,2,16,"2–16 人");
        ui::vspace(ui::px(8));
        ui::fieldLabel("连接端口","默认 47615，需与转发的本地端口一致");
        ImGui::SetNextItemWidth(ui::px(180));
        ImGui::InputInt("##port",&port,0);
        ui::vspace(ui::px(16));
        if(ui::primaryButton("开放房间",{ui::px(180),ui::px(34)})) {
            save();
            if(feedback.empty()) call({{"action","host"},{"room_name",room},{"password",hostPassword},{"port",port},{"max_players",limit}});
            hostPassword[0]=0;
        }
        ui::cardEnd();
        ImGui::EndDisabled();
        ui::vspace(ui::px(14));
        if(current!="host") {
            ui::pushSmall();
            ui::hint("房间开放后，把下面的地址发给好友；也可以用局域网房间列表自动发现。");
            ui::popSmall();
            return;
        }
        ui::sectionHeader("房间地址","分享给好友");
        const auto addresses=uid::objectsAt(state,"addresses");
        if(addresses.empty()) ui::emptyState("正在读取本机地址");
        for(const auto& adapter:addresses) {
            const auto text=uid::stringAt(adapter,"ip")+":"+std::to_string(uid::intAt(info,"port",47615));
            ImGui::PushID(text.c_str());
            ui::cardBegin({ui::px(14),ui::px(10)});
            ImGui::PushStyleColor(ImGuiCol_Text,ui::kText);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();
            const float copyWidth=ui::chipWidth("复制地址");
            ui::alignRight(copyWidth);
            if(ui::chipButton("复制地址",ui::Tone::Accent)) ImGui::SetClipboardText(text.c_str());
            ui::pushSmall();
            ui::muted("%s",uid::stringAt(adapter,"adapter").c_str());
            ui::popSmall();
            ui::cardEnd();
            ui::vspace(ui::px(8));
            ImGui::PopID();
        }
    }
    void pagePlayers() {
        ui::sectionHeader("房间玩家",roomTitle().c_str());
        if(online()&&!uid::boolAt(entry(),"active",false)) {
            ui::notice(ui::Tone::Warning,"已连接房间，世界接管尚未完成：现在还不能一起结算战斗。");
            ui::vspace(ui::px(8));
        }
        if(!online()) {ui::emptyState("还没有玩家","加入或开放房间后，这里会显示玩家、地图与同步状态。");return;}
        const auto players=peers();
        if(players.empty()) {ui::emptyState("房间暂时只有你","好友加入后会出现在这里。");return;}
        const bool host=role()=="host";
        for(const auto& peer:players) {
            const auto peerName=uid::stringAt(peer,"name","未命名玩家");
            const bool local=uid::boolAt(peer,"local",false);
            const bool fresh=uid::boolAt(peer,"valid",false)&&uid::intAt(peer,"state_age_ms",4000)<=3000;
            const float teleportWidth=ui::buttonWidth("传送到这位玩家");
            const float kickWidth=host?ui::buttonWidth("移出玩家")+ui::px(8):0.f;
            ImGui::PushID(uid::intAt(peer,"id",0));
            ui::cardBegin({ui::px(14),ui::px(12)});
            ImGui::PushStyleColor(ImGuiCol_Text,ui::kText);
            ImGui::TextUnformatted(ui::ellipsize(peerName,(std::max)(ui::px(60),ImGui::GetContentRegionAvail().x-teleportWidth-kickWidth-ui::px(120))).c_str());
            ImGui::PopStyleColor();
            if(uid::boolAt(peer,"host",false)) {ui::hspace(ui::px(8));ui::badge("房主",ui::Tone::Accent);}
            if(local) {ui::hspace(ui::px(6));ui::badge("你",ui::Tone::Info);}
            if(!local&&online()) {
                ui::alignRight(teleportWidth+kickWidth);
                ImGui::BeginDisabled(!fresh||!uid::boolAt(replication(),"scene_travel_supported",false));
                if(ui::secondaryButton("传送到这位玩家",{0,ui::px(30)})) call({{"action","teleport_to_player"},{"id",uid::intAt(peer,"id",0)}});
                ImGui::EndDisabled();
                if(host) {
                    ui::hspace(ui::px(8));
                    if(ui::dangerButton("移出玩家",{0,ui::px(30)})) {pendingKick=uid::intAt(peer,"id",0);pendingKickName=peerName;openKick=true;}
                }
            }
            ui::pushSmall();
            const char* status=local?"本机":(!fresh?"等待角色状态":(uid::boolAt(peer,"same_map",false)?"同地图 · 位置状态":"在其他地图"));
            const auto mapName=uid::stringAt(peer,"map");
            ui::muted("地图 %s  ·  %s",mapName.empty()?"读取中":mapName.c_str(),status);
            const auto endpoint=uid::stringAt(peer,"address");
            if(!local&&!endpoint.empty()) ui::muted("%s  ·  延迟 %u ms",endpoint.c_str(),uid::uintAt(peer,"rtt_ms",0));
            if(uid::hasAt(peer,"distance")) ui::muted("距离 %.1f 米",uid::floatAt(peer,"distance",0.f));
            ui::popSmall();
            ui::cardEnd();
            ui::vspace(ui::px(8));
            ImGui::PopID();
        }
    }
    void pageChat() {
        const auto lines=uid::objectsAt(state,"chat");
        ui::sectionHeader("房间聊天");
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ui::kSunken);
        ImGui::BeginChild("messages",{0,-ui::px(80)},ImGuiChildFlags_Borders);
        if(lines.empty()) {
            ui::pushSmall();
            ui::muted("%s","还没有消息。加入房间后就能和队友说话了。");
            ui::popSmall();
        }
        std::uint64_t newest=0;
        for(const auto& line:lines) {
            newest=(std::max)(newest,uid::serialAt(line,"serial",0));
            const auto text=uid::stringAt(line,"text");
            if(uid::boolAt(line,"system",false)) {
                ui::pushSmall();
                ui::muted("· %s",text.c_str());
                ui::popSmall();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text,ui::kAccent);
                ImGui::TextUnformatted((uid::stringAt(line,"name","玩家")+"：").c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0,0);
                ImGui::PushTextWrapPos(0.f);
                ImGui::TextWrapped("%s",text.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        if(newest!=lastChat) {ImGui::SetScrollHereY(1);lastChat=newest;}
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ui::vspace(ui::px(10));
        ImGui::BeginDisabled(!online());
        ImGui::SetNextItemWidth(-ui::px(92));
        bool send=ImGui::InputTextWithHint("##chat","输入消息，按 Enter 发送",chat,sizeof chat,ImGuiInputTextFlags_EnterReturnsTrue);
        ui::hspace(ui::px(8));
        send|=ui::secondaryButton("发送",{ui::px(80),ui::px(32)});
        if(send&&*chat) {call({{"action","chat"},{"text",chat}});chat[0]=0;}
        ImGui::EndDisabled();
    }
    void pageSettings() {
        ui::sectionHeader("玩家设置");
        ui::cardBegin();
        ui::fieldLabel("玩家名称","其他玩家看到的名字");
        ui::field("##name","玩家名称",name,sizeof name);
        ui::vspace(ui::px(12));
        ui::fieldLabel("菜单快捷键","游戏中随时打开联机菜单");
        static const char* keys[]={"=","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
        int index=key==VK_OEM_PLUS?0:key-VK_F1+1;
        ImGui::SetNextItemWidth(ui::px(180));
        if(ImGui::Combo("##menuKey",&index,keys,13)) key=index==0?VK_OEM_PLUS:VK_F1+index-1;
        ui::vspace(ui::px(16));
        ui::toggleSwitch("remote",&remote,"远端角色显示偏好（待适配）");
        ui::toggleSwitch("markers",&markers,"屏幕显示玩家位置与距离");
        ui::toggleSwitch("follow",&follow,"跟随房主偏好（换图未支持）");
        ui::toggleSwitch("combat",&combat,"共享战斗偏好（原生执行未支持）");
        ui::vspace(ui::px(10));
        if(ui::primaryButton("保存设置",{ui::px(140),ui::px(32)})) save();
        ui::hspace(ui::px(12));
        ui::dim("%s","收藏、名字与设置自动保存在本机。");
        ui::cardEnd();

        ui::vspace(ui::px(14));
        ui::sectionHeader("恢复","未支持");
        ui::cardBegin();
        ui::notice(ui::Tone::Warning,"原生个人世界隔离与恢复尚未验收，房主世界入场和自动应急传送未开放。");
        ui::cardEnd();

        ui::vspace(ui::px(14));
        ui::sectionHeader("世界接管","实验功能");
        ui::cardBegin();
        const auto& session=worldSession();
        const auto& game=uid::objectAt(state,"game");
        const auto& native=replication();
        ui::keyValue("游戏接入","%s",uid::boolAt(state,"hook_ready",false)?"已连接":"等待终末地场景");
        ui::keyValue("世界状态","%s  /  地图版本 %llu",uid::boolAt(session,"ready",false)?"已收到":"等待房主",
            static_cast<unsigned long long>(uid::serialAt(session,"epoch",0)));
        ui::keyValue("房主世界","%s",uid::boolAt(entry(),"active",false)?"已完成入场":"接管未完成");
        ui::keyValue("官方账号","%s","各自登录、各自连接");
        ui::keyValue("官方副本","%s","各账号保留自己的实例");
        ui::keyValue("合作验收","%s",uid::boolAt(entry(),"stage1_verified",false)?"第一阶段已通过":"开发测试中");
        ui::keyValue("当前阶段","%s",uid::stringAt(entry(),"phase","offline").c_str());
        ui::keyValue("敌人","已匹配 %u  /  缺失 %u",uid::uintAt(native,"matched",0),uid::uintAt(native,"missing",0));
        ui::keyValue("传送状态","%s",uid::stringAt(native,"teleport","idle").c_str());
        const auto mapName=uid::stringAt(game,"map_id");
        ui::keyValue("本机地图","%s",mapName.empty()?"读取中":mapName.c_str());
        ui::keyValue("骨骼节点","%d",uid::intAt(game,"local_bones",0));
        ui::keyValue("与房主延迟","%u ms",uid::uintAt(state,"rtt_ms",0));
        const auto& policy=uid::objectAt(native,"guest_policy");
        if(uid::boolAt(policy,"restricted",false)) {
            ui::vspace(ui::px(10));
            ui::notice(ui::Tone::Info,"房客模式：地图机关、采集、资源破坏和私人副本入口已禁用，切图与传送由房主世界管理，离开房间后恢复。");
            const auto policyError=uid::stringAt(policy,"error");
            if(!uid::boolAt(policy,"ready",false)&&!policyError.empty()) {
                ui::vspace(ui::px(6));
                ui::notice(ui::Tone::Warning,(std::string("交互隔离未就绪：")+policyError).c_str());
            }
        }
        const auto blockers=uid::stringsAt(entry(),"blockers");
        if(!blockers.empty()) {
            ui::vspace(ui::px(10));
            ui::pushSmall();
            ui::muted("%s","尚未接管");
            ui::popSmall();
            ui::vspace(ui::px(5));
            for(const auto& blocker:blockers) {
                const char* description=
                    blocker=="host_world_isolation"?"房主世界隔离":blocker=="host_logical_players"?"房主逻辑玩家":
                    blocker=="host_combat_settlement"?"房主战斗结算":blocker=="host_scene_rules"?"房主地图与副本规则":
                    blocker=="guest_map_loading"?"客机地图加载":blocker=="guest_entity_replicas"?"客机实体重建":
                    blocker=="guest_world_isolation"?"客机世界隔离":blocker=="guest_logical_players"?"客机逻辑玩家":blocker.c_str();
                ui::badge(description,ui::Tone::Neutral);
                ui::hspace(ui::px(6));
            }
            ui::vspace(ui::px(4));
        }
        const std::string messages[]={uid::stringAt(session,"error"),uid::stringAt(native,"error"),uid::stringAt(game,"error")};
        for(const auto& message:messages) {
            if(message.empty()) continue;
            ui::vspace(ui::px(8));
            ui::notice(ui::Tone::Danger,message.c_str());
        }
        const auto& stats=uid::objectAt(state,"traffic");
        ui::vspace(ui::px(10));
        if(role()=="client") {
            const auto pongs=uid::serialAt(stats,"udp_pongs",0);
            const auto states=static_cast<unsigned long long>(uid::serialAt(stats,"states_in",0));
            char text[192]{};
            if(pongs>0) std::snprintf(text,sizeof text,"UDP 已连通 · 已收到 %llu 条玩家状态",states);
            else if(states>0) std::snprintf(text,sizeof text,"UDP 未连通，已改用 TCP 同步 · 已收到 %llu 条玩家状态",states);
            else std::snprintf(text,sizeof text,"%s","UDP 未连通，正在改用 TCP 同步……");
            ui::notice(pongs>0?ui::Tone::Success:ui::Tone::Warning,text);
        } else if(role()=="host") {
            ui::keyValue("收到状态","%llu",static_cast<unsigned long long>(uid::serialAt(stats,"states_in",0)));
        } else {
            ui::pushSmall();
            ui::muted("%s","加入或开放房间后，这里会显示同步通道和世界接管进度。");
            ui::popSmall();
        }
        ui::cardEnd();

        ui::vspace(ui::px(14));
        ui::sectionHeader("开发者","测试后请关闭");
        ui::cardBegin();
        bool invincible=uid::boolAt(state,"testing_invincible",false);
        if(ui::toggleSwitch("invincible",&invincible,"本机角色无敌","不保存，重开游戏自动关闭。作废本机干员受到的伤害，仅供联机测试。"))
            call({{"action","testing_invincible"},{"enabled",invincible}});
        ui::cardEnd();
    }

    void draw() {
        refresh();
        // A minimised game window can report a zero-sized render target. Falling
        // back to the last known display keeps both the menu and the rectangle
        // the player moved it to intact, instead of collapsing it to a few pixels.
        const ImVec2 reported=ImGui::GetIO().DisplaySize;
        const ImVec2 display=reported.x>1.f&&reported.y>1.f?reported:layoutDisplay;
        // Everything is clamped into the display. The previous fixed size used a
        // floor that could outgrow a small screen, which pushed the window -- and
        // the footer with it -- off the bottom edge where nothing could reach it.
        const float maxWidth=(std::max)(1.f,display.x-ui::px(24));
        const float maxHeight=(std::max)(1.f,display.y-ui::px(24));
        const float minWidth=(std::min)(ui::px(880),maxWidth);
        const float minHeight=(std::min)(ui::px(520),maxHeight);
        // ImGui owns the rectangle between frames, so the panel only seeds it and
        // then reads back what the player settled on. Seeding also happens after a
        // gap, which is how a menu that stayed closed for a while comes back where
        // it was left rather than at a freshly created default.
        const std::uint64_t frame=std::uint64_t(ImGui::GetFrameCount());
        const bool reopened=frame>layoutFrame+1;
        const bool displayChanged=display.x!=layoutDisplay.x||display.y!=layoutDisplay.y;
        if(!layoutReady||reopened||displayChanged) {
            if(!layoutReady) {
                layoutSize={(std::min)(ui::px(1120),maxWidth),(std::min)(ui::px(780),maxHeight)};
                layoutPos={(display.x-layoutSize.x)*.5f,(display.y-layoutSize.y)*.5f};
                layoutReady=true;
            }
            layoutDisplay=display;
            layoutSize.x=(std::clamp)(layoutSize.x,minWidth,maxWidth);
            layoutSize.y=(std::clamp)(layoutSize.y,minHeight,maxHeight);
            layoutPos.x=(std::clamp)(layoutPos.x,0.f,(std::max)(0.f,display.x-layoutSize.x));
            layoutPos.y=(std::clamp)(layoutPos.y,0.f,(std::max)(0.f,display.y-layoutSize.y));
            ImGui::SetNextWindowPos(layoutPos);
            ImGui::SetNextWindowSize(layoutSize);
        } else if(dragActive&&ImGui::IsMousePosValid()) {
            // Dragging the title strip. ImGui's own "click empty space to move"
            // path cannot be used for it: the strip has to be a real item to be
            // clickable, and a hovered item blocks that path.
            const ImVec2 delta=ImGui::GetIO().MouseDelta;
            layoutPos.x=(std::clamp)(layoutPos.x+delta.x,0.f,(std::max)(0.f,display.x-layoutSize.x));
            layoutPos.y=(std::clamp)(layoutPos.y+delta.y,0.f,(std::max)(0.f,display.y-layoutSize.y));
            ImGui::SetNextWindowPos(layoutPos);
        }
        layoutFrame=frame;
        ImGui::SetNextWindowSizeConstraints({minWidth,minHeight},{maxWidth,maxHeight});
        // NoTitleBar keeps the shipped header. NoResize and NoMove stay off on
        // purpose, which is what enables the resize border, the two corner grips
        // and ImGui's own "drag empty space" mover as a second way to move the
        // window. NoScrollbar because the rail and the footer are pinned regions.
        ImGui::Begin("EF Link",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoSavedSettings);
        // Read back before finishing: the player may have resized the window
        // inside Begin(), and End() would pop the window we are querying.
        layoutPos=ImGui::GetWindowPos();
        layoutSize=ImGui::GetWindowSize();
        dragHovered=false;dragActive=false;
        header();
        // The map-change prompt is tall. On a short window let it take at most a
        // third of the height, otherwise it squeezes the pages and the rail into
        // nothing.
        const float reserved=mapChangePending()?(std::min)(ui::px(168),layoutSize.y*.34f):ui::px(72);
        rail(-reserved);
        ImGui::SameLine();
        ImGui::BeginChild("content",{0,-reserved},ImGuiChildFlags_AlwaysUseWindowPadding);
        ui::notice(ui::Tone::Warning,"实验模组存在账号处罚及状态异常风险；官方连接不代表官方认可，无法保证零封号风险。未验收能力不属于完整同步。");
        ui::vspace(ui::px(8));
        if(tab==0) pageRooms();
        else if(tab==1) pageConnect();
        else if(tab==2) pageHost();
        else if(tab==3) pagePlayers();
        else if(tab==4) pageChat();
        else pageSettings();
        const auto error=uid::stringAt(state,"last_error");
        if(!feedback.empty()||!error.empty()) {
            ui::vspace(ui::px(12));
            ui::notice(ui::Tone::Danger,(!feedback.empty()?feedback:error).c_str());
        }
        ImGui::EndChild();
        footer();
        popups();
        ImGui::End();
    }

private:
    int pendingKick=-1;
    std::string pendingKickName;
    bool openKick=false,openLeave=false,openMapPrompt=false;
    bool initialized=false;
};

// Screen overlay: one chip per remote player, clamped to the viewport with an
// arrow when the player is behind the camera or off screen.
inline void drawHud(const json& data) {
    const auto markers=uid::objectsAt(data,"markers");
    if(markers.empty()) return;
    const ImVec2 size=ImGui::GetIO().DisplaySize;
    ImDrawList* draw=ImGui::GetForegroundDrawList();
    const float padding=ui::px(18);
    // The "EF Link · N 位玩家" summary chip is drawn after the loop (it needs the
    // final count) but occupies the top-left corner, so reserve its height up
    // front -- otherwise the "other map" list is laid out on top of it.
    float otherY=ui::px(10)+ImGui::GetTextLineHeight()+ui::px(9)+ui::px(8);
    int visible=0;
    for(const auto& marker:markers) {
        std::string label=uid::stringAt(marker,"name","玩家");
        if(uid::boolAt(marker,"other_map",false)) {
            const std::string text=label+" · "+uid::stringAt(marker,"map");
            const ImVec2 textSize=ImGui::CalcTextSize(text.c_str());
            draw->AddRectFilled({padding-ui::px(7),otherY-ui::px(3)},{padding+textSize.x+ui::px(7),otherY+textSize.y+ui::px(3)},ui::u32(ui::kInk,.55f),ui::px(4));
            draw->AddCircleFilled({padding-ui::px(13),otherY+textSize.y*.5f},ui::px(3),ui::u32(ui::kTextMuted),8);
            draw->AddText({padding,otherY},ui::u32(ui::kTextMuted),text.c_str());
            otherY+=textSize.y+ui::px(9);
            continue;
        }
        float x=uid::floatAt(marker,"x",.5f)*size.x,y=uid::floatAt(marker,"y",.5f)*size.y;
        if(!std::isfinite(x)||!std::isfinite(y)) continue;
        const bool behind=uid::boolAt(marker,"behind",false);
        const bool off=behind||x<padding||y<padding||x>size.x-padding||y>size.y-padding;
        if(off) {
            float dx=x-size.x*.5f,dy=y-size.y*.5f;
            if(behind) {dx=-dx;dy=-dy;}
            if(std::abs(dx)+std::abs(dy)<.001f) dy=-1;
            const float scale=1.f/(std::max)(std::abs(dx)/((std::max)(1.f,size.x*.5f-ui::px(65))),std::abs(dy)/((std::max)(1.f,size.y*.5f-ui::px(65))));
            x=size.x*.5f+dx*scale;y=size.y*.5f+dy*scale;
        }
        char distance[48]{};
        std::snprintf(distance,sizeof distance," · %.1f m",uid::floatAt(marker,"distance",0.f));
        label+=distance;
        const ImVec2 textSize=ImGui::CalcTextSize(label.c_str());
        const float textX=(std::clamp)(x-textSize.x*.5f,padding,(std::max)(padding,size.x-textSize.x-padding));
        const float textY=y+ui::px(14);
        const ImVec2 chipMin{textX-ui::px(8),textY-ui::px(5)};
        const ImVec2 chipMax{textX+textSize.x+ui::px(8),textY+textSize.y+ui::px(5)};
        draw->AddRectFilled({chipMin.x+ui::px(1),chipMin.y+ui::px(2)},{chipMax.x+ui::px(1),chipMax.y+ui::px(2)},ui::u32(ui::kInk,.35f),ui::px(5));
        draw->AddRectFilled(chipMin,chipMax,ui::u32(ui::kInk,.78f),ui::px(5));
        draw->AddRect(chipMin,chipMax,ui::u32(ui::kAccent,.28f),ui::px(5),0,ui::px(1));
        draw->AddText({textX,textY},ui::u32(ui::kAccent),label.c_str());
        const float radius=off?ui::px(6):ui::px(4);
        draw->AddCircleFilled({x,y},radius,ui::u32(ui::kInk,.85f),16);
        draw->AddCircle({x,y},radius,ui::u32(ui::kAccent),16,ui::px(2));
        if(off) {
            const float dx=x-size.x*.5f,dy=y-size.y*.5f;
            const float length=(std::max)(.001f,std::sqrt(dx*dx+dy*dy));
            const float ux=dx/length,uy=dy/length;
            const ImVec2 tip{x+ux*ui::px(14),y+uy*ui::px(14)};
            const ImVec2 left{tip.x-ux*ui::px(9)-uy*ui::px(5),tip.y-uy*ui::px(9)+ux*ui::px(5)};
            const ImVec2 right{tip.x-ux*ui::px(9)+uy*ui::px(5),tip.y-uy*ui::px(9)-ux*ui::px(5)};
            draw->AddTriangleFilled(tip,left,right,ui::u32(ui::kAccent));
        }
        ++visible;
    }
    if(visible>0) {
        char summary[64]{};
        std::snprintf(summary,sizeof summary,"EF Link · %d 位玩家",visible);
        const ImVec2 textSize=ImGui::CalcTextSize(summary);
        const ImVec2 chipMin{padding-ui::px(9),ui::px(10)};
        const ImVec2 chipMax{chipMin.x+textSize.x+ui::px(18),chipMin.y+textSize.y+ui::px(9)};
        draw->AddRectFilled(chipMin,chipMax,ui::u32(ui::kInk,.65f),ui::px(5));
        draw->AddRect(chipMin,chipMax,ui::u32(ui::kAccent,.25f),ui::px(5),0,ui::px(1));
        draw->AddText({chipMin.x+ui::px(9),chipMin.y+ui::px(4)},ui::u32(ui::kTextDim),summary);
    }
}
}
