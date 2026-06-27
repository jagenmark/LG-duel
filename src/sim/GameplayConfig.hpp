#pragma once

#include "sim/Combat.hpp"

#include <string>
#include <string_view>

namespace lg {

struct GameplayConfig {
  GrenadeLauncherTuning grenadeLauncher = {};
};

struct GameplayConfigLoadResult {
  GameplayConfig config = {};
  bool ok = false;
  std::string error;
};

[[nodiscard]] GameplayConfigLoadResult loadGameplayConfigFromText(
  std::string_view text
);

[[nodiscard]] GameplayConfigLoadResult loadGameplayConfigFromFile(
  const std::string& path
);

} // namespace lg
