#pragma once
// One ImGui look for every surface: the in-game Unity Canvas menu, the screen
// HUD and the standalone preview window all build their style from here.
#include <windows.h>
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace eflink::ui {
// ---- palette ---------------------------------------------------------------
// Industrial dark, Endfield-leaning: desaturated blue-black surfaces and a
// single yellow-green accent used only for state that matters.
inline constexpr ImVec4 kInk{0.043f,0.051f,0.061f,1};        // behind panels
inline constexpr ImVec4 kPanel{0.071f,0.086f,0.102f,1};      // window body
inline constexpr ImVec4 kRaised{0.102f,0.122f,0.141f,1};     // cards, nav rail
inline constexpr ImVec4 kSunken{0.031f,0.039f,0.047f,1};     // wells, logs
inline constexpr ImVec4 kLine{0.145f,0.180f,0.212f,1};       // hairline borders
inline constexpr ImVec4 kLineStrong{0.235f,0.290f,0.333f,1};
inline constexpr ImVec4 kText{0.910f,0.937f,0.957f,1};
inline constexpr ImVec4 kTextDim{0.616f,0.671f,0.714f,1};
inline constexpr ImVec4 kTextMuted{0.427f,0.478f,0.522f,1};
inline constexpr ImVec4 kAccent{0.843f,0.894f,0.337f,1};     // #D7E456
inline constexpr ImVec4 kAccentDim{0.545f,0.596f,0.208f,1};
inline constexpr ImVec4 kInfo{0.396f,0.769f,0.839f,1};
inline constexpr ImVec4 kSuccess{0.549f,0.843f,0.427f,1};
inline constexpr ImVec4 kWarning{0.996f,0.706f,0.329f,1};
inline constexpr ImVec4 kDanger{1.000f,0.427f,0.361f,1};

enum class Tone { Neutral, Accent, Info, Success, Warning, Danger };

inline constexpr ImVec4 toneColor(Tone tone) {
    switch(tone) {
    case Tone::Accent:return kAccent;
    case Tone::Info:return kInfo;
    case Tone::Success:return kSuccess;
    case Tone::Warning:return kWarning;
    case Tone::Danger:return kDanger;
    default:return kTextDim;
    }
}
inline ImVec4 alpha(ImVec4 color,float a) {color.w=a;return color;}
// Draw lists take 0xAABBGGRR rather than ImVec4.
inline ImU32 u32(ImVec4 color) {return ImGui::ColorConvertFloat4ToU32(color);}
inline ImU32 u32(ImVec4 color,float a) {return ImGui::ColorConvertFloat4ToU32(alpha(color,a));}
inline ImU32 u32(Tone tone) {return u32(toneColor(tone));}
inline ImU32 u32(Tone tone,float a) {return u32(toneColor(tone),a);}

// ---- metrics ---------------------------------------------------------------
// Style is authored for 1080p and multiplied by the display scale, so a 1440p
// or 4K session gets proportionally larger rows instead of a tiny menu.
inline float scale=1.f;
inline float px(float value) {return std::round(value*scale);}

// Display scale from the render target height, clamped so the menu stays
// usable on unusually small or very large targets.
inline float scaleForHeight(float height) {
    if(!(height>0)) return 1.f;
    return std::clamp(std::round(height/1080.f*20.f)/20.f,0.85f,1.6f);
}

inline void applyTheme(float displayScale) {
    scale=displayScale;
    ImGuiStyle& s=ImGui::GetStyle();
    s.WindowRounding=px(10);s.ChildRounding=px(8);s.FrameRounding=px(6);s.PopupRounding=px(8);
    s.GrabRounding=px(6);s.TabRounding=px(6);s.ScrollbarRounding=px(8);
    s.WindowPadding={px(20),px(18)};
    s.FramePadding={px(12),px(8)};
    s.ItemSpacing={px(10),px(9)};
    s.ItemInnerSpacing={px(8),px(7)};
    s.CellPadding={px(8),px(6)};
    s.IndentSpacing=px(18);s.ScrollbarSize=px(12);s.GrabMinSize=px(14);
    s.WindowBorderSize=px(1);s.ChildBorderSize=px(1);s.FrameBorderSize=px(1);s.PopupBorderSize=px(1);
    s.WindowTitleAlign={0.f,.5f};s.ButtonTextAlign={.5f,.5f};s.SelectableTextAlign={0.f,.5f};
    s.SeparatorTextBorderSize=px(1);s.SeparatorTextAlign={0.f,.5f};s.SeparatorTextPadding={px(16),px(6)};
    s.WindowMinSize={px(420),px(280)};
    s.DisabledAlpha=.42f;

    ImVec4* c=s.Colors;
    c[ImGuiCol_Text]=kText;
    c[ImGuiCol_TextDisabled]=kTextMuted;
    c[ImGuiCol_WindowBg]=kPanel;
    c[ImGuiCol_ChildBg]=ImVec4{0,0,0,0};
    c[ImGuiCol_PopupBg]=ImVec4{0.086f,0.102f,0.118f,.98f};
    c[ImGuiCol_Border]=kLine;
    c[ImGuiCol_BorderShadow]=ImVec4{0,0,0,0};
    c[ImGuiCol_FrameBg]=kSunken;
    c[ImGuiCol_FrameBgHovered]=ImVec4{0.145f,0.176f,0.204f,1};
    c[ImGuiCol_FrameBgActive]=ImVec4{0.180f,0.216f,0.247f,1};
    c[ImGuiCol_TitleBg]=ImVec4{0.055f,0.067f,0.078f,1};
    c[ImGuiCol_TitleBgActive]=ImVec4{0.086f,0.102f,0.118f,1};
    c[ImGuiCol_MenuBarBg]=kRaised;
    c[ImGuiCol_ScrollbarBg]=ImVec4{0,0,0,0};
    c[ImGuiCol_ScrollbarGrab]=ImVec4{0.216f,0.263f,0.302f,1};
    c[ImGuiCol_ScrollbarGrabHovered]=ImVec4{0.290f,0.345f,0.388f,1};
    c[ImGuiCol_ScrollbarGrabActive]=kAccentDim;
    c[ImGuiCol_CheckMark]=kAccent;
    c[ImGuiCol_SliderGrab]=kAccentDim;
    c[ImGuiCol_SliderGrabActive]=kAccent;
    c[ImGuiCol_Button]=ImVec4{0.153f,0.184f,0.212f,1};
    c[ImGuiCol_ButtonHovered]=ImVec4{0.224f,0.267f,0.302f,1};
    c[ImGuiCol_ButtonActive]=alpha(kAccent,.35f);
    c[ImGuiCol_Header]=ImVec4{0.153f,0.184f,0.212f,1};
    c[ImGuiCol_HeaderHovered]=ImVec4{0.216f,0.259f,0.298f,1};
    c[ImGuiCol_HeaderActive]=alpha(kAccent,.30f);
    c[ImGuiCol_Separator]=kLine;
    c[ImGuiCol_SeparatorHovered]=alpha(kAccent,.55f);
    c[ImGuiCol_SeparatorActive]=kAccent;
    c[ImGuiCol_ResizeGrip]=ImVec4{0.216f,0.263f,0.302f,1};
    c[ImGuiCol_ResizeGripHovered]=kAccentDim;
    c[ImGuiCol_ResizeGripActive]=kAccent;
    c[ImGuiCol_Tab]=ImVec4{0.102f,0.122f,0.141f,1};
    c[ImGuiCol_TabHovered]=alpha(kAccent,.28f);
    c[ImGuiCol_TabSelected]=ImVec4{0.180f,0.216f,0.247f,1};
    c[ImGuiCol_TabSelectedOverline]=kAccent;
    c[ImGuiCol_TabDimmed]=ImVec4{0.086f,0.102f,0.118f,1};
    c[ImGuiCol_TabDimmedSelected]=ImVec4{0.153f,0.184f,0.212f,1};
    c[ImGuiCol_PlotLines]=kInfo;
    c[ImGuiCol_PlotLinesHovered]=kAccent;
    c[ImGuiCol_PlotHistogram]=kAccent;
    c[ImGuiCol_PlotHistogramHovered]=kAccent;
    c[ImGuiCol_TableHeaderBg]=ImVec4{0.102f,0.122f,0.141f,1};
    c[ImGuiCol_TableBorderStrong]=kLine;
    c[ImGuiCol_TableBorderLight]=ImVec4{0.110f,0.137f,0.157f,1};
    c[ImGuiCol_TableRowBg]=ImVec4{0,0,0,0};
    c[ImGuiCol_TableRowBgAlt]=ImVec4{1,1,1,.022f};
    c[ImGuiCol_TextSelectedBg]=alpha(kAccent,.30f);
    c[ImGuiCol_DragDropTarget]=kAccent;
    c[ImGuiCol_NavCursor]=kAccent;
    c[ImGuiCol_NavWindowingHighlight]=kAccent;
    c[ImGuiCol_NavWindowingDimBg]=ImVec4{0,0,0,.35f};
    c[ImGuiCol_ModalWindowDimBg]=ImVec4{.012f,.016f,.020f,.62f};
}

// ---- fonts -----------------------------------------------------------------
// Member names avoid "small": the Windows RPC headers define it as a macro.
struct Fonts {
    ImFont* regular=nullptr;
    ImFont* title=nullptr;
    ImFont* caption=nullptr;
    bool cjk=false;
};
// Filled in by loadFonts() / createContext(). Declared here, next to the loader
// that owns it, so the small and title faces are available to every surface.
inline Fonts fonts;
// The game ships no font we may redistribute, so the menu borrows a Windows
// system font. Sizes are the 1080p baseline scaled by ui::scale, so
// applyTheme() must run before this.
// NOTE: the final Build() resets ImFontAtlas::TexID to 0 (see
// ImFontAtlasBuildWithStbTruetype), so callers must assign the texture handle
// with SetTexID() *after* this returns -- imguimesh::convert() drops every
// command whose texture id does not match and would otherwise emit nothing.
inline Fonts loadFonts(ImFontAtlas* atlas) {
    Fonts result;
    const auto size=[](float base) {return std::clamp(base*scale,12.f,40.f);};
    ImFontConfig config;
    config.PixelSnapH=true;                    // crisp CJK stems
    config.OversampleH=1;config.OversampleV=1;
    wchar_t windows[MAX_PATH]{};
    if(!GetWindowsDirectoryW(windows,MAX_PATH)) return result;
    const auto fontDirectory=std::filesystem::path(windows)/L"Fonts";
    // msyh = Microsoft YaHei, then a fallback that also carries full CJK.
    for(const wchar_t* name:{L"msyh.ttc",L"msyhl.ttc",L"simhei.ttf",L"simsun.ttc"}) {
        const auto path=fontDirectory/name;
        std::error_code error;
        if(!std::filesystem::is_regular_file(path,error)) continue;
        const auto utf8=path.string();
        const auto ranges=atlas->GetGlyphRangesChineseFull();
        if(!result.regular) result.regular=atlas->AddFontFromFileTTF(utf8.c_str(),size(17),&config,ranges);
        if(!result.title) result.title=atlas->AddFontFromFileTTF(utf8.c_str(),size(21),&config,ranges);
        if(!result.caption) result.caption=atlas->AddFontFromFileTTF(utf8.c_str(),size(14),&config,ranges);
        if(result.regular) {result.cjk=true;break;}
    }
    if(!result.regular) {                     // ASCII-only fallback keeps the menu usable
        result.regular=atlas->AddFontDefault();
        result.title=result.regular;
        result.caption=result.regular;
    }
    atlas->Build();
    return result;
}

// Creates the ImGui context and applies the theme and fonts in the only order
// that works: applyTheme() writes ImGui::GetStyle() (so a context must already
// exist), and loadFonts() sizes the faces from ui::scale (so the theme must run
// first). Everything that draws must go through here.
// This is not a style preference: calling applyTheme() while GImGui is null
// trips ImGui's "No current context" assert inside GetStyle()
// (imgui.cpp:3310), which reaches players as a Visual C++ runtime dialog while
// eflink.core.dll is loading.
// Pass 0 for the 1080p baseline, or the real render target height to derive a
// DPI-scaled menu.
inline ImGuiContext* createContext(float displayHeight) {
    IMGUI_CHECKVERSION();
    ImGuiContext* context=ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    applyTheme(scaleForHeight(displayHeight));
    fonts=loadFonts(ImGui::GetIO().Fonts);
    return context;
}
}
