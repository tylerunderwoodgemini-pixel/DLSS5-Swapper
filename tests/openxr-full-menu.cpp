#define PALIA_QUEUE_TEST
#include "../payload/vr-foveated/native/vr-runtime-compat.cpp"
#include <cassert>
bool test_mirror_active=false;
static int selected=0;
static bool observed=false;
static float wheel_x=0,wheel_y=0;
static void TestOverlay(reshade::api::effect_runtime *){
    auto &io=ImGui::GetIO();if(!observed){observed=true;return;}
    wheel_x=io.MouseWheelH;wheel_y=io.MouseWheel;
    assert(io.MousePos.x==120&&io.MousePos.y==120);
    ImGui::SetNextWindowPos({50,50},ImGuiCond_Always);
    ImGui::SetNextWindowSize({300,200},ImGuiCond_Always);
    ImGui::SetNextWindowFocus(); ImGui::Begin("Controller test",nullptr,ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetCursorScreenPos({100,100});
    if(ImGui::Button("Select",{100,50}))++selected;
    ImGui::End();
}
int main(int argc,char **argv){
    const bool twelve=argc>1;DeleteFileW(L"ReShadeVRMenu.ini");
    auto owner=LoadLibraryW(L"dxgi.dll");assert(owner&&AddonInit(GetModuleHandleW(nullptr),owner));
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D12Device> device12;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    unsigned long long counter=0;
    if(!twelve){auto d3d=LoadLibraryW(L"d3d11.dll");auto create=(decltype(&D3D11CreateDevice))GetProcAddress(d3d,"D3D11CreateDevice");assert(SUCCEEDED(create(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)));}
    else{
        auto factory_fn=(decltype(&CreateDXGIFactory1))GetProcAddress(owner,"CreateDXGIFactory1");Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        assert(SUCCEEDED(factory_fn(IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
        auto create=(decltype(&D3D12CreateDevice))GetProcAddress(LoadLibraryW(L"d3d12.dll"),"D3D12CreateDevice");assert(SUCCEEDED(create(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device12))));
        D3D12_COMMAND_QUEUE_DESC q={};assert(SUCCEEDED(device12->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));assert(SUCCEEDED(device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));
    }
    auto drain=[&](){if(!queue)return;assert(SUCCEEDED(queue->Signal(fence.Get(),++counter)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(SUCCEEDED(fence->SetEventOnCompletion(counter,event))&&WaitForSingleObject(event,5000)==WAIT_OBJECT_0);CloseHandle(event);};
    vr_menu::Menu menu;assert(menu.Init(device.Get(),device12.Get(),queue.Get(),1024,640));
    reshade::register_addon(GetModuleHandleW(nullptr),owner);
    reshade::register_event<reshade::addon_event::reshade_overlay>(vr_menu::Overlay);
    reshade::register_event<reshade::addon_event::reshade_overlay>(TestOverlay);
    for(int i=0;i<6;++i){assert(menu.Render({120,120,false},queue.Get()));drain();}
    assert(observed);assert(menu.Render({120,120,true},queue.Get()));drain();assert(menu.Render({120,120,false},queue.Get()));drain();
    assert(selected==1);
    assert(menu.Render({120,120,false,1,-1},queue.Get()));drain();assert(wheel_x>0&&wheel_y<0);
    assert(menu.Render({120,120,false,-1,1},queue.Get()));drain();assert(wheel_x<0&&wheel_y>0);
    assert(menu.Render({120,120,false},queue.Get()));drain();assert(wheel_x==0&&wheel_y==0);
    reshade::unregister_event<reshade::addon_event::reshade_overlay>(TestOverlay);
    assert(menu.Render({800,500,false},queue.Get()));drain();
    std::vector<unsigned char> pixels(1024*640*4);assert(menu.runtime->capture_screenshot(pixels.data()));
    size_t colored=0;for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]>25||pixels[i+1]>25||pixels[i+2]>25)++colored;
    assert(colored>10000);
    FILE *f=nullptr;fopen_s(&f,"full-menu-rgba.raw","wb");assert(f);fwrite(pixels.data(),1,pixels.size(),f);fclose(f);
    reshade::unregister_event<reshade::addon_event::reshade_overlay>(TestOverlay);
    menu.Dispose();AddonUninit(GetModuleHandleW(nullptr),owner);
    puts("PASS: actual modified ReShade full menu, controller press/release button selection, GPU screenshot");
}
