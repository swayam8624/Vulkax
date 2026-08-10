#include "vulkax/geometry/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace vulkax::geometry {
namespace {

struct Edge{std::uint32_t a{},b{};bool operator==(const Edge&)const=default;};
struct EdgeHash{std::size_t operator()(const Edge&e)const noexcept{return(static_cast<std::size_t>(e.a)<<32u)^e.b;}};
Edge edge(std::uint32_t a,std::uint32_t b){return a<b?Edge{a,b}:Edge{b,a};}
std::uint32_t faceIndex(const std::string& token,std::size_t vertexCount){
    const auto slash=token.find('/');const std::string part=token.substr(0,slash);if(part.empty())throw std::invalid_argument("OBJ face has empty vertex index");
    const long long raw=std::stoll(part);if(raw==0)throw std::invalid_argument("OBJ indices are one-based");long long index=raw>0?raw-1:static_cast<long long>(vertexCount)+raw;if(index<0||index>=static_cast<long long>(vertexCount))throw std::out_of_range("OBJ face index out of range");return static_cast<std::uint32_t>(index);
}

} // namespace

TriangleMesh parseObj(const std::string& source){
    TriangleMesh mesh;std::istringstream input(source);std::string line;std::size_t lineNumber=0;
    while(std::getline(input,line)){++lineNumber;std::istringstream stream(line);std::string tag;if(!(stream>>tag)||tag[0]=='#')continue;
        try{
            if(tag=="v"){math::Vec3 p;if(!(stream>>p.x>>p.y>>p.z))throw std::invalid_argument("invalid OBJ vertex");mesh.positions.push_back(p);}
            else if(tag=="f"){std::vector<std::uint32_t> face;std::string token;while(stream>>token)face.push_back(faceIndex(token,mesh.positions.size()));if(face.size()<3)throw std::invalid_argument("OBJ face has fewer than three vertices");for(std::size_t i=1;i+1<face.size();++i)mesh.triangles.push_back({face[0],face[i],face[i+1]});}
        }catch(const std::exception&e){throw std::runtime_error("OBJ line "+std::to_string(lineNumber)+": "+e.what());}
    }
    if(mesh.positions.empty()||mesh.triangles.empty())throw std::invalid_argument("OBJ contains no triangle geometry");return mesh;
}

TriangleMesh loadObj(const std::string& path){std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("failed to open OBJ: "+path);std::ostringstream contents;contents<<stream.rdbuf();return parseObj(contents.str());}

Bounds3 bounds(const TriangleMesh& mesh){if(mesh.positions.empty())throw std::invalid_argument("cannot bound empty mesh");const double inf=std::numeric_limits<double>::infinity();Bounds3 b{{inf,inf,inf},{-inf,-inf,-inf}};for(const auto&p:mesh.positions){b.minimum.x=std::min(b.minimum.x,p.x);b.minimum.y=std::min(b.minimum.y,p.y);b.minimum.z=std::min(b.minimum.z,p.z);b.maximum.x=std::max(b.maximum.x,p.x);b.maximum.y=std::max(b.maximum.y,p.y);b.maximum.z=std::max(b.maximum.z,p.z);}return b;}

TopologyReport analyzeTopology(const TriangleMesh& mesh){TopologyReport report;std::unordered_map<Edge,std::size_t,EdgeHash> counts;for(const auto&t:mesh.triangles){if(t[0]>=mesh.positions.size()||t[1]>=mesh.positions.size()||t[2]>=mesh.positions.size())throw std::out_of_range("triangle index out of range");const auto a=mesh.positions[t[0]],b=mesh.positions[t[1]],c=mesh.positions[t[2]];if(math::length(math::cross(b-a,c-a))<=1e-14)++report.degenerateTriangles;for(const auto&e:std::array<Edge,3>{edge(t[0],t[1]),edge(t[1],t[2]),edge(t[2],t[0])})++counts[e];}for(const auto&[e,count]:counts){(void)e;if(count==1)++report.boundaryEdges;else if(count>2)++report.nonManifoldEdges;}report.watertight=report.boundaryEdges==0&&report.nonManifoldEdges==0&&report.degenerateTriangles==0;return report;}

TriangleMesh normalizedToUnitBox(const TriangleMesh& mesh){TriangleMesh result=mesh;const auto b=bounds(mesh);const math::Vec3 extent=b.maximum-b.minimum;const double largest=std::max({extent.x,extent.y,extent.z});if(largest<=0.0)throw std::invalid_argument("mesh has zero extent");const math::Vec3 center=(b.minimum+b.maximum)*0.5;for(auto&p:result.positions)p=(p-center)/largest*2.0;return result;}

} // namespace vulkax::geometry
