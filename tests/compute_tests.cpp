#include "vulkax/backend/backend.hpp"
#include "vulkax/compute/compute_ir.hpp"
#include "vulkax/compute/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool c,const std::string&m){if(!c){++failures;std::cerr<<"FAIL: "<<m<<'\n';}}
bool close(const std::vector<float>&a,const std::vector<float>&b,float eps=2e-5F){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(std::abs(a[i]-b[i])>eps)return false;return true;}
}

int main(int argc,char**argv){
    using namespace vulkax;
    constexpr std::uint32_t n=4096;
    std::vector<std::vector<float>> buffers(3,std::vector<float>(n));
    for(std::uint32_t i=0;i<n;++i){buffers[0][i]=std::sin(static_cast<float>(i)*0.013F);buffers[1][i]=0.25F*std::cos(static_cast<float>(i)*0.021F);}
    compute::ComputeProgram p{n,3,{{compute::OpCode::Axpy,2,0,1,2.0F,0.0F},{compute::OpCode::Clamp,2,2,0,-1.25F,1.25F}}};
    const auto cpu=compute::executeReference(p,buffers);
    check(cpu.ok,"CPU reference execution");
    compute::ExecutionResult actual;
    if(argc==2){
        const std::string requested=argv[1];
        backend::BackendKind kind=backend::BackendKind::CPUReference;
        if(requested=="Vulkan")kind=backend::BackendKind::Vulkan;else if(requested=="Metal")kind=backend::BackendKind::Metal;else if(requested=="OpenGL")kind=backend::BackendKind::OpenGL;
        actual=compute::executeWithBackend(kind,p,buffers);
        check(actual.ok,requested+" execution failed: "+actual.diagnostic);
        check(actual.backend==kind,requested+" backend identity");
    }else actual=compute::executeBest(p,buffers);
    if(actual.ok)check(close(cpu.buffers[2],actual.buffers[2]),"GPU/selected backend must match CPU AXPY+clamp reference");

    compute::ComputeProgram lap{n,2,{{compute::OpCode::Laplacian1DPeriodic,1,0,0,4.0F,0.0F}}};
    std::vector<std::vector<float>> wave(2,std::vector<float>(n));
    for(std::uint32_t i=0;i<n;++i)wave[0][i]=std::sin(6.28318530718F*static_cast<float>(i)/static_cast<float>(n));
    const auto lapCpu=compute::executeReference(lap,wave);
    const auto lapActual=argc==2?compute::executeWithBackend(actual.backend,lap,wave):compute::executeBest(lap,wave);
    check(lapActual.ok,"laplacian execution");
    if(lapActual.ok)check(close(lapCpu.buffers[1],lapActual.buffers[1],5e-5F),"periodic Laplacian conformance");

    if(failures){std::cerr<<failures<<" compute test(s) failed\n";return 1;}
    std::cout<<"ComputeIR conformance passed on "<<backend::toString(actual.backend)<<" ("<<actual.deviceName<<")\n";
    return 0;
}
