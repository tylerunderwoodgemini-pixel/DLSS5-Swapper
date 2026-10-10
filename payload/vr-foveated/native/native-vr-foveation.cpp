#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "imgui/imgui.h"
#include "native-foveation-layout.h"
#include "native-ngx-parameters.h"
#include <atomic>
#include <cstdio>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <detours.h>
#include <mutex>
#include <reshade.hpp>
#include <tlhelp32.h>
#include <tuple>
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
static Eval evaluate;
static Create create;
static Release release_feature, crop_release;
static HMODULE pinned, creation_owner;
static bool hook_attempted = false;
static std::recursive_mutex guard;
static std::atomic<ULONGLONG> vr_tick{0};
static unsigned preset = 0,
                layout = 0; // 0 separate-eye/mono features, 1 packed SBS
static ULONGLONG config_tick = 0;
static std::wstring config_path;
static uint64_t cropped = 0, retained = 0;
static std::string status = "Waiting for native neural rendering";
static void Status(const char *s) {
  if (status == s)
    return;
  status = s;
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
    return;
  }
  auto now = GetTickCount64();
  if (config_tick && now - config_tick < 750)
    return;
  config_tick = now;
  preset = std::min(4u, GetPrivateProfileIntW(L"NativeFoveation", L"Preset", 0,
                                              config_path.c_str()));
  layout =
      std::min(1u, GetPrivateProfileIntW(L"NativeFoveation", L"StereoLayout", 0,
                                         config_path.c_str()));
}
static std::pair<unsigned, unsigned> Percent() {
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
static void InitResource(reshade::api::device *device,
                         const reshade::api::resource_desc &,
                         const reshade::api::subresource_data *,
                         reshade::api::resource_usage state,
                         reshade::api::resource resource) {
  if (device->get_api() != reshade::api::device_api::d3d12)
    return;
  std::lock_guard<std::recursive_mutex> lock(guard);
  auto value = static_cast<uint32_t>(state);
  if (!(value & 0x80000000u))
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
  auto &s =
      states[reinterpret_cast<ID3D12GraphicsCommandList *>(cmd->get_native())];
  for (uint32_t i = 0; i < n; ++i) {
    auto value = static_cast<uint32_t>(next[i]);
    auto resource = reinterpret_cast<ID3D12Resource *>(r[i].handle);
    if (!resource)
      continue;
    if (!own_transitions)
      initial_states.erase(resource);
    if (value & 0x80000000u)
      s.erase(resource);
    else
      s[resource] = static_cast<D3D12_RESOURCE_STATES>(value);
  }
}
static void ResetList(reshade::api::command_list *cmd) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  states.erase(
      reinterpret_cast<ID3D12GraphicsCommandList *>(cmd->get_native()));
}
static void DestroyResource(reshade::api::device *, reshade::api::resource r) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  auto resource = reinterpret_cast<ID3D12Resource *>(r.handle);
  initial_states.erase(resource);
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
struct GPU {
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12DescriptorHeap> heap;
  ComPtr<ID3D12Resource> color, output;
};
static std::map<std::pair<ID3D12Resource *, ID3D12Resource *>, GPU> gpus;
static const char *feather_shader = R"(
Texture2D<float4> Color:register(t0);RWTexture2D<float4> Output:register(u0);
cbuffer Geometry:register(b0){uint4 rect;uint feather;};
[numthreads(8,8,1)]void Main(uint3 id:SV_DispatchThreadID){
 if(id.x>=rect.z||id.y>=rect.w)return;uint2 p=rect.xy+id.xy;
 float edge=min(min(id.x,rect.z-1-id.x),min(id.y,rect.w-1-id.y));
 float w=smoothstep(0.0,float(feather),edge);
 Output[p]=lerp(Color.Load(int3(p,0)),Output[p],w);
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
  parameters[1].Constants = {0, 0, 5};
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
struct Variant {
  unsigned w, h, layout;
  ID3D12Resource *color = nullptr, *output = nullptr;
  std::vector<void *> handles;
  bool reset = true, failed = false;
};
struct Feature {
  std::vector<Variant> variants;
  ULONGLONG last = 0;
  unsigned last_preset = 0;
};
static std::map<const void *, Feature> features;
static unsigned __cdecl Evaluate(ID3D12GraphicsCommandList *cmd,
                                 const void *handle,
                                 const NVSDK_NGX_Parameter *p, void *callback) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  Config();
  auto normal = [&](const char *reason) {
    ++retained;
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
        return evaluate(cmd, handle, &params, callback);
      }
    }
    return evaluate(cmd, handle, p, callback);
  };
  if (!preset || !VRActive() || GetModuleHandleW(L"dlss5-feed.addon64"))
    return normal(
        "Native foveation idle: disabled, flat runtime, or Feeder route");
  if (!p || callback || !cmd || !handle)
    return normal("Native foveation retained full frame: unsupported NGX call");
  std::vector<Guide> guides;
  if (!Guides(p, guides))
    return normal("Native foveation retained full frame: unsupported or "
                  "missing native NR guides");
  auto &color = guides[0], &output = guides[1];
  auto a = color.rect, b = output.rect;
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
    auto initial = initial_states.find(resource);
    if (initial == initial_states.end())
      return false;
    state = initial->second;
    return true;
  };
  D3D12_RESOURCE_STATES color_state, output_state;
  if (!observed(color.resource, color_state) ||
      !observed(output.resource, output_state) ||
      !(color_state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
      output_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    return normal("Native foveation retained full frame: color/output states "
                  "not qualified");
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
  GPU *gpu = nullptr;
  if (!Prepare(color.resource, output.resource, gpu))
    return normal("Native foveation retained full frame: unsupported format or "
                  "resource cache limit");
  if (features.size() >= 64 && !features.count(handle))
    return normal("Native foveation retained full frame: feature cache limit");
  auto &feature = features[handle];
  Variant *variant = nullptr;
  for (auto &v : feature.variants)
    if (v.w == crops[0].w && v.h == crops[0].h && v.layout == layout &&
        v.color == color.resource && v.output == output.resource) {
      variant = &v;
      break;
    }
  if (!variant) {
    if (feature.variants.size() >= 8)
      return normal("Native foveation retained full frame: restart to rebuild "
                    "crop cache");
    Variant v{crops[0].w, crops[0].h, layout, color.resource, output.resource};
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
      void *h = nullptr;
      auto result = create(cmd, 18, &params, &h);
      if (result != 1 || !h) {
        v.failed = true;
        break;
      }
      v.handles.push_back(h);
    }
    feature.variants.push_back(std::move(v));
    variant = &feature.variants.back();
  }
  if (variant->failed)
    return normal("Native foveation retained full frame: NR rejected cropped "
                  "feature creation");
  bool reset = variant->reset || feature.last_preset != preset ||
               GetTickCount64() - feature.last > 500;
  // Initialize the complete output with the current native image. A
  // subrectangle-only evaluate must never leave stale pixels at the edges.
  own_transitions = true;
  Transition(cmd, color.resource, color_state,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(cmd, output.resource, output_state,
             D3D12_RESOURCE_STATE_COPY_DEST);
  cmd->CopyResource(output.resource, color.resource);
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
    if (reset)
      params.Set("DLSSNR.Reset", 1u);
    auto result = evaluate(cmd, variant->handles[eye], &params, nullptr);
    if (result != 1) {
      variant->failed = true;
      return normal(
          "Native foveation retained full frame: cropped NR evaluation failed");
    }
  }
  UAV(cmd, output.resource);
  ID3D12DescriptorHeap *heaps[] = {gpu->heap.Get()};
  cmd->SetDescriptorHeaps(1, heaps);
  cmd->SetComputeRootSignature(gpu->root.Get());
  cmd->SetPipelineState(gpu->pipeline.Get());
  cmd->SetComputeRootDescriptorTable(
      0, gpu->heap->GetGPUDescriptorHandleForHeapStart());
  for (auto c : crops) {
    unsigned constants[] = {c.x, c.y, c.w, c.h,
                            std::max(8u, std::min(c.w, c.h) / 16u)};
    cmd->SetComputeRoot32BitConstants(1, 5, constants, 0);
    cmd->Dispatch((c.w + 7) / 8, (c.h + 7) / 8, 1);
  }
  UAV(cmd, output.resource);
  variant->reset = false;
  feature.last = GetTickCount64();
  feature.last_preset = preset;
  ++cropped;
  Status("Native foveation active: cropped NR, current-frame periphery, "
         "feathered border");
  return 1;
}
static unsigned __cdecl ReleaseFeature(const void *h) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  auto it = features.find(h);
  if (it != features.end()) {
    for (auto &v : it->second.variants)
      for (auto crop_handle : v.handles)
        (crop_release ? crop_release : release_feature)(crop_handle);
    features.erase(it);
  }
  return release_feature(h);
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
  if (hook_attempted || evaluate || GetModuleHandleW(L"dlss5-feed.addon64"))
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
  // Do not detour CreateFeature: providers already hook that entry point.
  hook_attempted = true;
  if (Transaction(true) != NO_ERROR) {
    evaluate = nullptr;
    Status("Native foveation hook failed; original native path retained");
  } else
    Status("Native foveation armed: awaiting qualified native NR buffers");
}
static void Overlay(reshade::api::effect_runtime *) {
  std::lock_guard<std::recursive_mutex> lock(guard);
  Config();
  int choice = static_cast<int>(preset), stereo = static_cast<int>(layout);
  ImGui::TextWrapped("%s", status.c_str());
  bool changed = ImGui::Combo(
      "Native VR foveation", &choice,
      "Off\0Small 50x45\0Balanced 60x50\0Wide 70x50\0Large 75x60\0");
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
  ImGui::Text("Cropped NR calls: %llu | Full-frame calls: %llu", cropped,
              retained);
  ImGui::TextWrapped("Native DLSS remains full frame. Select packed only if "
                     "the native NR buffer contains both eyes side by side. "
                     "Unsupported buffers retain the normal path.");
}
extern "C" __declspec(dllexport) const char *NAME = "Native DLSS VR foveation";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Crops native neural rendering while retaining native DLSS and the current "
    "outer image.";
extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE addon, HMODULE owner) {
  if (!reshade::register_addon(addon, owner))
    return FALSE;
  reshade::register_event<reshade::addon_event::init_resource>(InitResource);
  reshade::register_event<reshade::addon_event::barrier>(Barrier);
  reshade::register_event<reshade::addon_event::reset_command_list>(ResetList);
  reshade::register_event<reshade::addon_event::destroy_command_list>(
      ResetList);
  reshade::register_event<reshade::addon_event::destroy_resource>(
      DestroyResource);
  reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);
  reshade::register_overlay("Native DLSS VR foveation", Overlay);
  return TRUE;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon, HMODULE) {
  if (evaluate)
    Transaction(false);
  reshade::unregister_overlay("Native DLSS VR foveation", Overlay);
  reshade::unregister_event<reshade::addon_event::init_resource>(InitResource);
  reshade::unregister_event<reshade::addon_event::barrier>(Barrier);
  reshade::unregister_event<reshade::addon_event::reset_command_list>(
      ResetList);
  reshade::unregister_event<reshade::addon_event::destroy_command_list>(
      ResetList);
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
