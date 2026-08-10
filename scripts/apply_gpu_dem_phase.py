from pathlib import Path
import runpy

# Self-heal prerequisites if the prior one-shot phase has not committed yet.
if not Path('src/execution/aerodynamics_vertical.cpp').exists():
    prior = Path('scripts/apply_verticals_phase.py')
    if prior.exists(): runpy.run_path(str(prior), run_name='__main__')
if not Path('include/vulkax/execution/problem_runner.hpp').exists():
    prior = Path('scripts/apply_problem_runner_migration.py')
    if prior.exists(): runpy.run_path(str(prior), run_name='__main__')
if not Path('include/vulkax/execution/problem_runner.hpp').exists():
    raise RuntimeError('GPU DEM phase requires the problem-runner baseline')
for stale in ['scripts/apply_verticals_phase.py','.github/workflows/wip-verticals-phase.yml','scripts/apply_problem_runner_migration.py','.github/workflows/wip-problem-runner-migration.yml']:
    p=Path(stale)
    if p.exists(): p.unlink()

def replace(path, old, new):
    p=Path(path); text=p.read_text()
    if new in text: return
    if old not in text: raise RuntimeError(f'anchor not found in {path}: {old[:90]!r}')
    p.write_text(text.replace(old,new,1))

Path('include/vulkax/compute/gpu_dem.hpp').write_text(r'''#pragma once
#include "vulkax/backend/backend.hpp"
#include "vulkax/solvers/dem.hpp"
#include <cstddef>
#include <vector>
namespace vulkax::compute {
struct GpuDemResult { backend::BackendKind backend{backend::BackendKind::Vulkan}; std::string deviceName; std::vector<solvers::DemParticle> particles; };
[[nodiscard]] std::vector<backend::BackendKind> availableGpuDemBackends();
[[nodiscard]] GpuDemResult advanceDemGpu(backend::BackendKind,const std::vector<solvers::DemParticle>&,const solvers::DemBox&,const solvers::DemConfig&,std::size_t steps=1);
}
''')
Path('src/compute/gpu_dem.cpp').write_text(r'''#include "vulkax/compute/gpu_dem.hpp"
#include <stdexcept>
#ifndef VULKAX_HAS_VULKAN_GPU_DEM
#define VULKAX_HAS_VULKAN_GPU_DEM 0
#endif
#ifndef VULKAX_HAS_METAL_GPU_DEM
#define VULKAX_HAS_METAL_GPU_DEM 0
#endif
namespace vulkax::compute {
#if VULKAX_HAS_VULKAN_GPU_DEM
GpuDemResult advanceDemVulkan(const std::vector<solvers::DemParticle>&,const solvers::DemBox&,const solvers::DemConfig&,std::size_t);
#endif
#if VULKAX_HAS_METAL_GPU_DEM
GpuDemResult advanceDemMetal(const std::vector<solvers::DemParticle>&,const solvers::DemBox&,const solvers::DemConfig&,std::size_t);
#endif
std::vector<backend::BackendKind> availableGpuDemBackends(){std::vector<backend::BackendKind> r;
#if VULKAX_HAS_VULKAN_GPU_DEM
r.push_back(backend::BackendKind::Vulkan);
#endif
#if VULKAX_HAS_METAL_GPU_DEM
r.push_back(backend::BackendKind::Metal);
#endif
return r;}
GpuDemResult advanceDemGpu(backend::BackendKind b,const std::vector<solvers::DemParticle>&p,const solvers::DemBox&box,const solvers::DemConfig&c,std::size_t steps){if(p.empty()||steps==0)throw std::invalid_argument("GPU DEM requires particles and positive steps");switch(b){case backend::BackendKind::Vulkan:
#if VULKAX_HAS_VULKAN_GPU_DEM
return advanceDemVulkan(p,box,c,steps);
#else
throw std::runtime_error("Vulkan GPU DEM not built");
#endif
case backend::BackendKind::Metal:
#if VULKAX_HAS_METAL_GPU_DEM
return advanceDemMetal(p,box,c,steps);
#else
throw std::runtime_error("Metal GPU DEM not built");
#endif
case backend::BackendKind::OpenGL: throw std::runtime_error("OpenGL GPU DEM not built");}throw std::logic_error("unknown backend");}
}
''')
Path('shaders/dem_step.comp').write_text(r'''#version 450
layout(local_size_x=64) in;
struct Particle { vec4 pr; vec4 vm; };
layout(std430,binding=0) readonly buffer InputParticles { Particle inputParticles[]; };
layout(std430,binding=1) writeonly buffer OutputParticles { Particle outputParticles[]; };
layout(push_constant) uniform Params { uint count; float dt; float stiffness; float damping; float friction; float restitution; vec3 gravity; vec3 boxMin; vec3 boxMax; } params;
void main(){uint i=gl_GlobalInvocationID.x;if(i>=params.count)return;Particle a=inputParticles[i];vec3 pos=a.pr.xyz;float radius=a.pr.w;vec3 vel=a.vm.xyz;float mass=a.vm.w;vec3 force=params.gravity*mass;for(uint j=0;j<params.count;++j){if(j==i)continue;Particle b=inputParticles[j];vec3 delta=b.pr.xyz-pos;float distance=length(delta);float overlap=radius+b.pr.w-distance;if(overlap<=0.0)continue;vec3 normal=distance>1e-7?delta/distance:vec3(1,0,0);vec3 relative=b.vm.xyz-vel;float normalVelocity=dot(relative,normal);float normalForce=max(0.0,params.stiffness*overlap+params.damping*normalVelocity);vec3 tangent=relative-normal*normalVelocity;float ts=length(tangent);vec3 frictionForce=ts>1e-7?(tangent/ts)*(params.friction*normalForce):vec3(0);force+=-normal*normalForce+frictionForce;}vel+=force/mass*params.dt;pos+=vel*params.dt;for(int axis=0;axis<3;++axis){if(pos[axis]-radius<params.boxMin[axis]){pos[axis]=params.boxMin[axis]+radius;if(vel[axis]<0.0)vel[axis]=-vel[axis]*params.restitution;}if(pos[axis]+radius>params.boxMax[axis]){pos[axis]=params.boxMax[axis]-radius;if(vel[axis]>0.0)vel[axis]=-vel[axis]*params.restitution;}}outputParticles[i].pr=vec4(pos,radius);outputParticles[i].vm=vec4(vel,mass);}
''')

Path('src/compute/vulkan_dem.cpp').write_text(r'''#include "vulkax/compute/gpu_dem.hpp"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef VULKAX_DEM_SPV_PATH
#error VULKAX_DEM_SPV_PATH missing
#endif
namespace vulkax::compute { namespace {
void check(VkResult r,const char*w){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(w)+" failed: "+std::to_string((int)r));}
struct GpuParticle{float pr[4];float vm[4];};
struct Params{std::uint32_t count;float dt,stiffness,damping,friction,restitution;float gravity[3];float boxMin[3];float boxMax[3];};
std::vector<std::uint32_t>spv(){std::ifstream f(VULKAX_DEM_SPV_PATH,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("DEM SPIR-V missing");auto n=f.tellg();f.seekg(0);std::vector<std::uint32_t>w((std::size_t)n/4);f.read((char*)w.data(),n);return w;}
std::uint32_t memType(const VkPhysicalDeviceMemoryProperties&m,std::uint32_t bits){for(std::uint32_t i=0;i<m.memoryTypeCount;++i)if((bits&(1u<<i))&&(m.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)&&(m.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))return i;throw std::runtime_error("no host coherent Vulkan storage memory");}
}
GpuDemResult advanceDemVulkan(const std::vector<solvers::DemParticle>&particles,const solvers::DemBox&box,const solvers::DemConfig&cfg,std::size_t steps){VkInstance instance{};VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};ai.pApplicationName="Vulkax GPU DEM";ai.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&ai;check(vkCreateInstance(&ici,nullptr,&instance),"vkCreateInstance");try{std::uint32_t count=0;check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"vkEnumeratePhysicalDevices");if(!count)throw std::runtime_error("no Vulkan device");std::vector<VkPhysicalDevice>devs(count);check(vkEnumeratePhysicalDevices(instance,&count,devs.data()),"vkEnumeratePhysicalDevices");VkPhysicalDevice physical{};std::uint32_t family=0;for(auto d:devs){std::uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(d,&n,nullptr);std::vector<VkQueueFamilyProperties>q(n);vkGetPhysicalDeviceQueueFamilyProperties(d,&n,q.data());for(std::uint32_t i=0;i<n;++i)if(q[i].queueFlags&VK_QUEUE_COMPUTE_BIT){physical=d;family=i;break;}if(physical)break;}if(!physical)throw std::runtime_error("no Vulkan compute queue");float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;VkDevice device{};check(vkCreateDevice(physical,&dci,nullptr,&device),"vkCreateDevice");try{VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);VkPhysicalDeviceMemoryProperties mp{};vkGetPhysicalDeviceMemoryProperties(physical,&mp);const VkDeviceSize bytes=particles.size()*sizeof(GpuParticle);VkBuffer buffers[2]{};VkDeviceMemory memory[2]{};for(int k=0;k<2;++k){VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;check(vkCreateBuffer(device,&bi,nullptr,&buffers[k]),"vkCreateBuffer");VkMemoryRequirements mr{};vkGetBufferMemoryRequirements(device,buffers[k],&mr);VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mi.allocationSize=mr.size;mi.memoryTypeIndex=memType(mp,mr.memoryTypeBits);check(vkAllocateMemory(device,&mi,nullptr,&memory[k]),"vkAllocateMemory");check(vkBindBufferMemory(device,buffers[k],memory[k],0),"vkBindBufferMemory");}std::vector<GpuParticle>host(particles.size());for(std::size_t i=0;i<particles.size();++i){host[i]={{(float)particles[i].position.x,(float)particles[i].position.y,(float)particles[i].position.z,(float)particles[i].radius},{(float)particles[i].velocity.x,(float)particles[i].velocity.y,(float)particles[i].velocity.z,(float)particles[i].mass}};}void*mapped{};check(vkMapMemory(device,memory[0],0,bytes,0,&mapped),"vkMapMemory");std::memcpy(mapped,host.data(),(std::size_t)bytes);vkUnmapMemory(device,memory[0]);auto code=spv();VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smi.codeSize=code.size()*4;smi.pCode=code.data();VkShaderModule shader{};check(vkCreateShaderModule(device,&smi,nullptr,&shader),"vkCreateShaderModule");VkDescriptorSetLayoutBinding bindings[2]{};for(int i=0;i<2;++i){bindings[i].binding=(std::uint32_t)i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lci.bindingCount=2;lci.pBindings=bindings;VkDescriptorSetLayout dsl{};check(vkCreateDescriptorSetLayout(device,&lci,nullptr,&dsl),"vkCreateDescriptorSetLayout");VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Params)};VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};plci.setLayoutCount=1;plci.pSetLayouts=&dsl;plci.pushConstantRangeCount=1;plci.pPushConstantRanges=&range;VkPipelineLayout layout{};check(vkCreatePipelineLayout(device,&plci,nullptr,&layout),"vkCreatePipelineLayout");VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;stage.module=shader;stage.pName="main";VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pci.stage=stage;pci.layout=layout;VkPipeline pipeline{};check(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,nullptr,&pipeline),"vkCreateComputePipelines");VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpci.maxSets=1;dpci.poolSizeCount=1;dpci.pPoolSizes=&ps;VkDescriptorPool pool{};check(vkCreateDescriptorPool(device,&dpci,nullptr,&pool),"vkCreateDescriptorPool");VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};dsai.descriptorPool=pool;dsai.descriptorSetCount=1;dsai.pSetLayouts=&dsl;VkDescriptorSet ds{};check(vkAllocateDescriptorSets(device,&dsai,&ds),"vkAllocateDescriptorSets");VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpci.queueFamilyIndex=family;VkCommandPool cp{};check(vkCreateCommandPool(device,&cpci,nullptr,&cp),"vkCreateCommandPool");Params params{};params.count=(std::uint32_t)particles.size();params.dt=(float)cfg.dt;params.stiffness=(float)cfg.normalStiffness;params.damping=(float)cfg.normalDamping;params.friction=(float)cfg.friction;params.restitution=(float)cfg.wallRestitution;params.gravity[0]=(float)cfg.gravity.x;params.gravity[1]=(float)cfg.gravity.y;params.gravity[2]=(float)cfg.gravity.z;params.boxMin[0]=(float)box.minimum.x;params.boxMin[1]=(float)box.minimum.y;params.boxMin[2]=(float)box.minimum.z;params.boxMax[0]=(float)box.maximum.x;params.boxMax[1]=(float)box.maximum.y;params.boxMax[2]=(float)box.maximum.z;int src=0,dst=1;for(std::size_t s=0;s<steps;++s){VkDescriptorBufferInfo infos[2]{{buffers[src],0,bytes},{buffers[dst],0,bytes}};VkWriteDescriptorSet writes[2]{};for(int i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=ds;writes[i].dstBinding=(std::uint32_t)i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&infos[i];}vkUpdateDescriptorSets(device,2,writes,0,nullptr);VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cbai.commandPool=cp;cbai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cbai.commandBufferCount=1;VkCommandBuffer cmd{};check(vkAllocateCommandBuffers(device,&cbai,&cmd),"vkAllocateCommandBuffers");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(cmd,&begin),"vkBeginCommandBuffer");vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&ds,0,nullptr);vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),&params);vkCmdDispatch(cmd,(params.count+63)/64,1,1);check(vkEndCommandBuffer(cmd),"vkEndCommandBuffer");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};check(vkCreateFence(device,&fci,nullptr,&fence),"vkCreateFence");check(vkQueueSubmit(queue,1,&submit,fence),"vkQueueSubmit");check(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL),"vkWaitForFences");vkDestroyFence(device,fence,nullptr);vkFreeCommandBuffers(device,cp,1,&cmd);std::swap(src,dst);}check(vkMapMemory(device,memory[src],0,bytes,0,&mapped),"vkMapMemory(final)");std::memcpy(host.data(),mapped,(std::size_t)bytes);vkUnmapMemory(device,memory[src]);GpuDemResult result;result.backend=backend::BackendKind::Vulkan;VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);result.deviceName=props.deviceName;result.particles.resize(particles.size());for(std::size_t i=0;i<host.size();++i)result.particles[i]={{host[i].pr[0],host[i].pr[1],host[i].pr[2]},{host[i].vm[0],host[i].vm[1],host[i].vm[2]},host[i].pr[3],host[i].vm[3]};vkDestroyCommandPool(device,cp,nullptr);vkDestroyDescriptorPool(device,pool,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyDescriptorSetLayout(device,dsl,nullptr);vkDestroyShaderModule(device,shader,nullptr);for(int k=0;k<2;++k){vkDestroyBuffer(device,buffers[k],nullptr);vkFreeMemory(device,memory[k],nullptr);}vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);return result;}catch(...){vkDestroyDevice(device,nullptr);throw;}}catch(...){vkDestroyInstance(instance,nullptr);throw;}}
} // namespace vulkax::compute
''')

Path('src/compute/metal_dem.mm').write_text(r'''#include "vulkax/compute/gpu_dem.hpp"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdexcept>
#include <vector>
namespace vulkax::compute { namespace { struct GP{float pr[4];float vm[4];}; struct Params{uint32_t count;float dt,stiffness,damping,friction,restitution;float gravity[3];float boxMin[3];float boxMax[3];}; }
GpuDemResult advanceDemMetal(const std::vector<solvers::DemParticle>&particles,const solvers::DemBox&box,const solvers::DemConfig&cfg,std::size_t steps){@autoreleasepool{id<MTLDevice>device=MTLCreateSystemDefaultDevice();if(!device)throw std::runtime_error("no Metal device");NSString*source=@R"metal(#include <metal_stdlib>
using namespace metal; struct P{float4 pr;float4 vm;};struct Params{uint count;float dt,stiffness,damping,friction,restitution;float3 gravity;float3 boxMin;float3 boxMax;};kernel void dem(device const P*inP[[buffer(0)]],device P*outP[[buffer(1)]],constant Params&p[[buffer(2)]],uint i[[thread_position_in_grid]]){if(i>=p.count)return;P a=inP[i];float3 pos=a.pr.xyz;float radius=a.pr.w;float3 vel=a.vm.xyz;float mass=a.vm.w;float3 force=p.gravity*mass;for(uint j=0;j<p.count;++j){if(j==i)continue;P b=inP[j];float3 delta=b.pr.xyz-pos;float d=length(delta);float overlap=radius+b.pr.w-d;if(overlap<=0)continue;float3 n=d>1e-7?delta/d:float3(1,0,0);float3 rel=b.vm.xyz-vel;float nv=dot(rel,n);float nf=max(0.0f,p.stiffness*overlap+p.damping*nv);float3 t=rel-n*nv;float ts=length(t);float3 ff=ts>1e-7?(t/ts)*(p.friction*nf):float3(0);force+=-n*nf+ff;}vel+=force/mass*p.dt;pos+=vel*p.dt;for(int a=0;a<3;++a){if(pos[a]-radius<p.boxMin[a]){pos[a]=p.boxMin[a]+radius;if(vel[a]<0)vel[a]=-vel[a]*p.restitution;}if(pos[a]+radius>p.boxMax[a]){pos[a]=p.boxMax[a]-radius;if(vel[a]>0)vel[a]=-vel[a]*p.restitution;}}outP[i].pr=float4(pos,radius);outP[i].vm=float4(vel,mass);})metal";NSError*error=nil;id<MTLLibrary>lib=[device newLibraryWithSource:source options:nil error:&error];if(!lib)throw std::runtime_error([[error localizedDescription] UTF8String]);id<MTLFunction>fn=[lib newFunctionWithName:@"dem"];id<MTLComputePipelineState>pipe=[device newComputePipelineStateWithFunction:fn error:&error];if(!pipe)throw std::runtime_error([[error localizedDescription] UTF8String]);std::vector<GP>host(particles.size());for(std::size_t i=0;i<particles.size();++i)host[i]={{(float)particles[i].position.x,(float)particles[i].position.y,(float)particles[i].position.z,(float)particles[i].radius},{(float)particles[i].velocity.x,(float)particles[i].velocity.y,(float)particles[i].velocity.z,(float)particles[i].mass}};NSUInteger bytes=host.size()*sizeof(GP);id<MTLBuffer>a=[device newBufferWithBytes:host.data() length:bytes options:MTLResourceStorageModeShared];id<MTLBuffer>b=[device newBufferWithLength:bytes options:MTLResourceStorageModeShared];id<MTLCommandQueue>q=[device newCommandQueue];Params p{};p.count=(uint32_t)particles.size();p.dt=cfg.dt;p.stiffness=cfg.normalStiffness;p.damping=cfg.normalDamping;p.friction=cfg.friction;p.restitution=cfg.wallRestitution;p.gravity[0]=cfg.gravity.x;p.gravity[1]=cfg.gravity.y;p.gravity[2]=cfg.gravity.z;p.boxMin[0]=box.minimum.x;p.boxMin[1]=box.minimum.y;p.boxMin[2]=box.minimum.z;p.boxMax[0]=box.maximum.x;p.boxMax[1]=box.maximum.y;p.boxMax[2]=box.maximum.z;for(std::size_t s=0;s<steps;++s){id<MTLCommandBuffer>cb=[q commandBuffer];id<MTLComputeCommandEncoder>enc=[cb computeCommandEncoder];[enc setComputePipelineState:pipe];[enc setBuffer:a offset:0 atIndex:0];[enc setBuffer:b offset:0 atIndex:1];[enc setBytes:&p length:sizeof(p) atIndex:2];NSUInteger w=pipe.maxTotalThreadsPerThreadgroup<64?pipe.maxTotalThreadsPerThreadgroup:64;[enc dispatchThreads:MTLSizeMake(p.count,1,1) threadsPerThreadgroup:MTLSizeMake(w,1,1)];[enc endEncoding];[cb commit];[cb waitUntilCompleted];if(cb.status==MTLCommandBufferStatusError)throw std::runtime_error([[cb.error localizedDescription] UTF8String]);id<MTLBuffer>tmp=a;a=b;b=tmp;}std::memcpy(host.data(),[a contents],bytes);GpuDemResult result;result.backend=backend::BackendKind::Metal;result.deviceName=[[device name] UTF8String];result.particles.resize(host.size());for(std::size_t i=0;i<host.size();++i)result.particles[i]={{host[i].pr[0],host[i].pr[1],host[i].pr[2]},{host[i].vm[0],host[i].vm[1],host[i].vm[2]},host[i].pr[3],host[i].vm[3]};return result;}}
} // namespace vulkax::compute
''')

Path('tests/gpu_dem_tests.cpp').write_text(r'''#include "vulkax/compute/gpu_dem.hpp"
#include "vulkax/solvers/dem.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
int main(){using namespace vulkax;std::vector<solvers::DemParticle>initial={{{-0.08,0.4,0},{0.1,0,0},0.1,1},{{0.08,0.4,0},{-0.1,0,0},0.1,1},{{0.5,0.7,0},{0,-0.1,0},0.08,0.7}};const solvers::DemBox box{{-1,0,-1},{1,2,1}};const solvers::DemConfig cfg{1e-4,{0,-9.80665,0},12000,8,0.25,0.4};auto cpu=initial;solvers::advanceDem(cpu,box,cfg,1);for(auto backend:compute::availableGpuDemBackends()){const auto gpu=compute::advanceDemGpu(backend,initial,box,cfg,1);assert(gpu.particles.size()==cpu.size());for(std::size_t i=0;i<cpu.size();++i){const double pos=math::length(gpu.particles[i].position-cpu[i].position);const double vel=math::length(gpu.particles[i].velocity-cpu[i].velocity);assert(pos<2e-5);assert(vel<2e-3);}}return 0;}
''')

# CMake integration. The prior vertical phase may have bumped version to 0.19.
p=Path('CMakeLists.txt'); text=p.read_text();
text=text.replace('project(Vulkax VERSION 0.19.0 LANGUAGES CXX)','project(Vulkax VERSION 0.20.0 LANGUAGES CXX)').replace('project(Vulkax VERSION 0.18.0 LANGUAGES CXX)','project(Vulkax VERSION 0.20.0 LANGUAGES CXX)')
if 'src/compute/gpu_dem.cpp' not in text:text=text.replace('src/compiler/expression.cpp src/compute/conformance.cpp','src/compiler/expression.cpp src/compute/conformance.cpp src/compute/gpu_dem.cpp')
if 'VULKAX_DEM_SPV' not in text:
    text=text.replace('set(VULKAX_PARTICLE_FRAG_SPV "${VULKAX_SHADER_DIR}/particle.frag.spv")','set(VULKAX_PARTICLE_FRAG_SPV "${VULKAX_SHADER_DIR}/particle.frag.spv")\n        set(VULKAX_DEM_SPV "${VULKAX_SHADER_DIR}/dem_step.spv")')
    text=text.replace('"${VULKAX_PARTICLE_FRAG_SPV}"\n            COMMAND ${CMAKE_COMMAND}', '"${VULKAX_PARTICLE_FRAG_SPV}" "${VULKAX_DEM_SPV}"\n            COMMAND ${CMAKE_COMMAND}')
    text=text.replace('COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.frag" -o "${VULKAX_PARTICLE_FRAG_SPV}"', 'COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.frag" -o "${VULKAX_PARTICLE_FRAG_SPV}"\n            COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/dem_step.comp" -o "${VULKAX_DEM_SPV}"')
    text=text.replace('"${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.frag" VERBATIM)', '"${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.frag" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/dem_step.comp" VERBATIM)')
    text=text.replace('add_custom_target(vulkax_gpu_shaders DEPENDS "${VULKAX_CONFORMANCE_SPV}" "${VULKAX_PARTICLE_VERT_SPV}" "${VULKAX_PARTICLE_FRAG_SPV}")','add_custom_target(vulkax_gpu_shaders DEPENDS "${VULKAX_CONFORMANCE_SPV}" "${VULKAX_PARTICLE_VERT_SPV}" "${VULKAX_PARTICLE_FRAG_SPV}" "${VULKAX_DEM_SPV}")')
    text=text.replace('target_sources(vulkax_core PRIVATE src/compute/vulkan_conformance.cpp src/render/vulkan_headless.cpp)','target_sources(vulkax_core PRIVATE src/compute/vulkan_conformance.cpp src/compute/vulkan_dem.cpp src/render/vulkan_headless.cpp)')
    text=text.replace('VULKAX_HAS_VULKAN_COMPUTE=1 VULKAX_HAS_VULKAN_RENDER=1', 'VULKAX_HAS_VULKAN_COMPUTE=1 VULKAX_HAS_VULKAN_RENDER=1 VULKAX_HAS_VULKAN_GPU_DEM=1')
    text=text.replace('VULKAX_PARTICLE_FRAG_SPV_PATH="${VULKAX_PARTICLE_FRAG_SPV}")','VULKAX_PARTICLE_FRAG_SPV_PATH="${VULKAX_PARTICLE_FRAG_SPV}" VULKAX_DEM_SPV_PATH="${VULKAX_DEM_SPV}")')
    text=text.replace('VULKAX_HAS_VULKAN_COMPUTE=0 VULKAX_HAS_VULKAN_RENDER=0)', 'VULKAX_HAS_VULKAN_COMPUTE=0 VULKAX_HAS_VULKAN_RENDER=0 VULKAX_HAS_VULKAN_GPU_DEM=0)')
    text=text.replace('VULKAX_HAS_VULKAN=0 VULKAX_HAS_VULKAN_COMPUTE=0 VULKAX_HAS_VULKAN_RENDER=0)', 'VULKAX_HAS_VULKAN=0 VULKAX_HAS_VULKAN_COMPUTE=0 VULKAX_HAS_VULKAN_RENDER=0 VULKAX_HAS_VULKAN_GPU_DEM=0)')
    text=text.replace('src/backend/metal_probe.mm src/compute/metal_conformance.mm src/render/metal_headless.mm','src/backend/metal_probe.mm src/compute/metal_conformance.mm src/compute/metal_dem.mm src/render/metal_headless.mm')
    text=text.replace('VULKAX_HAS_METAL=1 VULKAX_HAS_METAL_COMPUTE=1 VULKAX_HAS_METAL_RENDER=1)', 'VULKAX_HAS_METAL=1 VULKAX_HAS_METAL_COMPUTE=1 VULKAX_HAS_METAL_RENDER=1 VULKAX_HAS_METAL_GPU_DEM=1)')
    text=text.replace('VULKAX_HAS_METAL=0 VULKAX_HAS_METAL_COMPUTE=0 VULKAX_HAS_METAL_RENDER=0)', 'VULKAX_HAS_METAL=0 VULKAX_HAS_METAL_COMPUTE=0 VULKAX_HAS_METAL_RENDER=0 VULKAX_HAS_METAL_GPU_DEM=0)')
if 'gpu_dem)' not in text:text=text.replace('problem_runner)','problem_runner gpu_dem)')
p.write_text(text)

# Add explicit native GPU DEM gates to Linux/macOS CI.
ci=Path('.github/workflows/ci.yml').read_text()
if 'Native GPU DEM reference' not in ci:
    ci=ci.replace('      - name: Vulkan compute conformance\n        run: ./build/vulkax --conformance Vulkan', '      - name: Vulkan compute conformance\n        run: ./build/vulkax --conformance Vulkan\n      - name: Native GPU DEM reference\n        run: ctest --test-dir build -R vulkax_gpu_dem --output-on-failure')
    ci=ci.replace('      - name: Metal compute conformance\n        run: ./build/vulkax --conformance Metal', '      - name: Metal compute conformance\n        run: ./build/vulkax --conformance Metal\n      - name: Native GPU DEM reference\n        run: ctest --test-dir build -R vulkax_gpu_dem --output-on-failure')
Path('.github/workflows/ci.yml').write_text(ci)
