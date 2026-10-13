#define XR_NO_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "../payload/vr-foveated/native/openxr/openxr_loader_negotiation.h"
static XrInstance instance=(XrInstance)0x10;static XrSession session=(XrSession)0x20;static unsigned forwarded=0,destroy_calls=0;static bool event_ready=false;
static const XrCompositionLayerBaseHeader *projection=nullptr;
static XrResult XRAPI_CALL Create(const XrInstanceCreateInfo *,const XrApiLayerCreateInfo *,XrInstance *out){*out=instance;return XR_SUCCESS;}
static XrResult XRAPI_CALL CreateSession(XrInstance,const XrSessionCreateInfo *,XrSession *out){*out=session;return XR_SUCCESS;}
static XrResult XRAPI_CALL DestroySession(XrSession){++destroy_calls;return XR_SUCCESS;}
static XrResult XRAPI_CALL DestroyInstance(XrInstance){return XR_SUCCESS;}
static XrResult XRAPI_CALL End(XrSession,const XrFrameEndInfo *info){assert(info->layerCount==2&&info->layers[0]==projection&&info->layers[1]->type==XR_TYPE_COMPOSITION_LAYER_QUAD);++forwarded;return XR_SUCCESS;}
static XrResult XRAPI_CALL Poll(XrInstance,XrEventDataBuffer *out){if(!event_ready)return XR_EVENT_UNAVAILABLE;auto e=(XrEventDataSessionStateChanged *)out;e->type=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;e->session=session;e->state=XR_SESSION_STATE_FOCUSED;event_ready=false;return XR_SUCCESS;}
static XrResult XRAPI_CALL Gipa(XrInstance,const char *name,PFN_xrVoidFunction *out){
#define PROC(n,f) if(!strcmp(name,n)){*out=(PFN_xrVoidFunction)f;return XR_SUCCESS;}
 PROC("xrGetInstanceProcAddr",Gipa) PROC("xrCreateSession",CreateSession) PROC("xrDestroySession",DestroySession) PROC("xrDestroyInstance",DestroyInstance) PROC("xrEndFrame",End) PROC("xrPollEvent",Poll)
 *out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
}
template<class T>T Get(PFN_xrGetInstanceProcAddr get,const char *name){PFN_xrVoidFunction out=nullptr;assert(get(instance,name,&out)==XR_SUCCESS&&out);return (T)out;}
int main(){
 auto capture=LoadLibraryW(L"DLSS5OpenXRPose.dll");assert(capture);auto negotiate=(PFN_xrNegotiateLoaderApiLayerInterface)GetProcAddress(capture,"xrNegotiateLoaderApiLayerInterface");assert(negotiate);
 XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,1,sizeof(loader),1,1,XR_MAKE_VERSION(1,0,0),XR_MAKE_VERSION(1,1,0)};XrNegotiateApiLayerRequest request{XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST,1,sizeof(request)};assert(negotiate(&loader,"XR_APILAYER_DLSS5_pose",&request)==XR_SUCCESS);
 XrApiLayerNextInfo next{XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO,1,sizeof(next)};next.nextGetInstanceProcAddr=Gipa;next.nextCreateApiLayerInstance=Create;XrApiLayerCreateInfo layer{XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO,1,sizeof(layer)};layer.nextInfo=&next;XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};XrInstance result{};assert(request.createApiLayerInstance(&ci,&layer,&result)==XR_SUCCESS);
 auto create=Get<PFN_xrCreateSession>(request.getInstanceProcAddr,"xrCreateSession");auto end=Get<PFN_xrEndFrame>(request.getInstanceProcAddr,"xrEndFrame");auto destroy=Get<PFN_xrDestroySession>(request.getInstanceProcAddr,"xrDestroySession");auto poll=Get<PFN_xrPollEvent>(request.getInstanceProcAddr,"xrPollEvent");
 auto probe=GetModuleHandleW(L"dlss5-vr-compat.addon64");assert(probe);auto read=(void (__cdecl *)(unsigned *,unsigned *,unsigned *,unsigned *,unsigned *,unsigned *))GetProcAddress(probe,"ReadMenuProbe");assert(read);
 for(auto type:{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR,XR_TYPE_GRAPHICS_BINDING_D3D12_KHR,XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR,XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR}){
  XrBaseInStructure binding{type,nullptr},prefix{XR_TYPE_UNKNOWN,&binding};XrSessionCreateInfo si{XR_TYPE_SESSION_CREATE_INFO};si.next=&prefix;XrSession out{};assert(create(instance,&si,&out)==XR_SUCCESS&&out==session);
  XrCompositionLayerProjectionView view[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};for(auto &v:view){v.pose.orientation.w=1;v.fov={-.8f,.8f,.8f,-.8f};}
  XrCompositionLayerProjection p{XR_TYPE_COMPOSITION_LAYER_PROJECTION};p.space=(XrSpace)0x30;p.viewCount=2;p.views=view;projection=(const XrCompositionLayerBaseHeader *)&p;XrFrameEndInfo frame{XR_TYPE_FRAME_END_INFO};frame.displayTime=1000000000;frame.layerCount=1;frame.layers=&projection;
  event_ready=true;XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};assert(poll(instance,&event)==XR_SUCCESS);assert(end(session,&frame)==XR_SUCCESS);
  unsigned a,f,d,h,focus,b;read(&a,&f,&d,&h,&focus,&b);assert(b==(unsigned)type&&f==forwarded&&focus==forwarded&&h>=4);assert(destroy(session)==XR_SUCCESS);read(&a,&f,&d,&h,&focus,&b);assert(d==destroy_calls);
 }
 Get<PFN_xrDestroyInstance>(request.getInstanceProcAddr,"xrDestroyInstance")(instance);
 puts("PASS: actual capture DLL loads the game-local menu, dispatches all four graphics bindings through structure chains, forwards focus/input hooks, adds exactly one quad and retires sessions");
}

