#pragma once
// Original implementation: bilinear crop processing and inward smoothstep
// feathering. Descriptor heaps/resources belong to the fenced crop variant.
static const char *crop_processing_shader=R"(
Texture2D<float4> Original:register(t0);Texture2D<float4> Neural:register(t1);
RWTexture2D<float4> WorkColor:register(u0);RWTexture2D<float4> FullOutput:register(u1);
cbuffer Geometry:register(b0){uint4 region;uint2 workSize;uint mode;uint fadePercent;};
float4 ReadOriginal(float2 p){float2 q=clamp(p,float2(region.xy),float2(region.xy+region.zw-1));int2 b=int2(floor(q));int2 hi=min(b+1,int2(region.xy+region.zw-1));float2 f=frac(q);return lerp(lerp(Original.Load(int3(b,0)),Original.Load(int3(hi.x,b.y,0)),f.x),lerp(Original.Load(int3(b.x,hi.y,0)),Original.Load(int3(hi,0)),f.x),f.y);}
float4 ReadNeural(float2 p){float2 q=clamp(p,0,float2(workSize-1));int2 b=int2(floor(q));int2 hi=min(b+1,int2(workSize-1));float2 f=frac(q);return lerp(lerp(Neural.Load(int3(b,0)),Neural.Load(int3(hi.x,b.y,0)),f.x),lerp(Neural.Load(int3(b.x,hi.y,0)),Neural.Load(int3(hi,0)),f.x),f.y);}
[numthreads(8,8,1)]void Main(uint3 id:SV_DispatchThreadID){
 if(mode==0){if(any(id.xy>=workSize))return;float2 p=float2(region.xy)+(float2(id.xy)+.5)*float2(region.zw)/float2(workSize)-.5;WorkColor[id.xy]=ReadOriginal(p);return;}
 if(any(id.xy>=region.zw))return;uint2 p=region.xy+id.xy;
 float2 edge=min(float2(id.xy),float2(region.zw-1-id.xy));
 float2 fade=max(1,float2(region.zw)*float(fadePercent)/100.0);
 float weight=fadePercent?smoothstep(0,fade.x,edge.x)*smoothstep(0,fade.y,edge.y):1;
 float2 q=(float2(id.xy)+.5)*float2(workSize)/float2(region.zw)-.5;
 FullOutput[p]=lerp(Original.Load(int3(p,0)),ReadNeural(q),weight);
})";
struct CropPipeline {ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pipeline;};
static std::map<ID3D12Device *,std::shared_ptr<CropPipeline>> crop_pipelines;
struct CropProcessing {
 std::shared_ptr<CropPipeline> program;ComPtr<ID3D12DescriptorHeap> heap;
 ComPtr<ID3D12Resource> source,destination;
};
static bool PrepareCropProcessing(ID3D12Resource *source,ID3D12Resource *destination,ID3D12Resource *work,ID3D12Resource *neural,CropProcessing &out){
 auto desc=source->GetDesc();if(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)return false;
 ComPtr<ID3D12Device> device; if(FAILED(source->GetDevice(IID_PPV_ARGS(&device))))return false;device=UnwrappedNative(device.Get());
 D3D12_FEATURE_DATA_FORMAT_SUPPORT support{desc.Format};
 if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||!(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)||!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))return false;
 auto found=crop_pipelines.find(device.Get());
 if(found==crop_pipelines.end()){
  if(crop_pipelines.size()>=8)return false;auto program=std::make_shared<CropPipeline>();
  D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,2}};
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,8};
  D3D12_ROOT_SIGNATURE_DESC root{2,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob,error;
  if(FAILED(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||FAILED(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&program->root))))return false;
  if(FAILED(D3DCompile(crop_processing_shader,strlen(crop_processing_shader),nullptr,nullptr,nullptr,"Main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error)))return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=program->root.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};if(FAILED(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&program->pipeline))))return false;
  found=crop_pipelines.emplace(device.Get(),std::move(program)).first;
 }
 D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,4,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&out.heap))))return false;
 auto cpu=out.heap->GetCPUDescriptorHandleForHeapStart();auto step=device->GetDescriptorHandleIncrementSize(hd.Type);
 D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
 device->CreateShaderResourceView(source,&srv,cpu);cpu.ptr+=step;device->CreateShaderResourceView(neural,&srv,cpu);cpu.ptr+=step;
 D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=desc.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
 device->CreateUnorderedAccessView(work,nullptr,&uav,cpu);cpu.ptr+=step;device->CreateUnorderedAccessView(destination,nullptr,&uav,cpu);
 out.program=found->second;out.source=source;out.destination=destination;return true;
}
static void DispatchCropProcessing(ID3D12GraphicsCommandList *cmd,const CropProcessing &gpu,Rect region,unsigned w,unsigned h,unsigned mode,unsigned fade){
 ID3D12DescriptorHeap *heaps[]={gpu.heap.Get()};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(gpu.program->root.Get());cmd->SetPipelineState(gpu.program->pipeline.Get());cmd->SetComputeRootDescriptorTable(0,gpu.heap->GetGPUDescriptorHandleForHeapStart());
 unsigned values[]={region.x,region.y,region.w,region.h,w,h,mode,fade};cmd->SetComputeRoot32BitConstants(1,8,values,0);cmd->Dispatch(((mode?region.w:w)+7)/8,((mode?region.h:h)+7)/8,1);
}
