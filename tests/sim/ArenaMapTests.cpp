#include "sim/Arena.hpp"
#include "sim/MapRegistry.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
  int failures = 0;

  {
    const lg::LocalMapLoadResult loaded = lg::loadLocalMap("eyetoeye");
    failures += expect(loaded.ok, "eyetoeye should load from the local map registry");
    const lg::Arena& arena = loaded.arena;
    failures += expect(arena.wallCount > 0, "eyetoeye should load static geometry");
    failures += expect(loaded.descriptor.mapName == "eyetoeye", "eyetoeye descriptor should use the map stem");
    failures += expect(
      loaded.descriptor.contentHash == lg::hashArena(arena),
      "eyetoeye descriptor hash should match loaded arena"
    );
    lg::Arena visuallyChanged = arena;
    visuallyChanged.walls[0].faceMaterialIds[0] ^= 0x1U;
    failures += expect(
      lg::hashArena(visuallyChanged) != loaded.descriptor.contentHash,
      "map hash should include per-face render materials"
    );
    visuallyChanged = arena;
    visuallyChanged.walls[0].faceTextureProjections[0].uOffset += 1.0F;
    failures += expect(
      lg::hashArena(visuallyChanged) != loaded.descriptor.contentHash,
      "map hash should include texture projections used by the renderer"
    );
    lg::Arena auditMetadataChanged = arena;
    auditMetadataChanged.walls[0].collisionKind = lg::ArenaCollisionKind::PlayerClip;
    failures += expect(
      lg::hashArena(auditMetadataChanged) != loaded.descriptor.contentHash,
      "map hash should include wall collision classification"
    );
    auditMetadataChanged = arena;
    auditMetadataChanged.walls[0].sourceEntityIndex = 7U;
    auditMetadataChanged.walls[0].sourceBrushIndex = 42U;
    failures += expect(
      lg::hashArena(auditMetadataChanged) != loaded.descriptor.contentHash,
      "map hash should include wall source provenance"
    );
    failures += expect(arena.brushCount > 0, "eyetoeye should include a convex brush");
    if (arena.brushCount > 0) {
      auditMetadataChanged = arena;
      auditMetadataChanged.brushes[0].collisionKind = lg::ArenaCollisionKind::WeaponClip;
      auditMetadataChanged.brushes[0].sourceEntityIndex = 8U;
      auditMetadataChanged.brushes[0].sourceBrushIndex = 9U;
      failures += expect(
        lg::hashArena(auditMetadataChanged) != loaded.descriptor.contentHash,
        "map hash should include convex-brush collision classification and provenance"
      );
    }
    lg::MapDescriptor mismatched = loaded.descriptor;
    mismatched.contentHash ^= 0x1U;
    const lg::LocalMapLoadResult verified = lg::loadAndVerifyLocalMap(mismatched);
    failures += expect(!verified.ok, "local map verification should reject mismatched hashes");
  }

  {
    const lg::LocalMapLoadResult loaded = lg::loadLocalMap("overkill_import");
    failures += expect(loaded.ok, "generated overkill import should load through the local map registry");
    failures += expect(
      loaded.ok && loaded.arena.wallCount == 1254,
      "generated overkill import should omit the three non-solid fog volumes"
    );
    failures += expect(
      loaded.ok && loaded.arena.brushCount == 915,
      "generated overkill import should preserve its validated convex-brush count"
    );
    failures += expect(
      loaded.ok &&
        loaded.arena.spawnCount == 32 &&
        nearlyEqual(loaded.arena.spawnPositions[0].x, -2.0F) &&
        nearlyEqual(loaded.arena.spawnPositions[0].y, -3.8F) &&
        nearlyEqual(loaded.arena.spawnPositions[31].x, -11.6F) &&
        nearlyEqual(loaded.arena.spawnPositions[31].y, -39.2F),
      "generated overkill import should preserve all source deathmatch spawns"
    );
    failures += expect(
      loaded.ok && loaded.arena.teleportCount == 1,
      "generated overkill import should preserve its restored teleport route"
    );
    failures += expect(
      loaded.ok && loaded.arena.staticLightCount == 6 && loaded.arena.sunLight.enabled,
      "generated overkill import should preserve its reviewed adaptation lighting"
    );
    failures += expect(
      loaded.ok && loaded.descriptor.contentHash == lg::hashArena(loaded.arena),
      "generated overkill import descriptor should bind to parsed arena content"
    );
  }

  {
    const lg::LocalMapLoadResult missing = lg::loadLocalMap("missing_map");
    failures += expect(!missing.ok, "missing local map should fail like any other local map");
  }

  {
    const std::filesystem::path temporaryDirectory =
      std::filesystem::temp_directory_path() / "lg-duel-map-error-test";
    const std::filesystem::path malformedMap = temporaryDirectory / "malformed.map";
    std::filesystem::create_directories(temporaryDirectory);
    {
      std::ofstream file(malformedMap);
      file << "version 1\n";
    }
    const lg::LocalMapLoadResult malformed =
      lg::loadLocalMap("malformed", temporaryDirectory.string());
    failures += expect(!malformed.ok, "malformed local map should fail");
    failures += expect(
      malformed.error.find("expected entity") != std::string::npos,
      "local map failures should preserve the parser diagnostic"
    );
    std::filesystem::remove(malformedMap);
    std::filesystem::remove(temporaryDirectory);
  }

  {
    constexpr std::string_view text = R"(version 1
bounds min=-4,-4,0 max=4,4,4
box center -1,-1,0 1,1,1
spawn p1 -2,0,0 yaw=0
spawn p2 2,0,0 yaw=180
)";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text);
    failures += expect(result.ok, "valid map text should load");
    failures += expect(result.arena.wallCount == 1, "valid map should include one box");
    failures += expect(nearlyEqual(result.arena.spawnPositions[1].x, 2.0F), "valid map should parse spawns");
  }

  {
    std::ostringstream text;
    text << "version 1\n"
         << "bounds min=-40,-2,0 max=40,2,4\n"
         << "box floor -40,-2,0 40,2,1\n";
    for (std::size_t index = 0; index < lg::Arena::kSpawnCount; ++index) {
      text << "spawn p" << index << ' ' << static_cast<int>(index) - 16 << ",0,1\n";
    }
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text.str());
    failures += expect(result.ok, "maps may author thirty-two deathmatch spawns");
    failures += expect(
      result.ok && result.arena.spawnCount == lg::Arena::kSpawnCount &&
        nearlyEqual(result.arena.spawnPositions[31].x, 15.0F),
      "loader should retain the complete authored spawn prefix"
    );

    if (result.ok) {
      lg::Arena inactiveChanged = result.arena;
      inactiveChanged.spawnCount = 31;
      const std::uint32_t before = lg::hashArena(inactiveChanged);
      inactiveChanged.spawnPositions[31].x += 1.0F;
      failures += expect(
        lg::hashArena(inactiveChanged) == before,
        "arena hash should ignore inactive spawn storage"
      );
    }
  }

  {
    std::ostringstream text;
    text << "version 1\n"
         << "bounds min=-40,-2,0 max=40,2,4\n"
         << "box floor -40,-2,0 40,2,1\n";
    for (std::size_t index = 0; index <= lg::Arena::kSpawnCount; ++index) {
      text << "spawn p" << index << ' ' << static_cast<int>(index) - 16 << ",0,1\n";
    }
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text.str());
    failures += expect(
      !result.ok && result.error.find("too many spawn points") != std::string::npos,
      "maps above the thirty-two-spawn capacity should fail clearly"
    );
  }

  {
    std::ostringstream text;
    text << "version 1\n";
    text << "bounds min=0,0,0 max=600,2,2\n";
    for (int index = 0; index < 300; ++index) {
      const float x = static_cast<float>(index) * 2.0F;
      text << "box box_" << index << ' '
           << x << ",0,0 "
           << x + 1.0F << ",1,1\n";
    }
    text << "spawn p1 1,1.5,0\n";
    text << "spawn p2 599,1.5,0\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text.str());
    failures += expect(result.ok, "three-hundred-box map should load above the old arena limit");
    failures += expect(result.arena.wallCount == 300, "expanded arena limit should preserve all boxes");
    failures += expect(
      nearlyEqual(result.arena.walls[299].min.x, 598.0F),
      "expanded arena storage should preserve authored box order"
    );
  }

  {
    lg::Arena arena;
    for (std::size_t index = 0; index < 300; ++index) {
      arena.brushes[index].materialId = static_cast<std::uint32_t>(index + 1U);
    }
    arena.brushCount = 300;
    const lg::Arena& loadedArena = arena;
    failures += expect(
      loadedArena.brushes[299].materialId == 300U,
      "expanded convex-brush storage should preserve entries above the old limit"
    );
  }

  {
    std::ostringstream text;
    text << "version 1\n";
    text << "bounds min=-2,-2,0 max=2,2,2\n";
    for (std::size_t index = 0; index <= lg::Arena::kWallCount; ++index) {
      text << "box box_" << index << " -1,-1,0 1,1,1\n";
    }
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text.str());
    failures += expect(!result.ok, "maps above the fixed box limit should still fail");
    failures += expect(
      result.error.find("too many boxes") != std::string::npos,
      "box capacity failures should preserve the loader diagnostic"
    );
  }

  {
    constexpr std::string_view text = R"(version 1
bounds min=-4,-4,0 max=4,4,4
box bad 2,0,0 1,1,1
spawn p1 -2,0,0
spawn p2 2,0,0
)";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text);
    failures += expect(!result.ok, "inverted box should be rejected");
  }

  {
    constexpr std::string_view text = R"(version 1
bounds min=-4,-4,0 max=4,4,4
box first -2,-2,0 1,1,1
box second 0,0,0 2,2,1
spawn p1 -2,0,0
spawn p2 2,0,0
)";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text);
    failures += expect(result.ok, "overlapping boxes should be accepted");
    failures += expect(result.arena.wallCount == 2, "overlapping boxes should be preserved");
  }

  {
    constexpr std::string_view text = R"(version 1
bounds min=-4,-4,0 max=4,4,4
box center -1,-1,0 1,1,1
spawn p1 -2,0,0
)";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text);
    failures += expect(!result.ok, "maps need at least two spawns");
  }

  return failures == 0 ? 0 : 1;
}
