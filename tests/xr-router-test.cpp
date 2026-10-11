#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    SetCurrentDirectoryA(argv[2]);
    wchar_t config[MAX_PATH]={};GetModuleFileNameW(nullptr,config,MAX_PATH);wcsrchr(config,L'\\')[1]=0;wcscat_s(config,L"dlss5-openxr-vulkan.ini");
    const bool configured=strcmp(argv[1],"vulkan")==0;
    if(configured){wchar_t library[MAX_PATH]={};GetFullPathNameW(L"ExistingReShade.dll",MAX_PATH,library,nullptr);WritePrivateProfileStringW(L"Routing",L"VulkanOwner",library,config);}
    HMODULE existing=nullptr;
    if((strcmp(argv[1],"existing")==0||strcmp(argv[1],"adapter")==0))existing=LoadLibraryA(".\\ExistingReShade.dll");
    HMODULE router=LoadLibraryA(".\\DLSS5OpenXRRouter.dll");
    if(!router)return 3;
    auto fn=reinterpret_cast<int32_t(*)(const void *,const char *,void *)>(GetProcAddress(router,"xrNegotiateLoaderApiLayerInterface"));
    if(!fn)return 4;
    int expected=strcmp(argv[1],"adapter")==0?333:(strcmp(argv[1],"existing")==0||configured)?111:strcmp(argv[1],"fallback")==0?222:-6;
    int result=fn(reinterpret_cast<const void *>(1),"XR_APILAYER_reshade",reinterpret_cast<void *>(2));
    if(existing)FreeLibrary(existing);
    if(result!=expected || fn(reinterpret_cast<const void *>(1),"XR_APILAYER_reshade",reinterpret_cast<void *>(2))!=expected) {
        printf("FAIL %s expected=%d actual=%d\n",argv[1],expected,result);return 1;
    }
    if(configured)DeleteFileW(config);
    printf("PASS: %s route, argument forwarding, owner lifetime\n",argv[1]);
}
