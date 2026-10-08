#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <reshade.hpp>
#include <mutex>
#include <unordered_map>
#include <cstdio>
#include "projection-depth.h"
#include "vr-depth-layout.h"
using Microsoft::WRL::ComPtr;
using namespace reshade::api;
struct Copy {ComPtr<ID3D11Buffer> source,stage;};
struct ProjectionState {unsigned width=0,height=0;ULONGLONG last_capture=0,checked=0;std::vector<Copy> pending;std::vector<DepthProjection> projections;unsigned reported=~0u;};
static std::mutex mutex;
static std::unordered_map<device *,ProjectionState> states;
static thread_local bool internal=false;
static bool CaptureBuffer(ID3D11DeviceContext *ctx,ID3D11Buffer *buffer,Copy &copy){
 D3D11_BUFFER_DESC d{};buffer->GetDesc(&d);if(d.ByteWidth<64||d.ByteWidth>65536||!(d.BindFlags&D3D11_BIND_CONSTANT_BUFFER))return false;
 ComPtr<ID3D11Device> dev;ctx->GetDevice(&dev);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;d.StructureByteStride=0;
 if(FAILED(dev->CreateBuffer(&d,nullptr,&copy.stage)))return false;copy.source=buffer;ctx->CopyResource(copy.stage.Get(),buffer);return true;
}
static bool Draw(command_list *cl,uint32_t,uint32_t,uint32_t,uint32_t){
 if(internal||cl->get_device()->get_api()!=device_api::d3d11)return false;
 std::lock_guard guard(mutex);auto it=states.find(cl->get_device());if(it==states.end())return false;auto &s=it->second;
 if(!s.width||!s.height||!s.pending.empty()||GetTickCount64()-s.last_capture<500)return false;
 auto ctx=reinterpret_cast<ID3D11DeviceContext *>(cl->get_native());if(ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
 ComPtr<ID3D11DepthStencilView> dsv;ctx->OMGetRenderTargets(0,nullptr,&dsv);if(!dsv)return false;
 ComPtr<ID3D11Resource> r;ComPtr<ID3D11Texture2D> t;dsv->GetResource(&r);if(FAILED(r.As(&t)))return false;
 D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);if(!StereoShape(d,s.width,s.height))return false;
 s.last_capture=GetTickCount64();ID3D11Buffer *buffers[28]{};ctx->VSGetConstantBuffers(0,14,buffers);ctx->PSGetConstantBuffers(0,14,buffers+14);
 internal=true;
 for(unsigned i=0;i<28;++i){if(!buffers[i])continue;bool duplicate=false;for(auto &copy:s.pending)duplicate|=copy.source.Get()==buffers[i];
  if(!duplicate){Copy copy;if(CaptureBuffer(ctx,buffers[i],copy))s.pending.push_back(std::move(copy));}buffers[i]->Release();}
 internal=false;return false;
}
static bool Indexed(command_list *cl,uint32_t n,uint32_t instances,uint32_t first,int32_t,uint32_t start){return Draw(cl,n,instances,first,start);}
static unsigned Resolve(const std::vector<DepthProjection> &candidates,const float fov[2][4],float result[2][4]){
 unsigned mask=0;
 for(unsigned eye=0;eye<2;++eye){bool found=false,ambiguous=false;DepthProjection chosen{};
  for(const auto &p:candidates)if(MatchesProjection(p,fov[eye])){
   if(!found){chosen=p;found=true;}else if(std::abs(p.scale-chosen.scale)>.001f*std::fmax(1.f,std::abs(chosen.scale))||std::abs(p.bias-chosen.bias)>.001f*std::fmax(1.f,std::abs(chosen.bias)))ambiguous=true;
  }
  if(found&&!ambiguous){result[eye][0]=chosen.scale;result[eye][1]=chosen.bias;result[eye][3]=1;mask|=1u<<eye;}
 }return mask;
}
static void Begin(effect_runtime *rt,command_list *cl,resource_view,resource_view){
 if(internal||rt->get_hwnd()!=0||rt->get_device()->get_api()!=device_api::d3d11)return;
 std::lock_guard guard(mutex);auto &s=states[rt->get_device()];unsigned w,h;rt->get_screenshot_width_and_height(&w,&h);
 if(s.width!=w||s.height!=h){s={};s.width=w;s.height=h;}
 auto ctx=reinterpret_cast<ID3D11DeviceContext *>(cl->get_native());std::vector<DepthProjection> candidates;bool ready=!s.pending.empty();
 for(auto &copy:s.pending){D3D11_MAPPED_SUBRESOURCE m{};auto hr=ctx->Map(copy.stage.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m);
  if(FAILED(hr)){ready=false;break;}D3D11_BUFFER_DESC d{};copy.stage->GetDesc(&d);auto found=FindDepthProjections(m.pData,d.ByteWidth);candidates.insert(candidates.end(),found.begin(),found.end());ctx->Unmap(copy.stage.Get(),0);}
 if(ready){s.projections=std::move(candidates);s.pending.clear();s.checked=GetTickCount64();}
 float fov[2][4]={{-1,1,-1,1},{-1,1,-1,1}},result[2][4]{};
 for(unsigned eye=0;eye<2;++eye){auto u=rt->find_uniform_variable("DLSS5_Feed.fx",eye?"VR_POSE_PROJ_R":"VR_POSE_PROJ_L");if(u.handle)rt->get_uniform_value_float(u,fov[eye],4);}
 unsigned mask=GetTickCount64()-s.checked<2000?Resolve(s.projections,fov,result):0;
 for(unsigned eye=0;eye<2;++eye){auto u=rt->find_uniform_variable("DLSS5_Feed.fx",eye?"VR_DEPTH_TO_INVZ_R":"VR_DEPTH_TO_INVZ_L");if(u.handle)rt->set_uniform_value_float(u,result[eye],4);}
 if(mask!=s.reported){s.reported=mask;char message[250];sprintf_s(message,"VR engine projection depth: mask=%u; inverseZ L=(%.8g*depth%+.8g) R=(%.8g*depth%+.8g); matching bound stereo camera constants, nonblocking readback",mask,result[0][0],result[0][1],result[1][0],result[1][1]);reshade::log::message(reshade::log::level::info,message);}
}
static void Destroy(device *dev){std::lock_guard guard(mutex);states.erase(dev);}
#ifndef VR_PROJECTION_TEST
extern "C" __declspec(dllexport) const char *NAME="VR engine projection bridge";
extern "C" __declspec(dllexport) const char *DESCRIPTION="Obtains validated stereo perspective depth conversion from bound D3D11 camera constants.";
extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE addon,HMODULE owner){if(!reshade::register_addon(addon,owner))return FALSE;reshade::register_event<reshade::addon_event::draw>(Draw);reshade::register_event<reshade::addon_event::draw_indexed>(Indexed);reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);reshade::register_event<reshade::addon_event::destroy_device>(Destroy);return TRUE;}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon,HMODULE){reshade::unregister_event<reshade::addon_event::draw>(Draw);reshade::unregister_event<reshade::addon_event::draw_indexed>(Indexed);reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);reshade::unregister_event<reshade::addon_event::destroy_device>(Destroy);std::lock_guard guard(mutex);states.clear();reshade::unregister_addon(addon);}
BOOL WINAPI DllMain(HMODULE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);return TRUE;}
#endif
