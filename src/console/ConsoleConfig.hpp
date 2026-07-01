#pragma once

#include "console/ConsoleSystem.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace lg {

struct ConsoleConfigResult {
  bool ok = true;
  std::vector<std::string> errors;
};

[[nodiscard]] ConsoleConfigResult executeConsoleConfigText(
  ConsoleSystem& console,
  std::string_view text
);

[[nodiscard]] ConsoleConfigResult executeConsoleConfigFile(
  ConsoleSystem& console,
  const std::string& path
);

} // namespace lg
