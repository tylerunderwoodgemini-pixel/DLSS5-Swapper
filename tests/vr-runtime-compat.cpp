#define PALIA_QUEUE_TEST
#include "../payload/vr-foveated/native/vr-runtime-compat.cpp"
#include <dxgi1_4.h>
bool test_mirror_active=false;
static bool reached=false;
static DesktopPresent saved_present;
static unsigned engine_presents;
static HRESULT STDMETHODCALLTYPE FakeEnginePresent(IDXGISwapChain *chain,UINT interval,UINT flags){++engine_presents;return saved_present(chain,interval,flags);}
static XrResult XRAPI_CALL FakeSession(XrInstance,const XrSessionCreateInfo *info,XrSession *) {
    auto b=(const XrGraphicsBindingD3D12KHR *)info->next;
    void *verified=nullptr;
    reached=SUCCEEDED(b->queue->QueryInterface(queue_iid,&verified));
    if(verified)((IUnknown *)verified)->Release();
    return XR_ERROR_RUNTIME_FAILURE;
}
static unsigned xr_ends;
static XrResult XRAPI_CALL FakeEnd(XrSession,const XrFrameEndInfo *){++xr_ends;return XR_SUCCESS;}
static XrResult XRAPI_CALL FakeGipa(XrInstance,const char *name,PFN_xrVoidFunction *fn){*fn=!strcmp(name,"xrEndFrame")?(PFN_xrVoidFunction)FakeEnd:(PFN_xrVoidFunction)FakeSession;return XR_SUCCESS;}
int main(){
    auto owner=LoadLibraryW(L"dxgi.dll");if(!owner||!AddonInit(GetModuleHandleW(nullptr),owner)){puts("FAIL addon registration");return 1;}
    XrNegotiateLoaderInfo loader={};loader.structType=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    loader.structVersion=XR_LOADER_INFO_STRUCT_VERSION;loader.structSize=sizeof(loader);
    loader.minInterfaceVersion=1;loader.maxInterfaceVersion=1;loader.minApiVersion=XR_MAKE_VERSION(1,0,0);loader.maxApiVersion=XR_MAKE_VERSION(1,0,1000);
    XrNegotiateApiLayerRequest request={};request.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
    request.structVersion=XR_API_LAYER_INFO_STRUCT_VERSION;request.structSize=sizeof(request);
    if(XR_FAILED(xrNegotiateLoaderApiLayerInterface(&loader,"XR_APILAYER_reshade",&request))||request.getInstanceProcAddr!=Gipa||!request.createApiLayerInstance){puts("FAIL modified ReShade negotiation forwarding");return 6;}
    auto factory_fn=(decltype(&CreateDXGIFactory1))GetProcAddress(owner,"CreateDXGIFactory1");
    IDXGIFactory4 *factory=nullptr;IDXGIAdapter *adapter=nullptr;ID3D12Device *device=nullptr;ID3D12CommandQueue *queue=nullptr;
    if(!factory_fn||FAILED(factory_fn(__uuidof(IDXGIFactory4),(void **)&factory))||FAILED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter),(void **)&adapter))){puts("FAIL WARP factory");return 2;}
    auto d3d=LoadLibraryW(L"d3d12.dll");auto create=(decltype(&D3D12CreateDevice))GetProcAddress(d3d,"D3D12CreateDevice");
    D3D12_COMMAND_QUEUE_DESC desc={};
    if(!create||FAILED(create(adapter,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),(void **)&device))||FAILED(device->CreateCommandQueue(&desc,__uuidof(ID3D12CommandQueue),(void **)&queue))){puts("FAIL WARP queue");return 3;}
    ID3D12CommandQueue *native=nullptr;
    {std::lock_guard<std::mutex> l(guard);for(auto &p:queues)if(p.second==queue){native=p.first;native->AddRef();break;}}
    if(!native){puts("FAIL no validated queue capture");return 4;}
    owner_gipa=FakeGipa;
    PFN_xrVoidFunction end_fn=nullptr;Gipa((XrInstance)1,"xrEndFrame",&end_fn);
    if(end_fn!=(PFN_xrVoidFunction)SubmittedEndFrame || ((PFN_xrEndFrame)end_fn)((XrSession)1,nullptr)!=XR_SUCCESS || xr_ends!=1){puts("FAIL submitted pose hook dispatch");return 7;}
    XrGraphicsBindingD3D12KHR binding={XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};binding.device=device;binding.queue=native;
    XrSessionCreateInfo info={XR_TYPE_SESSION_CREATE_INFO};info.next=&binding;XrSession out={};
    auto result=CreateSession((XrInstance)1,&info,&out);
    bool pass=reached&&result==XR_ERROR_RUNTIME_FAILURE&&binding.queue==native;
    ID3D12Device *native_device=nullptr;native->GetDevice(__uuidof(ID3D12Device),(void **)&native_device);
    binding.device=native_device;reached=false;
    result=CreateSession((XrInstance)1,&info,&out);
    pass=pass&&reached&&result==XR_ERROR_RUNTIME_FAILURE&&binding.queue==native;
    D3D12_HEAP_PROPERTIES heap={};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture={};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width=16;texture.Height=16;texture.DepthOrArraySize=1;texture.MipLevels=1;
    texture.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;texture.SampleDesc.Count=1;
    ID3D12Resource *resource=nullptr;
    pass=pass&&SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void **)&resource));
    if(resource){
        ID3D12Device *before=nullptr,*after=nullptr,*other=nullptr;
        resource->GetDevice(__uuidof(ID3D12Device),(void **)&before);
        pass=pass&&before==device&&before!=native_device;
        pass=pass&&ArmDeviceIdentity(resource,GetModuleHandleW(nullptr));
        resource->GetDevice(__uuidof(ID3D12Device),(void **)&after);
        auto begin=dfc_begin,end=dfc_end;dfc_begin=dfc_end=0;
        resource->GetDevice(__uuidof(ID3D12Device),(void **)&other);
        dfc_begin=begin;dfc_end=end;
        pass=pass&&after==native_device&&other==before;
        if(before)before->Release();if(after)after->Release();if(other)other->Release();resource->Release();
    }
    if(native_device)native_device->Release();
    WNDCLASSW wc={};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"PaliaQueueTest";RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"Queue bridge test",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    DXGI_SWAP_CHAIN_DESC1 sd={};sd.Width=256;sd.Height=256;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *swap=nullptr;
    pass=pass&&window&&SUCCEEDED(factory->CreateSwapChainForHwnd(queue,window,&sd,nullptr,nullptr,&swap));
    if(swap){swap->Present(0,0);pass=pass&&ui_frames==0;test_mirror_active=true;swap->Present(0,0);}
    printf("Desktop test state: swap=%p runtime=%p pass=%d\n",swap,desktop_runtime,pass);
    if(swap&&desktop_runtime){
        auto raw=(IDXGISwapChain *)desktop_runtime->get_native();auto slot=&(*(void ***)raw)[8];
        saved_present=(DesktopPresent)*slot;DWORD old;VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&old);*slot=(void *)FakeEnginePresent;VirtualProtect(slot,sizeof(void *),old,&old);
        pass=pass&&ArmDesktop(desktop_runtime);
        unsigned before_frames=desktop_frames;
        auto p=(DesktopPresent)(*(void ***)raw)[8];p(raw,0,0);
        pass=pass&&engine_presents==1&&desktop_frames==before_frames+1&&desktop_forwards==1;
        swap->Present(0,0);
        pass=pass&&engine_presents==2&&desktop_frames==before_frames+2&&desktop_forwards==1&&ui_frames>=3;
        VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&old);*slot=(void *)saved_present;VirtualProtect(slot,sizeof(void *),old,&old);
        auto resize=swap->ResizeBuffers(2,320,240,DXGI_FORMAT_R8G8B8A8_UNORM,0);
        pass=pass&&SUCCEEDED(resize)&&ui_runtime==nullptr;
        swap->Present(0,0);pass=pass&&ui_runtime!=nullptr;
        swap->Release();
    }else pass=false;
    if(window)DestroyWindow(window);
    native->Release();queue->Release();device->Release();adapter->Release();factory->Release();AddonUninit(GetModuleHandleW(nullptr),owner);
    puts(pass ? "PASS modified ReShade WARP: queue/device compatibility, desktop final-Present UI rendering, one engine Present per frame, UI reset and recreation on resize" : "FAIL queue/device/desktop substitution");
    return pass?0:5;
}

