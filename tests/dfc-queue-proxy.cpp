#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using Microsoft::WRL::ComPtr;
static ID3D12CommandQueue *expected;
static unsigned calls;
static unsigned __cdecl Qualified(ID3D12CommandQueue *queue) {
  assert(queue == expected); ++calls; return 1;
}
int main() {
  setvbuf(stdout,nullptr,_IONBF,0); puts("load proxy");
  assert(LoadLibraryExW(L"d3d12.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32));
  auto proxy=LoadLibraryW(L"tests/vr-runtime-compat-work/dxgi.dll"); assert(proxy);
  auto create=reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(proxy,"D3D12CreateDevice")); assert(create);
  puts("create device"); ComPtr<ID3D12Device> device; assert(SUCCEEDED(create(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))));
  puts("create queue"); ComPtr<ID3D12CommandQueue> queue,raw; D3D12_COMMAND_QUEUE_DESC desc{};
  assert(SUCCEEDED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue))));
  const GUID unwrap={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
  puts("unwrap queue"); assert(SUCCEEDED(queue->QueryInterface(unwrap,reinterpret_cast<void **>(raw.GetAddressOf()))));
  assert(raw && raw.Get()!=queue.Get()); expected=raw.Get();
  puts("map patch"); std::ifstream file("payload/deep-fried-chicken/deep-fried-chicken.addon64",std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{}); assert(!bytes.empty());
  auto dos=reinterpret_cast<const IMAGE_DOS_HEADER *>(bytes.data());
  auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64 *>(bytes.data()+dos->e_lfanew);
  auto image=static_cast<unsigned char *>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)); assert(image);
  auto sections=IMAGE_FIRST_SECTION(nt);
  for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
    memcpy(image+sections[i].VirtualAddress,bytes.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
  unsigned entry=0;
  for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)if(memcmp(sections[i].Name,".dfcque",7)==0)entry=sections[i].VirtualAddress;
  assert(entry);
  // Execute the shipped wrapper on the real proxy. Replace only its internal
  // Detours installer with an identity assertion; leave QI/Release untouched.
  auto target=reinterpret_cast<unsigned long long>(&Qualified);
  image[entry+0x180]=0x48;image[entry+0x181]=0xb8;
  memcpy(image+entry+0x182,&target,8);image[entry+0x18a]=0xff;image[entry+0x18b]=0xe0;
  FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
  auto normalize=reinterpret_cast<unsigned(__cdecl *)(ID3D12CommandQueue *)>(image+entry);
  puts("execute wrapper"); for(unsigned i=0;i<100;++i){assert(normalize(queue.Get())==1);assert(normalize(raw.Get())==1);}
  assert(calls==200);VirtualFree(image,0,MEM_RELEASE);
  puts("PASS: shipped wrapper executes on actual modified ReShade queue and raw queue; 200 canonical identities");
}
