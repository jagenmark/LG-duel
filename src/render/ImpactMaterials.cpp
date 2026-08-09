#include "render/ImpactMaterials.hpp"

#include "sim/Arena.hpp"

#include <algorithm>
#include <initializer_list>
#include <system_error>

namespace lg {
namespace {

[[nodiscard]] bool containsAny(
  std::string_view value,
  std::initializer_list<std::string_view> terms
) {
  return std::any_of(terms.begin(), terms.end(), [value](std::string_view term) {
    return value.find(term) != std::string_view::npos;
  });
}

void sortAndDedupe(std::vector<ImpactSurfaceMaterial>& materials) {
  std::sort(
    materials.begin(),
    materials.end(),
    [](const ImpactSurfaceMaterial& lhs, const ImpactSurfaceMaterial& rhs) {
      if (lhs.materialId != rhs.materialId) {
        return lhs.materialId < rhs.materialId;
      }
      return lhs.category < rhs.category;
    }
  );
  materials.erase(
    std::unique(
      materials.begin(),
      materials.end(),
      [](const ImpactSurfaceMaterial& lhs, const ImpactSurfaceMaterial& rhs) {
        return lhs.materialId == rhs.materialId;
      }
    ),
    materials.end()
  );
}

} // namespace

std::string normalizeImpactMaterialAlias(std::string_view alias) {
  std::string normalized(alias);
  std::transform(
    normalized.begin(),
    normalized.end(),
    normalized.begin(),
    [](unsigned char character) {
      if (character == '\\') {
        return '/';
      }
      if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character - 'A' + 'a');
      }
      return static_cast<char>(character);
    }
  );
  return normalized;
}

ImpactSurfaceCategory classifyImpactMaterialAlias(std::string_view alias) {
  const std::string normalized = normalizeImpactMaterialAlias(alias);
  // These terms come from the checked-in texture tree and map only broad
  // material families. Weapon presentation never inspects texture paths.
  if (containsAny(normalized, {"metal", "oxidized", "chain"})) {
    return ImpactSurfaceCategory::Metal;
  }
  if (containsAny(
        normalized,
        {
          "stone",
          "brick",
          "tile",
          "roof",
          "basalt",
          "sandstone",
          "concrete",
          "masonry",
          "marble",
          "plaster",
          "clay",
        }
      )) {
    return ImpactSurfaceCategory::Stone;
  }
  if (containsAny(normalized, {"wood", "timber", "plank", "cardboard"})) {
    return ImpactSurfaceCategory::WoodSoft;
  }
  if (containsAny(
        normalized,
        {
          "element",
          "energy",
          "teleport",
          "plasma",
          "forcefield",
          "tech",
          "amber_route",
        }
      )) {
    return ImpactSurfaceCategory::Energy;
  }
  return ImpactSurfaceCategory::GenericHard;
}

std::vector<ImpactSurfaceMaterial> buildImpactSurfaceMaterialsFromAliases(
  std::span<const std::string> aliases
) {
  std::vector<ImpactSurfaceMaterial> materials;
  materials.reserve(aliases.size());
  for (const std::string& alias : aliases) {
    const std::string normalized = normalizeImpactMaterialAlias(alias);
    materials.push_back({
      arenaMaterialId(normalized),
      classifyImpactMaterialAlias(normalized),
    });
  }
  sortAndDedupe(materials);
  return materials;
}

std::vector<ImpactSurfaceMaterial> loadImpactSurfaceMaterials(
  const std::filesystem::path& textureRoot
) {
  std::error_code error;
  if (!std::filesystem::is_directory(textureRoot, error) || error) {
    return {};
  }

  std::vector<std::string> aliases;
  std::filesystem::recursive_directory_iterator iterator(
    textureRoot,
    std::filesystem::directory_options::skip_permission_denied,
    error
  );
  const std::filesystem::recursive_directory_iterator end;
  while (!error && iterator != end) {
    const std::filesystem::directory_entry& entry = *iterator;
    if (entry.is_regular_file(error) && !error) {
      std::filesystem::path relative = entry.path().lexically_relative(textureRoot);
      const std::string withExtension =
        normalizeImpactMaterialAlias(relative.generic_string());
      if (relative.extension() == ".png" ||
          normalizeImpactMaterialAlias(relative.extension().string()) == ".png") {
        aliases.push_back(withExtension);
        relative.replace_extension();
        aliases.push_back(
          normalizeImpactMaterialAlias(relative.generic_string())
        );
      }
    }
    iterator.increment(error);
  }
  return buildImpactSurfaceMaterialsFromAliases(aliases);
}

ImpactSurfaceCategory impactSurfaceCategory(
  std::uint32_t materialId,
  std::span<const ImpactSurfaceMaterial> sortedMaterials
) {
  if (materialId == 0U) {
    return ImpactSurfaceCategory::GenericHard;
  }
  const auto found = std::lower_bound(
    sortedMaterials.begin(),
    sortedMaterials.end(),
    materialId,
    [](const ImpactSurfaceMaterial& entry, std::uint32_t id) {
      return entry.materialId < id;
    }
  );
  return found != sortedMaterials.end() && found->materialId == materialId
    ? found->category
    : ImpactSurfaceCategory::GenericHard;
}

ImpactSurfaceCategory impactSurfaceCategory(
  const WorldTrace& trace,
  std::span<const ImpactSurfaceMaterial> sortedMaterials
) {
  return impactSurfaceCategory(trace.materialId, sortedMaterials);
}

} // namespace lg
