#pragma once

#include <cstdint>
#include <string_view>

namespace lve::beacon {

void printProgressBar(std::string_view label, uint32_t current, uint32_t total, bool enabled = true);
void finishProgressBar(bool enabled = true);

}  // namespace lve::beacon
