#pragma once
#include "unity_bridge.h"
#include "link_imgui_mesh.h"
#include "nlohmann/json.hpp"

namespace eflink {
// Game-thread renderer. CanvasRenderer has no Graphic / raycast target and creates
// no HWND. CPU clipping preserves ImGui's per-command scissor rectangles on Vulkan.
class NativeGuiCanvas {
    unity::Api api;
    struct Batch {void* object=nullptr;void* renderer=nullptr;void* mesh=nullptr;};
    void* root=nullptr;void* material=nullptr;void* texture=nullptr;
    std::vector<Batch> batches;
    unsigned atlasWidth=0,atlasHeight=0;std::size_t lastVertices=0,lastChunks=0;
    bool readbackVerified=false;
    // Standalone assets (mesh, material, font texture) are not kept alive by
    // DontDestroyOnLoad on the root. A scene change runs UnloadUnusedAssets and
    // destroyed them, e.g. when the module loaded at the login screen.
    void protect(void* object) {
        int flags=32; // HideFlags.DontUnloadUnusedAsset
        api.call(api.method("Object","set_hideFlags","UnityEngine.HideFlags"),object,{&flags});
    }
    bool alive(void* object) {return object&&api.value<bool>(api.method("Object","op_Implicit","UnityEngine.Object","System.Boolean"),nullptr,{object});}
    Batch createBatch() {
        Batch batch;batch.object=api.gameObject("EF Link / ImGui mesh "+std::to_string(batches.size()));auto* rect=api.add(batch.object,"RectTransform");bool stays=false;
        api.call(api.method("Transform","SetParent","UnityEngine.Transform|System.Boolean"),rect,{api.transform(root),&stays});
        unity::V2 zero{0,0},one{1,1};
        for(const char* name:{"set_anchorMin","set_anchorMax","set_pivot","set_anchoredPosition"}) api.call(api.method("RectTransform",name,"UnityEngine.Vector2"),rect,{&zero});
        api.call(api.method("RectTransform","set_sizeDelta","UnityEngine.Vector2"),rect,{&one});
        batch.renderer=api.add(batch.object,"CanvasRenderer","UnityEngine.UIModule.dll");int count=1;bool cull=false;unity::Color white{1,1,1,1};
        api.call(api.method("CanvasRenderer","set_materialCount","System.Int32","System.Void","UnityEngine.UIModule.dll"),batch.renderer,{&count});
        api.call(api.method("CanvasRenderer","set_cull","System.Boolean","System.Void","UnityEngine.UIModule.dll"),batch.renderer,{&cull});
        api.call(api.method("CanvasRenderer","SetColor","UnityEngine.Color","System.Void","UnityEngine.UIModule.dll"),batch.renderer,{&white});
        api.call(api.method("CanvasRenderer","SetMaterial","UnityEngine.Material|UnityEngine.Texture","System.Void","UnityEngine.UIModule.dll"),batch.renderer,{material,texture});
        batch.mesh=api.object("Mesh");api.call(api.method("Mesh",".ctor"),batch.mesh);api.call(api.method("Mesh","MarkDynamic"),batch.mesh);protect(batch.mesh);
        int format=0; // IndexFormat.UInt16; UInt32 is incompatible with uGUI batching.
        api.call(api.method("Mesh","set_indexFormat","UnityEngine.Rendering.IndexFormat"),batch.mesh,{&format});
        return batch;
    }
    void verifyArray(void* mesh,const char* getter,const char* returns,const void* expected,std::size_t count,std::size_t stride) {
        auto* array=api.call(api.method("Mesh",getter,"",returns),mesh);unity::Api::TemporaryRoot rootHandle(api.raw(),array);
        const auto length=reinterpret_cast<std::uintptr_t(*)(void*)>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_array_length"));
        if(!length||length(array)!=count||std::memcmp(static_cast<unsigned char*>(array)+32,expected,count*stride)) throw std::runtime_error(std::string("Native ImGui mesh readback mismatch: ")+getter);
    }
public:
    void bind(const BE_HostApiV1* host) {api.bind(host);}
    void init(ImFontAtlas* fonts) {
        if(root) return;
        auto* shader=api.call(api.method("Shader","Find","System.String","UnityEngine.Shader"),nullptr,{api.string("UI/Default")});
        if(!shader) throw std::runtime_error("Native ImGui: UI/Default shader unavailable");
        material=api.object("Material");api.call(api.method("Material",".ctor","UnityEngine.Shader"),material,{shader});protect(material);
        unsigned char* pixels=nullptr;int w=0,h=0;fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
        atlasWidth=unsigned(w);atlasHeight=unsigned(h);
        texture=api.texture(atlasWidth,atlasHeight,pixels);protect(texture);fonts->SetTexID(ImTextureID(reinterpret_cast<std::uintptr_t>(texture)));
        root=api.gameObject("EF Link / in-game ImGui");
        auto* canvas=api.add(root,"Canvas","UnityEngine.UIModule.dll");int mode=0,order=32760;
        api.call(api.method("Canvas","set_renderMode","UnityEngine.RenderMode","System.Void","UnityEngine.UIModule.dll"),canvas,{&mode});
        api.call(api.method("Canvas","set_sortingOrder","System.Int32","System.Void","UnityEngine.UIModule.dll"),canvas,{&order});
        api.call(api.method("Object","DontDestroyOnLoad","UnityEngine.Object"),nullptr,{root});
        api.setActive(root,false);
    }
    void hide() {if(root) api.setActive(root,false);}
    // True when every Unity object this renderer owns still exists.
    bool valid() {
        if(!alive(root)||!alive(material)||!alive(texture)) return false;
        for(const auto& batch:batches) if(!alive(batch.object)||!alive(batch.renderer)||!alive(batch.mesh)) return false;
        return true;
    }
    // Recreate everything after the game destroyed part of it; only objects that
    // still exist are destroyed, references to dead ones are simply dropped.
    void rebuild(ImFontAtlas* fonts) {
        if(alive(root)) api.destroy(root);
        for(const auto& batch:batches) if(alive(batch.mesh)) api.destroy(batch.mesh);
        if(alive(material)) api.destroy(material);
        if(alive(texture)) api.destroy(texture);
        for(auto handle:api.roots) api.raw()->gchandle_free(api.raw()->context,handle);api.roots.clear();
        batches.clear();root=material=texture=nullptr;readbackVerified=false;++rebuilds;
        init(fonts);
    }
    unsigned rebuilds=0;
    void render(const ImDrawData* data,bool visible=true) {
        if(!root||!data) return;
        const auto geometry=imguimesh::convert(*data,ImTextureID(reinterpret_cast<std::uintptr_t>(texture)));
        lastVertices=geometry.colors.size();const auto parts=imguimesh::chunks(lastVertices);lastChunks=parts.size();
        if(!lastVertices) {hide();return;}
        for(std::size_t i=0;i<parts.size();++i) {
        if(i==batches.size()) batches.push_back(createBatch());auto& batch=batches[i];auto* mesh=batch.mesh;
        const auto [begin,count]=parts[i];std::vector<int> indices(count);for(std::size_t j=0;j<count;++j) indices[j]=int(j);
        api.call(api.method("Mesh","Clear"),mesh);
        auto* p=api.array("Vector3",geometry.positions.data()+begin*3,count,12);unity::Api::TemporaryRoot pr(api.raw(),p);
        auto* u=api.array("Vector2",geometry.uv.data()+begin*2,count,8);unity::Api::TemporaryRoot ur(api.raw(),u);
        auto* c=api.array("Color32",geometry.colors.data()+begin,count,4);unity::Api::TemporaryRoot cr(api.raw(),c);
        auto* t=api.array("Int32",indices.data(),count,4,"mscorlib.dll","System");unity::Api::TemporaryRoot tr(api.raw(),t);
        api.call(api.method("Mesh","set_vertices","UnityEngine.Vector3[]"),mesh,{p});
        api.call(api.method("Mesh","set_uv","UnityEngine.Vector2[]"),mesh,{u});
        api.call(api.method("Mesh","set_colors32","UnityEngine.Color32[]"),mesh,{c});
        api.call(api.method("Mesh","set_triangles","System.Int32[]"),mesh,{t});
        api.call(api.method("Mesh","RecalculateBounds"),mesh);
        if(api.value<int>(api.method("Mesh","get_indexFormat","","UnityEngine.Rendering.IndexFormat"),mesh)!=0) throw std::runtime_error("Native ImGui: Canvas mesh must have 16-bit indices");
        if(!readbackVerified) {
            verifyArray(mesh,"get_vertices","UnityEngine.Vector3[]",geometry.positions.data()+begin*3,count,12);
            verifyArray(mesh,"get_uv","UnityEngine.Vector2[]",geometry.uv.data()+begin*2,count,8);
            verifyArray(mesh,"get_colors32","UnityEngine.Color32[]",geometry.colors.data()+begin,count,4);
            verifyArray(mesh,"get_triangles","System.Int32[]",indices.data(),count,4);
        }
        api.call(api.method("CanvasRenderer","SetMesh","UnityEngine.Mesh","System.Void","UnityEngine.UIModule.dll"),batch.renderer,{mesh});
        api.setActive(batch.object,true);
        }
        for(std::size_t i=parts.size();i<batches.size();++i) api.setActive(batches[i].object,false);
        readbackVerified=true;api.setActive(root,visible);
    }
    nlohmann::json diagnostics() const {return {{"index_bits",16},{"vertices",lastVertices},{"chunks",lastChunks},{"mesh_readback_verified",readbackVerified},{"atlas_width",atlasWidth},{"atlas_height",atlasHeight},{"rebuilds",rebuilds}};}
    void shutdown() {
        // Objects the game already destroyed are skipped, never touched.
        try {if(alive(root)) {hide();api.destroy(root);}} catch(...) {}
        for(const auto& batch:batches) try {if(alive(batch.mesh)) api.destroy(batch.mesh);} catch(...) {}
        try {if(alive(material)) api.destroy(material);} catch(...) {}
        try {if(alive(texture)) api.destroy(texture);} catch(...) {}
        for(auto handle:api.roots) api.raw()->gchandle_free(api.raw()->context,handle);api.roots.clear();
        batches.clear();root=material=texture=nullptr;readbackVerified=false;
    }
};
}
