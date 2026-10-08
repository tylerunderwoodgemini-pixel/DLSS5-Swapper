#define PALIA_QUEUE_TEST
#include "../payload/vr-foveated/native/vr-runtime-compat.cpp"
bool test_mirror_active=false;
int main(){
 auto owner=LoadLibraryW(L".\\dxgi.dll");if(!owner){printf("FAIL owner load %lu\n",GetLastError());return 1;}
 LoadLibraryW(L"d3d11.dll");
 auto create=(decltype(&D3D11CreateDeviceAndSwapChain))GetProcAddress(owner,"D3D11CreateDeviceAndSwapChain");if(!create){puts("FAIL D3D11 export");return 2;}
 WNDCLASSW wc={};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"GenericMirrorTest";RegisterClassW(&wc);
 HWND window=CreateWindowW(wc.lpszClassName,L"Mirror",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
 DXGI_SWAP_CHAIN_DESC sd={};sd.BufferDesc.Width=256;sd.BufferDesc.Height=256;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;sd.OutputWindow=window;sd.Windowed=TRUE;
 IDXGISwapChain *swap=nullptr;ID3D11Device *device=nullptr;ID3D11DeviceContext *context=nullptr;
 auto result=create(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&device,nullptr,&context);
 if(FAILED(result)){printf("FAIL D3D11 creation %lx\n",result);return 3;}
 if(!AddonInit(GetModuleHandleW(nullptr),owner)){puts("FAIL addon registration");return 1;}
 reshade::api::effect_runtime *desktop=nullptr;
 if(!reshade::create_effect_runtime(reshade::api::device_api::d3d11,device,nullptr,swap,"ReShadeDesktopUI.ini",&desktop)){puts("FAIL desktop creation");return 5;}
 desktop->set_effects_state(false);CreateDesktopUI(desktop->get_command_queue(),desktop->get_native());
 swap->Present(0,0);bool pass=ui_runtime&&ui_frames==0;
 test_mirror_active=true;swap->Present(0,0);pass=pass&&ui_frames==1;
 reshade::destroy_effect_runtime(desktop);desktop=nullptr;
 swap->Release();context->Release();device->Release();DestroyWindow(window);AddonUninit(GetModuleHandleW(nullptr),owner);
 puts(pass?"PASS modified ReShade D3D11 WARP: flat UI unchanged, VR mirror UI renders once at final Present":"FAIL D3D11 mirror runtime");return pass?0:4;
}
