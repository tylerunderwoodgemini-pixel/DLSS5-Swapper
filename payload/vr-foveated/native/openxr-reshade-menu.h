#pragma once
// Render the real ReShade runtime and all registered add-on tabs at a fixed
// resolution. Never resize the game's mirror or headset render targets.
#include <dxgi1_4.h>
namespace vr_menu {
using Microsoft::WRL::ComPtr;
struct Input {float x=-1,y=-1;bool down=false;float scroll_x=0,scroll_y=0;};
static bool building=false;
static thread_local ImGuiIO **capture=nullptr;
static bool (*begin)(const char *,bool *,ImGuiWindowFlags)=nullptr;
inline bool Begin(const char *name,bool *open,ImGuiWindowFlags flags){
    if(capture&&name&&strcmp(name,"Viewport")&&strcmp(name,"###editor")&&!(flags&(ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings)))
        ImGui::SetNextWindowDockID(0xB0DF600F,ImGuiCond_Always);
    return begin(name,open,flags);
}
inline bool Hook(){if(begin)return true;auto table=imgui_function_table_instance();if(!table)return false;begin=table->Begin;
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());if(DetourAttach((PVOID *)&begin,(PVOID)Begin)!=NO_ERROR){DetourTransactionAbort();begin=nullptr;return false;}
    if(DetourTransactionCommit()!=NO_ERROR){begin=nullptr;return false;}return true;}

inline void Overlay(reshade::api::effect_runtime *){if(capture){*capture=&ImGui::GetIO();(*capture)->AppAcceptingEvents=false;}}
static std::recursive_mutex runtime_mutex;
static std::vector<reshade::api::effect_runtime *> targets;
static reshade::api::effect_runtime *menu_runtime=nullptr;
static thread_local bool forwarding_settings=false;
struct SettingsScope {SettingsScope(){forwarding_settings=true;}~SettingsScope(){forwarding_settings=false;}};
inline void Clear(){std::lock_guard<std::recursive_mutex> lock(runtime_mutex);targets.clear();menu_runtime=nullptr;if(begin){DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());DetourDetach((PVOID *)&begin,(PVOID)Begin);DetourTransactionCommit();begin=nullptr;}}
inline void Track(reshade::api::effect_runtime *rt){if(!building){std::lock_guard<std::recursive_mutex> lock(runtime_mutex);if(rt&&rt!=menu_runtime&&std::find(targets.begin(),targets.end(),rt)==targets.end())targets.push_back(rt);}}
inline void Forget(reshade::api::effect_runtime *rt){std::lock_guard<std::recursive_mutex> lock(runtime_mutex);targets.erase(std::remove(targets.begin(),targets.end(),rt),targets.end());if(menu_runtime==rt)menu_runtime=nullptr;}
inline bool Uniform(reshade::api::effect_runtime *rt,reshade::api::effect_uniform_variable variable,const void *data,size_t size){
    if(vr_settings_sync::forwarding)return false;
    std::lock_guard<std::recursive_mutex> lock(runtime_mutex);if(forwarding_settings||rt!=menu_runtime||!data||size>4096||size%4)return false;SettingsScope scope;
    char effect[512]={},name[512]={};rt->get_uniform_variable_effect_name(variable,effect);rt->get_uniform_variable_name(variable,name);
    reshade::api::format type;rt->get_uniform_variable_type(variable,&type);size_t count=size/4;
    for(auto target:targets){if(target==rt)continue;auto v=target->find_uniform_variable(effect,name);if(!v.handle)continue;
        if(type==reshade::api::format::r32_float)target->set_uniform_value_float(v,(const float *)data,count);
        else if(type==reshade::api::format::r32_sint)target->set_uniform_value_int(v,(const int32_t *)data,count);
        else if(type==reshade::api::format::r32_uint)target->set_uniform_value_uint(v,(const uint32_t *)data,count);
        else if(type==reshade::api::format::r32_typeless){auto values=std::make_unique<bool[]>(count);for(size_t i=0;i<count;++i)values[i]=((const uint32_t *)data)[i]!=0;target->set_uniform_value_bool(v,values.get(),count);}}
    return false;
}
inline bool Technique(reshade::api::effect_runtime *rt,reshade::api::effect_technique technique,bool enabled){
    if(vr_settings_sync::forwarding)return false;
    std::lock_guard<std::recursive_mutex> lock(runtime_mutex);if(forwarding_settings||rt!=menu_runtime)return false;SettingsScope scope;
    char effect[512]={},name[512]={};rt->get_technique_effect_name(technique,effect);rt->get_technique_name(technique,name);
    for(auto target:targets){if(target==rt)continue;auto t=target->find_technique(effect,name);if(t.handle)target->set_technique_state(t,enabled);}return false;
}
inline bool Effects(reshade::api::effect_runtime *rt,bool enabled){if(vr_settings_sync::forwarding)return false;std::lock_guard<std::recursive_mutex> lock(runtime_mutex);if(!forwarding_settings&&rt==menu_runtime){SettingsScope scope;for(auto target:targets)if(target!=rt)target->set_effects_state(enabled);}return false;}
inline void Preset(reshade::api::effect_runtime *rt,const char *path){if(vr_settings_sync::forwarding)return;std::lock_guard<std::recursive_mutex> lock(runtime_mutex);if(!forwarding_settings&&rt==menu_runtime&&path){SettingsScope scope;for(auto target:targets)if(target!=rt)target->set_current_preset_path(path);}}

inline LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
struct Menu {
    HWND window=nullptr;ComPtr<IDXGISwapChain3> chain;
    reshade::api::effect_runtime *runtime=nullptr;ImGuiIO *io=nullptr;
    ComPtr<ID3D11DeviceContext> context11;
    ComPtr<ID3D11RenderTargetView> rtv11;ComPtr<ID3D11Texture2D> readback;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    bool Init(ID3D11Device *d11,ID3D12Device *d12,ID3D12CommandQueue *queue,unsigned width,unsigned height){
        if(runtime)return true;
        WNDCLASSW wc={};wc.lpfnWndProc=Window;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DLSS5OpenXRReShadeMenu";
        RegisterClassW(&wc);
        window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"ReShade VR",WS_POPUP,0,0,width,height,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window)return false;
        auto dxgi=GetModuleHandleW(L"dxgi.dll");auto factory_fn=(decltype(&CreateDXGIFactory1))GetProcAddress(dxgi,"CreateDXGIFactory1");
        ComPtr<IDXGIFactory2> factory;if(!factory_fn||FAILED(factory_fn(IID_PPV_ARGS(&factory))))return false;
        DXGI_SWAP_CHAIN_DESC1 desc={};desc.Width=width;desc.Height=height;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swap;
        // The hidden window gets no input and is never presented to the desktop.
        building=true;auto result=factory->CreateSwapChainForHwnd(d11?(IUnknown *)d11:(IUnknown *)queue,window,&desc,nullptr,nullptr,&swap);building=false;
        if(FAILED(result)||FAILED(swap.As(&chain)))return false;
        char config[32768];GetModuleFileNameA(nullptr,config,sizeof(config));auto slash=strrchr(config,'\\');if(!slash)return false;slash[1]=0;strcat_s(config,"ReShadeVRMenu.ini");
        if(GetFileAttributesA(config)==INVALID_FILE_ATTRIBUTES){
            auto source=std::filesystem::path(config).parent_path()/"ReShade.ini";
            if(std::filesystem::exists(source)){
                std::ifstream in(source);std::ostringstream out;std::string line;bool layout=false;
                while(std::getline(in,line)){if(!line.empty()&&line[0]=='[')layout=line.rfind("[Window][",0)==0||line.rfind("[Docking]",0)==0;if(!layout)out<<line<<'\n';}
                std::ofstream file(config,std::ios::binary);file<<out.str();
            }
            WritePrivateProfileStringA("STYLE","FontSize","22",config);WritePrivateProfileStringA("STYLE","FontScale","1",config);
            WritePrivateProfileStringA("OVERLAY","TutorialProgress","4",config);
            WritePrivateProfileStringA("OVERLAY","Docking","[Docking][Data],DockSpace ID=0xB0DF600F Window=0xCC18005E Pos=8,,8 Size=1008,,624 CentralNode=1",config);
            WritePrivateProfileStringA("OVERLAY","Window","",config);WritePrivateProfileStringA("INPUT","InputProcessing","2",config);
        }
        building=true;bool ok=reshade::create_effect_runtime(d11?reshade::api::device_api::d3d11:reshade::api::device_api::d3d12,d11?(void *)d11:(void *)d12,queue,chain.Get(),config,&runtime);building=false;
        if(!ok||!runtime)return false;
        runtime->set_effects_state(true);menu_runtime=runtime;runtime->open_overlay(true,reshade::api::input_source::keyboard);
        if(d11){d11->GetImmediateContext(&context11);ComPtr<ID3D11Texture2D> buffer;if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)))||FAILED(d11->CreateRenderTargetView(buffer.Get(),nullptr,&rtv11)))return false;}
        else{
            D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
            if(FAILED(d12->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)))||FAILED(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
               FAILED(d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||FAILED(list->Close()))return false;
            ComPtr<ID3D12Resource> buffer;if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))))return false;d12->CreateRenderTargetView(buffer.Get(),nullptr,heap->GetCPUDescriptorHandleForHeapStart());
        }

        if(!Hook())return false;
        Log("Full ReShade OpenXR menu runtime created; all ReShade/add-on tabs enabled");return true;
    }

    bool ReadPixels(std::vector<uint8_t> &pixels,unsigned width,unsigned height){
        if(!context11||!chain)return false;ComPtr<ID3D11Texture2D> buffer;if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))))return false;
        if(!readback){D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);desc.BindFlags=0;desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Device> device;context11->GetDevice(&device);if(FAILED(device->CreateTexture2D(&desc,nullptr,&readback)))return false;}
        context11->CopyResource(readback.Get(),buffer.Get());D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(context11->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)))return false;
        pixels.resize(size_t(width)*height*4);for(unsigned y=0;y<height;++y)memcpy(pixels.data()+size_t(y)*width*4,(uint8_t *)map.pData+size_t(y)*map.RowPitch,width*4);
        context11->Unmap(readback.Get(),0);return true;
    }
    bool Render(const Input &pointer,ID3D12CommandQueue *queue){
        const float black[4]={0.025f,0.025f,0.025f,1};
        if(context11)context11->ClearRenderTargetView(rtv11.Get(),black);
        else{
            ComPtr<ID3D12Resource> buffer;if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)))||FAILED(allocator->Reset())||FAILED(list->Reset(allocator.Get(),nullptr)))return false;
            D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=buffer.Get();b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;b.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
            list->ResourceBarrier(1,&b);list->ClearRenderTargetView(heap->GetCPUDescriptorHandleForHeapStart(),black,0,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
            if(FAILED(list->Close()))return false;ID3D12CommandList *commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        }
        if(io){io->AppAcceptingEvents=false;io->MousePos={pointer.x,pointer.y};io->MouseDown[0]=pointer.down;for(int i=1;i<5;++i)io->MouseDown[i]=false;io->MouseDrawCursor=true;const float dt=(std::min)(0.05f,(std::max)(0.f,io->DeltaTime));io->MouseWheel=pointer.scroll_y*6.f*dt;io->MouseWheelH=pointer.scroll_x*6.f*dt;}
        capture=&io;runtime->open_overlay(true,reshade::api::input_source::keyboard);// The public zero-RTV path updates uniforms and marks effects rendered.
        // on_present then renders the real GUI without running any shader,
        // feeder pass or neural evaluation on this UI-only swap chain.
        runtime->render_effects(runtime->get_command_queue()->get_immediate_command_list(),{},{});
        reshade::update_and_present_effect_runtime(runtime);
        capture=nullptr;
        return true;
    }
    void Dispose(){if(runtime){Forget(runtime);building=true;reshade::destroy_effect_runtime(runtime);building=false;runtime=nullptr;}chain.Reset();if(window)DestroyWindow(window);window=nullptr;}
};
}
