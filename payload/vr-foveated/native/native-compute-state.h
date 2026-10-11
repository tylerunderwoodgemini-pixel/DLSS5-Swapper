#pragma once
// Observe only command lists used by NR. Native objects/handles are captured
// after proxy translation, then restored on that same native command list.
struct ComputeRootValue { unsigned kind=0;UINT64 value=0;std::map<UINT,UINT> constants; };
struct ComputeBindings { ID3D12RootSignature *root=nullptr;ID3D12PipelineState *pipeline=nullptr;std::map<UINT,ComputeRootValue> args;bool captured=false; };
static std::set<ID3D12GraphicsCommandList *> compute_lists;
// Learn the caller's root signature from a qualified NR evaluation. Recognize
// it before root arguments are recorded on newly rotated producer lists.
// Retaining the root prevents address reuse from misclassifying another root.
static std::map<ID3D12RootSignature *,ComPtr<ID3D12RootSignature>> nr_compute_roots;
static std::map<ID3D12GraphicsCommandList *,ComputeBindings> compute_bindings;
using SetPipelineFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,ID3D12PipelineState *);
using SetRootFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,ID3D12RootSignature *);
using SetTableFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
using SetConstantFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,UINT,UINT,UINT);
using SetConstantsFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,UINT,UINT,const void *,UINT);
using SetAddressFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,UINT,D3D12_GPU_VIRTUAL_ADDRESS);
using ClearComputeFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,ID3D12PipelineState *);
using ExecuteBundleFn=void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *,ID3D12GraphicsCommandList *);
static ClearComputeFn observed_clear;static ExecuteBundleFn observed_bundle;
static SetPipelineFn observed_pipeline;
static SetRootFn observed_root;
static SetTableFn observed_table;
static SetConstantFn observed_constant;
static SetConstantsFn observed_constants;
static SetAddressFn observed_cbv,observed_srv,observed_uav;
static void STDMETHODCALLTYPE CapturePipeline(ID3D12GraphicsCommandList *c,ID3D12PipelineState *p){observed_pipeline(c,p);if(own_bindings)return;std::lock_guard<std::recursive_mutex> lock(guard);compute_bindings[c].pipeline=p;}
static void STDMETHODCALLTYPE CaptureRoot(ID3D12GraphicsCommandList *c,ID3D12RootSignature *r){observed_root(c,r);if(own_bindings)return;std::lock_guard<std::recursive_mutex> lock(guard);auto &s=compute_bindings[c];if(s.root!=r){s.args.clear();s.captured=false;}s.root=r;
 if(r && nr_compute_roots.count(r))compute_lists.insert(c);
 if(compute_lists.count(c))s.captured=true;}
static void STDMETHODCALLTYPE CaptureTable(ID3D12GraphicsCommandList *c,UINT slot,D3D12_GPU_DESCRIPTOR_HANDLE h){observed_table(c,slot,h);if(own_bindings)return;std::lock_guard<std::recursive_mutex> lock(guard);if(compute_lists.count(c)&&slot<64)compute_bindings[c].args[slot]={1,h.ptr,{}};}
static void CaptureWords(ID3D12GraphicsCommandList *c,UINT slot,UINT count,const UINT *values,UINT offset){if(own_bindings)return;std::lock_guard<std::recursive_mutex> lock(guard);if(!compute_lists.count(c)||slot>=64||count>64||offset>64-count||!values)return;auto &arg=compute_bindings[c].args[slot];if(arg.kind!=2)arg={};arg.kind=2;for(UINT i=0;i<count;++i)arg.constants[offset+i]=values[i];}
static void STDMETHODCALLTYPE CaptureConstant(ID3D12GraphicsCommandList *c,UINT slot,UINT value,UINT offset){observed_constant(c,slot,value,offset);CaptureWords(c,slot,1,&value,offset);}
static void STDMETHODCALLTYPE CaptureConstants(ID3D12GraphicsCommandList *c,UINT slot,UINT count,const void *values,UINT offset){observed_constants(c,slot,count,values,offset);CaptureWords(c,slot,count,reinterpret_cast<const UINT *>(values),offset);}
static void CaptureAddress(ID3D12GraphicsCommandList *c,UINT slot,UINT64 value,unsigned kind){if(own_bindings)return;std::lock_guard<std::recursive_mutex> lock(guard);if(compute_lists.count(c)&&slot<64)compute_bindings[c].args[slot]={kind,value,{}};}
static void STDMETHODCALLTYPE CaptureCBV(ID3D12GraphicsCommandList *c,UINT slot,UINT64 value){observed_cbv(c,slot,value);CaptureAddress(c,slot,value,3);}
static void STDMETHODCALLTYPE CaptureSRV(ID3D12GraphicsCommandList *c,UINT slot,UINT64 value){observed_srv(c,slot,value);CaptureAddress(c,slot,value,4);}
static void STDMETHODCALLTYPE CaptureUAV(ID3D12GraphicsCommandList *c,UINT slot,UINT64 value){observed_uav(c,slot,value);CaptureAddress(c,slot,value,5);}
static void STDMETHODCALLTYPE CaptureClear(ID3D12GraphicsCommandList *c,ID3D12PipelineState *p){observed_clear(c,p);if(own_bindings)return;std::lock_guard<std::recursive_mutex> lock(guard);compute_bindings.erase(c);if(p)compute_bindings[c].pipeline=p;}
static void STDMETHODCALLTYPE CaptureBundle(ID3D12GraphicsCommandList *c,ID3D12GraphicsCommandList *bundle){observed_bundle(c,bundle);if(own_bindings)return;std::lock_guard<std::recursive_mutex> lock(guard);compute_bindings.erase(c);}
static void BindComputeTargets(void **table){observed_clear=reinterpret_cast<ClearComputeFn>(table[11]);observed_bundle=reinterpret_cast<ExecuteBundleFn>(table[27]);observed_pipeline=reinterpret_cast<SetPipelineFn>(table[25]);observed_root=reinterpret_cast<SetRootFn>(table[29]);observed_table=reinterpret_cast<SetTableFn>(table[31]);observed_constant=reinterpret_cast<SetConstantFn>(table[33]);observed_constants=reinterpret_cast<SetConstantsFn>(table[35]);observed_cbv=reinterpret_cast<SetAddressFn>(table[37]);observed_srv=reinterpret_cast<SetAddressFn>(table[39]);observed_uav=reinterpret_cast<SetAddressFn>(table[41]);}
static LONG ComputeTransaction(bool attach){
 LONG result=NO_ERROR;
#define COMPUTE_HOOK(pointer,hook) if(result==NO_ERROR && pointer)result=attach?DetourAttach(reinterpret_cast<void **>(&pointer),hook):DetourDetach(reinterpret_cast<void **>(&pointer),hook)
 COMPUTE_HOOK(observed_clear,CaptureClear);COMPUTE_HOOK(observed_bundle,CaptureBundle);COMPUTE_HOOK(observed_pipeline,CapturePipeline);COMPUTE_HOOK(observed_root,CaptureRoot);COMPUTE_HOOK(observed_table,CaptureTable);COMPUTE_HOOK(observed_constant,CaptureConstant);COMPUTE_HOOK(observed_constants,CaptureConstants);COMPUTE_HOOK(observed_cbv,CaptureCBV);COMPUTE_HOOK(observed_srv,CaptureSRV);COMPUTE_HOOK(observed_uav,CaptureUAV);
#undef COMPUTE_HOOK
 return result;
}
static void ClearComputeTargets(){observed_clear=nullptr;observed_bundle=nullptr;observed_pipeline=nullptr;observed_root=nullptr;observed_table=nullptr;observed_constant=nullptr;observed_constants=nullptr;observed_cbv=observed_srv=observed_uav=nullptr;}
static void LearnComputeRoot(ID3D12GraphicsCommandList *cmd){
 auto it=compute_bindings.find(cmd);if(it!=compute_bindings.end()&&it->second.root&&nr_compute_roots.size()<32)
   nr_compute_roots.emplace(it->second.root,it->second.root);
}
struct SavedCompute {
 ComputeBindings bindings;std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
 ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pipeline;
 bool ready=false;
 explicit SavedCompute(ID3D12GraphicsCommandList *cmd){
  auto it=compute_bindings.find(cmd);auto hs=bound_heaps.find(cmd);
  if(it==compute_bindings.end()||!it->second.root||!it->second.pipeline||!it->second.captured||hs==bound_heaps.end())return;
  bindings=it->second;root=bindings.root;pipeline=bindings.pipeline;
  for(auto h:hs->second)heaps.emplace_back(h);ready=true;
 }
 void Restore(ID3D12GraphicsCommandList *cmd)const{
  std::vector<ID3D12DescriptorHeap *> raw;for(auto &h:heaps)raw.push_back(h.Get());cmd->SetDescriptorHeaps(UINT(raw.size()),raw.data());
  cmd->SetComputeRootSignature(root.Get());cmd->SetPipelineState(pipeline.Get());
  for(const auto &entry:bindings.args){auto slot=entry.first;const auto &a=entry.second;
   switch(a.kind){case 1:cmd->SetComputeRootDescriptorTable(slot,{a.value});break;case 2:for(const auto &word:a.constants)cmd->SetComputeRoot32BitConstant(slot,word.second,word.first);break;case 3:cmd->SetComputeRootConstantBufferView(slot,a.value);break;case 4:cmd->SetComputeRootShaderResourceView(slot,a.value);break;case 5:cmd->SetComputeRootUnorderedAccessView(slot,a.value);break;}
  }
 }
};
