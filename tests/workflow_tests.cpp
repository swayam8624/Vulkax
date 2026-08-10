#include "vulkax/calibration/hyperelastic.hpp"
#include "vulkax/optimize/optimizer.hpp"
#include "vulkax/visualization/export.hpp"
#include "vulkax/workflow/fidelity.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace { int failures=0; void check(bool c,const std::string&m){if(!c){++failures;std::cerr<<"FAIL: "<<m<<'\n';}} }

int main(){
    using namespace vulkax;
    {
        std::vector<calibration::StressDatum> data;
        const std::vector<double> params{2.0e5,5.0e4,2.0e4};
        for(double stretch:{1.03,1.08,1.15,1.25,1.4,1.6,1.8})
            data.push_back({stretch,calibration::predictNominalStress(calibration::HyperelasticFamily::Yeoh3,params,stretch),1.0});
        auto selection=calibration::selectModel(data);
        check(selection.valid,"hyperelastic model selection valid");
        check(selection.valid&&selection.fits[selection.bestIndex].family==calibration::HyperelasticFamily::Yeoh3,"model selection identifies synthetic Yeoh material");
        auto next=calibration::recommendNextUniaxialStretch(selection,{1.1,1.5,2.0,2.5});
        check(next.valid&&next.stretch>1.0&&next.disagreement>=0,"experiment designer returns informative next stretch");
    }
    {
        auto optimum=optimize::goldenSectionMinimize([](double x){return (x-2.25)*(x-2.25)+3.0;},-5,7,1e-8);
        check(optimum.converged&&std::abs(optimum.x-2.25)<1e-5,"golden-section optimization");
        auto vectorOpt=optimize::projectedGradientMinimize([](const std::vector<double>&x){return (x[0]-1)*(x[0]-1)+2*(x[1]+2)*(x[1]+2);},{-3,4},{-5,-5},{5,5},1e-5,1e-6,200);
        check(std::abs(vectorOpt.x[0]-1)<1e-3&&std::abs(vectorOpt.x[1]+2)<1e-3,"bounded vector optimization");
    }
    {
        workflow::FidelityPolicy policy;policy.relativeTolerance=0.01;policy.residualTolerance=1e-5;policy.conservationTolerance=1e-5;
        const std::vector<workflow::FidelityLevel> levels{{"coarse",0.4,1},{"medium",0.2,4},{"fine",0.1,16},{"reference",0.05,64}};
        auto run=workflow::runFidelityLadder(11,22,"CPU","test",levels,policy,[](const workflow::FidelityLevel&l){return workflow::FidelityObservation{l,1.5+l.spacing*l.spacing,1e-7,2e-7,0.01};},true);
        check(run.convergence.valid&&std::abs(run.convergence.observedOrder-2.0)<1e-8,"Richardson detects second-order convergence");
        check(run.certificate.state==verify::TrustState::Verified,"fidelity ladder reaches verified state from measured uncertainty");
        check(run.observations.size()>=3,"verification requires actual convergence levels");
    }
    {
        field::GridShape grid{8,6,2,{},{1,1,1}};field::ScalarField scalar{grid,std::vector<double>(grid.cellCount())};
        for(std::uint32_t z=0;z<grid.nz;++z)for(std::uint32_t y=0;y<grid.ny;++y)for(std::uint32_t x=0;x<grid.nx;++x)scalar.values[grid.index(x,y,z)]=static_cast<double>(x+y);
        auto image=visualization::renderScalarSliceZ(scalar,0,0,12,visualization::ColorMap::Viridis,4);
        check(image.width==32&&image.height==24&&image.pixels.size()==768,"scientific scalar slice rendering");
        auto path=std::filesystem::temp_directory_path()/"vulkax_workflow_test.ppm";visualization::writePpm(image,path);check(std::filesystem::exists(path)&&std::filesystem::file_size(path)>100,"scientific PPM export");std::filesystem::remove(path);
    }
    if(failures){std::cerr<<failures<<" workflow test(s) failed\n";return 1;}std::cout<<"Vulkax scientific workflow tests passed\n";return 0;
}
