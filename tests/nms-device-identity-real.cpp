#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
static const GUID raw_guid={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
struct Wrapper: IUnknown {
 ID3D12Device* device; ULONG refs=1;
 Wrapper(ID3D12Device* d):device(d){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {*out=nullptr;if(iid==raw_guid){*out=device;device->AddRef();return S_OK;}if(iid==IID_IUnknown){*out=this;AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release() override{return --refs;}
};
int main(int argc,char** argv){
 std::ifstream file(argc>1?argv[1]:"payload/deep-fried-chicken/deep-fried-chicken-nms.addon64",std::ios::binary);
 std::vector<char> data((std::istreambuf_iterator<char>(file)),{});if(data.empty()){printf("Cannot read candidate: stream fail=%d argc=%d\n",int(file.fail()),argc);return 1;}
 auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(data.data());auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(data.data()+dos->e_lfanew);
 auto image=static_cast<char*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!image)return 2;
 memcpy(image,data.data(),nt->OptionalHeader.SizeOfHeaders);auto sections=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)memcpy(image+sections[i].VirtualAddress,data.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
 FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
 using Check=HRESULT(*)(ID3D12Resource*,IUnknown*);auto check=reinterpret_cast<Check>(image+0x2d520);
 ComPtr<ID3D12Device> a,b;if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&a))))return 3;ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))return 4;if(FAILED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&b))))return 4;
 D3D12_HEAP_PROPERTIES heap={};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=65536;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ComPtr<ID3D12Resource> resource;if(FAILED(a->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource))))return 5;
 Wrapper wrap(a.Get()),other(b.Get());
 if(check(resource.Get(),a.Get())!=S_OK)return 6;
 if(check(resource.Get(),&wrap)!=S_OK || wrap.refs!=1)return 7;
 if(check(resource.Get(),b.Get())!=E_INVALIDARG)return 8;
 if(check(resource.Get(),&other)!=E_INVALIDARG || other.refs!=1)return 9;
 VirtualFree(image,0,MEM_RELEASE);puts("PASS: real D3D12 resource/device identity, wrapped device and different-device rejection");return 0;
}
