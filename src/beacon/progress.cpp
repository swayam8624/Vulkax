#include "beacon/progress.hpp"

#include <algorithm>
#include <iostream>

namespace lve::beacon {

void printProgressBar(std::string_view label, uint32_t current, uint32_t total, bool enabled) {
  if (!enabled || total == 0) return;
  constexpr uint32_t width = 36;
  float fraction = std::clamp(static_cast<float>(current) / static_cast<float>(total), 0.f, 1.f);
  uint32_t filled = static_cast<uint32_t>(fraction * width);

  std::cout << "\r" << label << " [";
  for (uint32_t i = 0; i < width; ++i) {
    std::cout << (i < filled ? "#" : "-");
  }
  std::cout << "] " << static_cast<uint32_t>(fraction * 100.f) << "% (" << current << "/" << total
            << ")" << std::flush;
}

void finishProgressBar(bool enabled) {
  if (enabled) {
    std::cout << std::endl;
  }
}

}  // namespace lve::beacon
