#pragma once
// Google Material Design 3 (Material You) design system for the EF Link menu,
// paired with the original v1.6.6 lightweight palette for the in-game HUD.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include "imgui.h"

namespace eflink::ui {

// ---- Material Design 3 Color Scheme (Google Blue baseline, Light & Dark) ---
struct Scheme {
    bool dark = false;
    // Surfaces (tonal hierarchy — no harsh neon borders)
    ImVec4 surface;                 // Main outer canvas
    ImVec4 surfaceContainerLowest;  // Elevated cards (pure white in light mode)
    ImVec4 surfaceContainerLow;     // Navigation drawer / app bar backdrop
    ImVec4 surfaceContainer;        // Grouped list / nested panels
    ImVec4 surfaceContainerHigh;    // Inputs, chips, dialogs
    ImVec4 surfaceContainerHighest; // Tracks, unselected switch fill
    // Text & Outlines
    ImVec4 onSurface;
    ImVec4 onSurfaceVariant;
    ImVec4 outline;
    ImVec4 outlineVariant;
    // Primary (Google Blue)
    ImVec4 primary;
    ImVec4 onPrimary;
    ImVec4 primaryContainer;
    ImVec4 onPrimaryContainer;
    // Secondary (Tonal pill / Navigation Drawer active indicator)
    ImVec4 secondary;
    ImVec4 secondaryContainer;
    ImVec4 onSecondaryContainer;
    // Tertiary / Success (Google Green)
    ImVec4 tertiary;
    ImVec4 tertiaryContainer;
    ImVec4 onTertiaryContainer;
    // Warning (Google Amber)
    ImVec4 warning;
    ImVec4 warningContainer;
    ImVec4 onWarningContainer;
    // Error (Google Red)
    ImVec4 error;
    ImVec4 onError;
    ImVec4 errorContainer;
    ImVec4 onErrorContainer;
    // Purple / Operator accent (Material Violet)
    ImVec4 violet;
    ImVec4 violetContainer;
    ImVec4 onVioletContainer;
};

constexpr ImVec4 hex(unsigned rgb, float a = 1.f) {
    return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}

inline Scheme makeLightScheme() {
    Scheme s{};
    s.dark                    = false;
    s.surface                 = hex(0xF0F4F9); // Iconic Google Android / Pixel canvas
    s.surfaceContainerLowest  = hex(0xFFFFFF); // Pure white elevated M3 cards
    s.surfaceContainerLow     = hex(0xF0F4F9); // Seamless Nav Drawer + Top App Bar
    s.surfaceContainer        = hex(0xE9EEF6); // Subtle inner wells
    s.surfaceContainerHigh    = hex(0xE3E8F0); // Filled inputs / hover layers
    s.surfaceContainerHighest = hex(0xDCE1E8); // Switch off track / progress bg
    s.onSurface               = hex(0x1F1F1F);
    s.onSurfaceVariant        = hex(0x444746);
    s.outline                 = hex(0x747775);
    s.outlineVariant          = hex(0xDCE0E5);
    s.primary                 = hex(0x0B57D0); // Google Blue 600
    s.onPrimary               = hex(0xFFFFFF);
    s.primaryContainer        = hex(0xD3E3FD);
    s.onPrimaryContainer      = hex(0x041E49);
    s.secondary               = hex(0x445E91);
    s.secondaryContainer      = hex(0xC2E7FF); // Signature Android Nav Drawer active pill
    s.onSecondaryContainer    = hex(0x001D35);
    s.tertiary                = hex(0x146C2E); // Google Green
    s.tertiaryContainer       = hex(0xC4EED0);
    s.onTertiaryContainer     = hex(0x072711);
    s.warning                 = hex(0xB06000); // Google Amber
    s.warningContainer        = hex(0xFEEFC3);
    s.onWarningContainer      = hex(0x4A2500);
    s.error                   = hex(0xB3261E); // Material 3 Error
    s.onError                 = hex(0xFFFFFF);
    s.errorContainer          = hex(0xF9DEDC);
    s.onErrorContainer        = hex(0x410E0B);
    s.violet                  = hex(0x6750A4); // Material You Violet
    s.violetContainer         = hex(0xEADDFF);
    s.onVioletContainer       = hex(0x21005D);
    return s;
}

inline Scheme makeDarkScheme() {
    Scheme s{};
    s.dark                    = true;
    s.surface                 = hex(0x131314); // Authentic Google Android Dark
    s.surfaceContainerLowest  = hex(0x1E1F20); // Tonal dark cards
    s.surfaceContainerLow     = hex(0x131314); // Seamless Nav Drawer + Top App Bar
    s.surfaceContainer        = hex(0x242628); // Inner wells
    s.surfaceContainerHigh    = hex(0x2B2D30); // Inputs / dialogs
    s.surfaceContainerHighest = hex(0x37393D); // Switch off track / progress bg
    s.onSurface               = hex(0xE3E3E3);
    s.onSurfaceVariant        = hex(0xC4C7C5);
    s.outline                 = hex(0x8E918F);
    s.outlineVariant          = hex(0x33363A);
    s.primary                 = hex(0xA8C7FA); // Google Blue Dark
    s.onPrimary               = hex(0x062E6F);
    s.primaryContainer        = hex(0x0842A0);
    s.onPrimaryContainer      = hex(0xD3E3FD);
    s.secondary               = hex(0xA8C7FA);
    s.secondaryContainer      = hex(0x004A77); // Signature Android Dark active pill
    s.onSecondaryContainer    = hex(0xC2E7FF);
    s.tertiary                = hex(0x6DD58C);
    s.tertiaryContainer       = hex(0x0F5223);
    s.onTertiaryContainer     = hex(0xC4EED0);
    s.warning                 = hex(0xFDD663);
    s.warningContainer        = hex(0x523A00);
    s.onWarningContainer      = hex(0xFEEFC3);
    s.error                   = hex(0xF2B8B5);
    s.onError                 = hex(0x601410);
    s.errorContainer          = hex(0x8C1D18);
    s.onErrorContainer        = hex(0xF9DEDC);
    s.violet                  = hex(0xD0BCFF);
    s.violetContainer         = hex(0x4F378B);
    s.onVioletContainer       = hex(0xEADDFF);
    return s;
}

inline thread_local Scheme md = makeLightScheme();

// ---- Hybrid Endfield × Material 3 In-Game HUD Palette ----------------------
// Preserves the iconic Endfield lime-yellow tactical accent (#E3F23D) and
// lightweight dark glass feel, while adding MD3 tonal elevation, soft outlines,
// and higher-contrast scrims for visibility over bright 3D scenes.
namespace hud_orig {
constexpr ImVec4 base     {0.055f, 0.067f, 0.086f, 0.96f}; // MD3-tinted dark tonal slate (#0E1116)
constexpr ImVec4 surface  {0.082f, 0.098f, 0.122f, 1.00f}; // Elevated HUD container (#15191F)
constexpr ImVec4 well     {0.114f, 0.133f, 0.165f, 1.00f}; // Input / track well (#1D222A)
constexpr ImVec4 rim      {0.850f, 0.900f, 0.980f, 0.14f}; // Subtle MD3 tonal hairline border
constexpr ImVec4 text     {0.955f, 0.965f, 0.980f, 1.00f}; // High-legibility foreground
constexpr ImVec4 dim      {0.730f, 0.765f, 0.810f, 1.00f}; // Readable secondary metadata
constexpr ImVec4 faint    {0.420f, 0.455f, 0.500f, 1.00f};
constexpr ImVec4 accent   {0.890f, 0.950f, 0.240f, 1.00f}; // Iconic Endfield #E3F23D
constexpr ImVec4 ink      {0.060f, 0.070f, 0.040f, 1.00f};
constexpr ImVec4 ok       {0.427f, 0.835f, 0.549f, 1.00f}; // MD3 Green (#6DD58C)
constexpr ImVec4 warn     {0.992f, 0.839f, 0.388f, 1.00f}; // MD3 Amber (#FDD663)
constexpr ImVec4 bad      {0.949f, 0.545f, 0.510f, 1.00f}; // MD3 Coral Red (#F28B82)
constexpr ImVec4 info     {0.659f, 0.780f, 0.980f, 1.00f}; // MD3 Google Blue (#A8C7FA)
} // namespace hud_orig

enum class Tone { Primary, Secondary, Success, Warning, Error, Violet, Neutral };

inline ImVec4 alpha(ImVec4 c, float a) { c.w *= a; return c; }
inline ImU32 u32(ImVec4 c, float a = 1.f) { c.w *= a; return ImGui::ColorConvertFloat4ToU32(c); }

inline ImVec4 toneFg(Tone t) {
    switch (t) {
    case Tone::Primary:   return md.primary;
    case Tone::Secondary: return md.secondary;
    case Tone::Success:   return md.tertiary;
    case Tone::Warning:   return md.warning;
    case Tone::Error:     return md.error;
    case Tone::Violet:    return md.violet;
    default:              return md.onSurfaceVariant;
    }
}

inline ImVec4 toneContainer(Tone t) {
    switch (t) {
    case Tone::Primary:   return md.primaryContainer;
    case Tone::Secondary: return md.secondaryContainer;
    case Tone::Success:   return md.tertiaryContainer;
    case Tone::Warning:   return md.warningContainer;
    case Tone::Error:     return md.errorContainer;
    case Tone::Violet:    return md.violetContainer;
    default:              return md.surfaceContainerHigh;
    }
}

inline ImVec4 toneOnContainer(Tone t) {
    switch (t) {
    case Tone::Primary:   return md.onPrimaryContainer;
    case Tone::Secondary: return md.onSecondaryContainer;
    case Tone::Success:   return md.onTertiaryContainer;
    case Tone::Warning:   return md.onWarningContainer;
    case Tone::Error:     return md.onErrorContainer;
    case Tone::Violet:    return md.onVioletContainer;
    default:              return md.onSurfaceVariant;
    }
}

inline ImVec4 damageElementColor(const std::string& key) {
    if (md.dark) {
        if (key == "fire")      return hex(0xFFB4A9);
        if (key == "pulse")     return hex(0x8AB4F8);
        if (key == "cryst")     return hex(0x78D9EC);
        if (key == "natural")   return hex(0x81C995);
        if (key == "ether")     return hex(0xD0BCFF);
        if (key == "real")      return hex(0xFDD663);
        if (key == "lifedrain") return hex(0xF6AEA9);
        return md.onSurfaceVariant;
    }
    if (key == "fire")      return hex(0xC5221F);
    if (key == "pulse")     return hex(0x1967D2);
    if (key == "cryst")     return hex(0x007B83);
    if (key == "natural")   return hex(0x137333);
    if (key == "ether")     return hex(0x7627BB);
    if (key == "real")      return hex(0xB06000);
    if (key == "lifedrain") return hex(0xB80672);
    return md.onSurfaceVariant;
}

inline ImVec4 rankColor(int rank) {
    if (md.dark) {
        switch (rank) {
        case 0: return hex(0xA8C7FA); // Google Blue
        case 1: return hex(0x78D9EC); // Google Cyan
        case 2: return hex(0x81C995); // Google Green
        case 3: return hex(0xFDD663); // Google Amber
        default: return hex(0xD0BCFF);
        }
    }
    switch (rank) {
    case 0: return hex(0x0B57D0); // Google Blue
    case 1: return hex(0x007B83); // Google Teal
    case 2: return hex(0x146C2E); // Google Green
    case 3: return hex(0xB06000); // Google Amber
    default: return hex(0x6750A4);
    }
}

inline ImVec4 hudRankColor(int rank) {
    switch (rank) {
    case 0: return hud_orig::accent;
    case 1: return hud_orig::info;
    case 2: return hud_orig::ok;
    case 3: return hud_orig::warn;
    default: return hex(0xB497FF);
    }
}

// ---- Scale & Fonts ---------------------------------------------------------
inline thread_local float gScale = 1.0f;
inline float px(float v) { return std::round(v * gScale); }
inline float scaleForHeight(unsigned clientHeight) {
    return (std::clamp)(float(clientHeight) / 1080.f, 0.85f, 1.55f);
}

struct Fonts {
    ImFont* body     = nullptr;
    ImFont* bold     = nullptr;
    ImFont* title    = nullptr;
    ImFont* kpi      = nullptr;
    ImFont* note     = nullptr;
    ImFont* mono     = nullptr;
};
inline thread_local Fonts fonts{};

inline Fonts loadFonts(ImFontAtlas* atlas, float scale) {
    Fonts f{};
    wchar_t winDir[MAX_PATH]{};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    const auto fontDir = std::filesystem::path(winDir) / L"Fonts";
    const auto regular = fontDir / L"msyh.ttc";
    const auto bold    = fontDir / L"msyhbd.ttc";
    const auto mono    = fontDir / L"consola.ttf";

    if (std::filesystem::is_regular_file(regular)) {
        const auto* cjk = atlas->GetGlyphRangesChineseFull();
        ImFontConfig cfg{};
        cfg.OversampleH = 2; cfg.OversampleV = 1;
        f.body = atlas->AddFontFromFileTTF(regular.string().c_str(), 15.5f * scale, &cfg, cjk);
        f.note = atlas->AddFontFromFileTTF(regular.string().c_str(), 13.5f * scale, &cfg, cjk);
        if (std::filesystem::is_regular_file(bold)) {
            f.bold  = atlas->AddFontFromFileTTF(bold.string().c_str(), 15.5f * scale, &cfg, cjk);
            f.title = atlas->AddFontFromFileTTF(bold.string().c_str(), 20.5f * scale, &cfg, cjk);
            f.kpi   = atlas->AddFontFromFileTTF(bold.string().c_str(), 24.0f * scale, &cfg, cjk);
        } else {
            f.bold = f.body;
            f.title = atlas->AddFontFromFileTTF(regular.string().c_str(), 20.0f * scale, &cfg, cjk);
            f.kpi   = atlas->AddFontFromFileTTF(regular.string().c_str(), 23.0f * scale, &cfg, cjk);
        }
    }
    if (std::filesystem::is_regular_file(mono)) {
        ImFontConfig mcfg{}; mcfg.OversampleH = 2;
        f.mono = atlas->AddFontFromFileTTF(mono.string().c_str(), 14.0f * scale, &mcfg, atlas->GetGlyphRangesDefault());
    }
    if (!f.body)  f.body  = atlas->AddFontDefault();
    if (!f.bold)  f.bold  = f.body;
    if (!f.title) f.title = f.bold;
    if (!f.kpi)   f.kpi   = f.title;
    if (!f.note)  f.note  = f.body;
    if (!f.mono)  f.mono  = f.body;
    return f;
}

inline void applyTheme(float scale, bool darkMode = false) {
    gScale = scale;
    md = darkMode ? makeDarkScheme() : makeLightScheme();

    ImGuiStyle s{};
    s.Alpha                     = 1.0f;
    s.DisabledAlpha             = 0.44f;
    s.WindowPadding             = {16.f, 14.f};
    s.WindowRounding            = 24.f;  // Iconic Material 3 Expressive large window radius
    s.WindowBorderSize          = 1.0f;
    s.WindowMinSize             = {80.f, 40.f};
    s.ChildRounding             = 20.f;  // Material 3 card corner radius
    s.ChildBorderSize           = 1.0f;
    s.PopupRounding             = 16.f;  // Material 3 menu / dialog radius
    s.PopupBorderSize           = 1.0f;
    s.FramePadding              = {14.f, 9.f};
    s.FrameRounding             = 12.f;  // Material 3 outlined/filled text field radius
    s.FrameBorderSize           = 1.0f;
    s.ItemSpacing               = {10.f, 8.f};
    s.ItemInnerSpacing          = {8.f, 6.f};
    s.CellPadding               = {10.f, 8.f};
    s.IndentSpacing             = 18.f;
    s.ScrollbarSize             = 7.f;
    s.ScrollbarRounding         = 8.f;
    s.GrabMinSize               = 16.f;
    s.GrabRounding              = 10.f;
    s.TabRounding               = 12.f;
    s.SeparatorTextBorderSize   = 1.0f;
    s.SeparatorTextPadding      = {12.f, 4.f};

    auto& c = s.Colors;
    c[ImGuiCol_Text]                 = md.onSurface;
    c[ImGuiCol_TextDisabled]         = md.onSurfaceVariant;
    c[ImGuiCol_WindowBg]             = md.surface;
    c[ImGuiCol_ChildBg]              = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg]              = md.surfaceContainerLowest;
    c[ImGuiCol_Border]               = md.outlineVariant;
    c[ImGuiCol_BorderShadow]         = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg]              = md.dark ? md.surfaceContainerHigh : md.surfaceContainerLowest;
    c[ImGuiCol_FrameBgHovered]       = md.surfaceContainer;
    c[ImGuiCol_FrameBgActive]        = md.surfaceContainerHigh;
    c[ImGuiCol_TitleBg]              = md.surface;
    c[ImGuiCol_TitleBgActive]        = md.surface;
    c[ImGuiCol_TitleBgCollapsed]     = md.surface;
    c[ImGuiCol_MenuBarBg]            = md.surface;
    c[ImGuiCol_ScrollbarBg]          = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab]        = alpha(md.outline, 0.35f);
    c[ImGuiCol_ScrollbarGrabHovered] = alpha(md.outline, 0.58f);
    c[ImGuiCol_ScrollbarGrabActive]  = alpha(md.primary, 0.78f);
    c[ImGuiCol_CheckMark]            = md.primary;
    c[ImGuiCol_SliderGrab]           = md.primary;
    c[ImGuiCol_SliderGrabActive]     = md.primary;
    c[ImGuiCol_Button]               = md.secondaryContainer;
    c[ImGuiCol_ButtonHovered]        = alpha(md.primary, 0.18f);
    c[ImGuiCol_ButtonActive]         = alpha(md.primary, 0.26f);
    c[ImGuiCol_Header]               = md.secondaryContainer;
    c[ImGuiCol_HeaderHovered]        = alpha(md.onSurface, 0.08f);
    c[ImGuiCol_HeaderActive]         = alpha(md.primary, 0.16f);
    c[ImGuiCol_Separator]            = md.outlineVariant;
    c[ImGuiCol_SeparatorHovered]     = md.outline;
    c[ImGuiCol_SeparatorActive]      = md.primary;
    c[ImGuiCol_ResizeGrip]           = alpha(md.outline, 0.22f);
    c[ImGuiCol_ResizeGripHovered]    = alpha(md.primary, 0.55f);
    c[ImGuiCol_ResizeGripActive]     = md.primary;
    c[ImGuiCol_TableHeaderBg]        = md.surfaceContainer;
    c[ImGuiCol_TableBorderStrong]    = md.outlineVariant;
    c[ImGuiCol_TableBorderLight]     = alpha(md.outlineVariant, 0.7f);
    c[ImGuiCol_TableRowBg]           = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt]        = alpha(md.onSurface, 0.025f);
    c[ImGuiCol_TextSelectedBg]       = alpha(md.primary, 0.22f);
    c[ImGuiCol_NavHighlight]         = md.primary;
    c[ImGuiCol_ModalWindowDimBg]     = {0.0f, 0.0f, 0.0f, md.dark ? 0.56f : 0.32f};

    s.ScaleAllSizes(scale);
    ImGui::GetStyle() = s;
}

} // namespace eflink::ui
