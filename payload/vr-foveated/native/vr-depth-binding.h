#pragma once
// Restore only the binding this bridge installed. Never replace a newer
// binding supplied by Generic Depth or another addon, or touch another runtime.
struct DepthBindingScope {
    void *owner=nullptr;
    reshade::api::effect_texture_variable variable{};
    reshade::api::resource_view previous{},previous_srgb{},installed{};
};
template<class Runtime> void RestoreDepthBinding(DepthBindingScope &s,Runtime *rt) {
    if(s.owner!=rt)return;
    if(s.variable.handle && s.installed.handle){
        reshade::api::resource_view current{},srgb{};rt->get_texture_binding(s.variable,&current,&srgb);
        if(current.handle==s.installed.handle && srgb.handle==s.installed.handle)
            rt->update_texture_bindings("DEPTH",s.previous,s.previous_srgb);
    }
    s={};
}
template<class Runtime> bool BindScopedDepth(DepthBindingScope &s,Runtime *rt,reshade::api::resource_view view) {
    RestoreDepthBinding(s,rt);s={};s.owner=rt;
    for(const char *name:{"DepthBufferTex","V__ReShade__DepthBufferTex","ReShade::DepthBufferTex"}){
        s.variable=rt->find_texture_variable("DLSS5_Feed.fx",name);if(s.variable.handle)break;
    }
    // Without the original semantic binding, ownership cannot be restored safely.
    if(!s.variable.handle){s={};return false;}
    rt->get_texture_binding(s.variable,&s.previous,&s.previous_srgb);
    s.installed=view;rt->update_texture_bindings("DEPTH",view,view);return true;
}
