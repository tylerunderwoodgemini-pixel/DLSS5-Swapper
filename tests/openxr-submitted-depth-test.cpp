#include "../payload/vr-foveated/native/openxr-pose-layer.cpp"
#include <cassert>
#include <cstdio>
using Microsoft::WRL::ComPtr;
static std::map<XrSwapchain,std::vector<ComPtr<ID3D11Texture2D>>> backend;
static ComPtr<ID3D11Device> device;
static unsigned chain_id=100,index_to_acquire=1;
static XrResult XRAPI_CALL Create(XrSession,const XrSwapchainCreateInfo *,XrSwapchain *out){
 *out=reinterpret_cast<XrSwapchain>(uintptr_t(++chain_id));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=8;desc.Height=4;desc.ArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_D32_FLOAT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 for(unsigned i=0;i<2;++i){ComPtr<ID3D11Texture2D> texture;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture)));backend[*out].push_back(texture);}return XR_SUCCESS;
}
static XrResult XRAPI_CALL Enumerate(XrSwapchain h,unsigned cap,unsigned *count,XrSwapchainImageBaseHeader *images){
 *count=2;if(cap){auto typed=reinterpret_cast<XrSwapchainImageD3D11KHR *>(images);for(unsigned i=0;i<2;++i)typed[i].texture=backend.at(h)[i].Get();}return XR_SUCCESS;
}
static XrResult XRAPI_CALL Acquire(XrSwapchain,const XrSwapchainImageAcquireInfo *,unsigned *index){*index=index_to_acquire;return XR_SUCCESS;}
static XrResult XRAPI_CALL Wait(XrSwapchain,const XrSwapchainImageWaitInfo *){return XR_SUCCESS;}
static XrResult XRAPI_CALL Release(XrSwapchain,const XrSwapchainImageReleaseInfo *){return XR_SUCCESS;}
static XrResult XRAPI_CALL Destroy(XrSwapchain){return XR_SUCCESS;}
static XrResult XRAPI_CALL Gipa(XrInstance,const char *name,PFN_xrVoidFunction *out){
#define F(n,f) if(!strcmp(name,n)){*out=reinterpret_cast<PFN_xrVoidFunction>(f);return XR_SUCCESS;}
 F("xrCreateSwapchain",Create);F("xrEnumerateSwapchainImages",Enumerate);F("xrAcquireSwapchainImage",Acquire);F("xrWaitSwapchainImage",Wait);F("xrReleaseSwapchainImage",Release);F("xrDestroySwapchain",Destroy);
#undef F
 return XR_ERROR_FUNCTION_UNSUPPORTED;
}
int main(){
 assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)));
 auto session=reinterpret_cast<XrSession>(1);Instance instance{};instance.handle=reinterpret_cast<XrInstance>(2);instance.gipa=Gipa;
 sessions[0].handle=session;sessions[0].dispatch=instance;sessions[0].d3d11=true;
 XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.usageFlags=XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
 XrSwapchain chains[2]{};for(auto &h:chains)assert(CreateDepthSwapchain(session,&info,&h)==XR_SUCCESS);
 XrCompositionLayerDepthInfoKHR depths[2]{};XrCompositionLayerProjectionView views[2]{};
 for(unsigned eye=0;eye<2;++eye){depths[eye].type=XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;depths[eye].subImage.swapchain=chains[eye];depths[eye].subImage.imageRect.extent={8,4};views[eye].next=&depths[eye];}
 XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.viewCount=2;projection.views=views;
 PublishDepth(session,100,&projection);SubmittedDepthImages read{};
 assert(!DLSS5ReadOpenXRDepthImages(&read,sizeof(read))); // No completed release.
 for(auto h:chains){unsigned index;assert(AcquireDepthImage(h,nullptr,&index)==XR_SUCCESS);assert(index==1);assert(WaitDepthImage(h,nullptr)==XR_SUCCESS);assert(ReleaseDepthImage(h,nullptr)==XR_SUCCESS);}
 assert(DLSS5ReadOpenXRDepthImages(&read,sizeof(read)));for(unsigned eye=0;eye<2;++eye){assert(read.texture[eye]==backend[chains[eye]][1].Get());read.texture[eye]->Release();}
 unsigned index;AcquireDepthImage(chains[0],nullptr,&index);assert(!DLSS5ReadOpenXRDepthImages(&read,sizeof(read)));WaitDepthImage(chains[0],nullptr);ReleaseDepthImage(chains[0],nullptr);
 sessions[0].depth.tick=GetTickCount64()-101;assert(!DLSS5ReadOpenXRDepthImages(&read,sizeof(read)));PublishDepth(session,101,&projection);
 assert(DestroyDepthSwapchain(chains[1])==XR_SUCCESS);assert(!DLSS5ReadOpenXRDepthImages(&read,sizeof(read)));
 DestroyDepthSwapchain(chains[0]);assert(depth_swapchains.empty());
 puts("PASS: actual OpenXR capture code tracks acquire/wait/release indices, rejects unreleased/stale/missing-eye depth and clears destroyed swapchains");
}
