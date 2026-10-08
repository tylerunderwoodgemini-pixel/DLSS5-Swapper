#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    SetCurrentDirectoryA(argv[2]);
    HMODULE existing=nullptr;
    if((strcmp(argv[1],"existing")==0||strcmp(argv[1],"adapter")==0))existing=LoadLibraryA(".\\ExistingReShade.dll");
    HMODULE router=LoadLibraryA(".\\DLSS5OpenXRRouter.dll");
    if(!router)return 3;
    auto fn=reinterpret_cast<int32_t(*)(const void *,const char *,void *)>(GetProcAddress(router,"xrNegotiateLoaderApiLayerInterface"));
    if(!fn)return 4;
    int expected=strcmp(argv[1],"adapter")==0?333:strcmp(argv[1],"existing")==0?111:strcmp(argv[1],"fallback")==0?222:-6;
    int result=fn(reinterpret_cast<const void *>(1),"XR_APILAYER_reshade",reinterpret_cast<void *>(2));
    if(existing)FreeLibrary(existing);
    if(result!=expected || fn(reinterpret_cast<const void *>(1),"XR_APILAYER_reshade",reinterpret_cast<void *>(2))!=expected) {
        printf("FAIL %s expected=%d actual=%d\n",argv[1],expected,result);return 1;
    }
    printf("PASS: %s route, argument forwarding, owner lifetime\n",argv[1]);
}
