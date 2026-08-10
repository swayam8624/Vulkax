#include "vulkax/render/headless.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VULKAX_PARTICLE_VERT_SPV_PATH
#error "VULKAX_PARTICLE_VERT_SPV_PATH missing"
#endif
#ifndef VULKAX_PARTICLE_FRAG_SPV_PATH
#error "VULKAX_PARTICLE_FRAG_SPV_PATH missing"
#endif

namespace vulkax::render {
namespace {

void check(VkResult result,const char* what){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(what)+" failed: "+std::to_string(static_cast<int>(result)));}

std::vector<std::uint32_t> readSpv(const char* path){
    std::ifstream stream(path,std::ios::binary|std::ios::ate);if(!stream)throw std::runtime_error(std::string("cannot open shader ")+path);
    const auto bytes=stream.tellg();if(bytes<=0||bytes%4!=0)throw std::runtime_error("invalid SPIR-V size");stream.seekg(0);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes)/4u);stream.read(reinterpret_cast<char*>(words.data()),bytes);if(!stream)throw std::runtime_error("SPIR-V read failed");return words;
}

std::uint32_t memoryType(const VkPhysicalDeviceMemoryProperties& props,std::uint32_t bits,VkMemoryPropertyFlags required,VkMemoryPropertyFlags preferred=0){
    std::uint32_t fallback=UINT32_MAX;
    for(std::uint32_t i=0;i<props.memoryTypeCount;++i){if((bits&(1u<<i))==0)continue;const auto flags=props.memoryTypes[i].propertyFlags;if((flags&required)!=required)continue;if((flags&preferred)==preferred)return i;if(fallback==UINT32_MAX)fallback=i;}
    if(fallback==UINT32_MAX)throw std::runtime_error("no compatible Vulkan memory type");return fallback;
}

struct Vertex{float x,y,z,r,g,b,a,size;};

struct VulkanContext{
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};std::uint32_t family{};VkPhysicalDeviceMemoryProperties memory{};
    ~VulkanContext(){if(device)vkDestroyDevice(device,nullptr);if(instance)vkDestroyInstance(instance,nullptr);}
};

VulkanContext makeContext(){
    VulkanContext c;VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Vulkax Scientific Renderer";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;check(vkCreateInstance(&ci,nullptr,&c.instance),"vkCreateInstance");
    std::uint32_t count=0;check(vkEnumeratePhysicalDevices(c.instance,&count,nullptr),"vkEnumeratePhysicalDevices");if(!count)throw std::runtime_error("no Vulkan device");std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(c.instance,&count,devices.data()),"vkEnumeratePhysicalDevices");
    for(auto p:devices){std::uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(p,&n,nullptr);std::vector<VkQueueFamilyProperties> q(n);vkGetPhysicalDeviceQueueFamilyProperties(p,&n,q.data());for(std::uint32_t i=0;i<n;++i)if((q[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)!=0){c.physical=p;c.family=i;break;}if(c.physical)break;}
    if(!c.physical)throw std::runtime_error("no Vulkan graphics queue");float priority=1.0f;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=c.family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;check(vkCreateDevice(c.physical,&di,nullptr,&c.device),"vkCreateDevice");vkGetDeviceQueue(c.device,c.family,0,&c.queue);vkGetPhysicalDeviceMemoryProperties(c.physical,&c.memory);return c;
}

VkShaderModule shader(VkDevice device,const char* path){const auto words=readSpv(path);VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=words.size()*4u;ci.pCode=words.data();VkShaderModule m{};check(vkCreateShaderModule(device,&ci,nullptr,&m),"vkCreateShaderModule");return m;}

} // namespace

ImageRGBA8 renderParticlesVulkan(const std::vector<visualization::ParticleInstance>& particles,const RenderSettings& settings){
    auto c=makeContext();const VkDevice device=c.device;const VkDeviceSize pixelBytes=static_cast<VkDeviceSize>(settings.width)*settings.height*4u;
    VkImage image{};VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=VK_IMAGE_TYPE_2D;ii.format=VK_FORMAT_R8G8B8A8_UNORM;ii.extent={settings.width,settings.height,1};ii.mipLevels=1;ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;check(vkCreateImage(device,&ii,nullptr,&image),"vkCreateImage");
    VkMemoryRequirements imr{};vkGetImageMemoryRequirements(device,image,&imr);VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mai.allocationSize=imr.size;mai.memoryTypeIndex=memoryType(c.memory,imr.memoryTypeBits,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);VkDeviceMemory imageMemory{};check(vkAllocateMemory(device,&mai,nullptr,&imageMemory),"vkAllocateMemory(image)");check(vkBindImageMemory(device,image,imageMemory,0),"vkBindImageMemory");
    VkImageView view{};VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ii.format;vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;vi.subresourceRange.levelCount=1;vi.subresourceRange.layerCount=1;check(vkCreateImageView(device,&vi,nullptr,&view),"vkCreateImageView");
    VkAttachmentDescription attachment{};attachment.format=ii.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rpi.attachmentCount=1;rpi.pAttachments=&attachment;rpi.subpassCount=1;rpi.pSubpasses=&sub;VkRenderPass pass{};check(vkCreateRenderPass(device,&rpi,nullptr,&pass),"vkCreateRenderPass");
    VkFramebuffer fb{};VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fbi.renderPass=pass;fbi.attachmentCount=1;fbi.pAttachments=&view;fbi.width=settings.width;fbi.height=settings.height;fbi.layers=1;check(vkCreateFramebuffer(device,&fbi,nullptr,&fb),"vkCreateFramebuffer");
    const auto vs=shader(device,VULKAX_PARTICLE_VERT_SPV_PATH),fs=shader(device,VULKAX_PARTICLE_FRAG_SPV_PATH);VkPipelineShaderStageCreateInfo stages[2]{};stages[0]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_VERTEX_BIT,vs,"main",nullptr};stages[1]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_FRAGMENT_BIT,fs,"main",nullptr};
    VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription attrs[3]{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32A32_SFLOAT,12},{2,0,VK_FORMAT_R32_SFLOAT,28}};VkPipelineVertexInputStateCreateInfo vertexInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vertexInfo.vertexBindingDescriptionCount=1;vertexInfo.pVertexBindingDescriptions=&binding;vertexInfo.vertexAttributeDescriptionCount=3;vertexInfo.pVertexAttributeDescriptions=attrs;VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    VkViewport viewport{0,0,static_cast<float>(settings.width),static_cast<float>(settings.height),0,1};VkRect2D scissor{{0,0},{settings.width,settings.height}};VkPipelineViewportStateCreateInfo viewportInfo{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewportInfo.viewportCount=1;viewportInfo.pViewports=&viewport;viewportInfo.scissorCount=1;viewportInfo.pScissors=&scissor;VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1.0f;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;VkPipelineColorBlendAttachmentState blendAttachment{};blendAttachment.colorWriteMask=0xf;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&blendAttachment;VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(float)};VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pli.pushConstantRangeCount=1;pli.pPushConstantRanges=&range;VkPipelineLayout layout{};check(vkCreatePipelineLayout(device,&pli,nullptr,&layout),"vkCreatePipelineLayout");VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gpi.stageCount=2;gpi.pStages=stages;gpi.pVertexInputState=&vertexInfo;gpi.pInputAssemblyState=&assembly;gpi.pViewportState=&viewportInfo;gpi.pRasterizationState=&raster;gpi.pMultisampleState=&ms;gpi.pColorBlendState=&blend;gpi.layout=layout;gpi.renderPass=pass;VkPipeline pipeline{};check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gpi,nullptr,&pipeline),"vkCreateGraphicsPipelines");
    std::vector<visualization::ParticleInstance> sorted=particles;std::stable_sort(sorted.begin(),sorted.end(),[](const auto&a,const auto&b){return a.position.z<b.position.z;});std::vector<Vertex> vertices;vertices.reserve(sorted.size());const double minDimension=static_cast<double>(std::min(settings.width,settings.height));for(const auto&p:sorted){const float point=static_cast<float>(std::max(1.0,2.0*p.radius/settings.worldScale*minDimension));vertices.push_back({static_cast<float>(p.position.x),static_cast<float>(p.position.y),static_cast<float>(p.position.z),p.color.r,p.color.g,p.color.b,p.color.a,point});}
    VkBuffer vb{},readback{};VkDeviceMemory vmem{},rmem{};auto makeBuffer=[&](VkDeviceSize size,VkBufferUsageFlags usage,VkBuffer&buffer,VkDeviceMemory&memory){VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=std::max<VkDeviceSize>(size,4);bi.usage=usage;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(vkCreateBuffer(device,&bi,nullptr,&buffer),"vkCreateBuffer");VkMemoryRequirements mr{};vkGetBufferMemoryRequirements(device,buffer,&mr);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=mr.size;ai.memoryTypeIndex=memoryType(c.memory,mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(vkAllocateMemory(device,&ai,nullptr,&memory),"vkAllocateMemory(buffer)");check(vkBindBufferMemory(device,buffer,memory,0),"vkBindBufferMemory");};makeBuffer(vertices.size()*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vb,vmem);makeBuffer(pixelBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,readback,rmem);if(!vertices.empty()){void*mapped{};check(vkMapMemory(device,vmem,0,VK_WHOLE_SIZE,0,&mapped),"vkMapMemory(vertices)");std::copy(reinterpret_cast<const std::uint8_t*>(vertices.data()),reinterpret_cast<const std::uint8_t*>(vertices.data()+vertices.size()),static_cast<std::uint8_t*>(mapped));vkUnmapMemory(device,vmem);}
    VkCommandPool pool{};VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.queueFamilyIndex=c.family;check(vkCreateCommandPool(device,&pci,nullptr,&pool),"vkCreateCommandPool");VkCommandBuffer cmd{};VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;check(vkAllocateCommandBuffers(device,&cai,&cmd),"vkAllocateCommandBuffers");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(cmd,&begin),"vkBeginCommandBuffer");VkClearValue clear{{settings.clearColor.r,settings.clearColor.g,settings.clearColor.b,settings.clearColor.a}};VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rpbi.renderPass=pass;rpbi.framebuffer=fb;rpbi.renderArea={{0,0},{settings.width,settings.height}};rpbi.clearValueCount=1;rpbi.pClearValues=&clear;vkCmdBeginRenderPass(cmd,&rpbi,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);if(!vertices.empty()){VkDeviceSize offset=0;vkCmdBindVertexBuffers(cmd,0,1,&vb,&offset);const float scale=static_cast<float>(settings.worldScale);vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(float),&scale);vkCmdDraw(cmd,static_cast<std::uint32_t>(vertices.size()),1,0,0);}vkCmdEndRenderPass(cmd);VkBufferImageCopy copy{};copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;copy.imageExtent={settings.width,settings.height,1};vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);check(vkEndCommandBuffer(cmd),"vkEndCommandBuffer");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;VkFence fence{};VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device,&fci,nullptr,&fence),"vkCreateFence");check(vkQueueSubmit(c.queue,1,&submit,fence),"vkQueueSubmit");check(vkWaitForFences(device,1,&fence,VK_TRUE,10'000'000'000ULL),"vkWaitForFences");ImageRGBA8 output{settings.width,settings.height,std::vector<std::uint8_t>(static_cast<std::size_t>(pixelBytes))};void*pixels{};check(vkMapMemory(device,rmem,0,pixelBytes,0,&pixels),"vkMapMemory(readback)");std::copy(static_cast<std::uint8_t*>(pixels),static_cast<std::uint8_t*>(pixels)+output.pixels.size(),output.pixels.begin());vkUnmapMemory(device,rmem);
    vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyBuffer(device,readback,nullptr);vkFreeMemory(device,rmem,nullptr);vkDestroyBuffer(device,vb,nullptr);vkFreeMemory(device,vmem,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyShaderModule(device,fs,nullptr);vkDestroyShaderModule(device,vs,nullptr);vkDestroyFramebuffer(device,fb,nullptr);vkDestroyRenderPass(device,pass,nullptr);vkDestroyImageView(device,view,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,imageMemory,nullptr);return output;
}

} // namespace vulkax::render
