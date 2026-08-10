#include "vulkax/solvers/hyperelastic_fem.hpp"

#include "vulkax/numerics/dense.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace vulkax::solvers {

namespace {

struct Mat3 {
    double v[3][3]{};
};

Mat3 subtract(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.v[i][j] = a.v[i][j] - b.v[i][j];
    return r;
}

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 3; ++k) r.v[i][j] += a.v[i][k] * b.v[k][j];
    return r;
}

Mat3 transpose(const Mat3& a) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.v[i][j] = a.v[j][i];
    return r;
}

double determinant(const Mat3& a) {
    return a.v[0][0] * (a.v[1][1] * a.v[2][2] - a.v[1][2] * a.v[2][1])
         - a.v[0][1] * (a.v[1][0] * a.v[2][2] - a.v[1][2] * a.v[2][0])
         + a.v[0][2] * (a.v[1][0] * a.v[2][1] - a.v[1][1] * a.v[2][0]);
}

Mat3 inverse(const Mat3& a) {
    const double d = determinant(a);
    if (std::abs(d) <= 1.0e-15) throw std::invalid_argument("singular tetrahedral reference matrix");
    Mat3 r{};
    r.v[0][0] =  (a.v[1][1]*a.v[2][2]-a.v[1][2]*a.v[2][1])/d;
    r.v[0][1] = -(a.v[0][1]*a.v[2][2]-a.v[0][2]*a.v[2][1])/d;
    r.v[0][2] =  (a.v[0][1]*a.v[1][2]-a.v[0][2]*a.v[1][1])/d;
    r.v[1][0] = -(a.v[1][0]*a.v[2][2]-a.v[1][2]*a.v[2][0])/d;
    r.v[1][1] =  (a.v[0][0]*a.v[2][2]-a.v[0][2]*a.v[2][0])/d;
    r.v[1][2] = -(a.v[0][0]*a.v[1][2]-a.v[0][2]*a.v[1][0])/d;
    r.v[2][0] =  (a.v[1][0]*a.v[2][1]-a.v[1][1]*a.v[2][0])/d;
    r.v[2][1] = -(a.v[0][0]*a.v[2][1]-a.v[0][1]*a.v[2][0])/d;
    r.v[2][2] =  (a.v[0][0]*a.v[1][1]-a.v[0][1]*a.v[1][0])/d;
    return r;
}

Mat3 edgeMatrix(const std::array<math::Vec3, 4>& p) {
    Mat3 d{};
    const std::array<math::Vec3, 3> e{p[1]-p[0], p[2]-p[0], p[3]-p[0]};
    for (int col = 0; col < 3; ++col) {
        d.v[0][col] = e[static_cast<std::size_t>(col)].x;
        d.v[1][col] = e[static_cast<std::size_t>(col)].y;
        d.v[2][col] = e[static_cast<std::size_t>(col)].z;
    }
    return d;
}

double trace(const Mat3& a) { return a.v[0][0] + a.v[1][1] + a.v[2][2]; }

struct MaterialParameters { double mu; double lambda; };

MaterialParameters lame(NeoHookeanMaterial material) {
    if (material.youngModulus <= 0.0 || material.poissonRatio <= -1.0 || material.poissonRatio >= 0.5)
        throw std::invalid_argument("Neo-Hookean material requires E>0 and -1<nu<0.5");
    return {material.youngModulus / (2.0*(1.0+material.poissonRatio)),
            material.youngModulus*material.poissonRatio /
                ((1.0+material.poissonRatio)*(1.0-2.0*material.poissonRatio))};
}

std::vector<std::size_t> freeDofs(const std::vector<FemNode>& nodes) {
    std::vector<std::size_t> result;
    for (std::size_t node=0; node<nodes.size(); ++node)
        for (std::size_t axis=0; axis<3; ++axis)
            if (!nodes[node].fixed[axis]) result.push_back(node*3+axis);
    return result;
}

std::vector<math::Vec3> deformedPositions(const std::vector<FemNode>& nodes,
                                          const std::vector<std::size_t>& free,
                                          const std::vector<double>& q) {
    std::vector<math::Vec3> p;
    p.reserve(nodes.size());
    for (const auto& node : nodes) p.push_back(node.position);
    for (std::size_t i=0; i<free.size(); ++i) {
        const std::size_t node = free[i]/3;
        const std::size_t axis = free[i]%3;
        if (axis==0) p[node].x += q[i];
        else if (axis==1) p[node].y += q[i];
        else p[node].z += q[i];
    }
    return p;
}

struct EnergyEvaluation { double strain{}; double potential{}; bool valid{true}; };

EnergyEvaluation energy(const std::vector<FemNode>& nodes, const std::vector<Tetrahedron>& elements,
                        MaterialParameters material, const std::vector<std::size_t>& free,
                        const std::vector<double>& q) {
    const auto deformed = deformedPositions(nodes, free, q);
    double strainEnergy = 0.0;
    for (const auto& element : elements) {
        std::array<math::Vec3,4> x{}, X{};
        for (std::size_t i=0;i<4;++i) {
            if (element.node[i]>=nodes.size()) throw std::invalid_argument("invalid tetrahedral index");
            X[i]=nodes[element.node[i]].position;
            x[i]=deformed[element.node[i]];
        }
        const Mat3 Dm=edgeMatrix(X);
        const Mat3 Ds=edgeMatrix(x);
        const double referenceDet=determinant(Dm);
        const double volume=std::abs(referenceDet)/6.0;
        if (volume<=1e-15) throw std::invalid_argument("degenerate reference tetrahedron");
        const Mat3 F=multiply(Ds,inverse(Dm));
        const double J=determinant(F);
        if (!(J>1e-8) || !std::isfinite(J)) return {0.0,std::numeric_limits<double>::infinity(),false};
        const Mat3 C=multiply(transpose(F),F);
        const double logJ=std::log(J);
        const double psi=0.5*material.mu*(trace(C)-3.0)-material.mu*logJ+
                         0.5*material.lambda*logJ*logJ;
        strainEnergy += volume*psi;
    }
    double external=0.0;
    for (std::size_t i=0;i<free.size();++i) {
        const std::size_t node=free[i]/3, axis=free[i]%3;
        const double f=axis==0?nodes[node].force.x:(axis==1?nodes[node].force.y:nodes[node].force.z);
        external += f*q[i];
    }
    return {strainEnergy,strainEnergy-external,true};
}

std::vector<double> gradient(const std::vector<FemNode>& nodes, const std::vector<Tetrahedron>& elements,
                             MaterialParameters material, const std::vector<std::size_t>& free,
                             const std::vector<double>& q, double h) {
    std::vector<double> g(q.size());
    for (std::size_t i=0;i<q.size();++i) {
        const double step=h*std::max(1.0,std::abs(q[i]));
        auto plus=q, minus=q; plus[i]+=step; minus[i]-=step;
        const auto ep=energy(nodes,elements,material,free,plus);
        const auto em=energy(nodes,elements,material,free,minus);
        if (!ep.valid || !em.valid) throw std::runtime_error("finite-difference gradient crossed inverted element");
        g[i]=(ep.potential-em.potential)/(2.0*step);
    }
    return g;
}

numerics::DenseMatrix hessian(const std::vector<FemNode>& nodes, const std::vector<Tetrahedron>& elements,
                              MaterialParameters material, const std::vector<std::size_t>& free,
                              const std::vector<double>& q, double h) {
    numerics::DenseMatrix H(q.size(),q.size());
    const double baseStep=h;
    for (std::size_t j=0;j<q.size();++j) {
        const double step=baseStep*std::max(1.0,std::abs(q[j]));
        auto plus=q, minus=q; plus[j]+=step; minus[j]-=step;
        const auto gp=gradient(nodes,elements,material,free,plus,h);
        const auto gm=gradient(nodes,elements,material,free,minus,h);
        for (std::size_t i=0;i<q.size();++i) H(i,j)=(gp[i]-gm[i])/(2.0*step);
    }
    for (std::size_t i=0;i<q.size();++i) H(i,i)+=1e-9;
    return H;
}

double vectorNorm(const std::vector<double>& v) { return numerics::l2Norm(v); }

double elementVonMises(const std::array<math::Vec3,4>& X,const std::array<math::Vec3,4>& x,
                       MaterialParameters m) {
    const Mat3 F=multiply(edgeMatrix(x),inverse(edgeMatrix(X)));
    const double J=determinant(F);
    const Mat3 B=multiply(F,transpose(F));
    Mat3 I{}; I.v[0][0]=I.v[1][1]=I.v[2][2]=1.0;
    Mat3 sigma=subtract(B,I);
    const double logJ=std::log(J);
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
        sigma.v[i][j]*=m.mu/J;
        if(i==j) sigma.v[i][j]+=m.lambda*logJ/J;
    }
    const double sx=sigma.v[0][0], sy=sigma.v[1][1], sz=sigma.v[2][2];
    const double txy=sigma.v[0][1], tyz=sigma.v[1][2], tzx=sigma.v[2][0];
    return std::sqrt(0.5*((sx-sy)*(sx-sy)+(sy-sz)*(sy-sz)+(sz-sx)*(sz-sx))+
                     3.0*(txy*txy+tyz*tyz+tzx*tzx));
}

} // namespace

NonlinearFemResult solveNeoHookeanTetrahedral(const std::vector<FemNode>& nodes,
                                              const std::vector<Tetrahedron>& elements,
                                              NeoHookeanMaterial material,
                                              const NonlinearFemOptions& options) {
    if(nodes.empty()||elements.empty()||options.maxNewtonIterations==0||options.gradientTolerance<=0.0||
       options.finiteDifferenceStep<=0.0||options.minimumLineSearchScale<=0.0)
        throw std::invalid_argument("invalid nonlinear FEM request");
    const auto mp=lame(material);
    const auto free=freeDofs(nodes);
    if(free.empty()) throw std::invalid_argument("nonlinear FEM has no free degrees of freedom");
    std::vector<double> q(free.size(),0.0);
    NonlinearFemResult result;

    for(std::size_t iteration=0;iteration<options.maxNewtonIterations;++iteration) {
        const auto g=gradient(nodes,elements,mp,free,q,options.finiteDifferenceStep);
        result.finalGradientNorm=vectorNorm(g);
        result.iterations=iteration;
        if(result.finalGradientNorm<options.gradientTolerance) { result.converged=true; break; }
        auto H=hessian(nodes,elements,mp,free,q,options.finiteDifferenceStep);
        std::vector<double> rhs=g; for(double& x:rhs)x=-x;
        std::vector<double> step;
        try { step=numerics::solveGaussian(H,rhs,1e-14); }
        catch(const std::exception&) { for(std::size_t i=0;i<q.size();++i) H(i,i)+=1e-3*material.youngModulus; step=numerics::solveGaussian(H,rhs,1e-14); }
        const auto current=energy(nodes,elements,mp,free,q);
        double scale=1.0;
        bool accepted=false;
        while(scale>=options.minimumLineSearchScale) {
            auto candidate=q; for(std::size_t i=0;i<q.size();++i) candidate[i]+=scale*step[i];
            const auto trial=energy(nodes,elements,mp,free,candidate);
            if(trial.valid&&trial.potential<current.potential) { q=std::move(candidate); accepted=true; break; }
            scale*=0.5;
        }
        if(!accepted) break;
        result.iterations=iteration+1;
    }

    const auto finalEnergy=energy(nodes,elements,mp,free,q);
    result.strainEnergy=finalEnergy.strain;
    result.totalPotential=finalEnergy.potential;
    result.displacement.assign(nodes.size(),{});
    for(std::size_t i=0;i<free.size();++i) {
        const std::size_t node=free[i]/3,axis=free[i]%3;
        if(axis==0)result.displacement[node].x=q[i]; else if(axis==1)result.displacement[node].y=q[i]; else result.displacement[node].z=q[i];
    }
    const auto deformed=deformedPositions(nodes,free,q);
    for(const auto& element:elements) {
        std::array<math::Vec3,4>X{},x{};
        for(std::size_t i=0;i<4;++i){X[i]=nodes[element.node[i]].position;x[i]=deformed[element.node[i]];}
        result.vonMisesStress.push_back(elementVonMises(X,x,mp));
    }
    const auto finalGradient=gradient(nodes,elements,mp,free,q,options.finiteDifferenceStep);
    result.finalGradientNorm=vectorNorm(finalGradient);
    if(result.finalGradientNorm<options.gradientTolerance) result.converged=true;
    return result;
}

} // namespace vulkax::solvers
