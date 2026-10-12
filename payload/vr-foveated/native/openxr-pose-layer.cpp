#define WIN32_LEAN_AND_MEAN
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <cstring>
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <map>
#include <deque>
#include <algorithm>
#define XR_USE_GRAPHICS_API_D3D11
#include "openxr/openxr_platform.h"
#include "submitted-depth.h"
#include "openxr/openxr_loader_negotiation.h"
#include "pose-data.h"
// Capture only; never load ReShade or create a second OpenXR instance/session.
struct Instance {
    XrInstance handle={}; PFN_xrGetInstanceProcAddr gipa={};
    PFN_xrDestroyInstance destroy={}; PFN_xrCreateSession create_session={};
    PFN_xrDestroySession destroy_session={}; PFN_xrLocateViews locate={};
    PFN_xrEndFrame end_frame={}; PFN_xrPollEvent poll={};
};
struct Session {XrSession handle={};Instance dispatch={};PoseFrame frame={};DepthSubmissionInfo depth={};uint64_t epoch=1;bool d3d11=false;};
static SRWLOCK lock=SRWLOCK_INIT;
static Instance instances[16];static Session sessions[32];
struct DepthSwapchain {XrSession session{};Instance dispatch{};std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> images;std::deque<std::pair<unsigned,bool>> acquired;unsigned released=~0u;};
static std::map<XrSwapchain,DepthSwapchain> depth_swapchains;
static uint64_t epoch_counter=1;
static PFN_xrGetInstanceProcAddr null_gipa=nullptr;
static Instance Find(XrInstance h){Instance r={};AcquireSRWLockShared(&lock);for(auto &i:instances)if(i.handle==h){r=i;break;}ReleaseSRWLockShared(&lock);return r;}
static Instance ForSession(XrSession h){Instance r={};AcquireSRWLockShared(&lock);for(auto &s:sessions)if(s.handle==h){r=s.dispatch;break;}ReleaseSRWLockShared(&lock);return r;}
static void Invalidate(XrSession h){AcquireSRWLockExclusive(&lock);for(auto &s:sessions)if(s.handle==h){s.frame.valid=0;s.depth={};s.epoch=++epoch_counter;}ReleaseSRWLockExclusive(&lock);}
static PoseEye Eye(const XrPosef &p,const XrFovf &f){
    PoseEye e={{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},
        {p.position.x,p.position.y,p.position.z},
        {std::tan(f.angleLeft),std::tan(f.angleRight),std::tan(f.angleDown),std::tan(f.angleUp)}};return e;
}
static void Publish(XrSession session,XrSpace space,XrTime time,const PoseEye *eyes){
    PoseFrame f;f.valid=ValidEye(eyes[0])&&ValidEye(eyes[1]);f.session=(uint64_t)session;f.space=(uint64_t)space;
    f.tick=GetTickCount64();f.display_time=time;f.eye[0]=eyes[0];f.eye[1]=eyes[1];
    AcquireSRWLockExclusive(&lock);for(auto &s:sessions)if(s.handle==session){f.epoch=s.epoch;s.frame=f;break;}ReleaseSRWLockExclusive(&lock);
}
extern "C" __declspec(dllexport) bool __cdecl DLSS5ReadOpenXRPose(PoseFrame *out,uint32_t size){
    if(!out || size!=sizeof(PoseFrame))return false;
    PoseFrame f;AcquireSRWLockShared(&lock);for(auto &s:sessions)if(s.handle && s.frame.tick>=f.tick)f=s.frame;ReleaseSRWLockShared(&lock);
    if(!f.valid || GetTickCount64()-f.tick>250)return false;*out=f;return true;
}
extern "C" __declspec(dllexport) bool __cdecl DLSS5OpenXRPoseActive(){
    bool active=false;AcquireSRWLockShared(&lock);for(auto &s:sessions)active|=s.handle!=XR_NULL_HANDLE;ReleaseSRWLockShared(&lock);return active;
}
extern "C" __declspec(dllexport) bool __cdecl DLSS5ReadOpenXRDepthSubmission(DepthSubmissionInfo *out,uint32_t size){
    if(!out || size!=sizeof(*out))return false;
    DepthSubmissionInfo f;AcquireSRWLockShared(&lock);for(auto &s:sessions)if(s.handle && s.depth.tick>=f.tick)f=s.depth;ReleaseSRWLockShared(&lock);
    if(!f.session || GetTickCount64()-f.tick>250)return false;*out=f;return true;
}
static void PublishDepth(XrSession h,XrTime time,const XrCompositionLayerProjection *p){
    DepthSubmissionInfo f;f.session=(uint64_t)h;f.display_time=time;f.tick=GetTickCount64();
    for(unsigned eye=0;eye<2;++eye){
        auto next=reinterpret_cast<const XrBaseInStructure *>(p->views[eye].next);
        for(unsigned n=0;next && n<32;++n,next=next->next){
            if(next->type!=XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)continue;
            const auto d=reinterpret_cast<const XrCompositionLayerDepthInfoKHR *>(next);
            auto &e=f.eye[eye];e.swapchain=(uint64_t)d->subImage.swapchain;e.array_index=d->subImage.imageArrayIndex;
            e.x=d->subImage.imageRect.offset.x;e.y=d->subImage.imageRect.offset.y;
            e.width=d->subImage.imageRect.extent.width;e.height=d->subImage.imageRect.extent.height;
            e.min_depth=d->minDepth;e.max_depth=d->maxDepth;e.near_z=d->nearZ;e.far_z=d->farZ;
            f.present_mask|=1u<<eye;break;
        }
    }
    AcquireSRWLockExclusive(&lock);for(auto &s:sessions)if(s.handle==h){s.depth=f;break;}ReleaseSRWLockExclusive(&lock);
}
static XrResult XRAPI_CALL DestroyInstance(XrInstance h){
    auto d=Find(h);if(!d.destroy)return XR_ERROR_HANDLE_INVALID;auto result=d.destroy(h);
    if(XR_SUCCEEDED(result)){AcquireSRWLockExclusive(&lock);for(auto i=depth_swapchains.begin();i!=depth_swapchains.end();)if(i->second.dispatch.handle==h)i=depth_swapchains.erase(i);else ++i;for(auto &s:sessions)if(s.dispatch.handle==h)s={};for(auto &i:instances)if(i.handle==h)i={};ReleaseSRWLockExclusive(&lock);}return result;
}
static XrResult XRAPI_CALL CreateSession(XrInstance h,const XrSessionCreateInfo *info,XrSession *out){
    auto d=Find(h);if(!d.create_session)return XR_ERROR_HANDLE_INVALID;auto r=d.create_session(h,info,out);
    if(XR_SUCCEEDED(r)&&out){bool stored=false;AcquireSRWLockExclusive(&lock);for(auto &s:sessions)if(!s.handle){s.handle=*out;s.dispatch=d;s.epoch=++epoch_counter;
        auto chain=info?reinterpret_cast<const XrBaseInStructure *>(info->next):nullptr;
        for(unsigned n=0;chain&&n<32;++n,chain=chain->next)if(chain->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)s.d3d11=true;stored=true;break;}ReleaseSRWLockExclusive(&lock);
        if(!stored){if(d.destroy_session)d.destroy_session(*out);*out=XR_NULL_HANDLE;return XR_ERROR_LIMIT_REACHED;}}
    return r;
}
static XrResult XRAPI_CALL DestroySession(XrSession h){
    auto d=ForSession(h);if(!d.destroy_session)return XR_ERROR_HANDLE_INVALID;auto r=d.destroy_session(h);
    if(XR_SUCCEEDED(r)){AcquireSRWLockExclusive(&lock);for(auto i=depth_swapchains.begin();i!=depth_swapchains.end();)if(i->second.session==h)i=depth_swapchains.erase(i);else ++i;for(auto &s:sessions)if(s.handle==h)s={};ReleaseSRWLockExclusive(&lock);}return r;
}
template<class Fn> static Fn DepthProc(const Instance &d,const char *name){Fn fn=nullptr;if(d.gipa)d.gipa(d.handle,name,reinterpret_cast<PFN_xrVoidFunction *>(&fn));return fn;}
static XrResult XRAPI_CALL CreateDepthSwapchain(XrSession h,const XrSwapchainCreateInfo *info,XrSwapchain *out){
 auto d=ForSession(h);auto fn=DepthProc<PFN_xrCreateSwapchain>(d,"xrCreateSwapchain");if(!fn)return XR_ERROR_HANDLE_INVALID;
 auto result=fn(h,info,out);if(XR_FAILED(result)||!out||!info)return result;
 DepthSwapchain initial;initial.session=h;initial.dispatch=d;
 AcquireSRWLockExclusive(&lock);if(depth_swapchains.size()<128)depth_swapchains[*out]=initial;ReleaseSRWLockExclusive(&lock);
 // The composition depth reference authorizes selection; usage flags alone
 // do not identify depth copied into a shader-writable swapchain.
 bool supported=false;AcquireSRWLockShared(&lock);for(const auto &s:sessions)if(s.handle==h)supported=s.d3d11;ReleaseSRWLockShared(&lock);
 if(!supported)return result;
 auto enumerate=DepthProc<PFN_xrEnumerateSwapchainImages>(d,"xrEnumerateSwapchainImages");unsigned count=0;
 if(!enumerate||XR_FAILED(enumerate(*out,0,&count,nullptr))||!count||count>32)return result;
 std::vector<XrSwapchainImageD3D11KHR> images(count);for(auto &i:images)i.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
 if(XR_FAILED(enumerate(*out,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data()))))return result;
 DepthSwapchain state;state.session=h;state.dispatch=d;for(const auto &i:images){if(!i.texture)return result;state.images.emplace_back(i.texture);}
 AcquireSRWLockExclusive(&lock);if(depth_swapchains.size()<128)depth_swapchains[*out]=std::move(state);ReleaseSRWLockExclusive(&lock);return result;
}
static Instance DepthDispatch(XrSwapchain h){Instance d{};AcquireSRWLockShared(&lock);auto i=depth_swapchains.find(h);if(i!=depth_swapchains.end())d=i->second.dispatch;else for(const auto &s:sessions)if(s.handle){d=s.dispatch;break;}ReleaseSRWLockShared(&lock);return d;}
static XrResult XRAPI_CALL AcquireDepthImage(XrSwapchain h,const XrSwapchainImageAcquireInfo *info,unsigned *index){
 auto fn=DepthProc<PFN_xrAcquireSwapchainImage>(DepthDispatch(h),"xrAcquireSwapchainImage");if(!fn)return XR_ERROR_HANDLE_INVALID;auto r=fn(h,info,index);
 if(XR_SUCCEEDED(r)&&index){AcquireSRWLockExclusive(&lock);auto i=depth_swapchains.find(h);if(i!=depth_swapchains.end()&&i->second.acquired.size()<32)i->second.acquired.emplace_back(*index,false);ReleaseSRWLockExclusive(&lock);}return r;
}
static XrResult XRAPI_CALL WaitDepthImage(XrSwapchain h,const XrSwapchainImageWaitInfo *info){
 auto fn=DepthProc<PFN_xrWaitSwapchainImage>(DepthDispatch(h),"xrWaitSwapchainImage");if(!fn)return XR_ERROR_HANDLE_INVALID;auto r=fn(h,info);
 if(r==XR_SUCCESS){AcquireSRWLockExclusive(&lock);auto i=depth_swapchains.find(h);if(i!=depth_swapchains.end())for(auto &a:i->second.acquired)if(!a.second){a.second=true;break;}ReleaseSRWLockExclusive(&lock);}return r;
}
static XrResult XRAPI_CALL ReleaseDepthImage(XrSwapchain h,const XrSwapchainImageReleaseInfo *info){
 auto fn=DepthProc<PFN_xrReleaseSwapchainImage>(DepthDispatch(h),"xrReleaseSwapchainImage");if(!fn)return XR_ERROR_HANDLE_INVALID;auto r=fn(h,info);
 if(XR_SUCCEEDED(r)){AcquireSRWLockExclusive(&lock);auto i=depth_swapchains.find(h);if(i!=depth_swapchains.end()&&!i->second.acquired.empty()){auto a=i->second.acquired.front();i->second.acquired.pop_front();i->second.released=a.second?a.first:~0u;}ReleaseSRWLockExclusive(&lock);}return r;
}
static XrResult XRAPI_CALL DestroyDepthSwapchain(XrSwapchain h){
 auto fn=DepthProc<PFN_xrDestroySwapchain>(DepthDispatch(h),"xrDestroySwapchain");if(!fn)return XR_ERROR_HANDLE_INVALID;
 AcquireSRWLockExclusive(&lock);depth_swapchains.erase(h);for(auto &s:sessions)for(const auto &eye:s.depth.eye)if(eye.swapchain==(uint64_t)h){s.depth={};break;}ReleaseSRWLockExclusive(&lock);return fn(h);
}
extern "C" __declspec(dllexport) bool __cdecl DLSS5ReadOpenXRDepthImages(SubmittedDepthImages *out,unsigned size){
 if(!out||size!=sizeof(*out))return false;SubmittedDepthImages result{};bool valid=false;
 AcquireSRWLockShared(&lock);
 for(const auto &s:sessions)if(s.handle&&s.d3d11&&s.depth.present_mask==3&&GetTickCount64()-s.depth.tick<=100){
  if(valid){valid=false;break;}result.metadata=s.depth;valid=true;
  for(unsigned eye=0;eye<2;++eye){auto i=depth_swapchains.find((XrSwapchain)s.depth.eye[eye].swapchain);
   if(i==depth_swapchains.end()||i->second.session!=s.handle||i->second.released>=i->second.images.size()||std::any_of(i->second.acquired.begin(),i->second.acquired.end(),[&](const auto &a){return a.first==i->second.released;})){valid=false;break;}
   result.texture[eye]=i->second.images[i->second.released].Get();}
 }
 if(valid)for(auto texture:result.texture)texture->AddRef();ReleaseSRWLockShared(&lock);
 if(valid)*out=result;return valid;
}
static XrResult XRAPI_CALL LocateViews(XrSession h,const XrViewLocateInfo *info,XrViewState *state,uint32_t capacity,uint32_t *count,XrView *views){
    auto d=ForSession(h);if(!d.locate)return XR_ERROR_HANDLE_INVALID;auto r=d.locate(h,info,state,capacity,count,views);
    if(XR_SUCCEEDED(r)&&info&&state&&count&&*count==2&&capacity>=2&&views&&
       (state->viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)){
        PoseEye e[]={Eye(views[0].pose,views[0].fov),Eye(views[1].pose,views[1].fov)};Publish(h,info->space,info->displayTime,e);
    }else if(XR_FAILED(r) || (state && !(state->viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)))Invalidate(h);
    return r;
}
// A ReShade compatibility adapter can invoke this before ReShade's own
// xrEndFrame effects. Layer ordering must not leave those effects using a
// later LocateViews prediction instead of the pose attached to this image.
extern "C" __declspec(dllexport) bool __cdecl DLSS5CaptureOpenXRSubmittedFrame(XrSession h,const XrFrameEndInfo *info){
    if(!ForSession(h).handle)return false;
    bool projection=false;
    if(info&&info->layers)for(uint32_t i=0;i<info->layerCount;++i){
        if(!info->layers[i] || info->layers[i]->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION)continue;
        auto p=reinterpret_cast<const XrCompositionLayerProjection *>(info->layers[i]);
        if(p->viewCount!=2 || !p->views)continue;
        PoseEye e[]={Eye(p->views[0].pose,p->views[0].fov),Eye(p->views[1].pose,p->views[1].fov)};
        Publish(h,p->space,info->displayTime,e);PublishDepth(h,info->displayTime,p);projection=true;break;
    }
    if(!projection)Invalidate(h);
    return projection;
}
static XrResult XRAPI_CALL EndFrame(XrSession h,const XrFrameEndInfo *info){
    auto d=ForSession(h);if(!d.end_frame)return XR_ERROR_HANDLE_INVALID;
    DLSS5CaptureOpenXRSubmittedFrame(h,info);
    auto result=d.end_frame(h,info);if(XR_FAILED(result))Invalidate(h);return result;
}
static XrResult XRAPI_CALL PollEvent(XrInstance h,XrEventDataBuffer *event){
    auto d=Find(h);if(!d.poll)return XR_ERROR_HANDLE_INVALID;auto r=d.poll(h,event);
    if(r==XR_SUCCESS&&event){
        if(event->type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
            Invalidate(reinterpret_cast<XrEventDataReferenceSpaceChangePending *>(event)->session);
        if(event->type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){auto e=reinterpret_cast<XrEventDataSessionStateChanged *>(event);
            if(e->state==XR_SESSION_STATE_STOPPING||e->state==XR_SESSION_STATE_LOSS_PENDING||e->state==XR_SESSION_STATE_EXITING)Invalidate(e->session);}
    }return r;
}
static XrResult XRAPI_CALL GetProc(XrInstance h,const char *name,PFN_xrVoidFunction *out){
    if(!name||!out)return XR_ERROR_VALIDATION_FAILURE;
    auto d=Find(h);auto next=d.gipa;
    if(!h){AcquireSRWLockShared(&lock);next=null_gipa;ReleaseSRWLockShared(&lock);}
    if(!next)return XR_ERROR_HANDLE_INVALID;
    const auto result=next(h,name,out);if(XR_FAILED(result)||!*out)return result;
#define HOOK(n,f) if(std::strcmp(name,n)==0)*out=reinterpret_cast<PFN_xrVoidFunction>(f)
    HOOK("xrGetInstanceProcAddr",GetProc);
    else HOOK("xrDestroyInstance",DestroyInstance);
    else HOOK("xrCreateSession",CreateSession);
    else HOOK("xrDestroySession",DestroySession);
    else HOOK("xrCreateSwapchain",CreateDepthSwapchain);
    else HOOK("xrAcquireSwapchainImage",AcquireDepthImage);
    else HOOK("xrWaitSwapchainImage",WaitDepthImage);
    else HOOK("xrReleaseSwapchainImage",ReleaseDepthImage);
    else HOOK("xrDestroySwapchain",DestroyDepthSwapchain);
    else HOOK("xrLocateViews",LocateViews);
    else HOOK("xrEndFrame",EndFrame);
    else HOOK("xrPollEvent",PollEvent);
#undef HOOK
    return result;
}
static XrResult XRAPI_CALL CreateLayer(const XrInstanceCreateInfo *info,const XrApiLayerCreateInfo *layer,XrInstance *out){
    if(!layer||!out||layer->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO||
       layer->structVersion!=XR_API_LAYER_CREATE_INFO_STRUCT_VERSION||layer->structSize!=sizeof(*layer)||!layer->nextInfo)
        return XR_ERROR_INITIALIZATION_FAILED;
    const auto next=layer->nextInfo;
    if(!next->nextCreateApiLayerInstance||!next->nextGetInstanceProcAddr)return XR_ERROR_INITIALIZATION_FAILED;
    XrApiLayerCreateInfo forward=*layer;forward.nextInfo=next->next;
    auto r=next->nextCreateApiLayerInstance(info,&forward,out);if(XR_FAILED(r))return r;
    Instance d;d.handle=*out;d.gipa=next->nextGetInstanceProcAddr;
#define GET(n,m) d.gipa(*out,n,reinterpret_cast<PFN_xrVoidFunction *>(&d.m))
    GET("xrDestroyInstance",destroy);GET("xrCreateSession",create_session);GET("xrDestroySession",destroy_session);
    GET("xrLocateViews",locate);GET("xrEndFrame",end_frame);GET("xrPollEvent",poll);
#undef GET
    bool stored=false;AcquireSRWLockExclusive(&lock);for(auto &i:instances)if(!i.handle){i=d;stored=true;break;}null_gipa=d.gipa;ReleaseSRWLockExclusive(&lock);
    if(!stored){if(d.destroy)d.destroy(*out);*out=XR_NULL_HANDLE;return XR_ERROR_LIMIT_REACHED;}return r;
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo *loader,const char *name,XrNegotiateApiLayerRequest *request){
    if(!loader||!request||!name||std::strcmp(name,"XR_APILAYER_DLSS5_pose")!=0||
       loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||loader->structVersion!=XR_LOADER_INFO_STRUCT_VERSION||
       loader->structSize!=sizeof(*loader)||request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||
       request->structVersion!=XR_API_LAYER_INFO_STRUCT_VERSION||request->structSize!=sizeof(*request)||
       loader->minInterfaceVersion>1||loader->maxInterfaceVersion<1||loader->minApiVersion>XR_MAKE_VERSION(1,0,0)||
       loader->maxApiVersion<XR_MAKE_VERSION(1,0,0))return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion=1;request->layerApiVersion=XR_MAKE_VERSION(1,0,0);
    request->getInstanceProcAddr=GetProc;request->createApiLayerInstance=CreateLayer;return XR_SUCCESS;
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
