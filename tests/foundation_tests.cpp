#include "vulkax/compiler/expression.hpp"
#include "vulkax/field/field.hpp"
#include "vulkax/numerics/solvers.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace { int failures=0; void check(bool c,const std::string&m){if(!c){++failures;std::cerr<<"FAIL: "<<m<<'\n';}} }

int main(){
    using namespace vulkax;
    auto parsed=compiler::parseExpression("sin(x) + 2*x^2");
    check(parsed.ok(),"expression parser");
    if(parsed.ok()){
        std::unordered_map<std::string,double> vars{{"x",0.5}};
        const double expected=std::sin(0.5)+0.5;
        check(std::abs(compiler::evaluateExpression(*parsed.root,vars)-expected)<1e-12,"expression evaluation");
        auto deriv=compiler::differentiate(*parsed.root,"x");
        check(std::abs(compiler::evaluateExpression(*deriv,vars)-(std::cos(0.5)+2.0))<1e-8,"symbolic derivative");
    }

    field::GridShape grid{17,1,1,{},{0.1,1,1}};
    field::ScalarField scalar{grid,std::vector<double>(grid.cellCount())};
    for(std::uint32_t x=0;x<grid.nx;++x)scalar.values[grid.index(x,0,0)]=static_cast<double>(x*x)*0.01;
    auto grad=field::gradient(scalar);
    check(std::abs(grad.values[grid.index(8,0,0)].x-1.6)<1e-10,"field gradient central difference");

    numerics::LinearOperator A=[](std::span<const double>x,std::span<double>y){y[0]=4*x[0]+x[1];y[1]=x[0]+3*x[1];};
    std::vector<double>b{1,2};
    auto cg=numerics::conjugateGradient(A,b,{0,0},1e-12,20);
    check(cg.converged,"CG converges SPD system");
    check(std::abs(cg.x[0]-1.0/11.0)<1e-10&&std::abs(cg.x[1]-7.0/11.0)<1e-10,"CG solution");

    numerics::OdeRhs rhs=[](double,std::span<const double>x,std::span<double>d){d[0]=x[0];};
    const std::vector<double> state{1};
    auto next=numerics::rk4Step(rhs,0,state,0.1);
    check(std::abs(next[0]-std::exp(0.1))<1e-6,"RK4 exponential");

    numerics::NonlinearResidual residual=[](std::span<const double>x,std::span<double>r){r[0]=x[0]*x[0]-2;};
    auto root=numerics::newtonSolve(residual,{1.0},1e-12,20);
    check(root.converged&&std::abs(root.x[0]-std::sqrt(2.0))<1e-8,"Newton nonlinear solve");

    if(failures){std::cerr<<failures<<" foundation test(s) failed\n";return 1;}std::cout<<"Vulkax foundation tests passed\n";return 0;
}
