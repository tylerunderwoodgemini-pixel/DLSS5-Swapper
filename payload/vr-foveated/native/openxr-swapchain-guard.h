#pragma once
#include <map>
#include <mutex>
namespace vr_swapchain_guard {
static PFN_xrCreateSwapchain create=nullptr;
static PFN_xrDestroySwapchain destroy=nullptr;
static std::mutex handles_mutex;
static std::map<XrSwapchain,XrSession> handles;
inline void ForgetSession(XrSession session){std::lock_guard<std::mutex> lock(handles_mutex);for(auto it=handles.begin();it!=handles.end();)if(it->second==session)it=handles.erase(it);else ++it;}
inline XrResult XRAPI_CALL Create(XrSession session,const XrSwapchainCreateInfo *info,XrSwapchain *out){
    if(!info||!out)return XR_ERROR_VALIDATION_FAILURE;
    if(!session)return XR_ERROR_HANDLE_INVALID;
    if(!create)return XR_ERROR_FUNCTION_UNSUPPORTED;
    *out=XR_NULL_HANDLE;auto result=create(session,info,out);
    char message[256];sprintf_s(message,"OpenXR swapchain create: result=%d handle=%llx size=%ux%u array=%u format=%lld usage=%llx",result,(unsigned long long)(uintptr_t)*out,info->width,info->height,info->arraySize,(long long)info->format,(unsigned long long)info->usageFlags);Log(message);
    if(XR_FAILED(result)){*out=XR_NULL_HANDLE;return result;}
    if(!*out){Log("OpenXR swapchain creation returned success without a handle; reporting runtime failure");return XR_ERROR_RUNTIME_FAILURE;}
    {std::lock_guard<std::mutex> lock(handles_mutex);handles[*out]=session;}
    return result;
}
inline XrResult XRAPI_CALL Destroy(XrSwapchain swapchain){
    if(!swapchain){Log("OpenXR null swapchain cleanup rejected before ReShade handle lookup");return XR_ERROR_HANDLE_INVALID;}
    if(!destroy)return XR_ERROR_FUNCTION_UNSUPPORTED;
    XrSession session{};
    {std::lock_guard<std::mutex> lock(handles_mutex);auto it=handles.find(swapchain);
        if(it==handles.end()){Log("OpenXR duplicate/untracked swapchain cleanup rejected before ReShade handle lookup");return XR_ERROR_HANDLE_INVALID;}
        session=it->second;handles.erase(it);}
    // Claim cleanup before calling the owner; a second Unity render/input
    // thread must not enter the owner's already-erased handle table.
    auto result=destroy(swapchain);
    if(XR_FAILED(result)){std::lock_guard<std::mutex> lock(handles_mutex);handles.emplace(swapchain,session);}
    return result;
}
inline void Hook(const char *name,PFN_xrVoidFunction *fn){
    if(!fn||!*fn)return;
    if(!strcmp(name,"xrCreateSwapchain")&&*fn!=(PFN_xrVoidFunction)Create){create=(PFN_xrCreateSwapchain)*fn;*fn=(PFN_xrVoidFunction)Create;}
    if(!strcmp(name,"xrDestroySwapchain")&&*fn!=(PFN_xrVoidFunction)Destroy){destroy=(PFN_xrDestroySwapchain)*fn;*fn=(PFN_xrVoidFunction)Destroy;}
}
}
