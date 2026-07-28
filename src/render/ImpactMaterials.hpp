#pragma once

#include "render/CombatEffects.hpp"
#include "sim/Combat.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

struct ImpactSurfaceMaterial {
  std::uint32_t materialId = 0;
  ImpactSurfaceCategory category = ImpactSurfaceCategory::GenericHard;
};

[[nodiscard]] std::string normalizeImpactMaterialAlias(
  std::string_view alias
);

[[nodiscard]] ImpactSurfaceCategory classifyImpactMaterialAlias(
  std::string_view alias
);

[[nodiscard]] std::vector<ImpactSurfaceMaterial>
buildImpactSurfaceMaterialsFromAliases(
  std::span<const std::string> aliases
);

// Scans once at client startup. Missing or unreadable roots return an empty
// table, so presentation falls back without blocking the client.
[[nodiscard]] std::vector<ImpactSurfaceMaterial> loadImpactSurfaceMaterials(
  const std::filesystem::path& textureRoot
);

[[nodiscard]] ImpactSurfaceCategory impactSurfaceCategory(
  std::uint32_t materialId,
  std::span<const ImpactSurfaceMaterial> sortedMaterials
);

[[nodiscard]] ImpactSurfaceCategory impactSurfaceCategory(
  const WorldTrace& trace,
  std::span<const ImpactSurfaceMaterial> sortedMaterials
);

} // namespace lg
