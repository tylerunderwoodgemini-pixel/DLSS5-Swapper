#pragma once
// A fixed-size OpenXR quad settings panel. Mouse input is deliberately the
// existing VR mod's controller mouse emulation, not a second action binding.
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <algorithm>
#include <string>
#include <cstdint>
#include <sstream>
#include <fstream>
#include <filesystem>
#include "openxr-dashboard-input.h"
#include "openxr-dashboard-block.h"
#ifndef VR_DASHBOARD_TEST
#include "openxr-reshade-menu.h"
#endif
namespace vr_dashboard {
using Microsoft::WRL::ComPtr;
constexpr unsigned width=1024,height=640;
constexpr float physical_width=1.25f,physical_height=0.78125f;
struct Pointer { int x=-1,y=-1; bool click=false,down=false; float scroll_x=0,scroll_y=0; };
inline int Hit(int x,int y) {
    if(y<220 || y>=306)return -1;
    for(int i=0;i<5;++i)if(x>=32+i*194 && x<32+i*194+184)return i;
    return -1;
}
using Read=bool (__cdecl *)(unsigned *,unsigned long long *,unsigned long long *);
using Set=bool (__cdecl *)(unsigned);
struct Native {
    Read read=nullptr;Set set=nullptr;
    Native(){auto module=GetModuleHandleW(L"native-vr-foveation-nvngx.dll.addon64");
        if(module){read=(Read)GetProcAddress(module,"DLSS5NativeFoveationRead");set=(Set)GetProcAddress(module,"DLSS5NativeFoveationSet");}}
};
struct Feeder {
    std::filesystem::path file;
    Feeder(){wchar_t name[32768];if(GetModuleFileNameW(nullptr,name,32768))file=std::filesystem::path(name).parent_path()/L"dlss5-feed.cfg";}
    static bool Key(const std::string &line,std::string &key,int &value){
        auto eq=line.find('=');if(eq==std::string::npos)return false;key=line.substr(0,eq);if(key.rfind("\xef\xbb\xbf",0)==0)key.erase(0,3);
        auto first=key.find_first_not_of(" \t");if(first==std::string::npos)return false;key.erase(0,first);auto last=key.find_last_not_of(" \t");key.resize(last+1);
        for(auto &c:key)if(c>='A'&&c<='Z')c+=32;
        if(key.rfind("vr_foveation",0)!=0)return false;char *end=nullptr;value=(int)strtol(line.c_str()+eq+1,&end,10);return end!=line.c_str()+eq+1;
    }
    bool Read(unsigned &preset){
        if(!GetModuleHandleW(L"dlss5-feed.addon64")||file.empty())return false;
        std::ifstream in(file,std::ios::binary);if(!in)return false;int enabled=0,value=2;std::string line,key;int parsed;
        while(std::getline(in,line))if(Key(line,key,parsed)){if(key=="vr_foveation")enabled=parsed;if(key=="vr_foveation_preset")value=parsed;}
        preset=enabled?((value>=1&&value<=4)?(unsigned)value:UINT_MAX):0;return true;
    }
    bool Write(unsigned preset){
        if(preset>4||file.empty())return false;std::ifstream in(file,std::ios::binary);if(!in)return false;
        std::string text((std::istreambuf_iterator<char>(in)),{});in.close();if(text.size()>1024*1024)return false;
        std::istringstream lines(text);std::ostringstream out;std::string line,key;int value;
        while(std::getline(lines,line)){if(Key(line,key,value)&&(key=="vr_foveation"||key=="vr_foveation_preset"||key=="vr_foveation_width"||key=="vr_foveation_height"))continue;out<<line<<'\n';}
        const unsigned widths[]={60,50,60,70,75},heights[]={50,45,50,50,60};
        out<<"vr_foveation="<<(preset?1:0)<<"\nvr_foveation_preset="<<(preset?preset:2)<<"\nvr_foveation_width="<<widths[preset]<<"\nvr_foveation_height="<<heights[preset]<<'\n';
        auto temp=file;temp+=L".dashboard-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
        std::ofstream stream(temp,std::ios::binary|std::ios::trunc);if(!stream)return false;auto data=out.str();stream.write(data.data(),data.size());stream.flush();bool ok=stream.good();stream.close();
        if(ok)ok=MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
        if(!ok)DeleteFileW(temp.c_str());return ok;
    }
};
struct Surface {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;void *pixels=nullptr;
    HFONT title=nullptr,font=nullptr,small_font=nullptr;
    Surface(){
        BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-(int)height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        dc=CreateCompatibleDC(nullptr);bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(dc && bitmap)old=SelectObject(dc,bitmap);
        title=CreateFontW(-46,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        font=CreateFontW(-32,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        small_font=CreateFontW(-26,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    }
    ~Surface(){if(dc&&old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(title)DeleteObject(title);if(font)DeleteObject(font);if(small_font)DeleteObject(small_font);}
    bool valid()const{return dc&&bitmap&&pixels&&title&&font&&small_font;}
    void Rect(int x,int y,int w,int h,COLORREF color){RECT r={x,y,x+w,y+h};auto b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
    void Text(int x,int y,const wchar_t *text,HFONT f,COLORREF color=RGB(235,239,246)){
        SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);TextOutW(dc,x,y,text,(int)wcslen(text));}
    void Draw(unsigned preset,unsigned long long cropped,unsigned long long full,Pointer p,bool native,bool saved=true,bool feeder=false){
        Rect(0,0,width,height,RGB(16,23,34));Rect(0,0,width,7,RGB(85,168,255));
        Text(32,28,L"DLSS5 VR settings",title);
        Text(32,91,L"F8 opens / closes this panel",small_font,RGB(150,185,225));
        Text(32,142,L"Point with your right controller. Press the trigger to select.",small_font);
        Text(32,178,L"Neural VR foveation",font);
        const wchar_t *labels[]={L"Off",L"Small",L"Balanced",L"Wide",L"Large"};
        const wchar_t *sizes[]={L"Full frame",L"50 x 45%",L"60 x 50%",L"70 x 50%",L"75 x 60%"};
        for(int i=0;i<5;++i){int x=32+i*194;bool active=preset==(unsigned)i,hover=Hit(p.x,p.y)==i;
            Rect(x,220,184,86,active?RGB(33,111,199):(hover?RGB(59,75,99):RGB(35,46,63)));
            Text(x+12,225,labels[i],font);Text(x+12,269,sizes[i],small_font);}
        Text(32,339,native?L"Changes apply immediately and are saved for this game.":L"No compatible foveation settings found for this route.",small_font,native?RGB(184,208,235):RGB(255,184,104));
        wchar_t counts[180];swprintf_s(counts,L"Cropped neural calls: %llu     Full-frame calls: %llu",cropped,full);
        Text(32,389,feeder?L"Feeder settings reload automatically within one second.":counts,small_font);
        Text(32,435,L"Game rendering and native DLSS stay full frame.",small_font);
        Text(32,481,L"Right-controller pointer; mouse emulation is also supported.",small_font);
        Text(32,530,L"Home still opens the desktop ReShade menu for other settings.",small_font);
        if(!saved)Text(32,578,L"Could not save the selection. Check the game's file permissions.",small_font,RGB(255,144,114));
        else Text(32,578,L"Press F8 to return to the game.",small_font,RGB(150,185,225));
        if(p.x>=0&&p.y>=0&&p.x<(int)width&&p.y<(int)height){
            Rect(p.x-2,p.y-13,5,27,RGB(0,0,0));Rect(p.x-13,p.y-2,27,5,RGB(0,0,0));
            Rect(p.x-1,p.y-11,3,23,RGB(255,255,255));Rect(p.x-11,p.y-1,23,3,RGB(255,255,255));}
        GdiFlush(); // Finish raster writes before the CPU reads the DIB.
        auto bytes=(uint8_t *)pixels;for(size_t i=0;i<(size_t)width*height;++i)bytes[i*4+3]=255;
    }
};
struct Panel {
    XrInstance instance={};XrSession session={};PFN_xrGetInstanceProcAddr gipa=nullptr;
    PFN_xrCreateReferenceSpace create_space=nullptr;PFN_xrDestroySpace destroy_space=nullptr;
    PFN_xrEnumerateSwapchainFormats formats=nullptr;PFN_xrCreateSwapchain create_swapchain=nullptr;
    PFN_xrDestroySwapchain destroy_swapchain=nullptr;PFN_xrEnumerateSwapchainImages enumerate=nullptr;
    PFN_xrAcquireSwapchainImage acquire=nullptr;PFN_xrWaitSwapchainImage wait=nullptr;PFN_xrReleaseSwapchainImage release=nullptr;
    XrSpace space={},view_space={},local_space={};PFN_xrLocateSpace locate=nullptr;bool anchor_pending=true;XrSwapchain swapchain={};XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    ComPtr<ID3D11Device> device11;ComPtr<ID3D11DeviceContext> context11;
    std::vector<ComPtr<ID3D11Texture2D>> images11;
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Resource> upload;ComPtr<ID3D12Fence> fence;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint={};UINT64 upload_size=0,fence_value=0;
    std::vector<ComPtr<ID3D12Resource>> images;
    std::shared_ptr<vr_dashboard_input::State> controller;
    bool input_armed=false;
    unsigned max_layers=0;
    #ifndef VR_DASHBOARD_TEST
    vr_menu::Menu menu;
#endif
    Surface surface;bool initialized=false,visible=false,key_down=false,click_down=false,ready=false,fault=false,acquired=false;
    bool save_ok=true;uint32_t image_index=0;unsigned frames=0;
    HWND window=nullptr;
    template<class T>bool Get(const char *name,T &fn){return XR_SUCCEEDED(gipa(instance,name,(PFN_xrVoidFunction *)&fn))&&fn;}
    bool Init(){
        if(initialized)return true;if(fault || !surface.valid() || (!device11 && (!device || !queue)))return false;
        if(!Get("xrLocateSpace",locate)||!Get("xrCreateReferenceSpace",create_space)||!Get("xrDestroySpace",destroy_space)||!Get("xrEnumerateSwapchainFormats",formats)||
           !Get("xrCreateSwapchain",create_swapchain)||!Get("xrDestroySwapchain",destroy_swapchain)||!Get("xrEnumerateSwapchainImages",enumerate)||
           !Get("xrAcquireSwapchainImage",acquire)||!Get("xrWaitSwapchainImage",wait)||!Get("xrReleaseSwapchainImage",release))return false;
        uint32_t count=0;if(XR_FAILED(formats(session,0,&count,nullptr))||!count||count>256)return false;
        std::vector<int64_t> options(count);if(XR_FAILED(formats(session,count,&count,options.data()))||std::find(options.begin(),options.end(),DXGI_FORMAT_B8G8R8A8_UNORM)==options.end())return false;
        XrReferenceSpaceCreateInfo si{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};si.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;si.poseInReferenceSpace.orientation.w=1;
        if(XR_FAILED(create_space(session,&si,&view_space)))return false;
        si.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;if(XR_FAILED(create_space(session,&si,&local_space)))return false;
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format=DXGI_FORMAT_B8G8R8A8_UNORM;ci.sampleCount=1;ci.width=width;ci.height=height;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
        if(XR_FAILED(create_swapchain(session,&ci,&swapchain)))return false;
        count=0;if(XR_FAILED(enumerate(swapchain,0,&count,nullptr))||!count||count>32)return false;
        if(device11){
            std::vector<XrSwapchainImageD3D11KHR> xr_images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            if(XR_FAILED(enumerate(swapchain,count,&count,(XrSwapchainImageBaseHeader *)xr_images.data())))return false;
            for(auto &i:xr_images){if(!i.texture)return false;images11.emplace_back(i.texture);}
            device11->GetImmediateContext(&context11);if(!context11)return false;
        }else{
        std::vector<XrSwapchainImageD3D12KHR> xr_images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if(XR_FAILED(enumerate(swapchain,count,&count,(XrSwapchainImageBaseHeader *)xr_images.data())))return false;
        for(auto &i:xr_images){if(!i.texture)return false;images.emplace_back(i.texture);}
        auto desc=images[0]->GetDesc();device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&upload_size);
        D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buffer={};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=upload_size;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)))||
           FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
           FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||
           FAILED(list->Close())||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return false;
        }
        quad.space=space;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.subImage.swapchain=swapchain;quad.subImage.imageRect.extent={(int)width,(int)height};
        quad.pose.orientation.w=1;quad.pose.position.z=-1.25f;quad.size={physical_width,physical_height};
        #ifndef VR_DASHBOARD_TEST
        if(!menu.Init(device11.Get(),device.Get(),queue.Get(),width,height))return false;
#endif
        initialized=true;Log("OpenXR headset dashboard ready: F8; controller mouse emulation; fixed 1024x640 quad");return true;
    }
    bool Focus(){DWORD pid=0;auto fg=GetForegroundWindow();GetWindowThreadProcessId(fg,&pid);if(pid!=GetCurrentProcessId())return false;window=fg;return true;}
    Pointer Input(XrTime time){Pointer p;
        if(controller){auto mouse=controller->Read(space,time);if(mouse.valid){p.x=mouse.x;p.y=mouse.y;p.scroll_x=mouse.scroll_x;p.scroll_y=mouse.scroll_y;if(!mouse.pressed)input_armed=true;p.down=input_armed&&mouse.pressed;p.click=p.down&&!click_down;click_down=mouse.pressed;return p;}}if(!Focus()){click_down=false;return p;}RECT r={};POINT mouse={};
        if(GetClientRect(window,&r)&&r.right>0&&r.bottom>0&&GetCursorPos(&mouse)&&ScreenToClient(window,&mouse)){
            p.x=(int)((int64_t)mouse.x*width/r.right);p.y=(int)((int64_t)mouse.y*height/r.bottom);}
        bool pressed=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;if(!pressed)input_armed=true;p.down=input_armed&&pressed;p.click=p.down&&!click_down;click_down=pressed;return p;
    }
    bool Fail(){fault=true;visible=false;if(controller)controller->visible=false;vr_dashboard_block::Set(false,window);return false;}
    bool Frame(XrTime time){
        bool focused=Focus();bool down=focused&&(GetAsyncKeyState(VK_F8)&0x8000)!=0;
        if(down&&!key_down){visible=!visible;if(visible)anchor_pending=true;input_armed=false;if(controller)controller->visible=visible;vr_dashboard_block::Set(visible,window);click_down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;Log(visible?"OpenXR headset dashboard opened":"OpenXR headset dashboard closed");}
        key_down=down;if(!visible||fault)return false;
        if(!Init()){fault=true;visible=false;if(controller)controller->visible=false;vr_dashboard_block::Set(false,window);Log("OpenXR headset dashboard initialization failed; original game layers retained");return false;}
        if(anchor_pending){
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if(XR_FAILED(locate(view_space,local_space,time,&head))||(head.locationFlags&valid)!=valid)return Fail();
            XrReferenceSpaceCreateInfo si{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};si.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;si.poseInReferenceSpace=head.pose;
            XrSpace anchor={};if(XR_FAILED(create_space(session,&si,&anchor)))return Fail();
            if(space)destroy_space(space);space=anchor;quad.space=space;anchor_pending=false;
            Log("ReShade VR menu anchored in LOCAL space at opening pose; does not follow head");
        }
        if(fence && fence->GetCompletedValue()==UINT64_MAX){return Fail();}
        if(fence && fence_value && fence->GetCompletedValue()<fence_value)return ready;
        auto pointer=Input(time);
#ifdef VR_DASHBOARD_TEST
        Native api;unsigned preset=0;unsigned long long cropped=0,full=0;bool available=api.read&&api.set&&api.read(&preset,&cropped,&full);
        Feeder feeder;bool feeder_route=!available&&feeder.Read(preset);available|=feeder_route;
        int hovered=Hit(pointer.x,pointer.y);
        if(pointer.click&&hovered>=0&&available){save_ok=feeder_route?feeder.Write((unsigned)hovered):api.set((unsigned)hovered);if(feeder_route)feeder.Read(preset);else api.read(&preset,&cropped,&full);}
        surface.Draw(preset,cropped,full,pointer,available,save_ok,feeder_route);
#endif
        if(!acquired){XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(XR_FAILED(acquire(swapchain,&ai,&image_index)))return ready;acquired=true;}
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=0;auto waited=wait(swapchain,&wi);
        if(waited==XR_TIMEOUT_EXPIRED)return ready;
        if(XR_FAILED(waited)||image_index>=(device11?images11.size():images.size())){return Fail();}
        if(device11){
            #ifdef VR_DASHBOARD_TEST
            context11->UpdateSubresource(images11[image_index].Get(),0,nullptr,surface.pixels,width*4,0);
#else
            if(!menu.Render({(float)pointer.x,(float)pointer.y,pointer.down,pointer.scroll_x,pointer.scroll_y},nullptr))return Fail();
            ComPtr<ID3D11Texture2D> buffer;if(FAILED(menu.chain->GetBuffer(0,IID_PPV_ARGS(&buffer))))return Fail();
            context11->CopyResource(images11[image_index].Get(),buffer.Get());
#endif
            context11->Flush();
        }else{
#ifndef VR_DASHBOARD_TEST
        if(!menu.Render({(float)pointer.x,(float)pointer.y,pointer.down,pointer.scroll_x,pointer.scroll_y},queue.Get()))return Fail();
#else
        void *mapped=nullptr;D3D12_RANGE read={0,0};
        if(FAILED(upload->Map(0,&read,&mapped))){return Fail();}
        for(unsigned y=0;y<height;++y)memcpy((uint8_t *)mapped+footprint.Offset+y*footprint.Footprint.RowPitch,(uint8_t *)surface.pixels+y*width*4,width*4);
        upload->Unmap(0,nullptr);
#endif
        if(FAILED(allocator->Reset())||FAILED(list->Reset(allocator.Get(),nullptr))){return Fail();}
        D3D12_RESOURCE_BARRIER barrier={};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=images[image_index].Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1,&barrier);
#ifdef VR_DASHBOARD_TEST
        D3D12_TEXTURE_COPY_LOCATION dst={};dst.pResource=images[image_index].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src={};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
#else
        ComPtr<ID3D12Resource> buffer;if(FAILED(menu.chain->GetBuffer(0,IID_PPV_ARGS(&buffer))))return Fail();
        auto source_barrier=barrier;source_barrier.Transition.pResource=buffer.Get();source_barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;source_barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1,&source_barrier);list->CopyResource(images[image_index].Get(),buffer.Get());
        std::swap(source_barrier.Transition.StateBefore,source_barrier.Transition.StateAfter);list->ResourceBarrier(1,&source_barrier);
#endif
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
        if(FAILED(list->Close())){return Fail();}
        ID3D12CommandList *commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        if(FAILED(queue->Signal(fence.Get(),++fence_value))){return Fail();}
        }
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};auto result=release(swapchain,&ri);acquired=false;
        if(XR_FAILED(result)){return Fail();}ready=true;if(++frames==1)Log("OpenXR dashboard quad submitted to both eyes");return true;
    }
    bool Dispose(){
        if(visible)vr_dashboard_block::Set(false,window);if(controller)controller->visible=false;
        if(fence && fence_value && fence->GetCompletedValue()<fence_value){
            auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return false;
            bool done=SUCCEEDED(fence->SetEventOnCompletion(fence_value,event))&&WaitForSingleObject(event,250)==WAIT_OBJECT_0;CloseHandle(event);if(!done)return false;}
        if(swapchain&&destroy_swapchain)destroy_swapchain(swapchain);swapchain={};
        #ifndef VR_DASHBOARD_TEST
        menu.Dispose();
#endif
        if(space&&destroy_space)destroy_space(space);space={};
        if(view_space&&destroy_space)destroy_space(view_space);view_space={};
        if(local_space&&destroy_space)destroy_space(local_space);local_space={};return true;
    }
};
static std::mutex mutex;
static std::map<XrSession,std::unique_ptr<Panel>> panels;
// Retain in-flight GPU resources on a failed drain instead of freeing them.
static std::vector<std::unique_ptr<Panel>> retired;
inline void Attach(XrInstance instance,XrSession session,const XrGraphicsBindingD3D12KHR &binding,PFN_xrGetInstanceProcAddr gipa,XrSystemId system){
    std::lock_guard<std::mutex> lock(mutex);if(panels.size()>=8)return;
    auto panel=std::make_unique<Panel>();panel->instance=instance;panel->session=session;panel->gipa=gipa;panel->device=binding.device;panel->queue=binding.queue;
    PFN_xrGetSystemProperties properties=nullptr;
    if(panel->Get("xrGetSystemProperties",properties)){XrSystemProperties info{XR_TYPE_SYSTEM_PROPERTIES};if(XR_SUCCEEDED(properties(instance,system,&info)))panel->max_layers=info.graphicsProperties.maxLayerCount;}
    panel->controller=vr_dashboard_input::Create(instance,session,gipa);
    panels[session]=std::move(panel);Log("OpenXR D3D12 headset dashboard attached to existing session");
}
inline void Attach11(XrInstance instance,XrSession session,const XrGraphicsBindingD3D11KHR &binding,PFN_xrGetInstanceProcAddr gipa,XrSystemId system){
    std::lock_guard<std::mutex> lock(mutex);if(panels.size()>=8)return;
    auto panel=std::make_unique<Panel>();panel->instance=instance;panel->session=session;panel->gipa=gipa;panel->device11=binding.device;
    PFN_xrGetSystemProperties properties=nullptr;
    if(panel->Get("xrGetSystemProperties",properties)){XrSystemProperties info{XR_TYPE_SYSTEM_PROPERTIES};if(XR_SUCCEEDED(properties(instance,system,&info)))panel->max_layers=info.graphicsProperties.maxLayerCount;}
    panel->controller=vr_dashboard_input::Create(instance,session,gipa);
    panels[session]=std::move(panel);Log("OpenXR D3D11 headset dashboard attached to existing session");
}
inline void Destroy(XrSession session){std::lock_guard<std::mutex> lock(mutex);auto it=panels.find(session);if(it==panels.end())return;
    vr_dashboard_input::Destroy(session);
    if(!it->second->Dispose())retired.push_back(std::move(it->second));panels.erase(it);}
inline bool Layer(XrSession session,XrTime time,unsigned layer_count,XrCompositionLayerQuad &out){std::lock_guard<std::mutex> lock(mutex);auto it=panels.find(session);
    if(it==panels.end()||!it->second->max_layers||layer_count>=it->second->max_layers||!it->second->Frame(time))return false;out=it->second->quad;return true;}
inline void Clear(){std::lock_guard<std::mutex> lock(mutex);for(auto &entry:panels){vr_dashboard_input::Destroy(entry.first);if(!entry.second->Dispose())retired.push_back(std::move(entry.second));}panels.clear();}
}
