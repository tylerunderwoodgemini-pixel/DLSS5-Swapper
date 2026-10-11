#define NATIVE_FOVEATION_TEST
#include "../payload/vr-foveated/native/native-vr-foveation.cpp"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <cassert>
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

int wmain(int argc, wchar_t **argv){
 if(argc<2||argc>3){puts("Usage: real-nr.exe <nvngx_dlssnr.dll path> [packed|reshade|reshade-packed]");return 1;}
 const bool packed=argc==3 && (wcscmp(argv[2],L"packed")==0 || wcscmp(argv[2],L"reshade-packed")==0);const bool proxy_device=argc==3 && (wcscmp(argv[2],L"reshade")==0 || wcscmp(argv[2],L"reshade-packed")==0);const unsigned width=packed?6160u:3635u,height=packed?3272u:3709u;
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 auto create_device=&D3D12CreateDevice;
 if(proxy_device){auto proxy=LoadLibraryW(L"tests/vr-runtime-compat-work/dxgi.dll");assert(proxy);create_device=reinterpret_cast<decltype(create_device)>(GetProcAddress(proxy,"D3D12CreateDevice"));assert(create_device);}
 ComPtr<ID3D12Device> device;auto result=create_device(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device));if(FAILED(result)){printf("DEVICE %08X\n",unsigned(result));return 1;}
 if(proxy_device){assert(UnwrappedNative(device.Get()).Get()!=device.Get());puts("PASS: actual modified ReShade proxy device active");}else assert(UnwrappedNative(device.Get()).Get()==device.Get());
 assert(ObserveNativeDevice(device.Get()));
 auto nr=LoadLibraryW(argv[1]);assert(nr);
 using Init=unsigned(__cdecl *)(unsigned long long,const wchar_t *,ID3D12Device *,unsigned,const NVSDK_NGX_Parameter *);
 auto init=reinterpret_cast<Init>(GetProcAddress(nr,"NVSDK_NGX_D3D12_Init_Ext"));assert(init);
 Empty empty;NativeParameters params(&empty);params.Set("DLSSNR.Enabled",1u);params.Set("DLSSNR.Width",width);params.Set("DLSSNR.Height",height);params.Set("DLSSNR.ScalingRatio",1.f);params.Set("DLSSNR.Intensity",1.f);
 auto bridge=LoadLibraryW(L"payload/deep-fried-chicken/deep-fried-chicken-nvngx.dll");assert(bridge);
 using BridgeInit=unsigned(__cdecl *)(Init,unsigned long long,const wchar_t *,ID3D12Device *,unsigned,const NVSDK_NGX_Parameter *);
 auto bridge_init=reinterpret_cast<BridgeInit>(GetProcAddress(bridge,"DfcCallNgxD3D12InitExt"));assert(bridge_init);
 auto ir=bridge_init(init,0x876232C,L"tests/vr-runtime-compat-work",device.Get(),0x15,nullptr);printf("REAL NR INIT = %08X\n",ir);fflush(stdout);if(ir!=1)return 2;
 create=reinterpret_cast<Create>(GetProcAddress(nr,"NVSDK_NGX_D3D12_CreateFeature"));evaluate=reinterpret_cast<Eval>(GetProcAddress(nr,"NVSDK_NGX_D3D12_EvaluateFeature"));release_feature=reinterpret_cast<Release>(GetProcAddress(nr,"NVSDK_NGX_D3D12_ReleaseFeature"));crop_release=release_feature;
 using Configure=unsigned(__cdecl *)(Create,Eval,Release);
 auto configure=reinterpret_cast<Configure>(GetProcAddress(bridge,"DfcConfigureNgxD3D12FeatureExports"));assert(configure&&configure(create,evaluate,release_feature)==1u);
 create=reinterpret_cast<Create>(GetProcAddress(bridge,"DfcCallNgxD3D12CreateFeature"));evaluate=reinterpret_cast<Eval>(GetProcAddress(bridge,"DfcCallNgxD3D12EvaluateFeature"));release_feature=reinterpret_cast<Release>(GetProcAddress(bridge,"DfcCallNgxD3D12ReleaseFeature"));crop_release=release_feature;
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};assert(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> cmd;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&cmd))));
 ComPtr<ID3D12Resource> color,output,depth,mv;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&color))));assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output))));
 desc.Width=packed?width:2220;desc.Height=packed?height:2264;desc.Format=DXGI_FORMAT_R32_TYPELESS;assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&depth))));desc.Format=DXGI_FORMAT_R16G16_SNORM;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&mv))));
 watched_resources.insert(color.Get());
 // Define the NR input so the intermediate outer pixels can be checked exactly.
 ComPtr<ID3D12DescriptorHeap> color_heap;D3D12_DESCRIPTOR_HEAP_DESC color_heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};assert(SUCCEEDED(device->CreateDescriptorHeap(&color_heap_desc,IID_PPV_ARGS(&color_heap))));
 D3D12_UNORDERED_ACCESS_VIEW_DESC color_uav{};color_uav.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;color_uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(color.Get(),nullptr,&color_uav,color_heap->GetCPUDescriptorHandleForHeapStart());
 ID3D12DescriptorHeap *color_heaps[]={color_heap.Get()};cmd->SetDescriptorHeaps(1,color_heaps);Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 const FLOAT input_color[]={0.25f,0.5f,0.75f,1.f};cmd->ClearUnorderedAccessViewFloat(color_heap->GetGPUDescriptorHandleForHeapStart(),color_heap->GetCPUDescriptorHandleForHeapStart(),color.Get(),input_color,0,nullptr);
 Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 params.Set("DLSSNR.Color",color.Get());params.Set("DLSSNR.Output",output.Get());params.Set("DLSSNR.Depth",depth.Get());params.Set("DLSSNR.MVec",mv.Get());params.Set("DLSSNR.Backbuffer",color.Get());params.Set("DLSSNR.Reset",1u);params.Set("DLSSNR.MVecScaleX",-1054.587036f);params.Set("DLSSNR.MVecScaleY",1075.458008f);
 void *handle=nullptr;auto cr=create(cmd.Get(),18,&params,&handle);printf("REAL NR CREATE = %08X handle=%p\n",cr,handle);fflush(stdout);if(cr!=1)return 3;
 ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));unsigned long long sequence=0;
 auto submit=[&](){auto closed=cmd->Close();printf("CLOSE %08X\n",unsigned(closed));if(FAILED(closed))return false;ID3D12CommandList *lists[]={cmd.Get()};queue->ExecuteCommandLists(1,lists);auto signaled=queue->Signal(fence.Get(),++sequence);if(FAILED(signaled)){printf("SIGNAL %08X removed=%08X\n",unsigned(signaled),unsigned(device->GetDeviceRemovedReason()));return false;}HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);fence->SetEventOnCompletion(sequence,event);auto waited=WaitForSingleObject(event,15000);CloseHandle(event);auto removed=device->GetDeviceRemovedReason();printf("GPU wait=%u removed=%08X\n",waited,unsigned(removed));fflush(stdout);if(waited!=WAIT_OBJECT_0||FAILED(removed))return false;allocator->Reset();cmd->Reset(allocator.Get(),nullptr);return true;};
 if(!submit())return 4;
 evaluate=reinterpret_cast<Eval>(GetProcAddress(nr,"NVSDK_NGX_D3D12_EvaluateFeature"));
 evaluate_via=reinterpret_cast<EvalVia>(GetProcAddress(bridge,"DfcCallNgxD3D12EvaluateFeatureVia")); assert(evaluate_via);
 ComPtr<ID3D12InfoQueue> live_info;device.As(&live_info);if(live_info)live_info->ClearStoredMessages();
 unsigned debug_errors=0;
 auto check_errors=[&](const char *stage){if(!live_info)return;for(UINT64 i=0;i<live_info->GetNumStoredMessages();++i){SIZE_T n=0;live_info->GetMessage(i,nullptr,&n);std::vector<unsigned char> bytes(n);auto m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());live_info->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++debug_errors;printf("DEBUG %s: %s\n",stage,m->pDescription);}}live_info->ClearStoredMessages();};
 native_processing=true;neural_scale=85;feather_percent=10;
 preset=2;custom_width=custom_height=15;layout=packed?1:0;test_feeder_present=feeder_vulkan=packed;config_tick=vr_tick=GetTickCount64();assert(ObserveNativeList(UnwrappedNative(cmd.Get()).Get()));
 // Emulate DFC's cached compute bindings across feature creation/evaluation.
 ComPtr<ID3DBlob> shader,root_blob,errors;
 const char *sentinel="RWByteAddressBuffer output:register(u0);RWByteAddressBuffer extra:register(u1);ByteAddressBuffer input:register(t1);cbuffer Values:register(b0){uint seed;uint pad;};cbuffer Zero:register(b1){uint zero;}; [numthreads(1,1,1)]void main(){uint value=seed+pad+zero+input.Load(0);output.Store(0,value);extra.Store(4,value^0x55);}";
 assert(SUCCEEDED(D3DCompile(sentinel,strlen(sentinel),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&shader,&errors)));
 D3D12_DESCRIPTOR_RANGE sentinel_range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};
 D3D12_ROOT_PARAMETER parameters[5]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={1,&sentinel_range};
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants={0,0,2};
 parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[2].Descriptor={1,0};
 parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[3].Descriptor={1,0};
 parameters[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameters[4].Descriptor={1,0};
 D3D12_ROOT_SIGNATURE_DESC root_desc{};root_desc.NumParameters=5;root_desc.pParameters=parameters;
 assert(SUCCEEDED(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&root_blob,&errors)));
 ComPtr<ID3D12RootSignature> sentinel_root;assert(SUCCEEDED(device->CreateRootSignature(0,root_blob->GetBufferPointer(),root_blob->GetBufferSize(),IID_PPV_ARGS(&sentinel_root))));
 D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};pipeline_desc.pRootSignature=sentinel_root.Get();pipeline_desc.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
 ComPtr<ID3D12PipelineState> sentinel_pipeline;assert(SUCCEEDED(device->CreateComputePipelineState(&pipeline_desc,IID_PPV_ARGS(&sentinel_pipeline))));
 D3D12_RESOURCE_DESC buffer_desc{};buffer_desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer_desc.Width=256;buffer_desc.Height=1;buffer_desc.DepthOrArraySize=buffer_desc.MipLevels=1;buffer_desc.SampleDesc.Count=1;buffer_desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;buffer_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 ComPtr<ID3D12Resource> sentinel_buffer;assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer_desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&sentinel_buffer))));
 D3D12_HEAP_PROPERTIES readback_heap{};readback_heap.Type=D3D12_HEAP_TYPE_READBACK;buffer_desc.Flags=D3D12_RESOURCE_FLAG_NONE;
 ComPtr<ID3D12Resource> sentinel_readback;assert(SUCCEEDED(device->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,&buffer_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&sentinel_readback))));
 ComPtr<ID3D12DescriptorHeap> sentinel_heap;D3D12_DESCRIPTOR_HEAP_DESC sentinel_heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};assert(SUCCEEDED(device->CreateDescriptorHeap(&sentinel_heap_desc,IID_PPV_ARGS(&sentinel_heap))));
 D3D12_UNORDERED_ACCESS_VIEW_DESC sentinel_uav{};sentinel_uav.Format=DXGI_FORMAT_R32_TYPELESS;sentinel_uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;sentinel_uav.Buffer.NumElements=64;sentinel_uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;device->CreateUnorderedAccessView(sentinel_buffer.Get(),nullptr,&sentinel_uav,sentinel_heap->GetCPUDescriptorHandleForHeapStart());
 ID3D12DescriptorHeap *sentinel_heaps[]={sentinel_heap.Get()};cmd->SetDescriptorHeaps(1,sentinel_heaps);
 cmd->SetComputeRootSignature(sentinel_root.Get());cmd->SetPipelineState(sentinel_pipeline.Get());cmd->SetComputeRootDescriptorTable(0,sentinel_heap->GetGPUDescriptorHandleForHeapStart());
 ComPtr<ID3D12Resource> sentinel_input;D3D12_HEAP_PROPERTIES upload_heap{};upload_heap.Type=D3D12_HEAP_TYPE_UPLOAD;
 auto input_desc=buffer_desc;input_desc.Flags=D3D12_RESOURCE_FLAG_NONE;
 assert(SUCCEEDED(device->CreateCommittedResource(&upload_heap,D3D12_HEAP_FLAG_NONE,&input_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&sentinel_input))));
 void *zero_data=nullptr;assert(SUCCEEDED(sentinel_input->Map(0,nullptr,&zero_data)));memset(zero_data,0,256);sentinel_input->Unmap(0,nullptr);
 auto bind_sentinel=[&](){cmd->SetDescriptorHeaps(1,sentinel_heaps);cmd->SetComputeRootSignature(sentinel_root.Get());cmd->SetPipelineState(sentinel_pipeline.Get());cmd->SetComputeRootDescriptorTable(0,sentinel_heap->GetGPUDescriptorHandleForHeapStart());
   unsigned words[]={0x12345678,7};cmd->SetComputeRoot32BitConstants(1,2,words,0);cmd->SetComputeRoot32BitConstant(1,0,1);
   cmd->SetComputeRootUnorderedAccessView(2,sentinel_buffer->GetGPUVirtualAddress());cmd->SetComputeRootShaderResourceView(3,sentinel_input->GetGPUVirtualAddress());cmd->SetComputeRootConstantBufferView(4,sentinel_input->GetGPUVirtualAddress());};
 bind_sentinel();
 // First NR evaluation must crop without a full-frame warmup or synthetic
 // output transitions; its state is proven by early creation observation.
 auto first_cropped=cropped;auto first_full=retained;
 NativeListWrapper inner(cmd.Get(),true),outer(&inner,false);
 assert(UnwrappedNative(reinterpret_cast<ID3D12GraphicsCommandList *>(&outer)).Get()==UnwrappedNative(cmd.Get()).Get());
 auto first_result=Evaluate(cmd.Get(),handle,&params,nullptr);
 assert(inner.refs==1 && outer.refs==1);
 assert(features.at(handle).variants.back().processing);assert(features.at(handle).variants.back().work_scale==85);
 check_errors("after first NR before sentinel");
 cmd->Dispatch(1,1,1);
 check_errors("after sentinel");
 Transition(cmd.Get(),sentinel_buffer.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
 cmd->CopyBufferRegion(sentinel_readback.Get(),0,sentinel_buffer.Get(),0,8);
 ComPtr<ID3D12Resource> border_readback;assert(SUCCEEDED(device->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,&buffer_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&border_readback))));
 D3D12_TEXTURE_COPY_LOCATION border_to{},border_from{};border_to.pResource=border_readback.Get();border_to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;border_to.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16B16A16_FLOAT,1,1,1,256};border_from.pResource=output.Get();border_from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
 D3D12_BOX border_box{0,0,0,1,1,1};Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);cmd->CopyTextureRegion(&border_to,0,0,0,&border_from,&border_box);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

 printf("REAL NR FIRST EVAL=%08X status=%s\n",first_result,status.c_str());
 if(first_result!=1 || cropped!=first_cropped+1 || retained!=first_full || !submit())return 5;
 unsigned *sentinel_value=nullptr;D3D12_RANGE read_range{0,8};assert(SUCCEEDED(sentinel_readback->Map(0,&read_range,reinterpret_cast<void **>(&sentinel_value))));assert(sentinel_value[0]==0x12345678 && sentinel_value[1]==(0x12345678^0x55));sentinel_readback->Unmap(0,nullptr);
 unsigned short *border_value=nullptr;D3D12_RANGE border_range{0,8};assert(SUCCEEDED(border_readback->Map(0,&border_range,reinterpret_cast<void **>(&border_value))));assert(border_value[0]==0x3400&&border_value[1]==0x3800&&border_value[2]==0x3a00&&border_value[3]==0x3c00);border_readback->Unmap(0,nullptr);puts("PASS: intermediate FP16 outer pixel equals NR input bit for bit");
 puts("PASS: cached caller compute bindings produce the expected GPU value after first crop creation/evaluation");
 Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 for (int frame=0;frame<8;++frame) {
 bind_sentinel();
 config_tick=vr_tick=GetTickCount64();
 Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 auto fr=Evaluate(cmd.Get(),handle,&params,nullptr);check_errors("subsequent NR");printf("REAL NR CROPPED EVAL=%08X status=%s\n",fr,status.c_str());fflush(stdout);if(fr!=1||!submit())return 6; }
 bind_sentinel();
 custom_width=custom_height=25;config_tick=vr_tick=GetTickCount64();
 Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 auto small_result=Evaluate(cmd.Get(),handle,&params,nullptr);check_errors("custom NR");printf("REAL NR CUSTOM 25x25=%08X status=%s\n",small_result,status.c_str());if(small_result!=1||!submit())return 9;
 // Exceed both old limits: eight sizes per feature and 64 retained images.
 // Three independent NR passes mimic DFC's native/private pass histories.
 std::vector<void *> resize_handles{handle};
 for(unsigned i=0;i<2;++i){void *extra=nullptr;assert(CreateCropFeature(cmd.Get(),params,&extra)==1&&extra);resize_handles.push_back(extra);}
 auto resize_full=retained;
 for(unsigned step=0;step<22;++step){
   custom_width=step==21?60:15+step;custom_height=step==21?55:16+step;
   bind_sentinel();
   config_tick=vr_tick=GetTickCount64();auto before_crop=cropped;
   for(auto h:resize_handles){assert(Evaluate(cmd.Get(),h,&params,nullptr)==1);}
   assert(cropped==before_crop+resize_handles.size()&&retained==resize_full);
   check_errors("multi-pass resize");assert(submit());CollectCrops();
   assert(retired_variants.empty());
   for(auto h:resize_handles){assert(features.at(h).variants.size()==1);assert(features.at(h).variants.back().processing);}
 }
 // Real games rotate freshly created producer lists; binding observation
 // must qualify them before Evaluate, without changing the feature route.
 auto stable_variant_handle=features.at(handle).variants.back().handles[0];
 auto stable_region_w=features.at(handle).variants.back().region_w;auto stable_region_h=features.at(handle).variants.back().region_h;
 assert(features.at(handle).variants.back().processing);
 auto saved_cmd=cmd;auto saved_allocator=allocator;
 for(unsigned rotation=0;rotation<24;++rotation){
   assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
   assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&cmd))));
   auto raw=UnwrappedNative(cmd.Get());assert(!compute_lists.count(raw.Get()));
   bind_sentinel();assert(SavedCompute(raw.Get()).ready);
   Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   config_tick=vr_tick=GetTickCount64();auto before=cropped;assert(Evaluate(cmd.Get(),handle,&params,nullptr)==1);assert(cropped==before+1);
   auto &variant=features.at(handle).variants.back();assert(variant.processing&&variant.handles[0]==stable_variant_handle&&variant.region_w==stable_region_w&&variant.region_h==stable_region_h);
   cmd->Dispatch(1,1,1);check_errors("rotating NR producer");assert(submit());CollectCrops();
   // WARP/native tests lack ReShade destroy callbacks. Explicitly end that
   // observed recording identity before allowing allocator-address reuse.
   compute_lists.erase(raw.Get());compute_bindings.erase(raw.Get());bound_heaps.erase(raw.Get());
 }
 // Invalidate one recording deliberately: fail safely without evicting or
 // reconstructing the prepared blended NR history, then recover next frame.
 auto raw_last=UnwrappedNative(cmd.Get());compute_bindings.erase(raw_last.Get());config_tick=vr_tick=GetTickCount64();
 assert(Evaluate(cmd.Get(),handle,&params,nullptr)==1);
 assert(features.at(handle).variants.back().handles[0]==stable_variant_handle);
 assert(status.find("cropped history retained")!=std::string::npos);assert(submit());
 bind_sentinel();Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);Transition(cmd.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 config_tick=vr_tick=GetTickCount64();assert(Evaluate(cmd.Get(),handle,&params,nullptr)==1);assert(features.at(handle).variants.back().handles[0]==stable_variant_handle);check_errors("state recovery");assert(submit());
 cmd=saved_cmd;allocator=saved_allocator;
 puts("PASS: 24 fresh producer lists reuse one blended neural feature without path changes/rebuilds");
 // Timestamp the complete addon route, including copies and the blend.
 // The native game's DLSS and DFC HDR codec are outside this measurement.
 D3D12_QUERY_HEAP_DESC query_desc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP,2,0};ComPtr<ID3D12QueryHeap> timer;ComPtr<ID3D12Resource> timing_readback;
 assert(SUCCEEDED(device->CreateQueryHeap(&query_desc,IID_PPV_ARGS(&timer))));assert(SUCCEEDED(device->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,&buffer_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timing_readback))));
 UINT64 frequency=0;assert(SUCCEEDED(queue->GetTimestampFrequency(&frequency)));
 custom_width=custom_height=35;
 for(unsigned mode=0;mode<4;++mode){test_duplicate_backbuffer=mode==0;native_processing=mode!=0;neural_scale=mode==2?85:mode==3?75:100;std::vector<double> milliseconds;
   for(unsigned sample=0;sample<8;++sample){bind_sentinel();config_tick=vr_tick=GetTickCount64();cmd->EndQuery(timer.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
     for(auto h:resize_handles)assert(Evaluate(cmd.Get(),h,&params,nullptr)==1);
     cmd->EndQuery(timer.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);cmd->ResolveQueryData(timer.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,timing_readback.Get(),0);check_errors("GPU benchmark");assert(submit());CollectCrops();
     UINT64 *ticks=nullptr;D3D12_RANGE range{0,16};assert(SUCCEEDED(timing_readback->Map(0,&range,reinterpret_cast<void **>(&ticks))));double ms=1000.0*double(ticks[1]-ticks[0])/double(frequency);timing_readback->Unmap(0,nullptr);if(sample>=2)milliseconds.push_back(ms);
   }
   std::sort(milliseconds.begin(),milliseconds.end());printf("GPU BENCHMARK 35x35 three NR passes: processing=%u scale=%u median_ms=%.4f\n",unsigned(native_processing),neural_scale,milliseconds[milliseconds.size()/2]);fflush(stdout);
 }
 for(unsigned i=1;i<resize_handles.size();++i)ReleaseFeature(resize_handles[i]);
 CollectCrops();assert(retired_variants.empty());
 puts("PASS: 22 crop sizes across three NR passes remain cropped and reclaim obsolete histories after GPU completion");
 assert(!features.empty());
 preset=0;config_tick=vr_tick=GetTickCount64();
 auto fallback=Evaluate(cmd.Get(),handle,&params,nullptr);check_errors("full fallback");if(fallback!=1||!submit())return 7;
 ComPtr<ID3D12InfoQueue> info; if(SUCCEEDED(device.As(&info))) { for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<unsigned char> data(size);auto message=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,message,&size);if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){puts(message->pDescription);return 8;}} }
 assert(debug_errors==0);ReleaseFeature(handle);CollectCrops();assert(retired_variants.empty());StopNativeObservation();puts("PASS: actual NVIDIA NR full-frame, cropped presets, custom 25x25 and disabled fallback GPU completion; cropped NR without extra compute binding changes");return 0;
}
