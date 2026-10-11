#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "imgui/imgui.h"
#include "native-foveation-layout.h"
#include "native-runtime-health.h"
#include <fstream>
#include "native-ngx-parameters.h"
#include <atomic>
#include <cstdio>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <detours.h>
#include <mutex>
#include <memory>
#include <set>
#include <reshade.hpp>
#include <tlhelp32.h>
#include <tuple>
#include <thread>
#include <d3d12sdklayers.h>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using namespace native_foveation;
using Eval = unsigned(__cdecl *)(ID3D12GraphicsCommandList *, const void *,
                                 const NVSDK_NGX_Parameter *, void *);
using Create = unsigned(__cdecl *)(ID3D12GraphicsCommandList *, unsigned,
                                   NVSDK_NGX_Parameter *, void **);
using Release = unsigned(__cdecl *)(const void *);
using EvalVia = unsigned(__cdecl *)(Eval, ID3D12GraphicsCommandList *, const void *, const NVSDK_NGX_Parameter *, void *);
using ReleaseVia = unsigned(__cdecl *)(Release, const void *);
static EvalVia evaluate_via;
static ReleaseVia release_via;
static unsigned ProviderEvaluate(ID3D12GraphicsCommandList *c, const void *h, const NVSDK_NGX_Parameter *p, void *cb);
static Eval evaluate;
static unsigned ProviderEvaluate(ID3D12GraphicsCommandList *c, const void *h, const NVSDK_NGX_Parameter *p, void *cb) { return evaluate_via ? evaluate_via(evaluate, c, h, p, cb) : evaluate(c, h, p, cb); }

static Create create;
static Release release_feature, crop_release;
static HMODULE pinned, creation_owner;
static bool hook_attempted = false;
static std::recursive_mutex guard;
static std::atomic<ULONGLONG> vr_tick{0};
static unsigned preset = 0,
                layout = 0; // 0 separate-eye/mono features, 1 packed SBS
static unsigned custom_width=0, custom_height=0;
static unsigned neural_scale=85,feather_percent=10;
#ifdef NATIVE_FOVEATION_TEST
static bool native_processing=false;
static bool test_duplicate_backbuffer=false;
#else
static bool native_processing=true;
#endif
static bool feeder_vulkan=false;
#ifdef NATIVE_FOVEATION_TEST
static bool test_feeder_present=false;
static bool FeederPresent(){return test_feeder_present;}
#else
static bool FeederPresent(){return GetModuleHandleW(L"dlss5-feed.addon64")!=nullptr;}
#endif
static ULONGLONG config_tick = 0;
static std::wstring config_path;
static uint64_t cropped = 0, retained = 0;
struct PassStats{uint64_t cropped=0,full=0,last=0;unsigned eyes=0,width=0,height=0,region_width=0,region_height=0,work_width=0,work_height=0;bool active=false;std::string reason;};
static std::map<const void *,PassStats> pass_stats;
static void ReportPass(const void *handle){
#ifndef NATIVE_FOVEATION_TEST
  const auto &stats=pass_stats[handle];const auto total=stats.cropped+stats.full;
  if(total && total%600==0){char line[400];sprintf_s(line,"Native foveation v21 totals: feature=%p evaluations=%llu cropped=%llu full=%llu lastFullReason=%s",handle,total,stats.cropped,stats.full,stats.reason.empty()?"none":stats.reason.c_str());reshade::log::message(reshade::log::level::info,line);}
#endif
}
static std::string status = "Waiting for native neural rendering";
static void Status(const char *s) {
  if (status == s)
    return;
  status = s;
  static unsigned notices=0;static ULONGLONG last_notice=0;const auto now=GetTickCount64();
  if(notices>=16 && now-last_notice<2000)return;
  ++notices;last_notice=now;
#ifndef NATIVE_FOVEATION_TEST
  reshade::log::message(reshade::log::level::info, s);
#endif
}
static void Config(bool save = false) {
  if (config_path.empty()) {
    wchar_t p[32768];
    if (!GetModuleFileNameW(nullptr, p, 32768))
      return;
    auto slash = wcsrchr(p, L'\\');
    if (!slash)
      return;
    slash[1] = 0;
    config_path = std::wstring(p) + L"dlss5-native-foveation.ini";
  }
  if (save) {
    wchar_t n[24];
    swprintf_s(n, L"%u", preset);
    WritePrivateProfileStringW(L"NativeFoveation", L"Preset", n,
                               config_path.c_str());
    swprintf_s(n, L"%u", layout);
    WritePrivateProfileStringW(L"NativeFoveation", L"StereoLayout", n,
                               config_path.c_str());
    swprintf_s(n,L"%u",custom_width);WritePrivateProfileStringW(L"NativeFoveation",L"WidthPercent",n,config_path.c_str());
    swprintf_s(n,L"%u",custom_height);WritePrivateProfileStringW(L"NativeFoveation",L"HeightPercent",n,config_path.c_str());
    swprintf_s(n,L"%u",neural_scale);WritePrivateProfileStringW(L"NativeFoveation",L"NeuralScalePercent",n,config_path.c_str());
    swprintf_s(n,L"%u",feather_percent);WritePrivateProfileStringW(L"NativeFoveation",L"FeatherPercent",n,config_path.c_str());
    return;
  }
  auto now = GetTickCount64();
  if (config_tick && now - config_tick < 750)
    return;
  config_tick = now;
  preset = std::min(4u, GetPrivateProfileIntW(L"NativeFoveation", L"Preset", 0,
                                              config_path.c_str()));
  feeder_vulkan=GetPrivateProfileIntW(L"NativeFoveation",L"FeederVulkan",0,config_path.c_str())!=0;
  neural_scale=std::clamp(GetPrivateProfileIntW(L"NativeFoveation",L"NeuralScalePercent",85,config_path.c_str()),50u,100u);
  feather_percent=std::min(GetPrivateProfileIntW(L"NativeFoveation",L"FeatherPercent",10,config_path.c_str()),35u);
  custom_width=GetPrivateProfileIntW(L"NativeFoveation",L"WidthPercent",0,config_path.c_str());
  custom_height=GetPrivateProfileIntW(L"NativeFoveation",L"HeightPercent",0,config_path.c_str());
  layout =
      std::min(1u, GetPrivateProfileIntW(L"NativeFoveation", L"StereoLayout", 0,
                                         config_path.c_str()));
}
static std::pair<unsigned, unsigned> Percent() {
  if(custom_width && custom_height)return {std::clamp(custom_width,15u,90u),std::clamp(custom_height,15u,90u)};
  switch (preset) {
  case 1:
    return {50, 45};
  case 3:
    return {70, 50};
  case 4:
    return {75, 60};
  default:
    return {60, 50};
  }
}
static bool VRActive() {
  // A loaded OpenXR/OpenVR DLL is not proof of a VR session. Require a
  // recently presented headset runtime, or the active OpenXR pose layer.
  auto now = GetTickCount64(), tick = vr_tick.load();
  if (tick && now - tick < 2000)
    return true;
  using Active = bool(__cdecl *)();
  auto layer = GetModuleHandleW(L"DLSS5OpenXRPose.dll");
  auto active = layer ? reinterpret_cast<Active>(
                            GetProcAddress(layer, "DLSS5OpenXRPoseActive"))
                      : nullptr;
  return active && active();
}
// State observations must belong to this recording list. A resource state
// observed on a different list/queue is not evidence for our transitions.
static std::map<ID3D12GraphicsCommandList *,
                std::map<ID3D12Resource *, D3D12_RESOURCE_STATES>>
    states;
static std::map<ID3D12Resource *, D3D12_RESOURCE_STATES> initial_states;
static thread_local bool own_transitions = false;
// NR guides are single-mip, single-slice textures. Buffer transitions and
// unrelated mip/array resources need no per-list history in this addon.
static bool StateTexture(ID3D12Resource *resource){
  if(!resource)return false;const auto d=resource->GetDesc();
  return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.MipLevels==1 && d.DepthOrArraySize==1 && d.SampleDesc.Count==1;
}
static void InitResource(reshade::api::device *device,
                         const reshade::api::resource_desc &,
                         const reshade::api::subresource_data *,
                         reshade::api::resource_usage state,
                         reshade::api::resource resource) {
  if (device->get_api() != reshade::api::device_api::d3d12)
    return;
  std::lock_guard<std::recursive_mutex> lock(guard);
  auto value = static_cast<uint32_t>(state);
  if (!(value & 0x80000000u) && StateTexture(reinterpret_cast<ID3D12Resource *>(resource.handle)))
    initial_states[reinterpret_cast<ID3D12Resource *>(resource.handle)] =
        static_cast<D3D12_RESOURCE_STATES>(value);
}
static void Barrier(reshade::api::command_list *cmd, uint32_t n,
                    const reshade::api::resource *r,
                    const reshade::api::resource_usage *,
                    const reshade::api::resource_usage *next) {
  if (cmd->get_device()->get_api() != reshade::api::device_api::d3d12)
    return;
  std::lock_guard<std::recursive_mutex> lock(guard);
  for (uint32_t i = 0; i < n; ++i) {
    auto value = static_cast<uint32_t>(next[i]);
    auto resource = reinterpret_cast<ID3D12Resource *>(r[i].handle);
    if (!StateTexture(resource))
      continue;
    auto &s=states[reinterpret_cast<ID3D12GraphicsCommandList *>(cmd->get_native())];
    if (!own_transitions)
      initial_states.erase(resource);
    if (value & 0x80000000u)
      s.erase(resource);
    else
      s[resource] = static_cast<D3D12_RESOURCE_STATES>(value);
  }
}
// DFC records its private codec/NR transitions through native D3D12 lists.
// ReShade barrier events cover application proxy lists, not these private lists.
using NativeBarrierFn = void (STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *, UINT, const D3D12_RESOURCE_BARRIER *);
using NativeResetFn = HRESULT (STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *, ID3D12CommandAllocator *, ID3D12PipelineState *);
using NativeHeapsFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,UINT,ID3D12DescriptorHeap *const *);
static NativeHeapsFn native_heaps,heaps_target;
static thread_local bool own_bindings=false;
static std::map<ID3D12GraphicsCommandList *,std::vector<ID3D12DescriptorHeap *>> bound_heaps;
static void STDMETHODCALLTYPE NativeHeaps(ID3D12GraphicsCommandList *cmd,UINT count,ID3D12DescriptorHeap *const *heaps){
  native_heaps(cmd,count,heaps);
  if(own_bindings)return;
  std::lock_guard<std::recursive_mutex> lock(guard);
  if(count<=2 && (!count || heaps)){auto &saved=bound_heaps[cmd];saved.clear();if(count)saved.assign(heaps,heaps+count);}
  else bound_heaps.erase(cmd);
}
#include "native-compute-state.h"
static NativeBarrierFn native_barrier;
static NativeResetFn native_reset;
static NativeBarrierFn barrier_target;static NativeResetFn reset_target;
using NativeCommittedFn=HRESULT(STDMETHODCALLTYPE *)(ID3D12Device *,const D3D12_HEAP_PROPERTIES *,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC *,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE *,REFIID,void **);
static NativeCommittedFn native_committed;static bool committed_hooked=false;
using NativeExecuteFn=void(STDMETHODCALLTYPE *)(ID3D12CommandQueue *,UINT,ID3D12CommandList *const *);
static NativeExecuteFn native_execute;static bool execute_hooked=false;
static std::map<ID3D12GraphicsCommandList *,ID3D12CommandQueue *> list_queues;
struct SubmittedState{ID3D12CommandQueue *queue;D3D12_RESOURCE_STATES state;};
static std::map<ID3D12Resource *,SubmittedState> submitted_states;
static std::map<ID3D12GraphicsCommandList *,std::set<ID3D12Resource *>> uncertain_states;
static std::set<ID3D12Resource *> watched_resources;
// Recorded lists can outlive a feature or a slider value. Retire each crop
// only after all recorded uses are discarded by Reset or completed on GPU.
struct CropCompletion { ComPtr<ID3D12Fence> fence; bool signaled=false; };
struct CropUsage {
  std::set<ID3D12GraphicsCommandList *> recordings;
  std::vector<std::shared_ptr<CropCompletion>> submissions;
};
static std::map<ID3D12GraphicsCommandList *,std::vector<std::shared_ptr<CropUsage>>> crop_recordings;
static void RecordCropUse(ID3D12GraphicsCommandList *list,const std::shared_ptr<CropUsage> &use){
  if(use->recordings.insert(list).second)crop_recordings[list].push_back(use);
}
static void DiscardCropRecording(ID3D12GraphicsCommandList *cmd){
  auto it=crop_recordings.find(cmd);if(it!=crop_recordings.end()){
    for(auto &use:it->second)use->recordings.erase(cmd);crop_recordings.erase(it);
  }
}
static bool CropComplete(CropUsage &use){
  auto &submissions=use.submissions;
  submissions.erase(std::remove_if(submissions.begin(),submissions.end(),[](const auto &point){
    if(!point->signaled || !point->fence)return false;
    const auto value=point->fence->GetCompletedValue();
    return value!=UINT64_MAX && value>=1;
  }),submissions.end());
  return use.recordings.empty() && submissions.empty();
}
static void STDMETHODCALLTYPE NativeExecute(ID3D12CommandQueue *queue,UINT count,ID3D12CommandList *const *lists){
  std::map<ID3D12Resource *,D3D12_RESOURCE_STATES> pending;
  {std::lock_guard<std::recursive_mutex> lock(guard);for(UINT i=0;i<count;++i){auto list=reinterpret_cast<ID3D12GraphicsCommandList *>(lists[i]);list_queues[list]=queue;auto found=states.find(list);if(found!=states.end())for(const auto &entry:found->second)if(watched_resources.count(entry.first))pending[entry.first]=entry.second;auto unknown=uncertain_states.find(list);if(unknown!=uncertain_states.end())for(auto resource:unknown->second){submitted_states.erase(resource);pending.erase(resource);}}}
  // Install an incomplete point before submission so a concurrent resize
  // cannot release the feature between Execute and the completion signal.
  auto point=std::make_shared<CropCompletion>();bool used=false;
  {std::lock_guard<std::recursive_mutex> lock(guard);
    for(UINT i=0;i<count;++i){auto list=reinterpret_cast<ID3D12GraphicsCommandList *>(lists[i]);auto it=crop_recordings.find(list);
      if(it!=crop_recordings.end())for(auto &use:it->second){
        CropComplete(*use);use->submissions.push_back(point);used=true;
      }
    }
  }
  native_execute(queue,count,lists);
  if(used){ComPtr<ID3D12Device> device;ComPtr<ID3D12Fence> fence;
    const bool signaled=SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))) &&
      SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))) && SUCCEEDED(queue->Signal(fence.Get(),1));
    std::lock_guard<std::recursive_mutex> lock(guard);point->fence=fence;point->signaled=signaled;
  }
  {std::lock_guard<std::recursive_mutex> lock(guard);for(const auto &entry:pending)submitted_states[entry.first]={queue,entry.second};}
}

static HRESULT STDMETHODCALLTYPE NativeCommitted(ID3D12Device *device,const D3D12_HEAP_PROPERTIES *heap,D3D12_HEAP_FLAGS flags,const D3D12_RESOURCE_DESC *desc,D3D12_RESOURCE_STATES initial,const D3D12_CLEAR_VALUE *clear,REFIID iid,void **out){
  auto result=native_committed(device,heap,flags,desc,initial,clear,iid,out);
  if(SUCCEEDED(result)&&out&&*out&&desc&&desc->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&desc->MipLevels==1&&desc->DepthOrArraySize==1&&desc->SampleDesc.Count==1){
    ComPtr<ID3D12Resource> resource;if(SUCCEEDED(reinterpret_cast<IUnknown *>(*out)->QueryInterface(IID_PPV_ARGS(&resource)))){
      std::lock_guard<std::recursive_mutex> lock(guard);initial_states[resource.Get()]=initial;
      submitted_states.erase(resource.Get());watched_resources.erase(resource.Get());
      for(auto &entry:states)entry.second.erase(resource.Get());for(auto &entry:uncertain_states)entry.second.erase(resource.Get());
    }
  }return result;
}

static void STDMETHODCALLTYPE NativeBarrier(ID3D12GraphicsCommandList *cmd, UINT count, const D3D12_RESOURCE_BARRIER *barriers) {
  native_barrier(cmd, count, barriers);
  std::lock_guard<std::recursive_mutex> lock(guard);
  for (UINT i=0; i<count; ++i) {
    const auto &b=barriers[i];
    if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || !b.Transition.pResource)continue;
    auto resource=b.Transition.pResource;
    if(!StateTexture(resource))continue;
    if(!own_transitions)initial_states.erase(resource);
    // Whole-resource state is only proven by an all-subresources transition.
    bool complete=b.Transition.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if(!complete && b.Transition.Subresource==0){
      const auto d=resource->GetDesc();
      // These color formats have one plane; mip 0 is the entire texture.
      const bool single_plane=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT || d.Format==DXGI_FORMAT_R11G11B10_FLOAT ||
        d.Format==DXGI_FORMAT_R8G8B8A8_UNORM || d.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS ||
        d.Format==DXGI_FORMAT_R10G10B10A2_UNORM || d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT;
      complete=single_plane && d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.MipLevels==1 && d.DepthOrArraySize==1;
    }
    if(!complete || b.Flags==D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY){states[cmd].erase(resource);uncertain_states[cmd].insert(resource);submitted_states.erase(resource);}
    else {states[cmd][resource]=b.Transition.StateAfter;uncertain_states[cmd].erase(resource);}
  }
}
static HRESULT STDMETHODCALLTYPE NativeReset(ID3D12GraphicsCommandList *cmd, ID3D12CommandAllocator *allocator, ID3D12PipelineState *pipeline) {
  auto result=native_reset(cmd,allocator,pipeline);
  if(SUCCEEDED(result)){std::lock_guard<std::recursive_mutex> lock(guard);states.erase(cmd);uncertain_states.erase(cmd);bound_heaps.erase(cmd);
    DiscardCropRecording(cmd);compute_bindings.erase(cmd);if(pipeline)compute_bindings[cmd].pipeline=pipeline;
  }
  return result;
}
static LONG NativeObservationTransaction(bool attach, bool list_hooks=true) {
  LONG result=DetourTransactionBegin();if(result!=NO_ERROR)return result;
  std::vector<HANDLE> threads;
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
  if(snapshot==INVALID_HANDLE_VALUE){DetourTransactionAbort();return GetLastError();}
  THREADENTRY32 entry{sizeof(entry)};
  for(BOOL ok=Thread32First(snapshot,&entry);ok;ok=Thread32Next(snapshot,&entry))
    if(entry.th32OwnerProcessID==GetCurrentProcessId() && entry.th32ThreadID!=GetCurrentThreadId()) {
      HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
      if(thread){threads.push_back(thread);result=DetourUpdateThread(thread);if(result!=NO_ERROR)break;}
    }
  CloseHandle(snapshot);
  if(result==NO_ERROR && list_hooks && native_barrier)result=attach?DetourAttach(reinterpret_cast<void **>(&native_barrier),NativeBarrier):DetourDetach(reinterpret_cast<void **>(&native_barrier),NativeBarrier);
  if(result==NO_ERROR && list_hooks && native_heaps)result=attach?DetourAttach(reinterpret_cast<void **>(&native_heaps),NativeHeaps):DetourDetach(reinterpret_cast<void **>(&native_heaps),NativeHeaps);
  if(result==NO_ERROR && list_hooks)result=ComputeTransaction(attach);
  if(result==NO_ERROR && list_hooks && native_reset)result=attach?DetourAttach(reinterpret_cast<void **>(&native_reset),NativeReset):DetourDetach(reinterpret_cast<void **>(&native_reset),NativeReset);
  if(result==NO_ERROR && native_committed && (attach?!committed_hooked:committed_hooked))result=attach?DetourAttach(reinterpret_cast<void **>(&native_committed),NativeCommitted):DetourDetach(reinterpret_cast<void **>(&native_committed),NativeCommitted);
  if(result==NO_ERROR && native_execute && (attach?!execute_hooked:execute_hooked))result=attach?DetourAttach(reinterpret_cast<void **>(&native_execute),NativeExecute):DetourDetach(reinterpret_cast<void **>(&native_execute),NativeExecute);
  if(result==NO_ERROR)result=DetourTransactionCommit();else DetourTransactionAbort();
  if(result==NO_ERROR){
    if(native_committed)committed_hooked=attach;
    if(native_execute)execute_hooked=attach;
  }
  for(auto thread:threads)CloseHandle(thread);
  return result;
}
template<class T> static ComPtr<T> UnwrappedNative(T *object) {
  ComPtr<T> result;
  if(!object)return result;
  // Modified ReShade and Streamline use different unwrap contracts.
  static const GUID ids[]={
    {0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}},
    {0xadec44e2,0x61f0,0x45c3,{0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}}};
  result=object;
  for(unsigned depth=0;depth<4;++depth){bool changed=false;
    for(const auto &id:ids){ComPtr<T> raw;
      if(SUCCEEDED(result->QueryInterface(id,reinterpret_cast<void **>(raw.GetAddressOf())))&&raw&&raw.Get()!=result.Get()){
        // Verify the interface type before using the returned object's vtable.
        ComPtr<T> typed;if(SUCCEEDED(raw.As(&typed))&&typed){result=typed;changed=true;break;}
      }
    }
    if(!changed)break;
  }
  return result;
}
static std::atomic<bool> diagnostic_stop{false};
// No joinable thread destructor during process-exit DLL detach under loader lock.
static std::thread *diagnostic_thread=nullptr;
static void EnableGpuDiagnostics(){
#ifndef NATIVE_FOVEATION_TEST
  Config();
  // Forced DRED instruments all game GPU commands. Keep it opt-in.
  if(!GetPrivateProfileIntW(L"NativeFoveation",L"GpuDiagnostics",0,config_path.c_str()))return;
  ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
  if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&settings)))){
    settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  }
#endif
}
static void StartGpuDiagnostics(ID3D12Device *device){
#ifndef NATIVE_FOVEATION_TEST
  if(diagnostic_thread)return;
  ComPtr<ID3D12Device> held=device;diagnostic_stop=false;
  diagnostic_thread=new std::thread([held](){
    while(!diagnostic_stop){auto reason=held->GetDeviceRemovedReason();
      if(FAILED(reason)){
        char line[800];sprintf_s(line,"Native foveation v21 GPU removed: HRESULT=0x%08X",unsigned(reason));reshade::log::message(reshade::log::level::error,line);
        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if(SUCCEEDED(held.As(&dred))){D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
          auto br=dred->GetAutoBreadcrumbsOutput(&breadcrumbs),fr=dred->GetPageFaultAllocationOutput(&fault);
          sprintf_s(line,"Native foveation v21 DRED: breadcrumbs=0x%08X pageFault=0x%08X address=0x%llX",unsigned(br),unsigned(fr),fault.PageFaultVA);reshade::log::message(reshade::log::level::error,line);
          unsigned count=0;for(auto node=breadcrumbs.pHeadAutoBreadcrumbNode;SUCCEEDED(br)&&node&&count++<64;node=node->pNext){
            const auto done=node->pLastBreadcrumbValue?*node->pLastBreadcrumbValue:0;
            if(done>=node->BreadcrumbCount)continue;
            sprintf_s(line,"Native foveation v21 DRED incomplete list=%p queue=%p name=%s completed=%u total=%u nextOp=%u",node->pCommandList,node->pCommandQueue,node->pCommandListDebugNameA?node->pCommandListDebugNameA:"unnamed",done,node->BreadcrumbCount,node->pCommandHistory&&done<node->BreadcrumbCount?unsigned(node->pCommandHistory[done]):0);reshade::log::message(reshade::log::level::error,line);
          }
        }return;
      }Sleep(100);
    }
  });
#endif
}
static void StopGpuDiagnostics(){diagnostic_stop=true;if(diagnostic_thread){if(diagnostic_thread->joinable())diagnostic_thread->join();delete diagnostic_thread;diagnostic_thread=nullptr;}}
static bool ObserveNativeDevice(ID3D12Device *device){
  if(committed_hooked)return true;if(!device)return false;
  ComPtr<ID3D12Device> raw;const GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
  if(SUCCEEDED(device->QueryInterface(unwrapped,reinterpret_cast<void **>(raw.GetAddressOf())))&&raw)device=raw.Get();
  ComPtr<ID3D12CommandQueue> probe;D3D12_COMMAND_QUEUE_DESC description{};
  if(FAILED(device->CreateCommandQueue(&description,IID_PPV_ARGS(&probe))))return false;
  // Install list observation before the first neural feature allocates or
  // transitions its private output. Waiting until Evaluate misses that setup.
  ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
  if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))) ||
     FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))))return false;
  if(FAILED(list->Close()))return false;
  probe=UnwrappedNative(probe.Get());list=UnwrappedNative(list.Get());
  const bool had_list_hooks=native_barrier!=nullptr;
  if(!had_list_hooks){auto table=*reinterpret_cast<void ***>(list.Get());
    native_barrier=reinterpret_cast<NativeBarrierFn>(table[26]);
    native_reset=reinterpret_cast<NativeResetFn>(table[10]);barrier_target=native_barrier;reset_target=native_reset;native_heaps=reinterpret_cast<NativeHeapsFn>(table[28]);heaps_target=native_heaps;BindComputeTargets(table);}
  native_execute=reinterpret_cast<NativeExecuteFn>((*reinterpret_cast<void ***>(probe.Get()))[10]);
  native_committed=reinterpret_cast<NativeCommittedFn>((*reinterpret_cast<void ***>(device))[27]);
  if(NativeObservationTransaction(true, !had_list_hooks)!=NO_ERROR){native_committed=nullptr;native_execute=nullptr;if(!had_list_hooks){native_barrier=nullptr;native_reset=nullptr;native_heaps=nullptr;ClearComputeTargets();}return false;}
  StartGpuDiagnostics(device);
  Status("Native foveation v21: creation, barrier, reset and submission tracking armed before neural setup");return true;
}
static bool ObserveNativeList(ID3D12GraphicsCommandList *cmd) {
  if(!cmd)return false;
  compute_lists.insert(cmd);
  if(native_barrier){auto table=*reinterpret_cast<void ***>(cmd);
    if(reinterpret_cast<NativeBarrierFn>(table[26])==barrier_target && reinterpret_cast<NativeResetFn>(table[10])==reset_target)return true;
    Status("Native foveation: actual NR command list differs from observed native target");
    return false;
  }
  // Windows SDK ID3D12GraphicsCommandList ABI: IUnknown(3), ID3D12Object(4),
  // DeviceChild(1), CommandList(1), Close(1), Reset at 10; ResourceBarrier at 26.
  auto table=*reinterpret_cast<void ***>(cmd);
  native_barrier=reinterpret_cast<NativeBarrierFn>(table[26]);
  native_reset=reinterpret_cast<NativeResetFn>(table[10]);barrier_target=native_barrier;reset_target=native_reset;native_heaps=reinterpret_cast<NativeHeapsFn>(table[28]);heaps_target=native_heaps;BindComputeTargets(table);
  LONG result=NativeObservationTransaction(true);
  if(result!=NO_ERROR){native_barrier=nullptr;native_reset=nullptr;native_heaps=nullptr;ClearComputeTargets();return false;}
  Status("Native foveation observing private D3D12 transitions; awaiting qualified buffers");
  return true;
}
static void StopNativeObservation() {
  if(!native_barrier && !committed_hooked && !execute_hooked)return;
  if(NativeObservationTransaction(false)==NO_ERROR){native_barrier=nullptr;native_reset=nullptr;native_heaps=nullptr;ClearComputeTargets();native_committed=nullptr;native_execute=nullptr;}
}
static void InitDevice(reshade::api::device *device) {
  if(device->get_api()!=reshade::api::device_api::d3d12)return;
  std::lock_guard<std::recursive_mutex> lock(guard);
  ObserveNativeDevice(reinterpret_cast<ID3D12Device *>(device->get_native()));
}
static void ResetList(reshade::api::command_list *cmd) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  auto native=reinterpret_cast<ID3D12GraphicsCommandList *>(cmd->get_native());states.erase(native);bound_heaps.erase(native);DiscardCropRecording(native);
}
static void DestroyList(reshade::api::command_list *cmd){
  std::lock_guard<std::recursive_mutex> lock(guard);ResetList(cmd);
  auto native=reinterpret_cast<ID3D12GraphicsCommandList *>(cmd->get_native());compute_bindings.erase(native);compute_lists.erase(native);list_queues.erase(native);uncertain_states.erase(native);
}
static void DestroyResource(reshade::api::device *, reshade::api::resource r) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  auto resource = reinterpret_cast<ID3D12Resource *>(r.handle);
  initial_states.erase(resource);submitted_states.erase(resource);watched_resources.erase(resource);
  for (auto &entry : states)
    entry.second.erase(resource);
}
static bool Texture(ID3D12Resource *r) {
  if (!r)
    return false;
  auto d = r->GetDesc();
  return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
         d.Width <= UINT_MAX && d.Width && d.Height &&
         d.DepthOrArraySize == 1 && d.MipLevels == 1 && d.SampleDesc.Count == 1;
}
static bool ReadRect(const NVSDK_NGX_Parameter *p, const char *name,
                     ID3D12Resource *r, Rect &rect) {
  if (!Texture(r))
    return false;
  auto d = r->GetDesc();
  rect = {0, 0, static_cast<unsigned>(d.Width), d.Height};
  char key[128];
  unsigned v;
  const char *suffix[] = {"BaseX", "BaseY", "Width", "Height"};
  unsigned *values[] = {&rect.x, &rect.y, &rect.w, &rect.h};
  for (int i = 0; i < 4; ++i) {
    sprintf_s(key, "DLSSNR.%sSubrect%s", name, suffix[i]);
    if (p->Get(key, &v) == 1) {
      if (i < 2 || v)
        *values[i] = v;
    }
  }
  return valid(rect, static_cast<unsigned>(d.Width), d.Height);
}
static void WriteRect(NativeParameters &p, const char *name, Rect r) {
  char key[128];
  const char *suffix[] = {"BaseX", "BaseY", "Width", "Height"};
  unsigned values[] = {r.x, r.y, r.w, r.h};
  for (int i = 0; i < 4; ++i) {
    sprintf_s(key, "DLSSNR.%sSubrect%s", name, suffix[i]);
    p.Set(key, values[i]);
  }
}
struct Guide {
  const char *name;
  ID3D12Resource *resource;
  Rect rect;
};
static bool Guides(const NVSDK_NGX_Parameter *p, std::vector<Guide> &guides) {
  for (auto name : {"Color", "Output", "Depth", "MVec", "ControlMask", "UI",
                    "UIAlpha", "Backbuffer", "BidirectionalDistortionField"}) {
    char key[128];
    sprintf_s(key, "DLSSNR.%s", name);
    ID3D12Resource *r = nullptr;
    p->Get(key, &r);
    if (!r) {
      if (!strcmp(name, "Color") || !strcmp(name, "Output") ||
          !strcmp(name, "Depth") || !strcmp(name, "MVec"))
        return false;
      continue;
    }
    Rect rect;
    if (!ReadRect(p, name, r, rect))
      return false;
    guides.push_back({name, r, rect});
  }
  return true;
}
// Qualified cropped NR processing; tests may select the legacy copy path.
static bool experimental_crop = true;
static bool test_feather = false;
struct GPU {
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12DescriptorHeap> heap;
  ComPtr<ID3D12Resource> color, output;
};
static std::map<std::pair<ID3D12Resource *, ID3D12Resource *>, GPU> gpus;
static const char *feather_shader = R"(
Texture2D<float4> Color:register(t0);RWTexture2D<float4> Output:register(u0);
cbuffer Geometry:register(b0){uint4 rect;uint feather;uint compose;uint eyeShift;uint reserved;};
[numthreads(8,8,1)]void Main(uint3 id:SV_DispatchThreadID){
 uint2 p;uint2 local;
 if(compose){uint width,height;Output.GetDimensions(width,height);if(id.x>=width||id.y>=height)return;p=id.xy;uint2 q=p;if(eyeShift && q.x>=rect.x+eyeShift)q.x-=eyeShift;
 if(q.x<rect.x||q.y<rect.y||q.x>=rect.x+rect.z||q.y>=rect.y+rect.w){Output[p]=Color.Load(int3(p,0));return;}local=q-rect.xy;
 }else{if(id.x>=rect.z||id.y>=rect.w)return;p=rect.xy+id.xy;local=id.xy;}
 float edge=min(min(local.x,rect.z-1-local.x),min(local.y,rect.w-1-local.y));
 float weight=smoothstep(0.0,float(feather),edge);
 Output[p]=lerp(Color.Load(int3(p,0)),Output[p],weight);
})";
static bool Prepare(ID3D12Resource *color, ID3D12Resource *output, GPU *&gpu) {
  auto key = std::make_pair(color, output);
  auto found = gpus.find(key);
  if (found != gpus.end()) {
    gpu = &found->second;
    return true;
  }
  // Keep descriptors/resources alive for the lifetime of the bridge. No live
  // resize or preset change frees in-flight GPU work.
  if (gpus.size() >= 16)
    return false;
  auto a = color->GetDesc(), b = output->GetDesc();
  if (a.Width != b.Width || a.Height != b.Height || a.Format != b.Format ||
      !(b.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ||
      (a.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
    return false;
  ComPtr<ID3D12Device> device;
  if (FAILED(color->GetDevice(IID_PPV_ARGS(&device))))
    return false;
  const GUID unwrapped = {0x7f2c9a11,
                          0x3b4e,
                          0x4d6a,
                          {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};
  ComPtr<ID3D12Device> native;
  if (SUCCEEDED(device->QueryInterface(
          unwrapped, reinterpret_cast<void **>(native.GetAddressOf()))) &&
      native)
    device = native;
  D3D12_FEATURE_DATA_FORMAT_SUPPORT format{a.Format};
  if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &format,
                                         sizeof(format))) ||
      !(format.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) ||
      !(format.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) ||
      !(format.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
    return false;
  GPU s;
  D3D12_DESCRIPTOR_RANGE ranges[2] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1}};
  D3D12_ROOT_PARAMETER parameters[2]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {2, ranges};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants = {0, 0, 8};
  D3D12_ROOT_SIGNATURE_DESC desc{2, parameters, 0, nullptr,
                                 D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> blob, error;
  if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &blob, &error)) ||
      FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                         blob->GetBufferSize(),
                                         IID_PPV_ARGS(&s.root))))
    return false;
  if (FAILED(D3DCompile(feather_shader, strlen(feather_shader), nullptr,
                        nullptr, nullptr, "Main", "cs_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error)))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
  pd.pRootSignature = s.root.Get();
  pd.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
  if (FAILED(
          device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&s.pipeline))))
    return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2,
                                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&s.heap))))
    return false;
  auto cpu = s.heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = a.Format;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(color, &srv, cpu);
  cpu.ptr += device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = b.Format;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device->CreateUnorderedAccessView(output, nullptr, &uav, cpu);
  s.color = color;
  s.output = output;
  gpu = &gpus.emplace(key, std::move(s)).first->second;
  return true;
}
static void Transition(ID3D12GraphicsCommandList *cmd, ID3D12Resource *r,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after) {
  if (before == after)
    return;
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  cmd->ResourceBarrier(1, &b);
}
static void UAV(ID3D12GraphicsCommandList *cmd, ID3D12Resource *r) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  b.UAV.pResource = r;
  cmd->ResourceBarrier(1, &b);
}
#include "native-crop-processing.h"
struct CropImages {CropProcessing processing;ComPtr<ID3D12Resource> color,output;std::map<std::string,ComPtr<ID3D12Resource>> ancillary;};
// Only uncertain initialization failures retain images for process lifetime.
static std::vector<CropImages> failed_initialization_images;
static bool AllocateCrop(ID3D12Resource *source,unsigned w,unsigned h,CropImages &images) {
  ComPtr<ID3D12Device> device,native;if(FAILED(source->GetDevice(IID_PPV_ARGS(&device))))return false;
  const GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
  if(SUCCEEDED(device->QueryInterface(unwrapped,reinterpret_cast<void **>(native.GetAddressOf())))&&native)device=native;
  auto desc=source->GetDesc();desc.Width=w;desc.Height=h;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&images.color))) ||
     FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&images.output))))return false;
  return true;
}
static bool AllocateAncillary(ID3D12Resource *source,Rect crop,ComPtr<ID3D12Resource> &image) {
  ComPtr<ID3D12Device> device,native;if(FAILED(source->GetDevice(IID_PPV_ARGS(&device))))return false;
  const GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
  if(SUCCEEDED(device->QueryInterface(unwrapped,reinterpret_cast<void **>(native.GetAddressOf())))&&native)device=native;
  auto desc=source->GetDesc();if(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)return false;
  desc.Width=crop.w;desc.Height=crop.h;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&image))))return false;
  return true;
}
static void CropParameters(NativeParameters &p,const CropImages &images,unsigned w,unsigned h) {
  p.Set("DLSSNR.Color",images.color.Get());p.Set("DLSSNR.Output",images.output.Get());
  WriteRect(p,"Color",{0,0,w,h});WriteRect(p,"Output",{0,0,w,h});
  for(const auto &entry:images.ancillary){
    const auto desc=entry.second->GetDesc();std::string key="DLSSNR."+entry.first;
    p.Set(key.c_str(),entry.second.Get());WriteRect(p,entry.first.c_str(),{0,0,unsigned(desc.Width),desc.Height});
  }
  p.Set("DLSSNR.Width",w);p.Set("DLSSNR.Height",h);
  p.Set("Width",w);p.Set("Height",h);p.Set("OutWidth",w);p.Set("OutHeight",h);
}
// CreateFeature saves/restores descriptor heaps. Starfield crashes when it
// runs on DFC's already-bound producer list. Use a fresh list and fence it.
struct CropInitialization {
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
};
static std::vector<CropInitialization> pending_initializations;
static unsigned CreateCropFeature(ID3D12GraphicsCommandList *producer,NativeParameters &params,void **handle){
  *handle=nullptr;ComPtr<ID3D12Device> device;
  if(FAILED(producer->GetDevice(IID_PPV_ARGS(&device))))return 0xbad00005;
  // Keep the provider device proxy: it owns descriptor-handle translation.
  // Do not guess resource node selection on multi-adapter devices.
  if(!device || device->GetNodeCount()!=1 || pending_initializations.size()>=8)return 0xbad00005;
  CropInitialization init;D3D12_COMMAND_QUEUE_DESC q{};
  if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&init.queue))) ||
     FAILED(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&init.allocator))) ||
     FAILED(device->CreateCommandList(0,q.Type,init.allocator.Get(),nullptr,IID_PPV_ARGS(&init.list))) ||
     FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&init.fence))))return 0xbad00005;
  // Keep its matching recording proxy for CPU descriptors, clears and copies.
  init.list->SetName(L"DLSS5 cropped NR initialization (isolated)");
  own_bindings=true;const auto result=create(init.list.Get(),18,&params,handle);own_bindings=false;
  if(result!=1 || !*handle)return result==1?0xbad00005:result;
  if(FAILED(init.list->Close())){(crop_release?crop_release:release_feature)(*handle);*handle=nullptr;return 0xbad00005;}
  ID3D12CommandList *lists[]={init.list.Get()};init.queue->ExecuteCommandLists(1,lists);
  const auto signaled=init.queue->Signal(init.fence.Get(),1);
  HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  const auto armed=event?init.fence->SetEventOnCompletion(1,event):E_FAIL;
  const auto waited=SUCCEEDED(signaled)&&SUCCEEDED(armed)?WaitForSingleObject(event,15000):WAIT_FAILED;
  if(event)CloseHandle(event);
  if(waited!=WAIT_OBJECT_0 || FAILED(device->GetDeviceRemovedReason())){
    // Submitted commands can still reference these objects. Retain them and
    // never evaluate/release an in-flight feature after a timeout.
    pending_initializations.push_back(std::move(init));*handle=nullptr;return 0xbad00005;
  }
  return result;
}
struct Variant {
  unsigned w, h, layout;
  ID3D12Resource *color = nullptr, *output = nullptr;
  unsigned region_w=0,region_h=0,work_scale=100;bool processing=false;
  std::vector<void *> handles;
  std::vector<CropImages> images;
  std::shared_ptr<CropUsage> usage=std::make_shared<CropUsage>();
  bool reset = true, failed = false;
  uint64_t failed_at=0;bool retryable=false;
  std::string failure="Native foveation retained full frame: cropped feature unavailable";
};
struct Feature {
  unsigned recovery_attempts=0;
  std::vector<Variant> variants;
  ULONGLONG last = 0;
  unsigned last_preset = 0;
};
static std::map<const void *, Feature> features;
static std::vector<Variant> retired_variants;
static void CollectCrops(){
  for(auto it=retired_variants.begin();it!=retired_variants.end();){
    if(!CropComplete(*it->usage)){++it;continue;}
    for(auto h:it->handles)(crop_release?crop_release:release_feature)(h);
    it=retired_variants.erase(it);
  }
}
static void RetireCrops(Feature &feature){
  for(auto &variant:feature.variants)retired_variants.push_back(std::move(variant));
  feature.variants.clear();CollectCrops();
}

static unsigned __cdecl Evaluate(ID3D12GraphicsCommandList *cmd,
                                 const void *handle,
                                 const NVSDK_NGX_Parameter *p, void *callback) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  Config();
  CollectCrops();
  auto provider_cmd=cmd;
  auto normal = [&](const char *reason) {
    ++retained;
    if(handle&&(pass_stats.count(handle)||pass_stats.size()<128)){auto &stats=pass_stats[handle];++stats.full;stats.reason=reason;stats.last=GetTickCount64();stats.active=false;ReportPass(handle);}
    Status(reason);
    auto it = features.find(handle);
    if (it != features.end()) {
      for (auto &v : it->second.variants)
        v.reset = true;
      // The original full-frame history has not been evaluated while
      // cropping. Reset it once when returning to the normal route.
      if (it->second.last && p) {
        it->second.last = 0;
        NativeParameters params(p);
        params.Set("DLSSNR.Reset", 1u);
        return ProviderEvaluate(provider_cmd, handle, &params, callback);
      }
    }
    return ProviderEvaluate(provider_cmd, handle, p, callback);
  };
  if (!preset || !VRActive() || (FeederPresent() && !feeder_vulkan))
    return normal(
        "Native foveation idle: disabled, flat runtime, or Feeder route");
  auto native_list=UnwrappedNative(cmd);
  if(native_list)cmd=native_list.Get();
  if (cmd && !ObserveNativeList(cmd))
    return normal("Native foveation retained full frame: native state observation unavailable");
  if (!p || callback || !cmd || !handle)
    return normal("Native foveation retained full frame: unsupported NGX call");
  std::vector<Guide> guides;
  if (!Guides(p, guides))
    return normal("Native foveation retained full frame: unsupported or "
                  "missing native NR guides");
  for(const char *name:{"DLSSNR.MVecScaleX","DLSSNR.MVecScaleY","DLSSNR.Jitter.Offset.X","DLSSNR.Jitter.Offset.Y"}){
    float value=0;if(p->Get(name,&value)==1 && !finite_guide_value(value))
      return normal("Native foveation retained full frame: non-finite motion/jitter guide");
  }
  for(const auto &guide:guides)watched_resources.insert(guide.resource);
  auto &color = guides[0], &output = guides[1];
  auto a = color.rect, b = output.rect;
  const auto color_desc=color.resource->GetDesc(),output_desc=output.resource->GetDesc();
  // CopyTextureRegion performs no format conversion. DFC's HDR codec may expose
  // a different color/output contract; never submit an incompatible copy.
  if(color_desc.Format!=output_desc.Format ||
     (color_desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ||
     (output_desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
    char reason[240];sprintf_s(reason,"Native foveation retained full frame: incompatible copy formats (color=%u flags=0x%X; output=%u flags=0x%X)",unsigned(color_desc.Format),unsigned(color_desc.Flags),unsigned(output_desc.Format),unsigned(output_desc.Flags));
    return normal(reason);
  }
  if(cmd->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT && cmd->GetType()!=D3D12_COMMAND_LIST_TYPE_COMPUTE)
    return normal("Native foveation retained full frame: unsupported command-list type");
  // Pre-upscale and post-upscale providers are both 1:1 NR jobs. The game's
  // separate native SR job is never intercepted (we hook only dlssnr.dll).
  if (a.w != b.w || a.h != b.h || a.x != b.x || a.y != b.y ||
      color.resource == output.resource)
    return normal("Native foveation retained full frame: incompatible native "
                  "NR color/output");
  auto observed = [&](ID3D12Resource *resource, D3D12_RESOURCE_STATES &state) {
    auto list = states.find(cmd);
    if (list != states.end()) {
      auto r = list->second.find(resource);
      if (r != list->second.end()) {
        state = r->second;
        return true;
      }
    }
    // A newly created resource which has never undergone an observed
    // transition still has its explicit creation state (not COMMON).
    auto uncertain=uncertain_states.find(cmd);if(uncertain!=uncertain_states.end()&&uncertain->second.count(resource))return false;
    auto prior=submitted_states.find(resource);auto queue=list_queues.find(cmd);
    if(prior!=submitted_states.end()&&queue!=list_queues.end()&&prior->second.queue==queue->second){state=prior->second.state;return true;}
    auto initial = initial_states.find(resource);
    if (initial == initial_states.end())
      return false;
    state = initial->second;
    return true;
  };
  D3D12_RESOURCE_STATES color_state={}, output_state={};
  const bool color_known=observed(color.resource,color_state), output_known=observed(output.resource,output_state);
  if (!color_known || !output_known ||
      !(color_state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
      output_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
    char reason[240];sprintf_s(reason,"Native foveation retained full frame: color/output states not qualified (color known=%d state=0x%X; output known=%d state=0x%X)",color_known,unsigned(color_state),output_known,unsigned(output_state));
    return normal(reason);
  }
  auto percent = Percent();
  std::vector<Rect> crops;
  const unsigned eyes = layout ? 2 : 1;
  if (layout && (a.w % 2))
    return normal(
        "Native foveation retained full frame: odd packed stereo width");
  for (unsigned eye = 0; eye < eyes; ++eye) {
    Rect viewport = a;
    if (layout) {
      viewport.w /= 2;
      viewport.x += eye * viewport.w;
    }
    auto c = crop(viewport, percent.first, percent.second);
    if (!valid(c, static_cast<unsigned>(color.resource->GetDesc().Width),
               color.resource->GetDesc().Height))
      return normal("Native foveation retained full frame: crop too small");
    crops.push_back(c);
  }
  if (!experimental_crop) {
    auto result = ProviderEvaluate(provider_cmd, handle, p, callback);
    if (result != 1) { Status("Native foveation: original neural runtime evaluation failed"); return result; }
    // DFC resumes recording on this exact list. Changing heaps/root bindings
    // here invalidates its cached compute state. Copies preserve those bindings.
    own_transitions=true;UAV(cmd,output.resource);
    Transition(cmd,color.resource,color_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(cmd,output.resource,output_state,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=color.resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=output.resource;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    auto copy=[&](unsigned x,unsigned y,unsigned w,unsigned h){if(!w||!h)return;D3D12_BOX box={x,y,0,x+w,y+h,1};cmd->CopyTextureRegion(&to,x,y,0,&from,&box);};
    for(unsigned eye=0;eye<eyes;++eye){Rect viewport=a;if(layout){viewport.w/=2;viewport.x+=eye*viewport.w;}const auto c=crops[eye];
      copy(viewport.x,viewport.y,viewport.w,c.y-viewport.y);
      copy(viewport.x,c.y+c.h,viewport.w,viewport.y+viewport.h-c.y-c.h);
      copy(viewport.x,c.y,c.x-viewport.x,c.h);
      copy(c.x+c.w,c.y,viewport.x+viewport.w-c.x-c.w,c.h);
    }
    Transition(cmd,output.resource,D3D12_RESOURCE_STATE_COPY_DEST,output_state);
    Transition(cmd,color.resource,D3D12_RESOURCE_STATE_COPY_SOURCE,color_state);own_transitions=false;
    ++cropped;Status("Native foveation active v6: binding-preserving periphery copies (full-frame NR compute)");return result;
  }
  GPU *gpu = nullptr;
  if (test_feather && !Prepare(color.resource, output.resource, gpu))
    return normal("Native foveation retained full frame: unsupported format or resource cache limit");
  if (features.size() >= 64 && !features.count(handle))
    return normal("Native foveation retained full frame: feature cache limit");
  LearnComputeRoot(cmd);
  SavedCompute saved_compute(cmd);
  // A missed/incomplete recording must not destroy an already prepared
  // blended history and reallocate a plain history. Preserve it for recovery.
  if(native_processing && !saved_compute.ready && !test_feather){
    auto existing=features.find(handle);if(existing!=features.end())for(const auto &v:existing->second.variants)
      if(v.processing && v.region_w==crops[0].w && v.region_h==crops[0].h && v.layout==layout && v.color==color.resource && v.output==output.resource)
        return normal("Native foveation: compute state temporarily unavailable; cropped history retained");
  }
  const bool processing=native_processing && saved_compute.ready && !test_feather;
  const unsigned work_scale=processing?neural_scale:100;
  const unsigned work_w=std::max(32u,(crops[0].w*work_scale/100u)/8u*8u);
  const unsigned work_h=std::max(32u,(crops[0].h*work_scale/100u)/8u*8u);
  auto &feature = features[handle];
  Variant *variant = nullptr;
  for (auto &v : feature.variants)
    if (v.region_w == crops[0].w && v.region_h == crops[0].h && v.work_scale==work_scale && v.processing==processing && v.layout == layout &&
        v.color == color.resource && v.output == output.resource) {
      variant = &v;
      break;
    }
  if(variant && variant->failed && variant->retryable){
    ComPtr<ID3D12Device> device;
    const bool alive=SUCCEEDED(color.resource->GetDevice(IID_PPV_ARGS(&device))) && SUCCEEDED(device->GetDeviceRemovedReason());
    if(may_recover(GetTickCount64(),variant->failed_at,feature.recovery_attempts,CropComplete(*variant->usage),alive)){
      ++feature.recovery_attempts;RetireCrops(feature);variant=nullptr;
      Status("Native foveation: retrying cropped NR after completed GPU retirement");
    }
  }
  if (!variant) {
    // Keep one size per feature; obsolete temporal histories are fenced,
    // released and reclaimed instead of permanently consuming eight slots.
    // Bound only outstanding recordings, never the number of slider changes.
    if(retired_variants.size()>=64)
      return normal("Native foveation retained full frame: awaiting crop GPU retirement");
    RetireCrops(feature);
#ifndef NATIVE_FOVEATION_TEST
    for(const auto &g:guides){const auto d=g.resource->GetDesc();D3D12_RESOURCE_STATES st{};bool known=observed(g.resource,st);char info[500];
      sprintf_s(info,"Native foveation v21 contract: feature=%p list=%p guide=%s resource=%p format=%u flags=0x%X image=%llux%u rect=%u,%u %ux%u stateKnown=%d state=0x%X",handle,cmd,g.name,g.resource,unsigned(d.Format),unsigned(d.Flags),d.Width,d.Height,g.rect.x,g.rect.y,g.rect.w,g.rect.h,known,unsigned(st));
      reshade::log::message(reshade::log::level::info,info);}
    float ratio=0,mx=0,my=0;p->Get("DLSSNR.ScalingRatio",&ratio);p->Get("DLSSNR.MVecScaleX",&mx);p->Get("DLSSNR.MVecScaleY",&my);char info[240];sprintf_s(info,"Native foveation v21 parameters: scalingRatio=%.6f mvecScale=(%.6f,%.6f) crop=%ux%u",ratio,mx,my,crops[0].w,crops[0].h);reshade::log::message(reshade::log::level::info,info);
#endif
    Variant v{work_w,work_h,layout,color.resource,output.resource};
    v.region_w=crops[0].w;v.region_h=crops[0].h;v.work_scale=work_scale;v.processing=processing;
    for (unsigned eye = 0; eye < eyes; ++eye) {
      NativeParameters params(p);
      params.Set("DLSSNR.Width", v.w);
      params.Set("DLSSNR.Height", v.h);
      params.Set("Width", v.w);
      params.Set("Height", v.h);
      params.Set("OutWidth", v.w);
      params.Set("OutHeight", v.h);
      for (auto &g : guides)
        WriteRect(params, g.name, scale(crops[eye], a, g.rect));
      CropImages images;
      if(!AllocateCrop(color.resource,v.w,v.h,images)){v.failed=true;v.failure="Native foveation retained full frame: cropped image allocation failed";break;}
      for(const auto &g:guides)if(strcmp(g.name,"Color")&&strcmp(g.name,"Output")&&strcmp(g.name,"Depth")&&strcmp(g.name,"MVec")) {
        // The common Backbuffer alias is exactly the same color/rectangle.
        // Reuse its prepared crop instead of a second allocation and copy.
        bool reuse_color_guide=true;
#ifdef NATIVE_FOVEATION_TEST
        reuse_color_guide=!test_duplicate_backbuffer;
#endif
        if(reuse_color_guide && g.resource==color.resource && g.rect.x==a.x && g.rect.y==a.y && g.rect.w==a.w && g.rect.h==a.h){images.ancillary[g.name]=images.color;continue;}
        if(!AllocateAncillary(g.resource,scale(crops[eye],a,g.rect),images.ancillary[g.name])){v.failed=true;v.failure="Native foveation retained full frame: ancillary image allocation unsupported";break;}
      }
      if(v.failed)break;
      if(processing && !PrepareCropProcessing(color.resource,output.resource,images.color.Get(),images.output.Get(),images.processing)){v.failed=true;v.failure="Native foveation retained full frame: crop processing shader/format unavailable";break;}
      CropParameters(params,images,v.w,v.h);
      v.images.push_back(std::move(images));
      void *h = nullptr;
      const auto pending_before=pending_initializations.size();
      auto result = CreateCropFeature(provider_cmd, params, &h);
      if(pending_initializations.size()!=pending_before)failed_initialization_images.push_back(v.images.back());
      if (result != 1 || !h) {
        v.failed = true;char error[180];sprintf_s(error,"Native foveation retained full frame: cropped feature creation failed (NGX=0x%08X, feature=%ux%u)",result,v.w,v.h);v.failure=error;
        break;
      }
      v.handles.push_back(h);
    }
    feature.variants.push_back(std::move(v));
    variant = &feature.variants.back();
  }
  if (variant->failed)
    return normal(variant->failure.c_str());
  RecordCropUse(cmd,variant->usage);
  bool reset = variant->reset || feature.last_preset != preset ||
               GetTickCount64() - feature.last > 500;
  // Initialize the complete output with the current native image. A
  // subrectangle-only evaluate must never leave stale pixels at the edges.
  own_transitions = true;
  Transition(cmd, color.resource, color_state,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(cmd, output.resource, output_state,
             D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION peripheral_from{},peripheral_to{};
  peripheral_from.pResource=color.resource;peripheral_from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  peripheral_to.pResource=output.resource;peripheral_to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  auto copy_periphery=[&](unsigned x,unsigned y,unsigned w,unsigned h){if(!w||!h)return;D3D12_BOX box={x,y,0,x+w,y+h,1};cmd->CopyTextureRegion(&peripheral_to,x,y,0,&peripheral_from,&box);};
  for(unsigned eye=0;eye<eyes;++eye){Rect viewport=a;if(layout){viewport.w/=2;viewport.x+=eye*viewport.w;}const auto c=crops[eye];
    copy_periphery(viewport.x,viewport.y,viewport.w,c.y-viewport.y);
    copy_periphery(viewport.x,c.y+c.h,viewport.w,viewport.y+viewport.h-c.y-c.h);
    copy_periphery(viewport.x,c.y,c.x-viewport.x,c.h);
    copy_periphery(c.x+c.w,c.y,viewport.x+viewport.w-c.x-c.w,c.h);
  }
  Transition(cmd, color.resource, D3D12_RESOURCE_STATE_COPY_SOURCE,
             color_state);
  Transition(cmd, output.resource, D3D12_RESOURCE_STATE_COPY_DEST,
             output_state);
  own_transitions = false;
  for (unsigned eye = 0; eye < eyes; ++eye) {
    NativeParameters params(p);
    params.Set("DLSSNR.Width", variant->w);
    params.Set("DLSSNR.Height", variant->h);
    for (auto &g : guides)
      WriteRect(params, g.name, scale(crops[eye], a, g.rect));
    auto &images=variant->images[eye];
    // Backbuffer/active-output validation requires the same local origin.
    // Copy every optional image guide, preserving its native format/content.
    for(const auto &g:guides){auto entry=images.ancillary.find(g.name);if(entry==images.ancillary.end())continue;
      if(entry->second.Get()==images.color.Get())continue;
      D3D12_RESOURCE_STATES source_state{};
      if(!observed(g.resource,source_state) || !(source_state&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
        return normal("Native foveation retained full frame: ancillary guide states not qualified");
      const auto rect=scale(crops[eye],a,g.rect);const auto desc=entry->second->GetDesc();
      if(desc.Width!=rect.w||desc.Height!=rect.h||desc.Format!=g.resource->GetDesc().Format)
        return normal("Native foveation retained full frame: ancillary guide layout changed");
      own_transitions=true;
      Transition(cmd,g.resource,source_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
      Transition(cmd,entry->second.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
      D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=g.resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      to.pResource=entry->second.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      const D3D12_BOX box={rect.x,rect.y,0,rect.x+rect.w,rect.y+rect.h,1};cmd->CopyTextureRegion(&to,0,0,0,&from,&box);
      Transition(cmd,entry->second.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      Transition(cmd,g.resource,D3D12_RESOURCE_STATE_COPY_SOURCE,source_state);own_transitions=false;
    }
    CropParameters(params,images,variant->w,variant->h);
    // The model's color/output allocations now agree exactly with its feature
    // dimensions. Only the original native depth/MV guides retain subrects.
    const auto c=crops[eye];
    D3D12_TEXTURE_COPY_LOCATION from{},to{};
    if(processing && work_scale<100){
      own_transitions=own_bindings=true;
      Transition(cmd,images.color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      DispatchCropProcessing(cmd,images.processing,c,variant->w,variant->h,0,0);UAV(cmd,images.color.Get());
      Transition(cmd,images.color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      saved_compute.Restore(cmd);own_transitions=own_bindings=false;
    }else{
      own_transitions=true;
      Transition(cmd,color.resource,color_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
      Transition(cmd,images.color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
      from.pResource=color.resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      to.pResource=images.color.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      const D3D12_BOX box={c.x,c.y,0,c.x+c.w,c.y+c.h,1};cmd->CopyTextureRegion(&to,0,0,0,&from,&box);
      Transition(cmd,images.color.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      Transition(cmd,color.resource,D3D12_RESOURCE_STATE_COPY_SOURCE,color_state);own_transitions=false;
    }
    if (reset)params.Set("DLSSNR.Reset",1u);
    // NR restores root arguments but leaves its private descriptor heap bound.
    // Preserve the caller heap so DFC's next cached-table dispatch is valid.
    std::vector<ComPtr<ID3D12DescriptorHeap>> held_heaps;std::vector<ID3D12DescriptorHeap *> restore_heaps;
    const auto saved_heaps=bound_heaps.find(cmd);const bool heaps_known=saved_heaps!=bound_heaps.end();
    if(heaps_known)for(auto heap:saved_heaps->second){held_heaps.emplace_back(heap);restore_heaps.push_back(heap);}
    own_bindings=true;auto result = ProviderEvaluate(provider_cmd, variant->handles[eye], &params, nullptr);
    if(heaps_known)cmd->SetDescriptorHeaps(UINT(restore_heaps.size()),restore_heaps.data());
    own_bindings=false;
    if (result != 1) {
      variant->failed = true;variant->retryable=true;variant->failed_at=GetTickCount64();
      char error[180];sprintf_s(error,"Native foveation retained full frame: cropped NR evaluation failed (NGX=0x%08X, feature=%ux%u)",result,variant->w,variant->h);
      variant->failure=error;return normal(variant->failure.c_str());
    }
    own_transitions=true;UAV(cmd,images.output.Get());
    if(processing){
      own_bindings=true;
      Transition(cmd,images.output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      DispatchCropProcessing(cmd,images.processing,c,variant->w,variant->h,1,feather_percent);UAV(cmd,output.resource);
      Transition(cmd,images.output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      saved_compute.Restore(cmd);own_bindings=false;
    }else{
      Transition(cmd,images.output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
      Transition(cmd,output.resource,output_state,D3D12_RESOURCE_STATE_COPY_DEST);
      from.pResource=images.output.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=output.resource;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      cmd->CopyTextureRegion(&to,c.x,c.y,0,&from,nullptr);
      Transition(cmd,output.resource,D3D12_RESOURCE_STATE_COPY_DEST,output_state);
      Transition(cmd,images.output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }own_transitions=false;
  }
  UAV(cmd, output.resource);
  if(test_feather){
  ID3D12DescriptorHeap *heaps[] = {gpu->heap.Get()};
  cmd->SetDescriptorHeaps(1, heaps);
  cmd->SetComputeRootSignature(gpu->root.Get());
  cmd->SetPipelineState(gpu->pipeline.Get());
  cmd->SetComputeRootDescriptorTable(
      0, gpu->heap->GetGPUDescriptorHandleForHeapStart());
  for (auto c : crops) {
    unsigned constants[] = {c.x, c.y, c.w, c.h,
                            std::max(8u, std::min(c.w, c.h) / 16u),0u,0u,0u};
    cmd->SetComputeRoot32BitConstants(1, 8, constants, 0);
    cmd->Dispatch((c.w + 7) / 8, (c.h + 7) / 8, 1);
  }
  UAV(cmd, output.resource);
  }
  variant->reset = false;
  feature.last = GetTickCount64();
  feature.last_preset = preset;
  ++cropped;
  if(pass_stats.count(handle)||pass_stats.size()<128){auto &stats=pass_stats[handle];++stats.cropped;stats.last=GetTickCount64();stats.active=true;stats.eyes=eyes;stats.width=a.w;stats.height=a.h;stats.region_width=variant->region_w;stats.region_height=variant->region_h;stats.work_width=variant->w;stats.work_height=variant->h;ReportPass(handle);}
  char info[360];sprintf_s(info,"Native foveation active v21: %ux%u native -> %ux%u region -> %ux%u neural per eye; NR area %.1f%%; feather=%u%%; %s",a.w,a.h,variant->region_w,variant->region_h,variant->w,variant->h,100.0*eyes*variant->w*variant->h/(double(a.w)*a.h),processing?feather_percent:0,processing?"compute bindings restored":"copy path (awaiting compute-state observation)");Status(info);
  return 1;
}
static unsigned __cdecl ReleaseFeature(const void *h) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  pass_stats.erase(h);
  auto it = features.find(h);
  if (it != features.end()) {
    RetireCrops(it->second);
    features.erase(it);
  }
  return release_via ? release_via(release_feature, h) : release_feature(h);
}
static LONG Transaction(bool attach) {
  LONG result = DetourTransactionBegin();
  if (result != NO_ERROR)
    return result;
  std::vector<HANDLE> threads;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    DetourTransactionAbort();
    return GetLastError();
  }
  THREADENTRY32 entry{sizeof(entry)};
  for (BOOL ok = Thread32First(snapshot, &entry); ok;
       ok = Thread32Next(snapshot, &entry))
    if (entry.th32OwnerProcessID == GetCurrentProcessId() &&
        entry.th32ThreadID != GetCurrentThreadId()) {
      HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                            FALSE, entry.th32ThreadID);
      if (t) {
        threads.push_back(t);
        result = DetourUpdateThread(t);
        if (result != NO_ERROR)
          break;
      }
    }
  CloseHandle(snapshot);
  if (result == NO_ERROR)
    result = attach
                 ? DetourAttach(reinterpret_cast<void **>(&evaluate), Evaluate)
                 : DetourDetach(reinterpret_cast<void **>(&evaluate), Evaluate);
  if (result == NO_ERROR)
    result = attach ? DetourAttach(reinterpret_cast<void **>(&release_feature),
                                   ReleaseFeature)
                    : DetourDetach(reinterpret_cast<void **>(&release_feature),
                                   ReleaseFeature);
  if (result == NO_ERROR)
    result = DetourTransactionCommit();
  else
    DetourTransactionAbort();
  for (auto t : threads)
    CloseHandle(t);
  return result;
}
static void Begin(reshade::api::effect_runtime *rt,
                  reshade::api::command_list *, reshade::api::resource_view,
                  reshade::api::resource_view) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  if (rt->get_hwnd() == 0)
    vr_tick = GetTickCount64();
  Config();
  // Existing devices may predate addon loading; arm at the first runtime event
  // even when the neural provider/companion has not been loaded yet.
  if((!FeederPresent() || feeder_vulkan) && rt->get_device()->get_api()==reshade::api::device_api::d3d12)
    ObserveNativeDevice(reinterpret_cast<ID3D12Device *>(rt->get_device()->get_native()));
  if (hook_attempted || evaluate || (FeederPresent() && !feeder_vulkan))
    return;
  auto nr = GetModuleHandleW(L"nvngx_dlssnr.dll");
  if (!nr)
    return;
  evaluate = reinterpret_cast<Eval>(
      GetProcAddress(nr, "NVSDK_NGX_D3D12_EvaluateFeature"));
  release_feature = reinterpret_cast<Release>(
      GetProcAddress(nr, "NVSDK_NGX_D3D12_ReleaseFeature"));
  // The NR snippet associates initialization with its calling module.
  // Never initialize it again or call its CreateFeature directly from this
  // bridge. DFC owns a caller-preserving companion; other providers can use
  // an already initialized NGX core. No snippet fallback is safe here.
  auto companion = GetModuleHandleW(L"deep-fried-chicken-nvngx.dll");
  if (companion && GetModuleHandleW(L"deep-fried-chicken.addon64")) {
    evaluate_via = reinterpret_cast<EvalVia>(GetProcAddress(companion, "DfcCallNgxD3D12EvaluateFeatureVia"));
    release_via = reinterpret_cast<ReleaseVia>(GetProcAddress(companion, "DfcCallNgxD3D12ReleaseFeatureVia"));
    if (!evaluate_via || !release_via) { evaluate = nullptr; Status("Native foveation requires updated DFC companion"); return; }
    create = reinterpret_cast<Create>(
        GetProcAddress(companion, "DfcCallNgxD3D12CreateFeature"));
    crop_release = reinterpret_cast<Release>(
        GetProcAddress(companion, "DfcCallNgxD3D12ReleaseFeature"));
  }
  if (!create || !crop_release) {
    create = nullptr;
    crop_release = nullptr;
    for (auto name : {L"_nvngx.dll", L"nvngx.dll"}) {
      auto core = GetModuleHandleW(name);
      if (!core || core == nr)
        continue;
      auto c = GetProcAddress(core, "NVSDK_NGX_D3D12_CreateFeature"),
           r = GetProcAddress(core, "NVSDK_NGX_D3D12_ReleaseFeature");
      if (c && r) {
        create = reinterpret_cast<Create>(c);
        crop_release = reinterpret_cast<Release>(r);
        break;
      }
    }
  }
  if (!evaluate || !create || !release_feature) {
    evaluate = nullptr;
    Status("Native foveation unavailable: provider has no initialized crop "
           "creation path");
    return;
  }
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(create), &creation_owner)) {
    evaluate = nullptr;
    return;
  }
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(evaluate), &pinned)) {
    evaluate = nullptr;
    return;
  }
  if(rt->get_device()->get_api()==reshade::api::device_api::d3d12)
    ObserveNativeDevice(reinterpret_cast<ID3D12Device *>(rt->get_device()->get_native()));
  // Do not detour CreateFeature: providers already hook that entry point.
  hook_attempted = true;
  if (Transaction(true) != NO_ERROR) {
    evaluate = nullptr;
    Status("Native foveation hook failed; original native path retained");
  } else
    Status("Native foveation armed: awaiting qualified native NR buffers");
}
static void RuntimePresent(reshade::api::effect_runtime *rt) { Begin(rt, nullptr, {}, {}); }
// Preview slider values during a drag; creating NR histories is expensive.
// Commit once when mouse/controller/keyboard editing finishes.
struct CropSizeEdit {
  int width=15,height=15;bool editing=false;
  void Sync(std::pair<unsigned,unsigned> value){if(!editing){width=int(value.first);height=int(value.second);}}
  bool Finish(bool changed,bool active){
    editing|=changed;if(!editing || active)return false;
    editing=false;return true;
  }
};
static bool SaveHealthReport(){
  Config();if(config_path.empty())return false;
  const auto slash=config_path.find_last_of(L"\\");if(slash==std::wstring::npos)return false;
  const auto file=config_path.substr(0,slash+1)+L"dlss5-native-foveation-status.json";
  std::ofstream out(file,std::ios::trunc);if(!out)return false;
  const auto now=GetTickCount64();
  out<<"{\"schema\":1,\"bridge\":\"native-v21\",\"vrActive\":"<<(VRActive()?"true":"false")<<",\"preset\":"<<preset<<",\"status\":"<<json_string(status)<<",\"passes\":[";
  bool first=true;for(const auto &entry:pass_stats){const auto &p=entry.second;if(!first)out<<',';first=false;
    char id[32];sprintf_s(id,"%p",entry.first);
    out<<"{\"feature\":"<<json_string(id)<<",\"recent\":"<<(recently_evaluated(now,p.last)?"true":"false")<<",\"cropping\":"<<(p.active&&recently_evaluated(now,p.last)?"true":"false")<<",\"eyes\":"<<p.eyes<<",\"croppedCalls\":"<<p.cropped<<",\"fullCalls\":"<<p.full<<",\"lastFallback\":"<<json_string(p.reason)<<'}';
  }out<<"]}\n";return bool(out);
}
static void Overlay(reshade::api::effect_runtime *) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  Config();
  int choice = static_cast<int>(preset), stereo = static_cast<int>(layout);
  const auto now=GetTickCount64();unsigned live=0,foveated=0;
  for(const auto &entry:pass_stats)if(recently_evaluated(now,entry.second.last)){++live;if(entry.second.active)++foveated;}
  ImGui::Text("Headset: %s | recent NR features: %u | cropping: %u",VRActive()?"active":"waiting",live,foveated);
  if(!live)ImGui::TextWrapped("Awaiting gameplay NR calls. Earlier counters do not indicate a currently running effect.");
  ImGui::TextWrapped("%s", status.c_str());
  if(ImGui::Button("Save compatibility report"))Status(SaveHealthReport()?"Native foveation: diagnostic report saved":"Native foveation: diagnostic report could not be saved");
  static CropSizeEdit size_edit;size_edit.Sync(Percent());
  bool changed = ImGui::Combo(
      "Native VR foveation", &choice,
      "Off\0Small 50x45\0Balanced 60x50\0Wide 70x50\0Large 75x60\0");
  if(changed){preset=choice;custom_width=custom_height=0;size_edit.editing=false;size_edit.Sync(Percent());}
  bool resized=ImGui::SliderInt("Center width (%)",&size_edit.width,15,90);
  bool active=ImGui::IsItemActive();
  resized|=ImGui::SliderInt("Center height (%)",&size_edit.height,15,90);
  active|=ImGui::IsItemActive();
  if(size_edit.Finish(resized,active)){custom_width=size_edit.width;custom_height=size_edit.height;changed=true;}
  ImGui::TextUnformatted("Size applies when you release the slider. Larger areas require more neural processing.");
  changed |=
      ImGui::Combo("Native NR stereo layout", &stereo,
                   "Separate eye / mono features\0Packed side-by-side\0");
  if (changed) {
    preset = choice;
    layout = stereo;
    for (auto &f : features)
      for (auto &v : f.second.variants)
        v.reset = true;
    Config(true);
  }
  static CropSizeEdit quality_edit;quality_edit.Sync({neural_scale,feather_percent});
  bool quality_changed=ImGui::SliderInt("Neural center resolution (%)",&quality_edit.width,50,100);bool quality_active=ImGui::IsItemActive();
  quality_changed|=ImGui::SliderInt("Edge fade (%)",&quality_edit.height,0,35);quality_active|=ImGui::IsItemActive();
  if(quality_edit.Finish(quality_changed,quality_active)){neural_scale=quality_edit.width;feather_percent=quality_edit.height;Config(true);}
  ImGui::TextUnformatted("85% is the balanced center resolution; 100% preserves full crop detail. Fade blends inward.");
  ImGui::Text("Foveated frames: %llu | Unmasked frames: %llu", cropped,
              retained);
  for(const auto &entry:pass_stats){const auto &p=entry.second;ImGui::Text("NR feature %p: %s | cropped %llu | full %llu",entry.first,recently_evaluated(now,p.last)?(p.active?"cropping":"full frame"):"inactive",p.cropped,p.full);if(p.width)ImGui::Text("Eyes: %u | image %ux%u | center %ux%u | NR %ux%u",p.eyes,p.width,p.height,p.region_width,p.region_height,p.work_width,p.work_height);if(entry.second.full)ImGui::TextWrapped("Last full-frame reason: %s",entry.second.reason.c_str());}
  if(feeder_vulkan)ImGui::TextWrapped("Vulkan Feeder transport: these controls crop each eye's neural-rendering region. DLSS 5 Feed retains its full-frame transport.");
  ImGui::TextWrapped("Only NR processes the cropped center. Native DLSS and DFC HDR conversion stay full frame; reduced DFC working resolution can also change the outer image. The processed center fades into the original input using a smooth edge blend. Lower center resolution trades neural detail for less GPU work. Select packed only if "
                     "the native NR buffer contains both eyes side by side. "
                     "Unsupported buffers retain the normal path.");
}
// Stable settings API for the in-headset dashboard. Share the same lock as
// evaluation and the ReShade tab; never maintain a second preset value.
extern "C" __declspec(dllexport) bool __cdecl DLSS5NativeFoveationRead(
    unsigned *out, unsigned long long *crop, unsigned long long *full) {
  if (!out || !crop || !full) return false;
  std::lock_guard<std::recursive_mutex> lock(guard);
  Config(); *out = preset; *crop = cropped; *full = retained; return true;
}
extern "C" __declspec(dllexport) bool __cdecl DLSS5NativeFoveationSet(unsigned value) {
  if (value > 4) return false;
  std::lock_guard<std::recursive_mutex> lock(guard);
  Config();
  const auto old = preset;
  preset = value; Config(true);
  if (GetPrivateProfileIntW(L"NativeFoveation", L"Preset", 0xffffffff,
                           config_path.c_str()) != value) {
    preset = old; return false;
  }
  config_tick = GetTickCount64();
  if (old != preset) for (auto &f : features) for (auto &v : f.second.variants) v.reset = true;
  return true;
}
extern "C" __declspec(dllexport) const char *NAME = "Native DLSS VR foveation";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Crops native neural rendering while retaining native DLSS and the current "
    "outer image.";
extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE addon, HMODULE owner) {
  EnableGpuDiagnostics();
  if (!reshade::register_addon(addon, owner))
    return FALSE;
  reshade::register_event<reshade::addon_event::init_device>(InitDevice);
  reshade::register_event<reshade::addon_event::init_resource>(InitResource);
  reshade::register_event<reshade::addon_event::barrier>(Barrier);
  reshade::register_event<reshade::addon_event::reset_command_list>(ResetList);
  reshade::register_event<reshade::addon_event::destroy_command_list>(
      DestroyList);
  reshade::register_event<reshade::addon_event::destroy_resource>(
      DestroyResource);
  reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);
  reshade::register_event<reshade::addon_event::reshade_present>(RuntimePresent);
  reshade::register_event<reshade::addon_event::init_effect_runtime>(RuntimePresent);
  reshade::register_overlay("Native DLSS VR foveation", Overlay);
  return TRUE;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon, HMODULE) {
  StopGpuDiagnostics();
  if (evaluate)
    Transaction(false);
  StopNativeObservation();
  reshade::unregister_event<reshade::addon_event::reshade_present>(RuntimePresent);
  reshade::unregister_event<reshade::addon_event::init_effect_runtime>(RuntimePresent);
  reshade::unregister_overlay("Native DLSS VR foveation", Overlay);
  reshade::unregister_event<reshade::addon_event::init_device>(InitDevice);
  reshade::unregister_event<reshade::addon_event::init_resource>(InitResource);
  reshade::unregister_event<reshade::addon_event::barrier>(Barrier);
  reshade::unregister_event<reshade::addon_event::reset_command_list>(
      ResetList);
  reshade::unregister_event<reshade::addon_event::destroy_command_list>(
      DestroyList);
  reshade::unregister_event<reshade::addon_event::destroy_resource>(
      DestroyResource);
  reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);
  reshade::unregister_addon(addon);
}
BOOL WINAPI DllMain(HMODULE h, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH)
    DisableThreadLibraryCalls(h);
  return TRUE;
}
