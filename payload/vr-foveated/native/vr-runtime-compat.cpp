#define WIN32_LEAN_AND_MEAN
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D12
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <map>
#include <intrin.h>
#include <detours.h>
#include "pose-data.h"
#include "openxr/openxr_loader_negotiation.h"
#include <openxr/openxr_platform.h>
#include <reshade.hpp>
#include "reshade-internals/source/com_ptr.hpp"
#include "reshade-internals/source/d3d12/d3d12_impl_command_queue.hpp"
// ReShade's queue uses these two polymorphic bases. Additional queue interface
// versions add methods, not instance fields or a different base layout.
struct QueueLayout : ID3D12CommandQueue, reshade::d3d12::command_queue_impl {};
static const GUID queue_iid={0x2c576d2a,0x0c1c,0x4d1d,{0xad,0x7c,0xbc,0x4f,0xae,0xc1,0x5a,0xbc}};
static const GUID device_iid={0x2523aff4,0x978b,0x4939,{0xba,0x16,0x8e,0xe8,0x76,0xa4,0xcb,0x2a}};

// Runtime compatibility adapter: preserve the modified ReShade binary and supply its own
// queue proxy when UEVR binds the unwrapped queue to OpenXR.
static std::mutex guard;
static std::map<ID3D12CommandQueue *, ID3D12CommandQueue *> queues;
static HMODULE reshade_owner;
static PFN_xrGetInstanceProcAddr owner_gipa;
static PFN_xrEndFrame owner_end_frame;
static unsigned submitted_pose_frames;
static XrResult XRAPI_CALL SubmittedEndFrame(XrSession h,const XrFrameEndInfo *info);
using ResourceDevice=HRESULT (STDMETHODCALLTYPE *)(ID3D12Resource *,REFIID,void **);
static ResourceDevice resource_device;
static uintptr_t dfc_begin,dfc_end;
static unsigned device_fixes;
static const GUID unwrapped_iid={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
using DesktopPresent=HRESULT (STDMETHODCALLTYPE *)(IDXGISwapChain *,UINT,UINT);
static DesktopPresent desktop_present;
static IDXGISwapChain *desktop_native,*desktop_proxy;
static thread_local bool in_reshade_desktop,forwarding_desktop;
static unsigned desktop_frames,desktop_forwards;
static DesktopPresent final_present;
static IDXGISwapChain *ui_chain;
static reshade::api::effect_runtime *ui_runtime;
static bool building_ui;
static thread_local bool drawing_ui;
static unsigned ui_frames;
static bool ui_key_down,ui_open;
static const GUID swapchain_iid={0x1f445f9f,0x9887,0x4c4c,{0x90,0x55,0x4e,0x3b,0xad,0xaf,0xcc,0xa8}};
static void Log(const char *s) {
    wchar_t path[32768]; if (!GetModuleFileNameW(nullptr,path,32768)) return;
    auto slash=wcsrchr(path,L'\\'); if(!slash)return; slash[1]=0;
    if(wcscat_s(path,L"dlss5-vr-compat.log"))return;
    HANDLE f=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,0,nullptr);
    if(f!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(f,s,(DWORD)strlen(s),&n,nullptr);WriteFile(f,"\r\n",2,&n,nullptr);CloseHandle(f);}
}
static bool NeedsDesktopMirrorUI() {
#ifdef PALIA_QUEUE_TEST
    extern bool test_mirror_active;
    return test_mirror_active;
#else
    return GetModuleHandleW(L"UEVRBackend.dll") != nullptr;
#endif
}
static HRESULT STDMETHODCALLTYPE FinalPresent(IDXGISwapChain *chain,UINT interval,UINT flags) {
    if(NeedsDesktopMirrorUI()&&chain==ui_chain&&ui_runtime&&!drawing_ui&&!(flags&DXGI_PRESENT_TEST)){
        drawing_ui=true;
        HWND foreground=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(foreground,&pid);
        const bool key=pid==GetCurrentProcessId()&&(GetAsyncKeyState(VK_HOME)&0x8000)!=0;
        if(key&&!ui_key_down){ui_open=!ui_open;ui_runtime->open_overlay(ui_open,reshade::api::input_source::keyboard);Log(ui_open?"Independent desktop UI opened":"Independent desktop UI closed");}
        ui_key_down=key;
        reshade::update_and_present_effect_runtime(ui_runtime);
        if(++ui_frames<=3)Log("Independent ReShade desktop UI rendered immediately before DXGI Present");
        drawing_ui=false;
    }
    return final_present(chain,interval,flags);
}
static void CreateDesktopUI(reshade::api::command_queue *queue,uint64_t swapchain_native) {
    if(ui_runtime||building_ui||!queue)return;
    const auto api=queue->get_device()->get_api();
    if(api!=reshade::api::device_api::d3d11&&api!=reshade::api::device_api::d3d12)return;
    auto native=(IDXGISwapChain *)swapchain_native;
    if((uintptr_t)native<65536)return;
    DXGI_SWAP_CHAIN_DESC desc={};if(FAILED(native->GetDesc(&desc))||!desc.OutputWindow)return;
    IUnknown *device=nullptr;const GUID iid=api==reshade::api::device_api::d3d11?__uuidof(ID3D11Device):__uuidof(ID3D12Device);
    if(FAILED(native->GetDevice(iid,(void **)&device)))return;
    char config[32768];GetModuleFileNameA(nullptr,config,32768);auto slash=strrchr(config,'\\');if(!slash){device->Release();return;}slash[1]=0;strcat_s(config,"ReShadeDesktopUI.ini");
    building_ui=true;
    bool created=reshade::create_effect_runtime(api,device,api==reshade::api::device_api::d3d11?nullptr:(void *)queue->get_native(),native,config,&ui_runtime);
    building_ui=false;device->Release();
    if(!created||!ui_runtime){Log("Independent desktop UI runtime creation failed");return;}
    ui_runtime->set_effects_state(false);ui_chain=native;
    if(!final_present){
        final_present=(DesktopPresent)(*(void ***)native)[8];
        DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
        if(DetourAttach((PVOID *)&final_present,(PVOID)FinalPresent)!=NO_ERROR){DetourTransactionAbort();final_present=nullptr;}
        else if(DetourTransactionCommit()!=NO_ERROR)final_present=nullptr;
    }
    if(!final_present){reshade::destroy_effect_runtime(ui_runtime);ui_runtime=nullptr;ui_chain=nullptr;Log("Independent desktop UI Present hook failed");}
    else Log("Independent desktop UI runtime created on game window; final Present hook armed");
}
static void DestroySwapchain(reshade::api::swapchain *chain,bool){
    if(ui_runtime&&!building_ui&&chain->get_native()==(uint64_t)ui_chain){
        auto old=ui_runtime;ui_runtime=nullptr;ui_chain=nullptr;building_ui=true;reshade::destroy_effect_runtime(old);building_ui=false;
    }
}
static HRESULT STDMETHODCALLTYPE DesktopPresentBridge(IDXGISwapChain *chain,UINT interval,UINT flags) {
    if(chain!=desktop_native||!desktop_proxy||in_reshade_desktop||forwarding_desktop)return desktop_present(chain,interval,flags);
    forwarding_desktop=true;
    auto r=desktop_proxy->Present(interval,flags);
    forwarding_desktop=false;
    if(++desktop_forwards<=3)Log("Native desktop Present routed through existing ReShade wrapper");
    return r;
}
static bool ArmDesktop(reshade::api::effect_runtime *desktop) {
    if(!desktop||!desktop->get_hwnd())return false;
    auto native=(IDXGISwapChain *)desktop->get_native();
    IDXGISwapChain *proxy=nullptr;UINT size=sizeof(proxy);
    if(FAILED(native->GetPrivateData(swapchain_iid,&size,&proxy))||!proxy||proxy==native)return false;
    if(desktop_present){desktop_native=native;desktop_proxy=proxy;return true;}
    auto target=(DesktopPresent)(*(void ***)native)[8];
#ifndef PALIA_QUEUE_TEST
    HMODULE owner=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)target,&owner))return false;
    wchar_t path[32768];GetModuleFileNameW(owner,path,32768);auto slash=wcsrchr(path,L'\\');
    if(!slash||_wcsicmp(slash+1,L"UEVRBackend.dll"))return false;
#endif
    desktop_present=target;desktop_native=native;desktop_proxy=proxy;
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    if(DetourAttach((PVOID *)&desktop_present,(PVOID)DesktopPresentBridge)!=NO_ERROR){DetourTransactionAbort();desktop_present=nullptr;return false;}
    if(DetourTransactionCommit()!=NO_ERROR){desktop_present=nullptr;return false;}
    Log("UEVR desktop Present bridge armed on existing ReShade swapchain");return true;
}
static reshade::api::effect_runtime *desktop_runtime;
static void InitRuntime(reshade::api::effect_runtime *rt){if(rt->get_hwnd())desktop_runtime=rt;}
static void DestroyRuntime(reshade::api::effect_runtime *rt){if(desktop_runtime==rt){desktop_runtime=nullptr;desktop_native=nullptr;desktop_proxy=nullptr;}}
static void Present(reshade::api::command_queue *queue,reshade::api::swapchain *chain,const reshade::api::rect *,const reshade::api::rect *,uint32_t,const reshade::api::rect *){
    if(!ui_runtime)CreateDesktopUI(queue,chain->get_native());
    if(desktop_runtime && chain->get_native()==desktop_runtime->get_native()){
        in_reshade_desktop=true;
        if(++desktop_frames<=3)Log("ReShade desktop presentation callback reached");
    }
}
static void FinishPresent(reshade::api::command_queue *,reshade::api::swapchain *chain){if(desktop_runtime&&chain->get_native()==desktop_runtime->get_native())in_reshade_desktop=false;}
static HRESULT STDMETHODCALLTYPE ResourceGetDevice(ID3D12Resource *resource,REFIID iid,void **out) {
    auto caller=(uintptr_t)_ReturnAddress();
    auto result=resource_device(resource,iid,out);
    // Keep ReShade's device identity for the game and all shader consumers.
    // DFC's native codec compares resource devices to the native NGX device.
    if(FAILED(result)||!out||!*out||caller<dfc_begin||caller>=dfc_end||iid!=__uuidof(ID3D12Device))return result;
    IUnknown *native=nullptr;
    if(SUCCEEDED(((IUnknown *)*out)->QueryInterface(unwrapped_iid,(void **)&native))&&native){
        void *requested=nullptr;
        auto hr=native->QueryInterface(iid,&requested);native->Release();
        if(SUCCEEDED(hr)&&requested){((IUnknown *)*out)->Release();*out=requested;
            if(++device_fixes<=3)Log("DFC resource device normalized to native identity; other callers retain ReShade proxy");}
    }
    return result;
}
static bool ArmDeviceIdentity(ID3D12Resource *resource,HMODULE consumer) {
    if(resource_device||!resource||!consumer)return resource_device!=nullptr;
    auto dos=(IMAGE_DOS_HEADER *)consumer;
    auto nt=(IMAGE_NT_HEADERS *)((char *)consumer+dos->e_lfanew);
    dfc_begin=(uintptr_t)consumer;dfc_end=dfc_begin+nt->OptionalHeader.SizeOfImage;
    resource_device=(ResourceDevice)(*(void ***)resource)[7];
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    auto attach=DetourAttach((PVOID *)&resource_device,(PVOID)ResourceGetDevice);
    if(attach!=NO_ERROR){DetourTransactionAbort();resource_device=nullptr;return false;}
    auto commit=DetourTransactionCommit();
    if(commit!=NO_ERROR){resource_device=nullptr;return false;}
    Log("DFC-only D3D12 resource device identity adapter armed");return true;
}
static void Begin(reshade::api::effect_runtime *rt,reshade::api::command_list *,reshade::api::resource_view,reshade::api::resource_view) {
    // Desktop UI is rendered on the final DXGI call, after UEVR's mirror draw.
    if(resource_device||rt->get_hwnd()!=0||rt->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
    ArmDeviceIdentity((ID3D12Resource *)rt->get_back_buffer(0).handle,GetModuleHandleW(L"deep-fried-chicken.addon64"));
}
static void InitQueue(reshade::api::command_queue *q) {
    if(q->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
    // Compiler-derived base conversion, not a guessed object offset.
    auto proxy=static_cast<QueueLayout *>(static_cast<reshade::d3d12::command_queue_impl *>(q));
    ID3D12CommandQueue *verified=nullptr;
    if(FAILED(proxy->QueryInterface(queue_iid,(void **)&verified)))return;
    auto native=(ID3D12CommandQueue *)q->get_native();
    {std::lock_guard<std::mutex> l(guard);queues[native]=verified;}
    verified->Release(); Log("Captured validated ReShade D3D12 command queue proxy");
}
static void DestroyQueue(reshade::api::command_queue *q) {
    std::lock_guard<std::mutex> l(guard);queues.erase((ID3D12CommandQueue *)q->get_native());
}
static XrResult XRAPI_CALL CreateSession(XrInstance instance,const XrSessionCreateInfo *info,XrSession *out) {
    PFN_xrCreateSession next=nullptr;
    auto r=owner_gipa(instance,"xrCreateSession",(PFN_xrVoidFunction *)&next);
    if(XR_FAILED(r)||!next)return XR_ERROR_FUNCTION_UNSUPPORTED;
    // Only replace a head binding. Unknown structure chains pass unchanged.
    auto head=info ? (const XrBaseInStructure *)info->next : nullptr;
    if(!head||head->type!=XR_TYPE_GRAPHICS_BINDING_D3D12_KHR)return next(instance,info,out);
    auto binding=*(const XrGraphicsBindingD3D12KHR *)head;
    ID3D12CommandQueue *proxy=nullptr;
    {std::lock_guard<std::mutex> l(guard);auto it=queues.find(binding.queue);if(it!=queues.end()){proxy=it->second;proxy->AddRef();}}
    void *already=nullptr;
    HRESULT original_qi=binding.queue->QueryInterface(queue_iid,&already);
    if(already)((IUnknown *)already)->Release();
    char message[160];sprintf_s(message,"OpenXR original queue proxy QI=0x%08lx; mapped=%d",(unsigned long)original_qi,proxy!=nullptr);Log(message);
    if(!proxy||SUCCEEDED(original_qi)){if(proxy)proxy->Release();return next(instance,info,out);}
    ID3D12Device *device=nullptr;
    HRESULT device_result=proxy->GetDevice(__uuidof(ID3D12Device),(void **)&device);
    IUnknown *a=nullptr,*b=nullptr;
    ID3D12Device *binding_proxy=nullptr;UINT pointer_size=sizeof(binding_proxy);
    binding.device->GetPrivateData(device_iid,&pointer_size,&binding_proxy);
    if(SUCCEEDED(device_result)&&binding_proxy&&pointer_size==sizeof(binding_proxy)){
        device->QueryInterface(__uuidof(IUnknown),(void **)&a);
        binding_proxy->QueryInterface(__uuidof(IUnknown),(void **)&b);
    }
    bool same=a&&a==b;
    if(a)a->Release();if(b)b->Release();if(device)device->Release();
    if(!same){Log("Device identity mismatch; preserving original binding");proxy->Release();return next(instance,info,out);}
    binding.queue=proxy;auto copy=*info;copy.next=&binding;
    r=next(instance,&copy,out);proxy->Release();
    sprintf_s(message,"OpenXR session with validated ReShade queue proxy: %d",r);Log(message);return r;
}
static XrResult XRAPI_CALL Gipa(XrInstance instance,const char *name,PFN_xrVoidFunction *fn) {
    auto r=owner_gipa(instance,name,fn);
    if(XR_SUCCEEDED(r)&&fn&&!strcmp(name,"xrCreateSession"))*fn=(PFN_xrVoidFunction)CreateSession;
    if(XR_SUCCEEDED(r)&&fn&&*fn&&!strcmp(name,"xrEndFrame")&&*fn!=(PFN_xrVoidFunction)SubmittedEndFrame){
        owner_end_frame=(PFN_xrEndFrame)*fn;*fn=(PFN_xrVoidFunction)SubmittedEndFrame;
    }
    return r;
}
static XrResult XRAPI_CALL SubmittedEndFrame(XrSession h,const XrFrameEndInfo *info){
    auto module=GetModuleHandleW(L"DLSS5OpenXRPose.dll");
    using Capture=bool (__cdecl *)(XrSession,const XrFrameEndInfo *);
    auto capture=module?(Capture)GetProcAddress(module,"DLSS5CaptureOpenXRSubmittedFrame"):nullptr;
    using Read=bool (__cdecl *)(PoseFrame *,uint32_t);
    auto read=module?(Read)GetProcAddress(module,"DLSS5ReadOpenXRPose"):nullptr;
    if(read&&info&&info->layers&&(submitted_pose_frames<3 || submitted_pose_frames%600==0)){
        PoseFrame previous;
        if(read(&previous,sizeof(previous)))for(uint32_t i=0;i<info->layerCount;++i){
            if(!info->layers[i]||info->layers[i]->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION)continue;
            auto p=(const XrCompositionLayerProjection *)info->layers[i];if(p->viewCount!=2||!p->views)continue;
            const auto q=p->views[0].pose.orientation;
            const float dot=std::abs(previous.eye[0].q[0]*q.x+previous.eye[0].q[1]*q.y+previous.eye[0].q[2]*q.z+previous.eye[0].q[3]*q.w);
            char message[220];sprintf_s(message,"Pre-effects pose/image timing: prior-minus-submitted=%lld ns left rotation mismatch=%.4f deg",(long long)(previous.display_time-info->displayTime),2*std::acos((std::min)(1.f,dot))*57.2957795f);Log(message);break;
        }
    }
    if(capture&&capture(h,info)&&++submitted_pose_frames<=3)Log("Submitted image eye poses published before ReShade OpenXR effects");
    return owner_end_frame?owner_end_frame(h,info):XR_ERROR_FUNCTION_UNSUPPORTED;
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo *info,const char *name,XrNegotiateApiLayerRequest *request) {
    auto owner=reshade_owner;
    if(!owner){
        HMODULE modules[1024];DWORD bytes=0;
        if(K32EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes))
            for(DWORD i=0;i<(std::min)(bytes,DWORD(sizeof(modules)))/sizeof(HMODULE);++i)
                if(GetProcAddress(modules[i],"ReShadeRegisterAddon")&&GetProcAddress(modules[i],"xrNegotiateLoaderApiLayerInterface")){owner=modules[i];break;}
    }
    auto next=owner ? (PFN_xrNegotiateLoaderApiLayerInterface)GetProcAddress(owner,"xrNegotiateLoaderApiLayerInterface") : nullptr;
    if(!next)return XR_ERROR_INITIALIZATION_FAILED;
    auto r=next(info,name,request);
    if(XR_SUCCEEDED(r)){owner_gipa=request->getInstanceProcAddr;request->getInstanceProcAddr=Gipa;Log("Universal queue adapter negotiated; modified ReShade remains layer owner");}
    return r;
}
extern "C" __declspec(dllexport) const char *NAME="DLSS5 VR runtime compatibility";
extern "C" __declspec(dllexport) const char *DESCRIPTION="Reconnects a validated ReShade queue proxy to UEVR OpenXR sessions.";
extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE module,HMODULE owner) {
    if(!reshade::register_addon(module,owner))return FALSE;
    reshade_owner=owner;
    reshade::register_event<reshade::addon_event::init_command_queue>(InitQueue);
    reshade::register_event<reshade::addon_event::destroy_command_queue>(DestroyQueue);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);
    reshade::register_event<reshade::addon_event::init_effect_runtime>(InitRuntime);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(DestroyRuntime);
    reshade::register_event<reshade::addon_event::present>(Present);
    reshade::register_event<reshade::addon_event::finish_present>(FinishPresent);
    reshade::register_event<reshade::addon_event::destroy_swapchain>(DestroySwapchain);return TRUE;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE module,HMODULE) {
    reshade::unregister_event<reshade::addon_event::init_command_queue>(InitQueue);
    reshade::unregister_event<reshade::addon_event::destroy_command_queue>(DestroyQueue);
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);
    reshade::unregister_event<reshade::addon_event::init_effect_runtime>(InitRuntime);
    reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(DestroyRuntime);
    reshade::unregister_event<reshade::addon_event::present>(Present);
    reshade::unregister_event<reshade::addon_event::finish_present>(FinishPresent);
    reshade::unregister_event<reshade::addon_event::destroy_swapchain>(DestroySwapchain);
    if(final_present){DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());DetourDetach((PVOID *)&final_present,(PVOID)FinalPresent);DetourTransactionCommit();final_present=nullptr;}
    if(ui_runtime){auto old=ui_runtime;ui_runtime=nullptr;building_ui=true;reshade::destroy_effect_runtime(old);building_ui=false;}
    if(desktop_present){DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());DetourDetach((PVOID *)&desktop_present,(PVOID)DesktopPresentBridge);DetourTransactionCommit();desktop_present=nullptr;}
    if(resource_device){DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());DetourDetach((PVOID *)&resource_device,(PVOID)ResourceGetDevice);DetourTransactionCommit();resource_device=nullptr;}
    {std::lock_guard<std::mutex> l(guard);queues.clear();}reshade::unregister_addon(module);
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
