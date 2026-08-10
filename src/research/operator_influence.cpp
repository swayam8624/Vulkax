#include "vulkax/research/operator_influence.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::research {
OperatorInfluenceField computeOperatorInfluence(std::string id,std::span<const double>lambda,std::span<const double>term){if(lambda.size()!=term.size())throw std::invalid_argument("adjoint/residual size mismatch");OperatorInfluenceField out{std::move(id),std::vector<double>(lambda.size())};for(std::size_t i=0;i<lambda.size();++i)out.values[i]=-lambda[i]*term[i];return out;}
double predictObjectiveDelta(const OperatorInfluenceField&f,std::span<const double>a,double w){if(f.values.size()!=a.size())throw std::invalid_argument("influence/intervention size mismatch");if(w<=0)throw std::invalid_argument("quadrature weight must be positive");double d=0;for(std::size_t i=0;i<a.size();++i)d+=f.values[i]*a[i]*w;return d;}
double relativeCounterfactualError(double p,double m,double floor){if(floor<=0)throw std::invalid_argument("floor must be positive");return std::abs(p-m)/std::max({std::abs(m),std::abs(p),floor});}
} // namespace vulkax::research
