#define VR_DASHBOARD_TEST
#define WIN32_LEAN_AND_MEAN
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_PLATFORM_WIN32
#define VK_NO_PROTOTYPES
#include "../payload/vr-foveated/native/vulkan-headers/include/vulkan/vulkan_core.h"
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <detours.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <map>
#include <cassert>
#include "../payload/vr-foveated/native/openxr/openxr.h"
#include "../payload/vr-foveated/native/openxr/openxr_platform.h"
static void Log(const char *s){puts(s);}
#include "../payload/vr-foveated/native/openxr-dashboard.h"
using Microsoft::WRL::ComPtr;
static ComPtr<ID3D12Resource> image;
static ComPtr<ID3D11Texture2D> image11;
static bool use11=false,timeout=false,fail=false,trigger=false,left_click=false,right_click=false;
static XrVector2f stick={};
static unsigned anchors=0;
static XrPosef last_anchor={},head_pose={{0,0,0,1},{1,2,3}};
static XrPosef aim_pose={{0,0,0,1},{0,0,0}};
static std::map<std::string,XrPath> paths;
static unsigned acquired=0,released=0,destroyed=0;
static std::vector<XrActionSet> attached;
static std::vector<XrActiveActionSet> active;
static std::map<XrPath,std::vector<XrActionSuggestedBinding>> suggestions;
static XrResult XRAPI_CALL Path(XrInstance,const char *name,XrPath *out){auto it=paths.find(name);if(it==paths.end())it=paths.emplace(name,paths.size()+1).first;*out=it->second;return XR_SUCCESS;}
static XrResult XRAPI_CALL SetCreate(XrInstance,const XrActionSetCreateInfo *,XrActionSet *out){static uintptr_t next=0x100;*out=(XrActionSet)++next;return XR_SUCCESS;}
static XrResult XRAPI_CALL ActionCreate(XrActionSet,const XrActionCreateInfo *,XrAction *out){static uintptr_t n=0x200;*out=(XrAction)++n;return XR_SUCCESS;}
static XrResult XRAPI_CALL SetDestroy(XrActionSet){return XR_SUCCESS;}
static XrResult XRAPI_CALL ActionSpace(XrSession,const XrActionSpaceCreateInfo *,XrSpace *out){*out=(XrSpace)0x400;return XR_SUCCESS;}
static XrResult XRAPI_CALL Space(XrSession,const XrReferenceSpaceCreateInfo *info,XrSpace *out){assert(info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_VIEW||info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL);if(info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL&&info->poseInReferenceSpace.position.x!=0){++anchors;last_anchor=info->poseInReferenceSpace;}*out=(XrSpace)0x500;return XR_SUCCESS;}
static XrResult XRAPI_CALL DestroySpace(XrSpace){return XR_SUCCESS;}
static XrResult XRAPI_CALL Locate(XrSpace from,XrSpace,XrTime,XrSpaceLocation *out){out->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;out->pose=from==(XrSpace)0x400?aim_pose:head_pose;return XR_SUCCESS;}
static XrResult XRAPI_CALL Pose(XrSession,const XrActionStateGetInfo *,XrActionStatePose *out){out->isActive=XR_TRUE;return XR_SUCCESS;}
static XrResult XRAPI_CALL Value(XrSession,const XrActionStateGetInfo *,XrActionStateFloat *out){out->isActive=XR_TRUE;out->currentState=trigger?1.f:0.f;return XR_SUCCESS;}
static XrResult XRAPI_CALL Vector(XrSession,const XrActionStateGetInfo *,XrActionStateVector2f *out){out->isActive=XR_TRUE;out->currentState=stick;return XR_SUCCESS;}
static XrResult XRAPI_CALL Boolean(XrSession,const XrActionStateGetInfo *i,XrActionStateBoolean *out){out->isActive=XR_TRUE;out->currentState=i->subactionPath==paths["/user/hand/left"]?left_click:right_click;return XR_SUCCESS;}
static XrResult XRAPI_CALL Suggest(XrInstance,const XrInteractionProfileSuggestedBinding *i){suggestions[i->interactionProfile]={i->suggestedBindings,i->suggestedBindings+i->countSuggestedBindings};return XR_SUCCESS;}
static XrResult XRAPI_CALL Attach(XrSession,const XrSessionActionSetsAttachInfo *i){attached={i->actionSets,i->actionSets+i->countActionSets};return XR_SUCCESS;}
static XrResult XRAPI_CALL Sync(XrSession,const XrActionsSyncInfo *i){active={i->activeActionSets,i->activeActionSets+i->countActiveActionSets};return XR_SUCCESS;}
static XrResult XRAPI_CALL Formats(XrSession,uint32_t capacity,uint32_t *count,int64_t *out){*count=1;if(capacity)out[0]=DXGI_FORMAT_B8G8R8A8_UNORM;return XR_SUCCESS;}
static XrResult XRAPI_CALL Swapchain(XrSession,const XrSwapchainCreateInfo *i,XrSwapchain *out){assert(i->width==1024&&i->height==640&&i->arraySize==1);*out=(XrSwapchain)0x600;return XR_SUCCESS;}
static XrResult XRAPI_CALL DestroySwapchain(XrSwapchain){++destroyed;return XR_SUCCESS;}
static XrResult XRAPI_CALL Images(XrSwapchain,uint32_t capacity,uint32_t *count,XrSwapchainImageBaseHeader *out){*count=1;if(capacity){if(use11)((XrSwapchainImageD3D11KHR *)out)->texture=image11.Get();else ((XrSwapchainImageD3D12KHR *)out)->texture=image.Get();}return XR_SUCCESS;}
static XrResult XRAPI_CALL Acquire(XrSwapchain,const XrSwapchainImageAcquireInfo *,uint32_t *out){++acquired;*out=0;return XR_SUCCESS;}
static XrResult XRAPI_CALL Wait(XrSwapchain,const XrSwapchainImageWaitInfo *){return timeout?XR_TIMEOUT_EXPIRED:(fail?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS);}
static XrResult XRAPI_CALL Release(XrSwapchain,const XrSwapchainImageReleaseInfo *){++released;return XR_SUCCESS;}
static XrResult XRAPI_CALL Gipa(XrInstance,const char *name,PFN_xrVoidFunction *out){
#define FN(n,f) if(!strcmp(name,n)){*out=(PFN_xrVoidFunction)f;return XR_SUCCESS;}
 FN("xrStringToPath",Path) FN("xrCreateActionSet",SetCreate) FN("xrCreateAction",ActionCreate) FN("xrDestroyActionSet",SetDestroy)
 FN("xrCreateActionSpace",ActionSpace) FN("xrDestroySpace",DestroySpace) FN("xrLocateSpace",Locate)
 FN("xrGetActionStateVector2f",Vector) FN("xrGetActionStatePose",Pose) FN("xrGetActionStateFloat",Value) FN("xrGetActionStateBoolean",Boolean)
 FN("xrSuggestInteractionProfileBindings",Suggest) FN("xrAttachSessionActionSets",Attach) FN("xrSyncActions",Sync)
 FN("xrCreateReferenceSpace",Space) FN("xrEnumerateSwapchainFormats",Formats) FN("xrCreateSwapchain",Swapchain)
 FN("xrDestroySwapchain",DestroySwapchain) FN("xrEnumerateSwapchainImages",Images) FN("xrAcquireSwapchainImage",Acquire) FN("xrWaitSwapchainImage",Wait) FN("xrReleaseSwapchainImage",Release)
#undef FN
 *out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
}
static void Drain(vr_dashboard::Panel &p){if(!p.fence)return;auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(SUCCEEDED(p.fence->SetEventOnCompletion(p.fence_value,event)));assert(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);CloseHandle(event);}
static SHORT WINAPI FakeKey(int){return (SHORT)0x8001;}
static BOOL WINAPI FakeKeys(PBYTE keys){memset(keys,0x80,256);return TRUE;}
static DWORD WINAPI FakePad(DWORD,XINPUT_STATE *state){memset(state,0xff,sizeof(*state));return ERROR_SUCCESS;}
static int window_inputs=0;
static LRESULT CALLBACK FakeWindow(HWND,UINT,WPARAM,LPARAM){++window_inputs;return 123;}

int main(){
    vr_dashboard::Surface preview;assert(preview.valid());preview.Draw(2,42,18,{512,260,false},true);
    BITMAPFILEHEADER file={};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+1024*640*4;
    BITMAPINFOHEADER info={};info.biSize=sizeof(info);info.biWidth=1024;info.biHeight=-640;info.biPlanes=1;info.biBitCount=32;
    FILE *f=nullptr;fopen_s(&f,"dashboard-preview.bmp","wb");assert(f);fwrite(&file,sizeof(file),1,f);fwrite(&info,sizeof(info),1,f);fwrite(preview.pixels,1024*640*4,1,f);fclose(f);
    assert(vr_dashboard::Hit(32,220)==0&&vr_dashboard::Hit(900,260)==4&&vr_dashboard::Hit(31,220)==-1&&vr_dashboard::Hit(100,306)==-1);
    auto module=LoadLibraryW(L"native-vr-foveation-nvngx.dll.addon64");assert(module);
    vr_dashboard::Native native;assert(native.read&&native.set);assert(native.set(0));assert(!native.set(5));
    XrInstance instance=(XrInstance)0x1;XrSession session=(XrSession)0x2;
    vr_dashboard_input::suggest=Suggest;vr_dashboard_input::attach=Attach;vr_dashboard_input::sync=Sync;
    XrPath profile={};Path(instance,"/interaction_profiles/oculus/touch_controller",&profile);
    XrActionSuggestedBinding game_binding={(XrAction)0x900,0x901};XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};suggested.interactionProfile=profile;suggested.countSuggestedBindings=1;suggested.suggestedBindings=&game_binding;
    assert(XR_SUCCEEDED(vr_dashboard_input::Suggest(instance,&suggested)));
    auto controller=vr_dashboard_input::Create(instance,session,Gipa);assert(controller);
    assert(suggestions[profile].size()==8&&suggestions[profile][0].action==game_binding.action);
    XrActionSet game_set=(XrActionSet)0x800;XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&game_set;
    assert(XR_SUCCEEDED(vr_dashboard_input::Attach(session,&attach)));assert(attached.size()==3&&attached[0]==game_set);
    XrActiveActionSet game_active={game_set,0};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&game_active;
    assert(XR_SUCCEEDED(vr_dashboard_input::Sync(session,&sync)));assert(active.size()==2&&active[0].actionSet==game_set&&active[1].actionSet==controller->opener);
    controller->visible=true;assert(XR_SUCCEEDED(vr_dashboard_input::Sync(session,&sync)));assert(active.size()==2&&active[0].actionSet==game_set&&active[1].actionSet==controller->set);
    vr_dashboard::Panel p;p.instance=instance;p.session=session;p.gipa=Gipa;p.controller=controller;p.visible=true;
    ComPtr<IDXGIFactory4> factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> warp;assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
    assert(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&p.device))));D3D12_COMMAND_QUEUE_DESC q={};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;assert(SUCCEEDED(p.device->CreateCommandQueue(&q,IID_PPV_ARGS(&p.queue))));
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=1024;rd.Height=640;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;rd.SampleDesc.Count=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    assert(SUCCEEDED(p.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&image))));
    // Point at the center of the Balanced button; first held press cannot click.
    aim_pose.position.x=0;aim_pose.position.y=(0.5f-260.f/640)*0.78125f;
    trigger=true;assert(p.Frame(1));Drain(p);unsigned preset;unsigned long long cropped,full;assert(native.read(&preset,&cropped,&full)&&preset==0);
    assert(anchors==1&&last_anchor.position.x==1);head_pose.position.x=9;trigger=false;assert(p.Frame(2));Drain(p);assert(anchors==1&&last_anchor.position.x==1);trigger=true;assert(p.Frame(3));Drain(p);assert(native.read(&preset,&cropped,&full)&&preset==2);
    stick={1,-1};auto scroll=controller->Read(p.space,4);assert(scroll.valid&&scroll.scroll_x==1&&scroll.scroll_y==-1);
    stick={0.1f,-0.2f};scroll=controller->Read(p.space,4);assert(scroll.scroll_x==0&&scroll.scroll_y==0);
    stick={-1,1};scroll=controller->Read(p.space,4);assert(scroll.scroll_x==-1&&scroll.scroll_y==1);stick={};
    assert(p.Frame(4));Drain(p);assert(native.read(&preset,&cropped,&full)&&preset==2);
    assert(p.quad.eyeVisibility==XR_EYE_VISIBILITY_BOTH&&p.quad.space==(XrSpace)0x500&&p.quad.pose.position.z==-1.25f);
    // Read back actual GPU pixels to verify upload and resource barriers.
    ComPtr<ID3D12Resource> readback;hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer={};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=p.upload_size;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    assert(SUCCEEDED(p.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))));
    assert(SUCCEEDED(p.allocator->Reset())&&SUCCEEDED(p.list->Reset(p.allocator.Get(),nullptr)));D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=image.Get();b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;p.list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION dst={};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=p.footprint;D3D12_TEXTURE_COPY_LOCATION src={};src.pResource=image.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;p.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);p.list->ResourceBarrier(1,&b);assert(SUCCEEDED(p.list->Close()));ID3D12CommandList *commands[]={p.list.Get()};p.queue->ExecuteCommandLists(1,commands);assert(SUCCEEDED(p.queue->Signal(p.fence.Get(),++p.fence_value)));Drain(p);
    void *mapped=nullptr;assert(SUCCEEDED(readback->Map(0,nullptr,&mapped)));for(unsigned y=0;y<640;++y)assert(!memcmp((char *)mapped+p.footprint.Offset+y*p.footprint.Footprint.RowPitch,(char *)p.surface.pixels+y*1024*4,1024*4));readback->Unmap(0,nullptr);
    timeout=true;unsigned before_acquire=acquired,before_release=released;assert(p.Frame(5));assert(acquired==before_acquire+1&&released==before_release);assert(p.Frame(6));assert(acquired==before_acquire+1);timeout=false;assert(p.Frame(7));Drain(p);assert(released==before_release+1);
    assert(p.Dispose());controller->visible=false;assert(XR_SUCCEEDED(vr_dashboard_input::Sync(session,&sync)));assert(active[0].actionSet==game_set);
    // D3D11 path and a runtime failure both retain usable game input.
    use11=true;vr_dashboard::Panel p11;p11.instance=instance;p11.session=session;p11.gipa=Gipa;p11.visible=true;
    D3D_FEATURE_LEVEL feature;assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&p11.device11,&feature,nullptr)));
    D3D11_TEXTURE2D_DESC td={};td.Width=1024;td.Height=640;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;assert(SUCCEEDED(p11.device11->CreateTexture2D(&td,nullptr,&image11)));
    assert(p11.Frame(8));fail=true;vr_dashboard_block::active=true;assert(!p11.Frame(9)&&!vr_dashboard_block::active.load());fail=false;assert(p11.Dispose());
    // Game input is blocked while UI owns input and restored on close.
    vr_dashboard_block::async_key=FakeKey;vr_dashboard_block::key_state=FakeKey;vr_dashboard_block::keyboard=FakeKeys;
    vr_dashboard_block::gamepad[0]=FakePad;vr_dashboard_block::previous=FakeWindow;
    vr_dashboard_block::active=true;assert(vr_dashboard_block::AsyncKey('W')==0&&vr_dashboard_block::Key('W')==0);
    BYTE keys[256];memset(keys,0xff,sizeof(keys));assert(vr_dashboard_block::Keyboard(keys));for(auto key:keys)assert(key==0);
    XINPUT_STATE pad;assert(vr_dashboard_block::GamepadState<0>(0,&pad)==ERROR_SUCCESS&&pad.Gamepad.wButtons==0);
    assert(vr_dashboard_block::Window(nullptr,WM_KEYDOWN,'W',0)==0&&window_inputs==0);
    assert(vr_dashboard_block::Window(nullptr,WM_LBUTTONDOWN,0,0)==0&&window_inputs==0);
    vr_dashboard_block::active=false;assert(vr_dashboard_block::AsyncKey('W')==(SHORT)0x8001);
    assert(vr_dashboard_block::Window(nullptr,WM_KEYDOWN,'W',0)==123&&window_inputs==1);
    assert(vr_dashboard_block::GamepadState<0>(0,&pad)==ERROR_SUCCESS&&pad.Gamepad.wButtons==0xffff);
    vr_dashboard_block::gamepad[0]=nullptr;vr_dashboard_block::previous=nullptr;

    // Game analog inputs are neutral while menu owns input; tracked poses stay live.
    vr_dashboard_input::query_float=Value;vr_dashboard_input::query_vector=Vector;vr_dashboard_input::query_boolean=Boolean;
    controller->visible=true;trigger=true;stick={1,1};XrActionStateGetInfo query{XR_TYPE_ACTION_STATE_GET_INFO};query.action=(XrAction)0x999;
    XrActionStateFloat analog{XR_TYPE_ACTION_STATE_FLOAT};assert(XR_SUCCEEDED(vr_dashboard_input::Float(session,&query,&analog))&&!analog.isActive&&analog.currentState==0);
    XrActionStateVector2f axes{XR_TYPE_ACTION_STATE_VECTOR2F};assert(XR_SUCCEEDED(vr_dashboard_input::Vector(session,&query,&axes))&&!axes.isActive&&axes.currentState.x==0);
    query.action=controller->select;assert(XR_SUCCEEDED(vr_dashboard_input::Float(session,&query,&analog))&&analog.isActive&&analog.currentState==1);
    XrActionStatePose tracking{XR_TYPE_ACTION_STATE_POSE};assert(XR_SUCCEEDED(Pose(session,&query,&tracking))&&tracking.isActive);
    controller->visible=false;query.action=(XrAction)0x999;assert(XR_SUCCEEDED(vr_dashboard_input::Float(session,&query,&analog))&&analog.isActive&&analog.currentState==1);
    // Shortcut polls while closed and toggles exactly once until both clicks release.
    left_click=right_click=false;assert(XR_SUCCEEDED(vr_dashboard_input::Sync(session,&sync)));
    left_click=right_click=true;controller->Shortcut(GetTickCount64());controller->gesture.began=GetTickCount64()-3001;controller->Shortcut(GetTickCount64());assert(controller->toggle_pending.exchange(false));
    controller->Shortcut(GetTickCount64());assert(!controller->toggle_pending.load());left_click=right_click=false;controller->Shortcut(GetTickCount64());
    vr_dashboard_input::Destroy(session);
    // Inactive high-priority game sets cannot steal the low-priority context's sticks.
    auto session2=(XrSession)0x991;vr_dashboard_input::create_game_set=SetCreate;XrActionSet high{},low{};XrActionSetCreateInfo create{XR_TYPE_ACTION_SET_CREATE_INFO};create.priority=7;
    assert(XR_SUCCEEDED(vr_dashboard_input::GameSet(instance,&create,&high)));create.priority=0;assert(XR_SUCCEEDED(vr_dashboard_input::GameSet(instance,&create,&low)));
    auto second=vr_dashboard_input::Create(instance,session2,Gipa);assert(second);XrActionSet game_sets[]={high,low};attach.countActionSets=2;attach.actionSets=game_sets;
    assert(XR_SUCCEEDED(vr_dashboard_input::Attach(session2,&attach))&&second->openers.count(7));
    XrActiveActionSet context={low,XR_NULL_PATH};sync.activeActionSets=&context;assert(XR_SUCCEEDED(vr_dashboard_input::Sync(session2,&sync)));assert(active.back().actionSet==second->openers.at(0).first);
    context={high,XR_NULL_PATH};assert(XR_SUCCEEDED(vr_dashboard_input::Sync(session2,&sync)));assert(active.back().actionSet==second->openers.at(7).first);
    XrActiveActionSet mixed[]={{high,second->left_hand},{low,second->hand}};sync.countActiveActionSets=2;sync.activeActionSets=mixed;
    assert(XR_SUCCEEDED(vr_dashboard_input::Sync(session2,&sync))&&active.size()==4&&active[2].subactionPath==second->left_hand&&active[3].subactionPath==second->hand);
    vr_dashboard_input::Destroy(session2);

    puts("PASS: D3D12 WARP texture pixels; D3D11 upload; controller ray/click; held-click guard; binding preservation; exclusive game input; restored game actions; timeout/failure cleanup");
    return 0;
}
