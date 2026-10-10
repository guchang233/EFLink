#include "link_gui.h"
#include "link_settings.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "link_native_gui.h"
#include "link_input_ownership.h"
#include <commctrl.h>
#include <mutex>
#include <tuple>

using Microsoft::WRL::ComPtr;
using nlohmann::json;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace eflink {
namespace {
HWND findGame() {
    HWND result=nullptr;
    EnumWindows([](HWND w,LPARAM arg)->BOOL {
        DWORD pid=0;GetWindowThreadProcessId(w,&pid);
        wchar_t klass[64]{};GetClassNameW(w,klass,64);
        if(pid==GetCurrentProcessId()&&IsWindowVisible(w)&&wcscmp(klass,L"UnityWndClass")==0&&GetWindow(w,GW_OWNER)==nullptr) {
            *reinterpret_cast<HWND*>(arg)=w;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
struct Renderer {
    HWND window=nullptr;bool closing=false,toggle=false,interactive=true;unsigned width=0,height=0;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;ComPtr<ID3D11RenderTargetView> target;
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
        auto* self=reinterpret_cast<Renderer*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE) {self=static_cast<Renderer*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self&&!self->interactive&&m==WM_NCHITTEST) return HTTRANSPARENT;
        if(self&&!self->interactive&&m==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if(ImGui::GetCurrentContext()&&ImGui_ImplWin32_WndProcHandler(w,m,a,b)) return 1;
        if(self&&m==WM_SIZE&&a!=SIZE_MINIMIZED) {self->width=LOWORD(b);self->height=HIWORD(b);return 0;}
        if(self&&m==WM_CLOSE) {self->closing=true;return 0;}
        if(self&&m==WM_HOTKEY) {self->toggle=true;return 0;}
        return DefWindowProcW(w,m,a,b);
    }
    void makeTarget() {
        ComPtr<ID3D11Texture2D> back;
        if(SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&back)))) device->CreateRenderTargetView(back.Get(),nullptr,&target);
    }
    HRESULT create() {
        D3D_FEATURE_LEVEL feature;
        auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context);
        if(FAILED(hr)) hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context);
        if(FAILED(hr)) return hr;
        RECT bounds{};GetClientRect(window,&bounds);
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory> factory;
        device.As(&dxgi);dxgi->GetAdapter(&adapter);adapter->GetParent(IID_PPV_ARGS(&factory));
        DXGI_SWAP_CHAIN_DESC d{};d.BufferCount=2;d.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.OutputWindow=window;d.SampleDesc.Count=1;d.Windowed=TRUE;d.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        hr=factory->CreateSwapChain(device.Get(),&d,&swap);if(FAILED(hr)) return hr;
        makeTarget();return target?S_OK:E_FAIL;
    }
    void capture(const std::filesystem::path& path) {
        ComPtr<ID3D11Texture2D> back;swap->GetBuffer(0,IID_PPV_ARGS(&back));D3D11_TEXTURE2D_DESC d{};back->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&d,nullptr,&staging))) return;
        context->CopyResource(staging.Get(),back.Get());D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))) return;
        std::vector<unsigned char> pixels(std::size_t(d.Width)*d.Height*4);
        for(unsigned y=0;y<d.Height;++y) for(unsigned x=0;x<d.Width;++x) {
            auto* src=static_cast<unsigned char*>(map.pData)+y*map.RowPitch+x*4;auto* dst=pixels.data()+(std::size_t(y)*d.Width+x)*4;
            bool bgra=d.Format==DXGI_FORMAT_B8G8R8A8_UNORM;dst[0]=src[bgra?0:2];dst[1]=src[1];dst[2]=src[bgra?2:0];dst[3]=255;
        }
        context->Unmap(staging.Get(),0);
        BITMAPFILEHEADER f{};BITMAPINFOHEADER info{};f.bfType=0x4D42;f.bfOffBits=sizeof(f)+sizeof(info);f.bfSize=f.bfOffBits+DWORD(pixels.size());
        info.biSize=sizeof(info);info.biWidth=LONG(d.Width);info.biHeight=-LONG(d.Height);info.biPlanes=1;info.biBitCount=32;info.biSizeImage=DWORD(pixels.size());
        std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);
        out.write(reinterpret_cast<char*>(&f),sizeof(f));out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(reinterpret_cast<char*>(pixels.data()),std::streamsize(pixels.size()));
    }
};
void theme() {
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowRounding=8;s.ChildRounding=5;s.FrameRounding=3;s.WindowBorderSize=1;
    s.WindowPadding={24,20};s.FramePadding={12,9};s.ItemSpacing={12,12};s.ScrollbarSize=10;
    auto* c=s.Colors;c[ImGuiCol_WindowBg]={.065f,.075f,.082f,1};c[ImGuiCol_ChildBg]={.085f,.098f,.108f,1};
    c[ImGuiCol_FrameBg]={.12f,.145f,.16f,1};c[ImGuiCol_Button]={.18f,.22f,.23f,1};c[ImGuiCol_ButtonHovered]={.29f,.35f,.35f,1};
    c[ImGuiCol_Header]={.28f,.33f,.16f,1};c[ImGuiCol_HeaderHovered]={.34f,.39f,.20f,1};c[ImGuiCol_CheckMark]={.85f,.93f,.35f,1};
    c[ImGuiCol_SliderGrab]=c[ImGuiCol_CheckMark];c[ImGuiCol_Separator]={.22f,.27f,.29f,1};
}
void copyText(char* dst,std::size_t capacity,const std::string& text) {strncpy_s(dst,capacity,text.c_str(),_TRUNCATE);}
struct Panel {
    Gui::Request request;json state;std::string feedback;int tab=0;bool close=false,initialized=false;
    char name[65]{},room[65]{},hostPassword[129]{},address[257]{},joinPassword[129]{},serverName[129]{},chat[1001]{};
    int port=47615,limit=8,key=kDefaultMenuKey;bool remote=true,markers=true,follow=true,combat=true;std::uint64_t lastRefresh=0,lastChat=0;
    json call(const json& input) {
        try {auto result=request(input);if(result.contains("error")) feedback=result["error"].get<std::string>();else {state=std::move(result);if(input.value("action",std::string{})!="status") feedback.clear();}return state;}
        catch(const std::exception& e) {feedback=e.what();return state;}
    }
    void refresh() {
        if(GetTickCount64()-lastRefresh<250) return;lastRefresh=GetTickCount64();call({{"action","status"}});
        if(!initialized&&!state.empty()) {
            initialized=true;copyText(name,sizeof(name),state.value("name",std::string{}));
            auto c=state.value("configuration",json::object());copyText(room,sizeof(room),c.value("room_name",std::string{}));
            port=c.value("port",47615);limit=c.value("max_players",8);key=c.value("menu_key",kDefaultMenuKey);remote=c.value("show_remote_players",true);
            markers=c.value("show_player_markers",true);follow=c.value("follow_host_world",true);combat=c.value("sync_combat",true);
        }
    }
    void join() {if(*address) call({{"action","join"},{"address",address},{"password",joinPassword}});else feedback="请输入房主地址";}
    void save() {call({{"action","settings"},{"configuration",{{"player_name",name},{"room_name",room},{"port",port},{"max_players",limit},{"menu_key",key},{"show_remote_players",remote},{"show_player_markers",markers},{"follow_host_world",follow},{"sync_combat",combat}}}});}
    void draw() {
        refresh();auto role=state.value("role",std::string("offline"));bool online=role=="host"||role=="client",offline=role=="offline",requestLeave=false;
        auto display=ImGui::GetIO().DisplaySize;ImGui::SetNextWindowPos({display.x*.5f,display.y*.5f},ImGuiCond_Always,{.5f,.5f});
        ImGui::SetNextWindowSize({(std::min)(1020.f,display.x-24),(std::min)(700.f,display.y-24)},ImGuiCond_Always);
        ImGui::Begin("EF Link",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored({.85f,.93f,.35f,1},"EF LINK   /   终末地联机");ImGui::SameLine(ImGui::GetWindowWidth()-145);
        if(ImGui::SmallButton("返回游戏  ×")) close=true;
        ImGui::TextDisabled("%s  ·  %s",role=="host"?"房主":role=="client"?"已连接":role=="connecting"?"连接中":"离线",state.value("name",std::string("管理员")).c_str());
        ImGui::Separator();
        ImGui::BeginChild("navigation",{164,-42},false);const char* labels[]={"局域网房间","直接连接 / 收藏","开放房间","房间玩家","房间聊天","设置 / 诊断"};
        for(int i=0;i<6;++i) {if(ImGui::Selectable(labels[i],tab==i,0,{0,36})) tab=i;}
        ImGui::Spacing();ImGui::Separator();ImGui::TextDisabled("菜单：F%d",key-VK_F1+1);ImGui::TextWrapped("连接后可关闭菜单，联机继续运行。");
        ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("content",{0,-42},false);
        if(tab==0) {
            ImGui::Text("局域网房间");ImGui::SameLine();if(ImGui::Button("刷新列表")) call({{"action","refresh"}});
            ImGui::TextDisabled("自动寻找同一局域网中的房间；也可以直接输入好友地址。");
            ImGui::BeginDisabled(!offline);
            auto rooms=state.value("rooms",json::array());if(rooms.empty()) {ImGui::Spacing();ImGui::TextWrapped("暂时没有发现房间。让好友先开放房间，或在“直接连接”中输入 IP:端口。");}
            for(const auto& r:rooms) {
                ImGui::PushID(r.value("address",std::string{}).c_str());ImGui::Separator();
                ImGui::Text("%s  %s",r.value("name",std::string{}).c_str(),r.value("password",false)?"[密码]":"");
                ImGui::TextDisabled("%s  ·  %d/%d 人  ·  %s",r.value("host",std::string{}).c_str(),r.value("players",0),r.value("max_players",0),r.value("address",std::string{}).c_str());
                if(ImGui::Button("加入房间")) {copyText(address,sizeof(address),r.value("address",std::string{}));joinPassword[0]=0;if(r.value("password",false)) tab=1;else join();}
                ImGui::PopID();
            }
            ImGui::EndDisabled();
        } else if(tab==1) {
            ImGui::Text("连接好友");ImGui::TextDisabled("地址示例：192.168.1.20:47615");
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##address","房主的 IP 或 IP:端口",address,sizeof(address));
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##password","房间密码（可留空）",joinPassword,sizeof(joinPassword),ImGuiInputTextFlags_Password);
            ImGui::BeginDisabled(!offline);if(ImGui::Button("连接",{140,0})) join();ImGui::EndDisabled();
            ImGui::SameLine();if(ImGui::Button("收藏此地址")) {call({{"action","save_server"},{"address",address},{"name",*serverName?serverName:address}});}
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##serverName","收藏名称（可留空）",serverName,sizeof(serverName));
            ImGui::Separator();ImGui::Text("收藏的服务器");
            for(const auto& s:state.value("configuration",json::object()).value("saved_servers",json::array())) {
                auto a=s.value("address",std::string{});ImGui::PushID(a.c_str());
                if(ImGui::Selectable((s.value("name",a)+"  /  "+a).c_str())) {copyText(address,sizeof(address),a);joinPassword[0]=0;}
                if(ImGui::SmallButton("移除收藏")) call({{"action","remove_server"},{"address",a}});ImGui::PopID();
            }
        } else if(tab==2) {
            ImGui::Text("将房间开放到局域网");ImGui::TextDisabled("当前为实验版本：支持连接与角色显示，完整房主世界接管尚未完成。");
            if(!offline) {
                ImGui::Spacing();
                if(role=="host") {
                    ImGui::TextColored({.85f,.93f,.35f,1},"当前房间已开放：%s",state.value("room",json::object()).value("name",std::string{}).c_str());
                    ImGui::TextWrapped("开房设置暂时锁定。要修改名称、密码、人数或端口，请先关闭当前房间；已连接的玩家会断开。");
                    if(ImGui::Button("关闭当前房间以修改设置")) requestLeave=true;
                } else {
                    ImGui::TextWrapped("%s",role=="connecting"?"正在连接房间。请先取消连接，再设置自己的房间。":"你已加入其他房间。请先断开连接，再设置自己的房间。");
                    if(ImGui::Button(role=="connecting"?"取消当前连接":"断开当前连接以开放房间")) requestLeave=true;
                }
                ImGui::Separator();
            }
            ImGui::BeginDisabled(!offline);ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##room","房间名称",room,sizeof(room));
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##roomPassword","房间密码（可留空）",hostPassword,sizeof(hostPassword),ImGuiInputTextFlags_Password);
            ImGui::SliderInt("人数上限",&limit,2,16);ImGui::InputInt("连接端口",&port,0);
            if(ImGui::Button("开放房间",{180,0})) {save();if(feedback.empty()) call({{"action","host"},{"room_name",room},{"password",hostPassword},{"port",port},{"max_players",limit}});hostPassword[0]=0;}
            ImGui::EndDisabled();
            if(role=="host") {
                ImGui::Separator();ImGui::Text("房间已开放：%s",state["room"].value("name",std::string{}).c_str());
                for(const auto& a:state.value("addresses",json::array())) {
                    auto endpoint=a.value("ip",std::string{})+":"+std::to_string(state["room"].value("port",47615));
                    ImGui::PushID(endpoint.c_str());ImGui::Text("%s",endpoint.c_str());ImGui::SameLine();if(ImGui::SmallButton("复制地址")) ImGui::SetClipboardText(endpoint.c_str());ImGui::TextDisabled("%s",a.value("adapter",std::string{}).c_str());ImGui::PopID();
                }
            }
        } else if(tab==3) {
            ImGui::Text("房间玩家   /   %s",state.value("room",json::object()).value("name",std::string{}).c_str());
            auto entry=state.value("world_session",json::object()).value("entry",json::object());
            if(online&&!entry.value("active",false)) ImGui::TextWrapped("已连接房间，世界接管尚未完成。当前无法共同结算战斗。");
            if(!online) ImGui::TextWrapped("加入或开放房间后，这里会显示玩家和同步状态。");
            for(const auto& p:state.value("peers",json::array())) {
                ImGui::PushID(p.value("id",0));ImGui::Separator();ImGui::Text("%s  %s %s",p.value("name",std::string{}).c_str(),p.value("host",false)?"[房主]":"",p.value("local",false)?"[你]":"");
                ImGui::TextDisabled("地图 %s  ·  %s",p.value("map",std::string("读取中")).c_str(),p.value("local",false)?"本机":!p.value("valid",false)||p.value("state_age_ms",4000)>3000?"等待角色状态":p.value("same_map",false)?"同地图 · 同步中":"在其他地图");
                if(p.contains("distance")) {ImGui::SameLine();ImGui::TextDisabled("· %.1f 米",p["distance"].get<float>());}
                if(!p.value("local",false)) {
                    ImGui::BeginDisabled(!online||!p.value("valid",false)||p.value("state_age_ms",4000)>3000);
                    if(ImGui::Button("传送到这位玩家")) call({{"action","teleport_to_player"},{"id",p["id"]}});
                    ImGui::EndDisabled();
                }
                if(role=="host"&&!p.value("local",false)) {if(ImGui::Button("移出玩家")) {state["kick_id"]=p["id"];ImGui::OpenPopup("确认移出");}
                    if(ImGui::BeginPopupModal("确认移出",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {ImGui::Text("将这位玩家移出当前房间？");if(ImGui::Button("确认")) {call({{"action","kick"},{"id",p["id"]}});ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("取消")) ImGui::CloseCurrentPopup();ImGui::EndPopup();}}
                ImGui::PopID();
            }
        } else if(tab==4) {
            ImGui::Text("房间聊天");ImGui::BeginChild("messages",{0,-100},true);
            std::uint64_t newest=0;for(const auto& line:state.value("chat",json::array())) {
                newest=(std::max)(newest,line.value("serial",std::uint64_t(0)));
                auto text=(line.value("system",false)?std::string("· "):line.value("name",std::string{})+"：")+line.value("text",std::string{});ImGui::TextWrapped("%s",text.c_str());
            }
            if(newest!=lastChat) {ImGui::SetScrollHereY(1);lastChat=newest;}ImGui::EndChild();
            ImGui::BeginDisabled(!online);ImGui::SetNextItemWidth(-86);bool send=ImGui::InputTextWithHint("##chat","输入消息，按 Enter 发送",chat,sizeof(chat),ImGuiInputTextFlags_EnterReturnsTrue);ImGui::SameLine();send|=ImGui::Button("发送");
            if(send&&*chat) {call({{"action","chat"},{"text",chat}});chat[0]=0;}ImGui::EndDisabled();
        } else {
            ImGui::Text("玩家设置");ImGui::InputText("玩家名称",name,sizeof(name));ImGui::Checkbox("在游戏中显示其他玩家",&remote);
            ImGui::Checkbox("屏幕显示玩家位置与距离",&markers);ImGui::Checkbox("跟随房主地图与世界",&follow);ImGui::Checkbox("接收房主敌人状态（实验）",&combat);
            const char* keys[]={"=","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};int index=key==VK_OEM_PLUS?0:key-VK_F1+1;
            if(ImGui::Combo("菜单快捷键",&index,keys,13)) key=index==0?VK_OEM_PLUS:VK_F1+index-1;
            if(ImGui::Button("保存设置")) save();ImGui::SameLine();ImGui::TextDisabled("收藏、名字与设置自动保存在本机。");
            ImGui::Separator();ImGui::Text("房主世界接入");ImGui::TextWrapped("地图和实体加载完成后，还需完成客机世界隔离、逻辑玩家与房主规则接管。入场未完成时禁止客机结算战斗。");
            auto g=state.value("game",json::object());ImGui::Text("游戏接入：%s",state.value("hook_ready",false)?"已连接":"等待终末地场景");
            auto w=state.value("world_session",json::object());auto native=g.value("world_replication",json::object());
            ImGui::Text("世界状态：%s  /  地图版本 %llu",w.value("ready",false)?"已收到":"等待房主",static_cast<unsigned long long>(w.value("epoch",std::uint64_t(0))));
            auto entry=w.value("entry",json::object());
            ImGui::Text("房主世界：%s",entry.value("active",false)?"已完成入场":"接管未完成");
            ImGui::TextDisabled("%s",entry.value("phase",std::string("offline")).c_str());
            for(const auto& block:entry.value("blockers",json::array())) {
                auto blocker=block.get<std::string>();const char* description=
                    blocker=="host_world_isolation"?"房主世界隔离":blocker=="host_logical_players"?"房主逻辑玩家":
                    blocker=="host_combat_settlement"?"房主战斗结算":blocker=="host_scene_rules"?"房主地图与副本规则":
                    blocker=="guest_map_loading"?"客机地图加载":blocker=="guest_entity_replicas"?"客机实体重建":
                    blocker=="guest_world_isolation"?"客机世界隔离":blocker=="guest_logical_players"?"客机逻辑玩家":blocker.c_str();
                ImGui::TextDisabled("待完成：%s",description);
            }
            ImGui::Text("敌人：已匹配 %u / 缺失 %u",native.value("matched",0u),native.value("missing",0u));
            ImGui::Text("传送状态：%s",native.value("teleport",std::string("idle")).c_str());
            for(auto err:{w.value("error",std::string{}),native.value("error",std::string{})}) if(!err.empty()) ImGui::TextWrapped("%s",err.c_str());
            ImGui::Text("地图：%s   骨骼：%d",g.value("map_id",std::string{}).c_str(),g.value("local_bones",0));
            ImGui::Text("与房主延迟：%u ms",state.value("rtt_ms",0u));
            if(auto error=g.value("error",std::string{});!error.empty()) ImGui::TextWrapped("游戏接入：%s",error.c_str());
            ImGui::TextDisabled("EF Link 1.2.0-dev  /  Windows x64  /  Better-Endfield 模块");
        }
        auto error=state.value("last_error",std::string{});if(!feedback.empty()||!error.empty()) {ImGui::Separator();ImGui::TextColored({1,.65f,.4f,1},"%s",(!feedback.empty()?feedback:error).c_str());}
        ImGui::EndChild();ImGui::Separator();
        if(!offline) {if(ImGui::Button(role=="connecting"?"取消连接":role=="host"?"关闭房间":"断开连接")) requestLeave=true;
            if(requestLeave) ImGui::OpenPopup("离开房间");
            if(ImGui::BeginPopupModal("离开房间",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {ImGui::Text("%s",role=="host"?"关闭房间会断开所有玩家。":"确定结束当前连接？");if(ImGui::Button("确认")) {call({{"action","leave"}});joinPassword[0]=0;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("返回")) ImGui::CloseCurrentPopup();ImGui::EndPopup();}}
        else ImGui::TextDisabled("局域网连接  ·  同一地图显示玩家");
        ImGui::End();
    }
};
void drawMarkers(const json& data) {
    const auto size=ImGui::GetIO().DisplaySize;auto* draw=ImGui::GetForegroundDrawList();float otherY=20;
    for(const auto& marker:data.value("markers",json::array())) {
        std::string label=marker.value("name",std::string{});
        if(marker.value("other_map",false)) {
            label+=" · "+marker.value("map",std::string{});draw->AddText({20,otherY},IM_COL32(190,205,216,240),label.c_str());otherY+=25;continue;
        }
        float x=marker.value("x",.5f)*size.x,y=marker.value("y",.5f)*size.y;
        if(!std::isfinite(x)||!std::isfinite(y)) continue;
        bool off=marker.value("behind",false)||x<30||y<30||x>size.x-30||y>size.y-30;
        if(off) {
            float dx=x-size.x*.5f,dy=y-size.y*.5f;if(marker.value("behind",false)) {dx=-dx;dy=-dy;}
            if(std::abs(dx)+std::abs(dy)<.001f) dy=-1;
            const float scale=1.f/(std::max)(std::abs(dx)/((std::max)(1.f,size.x*.5f-65)),std::abs(dy)/((std::max)(1.f,size.y*.5f-65)));
            x=size.x*.5f+dx*scale;y=size.y*.5f+dy*scale;
        }
        char distance[40];std::snprintf(distance,sizeof distance," · %.1f m",marker.value("distance",0.f));label+=distance;
        auto text=ImGui::CalcTextSize(label.c_str());float tx=(std::clamp)(x-text.x*.5f,8.f,(std::max)(8.f,size.x-text.x-8));float ty=y+12;
        draw->AddRectFilled({tx-6,ty-4},{tx+text.x+6,ty+text.y+4},IM_COL32(10,17,21,195),4);
        draw->AddText({tx,ty},IM_COL32(227,244,145,255),label.c_str());
        draw->AddCircle({x,y},off?6.f:4.f,IM_COL32(227,244,145,255),12,2);
    }
}
}
struct Gui::NativeState {
    Panel panel;NativeGuiCanvas canvas;unity::Api api;ImGuiContext* context=nullptr;HWND window=nullptr;
    std::atomic<bool> visible=false,accepting=true,textInput=false;std::atomic<int> menuKey=kDefaultMenuKey;
    bool installed=false,failed=false,lastVisible=false,warmed=false;std::uint64_t lastFrame=0,retryAt=0,lastCheck=0;int failures=0;
    std::mutex eventsMutex;struct Event {UINT message;WPARAM value;};std::vector<Event> events;
    InputOwnership inputOwnership;
    NativeState(Request request,const json& options) {panel.request=std::move(request);menuKey=options.value("menu_key",kDefaultMenuKey);}
    static LRESULT CALLBACK input(HWND window,UINT message,WPARAM a,LPARAM b,UINT_PTR,DWORD_PTR data) {
        auto& state=*reinterpret_cast<NativeState*>(data);
        if(message==WM_NCDESTROY) {
            RemoveWindowSubclass(window,input,reinterpret_cast<UINT_PTR>(&state));state.installed=false;state.window=nullptr;
            return DefSubclassProc(window,message,a,b);
        }
        if(!state.accepting.load()) return DefSubclassProc(window,message,a,b);
        if(message==WM_KILLFOCUS)state.inputOwnership.focusLost();
        const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
        const bool up=message==WM_KEYUP||message==WM_SYSKEYUP;
        // A printable hotkey ("=") still types into a focused menu text box.
        const bool typing=state.visible.load()&&state.textInput.load()&&!(state.menuKey.load()>=VK_F1&&state.menuKey.load()<=VK_F12);
        if((down||up)&&int(a)==state.menuKey.load()&&!typing) {
            state.inputOwnership.route(std::size_t(a),down,true);
            if(down&&!(b&(LPARAM(1)<<30)))state.visible=!state.visible.load();
            return 0;
        }
        const bool menu=state.visible.load();
        bool paired=false,forward=false;
        if(down||up) {paired=true;forward=state.inputOwnership.route(std::size_t(a),down,menu);}
        for(auto [press,release,button]:{std::tuple{WM_LBUTTONDOWN,WM_LBUTTONUP,0u},std::tuple{WM_RBUTTONDOWN,WM_RBUTTONUP,1u},
            std::tuple{WM_MBUTTONDOWN,WM_MBUTTONUP,2u},std::tuple{WM_XBUTTONDOWN,WM_XBUTTONUP,3u}})
            if(message==UINT(press)||message==UINT(release)) {
                if(button==3u&&GET_XBUTTON_WPARAM(a)==XBUTTON2)button=4u;
                paired=true;forward=state.inputOwnership.route(256u+button,message==UINT(press),menu);break;
            }
        if(message==WM_INPUT) {
            RAWINPUT raw{};UINT size=sizeof(raw);
            const auto read=GetRawInputData(reinterpret_cast<HRAWINPUT>(b),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER));
            bool route=!menu;
            if(read!=UINT(-1)&&read>=sizeof(RAWINPUTHEADER)) {
                if(raw.header.dwType==RIM_TYPEKEYBOARD) {
                    const auto vk=raw.data.keyboard.VKey;
                    route=state.inputOwnership.route(512u+vk,(raw.data.keyboard.Flags&RI_KEY_BREAK)==0,menu||int(vk)==state.menuKey.load());
                }else if(raw.header.dwType==RIM_TYPEMOUSE) {
                    // Forward a game-owned release even while the menu is open.
                    // No input is generated or rewritten, and raw-motion-only
                    // packets remain suppressed while interacting with ImGui.
                    const auto flags=raw.data.mouse.usButtonFlags;
                    if(flags&0x3ffu)route=false;
                    for(unsigned i=0;i<5;++i) {
                        if(flags&(1u<<(i*2)))route|=state.inputOwnership.route(768u+i,true,menu);
                        if(flags&(2u<<(i*2)))route|=state.inputOwnership.route(768u+i,false,menu);
                    }
                }
            }
            return route?DefSubclassProc(window,message,a,b):DefWindowProcW(window,message,a,b);
        }
        if(menu) {
            if(message==WM_CHAR||message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL||
               message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||
               message==WM_MBUTTONDOWN||message==WM_MBUTTONUP||message==WM_XBUTTONDOWN||message==WM_XBUTTONUP) {
                std::lock_guard lock(state.eventsMutex);if(state.events.size()<2048) state.events.push_back({message,a});
            }
            // Never capture / move the mouse, change focus, or create a window.
            if(paired&&forward)return DefSubclassProc(window,message,a,b);
            if((message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST)||(message>=WM_KEYFIRST&&message<=WM_KEYLAST)||message==WM_SETCURSOR) return 0;
        }
        if(paired&&!forward)return 0;
        return DefSubclassProc(window,message,a,b);
    }
    void init(const BE_HostApiV1* host) {
        api.bind(host);canvas.bind(host);accepting=true;warmed=false;window=findGame();if(!window) throw std::runtime_error("Native ImGui: game window unavailable");
        if(GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()) throw std::runtime_error("Native ImGui: Canvas callback is not the window owner thread");
        IMGUI_CHECKVERSION();context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.BackendRendererName="eflink_unity_canvas";io.BackendPlatformName="eflink_game_input";
        io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::GetMainViewport()->PlatformHandleRaw=window;
        wchar_t winDir[MAX_PATH]{};GetWindowsDirectoryW(winDir,MAX_PATH);auto font=std::filesystem::path(winDir)/L"Fonts"/L"msyh.ttc";
        if(std::filesystem::is_regular_file(font)) io.Fonts->AddFontFromFileTTF(font.string().c_str(),18,nullptr,io.Fonts->GetGlyphRangesChineseFull());
        theme();canvas.init(io.Fonts);
        if(!SetWindowSubclass(window,input,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))) throw std::runtime_error("Native ImGui: input subclass unavailable");
        installed=true;panel.refresh();lastFrame=GetTickCount64();
        // A module can be loaded while a physical button is already held.
        for(unsigned vk=0;vk<256;++vk)if(GetAsyncKeyState(int(vk))&0x8000) {inputOwnership.seedGame(vk);inputOwnership.seedGame(512u+vk);}
        unsigned button=0;for(int vk:{VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2}) {
            if(GetAsyncKeyState(vk)&0x8000) {inputOwnership.seedGame(256u+button);inputOwnership.seedGame(768u+button);}++button;
        }
    }
    void feedInput(ImGuiIO& io,bool focused) {
        POINT position{};
        if(focused&&GetCursorPos(&position)&&ScreenToClient(window,&position)) io.AddMousePosEvent(float(position.x),float(position.y));
        else io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);
        std::vector<Event> pending;{std::lock_guard lock(eventsMutex);pending.swap(events);}
        for(const auto& event:pending) if(focused) {
            if(event.message==WM_CHAR) io.AddInputCharacterUTF16(ImWchar16(event.value));
            else if(event.message==WM_MOUSEWHEEL) io.AddMouseWheelEvent(0,float(GET_WHEEL_DELTA_WPARAM(event.value))/WHEEL_DELTA);
            else if(event.message==WM_MOUSEHWHEEL) io.AddMouseWheelEvent(-float(GET_WHEEL_DELTA_WPARAM(event.value))/WHEEL_DELTA,0);
            else io.AddMouseButtonEvent(event.message==WM_LBUTTONDOWN||event.message==WM_LBUTTONUP?0:event.message==WM_RBUTTONDOWN||event.message==WM_RBUTTONUP?1:
                event.message==WM_XBUTTONDOWN||event.message==WM_XBUTTONUP?(GET_XBUTTON_WPARAM(event.value)==XBUTTON2?4:3):2,
                event.message==WM_LBUTTONDOWN||event.message==WM_RBUTTONDOWN||event.message==WM_MBUTTONDOWN||event.message==WM_XBUTTONDOWN);
        }
        for(auto [vk,key]:{std::pair{VK_TAB,ImGuiKey_Tab},{VK_LEFT,ImGuiKey_LeftArrow},{VK_RIGHT,ImGuiKey_RightArrow},{VK_UP,ImGuiKey_UpArrow},
            {VK_DOWN,ImGuiKey_DownArrow},{VK_PRIOR,ImGuiKey_PageUp},{VK_NEXT,ImGuiKey_PageDown},{VK_HOME,ImGuiKey_Home},{VK_END,ImGuiKey_End},
            {VK_INSERT,ImGuiKey_Insert},{VK_DELETE,ImGuiKey_Delete},{VK_BACK,ImGuiKey_Backspace},{VK_SPACE,ImGuiKey_Space},{VK_RETURN,ImGuiKey_Enter},{VK_ESCAPE,ImGuiKey_Escape}})
            io.AddKeyEvent(key,focused&&(GetAsyncKeyState(vk)&0x8000)!=0);
        for(int i=0;i<26;++i) io.AddKeyEvent(ImGuiKey(ImGuiKey_A+i),focused&&(GetAsyncKeyState('A'+i)&0x8000)!=0);
        for(int i=0;i<10;++i) io.AddKeyEvent(ImGuiKey(ImGuiKey_0+i),focused&&(GetAsyncKeyState('0'+i)&0x8000)!=0);
        for(auto [vk,key]:{std::pair{VK_CONTROL,ImGuiMod_Ctrl},{VK_SHIFT,ImGuiMod_Shift},{VK_MENU,ImGuiMod_Alt},{VK_LWIN,ImGuiMod_Super}})
            io.AddKeyEvent(ImGuiKey(key),focused&&(GetAsyncKeyState(vk)&0x8000)!=0);
    }
    void shutdown(const BE_HostApiV1* host) {
        accepting=false;visible=false;api.bind(host);canvas.bind(host);
        if(installed&&window) {RemoveWindowSubclass(window,input,reinterpret_cast<UINT_PTR>(this));installed=false;}
        canvas.shutdown();if(context) {ImGui::DestroyContext(context);context=nullptr;}
    }
};
Gui::Gui()=default;
Gui::~Gui() {stop();}
void Gui::tickGame(const BE_HostApiV1* host) {
    if(!native||stopping) return;
    auto& state=*native;
    // A failed frame is retried a few times instead of leaving the player
    // without a menu or markers for the rest of the session.
    if(state.failed) {if(state.failures>5||GetTickCount64()<state.retryAt) return;state.failed=false;}
    try {
        if(!state.context) {state.init(host);uiReady=true;}
        ImGui::SetCurrentContext(state.context);
        if(GetTickCount64()-state.lastCheck>=500) {
            state.lastCheck=GetTickCount64();state.canvas.bind(host);
            if(!state.canvas.valid()) state.canvas.rebuild(ImGui::GetIO().Fonts);
        }
        if(!state.warmed) {
            // Exercise the actual managed mesh setters/getters while the Canvas
            // remains inactive. Readiness now includes a real geometry round trip.
            auto& io=ImGui::GetIO();state.api.bind(host);
            io.DisplaySize={float(state.api.value<int>(state.api.method("Screen","get_width","","System.Int32"))),float(state.api.value<int>(state.api.method("Screen","get_height","","System.Int32")))};
            io.DeltaTime=.016f;ImGui::NewFrame();state.panel.draw();ImGui::Render();state.canvas.render(ImGui::GetDrawData(),false);state.warmed=true;
        }
        const bool focused=state.window&&GetForegroundWindow()==state.window&&!IsIconic(state.window);
        if(focused&&openRequested.exchange(false)) state.visible=true;
        if(!focused) state.visible=false;
        const bool show=state.visible.load();menuVisible=show;
        if(!show&&state.lastVisible) {ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();}
        state.lastVisible=show;state.panel.refresh();state.menuKey=state.panel.state.value("configuration",json::object()).value("menu_key",kDefaultMenuKey);
        const auto hud=state.panel.request({{"action","hud"}});
        if(!show&&(!focused||hud.value("markers",json::array()).empty())) {state.canvas.hide();return;}
        auto& io=ImGui::GetIO();state.api.bind(host);
        io.DisplaySize={float(state.api.value<int>(state.api.method("Screen","get_width","","System.Int32"))),float(state.api.value<int>(state.api.method("Screen","get_height","","System.Int32")))};
        auto now=GetTickCount64();io.DeltaTime=(std::clamp)(float(now-state.lastFrame)/1000,.001f,.1f);state.lastFrame=now;
        state.feedInput(io,show&&focused);io.MouseDrawCursor=show;
        ImGui::NewFrame();if(show) state.panel.draw();else drawMarkers(hud);
        if(show&&ImGui::IsKeyPressed(ImGuiKey_Escape)&&!io.WantTextInput) state.panel.close=true;
        if(state.panel.close) {state.visible=false;menuVisible=false;state.panel.close=false;}
        state.textInput=show&&io.WantTextInput;
        ImGui::Render();state.canvas.render(ImGui::GetDrawData());++nativeFrames;
    } catch(const std::exception& e) {
        state.failed=true;state.visible=false;menuVisible=false;uiReady=false;
        ++state.failures;state.retryAt=GetTickCount64()+3000;
        try {state.shutdown(host);} catch(...) {}
        state.panel.request({{"action","gui_error"},{"error",e.what()}});
    }
}
nlohmann::json Gui::diagnostics() const {return native?native->canvas.diagnostics():json::object();}
void Gui::shutdownGame(const BE_HostApiV1* host) {
    if(native) {native->shutdown(host);native.reset();}menuVisible=false;uiReady=false;
}
void Gui::start(Request request,const json& options) {
    stop();stopping=false;nativeFrames=0;
    if(!options.value("preview_mode",false)) {native=std::make_unique<NativeState>(std::move(request),options);return;}
    thread=std::jthread([this,request=std::move(request),options]{
        try {run(request,options);} catch(const std::exception& e) {request({{"action","gui_error"},{"error",e.what()}});}
    });
}
void Gui::stop() {stopping=true;if(native) {native->accepting=false;native->visible=false;}if(thread.joinable()) thread.join();menuVisible=false;uiReady=false;}
void Gui::run(Request request,json options) {
    if(!options.value("preview_mode",false)) throw std::runtime_error("The Win32 GUI is only available in standalone preview mode");
    auto instance=GetModuleHandleW(nullptr);std::wstring className=L"EFLink.Preview."+std::to_wstring(GetCurrentProcessId());
    WNDCLASSEXW wc{sizeof(wc),0,Renderer::proc,0,0,instance,nullptr,LoadCursorW(nullptr,IDC_ARROW),nullptr,nullptr,className.c_str(),nullptr};
    if(!RegisterClassExW(&wc)) throw std::runtime_error("Cannot register standalone preview");
    Renderer r;r.window=CreateWindowExW(0,className.c_str(),L"EF Link preview",WS_OVERLAPPEDWINDOW,100,80,1280,800,nullptr,nullptr,instance,&r);
    if(!r.window||FAILED(r.create())) {if(r.window) DestroyWindow(r.window);UnregisterClassW(className.c_str(),instance);throw std::runtime_error("Cannot create standalone preview renderer");}
    IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    wchar_t winDir[MAX_PATH]{};GetWindowsDirectoryW(winDir,MAX_PATH);auto fontPath=std::filesystem::path(winDir)/L"Fonts"/L"msyh.ttc";
    if(std::filesystem::is_regular_file(fontPath)) io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(),18,nullptr,io.Fonts->GetGlyphRangesChineseFull());
    theme();ImGui_ImplWin32_Init(r.window);ImGui_ImplDX11_Init(r.device.Get(),r.context.Get());
    Panel panel;panel.request=std::move(request);panel.tab=options.value("preview_tab",0);panel.refresh();uiReady=true;menuVisible=true;
    const auto capture=options.value("preview_capture",std::string{});if(capture.empty()) ShowWindow(r.window,SW_SHOWNA);
    auto started=GetTickCount64();
    while(!stopping&&!r.closing) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}if(r.closing) break;
        if(r.width&&r.height) {r.target.Reset();r.swap->ResizeBuffers(0,r.width,r.height,DXGI_FORMAT_UNKNOWN,0);r.width=r.height=0;r.makeTarget();}
        if(!r.target) {std::this_thread::sleep_for(std::chrono::milliseconds(25));continue;}
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
        if(options.value("preview_hud",false)) drawMarkers({{"markers",json::array({{{"name","房主 · Yvonne"},{"x",.51},{"y",.37},{"distance",12.4}},{{"name","好友"},{"x",1.5},{"y",.25},{"distance",118.7}},{{"name","异地图玩家"},{"other_map",true},{"map","map02"}}})}});
        else panel.draw();
        ImGui::Render();const float clear[]={.025f,.032f,.04f,1.f};auto* target=r.target.Get();
        r.context->OMSetRenderTargets(1,&target,nullptr);r.context->ClearRenderTargetView(target,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if(!capture.empty()&&GetTickCount64()-started>1200) {r.capture(std::filesystem::path(std::u8string(capture.begin(),capture.end())));break;}
        if(panel.close) break;r.swap->Present(1,0);
    }
    menuVisible=false;uiReady=false;ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();
    DestroyWindow(r.window);UnregisterClassW(className.c_str(),instance);
}
}
