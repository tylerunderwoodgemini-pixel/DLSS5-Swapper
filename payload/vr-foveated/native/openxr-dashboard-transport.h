#pragma once
// API-independent UI transport. Vulkan work is submitted inside xrEndFrame,
// where OpenXR already requires the application's queue synchronization.
// https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html
#include <GL/gl.h>
namespace vr_dashboard_transport {
struct Vulkan {
    XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
    VkQueue queue{};VkCommandPool pool{};VkCommandBuffer command{};VkBuffer buffer{};VkDeviceMemory memory{};VkFence fence{};
    void *mapped=nullptr;bool coherent=false,pending=false,lost=false;unsigned w=0,h=0;VkFormat format=VK_FORMAT_UNDEFINED;
    PFN_vkGetDeviceProcAddr get=nullptr;PFN_vkGetInstanceProcAddr instance_get=nullptr;
#define VK_FN(n) PFN_vk##n n=nullptr
    VK_FN(GetDeviceQueue);VK_FN(GetPhysicalDeviceMemoryProperties);VK_FN(CreateBuffer);VK_FN(GetBufferMemoryRequirements);VK_FN(AllocateMemory);VK_FN(BindBufferMemory);VK_FN(MapMemory);VK_FN(UnmapMemory);VK_FN(FlushMappedMemoryRanges);
    VK_FN(CreateCommandPool);VK_FN(AllocateCommandBuffers);VK_FN(ResetCommandPool);VK_FN(BeginCommandBuffer);VK_FN(EndCommandBuffer);VK_FN(CmdPipelineBarrier);VK_FN(CmdCopyBufferToImage);
    VK_FN(CreateFence);VK_FN(GetFenceStatus);VK_FN(WaitForFences);VK_FN(ResetFences);VK_FN(QueueSubmit);VK_FN(DestroyFence);VK_FN(DestroyCommandPool);VK_FN(DestroyBuffer);VK_FN(FreeMemory);
#undef VK_FN
    template<class T>bool Device(const char *name,T &fn){fn=(T)get(binding.device,name);return fn!=nullptr;}
    bool Init(unsigned width,unsigned height,VkFormat f){
        w=width;h=height;format=f;auto module=GetModuleHandleW(L"vulkan-1.dll");if(!module)return false;
        instance_get=(PFN_vkGetInstanceProcAddr)GetProcAddress(module,"vkGetInstanceProcAddr");
        if(!instance_get)return false;get=(PFN_vkGetDeviceProcAddr)instance_get(binding.instance,"vkGetDeviceProcAddr");if(!get)return false;
        GetPhysicalDeviceMemoryProperties=(PFN_vkGetPhysicalDeviceMemoryProperties)instance_get(binding.instance,"vkGetPhysicalDeviceMemoryProperties");
        if(!GetPhysicalDeviceMemoryProperties)return false;
#define VK_LOAD(n) if(!Device("vk" #n,n))return false
        VK_LOAD(GetDeviceQueue);VK_LOAD(CreateBuffer);VK_LOAD(GetBufferMemoryRequirements);VK_LOAD(AllocateMemory);VK_LOAD(BindBufferMemory);VK_LOAD(MapMemory);VK_LOAD(UnmapMemory);VK_LOAD(FlushMappedMemoryRanges);
        VK_LOAD(CreateCommandPool);VK_LOAD(AllocateCommandBuffers);VK_LOAD(ResetCommandPool);VK_LOAD(BeginCommandBuffer);VK_LOAD(EndCommandBuffer);VK_LOAD(CmdPipelineBarrier);VK_LOAD(CmdCopyBufferToImage);
        VK_LOAD(CreateFence);VK_LOAD(GetFenceStatus);VK_LOAD(WaitForFences);VK_LOAD(ResetFences);VK_LOAD(QueueSubmit);VK_LOAD(DestroyFence);VK_LOAD(DestroyCommandPool);VK_LOAD(DestroyBuffer);VK_LOAD(FreeMemory);
#undef VK_LOAD
        GetDeviceQueue(binding.device,binding.queueFamilyIndex,binding.queueIndex,&queue);if(!queue)return false;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=uint64_t(w)*h*4;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(CreateBuffer(binding.device,&bi,nullptr,&buffer)!=VK_SUCCESS)return false;
        VkMemoryRequirements requirements{};GetBufferMemoryRequirements(binding.device,buffer,&requirements);
        VkPhysicalDeviceMemoryProperties props{};GetPhysicalDeviceMemoryProperties(binding.physicalDevice,&props);unsigned selected=~0u;
        for(unsigned i=0;i<props.memoryTypeCount;++i)if((requirements.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)){
            selected=i;if(props.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)break;}
        if(selected==~0u)return false;coherent=(props.memoryTypes[selected].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=requirements.size;ai.memoryTypeIndex=selected;
        if(AllocateMemory(binding.device,&ai,nullptr,&memory)!=VK_SUCCESS||BindBufferMemory(binding.device,buffer,memory,0)!=VK_SUCCESS||MapMemory(binding.device,memory,0,VK_WHOLE_SIZE,0,&mapped)!=VK_SUCCESS)return false;
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=binding.queueFamilyIndex;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if(CreateCommandPool(binding.device,&pi,nullptr,&pool)!=VK_SUCCESS)return false;
        VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ci.commandPool=pool;ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ci.commandBufferCount=1;
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        return AllocateCommandBuffers(binding.device,&ci,&command)==VK_SUCCESS&&CreateFence(binding.device,&fi,nullptr,&fence)==VK_SUCCESS;
    }
    bool Busy(){if(!pending)return false;auto result=GetFenceStatus(binding.device,fence);if(result==VK_SUCCESS){pending=false;return false;}if(result!=VK_NOT_READY){lost=true;pending=false;return false;}return true;}
    bool Upload(VkImage image,const uint8_t *pixels){
        if(!image||!pixels||!mapped||Busy())return false;
        memcpy(mapped,pixels,size_t(w)*h*4);
        if(format==VK_FORMAT_R8G8B8A8_UNORM||format==VK_FORMAT_R8G8B8A8_SRGB){auto bytes=(uint8_t *)mapped;for(size_t i=0;i<size_t(w)*h;++i)std::swap(bytes[i*4],bytes[i*4+2]);}
        if(!coherent){VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=memory;range.size=VK_WHOLE_SIZE;if(FlushMappedMemoryRanges(binding.device,1,&range)!=VK_SUCCESS)return false;}
        if(ResetCommandPool(binding.device,pool,0)!=VK_SUCCESS||ResetFences(binding.device,1,&fence)!=VK_SUCCESS)return false;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(BeginCommandBuffer(command,&begin)!=VK_SUCCESS)return false;
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};
        CmdCopyBufferToImage(command,buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        std::swap(b.oldLayout,b.newLayout);std::swap(b.srcAccessMask,b.dstAccessMask);
        CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&b);
        if(EndCommandBuffer(command)!=VK_SUCCESS)return false;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        if(QueueSubmit(queue,1,&submit,fence)!=VK_SUCCESS)return false;pending=true;return true;
    }
    bool Dispose(){
        if(pending){auto result=WaitForFences(binding.device,1,&fence,VK_TRUE,250000000);if(result!=VK_SUCCESS&&result!=VK_ERROR_DEVICE_LOST)return false;pending=false;}
        if(fence&&DestroyFence)DestroyFence(binding.device,fence,nullptr);fence={};
        if(pool&&DestroyCommandPool)DestroyCommandPool(binding.device,pool,nullptr);pool={};
        if(mapped&&UnmapMemory)UnmapMemory(binding.device,memory);mapped=nullptr;
        if(buffer&&DestroyBuffer)DestroyBuffer(binding.device,buffer,nullptr);buffer={};
        if(memory&&FreeMemory)FreeMemory(binding.device,memory,nullptr);memory={};return true;
    }
};
struct OpenGL {
    XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};HGLRC context=nullptr;unsigned w=0,h=0;std::vector<uint8_t> flipped;
    bool Init(unsigned width,unsigned height){w=width;h=height;context=wglCreateContext(binding.hDC);if(!context)return false;
        if(!wglShareLists(binding.hGLRC,context)){wglDeleteContext(context);context=nullptr;return false;}flipped.resize(size_t(w)*h*4);return true;}
    bool Upload(GLuint image,const uint8_t *pixels){
        if(!image||!pixels||!context)return false;
        const auto old=wglGetCurrentContext();const auto dc=wglGetCurrentDC();if(!wglMakeCurrent(binding.hDC,context))return false;
        // OpenXR OpenGL images have a bottom-left origin; D3D UI pixels are top-left.
        for(unsigned y=0;y<h;++y)memcpy(flipped.data()+size_t(y)*w*4,pixels+size_t(h-1-y)*w*4,size_t(w)*4);
        glBindTexture(GL_TEXTURE_2D,image);glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,0x80E1,GL_UNSIGNED_BYTE,flipped.data());
        const bool ok=glGetError()==GL_NO_ERROR;glFinish(); // Complete the shared-context write before releasing to the runtime.
        const bool restored=wglMakeCurrent(dc,old)!=FALSE;return ok&&restored;
    }
    bool Dispose(){if(context){if(wglGetCurrentContext()==context)wglMakeCurrent(nullptr,nullptr);if(!wglDeleteContext(context))return false;context=nullptr;}return true;}
};
}
