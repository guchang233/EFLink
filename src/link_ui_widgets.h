#pragma once
// Reusable Endfield Industrial-Tactical ImGui widget kit.
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
    d->AddText({at.x + 1.f, at.y + 1.f}, IM_COL32(0, 0, 0, 185), text);
    d->AddText(at, tint, text);
}

inline void shadowTextFont(ImDrawList* d, ImFont* font, float size, ImVec2 at, ImU32 tint, const char* text) {
    d->AddText(font, size, {at.x + 1.f, at.y + 1.f}, IM_COL32(0, 0, 0, 185), text);
    d->AddText(font, size, at, tint, text);
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
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
    va_list a; va_start(a, fmt); ImGui::TextV(fmt, a); va_end(a);
    ImGui::PopStyleColor();
}

inline void dim(const char* fmt, ...) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    va_list a; va_start(a, fmt); ImGui::TextV(fmt, a); va_end(a);
    ImGui::PopStyleColor();
}

inline void hint(const char* fmt, ...) {
    pushNote();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
    ImGui::PushTextWrapPos(0.f);
    va_list a; va_start(a, fmt); ImGui::TextWrappedV(fmt, a); va_end(a);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    popNote();
}

// ---- tactical chrome decorations -------------------------------------------
inline void drawWindowChrome(ImDrawList* d, ImVec2 min, ImVec2 max) {
    // Top subtle Endfield volt-to-cyan gradient accent bar inset within rounded corners.
    const float barH = px(2.5f);
    const float inset = px(12);
    const float midX = min.x + (max.x - min.x) * 0.55f;
    d->AddRectFilledMultiColor(
        {min.x + inset, min.y + px(1)},
        {midX, min.y + px(1) + barH},
        u32(kAccent, 0.95f), u32(kInfo, 0.70f),
        u32(kInfo, 0.70f),   u32(kAccent, 0.95f));
    d->AddRectFilledMultiColor(
        {midX, min.y + px(1)},
        {max.x - inset, min.y + px(1) + barH},
        u32(kInfo, 0.70f), u32(kInfo, 0.0f),
        u32(kInfo, 0.0f),  u32(kInfo, 0.70f));
}

inline void drawEndfieldLogo(ImDrawList* d, ImVec2 origin, float height) {
    // Geometric slanted double-bar tactical emblem (pure vector, razor sharp).
    const float slant = height * 0.28f;
    const float w1 = px(5.5f), w2 = px(3.0f), gap = px(3.5f);
    const float y0 = origin.y + px(2), y1 = origin.y + height - px(2);
    const ImU32 c1 = u32(kAccent);
    const ImU32 c2 = u32(kInfo, 0.9f);
    d->AddQuadFilled(
        {origin.x + slant, y0}, {origin.x + slant + w1, y0},
        {origin.x + w1, y1},    {origin.x, y1}, c1);
    const float x2 = origin.x + w1 + gap;
    d->AddQuadFilled(
        {x2 + slant, y0}, {x2 + slant + w2, y0},
        {x2 + w2, y1},    {x2, y1}, c2);
}

// ---- section headers & elevated cards --------------------------------------
inline void sectionHeader(const char* title, const char* note = nullptr, Tone tone = Tone::Accent) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    pushBold();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float lh = ImGui::GetTextLineHeight();
    const float tickW = px(3);
    d->AddRectFilled({origin.x, origin.y + px(2)}, {origin.x + tickW, origin.y + lh - px(2)}, u32(tone), px(1.5f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + tickW + px(9));
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    const float titleEnd = ImGui::GetItemRectMax().x;
    popBold();

    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float noteW = 0.f;
    ImVec2 notePos{};
    if (note && *note) {
        pushNote();
        const ImVec2 sz = ImGui::CalcTextSize(note);
        noteW = sz.x;
        notePos = {right - sz.x, origin.y + (lh - sz.y) * 0.5f};
        popNote();
    }
    const float lineFrom = titleEnd + px(12);
    const float lineTo = noteW > 0.f ? notePos.x - px(10) : right;
    if (lineTo > lineFrom) {
        d->AddLine({lineFrom, origin.y + lh * 0.5f}, {lineTo, origin.y + lh * 0.5f}, u32(kLine, 0.85f), px(1));
    }
    if (noteW > 0.f) {
        pushNote();
        ImGui::SetCursorScreenPos(notePos);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
        ImGui::TextUnformatted(note);
        ImGui::PopStyleColor();
        popNote();
    }
    ImGui::SetCursorScreenPos({origin.x, origin.y + lh});
    vspace(px(6));
}

inline void cardBegin(const char* id, const char* title = nullptr, ImVec2 size = {0, 0}, Tone titleTone = Tone::Accent) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
    ImGui::PushStyleColor(ImGuiCol_Border, kLine);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(15), px(13)});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
    const ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding
        | (size.y == 0.f ? ImGuiChildFlags_AutoResizeY : 0);
    ImGui::BeginChild(id, size, flags);
    // Subtle top sheen inside card.
    const ImVec2 p0 = ImGui::GetWindowPos();
    const ImVec2 sz = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddLine(
        {p0.x + px(8), p0.y + px(1)}, {p0.x + sz.x - px(8), p0.y + px(1)},
        IM_COL32(255, 255, 255, 13), px(1));
    if (title && *title) {
        sectionHeader(title, nullptr, titleTone);
    }
}

inline void cardEnd() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

inline void kpiCard(const char* id, const char* label, const char* bigValue, const char* subText, Tone tone, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
    ImGui::PushStyleColor(ImGuiCol_Border, alpha(toneColor(tone), 0.30f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(14), px(10)});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
    ImGui::BeginChild(id, {width, 0.f}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
    const ImVec2 p0 = ImGui::GetWindowPos();
    const ImVec2 sz = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled({p0.x, p0.y + px(10)}, {p0.x + px(3), p0.y + sz.y - px(10)}, u32(tone, 0.90f), px(1.5f));
    pushNote();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    popNote();
    vspace(px(2));
    pushKpi();
    ImGui::PushStyleColor(ImGuiCol_Text, tone == Tone::Neutral ? kText : toneColor(tone));
    ImGui::TextUnformatted(bigValue);
    ImGui::PopStyleColor();
    popKpi();
    if (subText && *subText) {
        vspace(px(1));
        pushNote();
        ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
        ImGui::TextUnformatted(subText);
        ImGui::PopStyleColor();
        popNote();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ---- badges, pills, notices, empty states ----------------------------------
inline float pillWidth(const char* text, bool dot = true) {
    pushNote();
    const float w = ImGui::CalcTextSize(text).x + px(10) * 2 + (dot ? px(12) : 0.f);
    popNote();
    return w;
}

inline void statusPill(const char* text, Tone tone, bool dot = true) {
    pushNote();
    const float dotSz = px(6);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad{px(10), px(3)};
    const float h = ts.y + pad.y * 2;
    const float dotSpace = dot ? dotSz + px(6) : 0.f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 m{o.x + ts.x + pad.x * 2 + dotSpace, o.y + h};
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(o, m, u32(tone, 0.14f), h * 0.5f);
    d->AddRect(o, m, u32(tone, 0.44f), h * 0.5f, 0, px(1));
    if (dot) {
        d->AddCircleFilled({o.x + pad.x + dotSz * 0.5f, o.y + h * 0.5f}, dotSz * 0.5f + px(1.5f), u32(tone, 0.25f), 12);
        d->AddCircleFilled({o.x + pad.x + dotSz * 0.5f, o.y + h * 0.5f}, dotSz * 0.5f, u32(tone), 12);
    }
    d->AddText({o.x + pad.x + dotSpace, o.y + pad.y}, tone == Tone::Neutral ? u32(kTextDim) : u32(tone), text);
    ImGui::Dummy({m.x - o.x, h});
    popNote();
}

inline void badge(const char* text, Tone tone = Tone::Neutral, bool solid = false) {
    pushNote();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad{px(7), px(1.5f)};
    const float h = ts.y + pad.y * 2;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 m{o.x + ts.x + pad.x * 2, o.y + h};
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (solid) {
        d->AddRectFilled(o, m, u32(tone, 0.95f), px(4));
        d->AddText({o.x + pad.x, o.y + pad.y}, u32(kAccentInk), text);
    } else {
        d->AddRectFilled(o, m, u32(tone, 0.14f), px(4));
        d->AddRect(o, m, u32(tone, 0.40f), px(4), 0, px(1));
        d->AddText({o.x + pad.x, o.y + pad.y}, tone == Tone::Neutral ? u32(kTextDim) : u32(tone), text);
    }
    ImGui::Dummy({m.x - o.x, h});
    popNote();
}

inline void noticeBanner(Tone tone, const char* text) {
    const float width = ImGui::GetContentRegionAvail().x;
    const float pad = px(11), bar = px(3);
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, (std::max)(px(40), width - pad * 2 - bar));
    const float h = ts.y + pad * 2;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(o, {o.x + width, o.y + h}, u32(tone, 0.11f), px(6));
    d->AddRect(o, {o.x + width, o.y + h}, u32(tone, 0.28f), px(6), 0, px(1));
    d->AddRectFilled(o, {o.x + bar, o.y + h}, u32(tone, 0.90f), px(2));
    ImGui::SetCursorScreenPos({o.x + bar + pad, o.y + pad});
    ImGui::PushStyleColor(ImGuiCol_Text, toneColor(tone));
    ImGui::PushTextWrapPos(o.x + width - pad);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos({o.x, o.y + h});
    ImGui::Dummy({0, 0});
}

inline void emptyState(const char* title, const char* body = nullptr) {
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    vspace(px(22));
    pushBold();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    const float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.f, (width - tw) * 0.5f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    popBold();
    if (body && *body) {
        vspace(px(5));
        pushNote();
        ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
        const float bw = ImGui::CalcTextSize(body).x;
        if (bw < width - px(40)) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - bw) * 0.5f);
            ImGui::TextUnformatted(body);
        } else {
            ImGui::Indent(px(24));
            ImGui::PushTextWrapPos(o.x + width - px(24));
            ImGui::TextWrapped("%s", body);
            ImGui::PopTextWrapPos();
            ImGui::Unindent(px(24));
        }
        ImGui::PopStyleColor();
        popNote();
    }
    vspace(px(22));
    const ImVec2 max{o.x + width, ImGui::GetItemRectMax().y};
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(o, max, u32(kWell, 0.45f), px(8));
    d->AddRect(o, max, u32(kLineStrong, 0.65f), px(8), 0, px(1));
}

// ---- buttons ---------------------------------------------------------------
inline float buttonWidth(const char* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.f;
}

inline bool primaryButton(const char* label, ImVec2 size = {0, 0}) {
    if (size.y == 0.f) size.y = px(34);
    pushBold();
    ImGui::PushStyleColor(ImGuiCol_Button,        kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.937f, 0.984f, 0.380f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kAccentSoft);
    ImGui::PushStyleColor(ImGuiCol_Border,        kAccent);
    ImGui::PushStyleColor(ImGuiCol_Text,          kAccentInk);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);
    popBold();
    return pressed;
}

inline bool secondaryButton(const char* label, ImVec2 size = {0, 0}) {
    if (size.y == 0.f) size.y = px(34);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.118f, 0.149f, 0.196f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.165f, 0.212f, 0.278f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(kAccent, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_Border,        kLineStrong);
    ImGui::PushStyleColor(ImGuiCol_Text,          kText);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);
    return pressed;
}

inline bool ghostButton(const char* label, ImVec2 size = {0, 0}) {
    if (size.y == 0.f) size.y = px(32);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(kTextDim, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(kTextDim, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_Border,        alpha(kLineStrong, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_Text,          kTextDim);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);
    return pressed;
}

inline bool dangerButton(const char* label, ImVec2 size = {0, 0}) {
    if (size.y == 0.f) size.y = px(34);
    ImGui::PushStyleColor(ImGuiCol_Button,        alpha(kDanger, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(kDanger, 0.28f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(kDanger, 0.42f));
    ImGui::PushStyleColor(ImGuiCol_Border,        alpha(kDanger, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_Text,          kDanger);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);
    return pressed;
}

inline bool chipButton(const char* label, Tone tone = Tone::Neutral) {
    pushNote();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {px(9), px(3)});
    ImGui::PushStyleColor(ImGuiCol_Button,        alpha(toneColor(tone), 0.13f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(toneColor(tone), 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  alpha(toneColor(tone), 0.38f));
    ImGui::PushStyleColor(ImGuiCol_Border,        alpha(toneColor(tone), 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Text,          tone == Tone::Neutral ? kTextDim : toneColor(tone));
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    popNote();
    return pressed;
}

// ---- navigation sidebar item with vector icon ------------------------------
enum class NavIcon { Lobby, Players, Combat, Chat, Settings, Diagnostics };

inline void drawNavIcon(ImDrawList* d, NavIcon icon, ImVec2 center, float r, ImU32 col) {
    const float t = px(1.5f);
    switch (icon) {
    case NavIcon::Lobby: {
        // Signal / hub diamond + inner dot
        d->AddQuad({center.x, center.y - r}, {center.x + r, center.y},
                   {center.x, center.y + r}, {center.x - r, center.y}, col, t);
        d->AddCircleFilled(center, r * 0.28f, col, 8);
        break;
    }
    case NavIcon::Players: {
        // Two stylized operator silhouettes (head + shoulders)
        d->AddCircle({center.x - r * 0.25f, center.y - r * 0.35f}, r * 0.32f, col, 12, t);
        d->AddLine({center.x - r * 0.85f, center.y + r * 0.65f}, {center.x + r * 0.35f, center.y + r * 0.65f}, col, t);
        d->AddCircleFilled({center.x + r * 0.55f, center.y - r * 0.20f}, r * 0.22f, col, 10);
        break;
    }
    case NavIcon::Combat: {
        // 3 ascending bar chart columns (damage meter)
        const float w = r * 0.42f;
        d->AddRectFilled({center.x - r * 0.85f, center.y + r * 0.05f}, {center.x - r * 0.85f + w, center.y + r * 0.78f}, col, px(1));
        d->AddRectFilled({center.x - w * 0.5f,  center.y - r * 0.78f}, {center.x + w * 0.5f,      center.y + r * 0.78f}, col, px(1));
        d->AddRectFilled({center.x + r * 0.85f - w, center.y - r * 0.32f}, {center.x + r * 0.85f, center.y + r * 0.78f}, col, px(1));
        break;
    }
    case NavIcon::Chat: {
        // Rounded speech bubble
        d->AddRect({center.x - r * 0.85f, center.y - r * 0.65f}, {center.x + r * 0.85f, center.y + r * 0.45f}, col, px(2.5f), 0, t);
        d->AddTriangleFilled({center.x - r * 0.35f, center.y + r * 0.42f}, {center.x - r * 0.55f, center.y + r * 0.88f}, {center.x + r * 0.05f, center.y + r * 0.42f}, col);
        break;
    }
    case NavIcon::Settings: {
        // Dual tactical slider bars
        d->AddLine({center.x - r * 0.85f, center.y - r * 0.38f}, {center.x + r * 0.85f, center.y - r * 0.38f}, col, t);
        d->AddCircleFilled({center.x + r * 0.25f, center.y - r * 0.38f}, r * 0.26f, col, 10);
        d->AddLine({center.x - r * 0.85f, center.y + r * 0.38f}, {center.x + r * 0.85f, center.y + r * 0.38f}, col, t);
        d->AddCircleFilled({center.x - r * 0.25f, center.y + r * 0.38f}, r * 0.26f, col, 10);
        break;
    }
    case NavIcon::Diagnostics: {
        // Telemetry heartbeat waveform
        const ImVec2 pts[5] = {
            {center.x - r * 0.90f, center.y},
            {center.x - r * 0.38f, center.y},
            {center.x - r * 0.12f, center.y - r * 0.72f},
            {center.x + r * 0.22f, center.y + r * 0.65f},
            {center.x + r * 0.90f, center.y}
        };
        d->AddPolyline(pts, 5, col, ImDrawFlags_None, t);
        break;
    }
    }
}

inline bool navItem(NavIcon icon, const char* label, bool selected, const char* badgeText = nullptr, Tone badgeTone = Tone::Neutral, float height = 0.f) {
    if (height <= 0.f) height = px(40);
    ImGui::PushID(label);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Header,        selected ? u32(kAccent, 0.13f) : 0u);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, u32(kText, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  u32(kAccent, 0.19f));
    const bool pressed = ImGui::Selectable("##nav", selected, 0, {0.f, height});
    const bool hovered = ImGui::IsItemHovered();
    const float w = ImGui::GetItemRectSize().x;
    ImGui::PopStyleColor(3);
    ImGui::PopID();

    ImDrawList* d = ImGui::GetWindowDrawList();
    if (selected) {
        d->AddRectFilled({o.x, o.y + px(7)}, {o.x + px(3.5f), o.y + height - px(7)}, u32(kAccent), px(1.5f));
        d->AddRect(o, {o.x + w, o.y + height}, u32(kAccent, 0.22f), px(6), 0, px(1));
    }
    const ImU32 fg = u32(selected ? kAccent : (hovered ? kText : kTextDim));
    drawNavIcon(d, icon, {o.x + px(22), o.y + height * 0.5f}, px(6.5f), fg);

    if (selected) pushBold();
    const float ty = o.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    d->AddText({o.x + px(38), ty}, fg, label);
    if (selected) popBold();

    if (badgeText && *badgeText) {
        pushNote();
        const ImVec2 bs = ImGui::CalcTextSize(badgeText);
        const ImVec2 bpad{px(6), px(1.5f)};
        const float bh = bs.y + bpad.y * 2;
        const ImVec2 bmin{o.x + w - bs.x - bpad.x * 2 - px(10), o.y + (height - bh) * 0.5f};
        const ImVec2 bmax{bmin.x + bs.x + bpad.x * 2, bmin.y + bh};
        d->AddRectFilled(bmin, bmax, u32(badgeTone, 0.18f), bh * 0.5f);
        d->AddRect(bmin, bmax, u32(badgeTone, 0.45f), bh * 0.5f, 0, px(1));
        d->AddText({bmin.x + bpad.x, bmin.y + bpad.y}, badgeTone == Tone::Neutral ? u32(kTextDim) : u32(badgeTone), badgeText);
        popNote();
    }
    return pressed;
}

// ---- animated toggle switch ------------------------------------------------
inline std::map<std::string, float>& switchAnim() {
    static thread_local std::map<std::string, float> states;
    return states;
}

inline bool toggleSwitch(const char* id, bool* value, const char* label, const char* description = nullptr) {
    ImGui::PushID(id);
    const ImVec2 start = ImGui::GetCursorPos();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float lh = (std::max)(ImGui::GetTextLineHeight(), px(22));
    ImGui::PushStyleColor(ImGuiCol_Header,        0u);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, u32(kText, 0.05f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  u32(kAccent, 0.10f));
    const bool pressed = ImGui::Selectable("##sw", false, 0, {availW, lh});
    const bool hovered = ImGui::IsItemHovered();
    const float rw = availW;
    ImGui::PopStyleColor(3);
    if (pressed) *value = !*value;

    float& anim = switchAnim()[id];
    const float target = *value ? 1.f : 0.f;
    const float step = ImGui::GetIO().DeltaTime * 14.f;
    if (std::abs(anim - target) < 0.005f) anim = target;
    else anim = anim < target ? (std::min)(target, anim + step) : (std::max)(target, anim - step);

    const float tw = px(40), th = px(20);
    const ImVec2 tmin{o.x + rw - tw - px(4), o.y + (lh - th) * 0.5f};
    const ImVec2 tmax{tmin.x + tw, tmin.y + th};
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddText({o.x + px(2), o.y + (lh - ImGui::GetTextLineHeight()) * 0.5f},
               u32(*value ? kText : kTextDim), label);

    const ImVec4 trackCol{
        kLineStrong.x + (kAccent.x - kLineStrong.x) * anim,
        kLineStrong.y + (kAccent.y - kLineStrong.y) * anim,
        kLineStrong.z + (kAccent.z - kLineStrong.z) * anim, 1.f
    };
    d->AddRectFilled(tmin, tmax, u32(trackCol, *value ? 0.92f : 0.72f), th * 0.5f);
    if (hovered) d->AddRect(tmin, tmax, u32(kAccent, 0.55f), th * 0.5f, 0, px(1));
    const float kr = th * 0.5f - px(2.5f);
    const float kx = tmin.x + th * 0.5f + anim * (tw - th);
    d->AddCircleFilled({kx, tmin.y + th * 0.5f}, kr, u32(*value ? kAccentInk : kText), 16);
    ImGui::PopID();

    if (description && *description) {
        ImGui::SetCursorPos({start.x + px(2), start.y + lh + px(2)});
        hint("%s", description);
    }
    vspace(px(4));
    return pressed;
}

// ---- progress bars & DPS sparkline -----------------------------------------
inline void drawProgressBar(ImDrawList* d, ImVec2 min, ImVec2 max, float fraction, ImU32 fillCol, ImU32 bgCol, int segments = 1) {
    fraction = std::clamp(fraction, 0.f, 1.f);
    const float r = (std::min)(px(3), (max.y - min.y) * 0.5f);
    if (segments <= 1) {
        d->AddRectFilled(min, max, bgCol, r);
        const float fillW = (max.x - min.x) * fraction;
        if (fillW > 1.f) {
            d->AddRectFilled(min, {min.x + fillW, max.y}, fillCol, r);
        }
        return;
    }
    const float gap = px(3);
    const float totalW = max.x - min.x;
    const float segW = (totalW - gap * float(segments - 1)) / float(segments);
    for (int i = 0; i < segments; ++i) {
        const float sx0 = min.x + float(i) * (segW + gap);
        const float sx1 = sx0 + segW;
        d->AddRectFilled({sx0, min.y}, {sx1, max.y}, bgCol, r);
        const float segFrac = std::clamp(fraction * float(segments) - float(i), 0.f, 1.f);
        if (segFrac > 0.005f) {
            d->AddRectFilled({sx0, min.y}, {sx0 + segW * segFrac, max.y}, fillCol, r);
        }
    }
}

inline void dpsSparkline(const std::vector<float>& values, ImVec2 size, Tone tone = Tone::Accent) {
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 m{o.x + size.x, o.y + size.y};
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(o, m, u32(kWell, 0.90f), px(6));
    d->AddRect(o, m, u32(kLine, 0.85f), px(6), 0, px(1));

    // Subtle horizontal grid lines
    for (int g = 1; g <= 2; ++g) {
        const float gy = o.y + size.y * (float(g) / 3.f);
        d->AddLine({o.x + px(4), gy}, {m.x - px(4), gy}, u32(kLine, 0.45f), px(1));
    }

    float peak = 1.f;
    for (float v : values) if (v > peak) peak = v;
    const std::size_t n = values.size();
    if (n > 0) {
        const float padX = px(6), padY = px(6);
        const float plotW = (std::max)(1.f, size.x - padX * 2.f);
        const float plotH = (std::max)(1.f, size.y - padY * 2.f);
        const float step = plotW / float(n);
        const float barW = (std::max)(px(2), step - px(2));
        for (std::size_t i = 0; i < n; ++i) {
            const float norm = std::clamp(values[i] / peak, 0.f, 1.f);
            const float bh = norm * plotH;
            const float bx0 = o.x + padX + float(i) * step;
            const float by1 = m.y - padY;
            if (bh > 1.f) {
                const float alphaScale = 0.35f + 0.65f * (float(i + 1) / float(n));
                d->AddRectFilled({bx0, by1 - bh}, {bx0 + barW, by1}, u32(tone, alphaScale * 0.55f), px(1.5f));
                d->AddRectFilled({bx0, by1 - bh}, {bx0 + barW, (std::min)(by1, by1 - bh + px(2.5f))}, u32(tone, alphaScale), px(1));
            } else {
                d->AddRectFilled({bx0, by1 - px(1.5f)}, {bx0 + barW, by1}, u32(kLineStrong, 0.4f));
            }
        }
    }
    ImGui::Dummy(size);
}

// ---- field labels & read-only kv rows --------------------------------------
inline void fieldLabel(const char* text, const char* note = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    if (note && *note) {
        ImGui::SameLine();
        pushNote();
        ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
        ImGui::TextUnformatted(note);
        ImGui::PopStyleColor();
        popNote();
    }
    vspace(px(2));
}

inline void kvRow(const char* key, const std::string& val, Tone tone = Tone::Neutral) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, tone == Tone::Neutral ? kText : toneColor(tone));
    ImGui::TextWrapped("%s", val.c_str());
    ImGui::PopStyleColor();
}

} // namespace eflink::ui
