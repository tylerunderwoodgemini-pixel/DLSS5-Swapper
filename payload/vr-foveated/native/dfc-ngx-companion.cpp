#include <windows.h>
#include <d3d12.h>
using Init=unsigned(__cdecl*)(unsigned long long,const wchar_t*,ID3D12Device*,unsigned,const void*);
using Create=unsigned(__cdecl*)(ID3D12GraphicsCommandList*,unsigned,void*,void**);
using Eval=unsigned(__cdecl*)(ID3D12GraphicsCommandList*,const void*,const void*,void*);
using Release=unsigned(__cdecl*)(const void*);
static Create create; static Eval evaluate; static Release release;
#define EXPORT extern "C" __declspec(dllexport) __declspec(noinline)
EXPORT unsigned DfcConfigureNgxD3D12FeatureExports(Create c,Eval e,Release r){if(!c||!e||!r)return false;create=c;evaluate=e;release=r;return true;}
EXPORT unsigned DfcCallNgxD3D12InitExt(Init fn,unsigned long long app,const wchar_t* path,ID3D12Device* device,unsigned sdk,const void* p){volatile unsigned result=fn(app,path,device,sdk,p);return result;}
EXPORT unsigned DfcCallNgxD3D12CreateFeature(ID3D12GraphicsCommandList* c,unsigned f,void* p,void** h){volatile unsigned result=create(c,f,p,h);return result;}
EXPORT unsigned DfcCallNgxD3D12EvaluateFeature(ID3D12GraphicsCommandList* c,const void* h,const void* p,void* cb){volatile unsigned result=evaluate(c,h,p,cb);return result;}
EXPORT unsigned DfcCallNgxD3D12ReleaseFeature(const void* h){volatile unsigned result=release(h);return result;}
// Explicit trampolines preserve the initialized calling module without re-entering detours.
EXPORT unsigned DfcCallNgxD3D12EvaluateFeatureVia(Eval fn,ID3D12GraphicsCommandList* c,const void* h,const void* p,void* cb){volatile unsigned result=fn(c,h,p,cb);return result;}
EXPORT unsigned DfcCallNgxD3D12ReleaseFeatureVia(Release fn,const void* h){volatile unsigned result=fn(h);return result;}
