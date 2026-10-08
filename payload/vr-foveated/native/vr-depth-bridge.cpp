#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <reshade.hpp>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include "vr-depth-layout.h"
using Microsoft::WRL::ComPtr;
using namespace reshade::api;
static thread_local bool internal=false;
struct Candidate {ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC desc={};uint64_t score=0;unsigned mask=0;DepthRect rects[2]={};unsigned rect_count=0;};
struct DepthState {
    unsigned width=0,height=0,captured_mask=0;uint64_t best_score=0,selected=0;bool fresh=false,bound=false,reported=false;
    std::unordered_map<uint64_t,Candidate> candidates;
    std::unordered_set<uint64_t> announced;
    DepthLayout layout={},shader_layout={};
    ComPtr<ID3D11Texture2D> snapshot,packed;
    ComPtr<ID3D11ShaderResourceView> input,output;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
};
struct DepthDrawState { device *owner=nullptr;uint64_t current=0;unsigned current_mask=0;viewport viewports[16]={};unsigned viewport_count=0; };
static std::unordered_map<command_list*,DepthDrawState> draws;
static std::mutex mutex;
static std::unordered_map<device*,DepthState> states;
static DXGI_FORMAT Storage(DXGI_FORMAT f){
    switch(f){case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_TYPELESS:return DXGI_FORMAT_R16_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32G8X24_TYPELESS:return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:return DXGI_FORMAT_UNKNOWN;}
}
static DXGI_FORMAT View(DXGI_FORMAT f){
    switch(f){case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R16_TYPELESS:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R32G8X24_TYPELESS:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;default:return DXGI_FORMAT_UNKNOWN;}
}
static bool Snapshot(DepthState &s,Candidate &c,ID3D11DeviceContext *ctx){
    const auto identity=reinterpret_cast<uint64_t>(c.texture.Get());
    const auto layout=ResolveDepthLayout(c.desc,s.width,s.height,c.rects,c.rect_count);
    if(!layout.valid || !c.score || !c.mask || (identity!=s.selected && c.score<s.best_score) || ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    auto desc=c.desc;desc.Format=Storage(desc.Format);if(desc.Format==DXGI_FORMAT_UNKNOWN)return false;
    ComPtr<ID3D11Device> dev;ctx->GetDevice(&dev);
    D3D11_TEXTURE2D_DESC old={};if(s.snapshot)s.snapshot->GetDesc(&old);
    if(!s.snapshot || old.Width!=desc.Width || old.Height!=desc.Height || old.ArraySize!=desc.ArraySize || old.Format!=desc.Format || old.SampleDesc.Count!=desc.SampleDesc.Count){
        s.snapshot.Reset();s.input.Reset();s.ps.Reset();desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;desc.MiscFlags=0;
        if(FAILED(dev->CreateTexture2D(&desc,nullptr,&s.snapshot)))return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC v={};v.Format=View(desc.Format);
        if(desc.SampleDesc.Count>1){v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;v.Texture2DMSArray.ArraySize=desc.ArraySize;}
        else{v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray.ArraySize=desc.ArraySize;v.Texture2DArray.MipLevels=1;}
        if(FAILED(dev->CreateShaderResourceView(s.snapshot.Get(),&v,&s.input))){s.snapshot.Reset();return false;}
    }
    if(identity!=s.selected || s.layout.eye[0]!=layout.eye[0] || s.layout.eye[1]!=layout.eye[1]){s.selected=identity;s.captured_mask=0;s.fresh=false;}
    s.layout=layout;
    if(c.desc.ArraySize==2){
        for(unsigned eye=0;eye<2;++eye)if(c.mask&(1u<<eye))ctx->CopySubresourceRegion(s.snapshot.Get(),eye,0,0,0,c.texture.Get(),eye,nullptr);
    }else ctx->CopyResource(s.snapshot.Get(),c.texture.Get());
    s.best_score=(std::max)(s.best_score,c.score);s.captured_mask|=c.mask;s.fresh=s.captured_mask==3;return true;
}
static bool Pack(DepthState &s,ID3D11DeviceContext *ctx){
    if(!s.fresh || !s.input || ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    ComPtr<ID3D11Device> dev;ctx->GetDevice(&dev);D3D11_TEXTURE2D_DESC source={};s.snapshot->GetDesc(&source);
    D3D11_TEXTURE2D_DESC old={};if(s.packed)s.packed->GetDesc(&old);
    if(!s.packed || old.Width!=s.width || old.Height!=s.height){
        s.packed.Reset();s.output.Reset();s.target.Reset();D3D11_TEXTURE2D_DESC d={};d.Width=s.width;d.Height=s.height;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R32_FLOAT;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        if(FAILED(dev->CreateTexture2D(&d,nullptr,&s.packed)) || FAILED(dev->CreateShaderResourceView(s.packed.Get(),nullptr,&s.output)) || FAILED(dev->CreateRenderTargetView(s.packed.Get(),nullptr,&s.target)))return false;
    }
    if(s.layout.eye[0]!=s.shader_layout.eye[0] || s.layout.eye[1]!=s.shader_layout.eye[1]){s.ps.Reset();s.shader_layout=s.layout;}
    if(!s.vs || !s.ps){
        const char *vertex="float4 main(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}";
        char pixel[1200];const auto &l=s.layout.eye[0],&r=s.layout.eye[1];
        sprintf_s(pixel,"%s<float> depth:register(t0); float main(float4 p:SV_Position):SV_Target {uint x=(uint)p.x,y=(uint)p.y;bool right=x>=%u;uint sx=x%% %u+(right?%u:%u);uint sy=y+(right?%u:%u);uint layer=right?%u:%u;return depth.Load(%s);}",
            source.SampleDesc.Count>1?"Texture2DMSArray":"Texture2DArray",s.width/2,s.width/2,r.x,l.x,r.y,l.y,r.layer,l.layer,
            source.SampleDesc.Count>1?"int3(sx,sy,layer),0":"int4(sx,sy,layer,0)");
        std::string code=pixel;
        ComPtr<ID3DBlob> vb,pb,errors;
        if(FAILED(D3DCompile(vertex,strlen(vertex),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vb,&errors)) || FAILED(D3DCompile(code.data(),code.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&pb,&errors)))return false;
        if(FAILED(dev->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&s.vs)) || FAILED(dev->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&s.ps)))return false;
    }
    // D3D11.1 context-state swapping restores every touched application binding.
    ComPtr<ID3D11Device1> dev1;ComPtr<ID3D11DeviceContext1> ctx1;ComPtr<ID3DDeviceContextState> blank,previous;
    if(FAILED(dev.As(&dev1)) || FAILED(ctx->QueryInterface(IID_PPV_ARGS(&ctx1))))return false;
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL level;
    if(FAILED(dev1->CreateDeviceContextState(0,levels,2,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&level,&blank)))return false;
    internal=true;ctx1->SwapDeviceContextState(blank.Get(),&previous);
    auto rtv=s.target.Get();auto srv=s.input.Get();D3D11_VIEWPORT vp={0,0,float(s.width),float(s.height),0,1};
    ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->RSSetViewports(1,&vp);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vs.Get(),nullptr,0);ctx->PSSetShader(s.ps.Get(),nullptr,0);ctx->PSSetShaderResources(0,1,&srv);ctx->Draw(3,0);
    ctx1->SwapDeviceContextState(previous.Get(),nullptr);internal=false;return true;
}
static void Bind(command_list *cl,uint32_t count,const resource_view *,resource_view dsv){
    if(internal || cl->get_device()->get_api()!=device_api::d3d11)return;
    std::lock_guard guard(mutex);auto &s=states[cl->get_device()];auto &d=draws[cl];d.owner=cl->get_device();d.current=0;
    if(!count || !dsv.handle)return;
    ComPtr<ID3D11Resource> resource;reinterpret_cast<ID3D11DepthStencilView *>(dsv.handle)->GetResource(&resource);
    ComPtr<ID3D11Texture2D> tex;if(FAILED(resource.As(&tex)))return;const auto key=reinterpret_cast<uint64_t>(tex.Get());
    auto it=s.candidates.find(key);
    if(it==s.candidates.end()){
        if(s.candidates.size()>=64)return;
        Candidate c;c.texture=tex;c.texture->GetDesc(&c.desc);
        const uint64_t shape=uint64_t(c.desc.Width)|(uint64_t(c.desc.Height)<<16)|(uint64_t(c.desc.ArraySize)<<32)|(uint64_t(c.desc.SampleDesc.Count)<<40)|(uint64_t(c.desc.Format)<<48);
        if(s.announced.size()<64 && s.announced.insert(shape).second){char message[200];sprintf_s(message,"VR depth candidate: %ux%u layers=%u samples=%u format=%u",c.desc.Width,c.desc.Height,c.desc.ArraySize,c.desc.SampleDesc.Count,unsigned(c.desc.Format));reshade::log::message(reshade::log::level::info,message);}
        s.candidates.emplace(key,std::move(c));
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC view={};reinterpret_cast<ID3D11DepthStencilView *>(dsv.handle)->GetDesc(&view);
    unsigned first=0,layers=1;
    if(view.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DARRAY){first=view.Texture2DArray.FirstArraySlice;layers=view.Texture2DArray.ArraySize;}
    if(view.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY){first=view.Texture2DMSArray.FirstArraySlice;layers=view.Texture2DMSArray.ArraySize;}
    d.current=key;
    d.current_mask=s.candidates.at(key).desc.ArraySize==1?3u:0u;
    for(unsigned eye=0;eye<2;++eye)if(eye>=first && uint64_t(eye)<uint64_t(first)+layers)d.current_mask|=1u<<eye;
}
static void Viewports(command_list *cl,uint32_t first,uint32_t count,const viewport *values){
    if(internal||cl->get_device()->get_api()!=device_api::d3d11||first>=16||!values)return;
    std::lock_guard guard(mutex);auto &s=draws[cl];s.owner=cl->get_device();
    if(first==0)s.viewport_count=0;
    for(unsigned i=0;i<count&&first+i<16;++i)s.viewports[first+i]=values[i];
    s.viewport_count=(std::min)(16u,first+count);
}
static void ObserveEyeViewports(DepthState &s,const DepthDrawState &d,Candidate &c){
    if(!s.width||!s.height)return;
    for(unsigned i=0;i<d.viewport_count;++i){const auto &v=d.viewports[i];
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.width)||!std::isfinite(v.height)||v.x<0||v.y<0||v.x>c.desc.Width||v.y>c.desc.Height)continue;
        if(v.width!=float(s.width/2)||v.height!=float(s.height)||std::floor(v.x)!=v.x||std::floor(v.y)!=v.y)continue;
        DepthRect rect={unsigned(v.x),unsigned(v.y),unsigned(v.width),unsigned(v.height),c.desc.ArraySize==2?(d.current_mask==2?1u:0u):0u};
        if(!RectInside(rect,c.desc))continue;
        bool duplicate=false;for(unsigned j=0;j<c.rect_count;++j)duplicate|=c.rects[j]==rect;
        if(!duplicate&&c.rect_count<2)c.rects[c.rect_count++]=rect;
        if(c.rect_count==2&&((c.desc.ArraySize==1&&c.rects[0].x>c.rects[1].x)||(c.desc.ArraySize==2&&c.rects[0].layer>c.rects[1].layer)))std::swap(c.rects[0],c.rects[1]);
    }
}
static bool Draw(command_list *cl,uint32_t n,uint32_t instances,uint32_t,uint32_t){
    if(internal)return false;std::lock_guard guard(mutex);auto it=states.find(cl->get_device());if(it!=states.end()){
        auto d=draws.find(cl);if(d==draws.end())return false;
        auto c=it->second.candidates.find(d->second.current);if(c!=it->second.candidates.end()){c->second.score+=uint64_t(n)*instances+1;c->second.mask|=d->second.current_mask;ObserveEyeViewports(it->second,d->second,c->second);}
    }return false;
}
static bool Indexed(command_list *cl,uint32_t n,uint32_t instances,uint32_t first,int32_t,uint32_t start){return Draw(cl,n,instances,first,start);}
static bool Clear(command_list *cl,resource_view dsv,const float *depth,const uint8_t *,uint32_t,const rect *){
    if(internal || !depth || cl->get_device()->get_api()!=device_api::d3d11 || !dsv.handle)return false;
    std::lock_guard guard(mutex);auto it=states.find(cl->get_device());if(it==states.end())return false;
    ComPtr<ID3D11Resource> resource;reinterpret_cast<ID3D11DepthStencilView *>(dsv.handle)->GetResource(&resource);
    ComPtr<ID3D11Texture2D> tex;if(FAILED(resource.As(&tex)))return false;
    auto c=it->second.candidates.find(reinterpret_cast<uint64_t>(tex.Get()));if(c!=it->second.candidates.end()){
        Snapshot(it->second,c->second,reinterpret_cast<ID3D11DeviceContext *>(cl->get_native()));c->second.score=0;c->second.mask=0;
    }return false;
}
static bool StereoShape12(const resource_desc &desc,unsigned w,unsigned h){
    // Generic Depth already provides a shader-readable snapshot on D3D12.
    // Accept only an exact single-layer SBS attachment, never a desktop,
    // shadow-map, single-eye or unresolved multisample depth surface.
    const auto typed=format_to_depth_stencil_typed(desc.texture.format);
    const bool depth_format=typed==format::d16_unorm || typed==format::d32_float ||
        typed==format::d24_unorm_s8_uint || typed==format::d32_float_s8_uint;
    return depth_format && desc.type==resource_type::texture_2d && w>=2 && !(w&1) &&
        desc.texture.width==w && desc.texture.height==h &&
        desc.texture.depth_or_layers==1 && desc.texture.samples==1;
}
static void Begin12(effect_runtime *rt){
    unsigned w=0,h=0;rt->get_screenshot_width_and_height(&w,&h);
    effect_texture_variable variable={};
    for(const char *name:{"DepthBufferTex","V__ReShade__DepthBufferTex","ReShade::DepthBufferTex"}){
        variable=rt->find_texture_variable("DLSS5_Feed.fx",name);if(variable.handle)break;
    }
    resource_view view={},srgb={};if(variable.handle)rt->get_texture_binding(variable,&view,&srgb);
    auto dev=rt->get_device();resource source={};resource_desc desc={};
    if(view.handle){source=dev->get_resource_from_view(view);if(source.handle)desc=dev->get_resource_desc(source);}
    const bool ready=source.handle && StereoShape12(desc,w,h);
    auto uniform=rt->find_uniform_variable("DLSS5_Feed.fx","VR_STEREO_DEPTH_VALID");
    const float valid=ready?1.f:0.f;if(uniform.handle)rt->set_uniform_value_float(uniform,&valid,1);
    std::lock_guard guard(mutex);auto &s=states[dev];
    if(!s.reported || s.bound!=ready || s.width!=w || s.height!=h){
        s.reported=true;s.bound=ready;s.width=w;s.height=h;
        char message[300];sprintf_s(message,"VR D3D12 stereo depth: %s; bound=%ux%u layers=%u samples=%u format=%u headset=%ux%u; image correspondence calibrates per-eye inverse Z",
            ready?"matching SBS depth enables translation calibration":"no matching SBS depth; rotation-only fallback",
            desc.texture.width,desc.texture.height,desc.texture.depth_or_layers,desc.texture.samples,unsigned(desc.texture.format),w,h);
        reshade::log::message(reshade::log::level::info,message);
    }
}
static void Begin(effect_runtime *rt,command_list *cl,resource_view,resource_view){
    if(internal || rt->get_hwnd()!=0)return;
    if(rt->get_device()->get_api()==device_api::d3d12){Begin12(rt);return;}
    if(rt->get_device()->get_api()!=device_api::d3d11)return;
    std::lock_guard guard(mutex);auto &s=states[rt->get_device()];unsigned w,h;rt->get_screenshot_width_and_height(&w,&h);
    if(s.width!=w || s.height!=h){s.width=w;s.height=h;s.fresh=false;s.best_score=0;s.captured_mask=0;s.selected=0;}
    auto ctx=reinterpret_cast<ID3D11DeviceContext *>(cl->get_native());
    for(auto &[key,c]:s.candidates)Snapshot(s,c,ctx);
    const bool ready=Pack(s,ctx);
    const auto valid_uniform=rt->find_uniform_variable("DLSS5_Feed.fx","VR_STEREO_DEPTH_VALID");
    const float valid=ready?1.f:0.f;if(valid_uniform.handle)rt->set_uniform_value_float(valid_uniform,&valid,1);
    if(ready){rt->update_texture_bindings("DEPTH",{reinterpret_cast<uint64_t>(s.output.Get())},{reinterpret_cast<uint64_t>(s.output.Get())});}
    if(!s.reported || ready!=s.bound){s.reported=true;s.bound=ready;char message[300];sprintf_s(message,"VR stereo depth v5: %s; layout=%s; output=%ux%u",ready?"packed both eyes into current-frame DEPTH guide":"no matching fresh stereo capture; Generic Depth remains responsible",ready?s.layout.method:"unresolved",w,h);reshade::log::message(reshade::log::level::info,message);}
}
static void Finish(effect_runtime *rt,command_list *,resource_view,resource_view){
    std::lock_guard guard(mutex);auto it=states.find(rt->get_device());if(it==states.end())return;
    auto &s=it->second;if(rt->get_hwnd()!=0 && s.width!=0)return;
    s.best_score=0;s.fresh=false;s.captured_mask=0;s.selected=0;s.candidates.clear();
}
static void DestroyList(command_list *cl){std::lock_guard guard(mutex);draws.erase(cl);}
static void Destroy(device *dev){std::lock_guard guard(mutex);states.erase(dev);for(auto it=draws.begin();it!=draws.end();)if(it->second.owner==dev)it=draws.erase(it);else ++it;}
#ifndef VR_DEPTH_TEST
extern "C" __declspec(dllexport) const char *NAME="VR stereo depth bridge";
extern "C" __declspec(dllexport) const char *DESCRIPTION="Preserves matching D3D11 stereo depth and packs both eye layers for VR effects.";
extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE addon,HMODULE owner){
    if(!reshade::register_addon(addon,owner))return FALSE;
    reshade::register_event<reshade::addon_event::destroy_command_list>(DestroyList);
    reshade::register_event<reshade::addon_event::bind_viewports>(Viewports);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(Bind);
    reshade::register_event<reshade::addon_event::draw>(Draw);reshade::register_event<reshade::addon_event::draw_indexed>(Indexed);
    reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(Clear);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);reshade::register_event<reshade::addon_event::reshade_finish_effects>(Finish);
    reshade::register_event<reshade::addon_event::destroy_device>(Destroy);return TRUE;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon,HMODULE){
    reshade::unregister_event<reshade::addon_event::destroy_command_list>(DestroyList);
    reshade::unregister_event<reshade::addon_event::bind_viewports>(Viewports);
    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(Bind);
    reshade::unregister_event<reshade::addon_event::draw>(Draw);reshade::unregister_event<reshade::addon_event::draw_indexed>(Indexed);
    reshade::unregister_event<reshade::addon_event::clear_depth_stencil_view>(Clear);
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(Finish);
    reshade::unregister_event<reshade::addon_event::destroy_device>(Destroy);std::lock_guard guard(mutex);states.clear();draws.clear();reshade::unregister_addon(addon);
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
#endif
