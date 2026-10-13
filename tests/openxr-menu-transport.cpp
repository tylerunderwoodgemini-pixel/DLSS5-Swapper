#define WIN32_LEAN_AND_MEAN
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_PLATFORM_WIN32
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <unknwn.h>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "../payload/vr-foveated/native/vulkan-headers/include/vulkan/vulkan_core.h"
#include "../payload/vr-foveated/native/openxr/openxr_platform.h"
#include "../payload/vr-foveated/native/openxr-dashboard-transport.h"
#include "../payload/vr-foveated/native/openxr-menu-gesture.h"
static void Gesture(){
 MenuHoldGesture g;assert(!g.Update(true,false,false,0));assert(!g.Update(true,true,true,1));assert(!g.Update(true,true,true,3000));assert(g.Update(true,true,true,3001));
 assert(!g.Update(true,true,true,9000));assert(!g.Update(true,false,true,10000));assert(!g.Update(true,true,true,14000));
 assert(!g.Update(true,false,false,14001));assert(!g.Update(true,true,true,15000));assert(g.Update(true,true,true,18000));
 assert(!g.Update(false,true,true,18001));assert(!g.Update(true,true,true,24000));assert(!g.Update(true,false,false,24001));
 assert(!g.Update(true,true,true,25000));assert(!g.Update(true,false,true,27000));assert(!g.Update(true,true,true,28000));assert(g.Update(true,true,true,31000));
 puts("PASS: three-second chord, one toggle per hold, both-release rearm, interrupted hold and focus-loss cancellation");
}
template<class T>T Proc(PFN_vkGetDeviceProcAddr get,VkDevice d,const char *name){auto p=(T)get(d,name);assert(p);return p;}
static void VulkanTest(){
 SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE","~implicit~");
 auto module=LoadLibraryW(L"vulkan-1.dll");assert(module);auto get=(PFN_vkGetInstanceProcAddr)GetProcAddress(module,"vkGetInstanceProcAddr");assert(get);
 auto create=(PFN_vkCreateInstance)get(nullptr,"vkCreateInstance");VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VkInstance instance{};assert(create(&ci,nullptr,&instance)==VK_SUCCESS);
 auto enumerate=(PFN_vkEnumeratePhysicalDevices)get(instance,"vkEnumeratePhysicalDevices");uint32_t count=0;assert(enumerate(instance,&count,nullptr)==VK_SUCCESS&&count);std::vector<VkPhysicalDevice> devices(count);assert(enumerate(instance,&count,devices.data())==VK_SUCCESS);
 auto physical=devices[0];auto families=(PFN_vkGetPhysicalDeviceQueueFamilyProperties)get(instance,"vkGetPhysicalDeviceQueueFamilyProperties");count=0;families(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> props(count);families(physical,&count,props.data());unsigned family=~0u;for(unsigned i=0;i<count;++i)if(props[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=i;break;}assert(family!=~0u);
 float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;VkDevice device{};assert(((PFN_vkCreateDevice)get(instance,"vkCreateDevice"))(physical,&di,nullptr,&device)==VK_SUCCESS);
 auto get_device=(PFN_vkGetDeviceProcAddr)get(instance,"vkGetDeviceProcAddr");VkPhysicalDeviceMemoryProperties memory_props{};((PFN_vkGetPhysicalDeviceMemoryProperties)get(instance,"vkGetPhysicalDeviceMemoryProperties"))(physical,&memory_props);
 for(auto format:{VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM}){
  vr_dashboard_transport::Vulkan transport;transport.binding.instance=instance;transport.binding.physicalDevice=physical;transport.binding.device=device;transport.binding.queueFamilyIndex=family;transport.binding.queueIndex=0;assert(transport.Init(4,2,format));
  VkImageCreateInfo im{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};im.imageType=VK_IMAGE_TYPE_2D;im.format=format;im.extent={4,2,1};im.mipLevels=1;im.arrayLayers=1;im.samples=VK_SAMPLE_COUNT_1_BIT;im.tiling=VK_IMAGE_TILING_OPTIMAL;im.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;im.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  VkImage image{};assert(Proc<PFN_vkCreateImage>(get_device,device,"vkCreateImage")(device,&im,nullptr,&image)==VK_SUCCESS);VkMemoryRequirements req{};Proc<PFN_vkGetImageMemoryRequirements>(get_device,device,"vkGetImageMemoryRequirements")(device,image,&req);unsigned memory_type=0;while(!(req.memoryTypeBits&(1u<<memory_type)))++memory_type;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memory_type;VkDeviceMemory image_memory{};assert(transport.AllocateMemory(device,&ai,nullptr,&image_memory)==VK_SUCCESS);assert(Proc<PFN_vkBindImageMemory>(get_device,device,"vkBindImageMemory")(device,image,image_memory,0)==VK_SUCCESS);
  auto begin=[&]{assert(transport.ResetCommandPool(device,transport.pool,0)==VK_SUCCESS);VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};assert(transport.BeginCommandBuffer(transport.command,&b)==VK_SUCCESS);};
  auto submit=[&]{assert(transport.EndCommandBuffer(transport.command)==VK_SUCCESS);assert(transport.ResetFences(device,1,&transport.fence)==VK_SUCCESS);VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&transport.command;assert(transport.QueueSubmit(transport.queue,1,&s,transport.fence)==VK_SUCCESS);assert(transport.WaitForFences(device,1,&transport.fence,VK_TRUE,5000000000)==VK_SUCCESS);};
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  begin();transport.CmdPipelineBarrier(transport.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&b);submit();
  uint8_t pixels[32];for(unsigned i=0;i<8;++i){pixels[i*4]=i<4?10:90;pixels[i*4+1]=30;pixels[i*4+2]=50;pixels[i*4+3]=255;}
  assert(transport.Upload(image,pixels));assert(transport.WaitForFences(device,1,&transport.fence,VK_TRUE,5000000000)==VK_SUCCESS);assert(!transport.Busy());
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=32;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;VkBuffer read{};assert(transport.CreateBuffer(device,&bi,nullptr,&read)==VK_SUCCESS);transport.GetBufferMemoryRequirements(device,read,&req);memory_type=0;while(!(req.memoryTypeBits&(1u<<memory_type))||!(memory_props.memoryTypes[memory_type].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))++memory_type;
  ai.allocationSize=req.size;ai.memoryTypeIndex=memory_type;VkDeviceMemory read_memory{};assert(transport.AllocateMemory(device,&ai,nullptr,&read_memory)==VK_SUCCESS);assert(transport.BindBufferMemory(device,read,read_memory,0)==VK_SUCCESS);
  begin();b.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;transport.CmdPipelineBarrier(transport.command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={4,2,1};Proc<PFN_vkCmdCopyImageToBuffer>(get_device,device,"vkCmdCopyImageToBuffer")(transport.command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,read,1,&copy);
  VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=read;host.size=VK_WHOLE_SIZE;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;transport.CmdPipelineBarrier(transport.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);submit();
  void *mapped{};assert(transport.MapMemory(device,read_memory,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS);
  if(!(memory_props.memoryTypes[memory_type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};r.memory=read_memory;r.size=VK_WHOLE_SIZE;assert(Proc<PFN_vkInvalidateMappedMemoryRanges>(get_device,device,"vkInvalidateMappedMemoryRanges")(device,1,&r)==VK_SUCCESS);}
  auto actual=(uint8_t *)mapped;for(unsigned i=0;i<8;++i){assert(actual[i*4]==(format==VK_FORMAT_R8G8B8A8_UNORM?pixels[i*4+2]:pixels[i*4]));assert(actual[i*4+1]==30&&actual[i*4+3]==255);}
  transport.UnmapMemory(device,read_memory);transport.DestroyBuffer(device,read,nullptr);transport.FreeMemory(device,read_memory,nullptr);Proc<PFN_vkDestroyImage>(get_device,device,"vkDestroyImage")(device,image,nullptr);transport.FreeMemory(device,image_memory,nullptr);assert(transport.Dispose());
 }
 Proc<PFN_vkDestroyDevice>(get_device,device,"vkDestroyDevice")(device,nullptr);((PFN_vkDestroyInstance)get(instance,"vkDestroyInstance"))(instance,nullptr);
 puts("PASS: real Vulkan queue upload, image barriers, fence retirement, BGRA/RGBA channel conversion and exact pixel readback");
}
static void OpenGLTest(){
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DLSS5GLTransportTest";RegisterClassW(&wc);auto window=CreateWindowW(wc.lpszClassName,L"test",WS_POPUP,0,0,8,8,nullptr,nullptr,wc.hInstance,nullptr);assert(window);auto dc=GetDC(window);
 PIXELFORMATDESCRIPTOR pf{sizeof(pf),1};pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=32;int selected=ChoosePixelFormat(dc,&pf);assert(selected&&SetPixelFormat(dc,selected,&pf));auto context=wglCreateContext(dc);assert(context&&wglMakeCurrent(dc,context));
 GLuint image=0;glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,2,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glPixelStorei(GL_UNPACK_ALIGNMENT,1);assert(glGetError()==GL_NO_ERROR);
 vr_dashboard_transport::OpenGL transport;transport.binding.hDC=dc;transport.binding.hGLRC=context;assert(transport.Init(4,2));uint8_t pixels[32];for(unsigned i=0;i<8;++i){pixels[i*4]=i<4?10:90;pixels[i*4+1]=30;pixels[i*4+2]=50;pixels[i*4+3]=255;}
 assert(transport.Upload(image,pixels));assert(wglGetCurrentContext()==context&&wglGetCurrentDC()==dc);GLint alignment=0,binding=0;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);assert(alignment==1&&(GLuint)binding==image);
 uint8_t actual[32];glGetTexImage(GL_TEXTURE_2D,0,0x80E1,GL_UNSIGNED_BYTE,actual);assert(glGetError()==GL_NO_ERROR);for(unsigned i=0;i<8;++i)assert(actual[i*4]==(i<4?90:10)&&actual[i*4+1]==30&&actual[i*4+2]==50&&actual[i*4+3]==255);
 assert(transport.Dispose());glDeleteTextures(1,&image);wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);
 puts("PASS: real shared OpenGL upload, vertical orientation, exact colors and application context/unpack/texture preservation");
}
int main(){Gesture();VulkanTest();OpenGLTest();}

