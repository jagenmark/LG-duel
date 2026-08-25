#pragma once

#include <cstdint>

namespace lg {

struct DeveloperControlOptions {
  bool enabled = false;
  std::uint16_t port = 27961;
};

} // namespace lg
