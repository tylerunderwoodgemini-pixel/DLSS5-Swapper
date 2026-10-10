// GPU contract test using WARP and a fake NR implementation. This verifies
// recorded copies/blending and the actual NGX parameter path; it does not
// substitute for testing NVIDIA's proprietary model on a headset.
#define NATIVE_FOVEATION_TEST
#include "../payload/vr-foveated/native/native-vr-foveation.cpp"
#include <cassert>
#include <dxgi1_4.h>
#include <iostream>
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
static unsigned __cdecl MockCreate(ID3D12GraphicsCommandList *,
                                   unsigned feature, NVSDK_NGX_Parameter *p,
                                   void **h) {
  assert(feature == 18);
  unsigned w = 0, height = 0;
  p->Get("DLSSNR.Width", &w);
  p->Get("DLSSNR.Height", &height);
  assert(w && height);
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
  D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
  src.pResource = upload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint = footprint;
  dst.pResource = color.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Transition(cmd.Get(), color.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  states[cmd.Get()][color.Get()] =
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  states[cmd.Get()][output.Get()] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  GPU *gpu;
  assert(Prepare(color.Get(), output.Get(), gpu));
  const char *shader =
      "RWTexture2D<float4> Output:register(u0);cbuffer G:register(b0){uint4 "
      "r;uint f;}[numthreads(8,8,1)]void Main(uint3 "
      "p:SV_DispatchThreadID){if(p.x<r.z&&p.y<r.w)Output[r.xy+p.xy]=float4(0,1,"
      "0,1);}";
  ComPtr<ID3DBlob> blob, error;
  OK(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "Main",
                "cs_5_0", 0, 0, &blob, &error));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
  pd.pRootSignature = gpu->root.Get();
  pd.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
  OK(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&mock_pipeline)));
  Empty empty;
  NativeParameters p(&empty);
  p.Set("DLSSNR.Color", color.Get());
  p.Set("DLSSNR.Output", output.Get());
  p.Set("DLSSNR.Depth", depth.Get());
  p.Set("DLSSNR.MVec", mvec.Get());
  p.Set("DLSSNR.Width", 256u);
  p.Set("DLSSNR.Height", 256u);
  p.Set("DLSSNR.MVecScaleX", 256.f);
  config_tick = GetTickCount64();
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  assert(cropped == 1);
  assert(creates == 1);
  assert(std::get<0>(calls.back()) != full);
  assert(std::get<2>(calls.back()) == 1);
  auto rect = std::get<1>(calls.back());
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
  assert(std::get<1>(calls[calls.size() - 2]).x < 128 &&
         std::get<1>(calls.back()).x >= 128);
  fail = true;
  config_tick = GetTickCount64();
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  assert(std::get<0>(calls.back()) == full);
  fail = false;
  states.clear();
  auto before = creates;
  config_tick = GetTickCount64();
  assert(Evaluate(cmd.Get(), full, &p, nullptr) == 1);
  assert(std::get<0>(calls.back()) == full && creates == before);
  ReleaseFeature(full);
  assert(releases == creates);
  assert(features.empty());
  // Nonzero engine viewport and low-resolution guides retain their offsets.
  Rect c{120, 80, 100, 80}, a{20, 30, 400, 300}, g{5, 7, 200, 150};
  auto scaled = scale(c, a, g);
  assert(scaled.x == 55 && scaled.y == 32 && scaled.w == 50 && scaled.h == 40);
  assert(!valid({UINT_MAX, 0, 10, 10}, 256, 256));
  assert(crop({0, 0, 32, 32}, 60, 50).w == 0);
  std::cout << "PASS: WARP crop output, current periphery, feathered border, "
               "native parameters unchanged, separate stereo histories, reset "
               "on off, evaluation failure fallback, unknown-state fallback, "
               "feature release, offset/guide alignment\n";
}
