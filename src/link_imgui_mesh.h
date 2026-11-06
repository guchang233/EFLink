#pragma once
#include "imgui.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace eflink::imguimesh {
struct Vertex {float x,y,u,v,r,g,b,a;};
struct Mesh {std::vector<float> positions,uv;std::vector<unsigned> colors;std::vector<int> indices;
    // Diagnostics for the in-game canvas: a single degenerate triangle can fill the
    // whole screen with a colour wash, so the bounds and the drop count travel with
    // the geometry instead of staying invisible.
    float minX=0,maxX=0,minY=0,maxY=0;std::size_t triangles=0,dropped=0;};
// uGUI CanvasRenderer uses 16-bit indices. Keep complete triangles below its
// 65k vertex limit and rebase each independent mesh to zero.
constexpr std::size_t canvasChunkVertices=60000;
struct Chunk {std::size_t begin,count;};
inline std::vector<Chunk> chunks(std::size_t vertices) {
    if(vertices%3) throw std::runtime_error("Canvas geometry has an incomplete triangle");
    std::vector<Chunk> result;
    for(std::size_t begin=0;begin<vertices;begin+=canvasChunkVertices) result.push_back({begin,(std::min)(canvasChunkVertices,vertices-begin)});
    return result;
}
inline Vertex mix(const Vertex& a,const Vertex& b,float t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t,
        a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};
}
inline Vertex vertex(const ImDrawVert& v,ImVec2 origin) {
    return {v.pos.x-origin.x,v.pos.y-origin.y,v.uv.x,v.uv.y,float(v.col&255),float((v.col>>8)&255),float((v.col>>16)&255),float(v.col>>24)};
}
inline void triangle(Mesh& mesh,const Vertex* source,ImVec4 clip,float height,float limit) {
    Vertex a[12],b[12];std::copy_n(source,3,a);int count=3;
    for(int side=0;side<4&&count;++side) {
        auto distance=[&](const Vertex& v) {switch(side) {case 0:return v.x-clip.x;case 1:return clip.z-v.x;case 2:return v.y-clip.y;default:return clip.w-v.y;}};
        int next=0;
        for(int i=0;i<count;++i) {
            const auto& prev=a[(i+count-1)%count];const auto& cur=a[i];float pd=distance(prev),cd=distance(cur);
            if((pd>=0)!=(cd>=0)) b[next++]=mix(prev,cur,pd/(pd-cd));
            if(cd>=0) b[next++]=cur;
        }
        count=next;std::copy_n(b,count,a);
    }
    // One non-finite or wildly out-of-range vertex makes the GPU fill the screen
    // with a single triangle, which reads as a colour wash over the game. Drop the
    // triangle instead of drawing it, and keep the count for the status page.
    if(count<3) return;
    float x0=a[0].x,x1=a[0].x,y0=a[0].y,y1=a[0].y;
    for(int i=0;i<count;++i) {
        if(!std::isfinite(a[i].x)||!std::isfinite(a[i].y)) {++mesh.dropped;return;}
        x0=(std::min)(x0,a[i].x);x1=(std::max)(x1,a[i].x);
        y0=(std::min)(y0,a[i].y);y1=(std::max)(y1,a[i].y);
    }
    if(x1-x0>limit||y1-y0>limit) {++mesh.dropped;return;}
    for(int i=1;i+1<count;++i) for(int j:{0,i,i+1}) {
        const auto& v=a[j];mesh.indices.push_back(int(mesh.colors.size()));
        mesh.positions.insert(mesh.positions.end(),{v.x,height-v.y,0});mesh.uv.insert(mesh.uv.end(),{v.u,v.v});
        auto byte=[](float c){return unsigned((std::clamp)(std::round(c),0.f,255.f));};
        mesh.colors.push_back(byte(v.r)|(byte(v.g)<<8)|(byte(v.b)<<16)|(byte(v.a)<<24));
    }
    if(!mesh.triangles) {mesh.minX=x0;mesh.maxX=x1;mesh.minY=y0;mesh.maxY=y1;}
    else {mesh.minX=(std::min)(mesh.minX,x0);mesh.maxX=(std::max)(mesh.maxX,x1);
          mesh.minY=(std::min)(mesh.minY,y0);mesh.maxY=(std::max)(mesh.maxY,y1);}
    ++mesh.triangles;
}
inline Mesh convert(const ImDrawData& data,ImTextureID atlas) {
    Mesh mesh;
    const float limit=8.f*(std::max)(data.DisplaySize.x,data.DisplaySize.y);
    for(const auto* list:data.CmdLists) for(const auto& cmd:list->CmdBuffer) {
        if(cmd.UserCallback) {if(cmd.UserCallback!=ImDrawCallback_ResetRenderState) cmd.UserCallback(list,&cmd);continue;}
        if(cmd.GetTexID()!=atlas) continue;
        const auto p=data.DisplayPos;ImVec4 clip{(std::max)(0.f,cmd.ClipRect.x-p.x),(std::max)(0.f,cmd.ClipRect.y-p.y),
            (std::min)(data.DisplaySize.x,cmd.ClipRect.z-p.x),(std::min)(data.DisplaySize.y,cmd.ClipRect.w-p.y)};
        if(clip.x>=clip.z||clip.y>=clip.w) continue;
        for(unsigned i=0;i+2<cmd.ElemCount;i+=3) {
            Vertex vertices[3];bool valid=true;
            for(unsigned j=0;j<3;++j) {
                const auto idx=std::size_t(cmd.IdxOffset)+i+j;
                if(idx>=std::size_t(list->IdxBuffer.Size)) {valid=false;break;}
                const auto vi=std::size_t(list->IdxBuffer[int(idx)])+cmd.VtxOffset;
                if(vi>=std::size_t(list->VtxBuffer.Size)) {valid=false;break;}
                vertices[j]=vertex(list->VtxBuffer[int(vi)],p);
                if(!std::isfinite(vertices[j].x)||!std::isfinite(vertices[j].y)) valid=false;
            }
            if(valid) triangle(mesh,vertices,clip,data.DisplaySize.y,limit);
            if(mesh.colors.size()>500000) throw std::runtime_error("ImGui geometry budget exceeded");
        }
    }
    return mesh;
}
}
