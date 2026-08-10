#include "vulkax/problem/document.hpp"
#include "vulkax/problem/validation.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace vulkax;
    problem::ProblemIR original;
    original.id = "airfoil";
    original.name = "Airfoil research problem";
    original.domains.push_back({"air", problem::DomainKind::Volume, 3});
    original.fields.push_back({"velocity", "air", problem::FieldRank::Vector, 3, units::velocity});
    original.fields.push_back({"pressure", "air", problem::FieldRank::Scalar, 1, units::pressure});
    original.operators.push_back({"momentum", "Momentum", "velocity", {"velocity", "pressure"},
                                  "du/dt + advect(u) + grad(p)/rho", "fluid"});
    original.materials.push_back({"air", {{"density", {1.225, units::makeDimension(-3,1,0)}}}});
    original.boundaryConditions.push_back({"inlet", "air", "velocity", "velocity_inlet",
                                           {30.0,0.0,0.0}, units::velocity});
    original.objectives.push_back({"drag", "Drag coefficient", "surface_drag(body)",
                                   problem::ObjectiveDirection::Minimize});
    original.accuracyTargets.push_back({"drag",0.02,std::nullopt});
    original.computeBudget.wallSeconds=120.0;
    original.computeBudget.gpuMemoryBytes=4ull<<30u;
    const std::string text=problem::writeProblemDocument(original);
    const auto parsed=problem::parseProblemDocument(text);
    assert(problem::validateProblem(parsed).ok());
    assert(problem::stableProblemHash(parsed)==problem::stableProblemHash(original));
    assert(problem::writeProblemDocument(parsed)==text);
    return 0;
}
