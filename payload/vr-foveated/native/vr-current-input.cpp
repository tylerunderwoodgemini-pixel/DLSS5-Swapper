#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <nvsdk_ngx_params.h>
#include <reshade.hpp>
#include <detours.h>
#include <map>
#include <mutex>
#include <vector>
#include <tuple>
#include <cstdio>
#include <intrin.h>
using Microsoft::WRL::ComPtr;
using Eval = unsigned (__cdecl *)(ID3D12GraphicsCommandList *,const void *,const NVSDK_NGX_Parameter *,void *);
using Qualify = bool (__cdecl *)(void *,void *,void *);
static Eval original{};static Qualify qualify{};static HMODULE pinned{},chicken{};
static unsigned long long bypasses{};
static bool Supported(const D3D12_RESOURCE_DESC &a,const D3D12_RESOURCE_DESC &b){
    return a.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && b.Dimension==a.Dimension &&
        a.Width==b.Width && a.Height==b.Height && a.Width && a.Height &&
        a.DepthOrArraySize==1 && b.DepthOrArraySize==1 && a.MipLevels==1 && b.MipLevels==1 &&
        a.SampleDesc.Count==1 && b.SampleDesc.Count==1 && !(a.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) &&
        (b.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}
static DXGI_FORMAT Typed(DXGI_FORMAT f){
    switch(f){case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:case DXGI_FORMAT_R10G10B10A2_UNORM:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:case DXGI_FORMAT_R16G16B16A16_FLOAT:return DXGI_FORMAT_R16G16B16A16_FLOAT;default:return DXGI_FORMAT_UNKNOWN;}
}
struct CopyState {ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pipeline;ComPtr<ID3D12DescriptorHeap> heap;ComPtr<ID3D12Resource> input,output;};
static std::mutex copy_lock;
static std::map<std::tuple<ID3D12Device *,ID3D12Resource *,ID3D12Resource *>,CopyState> copies;
static bool Prepare(ID3D12Device *device,ID3D12Resource *input,ID3D12Resource *output,CopyState &state){
    auto a=input->GetDesc(),b=output->GetDesc();if(!Supported(a,b)||Typed(a.Format)==DXGI_FORMAT_UNKNOWN||Typed(b.Format)==DXGI_FORMAT_UNKNOWN)return false;
    if(a.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || a.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return false;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT sf{Typed(a.Format)},uf{Typed(b.Format)};
    if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&sf,sizeof(sf))) || !(sf.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD) ||
       FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&uf,sizeof(uf))) || !(uf.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))return false;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1};
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={2,ranges};
    D3D12_ROOT_SIGNATURE_DESC desc{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob,error;
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)) || FAILED(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&state.root))))return false;
    const char *shader="Texture2D<float4> Input:register(t0); RWTexture2D<float4> Output:register(u0); [numthreads(8,8,1)] void Main(uint3 p:SV_DispatchThreadID){uint w,h;Output.GetDimensions(w,h);if(p.x<w&&p.y<h)Output[p.xy]=Input.Load(int3(p.xy,0));}";
    if(FAILED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"Main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error)))return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=state.root.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};if(FAILED(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&state.pipeline))))return false;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&state.heap))))return false;
    auto cpu=state.heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=Typed(a.Format);srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(input,&srv,cpu);cpu.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=Typed(b.Format);uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(output,nullptr,&uav,cpu);state.input=input;state.output=output;return true;
}
static bool CopyCurrent(ID3D12GraphicsCommandList *cmd,ID3D12Resource *input,ID3D12Resource *output){
    if(!cmd||!input||!output||input==output)return false;
    auto a=input->GetDesc(),b=output->GetDesc();if(!Supported(a,b))return false;
    ComPtr<ID3D12Device> device,other;if(FAILED(input->GetDevice(IID_PPV_ARGS(&device)))||FAILED(output->GetDevice(IID_PPV_ARGS(&other))))return false;
    // Record on the feeder's native list using native descriptor heaps, even
    // when ReShade's resource GetDevice hook returns its device proxy.
    static constexpr GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    ComPtr<ID3D12Device> native;
    if(SUCCEEDED(device->QueryInterface(unwrapped,reinterpret_cast<void **>(native.GetAddressOf())))&&native)device=native;
    native.Reset();
    if(SUCCEEDED(other->QueryInterface(unwrapped,reinterpret_cast<void **>(native.GetAddressOf())))&&native)other=native;
    if(device.Get()!=other.Get())return false;
    std::lock_guard<std::mutex> guard(copy_lock);auto key=std::make_tuple(device.Get(),input,output);auto it=copies.find(key);
    if(it==copies.end()){if(copies.size()>=8)return false;CopyState state;if(!Prepare(device.Get(),input,output,state))return false;it=copies.emplace(key,std::move(state)).first;}
    auto &s=it->second;ID3D12DescriptorHeap *heaps[]={s.heap.Get()};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(s.root.Get());cmd->SetPipelineState(s.pipeline.Get());cmd->SetComputeRootDescriptorTable(0,s.heap->GetGPUDescriptorHandleForHeapStart());cmd->Dispatch(static_cast<UINT>((b.Width+7)/8),(b.Height+7)/8,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=output;cmd->ResourceBarrier(1,&barrier);return true;
}
#ifndef VR_CURRENT_INPUT_TEST
static unsigned __cdecl Evaluate(ID3D12GraphicsCommandList *cmd,const void *handle,const NVSDK_NGX_Parameter *p,void *callback){
    // This hook is inside native SR, after DFC's outer wrapper.
    // NGX can dispatch through its own DLL; immediate caller is not ownership.
    ID3D12Resource *input{},*output{};
    const bool params = p && p->Get("Color",&input)==1 && p->Get("Output",&output)==1;
    const bool owned = params && qualify && qualify(cmd,input,output);
    if(!callback && owned && CopyCurrent(cmd,input,output)){
        const auto n=++bypasses;if(n<=3||n%1200==0){char msg[240];sprintf_s(msg,"VR current input: bypassed preliminary DLAA #%llu (%llux%u); native SR only, outer DFC wrapper retained",n,output->GetDesc().Width,output->GetDesc().Height);reshade::log::message(reshade::log::level::info,msg);}return 1;
    }
    static unsigned long long rejected=0;
    const auto n=++rejected;
    if(n<=3||n%1200==0){char msg[240];sprintf_s(msg,"VR current input: native SR retained #%llu (params=%d exactOwner=%d callback=%d bypasses=%llu)",n,params,owned,callback!=nullptr,bypasses);reshade::log::message(reshade::log::level::info,msg);}
    return original(cmd,handle,p,callback);
}
static LONG Transaction(bool attach){
    LONG result=DetourTransactionBegin();if(result!=NO_ERROR)return result;
    std::vector<HANDLE> threads;HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE){DetourTransactionAbort();return GetLastError();}
    THREADENTRY32 entry{sizeof(entry)};
    for(BOOL ok=Thread32First(snapshot,&entry);ok;ok=Thread32Next(snapshot,&entry))if(entry.th32OwnerProcessID==GetCurrentProcessId() && entry.th32ThreadID!=GetCurrentThreadId()){
        HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(thread){threads.push_back(thread);result=DetourUpdateThread(thread);if(result!=NO_ERROR)break;}
    }
    CloseHandle(snapshot);
    if(result==NO_ERROR)result=attach?DetourAttach(reinterpret_cast<void **>(&original),Evaluate):DetourDetach(reinterpret_cast<void **>(&original),Evaluate);
    if(result==NO_ERROR)result=DetourTransactionCommit();else DetourTransactionAbort();
    for(auto thread:threads)CloseHandle(thread);return result;
}
static void Begin(reshade::api::effect_runtime *rt,reshade::api::command_list *,reshade::api::resource_view,reshade::api::resource_view){
    if(original || rt->get_hwnd()!=0)return;
    const auto api=rt->get_device()->get_api();
    if(api!=reshade::api::device_api::d3d11 && api!=reshade::api::device_api::d3d12)return;
    auto feeder=GetModuleHandleW(L"dlss5-feed.addon64"),sr=GetModuleHandleW(L"nvngx_dlss.dll");chicken=GetModuleHandleW(L"deep-fried-chicken.addon64");if(!feeder||!sr||!chicken)return;
    qualify=reinterpret_cast<Qualify>(GetProcAddress(feeder,"DLSS5UseCurrentVRInput"));if(!qualify)return;
    auto fn=GetProcAddress(sr,"NVSDK_NGX_D3D12_EvaluateFeature");if(!fn)return;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(fn),&pinned))return;
    original=reinterpret_cast<Eval>(fn);auto result=Transaction(true);
    if(result!=NO_ERROR){original=nullptr;reshade::log::message(reshade::log::level::warning,"VR current input: hook failed; normal feeder path retained");}
    else reshade::log::message(reshade::log::level::info,"VR current input: preliminary DLAA bypass armed for exact feeder VR resources only");
}
extern "C" __declspec(dllexport) const char *NAME="VR current input bridge";
extern "C" __declspec(dllexport) const char *DESCRIPTION="Supplies current VR color to DFC without preliminary DLAA accumulation.";
extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE addon,HMODULE owner){if(!reshade::register_addon(addon,owner))return FALSE;reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);return TRUE;}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon,HMODULE){reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);if(original)Transaction(false);reshade::unregister_addon(addon);}
BOOL WINAPI DllMain(HMODULE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);return TRUE;}
#endif
