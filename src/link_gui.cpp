// EF Link interface: transparent DirectComposition overlay over the game window.
// HUD (name tags, room roster) is always drawn; the menu key turns the overlay
// interactive and shows the menu. Requests are executed on the game thread.
#include "link_version.h"
#include "link_gui.h"
#include "link_settings.h"
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
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

using Microsoft::WRL::ComPtr;
using nlohmann::json;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
thread_local ImGuiContext* EFLinkImGuiContext=nullptr;

namespace eflink {
namespace {
std::uint64_t now() {return GetTickCount64();}
template<class T> bool ready(const std::future<T>& f) {return f.valid()&&f.wait_for(std::chrono::seconds(0))==std::future_status::ready;}
std::string str(const json& j,const char* key,const std::string& fallback={}) {
    auto it=j.find(key);return it!=j.end()&&it->is_string()?it->get<std::string>():fallback;
}
void copyText(char* dst,std::size_t capacity,const std::string& text) {strncpy_s(dst,capacity,text.c_str(),_TRUNCATE);}

// ---- game window ----
HWND findGame() {
    HWND result=nullptr;
    EnumWindows([](HWND w,LPARAM arg)->BOOL {
        DWORD pid=0;GetWindowThreadProcessId(w,&pid);wchar_t klass[64]{};GetClassNameW(w,klass,64);
        if(pid==GetCurrentProcessId()&&IsWindowVisible(w)&&wcscmp(klass,L"UnityWndClass")==0&&GetWindow(w,GW_OWNER)==nullptr) {
            *reinterpret_cast<HWND*>(arg)=w;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}

// ---- look ----
namespace color {
constexpr ImVec4 base{.047f,.055f,.063f,.97f},panel{.071f,.082f,.094f,1},card{.094f,.106f,.122f,1},cardHover{.118f,.133f,.153f,1};
constexpr ImVec4 line{.176f,.196f,.22f,1},text{.91f,.93f,.94f,1},dim{.55f,.59f,.62f,1},faint{.36f,.39f,.42f,1};
constexpr ImVec4 accent{.89f,.95f,.24f,1},accentDim{.54f,.58f,.16f,1},ink{.06f,.07f,.04f,1};
constexpr ImVec4 ok{.31f,.83f,.6f,1},warn{1.f,.72f,.29f,1},bad{1.f,.42f,.35f,1},info{.36f,.72f,1.f,1};
ImU32 u32(ImVec4 c,float alpha=1.f) {c.w*=alpha;return ImGui::ColorConvertFloat4ToU32(c);}
}
struct Fonts {ImFont* body=nullptr;ImFont* note=nullptr;ImFont* title=nullptr;ImFont* mono=nullptr;};
Fonts loadFonts(float scale) {
    auto& io=ImGui::GetIO();Fonts f;
    wchar_t winDir[MAX_PATH]{};GetWindowsDirectoryW(winDir,MAX_PATH);
    const auto regular=std::filesystem::path(winDir)/L"Fonts"/L"msyh.ttc";
    if(std::filesystem::is_regular_file(regular)) {
        const auto* cjk=io.Fonts->GetGlyphRangesChineseFull();
        f.body=io.Fonts->AddFontFromFileTTF(regular.string().c_str(),17*scale,nullptr,cjk);
        f.note=io.Fonts->AddFontFromFileTTF(regular.string().c_str(),15*scale,nullptr,cjk);
    }
    if(!f.body) f.body=io.Fonts->AddFontDefault();
    if(!f.note) f.note=f.body;
    f.title=f.mono=f.body;io.FontDefault=f.body;
    return f;
}
void applyTheme(float scale) {
    ImGui::GetStyle()=ImGuiStyle();ImGui::StyleColorsDark();
    auto& s=ImGui::GetStyle();s.FramePadding={8,5};s.ItemSpacing={8,6};s.WindowPadding={12,10};
    s.ScaleAllSizes(scale);
}

// ---- widgets ----
bool accentButton(const char* label,ImVec2 size={0,0}) {return ImGui::Button(label,size);}
bool dangerButton(const char* label,ImVec2 size={0,0}) {return ImGui::Button(label,size);}
void chip(const char* text,ImVec4 tint) {ImGui::TextColored(tint,"[%s]",text);}
// A bordered group with a heading; content goes between begin/end.
void beginCard(const char* id,const char* title,ImFont*,ImVec2 size={0,0}) {
    ImGui::BeginChild(id,size,ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding|(size.y==0?ImGuiChildFlags_AutoResizeY:0));
    if(title&&*title) ImGui::SeparatorText(title);
}
void endCard() {ImGui::EndChild();}
void label(const char* text) {ImGui::TextDisabled("%s",text);}
std::string formatPosition(const json& p) {
    if(!p.is_array()||p.size()!=3) return "—";
    char text[96];std::snprintf(text,sizeof text,"%.1f, %.1f, %.1f",p[0].get<double>(),p[1].get<double>(),p[2].get<double>());return text;
}
const char* blockerText(const std::string& b) {
    return b=="host_world_isolation"?"房主世界隔离":b=="host_logical_players"?"房主逻辑玩家":b=="host_combat_settlement"?"房主战斗结算":
           b=="host_scene_rules"?"房主地图与副本规则":b=="guest_map_loading"?"加载房主地图":b=="guest_entity_replicas"?"重建房主实体":
           b=="guest_world_isolation"?"房客世界隔离":b=="guest_logical_players"?"房客逻辑玩家":b=="host_player_entity"?"房主侧角色":b.c_str();
}

// ---- interface state and drawing ----
struct Ui {
    std::function<std::future<json>(const json&)> post;
    Fonts fonts;
    json state=json::object(),hud=json::object();
    std::future<json> statusCall,hudCall;std::uint64_t statusSent=0,hudSent=0;
    struct Pending {std::future<json> result;std::string action;std::uint64_t sent=0;};std::vector<Pending> actions;
    std::string toast;std::uint64_t toastAt=0;bool toastError=false;
    int page=0;bool open=false,closeRequested=false,initialized=false,leaveConfirm=false,kickPending=false;int kickId=-1;
    char name[65]{},room[65]{},hostPassword[129]{},address[257]{},joinPassword[129]{},serverName[129]{},chat[1001]{};
    int port=47615,limit=8,key=kDefaultMenuKey;bool remote=true,squad=true,replay=true,outline=true,markers=true,follow=true,combat=true,pvp=false,pvpRevive=true;float pvpScale=1.f;
    std::uint64_t lastChat=0,lastMapPrompt=0;
    bool nameEdited=false; // the display name field was edited by the player
    // Chat on the HUD: recent lines stay for a while; Enter opens a one-line input.
    bool chatting=false,chatFocus=false,chatPrimed=false;char hudChat[1001]{};std::map<std::uint64_t,std::uint64_t> chatSeen;
    void say(std::string text,bool error) {toast=std::move(text);toastAt=now();toastError=error;}
    void act(json input) {const auto a=str(input,"action");actions.push_back({post(input),a,now()});}
    void init() {
        if(initialized||state.empty()) return;
        initialized=true;copyText(name,sizeof name,str(state,"name"));
        const auto c=state.value("configuration",json::object());copyText(room,sizeof room,str(c,"room_name"));
        port=c.value("port",47615);limit=c.value("max_players",8);key=c.value("menu_key",kDefaultMenuKey);
        remote=c.value("show_remote_players",true);squad=c.value("show_remote_squad",true);replay=c.value("replay_skills",true);outline=c.value("outline_teammates",true);pvp=c.value("room_pvp",false);pvpScale=c.value("room_pvp_scale",1.f);pvpRevive=c.value("room_pvp_revive",true);markers=c.value("show_player_markers",true);
        follow=c.value("follow_host_world",true);combat=c.value("sync_combat",true);
    }
    void poll(bool menuOpen) {
        const auto t=now();
        if(ready(statusCall)) {auto r=statusCall.get();if(r.contains("role")) {state=std::move(r);init();key=state.value("configuration",json::object()).value("menu_key",key);
            if(!nameEdited) copyText(name,sizeof name,str(state,"name"));}}
        else if(statusCall.valid()&&t-statusSent>5000) statusCall={}; // the game thread was busy (loading); ask again
        if(!statusCall.valid()&&t-statusSent>=(menuOpen?250u:500u)) {statusSent=t;statusCall=post({{"action","status"}});}
        if(ready(hudCall)) {auto r=hudCall.get();if(r.contains("markers")) hud=std::move(r);}
        else if(hudCall.valid()&&t-hudSent>5000) hudCall={};
        if(!hudCall.valid()&&t-hudSent>=50) {hudSent=t;hudCall=post({{"action","hud"}});}
        for(auto it=actions.begin();it!=actions.end();) {
            if(ready(it->result)) {
                auto r=it->result.get();
                if(r.contains("error")) say(r["error"].is_string()?r["error"].get<std::string>():"操作失败",true);
                else if(r.contains("role")) state=std::move(r);
                it=actions.erase(it);
            } else if(t-it->sent>15000) it=actions.erase(it);
            else ++it;
        }
    }
    std::string role() const {return str(state,"role","offline");}
    bool online() const {const auto r=role();return r=="host"||r=="client";}
    void saveSettings() {
        json patch={{"room_name",room},{"port",port},{"max_players",limit},{"menu_key",key},
            {"show_remote_players",remote},{"show_remote_squad",squad},{"replay_skills",replay},{"outline_teammates",outline},{"show_player_markers",markers},{"follow_host_world",follow},{"sync_combat",combat}};
        if(nameEdited) {patch["player_name"]=name;nameEdited=false;}
        act({{"action","settings"},{"configuration",patch}});
        say("设置已保存",false);
    }
    // Display and co-op switches take effect immediately (and are saved).
    void applySwitches() {
        act({{"action","settings"},{"configuration",{{"show_remote_players",remote},{"show_remote_squad",squad},{"replay_skills",replay},{"outline_teammates",outline},
            {"show_player_markers",markers},{"follow_host_world",follow},{"sync_combat",combat}}}});
    }
    void join() {if(*address) act({{"action","join"},{"address",address},{"password",joinPassword}});else say("请输入房主地址",true);}

    // ---- HUD ----
    static void shadowText(ImDrawList* d,ImVec2 at,ImU32 tint,const char* text) {
        d->AddText({at.x+1,at.y+1},IM_COL32(0,0,0,170),text);d->AddText(at,tint,text);
    }
    // Endfield-style markers: a small accent diamond over each other player with
    // "02 name 23m" above it; an accent arrow at the screen edge when off-screen.
    void drawHud(ImVec2 size) {
        auto* d=ImGui::GetBackgroundDrawList();
        const bool tags=online()&&state.value("configuration",json::object()).value("show_player_markers",true);
        if(tags) {
            ImGui::PushFont(fonts.note);
            const auto accent=color::u32(color::accent),white=color::u32(color::text),ink=color::u32(color::ink);
            std::map<int,float> distances;
            for(const auto& m:hud.value("markers",json::array())) {
                if(m.value("other_map",false)||m.value("self",false)) continue;
                const int id=m.value("id",0);const float dist=m.value("distance",0.f);distances[id]=dist;
                float x=m.value("x",.5f)*size.x,y=m.value("y",.5f)*size.y;
                if(!std::isfinite(x)||!std::isfinite(y)) continue;
                const bool behind=m.value("behind",false);
                char idText[8];std::snprintf(idText,sizeof idText,"%02d",id);
                char distText[24];std::snprintf(distText,sizeof distText,"%.0fm",dist);
                if(behind||x<40||y<40||x>size.x-40||y>size.y-40) {
                    float dx=x-size.x*.5f,dy=y-size.y*.5f;if(behind) {dx=-dx;dy=-dy;}
                    if(std::abs(dx)+std::abs(dy)<.001f) dy=-1;
                    const float len=std::sqrt(dx*dx+dy*dy),ux=dx/len,uy=dy/len;
                    const float k=1.f/(std::max)(std::abs(dx)/(std::max)(1.f,size.x*.5f-44),std::abs(dy)/(std::max)(1.f,size.y*.5f-44));
                    const float ex=size.x*.5f+dx*k,ey=size.y*.5f+dy*k;
                    d->AddTriangleFilled({ex+ux*9,ey+uy*9},{ex-uy*7-ux*5,ey+ux*7-uy*5},{ex+uy*7-ux*5,ey-ux*7-uy*5},accent);
                    char label[48];std::snprintf(label,sizeof label,"%s %s",idText,distText);
                    const auto ts=ImGui::CalcTextSize(label);
                    shadowText(d,{ex-ux*24-ts.x*.5f,ey-uy*24-ts.y*.5f},white,label);
                    continue;
                }
                const float r=4.5f;const ImVec2 c{x,y-4};
                d->AddQuadFilled({c.x,c.y-r},{c.x+r,c.y},{c.x,c.y+r},{c.x-r,c.y},accent);
                const auto who=str(m,"name");
                const auto is=ImGui::CalcTextSize(idText),ns=ImGui::CalcTextSize(who.c_str()),ds=ImGui::CalcTextSize(distText);
                const float w=is.x+6+8+ns.x+8+ds.x,left=x-w*.5f,top=c.y-r-8-is.y;
                d->AddRectFilled({left,top-1},{left+is.x+6,top+is.y+1},accent);
                d->AddText({left+3,top},ink,idText);
                shadowText(d,{left+is.x+14,top},white,who.c_str());
                shadowText(d,{left+is.x+14+ns.x+8,top},accent,distText);
            }
            drawRoster(size,distances);
            ImGui::PopFont();
        }
        if(online()) drawChat(size);else chatting=false;
        if(!toast.empty()&&now()-toastAt<4000&&!open) {
            ImGui::PushFont(fonts.body);const auto ts=ImGui::CalcTextSize(toast.c_str());
            const ImVec2 p{size.x*.5f-ts.x*.5f-16,size.y-140};
            d->AddRectFilled(p,{p.x+ts.x+32,p.y+ts.y+18},color::u32(color::base,.9f),6);
            d->AddText({p.x+16,p.y+9},color::u32(toastError?color::bad:color::accent),toast.c_str());ImGui::PopFont();
        }
        const auto change=state.value("game",json::object()).value("world_replication",json::object()).value("host_map_change",json::object());
        if(!open&&role()=="client"&&change.value("pending",false)) {
            ImGui::PushFont(fonts.body);const std::string text="房主已前往 "+str(change,"level")+"，按菜单键选择是否跟随";
            const auto ts=ImGui::CalcTextSize(text.c_str());const ImVec2 p{size.x*.5f-ts.x*.5f-16,90};
            d->AddRectFilled(p,{p.x+ts.x+32,p.y+ts.y+18},color::u32(color::base,.9f),6);
            d->AddRect(p,{p.x+ts.x+32,p.y+ts.y+18},color::u32(color::accent,.8f),6);
            d->AddText({p.x+16,p.y+9},color::u32(color::accent),text.c_str());ImGui::PopFont();
        }
    }
    void drawChat(ImVec2 size) {
        const auto lines=state.value("chat",json::array());const auto t=now();
        if(!chatPrimed) {for(const auto& l:lines) chatSeen[l.value("serial",std::uint64_t(0))]=0;chatPrimed=true;}
        for(const auto& l:lines) {const auto s=l.value("serial",std::uint64_t(0));if(!chatSeen.contains(s)) chatSeen[s]=t;}
        std::vector<const json*> show;
        for(auto it=lines.rbegin();it!=lines.rend()&&show.size()<(chatting?10u:6u);++it) {
            const auto seen=chatSeen[it->value("serial",std::uint64_t(0))];
            if(chatting||(seen&&t-seen<12000)) show.push_back(&*it);
        }
        ImGui::PushFont(fonts.note);
        auto* d=ImGui::GetBackgroundDrawList();
        const float line=ImGui::GetTextLineHeight()+4,left=24,width=(std::min)(560.f,size.x*.42f),bottom=size.y*.78f;
        float y=bottom-line*float(show.size());
        if(chatting&&!show.empty()) d->AddRectFilled({left-8,y-6},{left+width,bottom+2},IM_COL32(0,0,0,110),2);
        for(auto it=show.rbegin();it!=show.rend();++it,y+=line) {
            const auto& l=**it;const auto seen=chatSeen[l.value("serial",std::uint64_t(0))];
            const float alpha=chatting?1.f:(std::clamp)(float(12000-(t-seen))/2000.f,0.f,1.f);
            d->PushClipRect({left-8,y-2},{left+width,y+line},true);
            if(l.value("system",false)) shadowText(d,{left,y},color::u32(color::dim,alpha),("· "+str(l,"text")).c_str());
            else {
                const auto who=str(l,"name")+"：";shadowText(d,{left,y},color::u32(color::accent,alpha),who.c_str());
                shadowText(d,{left+ImGui::CalcTextSize(who.c_str()).x,y},color::u32(color::text,alpha),str(l,"text").c_str());
            }
            d->PopClipRect();
        }
        if(chatting) {
            ImGui::SetNextWindowPos({left-8,bottom+6});ImGui::SetNextWindowSize({width+8,0});
            ImGui::SetNextWindowBgAlpha(.75f);
            if(ImGui::Begin("##hudchat",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove)) {
                if(chatFocus) {ImGui::SetKeyboardFocusHere();chatFocus=false;}
                ImGui::SetNextItemWidth(-1);
                if(ImGui::InputTextWithHint("##hudchatinput","输入消息，Enter 发送，Esc 取消",hudChat,sizeof hudChat,ImGuiInputTextFlags_EnterReturnsTrue)) {
                    if(*hudChat) act({{"action","chat"},{"text",hudChat}});
                    hudChat[0]=0;chatting=false;
                }
                if(ImGui::IsKeyPressed(ImGuiKey_Escape)) {hudChat[0]=0;chatting=false;}
            }
            ImGui::End();
        }
        ImGui::PopFont();
    }
    // Member list at the right edge: id tile, name, distance. No coordinates.
    void drawRoster(ImVec2 size,const std::map<int,float>& distances) {
        auto* d=ImGui::GetBackgroundDrawList();
        const auto peers=state.value("peers",json::array());
        const float line=ImGui::GetTextLineHeight(),width=240,x=size.x-width-20;float y=size.y*.3f;
        const auto accent=color::u32(color::accent),white=color::u32(color::text),dim=color::u32(color::dim),ink=color::u32(color::ink);
        auto roomName=str(state.value("room",json::object()),"name");if(roomName.empty()) roomName=role()=="host"?"我的房间":"房间";
        const std::string head="EF LINK // "+roomName+(state.value("pvp",json::object()).value("active",false)?"  // PVP":"");
        d->AddRectFilled({x,y+2},{x+3,y+line-2},accent);
        shadowText(d,{x+9,y},accent,head.c_str());
        const bool tcpOnly=state.value("traffic",json::object()).value("tcp_only_links",0u)>0;
        if(role()=="client"||tcpOnly) {char rtt[32];if(tcpOnly)std::snprintf(rtt,sizeof rtt,"TCP");else std::snprintf(rtt,sizeof rtt,"%ums",state.value("rtt_ms",0u));
            shadowText(d,{x+width-ImGui::CalcTextSize(rtt).x,y},tcpOnly?color::u32(color::warn):dim,rtt);}
        y+=line+5;d->AddLine({x,y},{x+width,y},color::u32(color::accent,.4f));y+=6;
        for(const auto& peer:peers) {
            const int id=peer.value("id",0);const bool self=peer.value("local",false),host=peer.value("host",false),valid=peer.value("valid",false);
            char idText[8];std::snprintf(idText,sizeof idText,"%02d",id);const auto is=ImGui::CalcTextSize(idText);
            d->AddRectFilled({x,y},{x+is.x+6,y+line},!valid?color::u32(color::faint):self?white:accent);
            d->AddText({x+3,y},ink,idText);
            std::string who=str(peer,"name");if(host) who+="  房主";
            shadowText(d,{x+is.x+14,y},valid?white:dim,who.c_str());
            char right[24];
            if(self) std::snprintf(right,sizeof right,"你");
            else if(auto it=distances.find(id);it!=distances.end()) std::snprintf(right,sizeof right,"%.0fm",it->second);
            else std::snprintf(right,sizeof right,"—");
            shadowText(d,{x+width-ImGui::CalcTextSize(right).x,y},self?dim:accent,right);
            y+=line+6;
        }
    }

    // ---- menu ----
    bool selectPage=true;
    void drawMenu(ImVec2 size) {
        ImGui::SetNextWindowSize({(std::min)(900.f,size.x-40),(std::min)(620.f,size.y-40)},ImGuiCond_Appearing);
        ImGui::SetNextWindowPos({size.x*.5f,size.y*.5f},ImGuiCond_Appearing,{.5f,.5f});
        bool keep=true;
        if(ImGui::Begin("EF Link  " EFLINK_VERSION "###eflink",&keep,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings)) {
            const auto r=role();
            chip(r=="host"?"房主":r=="client"?"已连接":r=="connecting"?"连接中":"离线",r=="host"?color::accent:r=="client"?color::ok:r=="connecting"?color::warn:color::dim);
            ImGui::SameLine();ImGui::TextUnformatted(str(state,"name","—").c_str());
            if(online()) {ImGui::SameLine();ImGui::TextDisabled("房间：%s",str(state.value("room",json::object()),"name").c_str());}
            ImGui::SameLine();ImGui::TextDisabled("（%s 开关菜单，Esc 关闭）",key==VK_OEM_PLUS?"=":("F"+std::to_string(key-VK_F1+1)).c_str());
            if(ImGui::BeginTabBar("##tabs")) {
                const char* names[]={"联机","玩家","聊天","设置","诊断"};
                for(int i=0;i<5;++i) {
                    if(ImGui::BeginTabItem(names[i],nullptr,selectPage&&page==i?ImGuiTabItemFlags_SetSelected:0)) {
                        if(!selectPage) page=i;else if(i==page) selectPage=false; // requested tab is now current
                        ImGui::BeginChild("##page");
                        switch(i) {case 0: pageSession();break;case 1: pagePlayers();break;case 2: pageChat();break;case 3: pageSettings();break;default: pageDiagnostics();break;}
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            popups();
        }
        ImGui::End();
        if(!keep) closeRequested=true;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)&&!ImGui::GetIO().WantTextInput&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)) closeRequested=true;
    }
    void title(const char*,const char* subtitle=nullptr) {if(subtitle) label(subtitle);ImGui::Spacing();}
    void pageSession() {
        const auto r=role();
        if(!online()&&r!="connecting") {
            title("联机","开放自己的房间，或加入好友的房间。连接后可以关闭菜单，联机继续进行。");
            const float half=(ImGui::GetContentRegionAvail().x-14)*.5f;
            ImGui::BeginGroup();
            beginCard("##host","开放房间",fonts.title,{half,0});
            label("房间名称");ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##room","例如：周末一起打",room,sizeof room);
            label("房间密码（可留空）");ImGui::SetNextItemWidth(-1);ImGui::InputText("##hostpw",hostPassword,sizeof hostPassword,ImGuiInputTextFlags_Password);
            label("人数上限");ImGui::SetNextItemWidth(-1);ImGui::SliderInt("##limit",&limit,2,16);
            label("端口");ImGui::SetNextItemWidth(-1);ImGui::InputInt("##port",&port,0);port=(std::clamp)(port,1024,65535);
            ImGui::Checkbox("允许 PVP（玩家之间可以互相锁定、攻击）",&pvp);
            if(pvp) {
                ImGui::Indent();
                label("玩家之间的伤害倍率");ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##pvpscale",&pvpScale,.1f,5.f,"%.1f 倍");
                int mode=pvpRevive?0:1;label("被击败时");
                if(ImGui::RadioButton("回满生命",mode==0)) pvpRevive=true;
                ImGui::SameLine();if(ImGui::RadioButton("真正倒下",mode==1)) pvpRevive=false;
                ImGui::Unindent();
            }
            ImGui::Dummy({0,4});
            if(accentButton("开放房间",{-1,0})) {
                saveSettings();
                act({{"action","host"},{"room_name",room},{"password",hostPassword},{"port",port},{"max_players",limit},{"pvp",pvp},{"pvp_scale",pvpScale},{"pvp_revive",pvpRevive}});hostPassword[0]=0;
            }
            endCard();
            ImGui::EndGroup();ImGui::SameLine(0,14);
            ImGui::BeginGroup();
            beginCard("##join","加入房间",fonts.title,{half,0});
            label("房主地址（IP 或 IP:端口）");ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##address","192.168.1.20:47615",address,sizeof address);
            label("房间密码（可留空）");ImGui::SetNextItemWidth(-1);ImGui::InputText("##joinpw",joinPassword,sizeof joinPassword,ImGuiInputTextFlags_Password);
            if(accentButton("连接")) join();
            ImGui::SameLine();if(ImGui::Button("收藏地址",{-1,0})&&*address) act({{"action","save_server"},{"address",address},{"name",*serverName?serverName:address}});
            endCard();
            ImGui::EndGroup();
            ImGui::Dummy({0,4});
            beginCard("##lan","局域网房间",fonts.title);
            if(ImGui::SmallButton("刷新")) act({{"action","refresh"}});
            const auto rooms=state.value("rooms",json::array());
            if(rooms.empty()) label("暂未发现房间。让好友先开放房间，或直接输入地址。");
            for(const auto& item:rooms) {
                ImGui::PushID(str(item,"address").c_str());ImGui::Separator();
                ImGui::Text("%s",str(item,"name","未命名房间").c_str());if(item.value("password",false)) {ImGui::SameLine();chip("密码",color::warn);}
                ImGui::PushFont(fonts.note);label((str(item,"host")+"  ·  "+std::to_string(item.value("players",0))+"/"+std::to_string(item.value("max_players",0))+" 人  ·  "+str(item,"address")).c_str());ImGui::PopFont();
                ImGui::SameLine(ImGui::GetContentRegionAvail().x-60);
                if(ImGui::Button("加入")) {copyText(address,sizeof address,str(item,"address"));if(item.value("password",false)) say("请输入房间密码后点击连接",false);else join();}
                ImGui::PopID();
            }
            endCard();
            const auto saved=state.value("configuration",json::object()).value("saved_servers",json::array());
            if(!saved.empty()) {
                beginCard("##saved","收藏",fonts.title);
                for(const auto& s:saved) {
                    const auto a=str(s,"address");ImGui::PushID(a.c_str());
                    if(ImGui::Selectable((str(s,"name",a)+"   "+a).c_str(),false,0,{ImGui::GetContentRegionAvail().x-80,0})) copyText(address,sizeof address,a);
                    ImGui::SameLine();if(ImGui::SmallButton("移除")) act({{"action","remove_server"},{"address",a}});
                    ImGui::PopID();
                }
                endCard();
            }
            return;
        }
        title(r=="host"?"你的房间":r=="client"?"已加入房间":"正在连接",nullptr);
        const auto roomInfo=state.value("room",json::object());
        beginCard("##room","房间",fonts.title);
        ImGui::Text("%s",str(roomInfo,"name",r=="connecting"?"连接中…":"房间").c_str());
        ImGui::SameLine();chip(r=="host"?"房主":r=="client"?"房客":"连接中",r=="host"?color::accent:color::ok);
        ImGui::SameLine();chip((std::to_string(state.value("peers",json::array()).size())+" / "+std::to_string(roomInfo.value("max_players",0))+" 人").c_str(),color::dim);
        const bool tcpOnly=state.value("traffic",json::object()).value("tcp_only_links",0u)>0;
        if(tcpOnly) {ImGui::SameLine();chip("UDP 未连通 · 走 TCP",color::warn);}
        else if(r=="client") {ImGui::SameLine();chip((std::to_string(state.value("rtt_ms",0u))+" ms").c_str(),color::info);}
        const auto pvpState=state.value("pvp",json::object());
        if(pvpState.value("active",false)) {
            ImGui::SameLine();chip("PVP",color::bad);
            char rule[64];std::snprintf(rule,sizeof rule,"伤害 %.1f 倍 · %s",pvpState.value("scale",1.0),pvpState.value("revive",true)?"击败后回满":"会真正倒下");
            ImGui::SameLine();ImGui::TextDisabled("%s",rule);
        }
        if(r=="host") {
            ImGui::Spacing();ImGui::SeparatorText("PVP");
            bool roomPvp=pvpState.value("allowed",false);
            if(ImGui::Checkbox("允许 PVP（玩家之间可以互相锁定、攻击）",&roomPvp)) act({{"action","room_pvp"},{"enabled",roomPvp}});
            ImGui::BeginDisabled(!roomPvp);
            label("玩家之间的伤害倍率");ImGui::SetNextItemWidth(260);
            if(!ImGui::IsAnyItemActive()) pvpScale=float(pvpState.value("scale",1.0));
            ImGui::SliderFloat("##roompvpscale",&pvpScale,.1f,5.f,"%.1f 倍");
            if(ImGui::IsItemDeactivatedAfterEdit()) act({{"action","room_pvp"},{"enabled",roomPvp},{"scale",pvpScale}});
            int mode=pvpState.value("revive",true)?0:1;
            label("被击败时");
            if(ImGui::RadioButton("回满生命（打到 1 点判负，3 秒无敌后回满）",mode==0)) act({{"action","room_pvp"},{"enabled",roomPvp},{"revive",true}});
            if(ImGui::RadioButton("真正倒下（生命归零的干员按游戏规则倒下）",mode==1)) act({{"action","room_pvp"},{"enabled",roomPvp},{"revive",false}});
            ImGui::EndDisabled();
            ImGui::Spacing();label("好友可通过以下地址加入：");
            for(const auto& a:state.value("addresses",json::array())) {
                const auto endpoint=str(a,"ip")+":"+std::to_string(roomInfo.value("port",47615));
                ImGui::PushID(endpoint.c_str());ImGui::PushFont(fonts.mono);ImGui::TextUnformatted(endpoint.c_str());ImGui::PopFont();
                ImGui::SameLine();if(ImGui::SmallButton("复制")) {ImGui::SetClipboardText(endpoint.c_str());say("地址已复制",false);}
                ImGui::SameLine();ImGui::PushFont(fonts.note);label(str(a,"adapter").c_str());ImGui::PopFont();ImGui::PopID();
            }
        }
        ImGui::Spacing();
        if(dangerButton(r=="host"?"关闭房间":r=="client"?"离开房间":"取消连接",{0,0})) leaveConfirm=true;
        endCard();
        sync();
    }
    void sync() {
        const auto world=state.value("world_session",json::object());const auto entry=world.value("entry",json::object());
        const auto native=state.value("game",json::object()).value("world_replication",json::object());
        beginCard("##sync","世界同步",fonts.title);
        const bool active=entry.value("active",false);
        chip(active?"已进入共享世界":"正在进入共享世界",active?color::ok:color::warn);
        ImGui::SameLine();chip(("敌人 "+std::to_string(native.value("matched",0u))+" 已同步").c_str(),color::dim);
        if(native.value("missing",0u)>0) {ImGui::SameLine();chip(("缺失 "+std::to_string(native.value("missing",0u))).c_str(),color::bad);}
        for(const auto& b:entry.value("blockers",json::array())) if(b.is_string()) {ImGui::PushFont(fonts.note);label((std::string("· 等待：")+blockerText(b.get<std::string>())).c_str());ImGui::PopFont();}
        const auto travel=str(native,"teleport");
        if(travel=="failed") ImGui::TextColored(color::bad,"传送失败：%s",str(native,"error").c_str());
        else if(!travel.empty()&&travel!="idle"&&travel!="complete") {ImGui::PushFont(fonts.note);label(("传送：" + travel).c_str());ImGui::PopFont();}
        // Host changed map: follow or stay.
        const auto change=native.value("host_map_change",json::object());
        if(role()=="client"&&change.value("pending",false)) {
            ImGui::Separator();ImGui::TextColored(color::accent,"房主已前往：%s",str(change,"level").c_str());
            const auto blockers=entry.value("blockers",json::array());
            const bool supported=std::find(blockers.begin(),blockers.end(),json("host_scene_rules"))==blockers.end();
            const auto token=change.value("token",std::uint64_t(0));
            ImGui::BeginDisabled(!supported);if(accentButton("跟随房主",{0,0})) act({{"action","host_map_choice"},{"token",token},{"follow",true}});ImGui::EndDisabled();
            if(!change.value("deferred",false)) {ImGui::SameLine();if(ImGui::Button("暂不跟随",{0,0})) act({{"action","host_map_choice"},{"token",token},{"follow",false}});}
            if(!supported) {ImGui::PushFont(fonts.note);label("房主当前场景尚未开放联机入场，请等待其回到大世界。");ImGui::PopFont();}
        }
        endCard();
    }
    void pagePlayers() {
        title("玩家","房间内所有玩家的编号、位置与状态。");
        const auto peers=state.value("peers",json::array());
        if(peers.empty()) {beginCard("##none","",fonts.title);label("加入或开放房间后，这里会列出房间内的玩家。");endCard();return;}
        const bool host=role()=="host";
        if(ImGui::BeginTable("##players",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("编号",ImGuiTableColumnFlags_WidthFixed,56);ImGui::TableSetupColumn("玩家",0,1.4f);
            ImGui::TableSetupColumn("地图",0,1.2f);ImGui::TableSetupColumn("坐标",0,1.6f);ImGui::TableSetupColumn("状态",0,1.1f);
            ImGui::TableSetupColumn("操作",ImGuiTableColumnFlags_WidthFixed,host?170.f:96.f);
            ImGui::TableHeadersRow();
            for(const auto& p:peers) {
                ImGui::PushID(p.value("id",0));ImGui::TableNextRow(0,40);
                const bool self=p.value("local",false),valid=p.value("valid",false),stale=p.value("state_age_ms",4000)>3000;
                ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::PushFont(fonts.mono);ImGui::Text("#%d",p.value("id",0));ImGui::PopFont();
                ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(str(p,"name").c_str());
                if(p.value("host",false)) {ImGui::SameLine();chip("房主",color::accent);}
                if(self) {ImGui::SameLine();chip("你",color::info);}
                ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(str(p,"level",str(p,"map","—")).c_str());
                ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::PushFont(fonts.mono);ImGui::TextUnformatted(valid?formatPosition(p.value("position",json::array())).c_str():"—");ImGui::PopFont();
                ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();
                if(self) ImGui::TextColored(color::info,"本机");
                else if(!valid||stale) ImGui::TextColored(color::faint,"等待状态");
                else if(p.value("same_map",false)) ImGui::TextColored(color::ok,"同地图");
                else ImGui::TextColored(color::warn,"其他地图");
                ImGui::TableNextColumn();
                if(!self) {
                    ImGui::BeginDisabled(!online()||!valid||stale);if(ImGui::SmallButton("传送到TA")) act({{"action","teleport_to_player"},{"id",p["id"]}});ImGui::EndDisabled();
                    if(host) {ImGui::SameLine();if(ImGui::SmallButton("移出")) {kickId=p.value("id",0);kickPending=true;}}
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    void pageChat() {
        title("聊天",nullptr);
        ImGui::PushStyleColor(ImGuiCol_ChildBg,color::card);
        ImGui::BeginChild("##messages",{0,-56},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
        std::uint64_t newest=0;
        for(const auto& line:state.value("chat",json::array())) {
            newest=(std::max)(newest,line.value("serial",std::uint64_t(0)));
            if(line.value("system",false)) {ImGui::PushFont(fonts.note);label(("· "+str(line,"text")).c_str());ImGui::PopFont();continue;}
            ImGui::TextColored(color::accent,"%s",str(line,"name").c_str());ImGui::SameLine();ImGui::TextWrapped("%s",str(line,"text").c_str());
        }
        if(newest!=lastChat) {ImGui::SetScrollHereY(1);lastChat=newest;}
        ImGui::EndChild();ImGui::PopStyleColor();
        ImGui::BeginDisabled(!online());
        ImGui::SetNextItemWidth(-96);bool send=ImGui::InputTextWithHint("##chat","输入消息，Enter 发送",chat,sizeof chat,ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();send|=accentButton("发送",{-1,0});
        if(send&&*chat) {act({{"action","chat"},{"text",chat}});chat[0]=0;ImGui::SetKeyboardFocusHere(-1);}
        ImGui::EndDisabled();
    }
    void pageSettings() {
        title("设置","名字与显示设置保存在本机。");
        beginCard("##player","玩家",fonts.title);
        label("显示名称（默认使用游戏昵称，修改后以你填写的为准）");ImGui::SetNextItemWidth(320);if(ImGui::InputText("##name",name,sizeof name)) nameEdited=true;
        const char* keys[]={"=","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};int index=key==VK_OEM_PLUS?0:key-VK_F1+1;
        label("菜单快捷键");ImGui::SetNextItemWidth(160);if(ImGui::Combo("##key",&index,keys,13)) key=index==0?VK_OEM_PLUS:VK_F1+index-1;
        endCard();
        beginCard("##display","显示",fonts.title);
        bool changed=ImGui::Checkbox("显示其他玩家的角色",&remote);
        ImGui::BeginDisabled(!remote);changed|=ImGui::Checkbox("同时显示其他玩家队伍中的另外三名干员",&squad);
        changed|=ImGui::Checkbox("在其他玩家的角色上重放技能动作、武器与特效",&replay);
        changed|=ImGui::Checkbox("给其他玩家的角色加上描边",&outline);ImGui::EndDisabled();

        changed|=ImGui::Checkbox("显示玩家标记与成员列表（编号、名字、距离）",&markers);
        endCard();
        beginCard("##coop","联机",fonts.title);
        changed|=ImGui::Checkbox("作为房客时跟随房主的地图",&follow);
        changed|=ImGui::Checkbox("同步战斗（共同攻击房主世界的敌人）",&combat);
        endCard();
        if(changed) applySwitches();
        label("勾选项立即生效；名称、快捷键等需点击保存。");
        if(accentButton("保存设置",{0,0})) saveSettings();
        ImGui::Dummy({0,8});
        beginCard("##tools","工具",fonts.title);
        if(ImGui::Button("应急回到四号谷地",{0,0})) act({{"action","emergency_home"}});
        ImGui::SameLine();ImGui::PushFont(fonts.note);label("先离开房间，再用官方传送回到枢纽。");ImGui::PopFont();
        const auto teleport=state.value("game",json::object()).value("world_replication",json::object()).value("official_teleport",json::object());
        if(teleport.contains("error")) ImGui::TextColored(color::bad,"官方传送失败：%s",str(teleport,"error").c_str());
        bool invincible=state.value("testing_invincible",false);
        if(ImGui::Checkbox("测试：本机角色无敌（会保存，重启后保持）",&invincible)) act({{"action","testing_invincible"},{"enabled",invincible}});
        endCard();
    }
    void pageDiagnostics() {
        title("诊断","连接、同步与游戏接入的实时状态。");
        const auto g=state.value("game",json::object());const auto native=g.value("world_replication",json::object());
        const auto world=state.value("world_session",json::object());const auto entry=world.value("entry",json::object());
        const auto combatState=native.value("combat",json::object());const auto squadState=native.value("squad",json::object());
        auto row=[&](const char* k,const std::string& v,ImVec4 tint=color::text) {
            ImGui::TableNextRow();ImGui::TableNextColumn();label(k);ImGui::TableNextColumn();ImGui::TextColored(tint,"%s",v.c_str());
        };
        auto num=[](const json& j,const char* k) {return std::to_string(j.value(k,std::uint64_t(0)));};
        beginCard("##diag","",fonts.title);
        if(ImGui::BeginTable("##d",2,ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("k",0,1);ImGui::TableSetupColumn("v",0,2.4f);
            row("游戏接入",state.value("hook_ready",false)?"已连接":"等待场景",state.value("hook_ready",false)?color::ok:color::warn);
            row("地图",str(g,"map_id","—")+" / "+str(g,"level_id","—"));
            row("本机角色",g.value("local_valid",false)?"已读取（骨骼 "+std::to_string(g.value("local_bones",0))+"）":"未读取",g.value("local_valid",false)?color::ok:color::warn);
            row("共享世界",str(entry,"phase","offline"),entry.value("active",false)?color::ok:color::warn);
            row("房客隔离",native.value("guest_policy",json::object()).value("ready",false)?"就绪":str(native.value("guest_policy",json::object()),"error","—"));
            row("战斗过滤",state.value("op_filter_ready",false)?"就绪":str(state,"op_filter_note","—"),state.value("op_filter_ready",false)?color::ok:color::warn);
            row("敌人",num(native,"matched")+" 已同步 / "+num(native,"missing")+" 缺失");
            row("命中",num(combatState,"guest_hits_sent")+" 发出 / "+num(combatState,"room_hits_applied")+" 结算");
            row("技能重放",num(combatState,"player_casts_sent")+" 发出 / "+num(combatState,"player_casts_mirrored")+" 重放 / "+num(combatState,"player_casts_rejected")+" 失败");
            if(!str(combatState,"player_cast_error").empty()) row("重放错误",str(combatState,"player_cast_error"),color::bad);
            row("武器",str(combatState,"local_weapon").empty()?"未读取":"已读取（远端查询 "+num(combatState,"weapon_lookups")+"）");
            row("队伍成员",num(squadState,"poses_sent")+" 发出 / "+num(squadState,"poses_in")+" 收到 / "+std::to_string(squadState.value("remote",json::array()).size())+" 远端");
            if(!str(squadState,"error").empty()) row("队伍错误",str(squadState,"error"),color::bad);
            row("网络延迟",std::to_string(state.value("rtt_ms",0u))+" ms");
            const auto traffic=state.value("traffic",json::object());
            if(traffic.value("tcp_only_links",0u)>0) row("UDP",std::to_string(traffic.value("tcp_only_links",0u))+" 条连接未连通，走 TCP（备用通道 "+std::to_string(traffic.value("unreliable_over_tcp",std::uint64_t(0)))+" 包，丢弃 "+std::to_string(traffic.value("unreliable_dropped",std::uint64_t(0)))+"）",color::warn);
            else row("UDP",traffic.value("udp_links",0u)>0?"已连通":"—");
            row("传送",str(native,"teleport","idle"));
            for(const auto& e:{str(world,"error"),str(native,"error"),str(g,"error"),str(state,"last_error")}) if(!e.empty()) row("错误",e,color::bad);
            const auto gui=state.value("gui_render",json::object());
            row("界面",str(gui,"backend")+" · "+num(gui,"frames")+" 帧");
            row("版本","EF Link " EFLINK_VERSION " · Windows x64");
            ImGui::EndTable();
        }
        endCard();
    }
    void popups() {
        if(leaveConfirm) {ImGui::OpenPopup("##leave");leaveConfirm=false;}
        if(kickPending) {ImGui::OpenPopup("##kick");kickPending=false;}
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{24,20});
        if(ImGui::BeginPopupModal("##leave",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoTitleBar)) {
            ImGui::PushFont(fonts.title);ImGui::TextUnformatted(role()=="host"?"关闭房间？":"离开房间？");ImGui::PopFont();
            label(role()=="host"?"所有玩家都会断开连接。":"会回到进入房间前的位置。");ImGui::Spacing();
            if(dangerButton("确认",{120,0})) {act({{"action","leave"}});ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(ImGui::Button("返回",{120,0})) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("##kick",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoTitleBar)) {
            ImGui::PushFont(fonts.title);ImGui::Text("移出玩家 #%d？",kickId);ImGui::PopFont();ImGui::Spacing();
            if(dangerButton("移出",{120,0})) {act({{"action","kick"},{"id",kickId}});kickId=-1;ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(ImGui::Button("返回",{120,0})) {kickId=-1;ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
    }
};

// ---- renderer: DirectComposition swap chain on a popup window ----
struct Surface {
    HWND window=nullptr;bool interactive=false,shown=false,transparent=true;unsigned width=0,height=0;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain1> swap;ComPtr<ID3D11RenderTargetView> target;
    ComPtr<IDCompositionDevice> dcomp;ComPtr<IDCompositionTarget> dtarget;ComPtr<IDCompositionVisual> visual;
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
        auto* self=reinterpret_cast<Surface*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE) {self=static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self&&self->transparent&&!self->interactive) {
            if(m==WM_NCHITTEST) return HTTRANSPARENT;
            if(m==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        }
        if(ImGui::GetCurrentContext()&&ImGui_ImplWin32_WndProcHandler(w,m,a,b)) return 1;
        if(m==WM_CLOSE) {if(self) self->closeRequested=true;return 0;}
        if(m==WM_SIZE&&self&&a!=SIZE_MINIMIZED) {self->pendingWidth=LOWORD(b);self->pendingHeight=HIWORD(b);}
        return DefWindowProcW(w,m,a,b);
    }
    bool closeRequested=false;unsigned pendingWidth=0,pendingHeight=0;
    bool create(const wchar_t* className,HWND owner,bool overlay,int x,int y,unsigned w,unsigned h) {
        transparent=overlay;width=(std::max)(1u,w);height=(std::max)(1u,h);
        const DWORD ex=overlay?(WS_EX_NOREDIRECTIONBITMAP|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE):WS_EX_NOREDIRECTIONBITMAP;
        window=CreateWindowExW(ex,className,L"EF Link",overlay?WS_POPUP:WS_OVERLAPPEDWINDOW,x,y,int(width),int(height),owner,nullptr,GetModuleHandleW(nullptr),this);
        if(!window) return false;
        if(overlay) SetLayeredWindowAttributes(window,0,255,LWA_ALPHA);
        D3D_FEATURE_LEVEL level;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context))&&
           FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context))) return false;
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory2> factory;
        if(FAILED(device.As(&dxgi))||FAILED(dxgi->GetAdapter(&adapter))||FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
        DXGI_SWAP_CHAIN_DESC1 d{};d.Width=width;d.Height=height;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;
        d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=2;d.Scaling=DXGI_SCALING_STRETCH;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        d.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
        if(FAILED(factory->CreateSwapChainForComposition(device.Get(),&d,nullptr,&swap))) return false;
        if(FAILED(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&dcomp)))||FAILED(dcomp->CreateTargetForHwnd(window,TRUE,&dtarget))||
           FAILED(dcomp->CreateVisual(&visual))||FAILED(visual->SetContent(swap.Get()))||FAILED(dtarget->SetRoot(visual.Get()))||FAILED(dcomp->Commit())) return false;
        makeTarget();return target!=nullptr;
    }
    void makeTarget() {
        target.Reset();ComPtr<ID3D11Texture2D> back;
        if(SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&back)))) device->CreateRenderTargetView(back.Get(),nullptr,&target);
    }
    void resize(unsigned w,unsigned h) {
        w=(std::max)(1u,w);h=(std::max)(1u,h);if(w==width&&h==height) return;
        width=w;height=h;target.Reset();context->ClearState();context->Flush();
        swap->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0);makeTarget();
    }
    // Click-through HUD layer, or an interactive menu that takes the keyboard.
    void setInteractive(bool value) {
        if(interactive==value) return;
        interactive=value;
        if(transparent) {
            auto ex=GetWindowLongPtrW(window,GWL_EXSTYLE);
            ex=value?(ex&~LONG_PTR(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE)):(ex|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE);
            SetWindowLongPtrW(window,GWL_EXSTYLE,ex);
            SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        }
    }
    void show(bool value) {
        if(shown==value) return;
        shown=value;ShowWindow(window,value?SW_SHOWNA:SW_HIDE);
    }
    void render(const float clear[4]) {
        auto* rtv=target.Get();context->OMSetRenderTargets(1,&rtv,nullptr);context->ClearRenderTargetView(rtv,clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());swap->Present(1,0);
    }
    void capture(const std::filesystem::path& path) {
        ComPtr<ID3D11Texture2D> back;swap->GetBuffer(0,IID_PPV_ARGS(&back));D3D11_TEXTURE2D_DESC d{};back->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&d,nullptr,&staging))) return;
        context->CopyResource(staging.Get(),back.Get());D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))) return;
        std::vector<unsigned char> pixels(std::size_t(d.Width)*d.Height*4);
        for(unsigned y=0;y<d.Height;++y) std::memcpy(pixels.data()+std::size_t(y)*d.Width*4,static_cast<unsigned char*>(map.pData)+y*map.RowPitch,d.Width*4);
        context->Unmap(staging.Get(),0);
        BITMAPFILEHEADER f{};BITMAPINFOHEADER info{};f.bfType=0x4D42;f.bfOffBits=sizeof(f)+sizeof(info);f.bfSize=f.bfOffBits+DWORD(pixels.size());
        info.biSize=sizeof(info);info.biWidth=LONG(d.Width);info.biHeight=-LONG(d.Height);info.biPlanes=1;info.biBitCount=32;info.biSizeImage=DWORD(pixels.size());
        std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);
        out.write(reinterpret_cast<char*>(&f),sizeof(f));out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(reinterpret_cast<char*>(pixels.data()),std::streamsize(pixels.size()));
    }
    void destroy() {
        if(ImGui::GetCurrentContext()) {ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();}
        visual.Reset();dtarget.Reset();dcomp.Reset();target.Reset();swap.Reset();context.Reset();device.Reset();
        if(window) DestroyWindow(window);window=nullptr;
    }
};
float uiScale(unsigned clientHeight) {return (std::clamp)(float(clientHeight)/1080.f,.85f,1.6f);}
}

// ================= Gui =================
Gui::Gui()=default;
Gui::~Gui() {stop();}
std::future<json> Gui::post(const json& input) {
    auto call=std::make_shared<Call>();call->input=input;auto future=call->result.get_future();
    std::lock_guard lock(callsMutex);
    if(calls.size()>=64) {std::promise<json> busy;busy.set_value({{"error","界面请求过多"}});return busy.get_future();}
    calls.push_back(std::move(call));return future;
}
void Gui::tickGame(const BE_HostApiV1*) {
    gameThreadSeen=true;uiReady=overlayReady.load();
    std::deque<std::shared_ptr<Call>> pending;{std::lock_guard lock(callsMutex);pending.swap(calls);}
    for(auto& call:pending) {
        try {call->result.set_value(request?request(call->input):json{{"error","模块未就绪"}});}
        catch(const std::exception& e) {try {call->result.set_value({{"error",e.what()}});}catch(...) {}}
        catch(...) {try {call->result.set_value({{"error","请求失败"}});}catch(...) {}}
        ++callsServed;
    }
}
void Gui::shutdownGame(const BE_HostApiV1*) {menuVisible=false;}
nlohmann::json Gui::diagnostics() const {
    std::string error;{std::lock_guard lock(errorMutex);error=lastError;}
    return {{"backend","overlay_dcomp"},{"overlay_ready",overlayReady.load()},{"frames",overlayFrames.load()},{"calls_served",callsServed.load()},{"error",error}};
}
void Gui::start(Request handler,const json& options) {
    stop();stopping=false;overlayFrames=0;request=std::move(handler);
    if(options.value("preview_mode",false)) {
        thread=std::jthread([this,options]{try {runPreview(request,options);}catch(const std::exception& e) {setError(e.what());}});
        return;
    }
    thread=std::jthread([this,options]{try {runOverlay(options);}catch(const std::exception& e) {setError(e.what());}overlayReady=false;});
}
void Gui::stop() {
    stopping=true;if(thread.joinable()) thread.join();
    std::deque<std::shared_ptr<Call>> pending;{std::lock_guard lock(callsMutex);pending.swap(calls);}
    for(auto& call:pending) try {call->result.set_value({{"error","界面已关闭"}});}catch(...) {}
    menuVisible=false;uiReady=false;overlayReady=false;
}
void Gui::runOverlay(json) {
    const auto instance=GetModuleHandleW(nullptr);const std::wstring className=L"EFLink.Overlay."+std::to_wstring(GetCurrentProcessId());
    WNDCLASSEXW wc{sizeof(wc),0,Surface::proc,0,0,instance,nullptr,LoadCursorW(nullptr,IDC_ARROW),nullptr,nullptr,className.c_str(),nullptr};
    if(!RegisterClassExW(&wc)) throw std::runtime_error("界面窗口注册失败");
    struct Unregister {const wchar_t* name;HINSTANCE instance;~Unregister() {UnregisterClassW(name,instance);}} unregister{className.c_str(),instance};
    while(!stopping) {
        HWND game=nullptr;
        while(!stopping&&!(game=findGame())) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if(stopping) break;
        RECT client{};GetClientRect(game,&client);POINT origin{0,0};ClientToScreen(game,&origin);
        Surface surface;
        if(!surface.create(className.c_str(),game,true,origin.x,origin.y,unsigned(client.right),unsigned(client.bottom))) {surface.destroy();throw std::runtime_error("界面渲染器创建失败（DirectComposition）");}
        IMGUI_CHECKVERSION();auto* context=ImGui::CreateContext();ImGui::SetCurrentContext(context);
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        const float scale=uiScale(unsigned(client.bottom));
        Ui ui;ui.fonts=loadFonts(scale);applyTheme(scale);
        ImGui_ImplWin32_Init(surface.window);ImGui_ImplDX11_Init(surface.device.Get(),surface.context.Get());
        ui.post=[this](const json& input){return post(input);};
        overlayReady=true;bool keyDown=false,enterDown=false;
        while(!stopping&&IsWindow(game)) {
            MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
            const bool minimized=IsIconic(game)||!IsWindowVisible(game);
            const HWND foreground=GetForegroundWindow();const bool focused=foreground==game||foreground==surface.window;
            // Menu key: polled, one toggle per press; it types normally into a focused text box.
            const bool down=(GetAsyncKeyState(ui.key)&0x8000)!=0;
            if(down&&!keyDown&&focused&&!(ui.open&&io.WantTextInput)) ui.open=!ui.open;
            keyDown=down;
            if(openRequested.exchange(false)) ui.open=true;
            if(ui.closeRequested) {ui.open=false;ui.closeRequested=false;}
            // Enter in the game (menu closed, in a room) opens the HUD chat input.
            const bool enter=(GetAsyncKeyState(VK_RETURN)&0x8000)!=0;
            if(enter&&!enterDown&&foreground==game&&!ui.open&&!ui.chatting&&ui.online()) {ui.chatting=true;ui.chatFocus=true;}
            enterDown=enter;
            if(ui.open) ui.chatting=false;
            if(minimized||!focused) {ui.open=false;ui.chatting=false;}
            const bool wantInput=ui.open||ui.chatting;
            const bool wasInteractive=surface.interactive;
            surface.setInteractive(wantInput);
            if(wantInput&&!wasInteractive) SetForegroundWindow(surface.window);
            if(!wantInput&&wasInteractive&&GetForegroundWindow()==surface.window) SetForegroundWindow(game);
            menuVisible=ui.open;
            if(minimized) {surface.show(false);ui.poll(false);std::this_thread::sleep_for(std::chrono::milliseconds(100));continue;}
            RECT area{};GetClientRect(game,&area);POINT at{0,0};ClientToScreen(game,&at);
            if(at.x!=origin.x||at.y!=origin.y||area.right!=client.right||area.bottom!=client.bottom||!surface.shown) {
                client=area;origin=at;SetWindowPos(surface.window,nullptr,origin.x,origin.y,client.right,client.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
            }
            surface.resize(unsigned(client.right),unsigned(client.bottom));surface.show(true);
            ui.poll(ui.open||ui.chatting);
            ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
            const ImVec2 size{float(surface.width),float(surface.height)};
            ui.drawHud(size);
            if(ui.open) ui.drawMenu(size);
            ImGui::Render();const float clear[4]{0,0,0,0};surface.render(clear);++overlayFrames;
            if(!ui.open&&!ui.chatting) std::this_thread::sleep_for(std::chrono::milliseconds(8)); // the HUD needs no more than ~60 fps
        }
        overlayReady=false;menuVisible=false;
        surface.destroy();ImGui::DestroyContext(context);
    }
}
void Gui::runPreview(Request handler,json options) {
    const auto instance=GetModuleHandleW(nullptr);const std::wstring className=L"EFLink.NativeMenu."+std::to_wstring(GetCurrentProcessId());
    WNDCLASSEXW wc{sizeof(wc),0,Surface::proc,0,0,instance,nullptr,LoadCursorW(nullptr,IDC_ARROW),nullptr,nullptr,className.c_str(),nullptr};
    if(!RegisterClassExW(&wc)) throw std::runtime_error("预览窗口注册失败");
    Surface surface;
    if(!surface.create(className.c_str(),nullptr,false,80,60,1440,860)) {surface.destroy();UnregisterClassW(className.c_str(),instance);throw std::runtime_error("预览渲染器创建失败");}
    IMGUI_CHECKVERSION();auto* context=ImGui::CreateContext();ImGui::SetCurrentContext(context);
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    Ui ui;ui.fonts=loadFonts(1.f);applyTheme(1.f);
    ImGui_ImplWin32_Init(surface.window);ImGui_ImplDX11_Init(surface.device.Get(),surface.context.Get());
    ui.post=[handler](const json& input){std::promise<json> p;try {p.set_value(handler(input));}catch(const std::exception& e) {p.set_value({{"error",e.what()}});}return p.get_future();};
    ui.page=options.value("preview_tab",0);
    if(const char* tab=std::getenv("EFLINK_PREVIEW_TAB")) ui.page=(std::clamp)(std::atoi(tab),0,4);ui.open=!options.value("preview_hud",false);
    json demoHud;
    if(options.value("preview_hud",false)) demoHud={{"markers",json::array({{{"id",1},{"name","房主·Yvonne"},{"host",true},{"x",.52},{"y",.42},{"distance",12.4}},
        {{"id",2},{"name","管理员"},{"self",true},{"x",.48},{"y",.55},{"distance",0}},{{"id",3},{"name","好友"},{"x",1.4},{"y",.3},{"distance",118.7}}})}};
    const auto capture=str(options,"preview_capture");
    if(capture.empty()) ShowWindow(surface.window,SW_SHOW);else surface.show(true);
    uiReady=true;overlayReady=true;const auto started=now();
    while(!stopping&&!surface.closeRequested) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        if(surface.pendingWidth) {surface.resize(surface.pendingWidth,surface.pendingHeight);surface.pendingWidth=0;}
        ui.poll(true);
        if(!demoHud.is_null()) ui.hud=demoHud;
        if(options.value("preview_hud",false)&&ui.state.contains("role")) {ui.state["role"]="client";ui.state["peers"]=json::array({{{"id",1},{"name","房主·Yvonne"},{"host",true},{"valid",true},{"position",{-153.9,104.7,-493.2}},{"level","map01_lv001"}},{{"id",2},{"name","管理员"},{"local",true},{"valid",true},{"position",{-152.1,104.6,-491.0}},{"level","map01_lv001"}}});}
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
        const ImVec2 size{float(surface.width),float(surface.height)};
        ui.drawHud(size);if(ui.open) ui.drawMenu(size);
        if(ui.closeRequested) {ui.closeRequested=false;}
        ImGui::Render();const float clear[4]{.11f,.14f,.13f,1};surface.render(clear);++overlayFrames;
        if(!capture.empty()&&now()-started>1500) {surface.capture(std::filesystem::path(std::u8string(capture.begin(),capture.end())));break;}
    }
    surface.destroy();ImGui::DestroyContext(context);UnregisterClassW(className.c_str(),instance);
    uiReady=false;overlayReady=false;
}
}
