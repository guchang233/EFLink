#include "link_imgui_mesh.h"
#include <iostream>
int main() {
    using namespace eflink::imguimesh;
    auto check=[](bool ok,const char* what){if(!ok) throw std::runtime_error(what);std::cout<<"PASS "<<what<<'\n';};
    Vertex input[]={{-10,5,0,0,255,0,0,255},{15,5,1,0,0,255,0,255},{5,25,.5f,1,0,0,255,255}};
    Mesh clipped;triangle(clipped,input,{0,0,10,10},100);
    check(!clipped.colors.empty()&&clipped.colors.size()%3==0,"partial triangle produces complete triangles");
    for(std::size_t i=0;i<clipped.colors.size();++i) {
        check(clipped.positions[i*3]>=0&&clipped.positions[i*3]<=10&&clipped.positions[i*3+1]>=90&&clipped.positions[i*3+1]<=100,"clip stays within all four bounds and flips Unity y");
        if(clipped.indices[i]!=int(i)) throw std::runtime_error("indices outside generated vertices");
    }
    Mesh excluded;triangle(excluded,input,{40,40,50,50},100);check(excluded.colors.empty(),"fully clipped triangles emit no vertices");
    Vertex inside[]={{1,1,.2f,.3f,10,20,30,40},{4,1,.4f,.3f,10,20,30,40},{1,4,.2f,.6f,10,20,30,40}};
    Mesh preserved;triangle(preserved,inside,{0,0,10,10},100);
    check(preserved.colors.size()==3&&preserved.colors[0]==0x281e140a&&preserved.uv[0]==.2f&&preserved.uv[1]==.3f,"unclipped triangle preserves UV and RGBA");
    IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={800,600};io.DeltaTime=.016f;
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);io.Fonts->SetTexID(17);
    ImGui::NewFrame();auto* list=ImGui::GetForegroundDrawList();list->PushClipRect({20,20},{50,50});list->AddRectFilled({0,0},{100,100},0xffffffff);list->PopClipRect();ImGui::Render();
    auto geometry=convert(*ImGui::GetDrawData(),17);check(!geometry.colors.empty(),"real ImGui command data is converted");
    for(std::size_t i=0;i<geometry.colors.size();++i) if(geometry.positions[i*3]<20||geometry.positions[i*3]>50||geometry.positions[i*3+1]<550||geometry.positions[i*3+1]>580) throw std::runtime_error("ImGui command scissor was ignored");
    check(convert(*ImGui::GetDrawData(),18).colors.empty(),"unknown texture IDs are skipped");ImGui::DestroyContext();
    check(chunks(0).empty(),"empty frame needs no Canvas mesh");
    check(chunks(60000).size()==1&&chunks(60000)[0].count==60000,"exact 16-bit chunk boundary stays in one mesh");
    const auto split=chunks(120003);
    check(split.size()==3&&split[0].count==60000&&split[1].begin==60000&&split[2].count==3,"large frame splits without breaking triangles");
    for(const auto& part:split) if(part.count>=65000||part.begin%3||part.count%3) throw std::runtime_error("uGUI 16-bit batch overflow");
    bool refused=false;try {chunks(65536);} catch(const std::runtime_error&) {refused=true;}check(refused,"incomplete triangle is rejected before native submission");
    return 0;
}
