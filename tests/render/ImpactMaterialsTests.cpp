#include "render/ImpactMaterials.hpp"

#include "sim/Arena.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;
  failures += expect(
    lg::normalizeImpactMaterialAlias("Overkill\\METAL\\Panel.PNG") ==
      "overkill/metal/panel.png",
    "aliases should normalize slash direction and ASCII case like arenaMaterialId"
  );
  failures += expect(
    lg::classifyImpactMaterialAlias("Base/Metal/Mat_Metal_Gray_01-128x128.png") ==
        lg::ImpactSurfaceCategory::Metal &&
      lg::classifyImpactMaterialAlias("Base/Concrete/Mat_Concrete_Gray_01-128x128.png") ==
        lg::ImpactSurfaceCategory::Stone &&
      lg::classifyImpactMaterialAlias("Circular/Square/Wood/Square_Wood_01-128x128.png") ==
        lg::ImpactSurfaceCategory::WoodSoft &&
      lg::classifyImpactMaterialAlias("fx/teleport_energy.png") ==
        lg::ImpactSurfaceCategory::Energy &&
      lg::classifyImpactMaterialAlias("props/unknown_panel.png") ==
        lg::ImpactSurfaceCategory::GenericHard,
    "checked material aliases should select each broad surface category"
  );

  const std::vector<std::string> duplicateAliases = {
    "METAL\\panel.png",
    "metal/panel.png",
    "METAL\\panel.png",
  };
  const std::vector<lg::ImpactSurfaceMaterial> deduped =
    lg::buildImpactSurfaceMaterialsFromAliases(duplicateAliases);
  failures += expect(
    deduped.size() == 1U &&
      deduped[0].materialId == lg::arenaMaterialId("metal/panel.png") &&
      deduped[0].category == lg::ImpactSurfaceCategory::Metal,
    "normalized aliases should sort and dedupe to one stable material id"
  );

  const std::filesystem::path testRoot =
    std::filesystem::temp_directory_path() /
    (
      "lg-duel-impact-materials-" +
      std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
      )
    );
  std::filesystem::create_directories(testRoot / "Metal");
  std::ofstream(testRoot / "Metal" / "chain.PNG").put('\n');
  std::ofstream(testRoot / "Metal" / "ignore.txt").put('\n');

  const std::vector<lg::ImpactSurfaceMaterial> loaded =
    lg::loadImpactSurfaceMaterials(testRoot);
  failures += expect(
    lg::impactSurfaceCategory(
      lg::arenaMaterialId("metal/chain.png"),
      loaded
    ) == lg::ImpactSurfaceCategory::Metal &&
      lg::impactSurfaceCategory(
        lg::arenaMaterialId("metal/chain"),
        loaded
      ) == lg::ImpactSurfaceCategory::Metal &&
      loaded.size() == 2U,
    "texture scans should include extension and extensionless aliases only"
  );

  lg::WorldTrace trace;
  trace.materialId = lg::arenaMaterialId("metal/chain");
  failures += expect(
    lg::impactSurfaceCategory(trace, loaded) ==
      lg::ImpactSurfaceCategory::Metal,
    "world trace material ids should use the prebuilt lookup table"
  );
  const std::vector<lg::ImpactSurfaceMaterial> missing =
    lg::loadImpactSurfaceMaterials(testRoot / "missing");
  failures += expect(
    missing.empty() &&
      lg::impactSurfaceCategory(123456U, missing) ==
        lg::ImpactSurfaceCategory::GenericHard,
    "missing texture roots and unknown ids should fall back to generic hard"
  );

  std::error_code cleanupError;
  std::filesystem::remove_all(testRoot, cleanupError);
  return failures == 0 ? 0 : 1;
}
