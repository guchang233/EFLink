#pragma once
// Small widget kit built on ImGui. Every EF Link surface (in-game menu, HUD,
// standalone preview) draws with these so one visual language stays in sync.
#include "link_ui_theme.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace eflink::ui {
// ui::fonts lives in link_ui_theme.h, next to loadFonts() which fills it in.

inline void pushSmall() {if(fonts.caption) ImGui::PushFont(fonts.caption);}
inline void popSmall() {if(fonts.caption) ImGui::PopFont();}
inline void pushTitle() {if(fonts.title) ImGui::PushFont(fonts.title);}
inline void popTitle() {if(fonts.title) ImGui::PopFont();}

// ---- primitives ------------------------------------------------------------
inline void vspace(float height) {if(height>0) ImGui::Dummy({0,height});}
inline void hspace(float width) {ImGui::SameLine(0,width);}

inline void withColor(ImVec4 color,const char* fmt,va_list args) {
    ImGui::PushStyleColor(ImGuiCol_Text,color);ImGui::TextV(fmt,args);ImGui::PopStyleColor();
}
inline void muted(const char* fmt,...) {va_list a;va_start(a,fmt);withColor(kTextMuted,fmt,a);va_end(a);}
inline void dim(const char* fmt,...) {va_list a;va_start(a,fmt);withColor(kTextDim,fmt,a);va_end(a);}
inline void toned(Tone tone,const char* fmt,...) {va_list a;va_start(a,fmt);withColor(toneColor(tone),fmt,a);va_end(a);}

// Muted paragraph that wraps to the content width.
inline void hint(const char* fmt,...) {
    ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);
    ImGui::PushTextWrapPos(0.f);
    va_list a;va_start(a,fmt);ImGui::TextWrappedV(fmt,a);va_end(a);
    ImGui::PopTextWrapPos();ImGui::PopStyleColor();
}

inline void divider() {vspace(px(3));ImGui::Separator();vspace(px(3));}

inline void alignRight(float widgetWidth) {
    ImGui::SameLine();
    const float target=ImGui::GetWindowContentRegionMax().x-widgetWidth;
    if(target>ImGui::GetCursorPosX()) ImGui::SetCursorPosX(target);
}

// Truncate on a UTF-8 boundary so CJK text never breaks mid-character.
inline std::string ellipsize(const std::string& text,float maxWidth) {
    if(text.empty()||maxWidth<=px(24)) return text;
    if(ImGui::CalcTextSize(text.c_str()).x<=maxWidth) return text;
    const std::string dots="…";
    std::string result;
    for(std::size_t i=0;i<text.size();) {
        const unsigned char lead=static_cast<unsigned char>(text[i]);
        std::size_t length=lead<0x80?1:(lead>>5)==0x6?2:(lead>>4)==0xe?3:(lead>>3)==0x1e?4:1;
        if(i+length>text.size()) length=1;
        const std::string candidate=result+text.substr(i,length);
        if(ImGui::CalcTextSize((candidate+dots).c_str()).x>maxWidth) break;
        result=candidate;i+=length;
    }
    return result+dots;
}

inline void dashedRect(ImDrawList* list,ImVec2 min,ImVec2 max,ImU32 color,float thickness,float dash) {
    for(float x=min.x;x<max.x;x+=dash*2) {
        const float end=(std::min)(x+dash,max.x);
        list->AddLine({x,min.y},{end,min.y},color,thickness);
        list->AddLine({x,max.y},{end,max.y},color,thickness);
    }
    for(float y=min.y;y<max.y;y+=dash*2) {
        const float end=(std::min)(y+dash,max.y);
        list->AddLine({min.x,y},{min.x,end},color,thickness);
        list->AddLine({max.x,y},{max.x,end},color,thickness);
    }
}

// ---- headings and cards ----------------------------------------------------
// Accent tick, title, optional right-aligned note, hairline filling the gap.
inline void sectionHeader(const char* title,const char* note=nullptr) {
    ImDrawList* list=ImGui::GetWindowDrawList();
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const float lineHeight=ImGui::GetTextLineHeight();
    const float tick=px(3);
    list->AddRectFilled({origin.x,origin.y+px(3)},{origin.x+tick,origin.y+lineHeight-px(3)},u32(kAccent),px(2));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+tick+px(10));
    ImGui::PushStyleColor(ImGuiCol_Text,kText);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    const float titleEnd=ImGui::GetItemRectMax().x;
    const float right=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x;
    float noteWidth=0;ImVec2 notePos{0,0};
    if(note&&*note) {
        const ImVec2 size=ImGui::CalcTextSize(note);
        noteWidth=size.x;notePos={right-size.x,origin.y+(lineHeight-size.y)*.5f};
    }
    const float lineFrom=titleEnd+px(14);
    const float lineTo=noteWidth>0?notePos.x-px(12):right;
    if(lineTo>lineFrom) list->AddLine({lineFrom,origin.y+lineHeight*.5f},{lineTo,origin.y+lineHeight*.5f},u32(kLine),px(1));
    if(noteWidth>0) {
        ImGui::SetCursorScreenPos(notePos);
        ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);ImGui::TextUnformatted(note);ImGui::PopStyleColor();
    }
    ImGui::SetCursorScreenPos({origin.x,origin.y+lineHeight});
    vspace(px(7));
}

struct Card {ImDrawList* list;ImVec2 origin;float width;ImVec2 padding;};
inline std::vector<Card>& cardStack() {static std::vector<Card> stack;return stack;}
inline ImVec2 cardPad() {return cardStack().empty()?ImVec2{0.f,0.f}:cardStack().back().padding;}
// Auto-height panel behind whatever is drawn between the pair: the background
// goes on a lower draw channel, so callers never need to know the height.
// ImGui cannot nest channel splits, so a nested card degrades to indentation.
inline void cardBegin(ImVec2 padding={px(16),px(13)}) {
    const Card card{ImGui::GetWindowDrawList(),ImGui::GetCursorScreenPos(),ImGui::GetContentRegionAvail().x,padding};
    const bool nested=!cardStack().empty();
    if(!nested) {card.list->ChannelsSplit(2);card.list->ChannelsSetCurrent(1);}
    cardStack().push_back(card);
    vspace(padding.y);
    ImGui::Indent(padding.x);
    ImGui::PushTextWrapPos(card.origin.x+card.width-padding.x);
}
inline void cardEnd() {
    if(cardStack().empty()) return;
    const Card card=cardStack().back();cardStack().pop_back();
    ImGui::PopTextWrapPos();
    ImGui::Unindent(card.padding.x);
    vspace(card.padding.y);
    if(cardStack().empty()) {
        const ImVec2 max{card.origin.x+card.width,ImGui::GetItemRectMax().y};
        card.list->ChannelsSetCurrent(0);
        card.list->AddRectFilled(card.origin,max,u32(kRaised),px(8));
        card.list->AddRect(card.origin,max,u32(kLine),px(8),0,px(1));
        card.list->ChannelsMerge();
    }
}
// Width a full-width widget should use inside the innermost card.
inline void fullWidth() {ImGui::SetNextItemWidth(-(cardPad().x+px(1)));}

// ---- badges ----------------------------------------------------------------
inline void badge(const char* text,Tone tone=Tone::Neutral) {
    const ImVec2 textSize=ImGui::CalcTextSize(text);
    const ImVec2 pad{px(8),px(2)};
    const float height=textSize.y+pad.y*2;
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const ImVec2 max{origin.x+textSize.x+pad.x*2,origin.y+height};
    ImDrawList* list=ImGui::GetWindowDrawList();
    list->AddRectFilled(origin,max,u32(tone,.14f),height*.5f);
    list->AddRect(origin,max,u32(tone,.38f),height*.5f,0,px(1));
    list->AddText({origin.x+pad.x,origin.y+pad.y},tone==Tone::Neutral?u32(kTextDim):u32(tone),text);
    ImGui::Dummy({max.x-origin.x,height});
}
inline void statusPill(const char* text,Tone tone,bool dot=true) {
    const float dotSize=px(6);
    const ImVec2 textSize=ImGui::CalcTextSize(text);
    const ImVec2 pad{px(9),px(3)};
    const float height=textSize.y+pad.y*2;
    const float dotSpace=dot?dotSize+px(6):0;
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const ImVec2 max{origin.x+textSize.x+pad.x*2+dotSpace,origin.y+height};
    ImDrawList* list=ImGui::GetWindowDrawList();
    list->AddRectFilled(origin,max,u32(tone,.13f),height*.5f);
    list->AddRect(origin,max,u32(tone,.42f),height*.5f,0,px(1));
    if(dot) list->AddCircleFilled({origin.x+pad.x+dotSize*.5f,origin.y+height*.5f},dotSize*.5f,u32(tone),12);
    list->AddText({origin.x+pad.x+dotSpace,origin.y+pad.y},tone==Tone::Neutral?u32(kTextDim):u32(tone),text);
    ImGui::Dummy({max.x-origin.x,height});
}

// ---- notices and empty states ---------------------------------------------
inline void notice(Tone tone,const char* text) {
    const float width=ImGui::GetContentRegionAvail().x;
    const float padding=px(12),bar=px(3);
    const ImVec2 textSize=ImGui::CalcTextSize(text,nullptr,false,width-padding*2-bar);
    const float height=textSize.y+padding*2;
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    ImDrawList* list=ImGui::GetWindowDrawList();
    list->AddRectFilled(origin,{origin.x+width,origin.y+height},u32(tone,.10f),px(6));
    list->AddRectFilled(origin,{origin.x+bar,origin.y+height},u32(tone,.85f),px(2));
    ImGui::SetCursorScreenPos({origin.x+bar+padding,origin.y+padding});
    ImGui::PushStyleColor(ImGuiCol_Text,toneColor(tone));
    ImGui::PushTextWrapPos(origin.x+width-padding);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos({origin.x,origin.y+height});
    ImGui::Dummy({0,0});
}
inline void emptyState(const char* title,const char* body=nullptr) {
    const float width=ImGui::GetContentRegionAvail().x;
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    vspace(px(16));
    ImGui::PushStyleColor(ImGuiCol_Text,kTextDim);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+((width-ImGui::CalcTextSize(title).x)*.5f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if(body&&*body) {
        vspace(px(5));
        ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);
        ImGui::PushTextWrapPos(origin.x+width-px(26));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+px(26));
        ImGui::TextWrapped("%s",body);
        ImGui::PopTextWrapPos();ImGui::PopStyleColor();
    }
    vspace(px(16));
    dashedRect(ImGui::GetWindowDrawList(),origin,{origin.x+width,ImGui::GetItemRectMax().y},u32(kLineStrong),px(1),px(5));
}

// ---- buttons ---------------------------------------------------------------
inline void buttonColors(ImU32 fill,ImU32 hovered,ImU32 active,ImVec4 text) {
    ImGui::PushStyleColor(ImGuiCol_Button,fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,active);
    ImGui::PushStyleColor(ImGuiCol_Text,text);
}
inline ImVec2 buttonSize(ImVec2 size) {if(size.y<=0) size.y=px(32);return size;}
inline float buttonWidth(const char* label) {return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;}
inline float pillWidth(const char* text,bool dot=true) {return ImGui::CalcTextSize(text).x+px(9)*2+(dot?px(12):0);}
inline float chipWidth(const char* label) {return ImGui::CalcTextSize(label).x+px(9)*2;}
inline bool primaryButton(const char* label,ImVec2 size={0,0}) {
    buttonColors(u32(kAccent,.20f),u32(kAccent,.32f),u32(kAccent,.44f),kAccent);
    const bool pressed=ImGui::Button(label,buttonSize(size));
    ImGui::PopStyleColor(4);
    return pressed;
}
inline bool secondaryButton(const char* label,ImVec2 size={0,0}) {return ImGui::Button(label,buttonSize(size));}
inline bool ghostButton(const char* label,ImVec2 size={0,0}) {
    buttonColors(0,u32(kTextDim,.12f),u32(kTextDim,.20f),kTextDim);
    const bool pressed=ImGui::Button(label,buttonSize(size));
    ImGui::PopStyleColor(4);
    return pressed;
}
inline bool dangerButton(const char* label,ImVec2 size={0,0}) {
    buttonColors(u32(kDanger,.16f),u32(kDanger,.28f),u32(kDanger,.40f),kDanger);
    const bool pressed=ImGui::Button(label,buttonSize(size));
    ImGui::PopStyleColor(4);
    return pressed;
}
inline bool chipButton(const char* label,Tone tone=Tone::Neutral) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{px(9),px(2)});
    buttonColors(u32(tone,.12f),u32(tone,.24f),u32(tone,.36f),tone==Tone::Neutral?kTextDim:toneColor(tone));
    const bool pressed=ImGui::Button(label);
    ImGui::PopStyleColor(4);ImGui::PopStyleVar();
    return pressed;
}

// Sidebar / list row: text is drawn by hand so hover and selection can differ.
inline bool navItem(const char* label,bool selected,const char* note,float height) {
    ImGui::PushID(label);
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Header,selected?u32(kAccent,.13f):0u);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,u32(kText,.07f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,u32(kAccent,.18f));
    const bool pressed=ImGui::Selectable("##row",selected,0,{0,height});
    const bool hovered=ImGui::IsItemHovered();
    const float width=ImGui::GetItemRectSize().x;
    ImGui::PopStyleColor(3);ImGui::PopID();
    ImDrawList* list=ImGui::GetWindowDrawList();
    const float textY=origin.y+(height-ImGui::GetTextLineHeight())*.5f;
    float noteWidth=0;
    if(note&&*note) noteWidth=ImGui::CalcTextSize(note).x+px(22);
    const std::string label_text=ellipsize(label,width-px(16)-noteWidth-px(10));
    if(selected) list->AddRectFilled({origin.x,origin.y+px(7)},{origin.x+px(3),origin.y+height-px(7)},u32(kAccent),px(1));
    list->AddText({origin.x+px(16),textY},u32(selected?kAccent:(hovered?kText:kTextDim)),label_text.c_str());
    if(note&&*note) {
        const ImVec2 noteSize=ImGui::CalcTextSize(note);
        list->AddText({origin.x+width-noteSize.x-px(14),origin.y+(height-noteSize.y)*.5f},u32(kTextMuted),note);
    }
    return pressed;
}

// ---- fields ----------------------------------------------------------------
inline void fieldLabel(const char* text,const char* note=nullptr) {
    ImGui::PushStyleColor(ImGuiCol_Text,kTextDim);ImGui::TextUnformatted(text);ImGui::PopStyleColor();
    if(note&&*note) {
        ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);
        ImGui::SameLine();ImGui::TextUnformatted(note);
        ImGui::PopStyleColor();
    }
    vspace(px(3));
}
inline bool field(const char* id,const char* hintText,char* buffer,std::size_t capacity,ImGuiInputTextFlags flags=0) {
    fullWidth();
    return ImGui::InputTextWithHint(id,hintText,buffer,capacity,flags);
}
inline bool sliderField(const char* label,int* value,int low,int high,const char* note=nullptr) {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-px(158));
    const bool changed=ImGui::SliderInt(label,value,low,high);
    if(note&&*note) {
        ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);
        ImGui::SameLine();ImGui::TextUnformatted(note);
        ImGui::PopStyleColor();
    }
    return changed;
}

// ---- switches --------------------------------------------------------------
inline std::map<std::string,float>& switchStates() {static std::map<std::string,float> states;return states;}
inline void resetSwitchStates() {switchStates().clear();}
// Clickable label with an animated switch on the right and an optional
// description line underneath.
inline bool toggleSwitch(const char* id,bool* value,const char* label,const char* description=nullptr) {
    ImGui::PushID(id);
    const ImVec2 start=ImGui::GetCursorPos();
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const float lineHeight=ImGui::GetTextLineHeight();
    ImGui::PushStyleColor(ImGuiCol_Header,u32(kText,0.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,u32(kText,.05f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,u32(kAccent,.12f));
    const bool pressed=ImGui::Selectable("##toggle",false,0,{0,lineHeight});
    const bool hovered=ImGui::IsItemHovered();
    const float rowWidth=ImGui::GetItemRectSize().x;
    ImGui::PopStyleColor(3);
    if(pressed) *value=!*value;

    float& state=switchStates()[id];
    const float target=*value?1.f:0.f;
    if(state==0.f&&target==1.f) state=1.f;   // snap the first frame of a saved-on switch
    const float step=ImGui::GetIO().DeltaTime*12.f;
    state=state<target?(std::min)(target,state+step):(std::max)(target,state-step);

    const float trackWidth=px(40),trackHeight=px(22);
    const ImVec2 trackMin{origin.x+rowWidth-trackWidth,origin.y+(lineHeight-trackHeight)*.5f};
    const ImVec2 trackMax{trackMin.x+trackWidth,trackMin.y+trackHeight};
    ImDrawList* list=ImGui::GetWindowDrawList();
    list->AddText(origin,u32(*value?kText:kTextDim),label);
    const ImVec4 mixed{kLineStrong.x+(kAccent.x-kLineStrong.x)*state,kLineStrong.y+(kAccent.y-kLineStrong.y)*state,
        kLineStrong.z+(kAccent.z-kLineStrong.z)*state,1};
    list->AddRectFilled(trackMin,trackMax,u32(mixed,.92f),trackHeight*.5f);
    if(hovered) list->AddRect(trackMin,trackMax,u32(kAccent,.45f),trackHeight*.5f,0,px(1));
    const float knob=trackHeight*.5f-px(2);
    const float knobX=trackMin.x+trackHeight*.5f+state*(trackWidth-trackHeight);
    list->AddCircleFilled({knobX,trackMin.y+trackHeight*.5f},knob+px(1),u32(kInk,.55f),16);
    list->AddCircleFilled({knobX,trackMin.y+trackHeight*.5f},knob,u32(*value?kInk:kTextDim),16);
    ImGui::PopID();
    if(description&&*description) {
        ImGui::SetCursorPos({start.x,start.y+lineHeight+ImGui::GetStyle().ItemSpacing.y});
        pushSmall();
        ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextWrapped("%s",description);
        ImGui::PopTextWrapPos();ImGui::PopStyleColor();
        popSmall();
    }
    vspace(px(4));
    return pressed;
}
inline bool checkRow(const char* id,const char* label,bool* value,const char* description=nullptr) {
    ImGui::PushID(id);
    const ImVec2 start=ImGui::GetCursorPos();
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const bool changed=ImGui::Checkbox("##box",value);
    const float boxWidth=ImGui::GetItemRectSize().x;
    ImGui::SetCursorPos({start.x+boxWidth+px(12),start.y});
    ImGui::PushStyleColor(ImGuiCol_Text,kText);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if(description&&*description) {
        ImGui::SetCursorPosX(start.x+boxWidth+px(12));
        pushSmall();
        ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);
        ImGui::PushTextWrapPos(origin.x+ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s",description);
        ImGui::PopTextWrapPos();ImGui::PopStyleColor();
        popSmall();
    }
    ImGui::PopID();
    vspace(px(2));
    return changed;
}

// ---- read-only rows --------------------------------------------------------
inline float keyColumn() {return px(132);}
inline void keyValueText(const char* label,const std::string& value) {
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);ImGui::TextUnformatted(label);ImGui::PopStyleColor();
    const float x=origin.x+keyColumn();
    const float available=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x-x;
    ImGui::SameLine();
    ImGui::SetCursorScreenPos({x,origin.y});
    ImGui::TextUnformatted(ellipsize(value,available).c_str());
}
inline void keyValue(const char* label,const char* fmt,...) {
    char buffer[1024]{};
    va_list args;va_start(args,fmt);std::vsnprintf(buffer,sizeof buffer,fmt,args);va_end(args);
    keyValueText(label,buffer);
}
// Small label over a value, used for the status bar and player summaries.
inline void statCell(const char* label,const char* value,Tone tone=Tone::Neutral) {
    ImGui::BeginGroup();
    pushSmall();
    ImGui::PushStyleColor(ImGuiCol_Text,kTextMuted);ImGui::TextUnformatted(label);ImGui::PopStyleColor();
    popSmall();
    ImGui::PushStyleColor(ImGuiCol_Text,tone==Tone::Neutral?kText:toneColor(tone));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::EndGroup();
}
}
