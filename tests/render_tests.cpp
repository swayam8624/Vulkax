#include "vulkax/render/headless.hpp"

#include <cassert>
#include <cstddef>
#include <vector>

int main(){
    using namespace vulkax;
    const std::vector<visualization::ParticleInstance> particles={
        {{-0.45,-0.15,0.1},0.22,{0.95F,0.25F,0.10F,1.0F}},
        {{0.0,0.1,0.2},0.28,{0.15F,0.80F,0.95F,1.0F}},
        {{0.48,-0.05,0.3},0.18,{0.75F,0.95F,0.20F,1.0F}}};
    for(const auto backend:render::availableHeadlessRenderBackends()){
        const render::RenderSettings settings{160,120,1.0,{0.01F,0.01F,0.015F,1.0F}};
        const auto image=render::renderParticlesHeadless(backend,particles,settings);
        assert(image.pixels.size()==160u*120u*4u);
        std::size_t bright=0;
        for(std::size_t i=0;i<image.pixels.size();i+=4) if(image.pixels[i]>40||image.pixels[i+1]>40||image.pixels[i+2]>40)++bright;
        assert(bright>500);
    }
    return 0;
}
