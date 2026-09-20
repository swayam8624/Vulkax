#include "vulkax/solvers/mpm.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {

using vulkax::solvers::Matrix3;
using vulkax::solvers::MpmConstitutiveModel;
using vulkax::solvers::MpmMaterial;

double determinant(const Matrix3& f) {
    return f[0] * (f[4] * f[8] - f[5] * f[7]) -
           f[1] * (f[3] * f[8] - f[5] * f[6]) +
           f[2] * (f[3] * f[7] - f[4] * f[6]);
}

double energyDensity(const Matrix3& f, const MpmMaterial& m) {
    const double j=determinant(f);
    assert(j>0.0);
    const double mu=m.youngModulus/(2.0*(1.0+m.poissonRatio));
    const double lambda=m.youngModulus*m.poissonRatio/
        ((1.0+m.poissonRatio)*(1.0-2.0*m.poissonRatio));

    double i1=0.0;
    for(double x:f) i1+=x*x;

    switch(m.constitutiveModel) {
        case MpmConstitutiveModel::NeoHookeanLogJ: {
            const double l=std::log(j);
            return 0.5*mu*(i1-3.0)-mu*l+0.5*lambda*l*l;
        }
        case MpmConstitutiveModel::NeoHookeanQuadraticJ:
            return 0.5*mu*(i1-3.0-2.0*std::log(j))+
                   0.5*lambda*(j-1.0)*(j-1.0);
        case MpmConstitutiveModel::StVenantKirchhoff: {
            Matrix3 c{};
            for(std::size_t row=0;row<3;++row)
                for(std::size_t col=0;col<3;++col)
                    for(std::size_t k=0;k<3;++k)
                        c[row*3+col]+=f[k*3+row]*f[k*3+col];
            Matrix3 e{};
            double tr=0.0;
            for(std::size_t row=0;row<3;++row)
                for(std::size_t col=0;col<3;++col) {
                    const std::size_t q=row*3+col;
                    e[q]=0.5*(c[q]-(row==col?1.0:0.0));
                    if(row==col) tr+=e[q];
                }
            double frob=0.0;
            for(double x:e) frob+=x*x;
            return mu*frob+0.5*lambda*tr*tr;
        }
    }
    assert(false);
    return 0.0;
}

double maxAbs(const Matrix3& a) {
    double v=0.0;
    for(double x:a) v=std::max(v,std::abs(x));
    return v;
}

void verifyEnergyGradient(MpmConstitutiveModel model,const Matrix3& f) {
    MpmMaterial m{1000.0,5.0e4,0.31,model};
    const Matrix3 p=vulkax::solvers::firstPiolaMpm(f,m);
    constexpr double h=1.0e-6;
    for(std::size_t q=0;q<9;++q) {
        Matrix3 fp=f,fm=f;
        fp[q]+=h; fm[q]-=h;
        assert(determinant(fp)>0.0 && determinant(fm)>0.0);
        const double fd=(energyDensity(fp,m)-energyDensity(fm,m))/(2.0*h);
        const double scale=std::max({1.0,std::abs(fd),std::abs(p[q])});
        assert(std::abs(fd-p[q])/scale<2.0e-7);
    }
}

} // namespace

int main() {
    using namespace vulkax::solvers;

    // The historical Vulkax control must remain the implicit/default model.
    const MpmMaterial historical{1000.0,5.0e4,0.31};
    assert(historical.constitutiveModel==MpmConstitutiveModel::NeoHookeanLogJ);

    const Matrix3 identity=identityMatrix3();
    for(const auto model:{
        MpmConstitutiveModel::NeoHookeanLogJ,
        MpmConstitutiveModel::NeoHookeanQuadraticJ,
        MpmConstitutiveModel::StVenantKirchhoff}) {
        MpmMaterial m{1000.0,5.0e4,0.31,model};
        assert(maxAbs(firstPiolaMpm(identity,m))<1.0e-12);
    }

    // Independent finite-difference checks of d psi / d F for several finite,
    // orientation-preserving deformation gradients. The energy formulas in this
    // test are deliberately separate from the production stress implementation.
    const std::array<Matrix3,3> states{{
        {1.08,0.04,0.01, 0.02,0.95,0.03, 0.00,0.02,1.03},
        {0.91,0.08,0.02, 0.01,1.12,0.04, 0.03,0.00,0.98},
        {1.16,0.03,0.05, 0.06,0.89,0.01, 0.02,0.04,1.07},
    }};
    for(const auto& f:states) {
        assert(determinant(f)>0.0);
        verifyEnergyGradient(MpmConstitutiveModel::NeoHookeanLogJ,f);
        verifyEnergyGradient(MpmConstitutiveModel::NeoHookeanQuadraticJ,f);
        verifyEnergyGradient(MpmConstitutiveModel::StVenantKirchhoff,f);
    }

    return 0;
}
