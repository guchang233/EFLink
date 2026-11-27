#pragma once
// Authentic Google Material Design 3 (Android / Material You) ImGui Widget Kit.
#include "link_ui_theme.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace eflink::ui {

inline void pushBold()  { if (fonts.bold)  ImGui::PushFont(fonts.bold); }
inline void popBold()   { if (fonts.bold)  ImGui::PopFont(); }
inline void pushTitle() { if (fonts.title) ImGui::PushFont(fonts.title); }
inline void popTitle()  { if (fonts.title) ImGui::PopFont(); }
inline void pushKpi()   { if (fonts.kpi)   ImGui::PushFont(fonts.kpi); }
inline void popKpi()    { if (fonts.kpi)   ImGui::PopFont(); }
inline void pushNote()  { if (fonts.note)  ImGui::PushFont(fonts.note); }
inline void popNote()   { if (fonts.note)  ImGui::PopFont(); }
inline void pushMono()  { if (fonts.mono)  ImGui::PushFont(fonts.mono); }
inline void popMono()   { if (fonts.mono)  ImGui::PopFont(); }

inline void vspace(float h) { if (h > 0.f) ImGui::Dummy({0.f, h}); }
inline void hspace(float w) { ImGui::SameLine(0.f, w); }

inline void shadowText(ImDrawList* d, ImVec2 at, ImU32 tint, const char* text) {
    d->AddText({at.x + 1.f, at.y + 1.f}, IM_COL32(0, 0, 0, 175), text);
    d->AddText(at, tint, text);
}

inline void alignRight(float width) {
    ImGui::SameLine();
    const float target = ImGui::GetWindowContentRegionMax().x - width;
    if (target > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(target);
}

inline std::string ellipsize(const std::string& text, float maxWidth) {
    if (text.empty() || maxWidth <= px(24)) return text;
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    const std::string dots = "…";
    std::string result;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xe ? 3 : (lead >> 3) == 0x1e ? 4 : 1;
        if (i + len > text.size()) len = 1;
        const std::string cand = result + text.substr(i, len);
        if (ImGui::CalcTextSize((cand + dots).c_str()).x > maxWidth) break;
        result = cand;
        i += len;
    }
    return result + dots;
}

inline void muted(const char* fmt, ...) {
    ImGui::PushStyleColor(ImGuiCol_Text, md.onSurfaceVariant);
    va_list a; va_start(a, fmt); ImGui::TextV(fmt, a); va_end(a);
    ImGui::PopStyleColor();
}

inline void hint(const char* fmt, ...) {
    pushNote();
    ImGui::PushStyleColor(ImGuiCol_Text, md.onSurfaceVariant);
    ImGui::PushTextWrapPos(0.f);
    va_list a; va_start(a, fmt); ImGui::TextWrappedV(fmt, a); va_end(a);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    popNote();
}

// ---- Material 3 App Mark (clean Google-style rounded tonal icon) -----------
inline void drawAppMark(ImDrawList* d, ImVec2 center, float radius) {
    d->AddCircleFilled(center, radius, u32(md.primaryContainer), 28);
    // Clean geometric chain-link symbol inside circle
    const float r = radius * 0.32f;
    const float off = radius * 0.24f;
    const ImU32 fg = u32(md.primary);
    const float t = px(2.2f);
    d->AddRect({center.x - off - r, center.y - r * 0.72f}, {center.x + r * 0.25f, center.y + r * 0.72f}, fg, r * 0.72f, 0, t);
    d->AddRect({center.x - r * 0.25f, center.y - r * 0.72f}, {center.x + off + r, center.y + r * 0.72f}, fg, r * 0.72f, 0, t);
}

// ---- Material 3 Vector Icons (rounded strokes, Material Symbols style) -----
enum class Icon { Lobby, Players, Combat, Chat, Settings, Diagnostics, Sun, Moon, Close, Plus, Refresh, Reset };

inline void drawIcon(ImDrawList* d, Icon icon, ImVec2 c, float r, ImU32 col) {
    const float t = px(1.75f);
    switch (icon) {
    case Icon::Lobby: {
        // Material "hub" / "home" rounded house roof + body
        const ImVec2 roof[3] = {{c.x - r, c.y - r * 0.05f}, {c.x, c.y - r * 0.95f}, {c.x + r, c.y - r * 0.05f}};
        d->AddPolyline(roof, 3, col, ImDrawFlags_None, t);
        d->AddRect({c.x - r * 0.68f, c.y - r * 0.12f}, {c.x + r * 0.68f, c.y + r * 0.85f}, col, px(2), 0, t);
        break;
    }
    case Icon::Players: {
        // Material "group" icon
        d->AddCircle({c.x - r * 0.22f, c.y - r * 0.35f}, r * 0.34f, col, 16, t);
        d->AddBezierCubic({c.x - r * 0.85f, c.y + r * 0.75f}, {c.x - r * 0.85f, c.y + r * 0.15f},
                          {c.x + r * 0.42f, c.y + r * 0.15f}, {c.x + r * 0.42f, c.y + r * 0.75f}, col, t);
        d->AddCircle({c.x + r * 0.55f, c.y - r * 0.22f}, r * 0.24f, col, 12, t * 0.9f);
        break;
    }
    case Icon::Combat: {
        // Material "bar_chart" rounded columns
        const float bw = r * 0.38f;
        d->AddRectFilled({c.x - r * 0.85f, c.y + r * 0.05f}, {c.x - r * 0.85f + bw, c.y + r * 0.85f}, col, px(2));
        d->AddRectFilled({c.x - bw * 0.5f, c.y - r * 0.85f}, {c.x + bw * 0.5f, c.y + r * 0.85f}, col, px(2));
        d->AddRectFilled({c.x + r * 0.85f - bw, c.y - r * 0.35f}, {c.x + r * 0.85f, c.y + r * 0.85f}, col, px(2));
        break;
    }
    case Icon::Chat: {
        // Material "chat_bubble_outline"
        d->AddRect({c.x - r * 0.85f, c.y - r * 0.72f}, {c.x + r * 0.85f, c.y + r * 0.42f}, col, px(3.5f), 0, t);
        d->AddLine({c.x - r * 0.45f, c.y + r * 0.42f}, {c.x - r * 0.72f, c.y + r * 0.85f}, col, t);
        d->AddLine({c.x - r * 0.72f, c.y + r * 0.85f}, {c.x - r * 0.05f, c.y + r * 0.42f}, col, t);
        break;
    }
    case Icon::Settings: {
        // Material "tune" sliders
        d->AddLine({c.x - r * 0.85f, c.y - r * 0.40f}, {c.x + r * 0.85f, c.y - r * 0.40f}, col, t);
        d->AddLine({c.x - r * 0.85f, c.y + r * 0.40f}, {c.x + r * 0.85f, c.y + r * 0.40f}, col, t);
        d->AddCircleFilled({c.x - r * 0.25f, c.y - r * 0.40f}, r * 0.27f, col, 14);
        d->AddCircleFilled({c.x + r * 0.30f, c.y + r * 0.40f}, r * 0.27f, col, 14);
        break;
    }
    case Icon::Diagnostics: {
        // Material "monitoring" pulse line
        const ImVec2 pts[5] = {
            {c.x - r * 0.90f, c.y + r * 0.05f}, {c.x - r * 0.42f, c.y + r * 0.05f},
            {c.x - r * 0.14f, c.y - r * 0.75f}, {c.x + r * 0.22f, c.y + r * 0.72f},
            {c.x + r * 0.90f, c.y + r * 0.05f}
        };
        d->AddPolyline(pts, 5, col, ImDrawFlags_None, t);
        break;
    }
    case Icon::Sun: {
        d->AddCircle({c.x, c.y}, r * 0.44f, col, 16, t);
        for (int i = 0; i < 8; ++i) {
            const float a = float(i) * 0.785398f;
            d->AddLine({c.x + std::cos(a) * r * 0.68f, c.y + std::sin(a) * r * 0.68f},
                       {c.x + std::cos(a) * r * 0.96f, c.y + std::sin(a) * r * 0.96f}, col, t);
        }
        break;
    }
    case Icon::Moon: {
        d->AddCircleFilled({c.x, c.y}, r * 0.72f, col, 20);
        d->AddCircleFilled({c.x + r * 0.36f, c.y - r * 0.26f}, r * 0.62f, u32(md.surfaceContainerLow), 20);
        break;
    }
    case Icon::Close: {
        const float k = r * 0.66f;
        d->AddLine({c.x - k, c.y - k}, {c.x + k, c.y + k}, col, t);
        d->AddLine({c.x + k, c.y - k}, {c.x - k, c.y + k}, col, t);
        break;
    }
    case Icon::Plus: {
        const float k = r * 0.72f;
        d->AddLine({c.x - k, c.y}, {c.x + k, c.y}, col, t * 1.15f);
        d->AddLine({c.x, c.y - k}, {c.x, c.y + k}, col, t * 1.15f);
        break;
    }
    case Icon::Refresh: {
        d->PathArcTo(c, r * 0.68f, 0.4f, 5.4f, 18);
        d->PathStroke(col, ImDrawFlags_None, t);
        d->AddTriangleFilled({c.x + r * 0.86f, c.y - r * 0.42f}, {c.x + r * 0.34f, c.y - r * 0.56f}, {c.x + r * 0.66f, c.y - r * 0.04f}, col);
        break;
    }
    case Icon::Reset: {
        d->PathArcTo(c, r * 0.66f, 0.9f, 5.9f, 18);
        d->PathStroke(col, ImDrawFlags_None, t);
        break;
    }
    }
}

// ---- Material 3 Circular Icon Button (40x40 standard touch target) ---------
inline bool iconButton(const char* id, Icon icon, const char* tooltip = nullptr) {
    const float sz = px(38);
    ImGui::PushID(id);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##ib", {sz, sz});
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 c{o.x + sz * 0.5f, o.y + sz * 0.5f};
    if (act)      d->AddCircleFilled(c, sz * 0.5f, u32(md.onSurface, 0.14f), 24);
    else if (hov) d->AddCircleFilled(c, sz * 0.5f, u32(md.onSurface, 0.08f), 24);
    drawIcon(d, icon, c, px(8.5f), u32(hov ? md.onSurface : md.onSurfaceVariant));
    if (hov && tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked;
}

// ---- Material 3 Section Overline / Subheader -------------------------------
inline void sectionLabel(const char* title, const char* trailing = nullptr) {
    pushBold();
    ImGui::PushStyleColor(ImGuiCol_Text, md.primary);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    popBold();
    if (trailing && *trailing) {
        pushNote();
        alignRight(ImGui::CalcTextSize(trailing).x);
        muted("%s", trailing);
        popNote();
    }
    vspace(px(4));
}

// ---- Material 3 Elevated / Filled Card (20px rounded, clean white/tonal) ---
inline void cardBegin(const char* id, const char* title = nullptr, ImVec2 size = {0, 0}, const char* subtitle = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, md.surfaceContainerLowest);
    ImGui::PushStyleColor(ImGuiCol_Border, md.dark ? alpha(md.outlineVariant, 0.55f) : alpha(md.outlineVariant, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(20), px(16)});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(20));
    const ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding
        | (size.y == 0.f ? ImGuiChildFlags_AutoResizeY : 0);
    ImGui::BeginChild(id, size, flags, size.y == 0.f ? ImGuiWindowFlags_NoScrollbar : 0);
    if (title && *title) {
        pushBold();
        ImGui::PushStyleColor(ImGuiCol_Text, md.onSurface);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        popBold();
        if (subtitle && *subtitle) {
            pushNote();
            ImGui::PushStyleColor(ImGuiCol_Text, md.onSurfaceVariant);
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
            popNote();
        }
        vspace(px(8));
    }
}

inline void cardEnd() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ---- Material 3 Tonal Metric Card (Google Fit / Settings summary tile) -----
inline void kpiCard(const char* id, const char* label, const char* bigValue, const char* subText, Tone tone, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneContainer(tone));
    ImGui::PushStyleColor(ImGuiCol_Border, {0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(16), px(14)});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(18));
    ImGui::BeginChild(id, {width, 0.f}, ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
    const ImVec4 fg = toneOnContainer(tone);
    pushNote();
    ImGui::PushStyleColor(ImGuiCol_Text, alpha(fg, 0.82f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    popNote();
    vspace(px(3));
    pushKpi();
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImGui::TextUnformatted(bigValue);
    ImGui::PopStyleColor();
    popKpi();
    if (subText && *subText) {
        vspace(px(2));
        pushNote();
        ImGui::PushStyleColor(ImGuiCol_Text, alpha(fg, 0.72f));
        ImGui::TextUnformatted(subText);
        ImGui::PopStyleColor();
        popNote();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ---- Material 3 Status Chips & Tonal Badges --------------------------------
inline float pillWidth(const char* text, bool dot = true) {
    pushNote();
    const float w = ImGui::CalcTextSize(text).x + px(12) * 2 + (dot ? px(12) : 0.f);
    popNote();
    return w;
}

inline void statusPill(const char* text, Tone tone, bool dot = false) {
    pushNote();
    const float dotSz = px(6.5f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad{px(12), px(4)};
    const float h = ts.y + pad.y * 2;
    const float dotSpace = dot ? dotSz + px(6) : 0.f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 m{o.x + ts.x + pad.x * 2 + dotSpace, o.y + h};
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(o, m, u32(toneContainer(tone)), h * 0.5f);
    if (dot) {
        d->AddCircleFilled({o.x + pad.x + dotSz * 0.5f, o.y + h * 0.5f}, dotSz * 0.5f, u32(toneFg(tone)), 12);
    }
    d->AddText({o.x + pad.x + dotSpace, o.y + pad.y}, u32(toneOnContainer(tone)), text);
    ImGui::Dummy({m.x - o.x, h});
    popNote();
}

inline void badge(const char* text, Tone tone = Tone::Neutral, bool solid = false) {
    pushNote();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad{px(8), px(2)};
    const float h = ts.y + pad.y * 2;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 m{o.x + ts.x + pad.x * 2, o.y + h};
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (solid) {
        d->AddRectFilled(o, m, u32(toneFg(tone)), h * 0.5f);
        d->AddText({o.x + pad.x, o.y + pad.y}, u32(tone == Tone::Error ? md.onError : md.onPrimary), text);
    } else {
        d->AddRectFilled(o, m, u32(toneContainer(tone)), h * 0.5f);
        d->AddText({o.x + pad.x, o.y + pad.y}, u32(toneOnContainer(tone)), text);
    }
    ImGui::Dummy({m.x - o.x, h});
    popNote();
}

inline void noticeBanner(Tone tone, const char* text) {
    const float width = ImGui::GetContentRegionAvail().x;
    const float padX = px(16), padY = px(12);
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, (std::max)(px(40), width - padX * 2));
    const float h = ts.y + padY * 2;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(o, {o.x + width, o.y + h}, u32(toneContainer(tone)), px(14));
    ImGui::SetCursorScreenPos({o.x + padX, o.y + padY});
    ImGui::PushStyleColor(ImGuiCol_Text, toneOnContainer(tone));
    ImGui::PushTextWrapPos(o.x + width - padX);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos({o.x, o.y + h});
    ImGui::Dummy({0, 0});
}

inline void emptyState(const char* title, const char* body = nullptr) {
    const float width = ImGui::GetContentRegionAvail().x;
    vspace(px(22));
    pushBold();
    ImGui::PushStyleColor(ImGuiCol_Text, md.onSurface);
    const float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.f, (width - tw) * 0.5f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    popBold();
    if (body && *body) {
        vspace(px(6));
        pushNote();
        ImGui::PushStyleColor(ImGuiCol_Text, md.onSurfaceVariant);
        const float bw = ImGui::CalcTextSize(body).x;
        if (bw < width - px(48)) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - bw) * 0.5f);
            ImGui::TextUnformatted(body);
        } else {
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextWrapped("%s", body);
            ImGui::PopTextWrapPos();
        }
        ImGui::PopStyleColor();
        popNote();
    }
    vspace(px(22));
}

// ---- Material 3 Pill Buttons (Filled / Tonal / Outlined / Danger / Chip) ---
inline float buttonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + px(32);
}

inline bool filledButton(const char* label, ImVec2 size = {0, 0}) {
    const float h = size.y > 0.f ? size.y : px(38);
    ImGui::PushStyleColor(ImGuiCol_Button,        md.primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(md.primary, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(md.primary, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_Text,          md.onPrimary);
    ImGui::PushStyleColor(ImGuiCol_Border,        {0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    pushBold();
    const bool hit = ImGui::Button(label, {size.x, h});
    popBold();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return hit;
}

inline bool tonalButton(const char* label, ImVec2 size = {0, 0}) {
    const float h = size.y > 0.f ? size.y : px(38);
    ImGui::PushStyleColor(ImGuiCol_Button,        md.secondaryContainer);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(md.primary, md.dark ? 0.28f : 0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(md.primary, md.dark ? 0.36f : 0.26f));
    ImGui::PushStyleColor(ImGuiCol_Text,          md.onSecondaryContainer);
    ImGui::PushStyleColor(ImGuiCol_Border,        {0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    pushBold();
    const bool hit = ImGui::Button(label, {size.x, h});
    popBold();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return hit;
}

inline bool outlinedButton(const char* label, ImVec2 size = {0, 0}) {
    const float h = size.y > 0.f ? size.y : px(36);
    ImGui::PushStyleColor(ImGuiCol_Button,        {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(md.primary, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(md.primary, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_Text,          md.primary);
    ImGui::PushStyleColor(ImGuiCol_Border,        md.outline);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    const bool hit = ImGui::Button(label, {size.x, h});
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return hit;
}

inline bool textButton(const char* label, ImVec2 size = {0, 0}) {
    const float h = size.y > 0.f ? size.y : px(36);
    ImGui::PushStyleColor(ImGuiCol_Button,        {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(md.primary, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(md.primary, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_Text,          md.primary);
    ImGui::PushStyleColor(ImGuiCol_Border,        {0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    pushBold();
    const bool hit = ImGui::Button(label, {size.x, h});
    popBold();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return hit;
}

inline bool dangerButton(const char* label, ImVec2 size = {0, 0}) {
    const float h = size.y > 0.f ? size.y : px(36);
    ImGui::PushStyleColor(ImGuiCol_Button,        md.errorContainer);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(md.error, md.dark ? 0.34f : 0.22f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(md.error, md.dark ? 0.44f : 0.30f));
    ImGui::PushStyleColor(ImGuiCol_Text,          md.onErrorContainer);
    ImGui::PushStyleColor(ImGuiCol_Border,        {0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    pushBold();
    const bool hit = ImGui::Button(label, {size.x, h});
    popBold();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return hit;
}

inline const char* renderedEnd(const char* s) {
    const char* p = s;
    while (*p) {
        if (p[0] == '#' && p[1] == '#') break;
        ++p;
    }
    return p;
}

// Material 3 Filter / Assist Chip (8px rounded; checkmark drawn when selected)
inline bool filterChip(const char* label, bool selected, Tone tone = Tone::Secondary) {
    pushNote();
    const char* end = renderedEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float checkW = selected ? px(16) : 0.f;
    const float padX = px(12), h = px(30);
    const float w = ts.x + padX * 2 + checkW;
    ImGui::PushID(label);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const bool hit = ImGui::InvisibleButton("##chip", {w, h});
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 m{o.x + w, o.y + h};
    const float r = px(8);
    if (selected) {
        d->AddRectFilled(o, m, u32(toneContainer(tone)), r);
        if (act)      d->AddRectFilled(o, m, u32(md.onSurface, 0.12f), r);
        else if (hov) d->AddRectFilled(o, m, u32(md.onSurface, 0.06f), r);
        // Crisp Material checkmark
        const float cx = o.x + padX + px(2), cy = o.y + h * 0.5f;
        const ImU32 fg = u32(toneOnContainer(tone));
        const ImVec2 ck[3] = {{cx, cy}, {cx + px(3.5f), cy + px(3.5f)}, {cx + px(9.5f), cy - px(3.5f)}};
        d->AddPolyline(ck, 3, fg, ImDrawFlags_None, px(1.8f));
        d->AddText({o.x + padX + checkW, o.y + (h - ts.y) * 0.5f}, fg, label, end);
    } else {
        if (act)      d->AddRectFilled(o, m, u32(md.onSurface, 0.10f), r);
        else if (hov) d->AddRectFilled(o, m, u32(md.onSurface, 0.05f), r);
        d->AddRect(o, m, u32(md.outline, 0.65f), r, 0, px(1));
        d->AddText({o.x + padX, o.y + (h - ts.y) * 0.5f}, u32(md.onSurfaceVariant), label, end);
    }
    ImGui::PopID();
    popNote();
    return hit;
}

inline bool actionChip(const char* label, Tone tone = Tone::Neutral) {
    pushNote();
    const ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    const float h = px(28), padX = px(12);
    const float w = ts.x + padX * 2;
    const bool tonal = tone != Tone::Neutral;
    ImGui::PushStyleColor(ImGuiCol_Button,        tonal ? toneContainer(tone) : ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tonal ? alpha(toneFg(tone), 0.22f) : alpha(md.onSurface, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  tonal ? alpha(toneFg(tone), 0.30f) : alpha(md.onSurface, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_Text,          tonal ? toneOnContainer(tone) : md.onSurfaceVariant);
    ImGui::PushStyleColor(ImGuiCol_Border,        tonal ? ImVec4{0, 0, 0, 0} : md.outlineVariant);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {padX, px(4)});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, tonal ? 0.f : 1.f);
    const bool hit = ImGui::Button(label, {w, h});
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    popNote();
    return hit;
}

// ---- Material 3 Extended FAB (top of Navigation Drawer, like Gmail/Drive) --
inline bool extendedFab(const char* label, Icon icon, Tone tone = Tone::Primary) {
    const float h = px(44);
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(label);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const bool hit = ImGui::InvisibleButton("##fab", {w, h});
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 m{o.x + w, o.y + h};
    const float r = px(15);
    if (!md.dark) {
        d->AddRectFilled({o.x, o.y + px(2)}, {m.x, m.y + px(3)}, IM_COL32(0, 0, 0, 18), r);
    }
    d->AddRectFilled(o, m, u32(toneContainer(tone)), r);
    if (act)      d->AddRectFilled(o, m, u32(md.onSurface, 0.12f), r);
    else if (hov) d->AddRectFilled(o, m, u32(md.onSurface, 0.06f), r);
    const ImU32 fg = u32(toneOnContainer(tone));
    drawIcon(d, icon, {o.x + px(21), o.y + h * 0.5f}, px(8.0f), fg);
    pushBold();
    const float ty = o.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    d->AddText({o.x + px(40), ty}, fg, label);
    popBold();
    ImGui::PopID();
    return hit;
}

// ---- Material 3 Navigation Drawer Item (Full-Pill Indicator!) --------------
inline bool navDrawerItem(Icon icon, const char* label, bool selected, const char* badgeText = nullptr) {
    const float h = px(40);
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(label);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##nav", {w, h});
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    ImGui::PopID();

    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 m{o.x + w, o.y + h};
    const float r = h * 0.5f;
    if (selected) {
        d->AddRectFilled(o, m, u32(md.secondaryContainer), r);
        if (hovered) d->AddRectFilled(o, m, u32(md.onSecondaryContainer, 0.06f), r);
    } else if (active) {
        d->AddRectFilled(o, m, u32(md.onSurface, 0.12f), r);
    } else if (hovered) {
        d->AddRectFilled(o, m, u32(md.onSurface, 0.07f), r);
    }

    const ImU32 fg = u32(selected ? md.onSecondaryContainer : md.onSurfaceVariant);
    drawIcon(d, icon, {o.x + px(22), o.y + h * 0.5f}, px(7.5f), fg);

    if (selected) pushBold();
    const float ty = o.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    d->AddText({o.x + px(42), ty}, fg, label);
    if (selected) popBold();

    if (badgeText && *badgeText) {
        pushNote();
        const ImVec2 bs = ImGui::CalcTextSize(badgeText);
        d->AddText({m.x - bs.x - px(16), o.y + (h - bs.y) * 0.5f},
                   u32(selected ? md.onSecondaryContainer : md.onSurfaceVariant), badgeText);
        popNote();
    }
    return pressed;
}

// ---- Material 3 Switch (Signature Android 13/14/15/16 Switch w/ Checkmark) -
inline std::map<std::string, float>& switchAnim() {
    static thread_local std::map<std::string, float> states;
    return states;
}

inline bool toggleSwitch(const char* id, bool* value, const char* label, const char* description = nullptr) {
    ImGui::PushID(id);
    const ImVec2 start = ImGui::GetCursorPos();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float tw = px(48), th = px(28);
    const float lh = (std::max)(ImGui::GetTextLineHeight(), th + px(2));

    const bool pressed = ImGui::InvisibleButton("##sw", {availW, lh});
    const bool hovered = ImGui::IsItemHovered();
    if (pressed) *value = !*value;

    float& anim = switchAnim()[id];
    const float target = *value ? 1.f : 0.f;
    const float step = ImGui::GetIO().DeltaTime * 14.f;
    if (std::abs(anim - target) < 0.005f) anim = target;
    else anim = anim < target ? (std::min)(target, anim + step) : (std::max)(target, anim - step);

    ImDrawList* d = ImGui::GetWindowDrawList();
    if (hovered) {
        d->AddRectFilled({o.x - px(6), o.y - px(2)}, {o.x + availW + px(6), o.y + lh + px(2)}, u32(md.onSurface, 0.04f), px(10));
    }
    d->AddText({o.x, o.y + (lh - ImGui::GetTextLineHeight()) * 0.5f}, u32(md.onSurface), label);

    // Track
    const ImVec2 tmin{o.x + availW - tw, o.y + (lh - th) * 0.5f};
    const ImVec2 tmax{tmin.x + tw, tmin.y + th};
    const ImVec4 trk{
        md.surfaceContainerHighest.x + (md.primary.x - md.surfaceContainerHighest.x) * anim,
        md.surfaceContainerHighest.y + (md.primary.y - md.surfaceContainerHighest.y) * anim,
        md.surfaceContainerHighest.z + (md.primary.z - md.surfaceContainerHighest.z) * anim, 1.f
    };
    d->AddRectFilled(tmin, tmax, u32(trk), th * 0.5f);
    if (anim < 0.95f) {
        d->AddRect(tmin, tmax, u32(md.outline, 1.f - anim), th * 0.5f, 0, px(1.8f));
    }

    // Thumb grows from 14px (OFF) to 22px (ON) with checkmark inside!
    const float thumbR = px(7.0f) + anim * px(4.0f);
    const float kx = tmin.x + th * 0.5f + anim * (tw - th);
    const float ky = tmin.y + th * 0.5f;
    if (hovered) {
        d->AddCircleFilled({kx, ky}, thumbR + px(5), u32(*value ? md.primary : md.onSurface, 0.10f), 20);
    }
    const ImVec4 thumbCol{
        md.outline.x + (md.onPrimary.x - md.outline.x) * anim,
        md.outline.y + (md.onPrimary.y - md.outline.y) * anim,
        md.outline.z + (md.onPrimary.z - md.outline.z) * anim, 1.f
    };
    d->AddCircleFilled({kx, ky}, thumbR, u32(thumbCol), 20);
    if (anim > 0.55f) {
        const float a = (anim - 0.55f) / 0.45f;
        const ImVec2 ck[3] = {{kx - px(4.2f), ky + px(0.3f)}, {kx - px(1.2f), ky + px(3.3f)}, {kx + px(4.6f), ky - px(3.2f)}};
        d->AddPolyline(ck, 3, u32(md.primary, a), ImDrawFlags_None, px(1.9f));
    }
    ImGui::PopID();

    if (description && *description) {
        ImGui::SetCursorPos({start.x, start.y + lh + px(1)});
        pushNote();
        ImGui::PushStyleColor(ImGuiCol_Text, md.onSurfaceVariant);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availW - tw - px(12));
        ImGui::TextWrapped("%s", description);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        popNote();
    }
    vspace(px(8));
    return pressed;
}

// ---- Material 3 Leading Tonal Circle Avatar --------------------------------
inline void avatarCircle(const char* text, Tone tone = Tone::Primary, float diameter = 0.f) {
    if (diameter <= 0.f) diameter = px(34);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 c{o.x + diameter * 0.5f, o.y + diameter * 0.5f};
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddCircleFilled(c, diameter * 0.5f, u32(toneContainer(tone)), 24);
    pushBold();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    d->AddText({c.x - ts.x * 0.5f, c.y - ts.y * 0.5f}, u32(toneOnContainer(tone)), text);
    popBold();
    ImGui::Dummy({diameter, diameter});
}

// ---- Material 3 Linear Progress Indicator & Segmented Bar ------------------
inline void drawProgressBar(ImDrawList* d, ImVec2 min, ImVec2 max, float fraction, ImU32 fillCol, ImU32 bgCol, int segments = 1) {
    fraction = std::clamp(fraction, 0.f, 1.f);
    const float r = (max.y - min.y) * 0.5f;
    if (segments <= 1) {
        d->AddRectFilled(min, max, bgCol, r);
        const float fillW = (max.x - min.x) * fraction;
        if (fillW > 1.f) {
            d->AddRectFilled(min, {min.x + fillW, max.y}, fillCol, r);
        }
        return;
    }
    const float gap = px(4);
    const float totalW = max.x - min.x;
    const float segW = (totalW - gap * float(segments - 1)) / float(segments);
    const float scaled = fraction * float(segments);
    for (int i = 0; i < segments; ++i) {
        const float x0 = min.x + float(i) * (segW + gap);
        const float x1 = x0 + segW;
        d->AddRectFilled({x0, min.y}, {x1, max.y}, bgCol, r);
        const float segFrac = std::clamp(scaled - float(i), 0.f, 1.f);
        if (segFrac > 0.005f) {
            d->AddRectFilled({x0, min.y}, {x0 + segW * segFrac, max.y}, fillCol, r);
        }
    }
}

// ---- Material 3 Rounded-Top DPS Bar Chart (Google Fit / Wellbeing style) ---
inline void dpsSparkline(const std::vector<double>& values, float height = 0.f) {
    if (height <= 0.f) height = px(78);
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 m{o.x + width, o.y + height};
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(o, m, u32(md.surfaceContainer), px(14));

    // Subtle horizontal reference gridlines
    for (int g = 1; g <= 2; ++g) {
        const float gy = o.y + height * (float(g) / 3.f);
        d->AddLine({o.x + px(12), gy}, {m.x - px(12), gy}, u32(md.outlineVariant, 0.75f), px(1));
    }

    if (values.empty()) { ImGui::Dummy({width, height}); return; }
    double maxV = 1.0;
    for (double v : values) if (v > maxV) maxV = v;
    const int n = int(values.size());
    const float padX = px(12), padY = px(10);
    const float innerW = (std::max)(px(20), width - padX * 2);
    const float innerH = (std::max)(px(12), height - padY * 2);
    const float gap = px(3);
    const float barW = (std::max)(px(3), (innerW - gap * float(n - 1)) / float(n));

    for (int i = 0; i < n; ++i) {
        const float frac = float(std::clamp(values[i] / maxV, 0.0, 1.0));
        const float x0 = o.x + padX + float(i) * (barW + gap);
        const float x1 = x0 + barW;
        const float y1 = m.y - padY;
        const float bh = (std::max)(px(2.5f), innerH * frac);
        const float y0 = y1 - bh;
        const ImU32 barCol = frac > 0.01f ? u32(md.primary, 0.40f + 0.55f * frac) : u32(md.outlineVariant, 0.65f);
        d->AddRectFilled({x0, y0}, {x1, y1}, barCol, px(4), ImDrawFlags_RoundCornersTop);
    }
    ImGui::Dummy({width, height});
}

// ---- Field Label & Key-Value List Row --------------------------------------
inline void fieldLabel(const char* label, const char* hintText = nullptr) {
    pushNote();
    ImGui::PushStyleColor(ImGuiCol_Text, md.onSurfaceVariant);
    ImGui::TextUnformatted(label);
    if (hintText && *hintText) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, alpha(md.onSurfaceVariant, 0.72f));
        ImGui::Text("· %s", hintText);
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();
    popNote();
}

inline void kvRow(const char* key, const std::string& val, Tone valTone = Tone::Neutral) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, md.onSurfaceVariant);
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, valTone == Tone::Neutral ? md.onSurface : toneFg(valTone));
    ImGui::TextWrapped("%s", val.c_str());
    ImGui::PopStyleColor();
}

} // namespace eflink::ui
