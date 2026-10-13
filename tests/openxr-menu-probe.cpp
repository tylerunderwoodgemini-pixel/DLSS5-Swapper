#define XR_NO_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../payload/vr-foveated/native/openxr/openxr.h"
static unsigned attached=0,frames=0,destroyed=0,hooks=0,focused=0;static XrStructureType binding=XR_TYPE_UNKNOWN;
extern "C" __declspec(dllexport) void __cdecl DLSS5MenuAttach(XrInstance,XrSession,const XrSessionCreateInfo *info,PFN_xrGetInstanceProcAddr){++attached;auto p=(const XrBaseInStructure *)info->next;while(p&&p->type==XR_TYPE_UNKNOWN)p=p->next;binding=p?p->type:XR_TYPE_UNKNOWN;}
extern "C" __declspec(dllexport) bool __cdecl DLSS5MenuFrame(XrSession,XrTime,unsigned,XrCompositionLayerQuad *out){++frames;out->type=XR_TYPE_COMPOSITION_LAYER_QUAD;out->space=(XrSpace)0x55;out->pose.orientation.w=1;out->size={1.25f,.78125f};return true;}
extern "C" __declspec(dllexport) void __cdecl DLSS5MenuDestroy(XrSession){++destroyed;}
extern "C" __declspec(dllexport) void __cdecl DLSS5MenuFocus(XrSession,XrSessionState){++focused;}
extern "C" __declspec(dllexport) void __cdecl DLSS5MenuHook(const char *,PFN_xrVoidFunction *){++hooks;}
extern "C" __declspec(dllexport) void __cdecl ReadMenuProbe(unsigned *a,unsigned *f,unsigned *d,unsigned *h,unsigned *p,unsigned *b){*a=attached;*f=frames;*d=destroyed;*h=hooks;*p=focused;*b=binding;}
