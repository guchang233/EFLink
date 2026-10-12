// Headless checks for the menu: every page must produce Canvas-compatible
// geometry from a good payload, an empty one and a malformed one, and every
// page must stay inside the screen. No GPU, no game.
#include "link_ui_panel.h"
#include "link_imgui_mesh.h"
#include <iostream>
#include <limits>
#include <string>

namespace {
using nlohmann::json;
int checks=0;
void check(bool ok,const char* what) {
    if(!ok) throw std::runtime_error(std::string("FAILED: ")+what);
    ++checks;std::cout<<"PASS "<<what<<'\n';
}
void checkNear(bool ok,const char* what) {check(ok,what);}

constexpr ImTextureID kAtlas=ImTextureID(7);

struct Harness {
    eflink::UiPanel panel;
    json status;
    int calls=0;
    std::string lastAction;
    json lastRequest;
    explicit Harness(json payload) :status(std::move(payload)) {
        panel.request=[this](const json& input)->json {
            ++calls;lastAction=input.value("action",std::string{});lastRequest=input;
            if(lastAction=="error") return {{"error","测试错误"}};
            return status;
        };
    }
    // Draws one frame and returns the geometry the Unity canvas would receive.
    eflink::imguimesh::Mesh frame(int tabIndex,float width,float height) {
        auto& io=ImGui::GetIO();
        io.DisplaySize={width,height};io.DeltaTime=1.f/60.f;
        panel.tab=tabIndex;
        ImGui::NewFrame();
        panel.draw();
        ImGui::Render();
        return eflink::imguimesh::convert(*ImGui::GetDrawData(),kAtlas);
    }
    // Same frame, but with an explicit mouse state -- what the Win32 backend
    // produces after the player presses the left button over the title strip.
    eflink::imguimesh::Mesh frameMouse(int tabIndex,float width,float height,ImVec2 mouse,bool down) {
        auto& io=ImGui::GetIO();
        io.DisplaySize={width,height};io.DeltaTime=1.f/60.f;
        io.AddMousePosEvent(mouse.x,mouse.y);
        io.AddMouseButtonEvent(0,down);
        panel.tab=tabIndex;
        ImGui::NewFrame();
        panel.draw();
        ImGui::Render();
        return eflink::imguimesh::convert(*ImGui::GetDrawData(),kAtlas);
    }
    void expectFrame(int tabIndex,const char* what,float width=1600,float height=900) {
        const auto mesh=frame(tabIndex,width,height);
        check(!mesh.colors.empty(),what);
        check(mesh.colors.size()%3==0,"geometry is whole triangles");
        check(mesh.indices.size()==mesh.colors.size(),"every vertex has an index");
        for(std::size_t i=0;i<mesh.colors.size();++i) {
            const float x=mesh.positions[i*3],y=mesh.positions[i*3+1];
            if(!std::isfinite(x)||!std::isfinite(y)) throw std::runtime_error("non-finite geometry");
            if(x<0||x>width||y<0||y>height) throw std::runtime_error("geometry outside the screen");
        }
        (void)eflink::imguimesh::chunks(mesh.colors.size());
    }
};
json goodStatus(const char* role) {
    const bool online=std::string(role)!="offline";
    const bool guest=std::string(role)=="client";
    json peers=json::array();
    if(online) {
        peers.push_back({{"id",1},{"name","管理员"},{"host",true},{"local",true},{"map","map01"},{"valid",true},{"same_map",true},{"state_age_ms",30}});
        peers.push_back({{"id",2},{"name","Yvonne"},{"host",false},{"local",false},{"map","map01"},{"address","192.168.2.9:51967"},{"rtt_ms",18},
            {"valid",true},{"same_map",true},{"state_age_ms",120},{"distance",12.4f}});
        peers.push_back({{"id",3},{"name","拖进来看看"},{"host",false},{"local",false},{"map","map02"},{"valid",true},{"same_map",false},{"state_age_ms",2400}});
    }
    return {
        {"role",role},{"name","管理员"},{"rtt_ms",18},{"peers",peers},
        {"rooms",json::array({{{"name","EF Link 测试房间"},{"host","管理员"},{"players",3},{"max_players",8},{"password",false},{"map","map01"},{"address","192.168.2.3:47615"}}})},
        {"chat",json::array({{{"serial",1},{"system",true},{"text","Yvonne 加入了房间"}},{{"serial",2},{"name","管理员"},{"text","我在四号谷地枢纽"},{"system",false}}})},
        {"chat_serial",2},{"last_error",""},{"hook_ready",online},{"testing_invincible",false},
        {"addresses",json::array({{{"ip","192.168.2.3"},{"adapter","以太网"}}})},
        {"room",{{"name","EF Link 测试房间"},{"max_players",8},{"port",47615}}},
        {"game",{{"map_id","map01"},{"local_bones",64},{"error",""},
            {"world_replication",{{"matched",12},{"missing",1},{"teleport","idle"},{"error",""},
                {"guest_policy",{{"restricted",true},{"ready",false},{"error","隔离未就绪"}}},
                {"official_teleport",{{"map","map04"},{"hub","hub_04_a"}}},
                {"host_map_change",{{"pending",guest},{"level","map02_lv001"},{"deferred",false},{"token",77}}}}}}},
        {"world_session",{{"ready",true},{"epoch",4211},{"error",""},
            {"entry",{{"active",false},{"phase","awaiting_host_world"},
                {"blockers",json::array({"host_world_isolation","guest_players","host_scene_rules"})}}}}},
        {"configuration",{{"player_name","管理员"},{"room_name","EF Link 测试房间"},{"port",47615},{"max_players",8},{"menu_key",187},
            {"show_remote_players",true},{"show_player_markers",true},{"follow_host_world",true},{"sync_combat",false},
            {"saved_servers",json::array({{{"address","192.168.2.3:47615"},{"name","客厅主机"}}})}}},
        {"traffic",{{"states_in",142},{"udp_pongs",9}}}};
}
}

// A failed check throws, so main must turn that into a visible report: stdout is
// block-buffered when redirected and an uncaught exception (std::terminate ->
// abort) would throw away every PASS line printed so far.
int run();
int main() {
    try {
        return run();
    } catch(const std::exception& error) {
        std::cout.flush();
        std::cerr<<"\n[FAIL] "<<error.what()<<'\n'<<std::flush;
        return 1;
    } catch(...) {
        std::cout.flush();
        std::cerr<<"\n[FAIL] unknown exception\n"<<std::flush;
        return 1;
    }
}

int run() {
    // Exactly the initialisation path the in-game module and the preview window
    // use. createContext() owns the "context first, then theme, then fonts"
    // order that the module got wrong once -- applyTheme() touches
    // ImGui::GetStyle(), which asserts on a null GImGui and reached players as a
    // Visual C++ runtime dialog while eflink.core.dll loaded.
    // The module used to call applyTheme() before CreateContext() and aborted
    // with "No current context" (imgui.cpp:3310, ImGui::GetStyle) while
    // eflink.core.dll loaded. Asserting the precondition keeps this test pinned
    // to that crash: a fresh process with no context at all, then createContext().
    check(ImGui::GetCurrentContext()==nullptr,"the test starts with no ImGui context, like the loading module");
    ImGuiContext* context=eflink::ui::createContext(0.f);
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={1600,900};io.DeltaTime=1.f/60.f;
    check(context!=nullptr&&context==ImGui::GetCurrentContext(),"createContext makes its context current");
    check(eflink::ui::scale>0.f,"createContext installs a DPI scale before the fonts are sized");
    // loadFonts() builds the atlas, and ImFontAtlasBuildWithStbTruetype() resets
    // TexID back to 0. The mesh converter drops every command whose texture id
    // does not match, so assigning the handle before the build silently yields
    // an empty mesh instead of a visible failure.
    io.Fonts->SetTexID(kAtlas);
    check(io.Fonts->TexID==kAtlas,"the atlas texture id survives the font build");

    check(eflink::ui::fonts.regular!=nullptr,"a menu font is always available");
    check(eflink::ui::fonts.cjk,"the Windows CJK face is used when present");
    check(eflink::ui::fonts.regular->FindGlyph(ImWchar(0x7EC8))!=nullptr,"Chinese labels have glyphs, not a fallback box");
    check(eflink::ui::fonts.regular->FindGlyph(ImWchar(0x623F))!=nullptr,"secondary Chinese glyphs are baked");

    // ---- the menu is a real window: draggable, resizable, clamped ----
    // Runs before the role matrix on purpose. A client payload carries a pending
    // map change, which raises a modal prompt, and ImGui makes every other window
    // non-hoverable while a modal is focused -- once closed, the stale NavWindow
    // keeps blocking hover for another frame. Measuring the drag here keeps these
    // checks independent of that.
    // The in-game surface reads the same UiPanel, so this also pins what the game
    // relies on: the title line is a hit target even though its label is only
    // painted into the draw list, and a drag that runs past the edge must not park
    // the menu off-screen where the player could never reach it again.
    {
        auto& input=ImGui::GetIO();
        const bool trickle=input.ConfigInputTrickleEventQueue;
        input.ConfigInputTrickleEventQueue=false;   // apply every injected event at once
        const auto gripOf=[](const eflink::UiPanel& panel) {
            return ImVec2{(panel.dragMin.x+panel.dragMax.x)*.5f,(panel.dragMin.y+panel.dragMax.y)*.5f};
        };

        // -- the title strip moves the window ---------------------------------
        Harness dragged(goodStatus("offline"));
        dragged.frame(0,1400,860);
        const ImVec2 opened=dragged.panel.layoutPos;
        const ImVec2 openedSize=dragged.panel.layoutSize;
        check(openedSize.x>eflink::ui::px(600)&&openedSize.y>eflink::ui::px(400),"the menu opens with a usable rectangle");
        check(opened.x>=0.f&&opened.y>=0.f&&opened.x+openedSize.x<=1400.f&&opened.y+openedSize.y<=860.f,"the opening rectangle is inside the screen");
        const ImVec2 grip=gripOf(dragged.panel);
        check(grip.x>opened.x&&grip.y>opened.y&&grip.x<opened.x+openedSize.x,"the drag strip sits inside the window");
        dragged.frameMouse(0,1400,860,grip,false);
        check(dragged.panel.dragHovered&&!dragged.panel.dragActive,"the painted title line is a drag target");
        dragged.frameMouse(0,1400,860,grip,true);
        check(dragged.panel.dragActive,"pressing the title line starts a window drag");
        dragged.frameMouse(0,1400,860,{grip.x+60.f,grip.y+30.f},true);
        dragged.frameMouse(0,1400,860,{grip.x+60.f,grip.y+30.f},false);
        check(!dragged.panel.dragActive,"releasing the mouse ends the drag");
        check(std::abs(dragged.panel.layoutPos.x-(opened.x+60.f))<2.f&&std::abs(dragged.panel.layoutPos.y-(opened.y+30.f))<2.f,
            "the title strip drags the window with the mouse");
        dragged.frame(0,1400,860);
        check(std::abs(dragged.panel.layoutPos.x-(opened.x+60.f))<2.f&&std::abs(dragged.panel.layoutPos.y-(opened.y+30.f))<2.f,
            "the menu stays where the player dragged it");

        // -- a drag that runs past the edge is clamped -------------------------
        const ImVec2 movedGrip=gripOf(dragged.panel);
        dragged.frameMouse(0,1400,860,movedGrip,false);
        dragged.frameMouse(0,1400,860,movedGrip,true);
        dragged.frameMouse(0,1400,860,{1390.f,850.f},true);
        dragged.frameMouse(0,1400,860,{1390.f,850.f},false);
        const ImVec2 clamped=dragged.panel.layoutPos;
        const ImVec2 clampedSize=dragged.panel.layoutSize;
        check(clamped.x>opened.x+60.f&&clamped.y>opened.y+30.f,"the clamped drag still moved the menu");
        check(clamped.x+clampedSize.x<=1400.f&&clamped.y+clampedSize.y<=860.f,"a drag past the edge keeps the menu on screen");

        // -- the corner grip resizes the window -------------------------------
        Harness resized(goodStatus("offline"));
        resized.frame(0,1400,860);
        const ImVec2 before=resized.panel.layoutSize;
        const ImVec2 corner{resized.panel.layoutPos.x+before.x-eflink::ui::px(5),resized.panel.layoutPos.y+before.y-eflink::ui::px(5)};
        resized.frameMouse(0,1400,860,corner,false);
        resized.frameMouse(0,1400,860,corner,true);
        resized.frameMouse(0,1400,860,{corner.x-120.f,corner.y-80.f},true);
        resized.frameMouse(0,1400,860,{corner.x-120.f,corner.y-80.f},false);
        check(std::abs(resized.panel.layoutSize.x-(before.x-120.f))<2.f&&std::abs(resized.panel.layoutSize.y-(before.y-80.f))<2.f,
            "the bottom-right grip resizes the window with the mouse");
        resized.frame(0,1400,860);
        check(resized.panel.layoutSize.x<before.x&&resized.panel.layoutSize.y<before.y,"the resized menu keeps its new size");
        input.ConfigInputTrickleEventQueue=trickle;
    }

    // ---- every page renders for every role ----
    for(const char* role:{"offline","connecting","host","client"}) {
        Harness harness(goodStatus(role));
        for(int tab=0;tab<6;++tab) {
            char what[96]{};
            std::snprintf(what,sizeof what,"page %d draws Canvas geometry as %s",tab,role);
            harness.expectFrame(tab,what);
        }
    }
    // The host page only shows room addresses while hosting; the guest page only
    // shows players when connected. Drawing both is covered above.

    // ---- the menu fits every window size it can meet ----
    // 1024x600 is the smallest head the menu is designed for: the navigation
    // column has room for all six rows there, so it must stay scroll-free. A
    // smaller display still has to render valid geometry that stays on screen,
    // but the six fixed rows cannot fit, so the column is allowed to scroll.
    struct Layout {float width,height;bool railFits;};
    for(const Layout item:{Layout{1280,720,true},Layout{1024,600,true},Layout{800,450,false},Layout{3840,2160,true}}) {
        Harness harness(goodStatus("client"));
        for(int tab=0;tab<6;++tab) harness.expectFrame(tab,"the menu stays inside the screen",item.width,item.height);
        check(harness.panel.layoutPos.x>=0.f&&harness.panel.layoutPos.y>=0.f&&
            harness.panel.layoutPos.x+harness.panel.layoutSize.x<=item.width&&
            harness.panel.layoutPos.y+harness.panel.layoutSize.y<=item.height,
            "the menu window itself stays inside the screen");
        if(item.railFits) {
            char railNote[160]{};
            std::snprintf(railNote,sizeof railNote,"the navigation rail never needs a scrollbar (%.0fx%.0f rail=%.0f slack=%.0f)",
                item.width,item.height,harness.panel.railHeight,harness.panel.railSlack);
            check(!harness.panel.railOverflow,railNote);
        }
    }

    // ---- the HUD overlay is clipped to the screen too ----
    {
        const auto markers=json{{"markers",json::array({
            {{"name","Yvonne"},{"x",.42f},{"y",.36f},{"distance",12.4}},
            {{"name","身后的人"},{"x",.5f},{"y",.5f},{"behind",true},{"distance",31.7}},
            {{"name","远处的人"},{"x",1e30f},{"y",-1e30f},{"distance",1e30f}},
            {{"name","异地图"},{"other_map",true},{"map","map02"}}})}};
        auto& display=ImGui::GetIO().DisplaySize;
        display={1600,900};ImGui::GetIO().DeltaTime=1.f/60.f;
        ImGui::NewFrame();eflink::drawHud(markers);ImGui::Render();
        const auto mesh=eflink::imguimesh::convert(*ImGui::GetDrawData(),kAtlas);
        check(!mesh.colors.empty(),"markers and off-screen arrows are drawn");
        for(std::size_t i=0;i<mesh.colors.size();++i) {
            const float x=mesh.positions[i*3],y=mesh.positions[i*3+1];
            if(x<0||x>1600||y<0||y>900) throw std::runtime_error("marker geometry outside the screen");
        }
        check(true,"clamped markers stay on screen, including one behind the camera");
        // An empty snapshot must not emit anything at all.
        ImGui::NewFrame();eflink::drawHud(json::object());ImGui::Render();
        check(eflink::imguimesh::convert(*ImGui::GetDrawData(),kAtlas).colors.empty(),"no markers means no overlay geometry");
    }

    // ---- malformed payloads degrade instead of taking the menu down ----
    for(const auto& payload:std::vector<json>{
            json(nullptr),json(42),json("not an object"),json::array(),json::object(),
            json{{"role",5},{"name",nullptr},{"peers",json::object()},{"rooms","x"},{"chat",7},
                {"configuration",json::array()},{"world_session",9},{"game","no"},{"traffic",false},
                {"rtt_ms","slow"},{"hook_ready","yes"},{"addresses",json::array({json(1),json::object()})}},
            json{{"role","host"},{"peers",json::array({{{"id","x"},{"name",42},{"distance","far"},{"state_age_ms",nullptr},{"host",1},{
                "map",json::array()}}})},{"rooms",json::array({json(3)})},
                {"configuration",{{"saved_servers",json::array({{{"address",7}}})}}}},
            json{{"role","client"},{"world_session",{{"entry",{{"blockers",json::array({json(9),json("host_scene_rules")})}}}}},
                {"game",{{"world_replication",{{"host_map_change",{{"pending",true},{"token","x"}}}}}}}}}) {
        Harness harness(payload);
        for(int tab=0;tab<6;++tab) harness.expectFrame(tab,"a malformed payload still draws a usable page");
    }
    // Overlong single-byte-hostile strings must not break layout or UTF-8.
    {
        std::string longName,longText;
        for(int i=0;i<400;++i) longName+="房间";
        for(int i=0;i<600;++i) longText+="很长的一条消息，";
        auto payload=goodStatus("client");
        payload["configuration"]["room_name"]=longName;
        payload["peers"][1]["name"]=longName;
        payload["peers"][1]["map"]=longName;
        payload["chat"]=json::array({{{"serial",1},{"name",longName},{"text",longText},{"system",false}}});
        payload["rooms"][0]["name"]=longName;
        payload["addresses"]=json::array({{{"ip",longName},{"adapter",longName}}});
        Harness harness(payload);
        for(int tab=0;tab<6;++tab) harness.expectFrame(tab,"overlong text is truncated instead of overflowing");
    }
    // The menu must recover once a good payload arrives again.
    {
        Harness harness(json{{"role",3}});
        harness.frame(3,1600,900);
        harness.status=goodStatus("host");
        harness.panel.lastRefresh=0;
        harness.panel.refresh();
        check(harness.panel.state.value("role",std::string{})==std::string("host"),"a later good payload restores the menu");
        harness.expectFrame(3,"the menu keeps drawing after a bad frame");
    }

    // ---- action contract ----
    {
        Harness harness(goodStatus("offline"));
        // Nothing has been drawn yet, so the configuration is not loaded. Neither
        // action may reach the module carrying an empty identity in that window.
        harness.panel.save();
        check(harness.calls==0,"saving before the configuration loads never reaches the module");
        check(harness.panel.feedback==std::string("请输入玩家名"),"saving before the configuration loads explains itself");
        harness.panel.address[0]=0;
        harness.panel.join();
        check(harness.calls==0,"an empty address never reaches the module");
        check(harness.panel.feedback==std::string("请输入房主地址"),"an empty address explains itself in the menu");
        // Loading the configuration is what a drawn frame does.
        harness.panel.lastRefresh=0;harness.panel.refresh();
        check(harness.panel.name[0]!=0,"the first status payload fills in the player name");
        eflink::UiPanel::copyText(harness.panel.address,sizeof harness.panel.address,"192.168.2.3:47615");
        harness.panel.join();
        check(harness.lastAction=="join"&&harness.lastRequest.value("address",std::string{})=="192.168.2.3:47615","joining sends the address");
        harness.panel.save();
        check(harness.lastAction=="settings"&&harness.lastRequest["configuration"].value("player_name",std::string{}).size()>0,"saving sends a settings patch");
        const int before=harness.calls;
        harness.panel.refresh();harness.panel.refresh();
        check(harness.calls==before,"status polling is throttled");
        harness.panel.lastRefresh=0;harness.panel.refresh();
        check(harness.calls==before+1,"status polling resumes after the throttle window");
        Harness failing(json{{"error","测试错误"}});
        failing.panel.lastRefresh=0;failing.panel.refresh();
        check(failing.panel.feedback==std::string("测试错误"),"a module error is surfaced in the menu");
    }

    ImGui::DestroyContext();
    std::cout<<checks<<" UI checks passed\n";
    return 0;
}
