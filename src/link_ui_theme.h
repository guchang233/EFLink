#pragma once
// Endfield Industrial-Tactical design system for EF Link.
// Shared across the in-game DirectComposition menu, floating HUD overlays,
// and standalone D3D11 preview window.
#include <windows.h>
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace eflink::ui {

// ---- palette ---------------------------------------------------------------
// Deep obsidian / graphite tactical surfaces with Endfield's signature volt
// yellow-green (#E3F23D) primary accent and crisp telemetry secondary tones.
inline constexpr ImVec4 kCanvas     {0.043f, 0.055f, 0.075f, 0.96f}; // #0B0E13 main menu backdrop
inline constexpr ImVec4 kSidebar    {0.055f, 0.071f, 0.098f, 1.00f}; // #0E1219 navigation rail
inline constexpr ImVec4 kCard       {0.078f, 0.098f, 0.133f, 1.00f}; // #141922 elevated cards
inline constexpr ImVec4 kCardHover  {0.106f, 0.133f, 0.180f, 1.00f}; // #1B222E hovered card/row
inline constexpr ImVec4 kWell       {0.031f, 0.043f, 0.059f, 1.00f}; // #080B0F sunken inputs/logs
inline constexpr ImVec4 kLine       {0.141f, 0.176f, 0.227f, 1.00f}; // #242D3A hairline border
inline constexpr ImVec4 kLineStrong {0.216f, 0.271f, 0.341f, 1.00f}; // #374557 emphasis border
inline constexpr ImVec4 kText       {0.933f, 0.953f, 0.973f, 1.00f}; // #EEF3F8 primary ink
inline constexpr ImVec4 kTextDim    {0.580f, 0.635f, 0.698f, 1.00f}; // #94A2B2 secondary text
inline constexpr ImVec4 kTextMuted  {0.392f, 0.443f, 0.506f, 1.00f}; // #647181 captions/hints

inline constexpr ImVec4 kAccent     {0.890f, 0.949f, 0.239f, 1.00f}; // #E3F23D Endfield volt
inline constexpr ImVec4 kAccentSoft {0.722f, 0.784f, 0.180f, 1.00f}; // muted volt
inline constexpr ImVec4 kAccentInk  {0.047f, 0.059f, 0.031f, 1.00f}; // dark text on volt fill
inline constexpr ImVec4 kInfo       {0.271f, 0.831f, 0.910f, 1.00f}; // #45D4E8 telemetry cyan
inline constexpr ImVec4 kSuccess    {0.259f, 0.875f, 0.545f, 1.00f}; // #42DF8B emerald
inline constexpr ImVec4 kWarning    {1.000f, 0.718f, 0.290f, 1.00f}; // #FFB74A amber
inline constexpr ImVec4 kDanger     {1.000f, 0.353f, 0.322f, 1.00f}; // #FF5A52 coral red
inline constexpr ImVec4 kViolet     {0.710f, 0.482f, 1.000f, 1.00f}; // #B57BFF ether violet

enum class Tone { Neutral, Accent, Info, Success, Warning, Danger, Violet };

inline constexpr ImVec4 toneColor(Tone tone) {
    switch (tone) {
    case Tone::Accent:  return kAccent;
    case Tone::Info:    return kInfo;
    case Tone::Success: return kSuccess;
    case Tone::Warning: return kWarning;
    case Tone::Danger:  return kDanger;
    case Tone::Violet:  return kViolet;
    default:            return kTextDim;
    }
}

inline ImVec4 alpha(ImVec4 c, float a) { c.w = a; return c; }
inline ImU32 u32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImU32 u32(ImVec4 c, float a) { return ImGui::ColorConvertFloat4ToU32(alpha(c, a)); }
inline ImU32 u32(Tone t) { return u32(toneColor(t)); }
inline ImU32 u32(Tone t, float a) { return u32(toneColor(t), a); }

inline ImVec4 rankColor(int rankIndex) {
    switch (rankIndex) {
    case 0: return kAccent;
    case 1: return kInfo;
    case 2: return kSuccess;
    case 3: return kWarning;
    case 4: return kViolet;
    case 5: return ImVec4{1.000f, 0.478f, 0.565f, 1.00f};
    default: return ImVec4{0.420f, 0.639f, 1.000f, 1.00f};
    }
}

inline ImVec4 damageElementColor(const std::string& key) {
    if (key == "fire")      return ImVec4{1.000f, 0.420f, 0.259f, 1.00f};
    if (key == "pulse")     return ImVec4{0.961f, 0.839f, 0.259f, 1.00f};
    if (key == "cryst")     return ImVec4{0.329f, 0.847f, 1.000f, 1.00f};
    if (key == "natural")   return ImVec4{0.361f, 0.902f, 0.478f, 1.00f};
    if (key == "ether")     return kViolet;
    if (key == "real")      return kText;
    if (key == "lifedrain") return ImVec4{1.000f, 0.450f, 0.620f, 1.00f};
    return ImVec4{0.800f, 0.847f, 0.894f, 1.00f}; // physical / fixed
}

// ---- scaling ---------------------------------------------------------------
inline thread_local float scale = 1.0f;
inline float px(float value) { return std::round(value * scale); }

inline float scaleForHeight(unsigned height) {
    if (height == 0) return 1.0f;
    return std::clamp(std::round(float(height) / 1080.0f * 20.0f) / 20.0f, 0.85f, 1.55f);
}

// ---- fonts -----------------------------------------------------------------
struct Fonts {
    ImFont* body    = nullptr; // 16.5px regular CJK
    ImFont* bold    = nullptr; // 16.5px bold CJK
    ImFont* title   = nullptr; // 21px bold CJK
    ImFont* kpi     = nullptr; // 24px bold CJK for hero metrics
    ImFont* note    = nullptr; // 14px regular CJK
    ImFont* mono    = nullptr; // 14.5px Consolas ASCII monospace
    bool cjk        = false;
};

inline thread_local Fonts fonts;

inline Fonts loadFonts(ImFontAtlas* atlas, float displayScale) {
    Fonts f;
    const auto sz = [displayScale](float base) { return std::clamp(std::round(base * displayScale), 12.0f, 42.0f); };
    ImFontConfig cfg;
    cfg.PixelSnapH = true;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;

    wchar_t winDir[MAX_PATH]{};
    if (GetWindowsDirectoryW(winDir, MAX_PATH)) {
        const auto fontDir = std::filesystem::path(winDir) / L"Fonts";
        std::error_code ec;
        const auto regularPath = fontDir / L"msyh.ttc";
        const auto boldPath    = fontDir / L"msyhbd.ttc";
        const auto monoPath    = fontDir / L"consola.ttf";
        const auto* cjkRanges  = atlas->GetGlyphRangesChineseFull();

        std::filesystem::path bodyFile;
        for (const wchar_t* candidate : {L"msyh.ttc", L"msyhl.ttc", L"simhei.ttf", L"simsun.ttc"}) {
            const auto p = fontDir / candidate;
            if (std::filesystem::is_regular_file(p, ec)) { bodyFile = p; break; }
        }
        const auto boldFile = std::filesystem::is_regular_file(boldPath, ec) ? boldPath : bodyFile;

        if (!bodyFile.empty()) {
            const auto regUtf8  = bodyFile.string();
            const auto boldUtf8 = boldFile.string();
            f.body  = atlas->AddFontFromFileTTF(regUtf8.c_str(),  sz(16.5f), &cfg, cjkRanges);
            f.bold  = atlas->AddFontFromFileTTF(boldUtf8.c_str(), sz(16.5f), &cfg, cjkRanges);
            f.title = atlas->AddFontFromFileTTF(boldUtf8.c_str(), sz(21.0f), &cfg, cjkRanges);
            f.kpi   = atlas->AddFontFromFileTTF(boldUtf8.c_str(), sz(24.0f), &cfg, cjkRanges);
            f.note  = atlas->AddFontFromFileTTF(regUtf8.c_str(),  sz(14.0f), &cfg, cjkRanges);
            f.cjk   = (f.body != nullptr);
        }
        if (std::filesystem::is_regular_file(monoPath, ec)) {
            ImFontConfig monoCfg = cfg;
            f.mono = atlas->AddFontFromFileTTF(monoPath.string().c_str(), sz(14.5f), &monoCfg, atlas->GetGlyphRangesDefault());
        }
    }
    if (!f.body)  f.body  = atlas->AddFontDefault();
    if (!f.bold)  f.bold  = f.body;
    if (!f.title) f.title = f.bold;
    if (!f.kpi)   f.kpi   = f.title;
    if (!f.note)  f.note  = f.body;
    if (!f.mono)  f.mono  = f.note;
    return f;
}

inline void applyTheme(float displayScale) {
    scale = displayScale;
    ImGui::GetStyle() = ImGuiStyle();
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = px(10);
    s.ChildRounding     = px(8);
    s.FrameRounding     = px(6);
    s.PopupRounding     = px(8);
    s.GrabRounding      = px(5);
    s.TabRounding       = px(6);
    s.ScrollbarRounding = px(8);

    s.WindowPadding     = {px(18), px(16)};
    s.FramePadding      = {px(11), px(7)};
    s.ItemSpacing       = {px(10), px(8)};
    s.ItemInnerSpacing  = {px(8), px(6)};
    s.CellPadding       = {px(10), px(7)};
    s.IndentSpacing     = px(18);
    s.ScrollbarSize     = px(10);
    s.GrabMinSize       = px(14);

    s.WindowBorderSize  = px(1);
    s.ChildBorderSize   = px(1);
    s.FrameBorderSize   = px(1);
    s.PopupBorderSize   = px(1);
    s.WindowTitleAlign  = {0.0f, 0.5f};
    s.ButtonTextAlign   = {0.5f, 0.5f};
    s.SelectableTextAlign = {0.0f, 0.5f};
    s.SeparatorTextBorderSize = px(1);
    s.SeparatorTextAlign = {0.0f, 0.5f};
    s.SeparatorTextPadding = {px(14), px(6)};
    s.DisabledAlpha     = 0.42f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kTextMuted;
    c[ImGuiCol_WindowBg]             = kCanvas;
    c[ImGuiCol_ChildBg]              = ImVec4{0, 0, 0, 0};
    c[ImGuiCol_PopupBg]              = ImVec4{0.067f, 0.086f, 0.114f, 0.98f};
    c[ImGuiCol_Border]               = kLine;
    c[ImGuiCol_BorderShadow]         = ImVec4{0, 0, 0, 0};
    c[ImGuiCol_FrameBg]              = kWell;
    c[ImGuiCol_FrameBgHovered]       = ImVec4{0.106f, 0.133f, 0.176f, 1.0f};
    c[ImGuiCol_FrameBgActive]        = ImVec4{0.137f, 0.173f, 0.224f, 1.0f};
    c[ImGuiCol_TitleBg]              = kSidebar;
    c[ImGuiCol_TitleBgActive]        = kSidebar;
    c[ImGuiCol_TitleBgCollapsed]     = kSidebar;
    c[ImGuiCol_MenuBarBg]            = kSidebar;
    c[ImGuiCol_ScrollbarBg]          = ImVec4{0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab]        = ImVec4{0.200f, 0.247f, 0.310f, 0.85f};
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.280f, 0.340f, 0.420f, 1.0f};
    c[ImGuiCol_ScrollbarGrabActive]  = kAccentSoft;
    c[ImGuiCol_CheckMark]            = kAccent;
    c[ImGuiCol_SliderGrab]           = kAccentSoft;
    c[ImGuiCol_SliderGrabActive]     = kAccent;
    c[ImGuiCol_Button]               = ImVec4{0.118f, 0.149f, 0.196f, 1.0f};
    c[ImGuiCol_ButtonHovered]        = ImVec4{0.165f, 0.208f, 0.271f, 1.0f};
    c[ImGuiCol_ButtonActive]         = alpha(kAccent, 0.28f);
    c[ImGuiCol_Header]               = ImVec4{0.125f, 0.157f, 0.204f, 1.0f};
    c[ImGuiCol_HeaderHovered]        = ImVec4{0.173f, 0.216f, 0.278f, 1.0f};
    c[ImGuiCol_HeaderActive]         = alpha(kAccent, 0.25f);
    c[ImGuiCol_Separator]            = kLine;
    c[ImGuiCol_SeparatorHovered]     = alpha(kAccent, 0.6f);
    c[ImGuiCol_SeparatorActive]      = kAccent;
    c[ImGuiCol_ResizeGrip]           = alpha(kLineStrong, 0.65f);
    c[ImGuiCol_ResizeGripHovered]    = kAccentSoft;
    c[ImGuiCol_ResizeGripActive]     = kAccent;
    c[ImGuiCol_Tab]                  = kCard;
    c[ImGuiCol_TabHovered]           = alpha(kAccent, 0.24f);
    c[ImGuiCol_TabSelected]          = ImVec4{0.145f, 0.184f, 0.239f, 1.0f};
    c[ImGuiCol_TabSelectedOverline]  = kAccent;
    c[ImGuiCol_TabDimmed]            = kSidebar;
    c[ImGuiCol_TabDimmedSelected]    = kCard;
    c[ImGuiCol_PlotLines]            = kInfo;
    c[ImGuiCol_PlotLinesHovered]     = kAccent;
    c[ImGuiCol_PlotHistogram]        = kAccent;
    c[ImGuiCol_PlotHistogramHovered] = kAccent;
    c[ImGuiCol_TableHeaderBg]        = ImVec4{0.094f, 0.118f, 0.157f, 1.0f};
    c[ImGuiCol_TableBorderStrong]    = kLine;
    c[ImGuiCol_TableBorderLight]     = alpha(kLine, 0.55f);
    c[ImGuiCol_TableRowBg]           = ImVec4{0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt]        = ImVec4{1, 1, 1, 0.024f};
    c[ImGuiCol_TextSelectedBg]       = alpha(kAccent, 0.28f);
    c[ImGuiCol_DragDropTarget]       = kAccent;
    c[ImGuiCol_NavCursor]            = kAccent;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4{0.012f, 0.016f, 0.024f, 0.68f};
}

} // namespace eflink::ui
