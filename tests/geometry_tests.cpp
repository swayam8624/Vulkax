#include "vulkax/geometry/mesh.hpp"
#include "vulkax/geometry/voxelize.hpp"

#include <cassert>
#include <string>

int main(){using namespace vulkax;const std::string cube=R"obj(
v -0.5 -0.5 -0.5
v 0.5 -0.5 -0.5
v 0.5 0.5 -0.5
v -0.5 0.5 -0.5
v -0.5 -0.5 0.5
v 0.5 -0.5 0.5
v 0.5 0.5 0.5
v -0.5 0.5 0.5
f 1 3 2
f 1 4 3
f 5 6 7
f 5 7 8
f 1 2 6
f 1 6 5
f 4 8 7
f 4 7 3
f 1 5 8
f 1 8 4
f 2 3 7
f 2 7 6
)obj";const auto mesh=geometry::parseObj(cube);const auto topology=geometry::analyzeTopology(mesh);assert(topology.watertight);assert(topology.boundaryEdges==0);const auto voxels=geometry::voxelizeClosedMesh(mesh,24,24,24,{{-1,-1,-1},{1,1,1}});assert(voxels.solidCount()>1000);assert(voxels.solidCount()<3000);assert(voxels.at(12,12,12));assert(!voxels.at(1,1,1));const auto normalized=geometry::normalizedToUnitBox(mesh);const auto b=geometry::bounds(normalized);assert(b.minimum.x>=-1.000001&&b.maximum.x<=1.000001);return 0;}
