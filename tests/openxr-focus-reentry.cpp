#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <cassert>
#include <cstdio>
#include <exception>
#include "../payload/vr-foveated/native/openxr/openxr.h"
#include "../payload/vr-foveated/native/openxr/openxr_platform.h"
using FocusFn=void(__cdecl *)(XrSession,XrSessionState);
static FocusFn focus;static unsigned callbacks=0;
static XrSession session=(XrSession)0x20;
static XrResult XRAPI_CALL Properties(XrInstance,XrSystemId,XrSystemProperties *info){
    ++callbacks;focus(session,XR_SESSION_STATE_FOCUSED);info->graphicsProperties.maxLayerCount=16;return XR_SUCCESS;
}
static XrResult XRAPI_CALL Gipa(XrInstance,const char *name,PFN_xrVoidFunction *fn){
    *fn=nullptr;if(!strcmp(name,"xrGetSystemProperties")){*fn=(PFN_xrVoidFunction)Properties;return XR_SUCCESS;}return XR_ERROR_FUNCTION_UNSUPPORTED;
}
int main(int argc,char **argv){
    assert(argc>=2);auto dll=LoadLibraryA(argv[1]);assert(dll);
    auto attach=(void(__cdecl *)(XrInstance,XrSession,const XrSessionCreateInfo*,PFN_xrGetInstanceProcAddr))GetProcAddress(dll,"DLSS5MenuAttach");
    auto destroy=(void(__cdecl *)(XrSession))GetProcAddress(dll,"DLSS5MenuDestroy");
    focus=(FocusFn)GetProcAddress(dll,"DLSS5MenuFocus");assert(attach&&destroy&&focus);
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};info.next=&binding;info.systemId=1;
    bool threw=false;
    try{attach((XrInstance)0x10,session,&info,Gipa);}catch(const std::exception &e){threw=true;printf("Caught prior failure: %s\n",e.what());}
    assert(callbacks==1);
    if(argc==3){assert(threw);puts("PASS: old addon reproduces focus callback deadlock");}
    else {assert(!threw);for(unsigned i=0;i<100;++i){focus(session,XR_SESSION_STATE_VISIBLE);focus(session,XR_SESSION_STATE_FOCUSED);}destroy(session);puts("PASS: actual rebuilt addon handles reentrant and repeated session-focus callbacks");}
    FreeLibrary(dll);
}
