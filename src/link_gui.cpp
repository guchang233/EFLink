#include "link_version.h"
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
#include "link_ui_theme.h"
#include "link_ui_widgets.h"
#include "link_ui_panel.h"
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
// Standalone preview only: a real D3D11 window that hosts the same menu the
// game shows on its Unity canvas. Never used inside the game.
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

// ---- standalone preview fixtures -------------------------------------------
// `--demo` replaces the module reply with a realistic room so the layout can be
// reviewed without a second client. Nothing here reaches the network stack.
json demoStatus(const std::string& role) {
    const bool host=role=="host",client=role=="client";
    const bool offline=!(host||client);
    json peers=json::array();
    if(host||client) {
        peers.push_back({{"id",1},{"name",host?"管理员":"Yvonne"},{"host",host},{"local",true},{"map","map01"},
            {"level","map01_lv001"},{"address","192.168.2.3:47615"},{"rtt_ms",0},{"valid",true},{"same_map",true},{"state_age_ms",40}});
        peers.push_back({{"id",2},{"name",host?"Yvonne":"管理员"},{"host",client},{"local",false},{"map","map01"},
            {"level","map01_lv001"},{"address",host?"192.168.2.9:51967":"192.168.2.3:47615"},{"rtt_ms",18},{"valid",true},{"same_map",true},{"state_age_ms",120},{"distance",12.4f}});
        peers.push_back({{"id",3},{"name","拖进来看风景"},{"host",false},{"local",false},{"map","map02"},
            {"level","map02_lv001"},{"address",host?"192.168.2.11:51002":"192.168.2.3:47615"},{"rtt_ms",64},{"valid",true},{"same_map",false},{"state_age_ms",2100}});
    }
    json rooms=json::array();
    if(offline) {
        rooms.push_back({{"room_id",763792363},{"name","EF Link 测试房间"},{"host","管理员"},{"players",3},{"max_players",8},
            {"password",false},{"map","map01"},{"address","192.168.2.3:47615"}});
        rooms.push_back({{"room_id",918273645},{"name","好友的房间"},{"host","Yvonne"},{"players",2},{"max_players",4},
            {"password",true},{"map","map01"},{"address","192.168.2.9:47615"}});
        rooms.push_back({{"room_id",118273645},{"name","四号谷地跑图"},{"host","Ash"},{"players",1},{"max_players",16},
            {"password",false},{"map","map03"},{"address","192.168.2.21:47700"}});
    }
    json chat=json::array();
    if(host||client) {
        chat.push_back({{"serial",1},{"name",""},{"text","Yvonne 加入了房间"},{"system",true}});
        chat.push_back({{"serial",2},{"name","管理员"},{"text","来了，我在四号谷地枢纽这边"},{"system",false}});
        chat.push_back({{"serial",3},{"name","Yvonne"},{"text","好，我先把怪拉过去，你跟上传送就行"},{"system",false}});
        chat.push_back({{"serial",4},{"name",""},{"text","房主已切换到 map01_lv001"},{"system",true}});
    }
    return {
        {"module","eflink.core"},{"version",EFLINK_VERSION},{"name",offline?"管理员":(host?"管理员":"Yvonne")},
        {"role",offline?"offline":role},{"local_id",1},
        {"room",{{"id",763792363},{"name","EF Link 测试房间"},{"max_players",8},{"password",false},{"port",47615},{"address",host?"0.0.0.0":""}}},
        {"rtt_ms",18},{"peers",peers},{"rooms",rooms},{"chat",chat},{"chat_serial",4},{"last_error",""},
        {"addresses",json::array({{{"ip","192.168.2.3"},{"adapter","以太网"}},{{"ip","10.4.0.7"},{"adapter","Wintun"}}})},
        {"game",{{"map_id","map01"},{"level_id","map01_lv001"},{"local_bones",64},{"error",""},
            {"world_replication",{{"matched",12},{"missing",1},{"teleport","idle"},{"error",""},
                {"guest_policy",{{"restricted",true},{"ready",true},{"error",""}}},
                {"official_teleport",{{"map","map04"},{"hub","hub_04_a"}}},
                {"host_map_change",{{"pending",false},{"level",""},{"deferred",false},{"token",0}}}}}}},
        {"world_session",{{"ready",true},{"epoch",4211},{"error",""},
            {"entry",{{"active",false},{"phase","awaiting_host_world"},
                {"blockers",json::array({"host_world_isolation","guest_logical_players","host_combat_settlement"})}}}}},
        {"hook_ready",!offline},{"testing_invincible",false},
        {"configuration",{{"player_name",offline?"管理员":"Yvonne"},{"room_name","EF Link 测试房间"},{"port",47615},{"max_players",8},
            {"menu_key",VK_OEM_PLUS},{"show_remote_players",true},{"show_player_markers",true},{"follow_host_world",true},{"sync_combat",false},
            {"saved_servers",json::array({{{"address","192.168.2.3:47615"},{"name","客厅主机"}},{{"address","10.4.0.7:47615"},{"name","内网穿透"}}})}}},
        {"traffic",{{"bytes_in",184320},{"bytes_out",96256},{"states_in",client?142:0},{"states_out",host?142:0},{"udp_pongs",client?9:0}}}};
}
json demoHud() {
    return {{"markers",json::array({
        {{"name","Yvonne"},{"x",.42f},{"y",.36f},{"distance",12.4}},
        {{"name","拖进来看风景"},{"x",.72f},{"y",.58f},{"distance",48.1}},
        {{"name","Ash"},{"x",.5f},{"y",.5f},{"behind",true},{"distance",31.7}},
        {{"name","异地图玩家"},{"other_map",true},{"map","map02"}}})}};
}
}

struct Gui::NativeState {
    eflink::UiPanel panel;NativeGuiCanvas canvas;unity::Api api;ImGuiContext* context=nullptr;HWND window=nullptr;
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
    // Font size and style metrics follow the render target, so a 1440p or 4K
    // session gets a proportionally larger menu instead of a tiny one.
    void init(const BE_HostApiV1* host) {
        api.bind(host);canvas.bind(host);accepting=true;warmed=false;window=findGame();if(!window) throw std::runtime_error("Native ImGui: game window unavailable");
        if(GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()) throw std::runtime_error("Native ImGui: Canvas callback is not the window owner thread");
        const auto height=api.value<int>(api.method("Screen","get_height","","System.Int32"));
        // Theme and fonts are applied by createContext(): applyTheme() needs the
        // context that CreateContext() returns, so the two cannot be reordered.
        context=ui::createContext(float(height>0?height:1080));
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.BackendRendererName="eflink_unity_canvas";io.BackendPlatformName="eflink_game_input";
        io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::GetMainViewport()->PlatformHandleRaw=window;
        canvas.init(io.Fonts);
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
        // The menu is an overlay drawn onto the game's own canvas, not an OS
        // window: alt-tabbing, a launcher stealing focus, or the game being
        // minimised must not close it. Only the menu key, Escape and the
        // 返回游戏 button do. Input still follows focus -- feedInput() drops
        // every event while the game is in the background -- so a lingering menu
        // cannot swallow the player's clicks.
        if(focused&&openRequested.exchange(false)) state.visible=true;
        const bool show=state.visible.load();menuVisible=show;
        if(!show&&state.lastVisible) {ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();}
        state.lastVisible=show;state.panel.refresh();state.menuKey=state.panel.state.value("configuration",json::object()).value("menu_key",kDefaultMenuKey);
        const auto hud=state.panel.request({{"action","hud"}});
        if(!show&&(!focused||hud.value("markers",json::array()).empty())) {state.canvas.hide();return;}
        auto& io=ImGui::GetIO();state.api.bind(host);
        // A minimised game window can report a zero-sized render target. Pushing
        // that into ImGui would give the main viewport an inverted visibility rect
        // and drag the menu into the top-left corner, so keep the last known size
        // while the window is minimised -- the menu now stays open through that.
        const float width=float(state.api.value<int>(state.api.method("Screen","get_width","","System.Int32")));
        const float height=float(state.api.value<int>(state.api.method("Screen","get_height","","System.Int32")));
        if(width>1.f&&height>1.f) io.DisplaySize={width,height};
        auto now=GetTickCount64();io.DeltaTime=(std::clamp)(float(now-state.lastFrame)/1000,.001f,.1f);state.lastFrame=now;
        state.feedInput(io,show&&focused);io.MouseDrawCursor=show;
        ImGui::NewFrame();if(show) state.panel.draw();else drawHud(hud);
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
    if(!options.value("preview_mode",false)) throw std::runtime_error("The preview renderer is only available in standalone preview mode");
    const std::string demo=options.value("preview_demo",std::string{});
    // In demo mode the module is never asked for state: every menu action is
    // answered from a fixed room so the layout is stable while reviewing it.
    Request send=demo.empty()?request:Request{[request,demo](const json& input)->json {
        const auto action=input.value("action",std::string{});
        if(action=="hud") return demoHud();
        return demoStatus(demo);
    }};
    auto instance=GetModuleHandleW(nullptr);
    const auto className=previewWindowClass(GetCurrentProcessId());
    WNDCLASSEXW wc{sizeof(wc),0,Renderer::proc,0,0,instance,nullptr,LoadCursorW(nullptr,IDC_ARROW),nullptr,nullptr,className.c_str(),nullptr};
    if(!RegisterClassExW(&wc)) throw std::runtime_error("Cannot register standalone preview");
    Renderer r;r.window=CreateWindowExW(0,className.c_str(),L"EF Link preview",WS_OVERLAPPEDWINDOW,100,80,1280,800,nullptr,nullptr,instance,&r);
    if(!r.window||FAILED(r.create())) {if(r.window) DestroyWindow(r.window);UnregisterClassW(className.c_str(),instance);throw std::runtime_error("Cannot create standalone preview renderer");}
    ImGuiContext* context=ui::createContext(0.f);
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    ImGui_ImplWin32_Init(r.window);ImGui_ImplDX11_Init(r.device.Get(),r.context.Get());
    eflink::UiPanel panel;panel.request=send;panel.tab=options.value("preview_tab",0);panel.refresh();uiReady=true;menuVisible=true;
    const auto capture=options.value("preview_capture",std::string{});if(capture.empty()) ShowWindow(r.window,SW_SHOWNA);
    auto started=GetTickCount64();
    while(!stopping&&!r.closing) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}if(r.closing) break;
        if(r.width&&r.height) {r.target.Reset();r.swap->ResizeBuffers(0,r.width,r.height,DXGI_FORMAT_UNKNOWN,0);r.width=r.height=0;r.makeTarget();}
        if(!r.target) {std::this_thread::sleep_for(std::chrono::milliseconds(25));continue;}
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
        if(options.value("preview_hud",false)) drawHud(demoHud());
        else panel.draw();
        ImGui::Render();const float clear[]={.025f,.032f,.04f,1.f};auto* target=r.target.Get();
        r.context->OMSetRenderTargets(1,&target,nullptr);r.context->ClearRenderTargetView(target,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if(!capture.empty()&&GetTickCount64()-started>1200) {
            r.capture(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(capture.data()),capture.size())));
            break;
        }
        if(panel.close) break;r.swap->Present(1,0);
    }
    menuVisible=false;uiReady=false;ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(context);
    DestroyWindow(r.window);UnregisterClassW(className.c_str(),instance);
}
}
