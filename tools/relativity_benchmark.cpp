#include "vulkax/relativity/schwarzschild_lensing.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    std::filesystem::path output = "docs/results/physics_studio_current/relativity";
    if (argc == 3 && std::string{argv[1]} == "--output") output = argv[2];
    else if (argc != 1) throw std::invalid_argument("usage: vulkax-relativity [--output PATH]");
    std::filesystem::create_directories(output);
    std::ofstream csv{output / "schwarzschild_rays.csv"};
    if (!csv) throw std::runtime_error("could not open relativity result CSV");
    csv << "mass,impact_parameter,escaped,closest_approach,deflection_radians,weak_field_radians,integration_steps\n";
    constexpr double mass = 1.0;
    for (const double impact : {5.0, 5.3, 6.0, 8.0, 16.0, 32.0, 100.0}) {
      const auto result = vulkax::relativity::integrateSchwarzschildRay(mass, impact);
      csv << mass << ',' << impact << ',' << result.escaped << ',' << std::setprecision(17)
          << result.closestApproach << ',' << result.deflectionRadians << ','
          << vulkax::relativity::weakFieldDeflectionRadians(mass, impact) << ','
          << result.integrationSteps << '\n';
      std::cout << "b=" << impact << (result.escaped ? " escaped" : " captured")
                << " deflection=" << result.deflectionRadians << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "vulkax-relativity: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
