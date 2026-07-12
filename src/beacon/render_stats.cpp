#include "beacon/render_stats.hpp"

namespace lve::beacon {

void CpuTimer::begin() { start = std::chrono::high_resolution_clock::now(); }

double CpuTimer::elapsedMs() const {
  auto end = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
}

}  // namespace lve::beacon
