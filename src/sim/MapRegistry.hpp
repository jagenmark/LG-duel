#pragma once

#include "sim/Arena.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace lg {

struct MapDescriptor {
  std::string mapName = "thunderstruck";
  std::uint32_t contentHash = 0;
};

struct LocalMapLoadResult {
  Arena arena = {};
  MapDescriptor descriptor = {};
  bool ok = false;
  std::string error;
};

[[nodiscard]] bool isValidMapName(std::string_view mapName);
[[nodiscard]] std::uint32_t hashArena(const Arena& arena);
[[nodiscard]] MapDescriptor describeMap(std::string mapName, const Arena& arena);
[[nodiscard]] LocalMapLoadResult loadLocalMap(
  const std::string& mapName,
  const std::string& mapDirectory = "maps"
);
[[nodiscard]] LocalMapLoadResult loadAndVerifyLocalMap(
  const MapDescriptor& descriptor,
  const std::string& mapDirectory = "maps"
);

} // namespace lg
