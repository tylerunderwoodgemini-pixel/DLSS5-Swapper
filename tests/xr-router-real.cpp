#define XR_NO_PROTOTYPES
#include <windows.h>
#include <cstdio>
#include "../payload/vr-foveated/native/openxr/openxr_loader_negotiation.h"
int main(int argc,char **argv){
    if(argc>1&&LoadLibraryW(L".\\dxgi.dll")==nullptr)return 1;
    auto router=LoadLibraryW(L".\\DLSS5OpenXRRouter.dll");if(!router)return 2;
    auto negotiate=(PFN_xrNegotiateLoaderApiLayerInterface)GetProcAddress(router,"xrNegotiateLoaderApiLayerInterface");
    XrNegotiateLoaderInfo info={};info.structType=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;info.structVersion=XR_LOADER_INFO_STRUCT_VERSION;info.structSize=sizeof(info);
    info.minInterfaceVersion=info.maxInterfaceVersion=1;info.minApiVersion=XR_MAKE_VERSION(1,0,0);info.maxApiVersion=XR_MAKE_VERSION(1,0,1000);
    XrNegotiateApiLayerRequest out={};out.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;out.structVersion=XR_API_LAYER_INFO_STRUCT_VERSION;out.structSize=sizeof(out);
    auto result=negotiate(&info,"XR_APILAYER_reshade",&out);
    HMODULE provider=nullptr;
    if(out.getInstanceProcAddr)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)out.getInstanceProcAddr,&provider);
    auto adapter=GetModuleHandleW(L"dlss5-vr-compat.addon64");
    if(result!=XR_SUCCESS||!out.createApiLayerInstance||!adapter||provider!=adapter){printf("FAIL real router result=%d provider=%p adapter=%p\n",result,provider,adapter);return 3;}
    if(GetModuleHandleW(L"ReShade64.dll")){puts("FAIL loaded shared fallback alongside local owner");return 4;}
    puts(argc>1?"PASS real modified ReShade router reuses local owner and selects generic adapter":"PASS early OpenXR loads local modified ReShade and selects generic adapter without shared fallback");return 0;
}
