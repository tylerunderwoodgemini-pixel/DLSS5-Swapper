// GPU contract test using WARP and a fake NR implementation. This verifies
// recorded copies/blending and the actual NGX parameter path; it does not
// substitute for testing NVIDIA's proprietary model on a headset.
#define NATIVE_FOVEATION_TEST
#include "../payload/vr-foveated/native/native-vr-foveation.cpp"
#include <cassert>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <iostream>
#include "native-list-wrapper.h"
struct Empty final : NVSDK_NGX_Parameter {
#define S(T)                                                                   \
  void Set(const char *, T) override {}
  S(unsigned long long)
  S(float)
  S(double) S(unsigned) S(int) S(ID3D11Resource *) S(ID3D12Resource *) S(void *)
#undef S
#define G(T)                                                                   \
  NVSDK_NGX_Result Get(const char *, T *) const override {                     \
    return NVSDK_NGX_Result_FAIL_InvalidParameter;                             \
  }
      G(unsigned long long) G(float) G(double) G(unsigned) G(int)
          G(ID3D11Resource *) G(ID3D12Resource *) G(void *)
#undef G
              void Reset() override {
  }
};
struct MockHandle {
  unsigned w, h;
};
static ComPtr<ID3D12PipelineState> mock_pipeline;
static const void *full = reinterpret_cast<void *>(0x1234);
static std::vector<std::tuple<const void *, Rect, unsigned, unsigned>> calls;
static unsigned creates = 0, releases = 0;
static bool fail = false;
static ID3D12GraphicsCommandList *producer_for_creation=nullptr;
static unsigned __cdecl MockCreate(ID3D12GraphicsCommandList *creation_list,
                                   unsigned feature, NVSDK_NGX_Parameter *p,
                                   void **h) {
  assert(feature == 18);
  assert(creation_list && creation_list!=producer_for_creation);
  unsigned w = 0, height = 0;
  p->Get("DLSSNR.Width", &w);
  p->Get("DLSSNR.Height", &height);
  assert(w && height);
  void *color=nullptr,*output=nullptr;assert(p->Get("DLSSNR.Color",&color)==1&&color);assert(p->Get("DLSSNR.Output",&output)==1&&output);
  *h = new MockHandle{w, height};
  ++creates;
  return 1;
}
static unsigned __cdecl MockRelease(const void *h) {
  if (h != full) {
    delete static_cast<const MockHandle *>(h);
    ++releases;
  }
  return 1;
}
static unsigned __cdecl MockEvaluate(ID3D12GraphicsCommandList *cmd,
                                     const void *h,
                                     const NVSDK_NGX_Parameter *p, void *) {
  ID3D12Resource *c = nullptr, *o = nullptr;
  p->Get("DLSSNR.Color", &c);
  p->Get("DLSSNR.Output", &o);
  void *generic_color=nullptr,*generic_output=nullptr;
  assert(p->Get("DLSSNR.Color",&generic_color)==1&&generic_color==c);
  assert(p->Get("DLSSNR.Output",&generic_output)==1&&generic_output==o);
  unsigned long long opaque_color=0,opaque_output=0;
  assert(p->Get("DLSSNR.Color",&opaque_color)==1&&opaque_color==reinterpret_cast<uintptr_t>(c));
  assert(p->Get("DLSSNR.Output",&opaque_output)==1&&opaque_output==reinterpret_cast<uintptr_t>(o));


  Rect rect;
  assert(ReadRect(p, "Output", o, rect));
  unsigned reset = 0;
  p->Get("DLSSNR.Reset", &reset);
  unsigned width = 0;
  p->Get("DLSSNR.Width", &width);
  calls.emplace_back(h, rect, reset, width);
  if (fail && h != full)
    return 0xbad00005;
  if (h != full) {
    auto m = static_cast<const MockHandle *>(h);
    assert(m->w == rect.w && m->h == rect.h);
    ID3D12Resource *backbuffer=nullptr;p->Get("DLSSNR.Backbuffer",&backbuffer);assert(backbuffer);Rect back;assert(ReadRect(p,"Backbuffer",backbuffer,back));
    assert(back.x==rect.x&&back.y==rect.y&&back.w==rect.w&&back.h==rect.h);
    unsigned long long opaque_back=0;assert(p->Get("DLSSNR.Backbuffer",&opaque_back)==1&&opaque_back==reinterpret_cast<uintptr_t>(backbuffer));
    assert(backbuffer->GetDesc().Width==m->w&&backbuffer->GetDesc().Height==m->h);
    assert(c->GetDesc().Width==m->w&&c->GetDesc().Height==m->h);
    assert(o->GetDesc().Width==m->w&&o->GetDesc().Height==m->h&&rect.x==0&&rect.y==0);
  }
  GPU *gpu = nullptr;
  assert(Prepare(c, o, gpu));
  ID3D12DescriptorHeap *heaps[] = {gpu->heap.Get()};
  cmd->SetDescriptorHeaps(1, heaps);
  cmd->SetComputeRootSignature(gpu->root.Get());
  cmd->SetPipelineState(mock_pipeline.Get());
  cmd->SetComputeRootDescriptorTable(
      0, gpu->heap->GetGPUDescriptorHandleForHeapStart());
  unsigned k[] = {rect.x, rect.y, rect.w, rect.h, 0};
  cmd->SetComputeRoot32BitConstants(1, 5, k, 0);
  cmd->Dispatch((rect.w + 7) / 8, (rect.h + 7) / 8, 1);
  UAV(cmd, o);
  return 1;
}
template <class T> static void OK(T result) { assert(SUCCEEDED(result)); }
int main() {
  ComPtr<ID3D12Debug> debug;OK(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();ComPtr<ID3D12Debug1> gpu_debug;if(SUCCEEDED(debug.As(&gpu_debug)))gpu_debug->SetEnableGPUBasedValidation(TRUE);
  CropSizeEdit edit;edit.Sync({15,15});
  for(int percent=16;percent<=45;++percent){edit.width=percent;assert(!edit.Finish(true,true));edit.Sync({15,15});assert(edit.width==percent);}
  assert(edit.Finish(false,false));assert(edit.width==45);assert(!edit.Finish(false,false));
  edit.Sync({30,40});assert(edit.width==30&&edit.height==40);
  edit.height=44;assert(edit.Finish(true,false)); // A discrete edit commits immediately.
  experimental_crop = true;test_feather=true;
  Config();
  preset = 2;
  layout = 0;
  config_tick = GetTickCount64();
  vr_tick = config_tick;
  evaluate = MockEvaluate;
  create = MockCreate;
  release_feature = MockRelease;
  ComPtr<IDXGIFactory4> factory;
  OK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  ComPtr<IDXGIAdapter> warp;
  OK(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
  ComPtr<ID3D12Device> device;
  OK(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                       IID_PPV_ARGS(&device)));
  assert(ObserveNativeDevice(device.Get()));
  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC qd{};
  OK(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
  ComPtr<ID3D12CommandAllocator> allocator;
  OK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    IID_PPV_ARGS(&allocator)));
  ComPtr<ID3D12GraphicsCommandList> cmd;
  OK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                               allocator.Get(), nullptr, IID_PPV_ARGS(&cmd)));
  D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 256;
  desc.Height = 256;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> color, output, depth, mvec;
  OK(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                     IID_PPV_ARGS(&color)));
  OK(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                     nullptr, IID_PPV_ARGS(&output)));
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  OK(device->CreateCommittedResource(
      &hp, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
      IID_PPV_ARGS(&depth)));
  desc.Format = DXGI_FORMAT_R32G32_FLOAT;
  OK(device->CreateCommittedResource(
      &hp, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
      IID_PPV_ARGS(&mvec)));
  auto texture = color->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  UINT64 bytes;
  device->GetCopyableFootprints(&texture, 0, 1, 0, &footprint, nullptr, nullptr,
                                &bytes);
  D3D12_RESOURCE_DESC buffer{};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = bytes;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> upload, readback;
  hp.Type = D3D12_HEAP_TYPE_UPLOAD;
  OK(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &buffer,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                     IID_PPV_ARGS(&upload)));
  hp.Type = D3D12_HEAP_TYPE_READBACK;
  OK(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &buffer,
                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                     IID_PPV_ARGS(&readback)));
  unsigned char *data;
  OK(upload->Map(0, nullptr, reinterpret_cast<void **>(&data)));
  for (unsigned y = 0; y < 256; ++y)
    for (unsigned x = 0; x < 256; ++x) {
      auto pixel = reinterpret_cast<float *>(
          data + y * footprint.Footprint.RowPitch + x * 16);
      pixel[0] = 1;
      pixel[1] = 0;
      pixel[2] = 0;
      pixel[3] = 1;
    }
  upload->Unmap(0, nullptr);
  assert(ObserveNativeDevice(device.Get()));
  NativeListWrapper inner_list(cmd.Get(),true),outer_list(&inner_list,false);
  {auto normalized=UnwrappedNative(reinterpret_cast<ID3D12GraphicsCommandList *>(&outer_list));assert(normalized.Get()==cmd.Get());}
  assert(inner_list.refs==1 && outer_list.refs==1);
  assert(committed_hooked && execute_hooked && native_barrier && native_reset);
  auto saved_barrier=native_barrier;auto saved_execute=native_execute;auto saved_committed=native_committed;
  assert(ObserveNativeDevice(device.Get()));assert(ObserveNativeList(cmd.Get()));
  assert(native_barrier==saved_barrier && native_execute==saved_execute && native_committed==saved_committed);
  D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
  src.pResource = upload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint = footprint;
  dst.pResource = color.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Transition(cmd.Get(), color.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  assert(states[cmd.Get()][color.Get()] == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  D3D12_RESOURCE_BARRIER one{};one.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  one.Transition={color.Get(),0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
  cmd->ResourceBarrier(1,&one);assert(states[cmd.Get()][color.Get()]==D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::swap(one.Transition.StateBefore,one.Transition.StateAfter);cmd->ResourceBarrier(1,&one);
  assert(states[cmd.Get()][color.Get()]==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  assert(states[cmd.Get()][output.Get()] == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  GPU *gpu;
  assert(Prepare(color.Get(), output.Get(), gpu));
  const char *shader =
      "Texture2D<float4> Color:register(t0);RWTexture2D<float4> Output:register(u0);cbuffer G:register(b0){uint4 "
      "r;uint f;}[numthreads(8,8,1)]void Main(uint3 "
      "p:SV_DispatchThreadID){if(p.x<r.z&&p.y<r.w)Output[r.xy+p.xy]=float4(0,Color.Load(int3(r.xy+p.xy,0)).r,"
      "0,1);}";
  ComPtr<ID3DBlob> blob, error;
  OK(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "Main",
                "cs_5_0", 0, 0, &blob, &error));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
  pd.pRootSignature = gpu->root.Get();
  pd.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
  OK(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&mock_pipeline)));
  producer_for_creation=cmd.Get();
  Empty empty;
  NativeParameters p(&empty);
  p.Set("DLSSNR.Color", color.Get());
  p.Set("DLSSNR.Output", output.Get());
  p.Set("DLSSNR.Backbuffer",color.Get());
  p.Set("DLSSNR.Depth", depth.Get());
  p.Set("DLSSNR.MVec", mvec.Get());
  p.Set("DLSSNR.Width", 256u);
  p.Set("DLSSNR.Height", 256u);
  p.Set("DLSSNR.MVecScaleX", 256.f);
  config_tick = GetTickCount64();
  test_feeder_present=true;feeder_vulkan=false;
  assert(Evaluate(cmd.Get(),full,&p,nullptr)==1 && cropped==0 && std::get<0>(calls.back())==full);
  feeder_vulkan=true;
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  test_feeder_present=feeder_vulkan=false;
  assert(cropped == 1);
  assert(creates == 1);
  assert(std::get<0>(calls.back()) != full);
  assert(std::get<2>(calls.back()) == 1);
  auto rect = crop({0,0,256,256},60,50);
  assert(rect.w == 152 && rect.h == 128);
  unsigned width = 0, base = 99;
  assert(p.Get("DLSSNR.Width", &width) == 1 && width == 256);
  assert(p.Get("DLSSNR.OutputSubrectBaseX", &base) != 1);
  Transition(cmd.Get(), output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  src.pResource = output.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;
  cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Transition(cmd.Get(), output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  OK(cmd->Close());
  ID3D12CommandList *lists[] = {cmd.Get()};
  queue->ExecuteCommandLists(1, lists);
  ComPtr<ID3D12Fence> fence;
  OK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  OK(queue->Signal(fence.Get(), 1));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  OK(fence->SetEventOnCompletion(1, event));
  assert(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0);
  CloseHandle(event);
  OK(readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
  auto pixel = [&](unsigned x, unsigned y) {
    return reinterpret_cast<float *>(data + y * footprint.Footprint.RowPitch +
                                     x * 16);
  };
  assert(pixel(0, 0)[0] == 1 && pixel(0, 0)[1] == 0);
  assert(pixel(128, 128)[0] == 0 && pixel(128, 128)[1] == 1);
  assert(pixel(rect.x, rect.y)[0] == 1 && pixel(rect.x, rect.y)[1] == 0);
  assert(pixel(rect.x + 4, rect.y + 4)[0] > 0 &&
         pixel(rect.x + 4, rect.y + 4)[1] > 0);
  readback->Unmap(0, nullptr);
  OK(allocator->Reset());
  OK(cmd->Reset(allocator.Get(), nullptr));
  assert(!states.count(cmd.Get()));
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  // Normal frames reset their stale full-frame history after a crop, once.
  preset = 0;
  config_tick = GetTickCount64();
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  assert(std::get<0>(calls.back()) == full && std::get<2>(calls.back()) == 1);
  config_tick = GetTickCount64();
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  assert(std::get<2>(calls.back()) == 0);
  preset = 2;
  layout = 1;
  vr_tick = GetTickCount64();
  config_tick = vr_tick;
  config_tick = GetTickCount64();
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  assert(creates == 3);
  auto left = std::get<0>(calls[calls.size() - 2]),
       right = std::get<0>(calls.back());
  assert(left != right);
  assert(std::get<1>(calls[calls.size() - 2]).x == 0 &&
         std::get<1>(calls.back()).x == 0);
  // Late headset startup and pause/session interruptions recover the same
  // crop history, resetting it once without reallocating features.
  const auto pause_creates=creates;
  for(unsigned interruption=0;interruption<12;++interruption){
    vr_tick=0;config_tick=GetTickCount64();assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);
    assert(std::get<0>(calls.back())==full && !pass_stats.at(full).active);
    vr_tick=config_tick=GetTickCount64();assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);
    assert(std::get<0>(calls.back())!=full && std::get<2>(calls.back())==1);
    assert(creates==pause_creates && pass_stats.at(full).active);
  }
  p.Set("DLSSNR.MVecScaleX",std::numeric_limits<float>::quiet_NaN());
  const auto validated_creates=creates;config_tick=GetTickCount64();
  assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);assert(std::get<0>(calls.back())==full && creates==validated_creates);
  assert(status.find("non-finite")!=std::string::npos);p.Set("DLSSNR.MVecScaleX",1.0f);
  fail = true;
  config_tick = GetTickCount64();
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  assert(std::get<0>(calls.back()) == full);
  fail = false;
  auto &failed_variant=features.at(full).variants.back();assert(failed_variant.failed && failed_variant.retryable);
  failed_variant.failed_at=GetTickCount64()-5001;
  const auto before_retry=creates;
  config_tick=vr_tick=GetTickCount64();assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);
  assert(creates==before_retry); // Even overdue retries cannot free an unsubmitted recording.
  ComPtr<ID3D12Fence> recovery_fence;OK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&recovery_fence)));
  OK(cmd->Close());queue->ExecuteCommandLists(1,lists);OK(queue->Signal(recovery_fence.Get(),1));
  auto recovery_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);OK(recovery_fence->SetEventOnCompletion(1,recovery_event));
  assert(WaitForSingleObject(recovery_event,10000)==WAIT_OBJECT_0);CloseHandle(recovery_event);
  OK(allocator->Reset());OK(cmd->Reset(allocator.Get(),nullptr));
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  config_tick=vr_tick=GetTickCount64();assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);
  assert(creates==before_retry+2 && features.at(full).recovery_attempts==1);
  assert(std::get<0>(calls.back())!=full && !features.at(full).variants.back().failed);
  assert(pass_stats.at(full).active && pass_stats.at(full).eyes==2);
  assert(!may_recover(10000,1000,2,true,true));assert(!may_recover(10000,1000,0,true,false));
  assert(!recently_evaluated(10000,1000));assert(recently_evaluated(10000,9999));
  states.clear();submitted_states.erase(color.Get());submitted_states.erase(output.Get());initial_states.erase(color.Get());initial_states.erase(output.Get());
  auto before = creates;
  config_tick = GetTickCount64();
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  assert(std::get<0>(calls.back()) == full && creates == before);
  ReleaseFeature(full);
  assert(releases < creates); // Recorded, unsubmitted uses must survive ReleaseFeature.
  assert(!retired_variants.empty());
  assert(features.empty());
  // DFC's first private output may never transition away from its creation UAV state.
  ComPtr<ID3D12Resource> private_output;auto private_desc=output->GetDesc();D3D12_HEAP_PROPERTIES private_heap{};private_heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  OK(device->CreateCommittedResource(&private_heap,D3D12_HEAP_FLAG_NONE,&private_desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&private_output)));
  assert(initial_states.at(private_output.Get())==D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  p.Set("DLSSNR.Output",private_output.Get());preset=2;layout=0;config_tick=vr_tick=GetTickCount64();
  before=creates;assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);assert(creates==before+1);assert(std::get<0>(calls.back())!=full);
  p.Set("DLSSNR.Output",output.Get());ReleaseFeature(full);assert(features.empty());
  // Production composition must use the original feature and retain native guides.
  experimental_crop=false;test_feather=false;preset=2;layout=1;config_tick=vr_tick=GetTickCount64();
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  before=creates;assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);assert(creates==before&&features.empty());assert(std::get<0>(calls.back())==full);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
  cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  OK(cmd->Close());queue->ExecuteCommandLists(1,lists);OK(queue->Signal(fence.Get(),2));
  event=CreateEventW(nullptr,FALSE,FALSE,nullptr);OK(fence->SetEventOnCompletion(2,event));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);
  OK(readback->Map(0,nullptr,reinterpret_cast<void **>(&data)));
  assert(pixel(0,0)[0]==1&&pixel(0,0)[1]==0);
  assert(pixel(64,128)[0]==0&&pixel(64,128)[1]==1);
  assert(pixel(192,128)[0]==0&&pixel(192,128)[1]==1);
  assert(pixel(128,128)[0]==1&&pixel(128,128)[1]==0);
  readback->Unmap(0,nullptr);
  // Completed submission state survives a reset on the same native queue.
  OK(allocator->Reset());OK(cmd->Reset(allocator.Get(),nullptr));
  CollectCrops();assert(retired_variants.empty());assert(releases==creates);
  experimental_crop=true;test_feather=false;p.Set("DLSSNR.Output",private_output.Get());layout=0;preset=2;config_tick=vr_tick=GetTickCount64();
  initial_states.erase(private_output.Get());initial_states.erase(color.Get());
  assert(!states.count(cmd.Get()));assert(submitted_states.at(private_output.Get()).queue==queue.Get());
  before=creates;assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);assert(creates==before+1&&std::get<0>(calls.back())!=full);
  // The same proof must not cross queues or override an incomplete transition.
  states.clear();list_queues[cmd.Get()]=reinterpret_cast<ID3D12CommandQueue *>(uintptr_t(1));config_tick=vr_tick=GetTickCount64();
  assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);assert(std::get<0>(calls.back())==full);
  list_queues[cmd.Get()]=queue.Get();states.clear();uncertain_states[cmd.Get()].insert(private_output.Get());config_tick=vr_tick=GetTickCount64();
  assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);assert(std::get<0>(calls.back())==full);
  // An HDR input/output format mismatch must fall back before recording copies.
  ComPtr<ID3D12Resource> other_format;auto incompatible_desc=private_desc;incompatible_desc.Format=DXGI_FORMAT_R11G11B10_FLOAT;
  OK(device->CreateCommittedResource(&private_heap,D3D12_HEAP_FLAG_NONE,&incompatible_desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&other_format)));
  p.Set("DLSSNR.Output",other_format.Get());auto original_evaluate=evaluate;
  evaluate=+[](ID3D12GraphicsCommandList *,const void *h,const NVSDK_NGX_Parameter *,void *)->unsigned{assert(h==full);return 1;};
  before=creates;auto cropped_before=cropped;config_tick=vr_tick=GetTickCount64();
  assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);assert(creates==before&&cropped==cropped_before);
  assert(status.find("incompatible copy formats")!=std::string::npos);
  evaluate=original_evaluate;p.Set("DLSSNR.Output",private_output.Get());
  // Unrelated buffer barriers must not allocate texture-state history.
  D3D12_RESOURCE_DESC buffer_desc{};buffer_desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer_desc.Width=256;buffer_desc.Height=1;buffer_desc.DepthOrArraySize=buffer_desc.MipLevels=1;buffer_desc.SampleDesc.Count=1;buffer_desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> unrelated_buffer;OK(device->CreateCommittedResource(&private_heap,D3D12_HEAP_FLAG_NONE,&buffer_desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&unrelated_buffer)));
  Transition(cmd.Get(),unrelated_buffer.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);Transition(cmd.Get(),unrelated_buffer.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
  assert(!initial_states.count(unrelated_buffer.Get()));assert(!states[cmd.Get()].count(unrelated_buffer.Get()));assert(!submitted_states.count(unrelated_buffer.Get()));
  // Production compositor: reduced neural center, exact untouched boundary,
  // smooth intermediate pixels and independent packed-eye coordinates.
  uncertain_states[cmd.Get()].clear();list_queues[cmd.Get()]=queue.Get();
  native_processing=true;neural_scale=75;feather_percent=20;test_feather=false;
  preset=2;layout=0;custom_width=custom_height=0;config_tick=vr_tick=GetTickCount64();
  ID3D12DescriptorHeap *caller_heaps[]={gpu->heap.Get()};cmd->SetDescriptorHeaps(1,caller_heaps);
  cmd->SetComputeRootSignature(gpu->root.Get());cmd->SetPipelineState(mock_pipeline.Get());cmd->SetComputeRootDescriptorTable(0,gpu->heap->GetGPUDescriptorHandleForHeapStart());
  unsigned caller_words[]={0,0,256,256,0,0,0,0};cmd->SetComputeRoot32BitConstants(1,8,caller_words,0);
  Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);
  auto &processed=features.at(full).variants.back();assert(processed.processing&&processed.work_scale==75);
  assert(processed.images[0].ancillary.at("Backbuffer").Get()==processed.images[0].color.Get());
  Rect fade_region=crop({0,0,256,256},60,50);assert(processed.w<fade_region.w);
  src.pResource=private_output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
  Transition(cmd.Get(),private_output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Transition(cmd.Get(),private_output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  OK(cmd->Close());queue->ExecuteCommandLists(1,lists);OK(queue->Signal(fence.Get(),3));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);OK(fence->SetEventOnCompletion(3,event));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);
  OK(readback->Map(0,nullptr,reinterpret_cast<void **>(&data)));
  auto center_y=fade_region.y+fade_region.h/2;assert(pixel(fade_region.x,center_y)[0]==1&&pixel(fade_region.x,center_y)[1]==0);
  assert(pixel(fade_region.x+fade_region.w/2,center_y)[0]==0&&pixel(fade_region.x+fade_region.w/2,center_y)[1]==1);
  auto mid=pixel(fade_region.x+fade_region.w/10,center_y);assert(mid[0]>.35f&&mid[0]<.65f&&mid[1]>.35f&&mid[1]<.65f);
  assert(pixel(fade_region.x+fade_region.w-1,center_y)[0]==1&&pixel(fade_region.x+fade_region.w-1,center_y)[1]==0);
  assert(pixel(0,0)[0]==1&&pixel(0,0)[1]==0);readback->Unmap(0,nullptr);
  OK(allocator->Reset());OK(cmd->Reset(allocator.Get(),nullptr));CollectCrops();
  layout=1;custom_width=custom_height=75;config_tick=vr_tick=GetTickCount64();
  cmd->SetDescriptorHeaps(1,caller_heaps);cmd->SetComputeRootSignature(gpu->root.Get());cmd->SetPipelineState(mock_pipeline.Get());cmd->SetComputeRootDescriptorTable(0,gpu->heap->GetGPUDescriptorHandleForHeapStart());cmd->SetComputeRoot32BitConstants(1,8,caller_words,0);
  assert(Evaluate(cmd.Get(),full,&p,nullptr)==1);assert(features.at(full).variants.back().processing);
  Transition(cmd.Get(),private_output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Transition(cmd.Get(),private_output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  OK(cmd->Close());queue->ExecuteCommandLists(1,lists);OK(queue->Signal(fence.Get(),4));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);OK(fence->SetEventOnCompletion(4,event));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);
  OK(readback->Map(0,nullptr,reinterpret_cast<void **>(&data)));
  assert(pixel(64,128)[1]==1&&pixel(192,128)[1]==1);
  assert(pixel(16,128)[0]==1&&pixel(144,128)[0]==1&&pixel(128,128)[0]==1);
  auto left_fade=pixel(25,128),right_fade=pixel(153,128);assert(left_fade[1]>.3f&&left_fade[1]<.7f&&std::abs(left_fade[1]-right_fade[1])<.01f);
  readback->Unmap(0,nullptr);OK(allocator->Reset());OK(cmd->Reset(allocator.Get(),nullptr));CollectCrops();
  cmd->ClearState(nullptr);assert(!SavedCompute(cmd.Get()).ready);
  std::cout<<"PASS: production reduced-resolution crop, backbuffer alias reuse and smooth feather pixel readback\n";
  // Nonzero engine viewport and low-resolution guides retain their offsets.
  Rect c{120, 80, 100, 80}, a{20, 30, 400, 300}, g{5, 7, 200, 150};
  auto scaled = scale(c, a, g);
  assert(scaled.x == 55 && scaled.y == 32 && scaled.w == 50 && scaled.h == 40);
  assert(!valid({UINT_MAX, 0, 10, 10}, 256, 256));
  assert(crop({0, 0, 32, 32}, 60, 50).w == 0);
  ComPtr<ID3D12InfoQueue> info;OK(device.As(&info));
  for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T length=0;info->GetMessage(i,nullptr,&length);std::vector<unsigned char> storage(length);auto message=reinterpret_cast<D3D12_MESSAGE *>(storage.data());OK(info->GetMessage(i,message,&length));
    if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::cerr<<message->pDescription<<"\n";assert(false);}}
  // Neither a completed submission nor a discarded recording alone is
  // sufficient when another submission still references the same crop.
  auto lifetime=std::make_shared<CropUsage>();RecordCropUse(cmd.Get(),lifetime);
  auto pending=std::make_shared<CropCompletion>();
  OK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&pending->fence)));
  pending->signaled=true;lifetime->submissions.push_back(pending);
  assert(!CropComplete(*lifetime));
  RecordCropUse(cmd.Get(),features.at(full).variants.back().usage);
  ReleaseFeature(full);CollectCrops();assert(!retired_variants.empty());
  OK(cmd->Close());OK(allocator->Reset());OK(cmd->Reset(allocator.Get(),nullptr));
  assert(lifetime->recordings.empty());assert(!CropComplete(*lifetime));
  OK(pending->fence->Signal(1));assert(CropComplete(*lifetime));
  CollectCrops();assert(retired_variants.empty());assert(releases==creates);
  StopNativeObservation();
  assert(!committed_hooked && !execute_hooked && !native_barrier && !native_reset && !native_committed && !native_execute);
  std::cout << "PASS: WARP crop output, current periphery, feathered border, "
               "native parameters unchanged, separate stereo histories, reset "
               "on off, evaluation failure fallback, unknown-state fallback, "
               "feature release, offset/guide alignment\n";
}
