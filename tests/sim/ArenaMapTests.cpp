#include "sim/Arena.hpp"
#include "sim/MapRegistry.hpp"

#include <array>
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

std::size_t materialFaceCount(const lg::Arena& arena, std::uint32_t materialId) {
  std::size_t count = 0;
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const lg::ArenaWall& wall = arena.walls[index];
    for (const std::uint32_t faceMaterialId : wall.faceMaterialIds) {
      count += faceMaterialId == materialId ? 1U : 0U;
    }
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const lg::ArenaBrush& brush = arena.brushes[index];
    for (std::size_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      count += brush.faces[faceIndex].materialId == materialId ? 1U : 0U;
    }
  }
  return count;
}

void hashGeometryByte(std::uint64_t& hash, std::uint8_t value) {
  hash ^= value;
  hash *= 1099511628211ULL;
}

void hashGeometryU32(std::uint64_t& hash, std::uint32_t value) {
  hashGeometryByte(hash, static_cast<std::uint8_t>(value));
  hashGeometryByte(hash, static_cast<std::uint8_t>(value >> 8U));
  hashGeometryByte(hash, static_cast<std::uint8_t>(value >> 16U));
  hashGeometryByte(hash, static_cast<std::uint8_t>(value >> 24U));
}

void hashGeometryU64(std::uint64_t& hash, std::uint64_t value) {
  hashGeometryU32(hash, static_cast<std::uint32_t>(value));
  hashGeometryU32(hash, static_cast<std::uint32_t>(value >> 32U));
}

void hashGeometryFloat(std::uint64_t& hash, float value) {
  // Brush plane math can vary in its last float bit across compilers. Hash at
  // a fixed 0.00001 LG-world-unit step while keeping collision changes visible.
  constexpr double kGeometryHashUnitsPerWorldUnit = 100000.0;
  const std::int64_t quantized = static_cast<std::int64_t>(
    std::llround(static_cast<double>(value) * kGeometryHashUnitsPerWorldUnit)
  );
  hashGeometryU64(hash, static_cast<std::uint64_t>(quantized));
}

void hashGeometryVec3(std::uint64_t& hash, lg::Vec3 value) {
  hashGeometryFloat(hash, value.x);
  hashGeometryFloat(hash, value.y);
  hashGeometryFloat(hash, value.z);
}

void hashWallGeometry(std::uint64_t& hash, const lg::ArenaWall& wall) {
  hashGeometryVec3(hash, wall.min);
  hashGeometryVec3(hash, wall.max);
  hashGeometryU32(hash, static_cast<std::uint32_t>(wall.collisionKind));
  hashGeometryU32(hash, wall.sourceEntityIndex);
  hashGeometryU32(hash, wall.sourceBrushIndex);
  hashGeometryU32(hash, wall.sourcePatchIndex);
  hashGeometryU32(hash, wall.sourcePatchPieceIndex);
  hashGeometryU32(hash, wall.renderable ? 1U : 0U);
}

void hashBrushGeometry(std::uint64_t& hash, const lg::ArenaBrush& brush) {
  hashGeometryVec3(hash, brush.min);
  hashGeometryVec3(hash, brush.max);
  hashGeometryU32(hash, static_cast<std::uint32_t>(brush.collisionKind));
  hashGeometryU32(hash, brush.sourceEntityIndex);
  hashGeometryU32(hash, brush.sourceBrushIndex);
  hashGeometryU32(hash, brush.sourcePatchIndex);
  hashGeometryU32(hash, brush.sourcePatchPieceIndex);
  hashGeometryU32(hash, brush.renderable ? 1U : 0U);
  hashGeometryU32(hash, brush.vertexCount);
  for (std::size_t vertexIndex = 0; vertexIndex < brush.vertexCount; ++vertexIndex) {
    hashGeometryVec3(hash, brush.vertices[vertexIndex]);
  }
  hashGeometryU32(hash, brush.faceCount);
  for (std::size_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
    const lg::ArenaBrushFace& face = brush.faces[faceIndex];
    hashGeometryVec3(hash, face.normal);
    hashGeometryFloat(hash, face.distance);
    hashGeometryU32(hash, face.vertexCount);
    for (std::size_t vertexIndex = 0; vertexIndex < face.vertexCount; ++vertexIndex) {
      hashGeometryByte(hash, face.vertices[vertexIndex]);
    }
  }
}

std::uint64_t collisionGeometryFingerprint(const lg::Arena& arena) {
  std::uint64_t hash = 14695981039346656037ULL;
  hashGeometryVec3(hash, arena.min);
  hashGeometryVec3(hash, arena.max);
  hashGeometryU32(hash, static_cast<std::uint32_t>(arena.wallCount));
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    hashWallGeometry(hash, arena.walls[index]);
  }
  hashGeometryU32(hash, static_cast<std::uint32_t>(arena.brushCount));
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    hashBrushGeometry(hash, arena.brushes[index]);
  }
  hashGeometryU32(hash, static_cast<std::uint32_t>(arena.visualWallCount));
  for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
    hashWallGeometry(hash, arena.visualWalls[index]);
  }
  hashGeometryU32(hash, static_cast<std::uint32_t>(arena.visualBrushCount));
  for (std::size_t index = 0; index < arena.visualBrushCount; ++index) {
    hashBrushGeometry(hash, arena.visualBrushes[index]);
  }
  return hash;
}

} // namespace

int main() {
  int failures = 0;

  {
    const lg::LocalMapLoadResult loaded = lg::loadLocalMap("eyetoeye");
    failures += expect(loaded.ok, "eyetoeye should load from the local map registry");
    const lg::Arena& arena = loaded.arena;
    failures += expect(arena.wallCount > 0, "eyetoeye should load static geometry");
    constexpr std::uint64_t expectedCollisionGeometryFingerprint =
      0xa738db490b895daeULL;
    const std::uint64_t actualCollisionGeometryFingerprint =
      collisionGeometryFingerprint(arena);
    if (actualCollisionGeometryFingerprint != expectedCollisionGeometryFingerprint) {
      std::cerr << "EyeToEye collision geometry fingerprint: 0x" << std::hex
                << actualCollisionGeometryFingerprint << std::dec << '\n';
    }
    failures += expect(
      actualCollisionGeometryFingerprint == expectedCollisionGeometryFingerprint,
      "eyetoeye collision geometry fingerprint should stay fixed across art-only changes"
    );
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

    const std::uint32_t sandstone =
      lg::arenaMaterialId("Overkill/Overkill_Sandstone_Floor-128x128");
    const std::uint32_t trim =
      lg::arenaMaterialId("Overkill/Overkill_Oxidized_Trim-128x128");
    const std::uint32_t basalt =
      lg::arenaMaterialId("Overkill/Overkill_Basalt_Block-128x128");
    const std::uint32_t amber =
      lg::arenaMaterialId("Overkill/Overkill_Amber_Route-128x128");
    failures += expect(
      materialFaceCount(arena, sandstone) == 6U,
      "eyetoeye should parse six sandstone upward faces"
    );
    failures += expect(
      materialFaceCount(arena, trim) == 212U,
      "eyetoeye should parse 212 center, post, and lamp trim faces"
    );
    failures += expect(
      materialFaceCount(arena, basalt) == 96U,
      "eyetoeye should parse 96 basalt perimeter faces"
    );
    failures += expect(
      materialFaceCount(arena, amber) == 5U,
      "eyetoeye should parse four amber cardinal pads and one center fire face"
    );

    const lg::Vec3 expectedSunDirection = lg::normalize({0.35F, -0.5F, -1.0F});
    failures += expect(
      arena.sunLight.enabled &&
        nearlyEqual(arena.sunLight.direction.x, expectedSunDirection.x) &&
        nearlyEqual(arena.sunLight.direction.y, expectedSunDirection.y) &&
        nearlyEqual(arena.sunLight.direction.z, expectedSunDirection.z) &&
        nearlyEqual(arena.sunLight.color.x, 238.0F / 255.0F) &&
        nearlyEqual(arena.sunLight.color.y, 226.0F / 255.0F) &&
        nearlyEqual(arena.sunLight.color.z, 206.0F / 255.0F) &&
        nearlyEqual(arena.sunLight.intensity, 0.6F),
      "eyetoeye sun should parse its warm color, direction, and intensity"
    );
    failures += expect(
      arena.staticLightCount == 8U,
      "eyetoeye should keep its four outer lamps and four floor lights"
    );
    if (arena.staticLightCount == 8U) {
      constexpr std::array<lg::Vec3, 8> expectedPositions = {{
        {23.6F, 0.0F, -11.6F},
        {9.4F, -11.6F, -15.60875F},
        {10.6F, 10.8F, -15.60875F},
        {-12.0F, 9.2F, -15.60875F},
        {-9.8F, -11.2F, -15.60875F},
        {-0.1F, -23.6F, -11.6F},
        {-23.6F, 0.0F, -11.6F},
        {0.0F, 23.6F, -11.6F},
      }};
      for (std::size_t index = 0; index < expectedPositions.size(); ++index) {
        const lg::ArenaStaticLight& light = arena.staticLights[index];
        const bool outerLamp = index == 0U || index >= 5U;
        failures += expect(
          nearlyEqual(light.position.x, expectedPositions[index].x) &&
            nearlyEqual(light.position.y, expectedPositions[index].y) &&
            nearlyEqual(light.position.z, expectedPositions[index].z),
          "eyetoeye art pass should not move light origins"
        );
        failures += expect(
          nearlyEqual(light.color.x, 1.0F) &&
            nearlyEqual(light.color.y, (outerLamp ? 220.0F : 150.0F) / 255.0F) &&
            nearlyEqual(light.color.z, (outerLamp ? 180.0F : 72.0F) / 255.0F) &&
            nearlyEqual(light.intensity, outerLamp ? 0.75F : 0.35F) &&
            nearlyEqual(light.radius, outerLamp ? 12.5F : 14.0F),
          "eyetoeye point lights should parse the art-pass color, intensity, and radius"
        );
      }
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
      loaded.ok && loaded.arena.wallCount == 1251,
      "generated overkill import should omit fog volumes and replaced doorway main clips"
    );
    failures += expect(
      loaded.ok && loaded.arena.brushCount == 911,
      "generated overkill import should preserve its validated convex-brush count"
    );
    failures += expect(
      loaded.ok && loaded.arena.visualWallCount == 0 && loaded.arena.visualBrushCount == 83,
      "reviewed doorway patches and teleporter frame should load as render-only convex geometry"
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
      loaded.ok && loaded.arena.staticLightCount == 11 && loaded.arena.sunLight.enabled,
      "generated overkill import should preserve its reviewed adaptation lighting"
    );
    if (loaded.ok) {
      const lg::Arena& arena = loaded.arena;
      const lg::Vec3 expectedSunDirection = lg::normalize({0.35F, -0.5F, -1.0F});
      failures += expect(
        arena.sunLight.enabled &&
          nearlyEqual(arena.sunLight.direction.x, expectedSunDirection.x) &&
          nearlyEqual(arena.sunLight.direction.y, expectedSunDirection.y) &&
          nearlyEqual(arena.sunLight.direction.z, expectedSunDirection.z) &&
          nearlyEqual(arena.sunLight.color.x, 1.0F) &&
          nearlyEqual(arena.sunLight.color.y, 226.0F / 255.0F) &&
          nearlyEqual(arena.sunLight.color.z, 184.0F / 255.0F) &&
          nearlyEqual(arena.sunLight.intensity, 0.85F),
        "overkill sun should match its reviewed warm lighting"
      );
      if (arena.staticLightCount == 11U) {
        constexpr std::array<lg::Vec3, 11> expectedPositions = {{
          {-2.0F, 2.0F, 17.0F},
          {16.0F, 26.25F, 26.25F},
          {16.0F, -33.75F, 17.0F},
          {-27.5F, -2.5F, 14.0F},
          {26.25F, 16.25F, 15.0F},
          {29.6F, 52.0F, 13.0F},
          {0.0F, -9.0F, 11.0F},
          {16.0F, 9.0F, 19.0F},
          {16.0F, -26.0F, 11.0F},
          {-32.5F, -23.0F, 10.0F},
          {29.6F, 44.0F, 9.0F},
        }};
        constexpr std::array<lg::Vec3, 11> expectedColors = {{
          {1.0F, 218.0F / 255.0F, 170.0F / 255.0F},
          {188.0F / 255.0F, 214.0F / 255.0F, 1.0F},
          {1.0F, 205.0F / 255.0F, 150.0F / 255.0F},
          {196.0F / 255.0F, 220.0F / 255.0F, 1.0F},
          {1.0F, 216.0F / 255.0F, 164.0F / 255.0F},
          {1.0F, 174.0F / 255.0F, 82.0F / 255.0F},
          {168.0F / 255.0F, 202.0F / 255.0F, 1.0F},
          {176.0F / 255.0F, 208.0F / 255.0F, 1.0F},
          {185.0F / 255.0F, 210.0F / 255.0F, 1.0F},
          {190.0F / 255.0F, 216.0F / 255.0F, 1.0F},
          {190.0F / 255.0F, 214.0F / 255.0F, 1.0F},
        }};
        constexpr std::array<float, 11> expectedIntensities = {
          0.8F, 0.75F, 0.75F, 0.7F, 0.7F, 0.9F,
          1.1F, 1.05F, 1.0F, 1.1F, 0.75F,
        };
        constexpr std::array<float, 11> expectedRadii = {
          35.0F, 32.5F, 35.0F, 30.0F, 30.0F, 22.5F,
          40.0F, 40.0F, 37.5F, 30.0F, 25.0F,
        };
        for (std::size_t index = 0; index < expectedPositions.size(); ++index) {
          const lg::ArenaStaticLight& light = arena.staticLights[index];
          failures += expect(
            nearlyEqual(light.position.x, expectedPositions[index].x) &&
              nearlyEqual(light.position.y, expectedPositions[index].y) &&
              nearlyEqual(light.position.z, expectedPositions[index].z) &&
              nearlyEqual(light.color.x, expectedColors[index].x) &&
              nearlyEqual(light.color.y, expectedColors[index].y) &&
              nearlyEqual(light.color.z, expectedColors[index].z) &&
              nearlyEqual(light.intensity, expectedIntensities[index]) &&
              nearlyEqual(light.radius, expectedRadii[index]),
            "overkill point lights should match the reviewed art pass"
          );
        }
      }
    }
    failures += expect(
      loaded.ok && loaded.descriptor.contentHash == lg::hashArena(loaded.arena),
      "generated overkill import descriptor should bind to parsed arena content"
    );

    const auto findWall = [&loaded](std::uint32_t sourceBrushIndex) -> const lg::ArenaWall* {
      for (std::size_t index = 0; index < loaded.arena.wallCount; ++index) {
        const lg::ArenaWall& wall = loaded.arena.walls[index];
        if (wall.sourceEntityIndex == 0U && wall.sourceBrushIndex == sourceBrushIndex) {
          return &wall;
        }
      }
      return nullptr;
    };
    const auto findBrush = [&loaded](std::uint32_t sourceBrushIndex) -> const lg::ArenaBrush* {
      for (std::size_t index = 0; index < loaded.arena.brushCount; ++index) {
        const lg::ArenaBrush& brush = loaded.arena.brushes[index];
        if (brush.sourceEntityIndex == 0U && brush.sourceBrushIndex == sourceBrushIndex) {
          return &brush;
        }
      }
      return nullptr;
    };
    const auto findVisualBrush = [&loaded](std::uint32_t sourceBrushIndex) -> const lg::ArenaBrush* {
      for (std::size_t index = 0; index < loaded.arena.visualBrushCount; ++index) {
        const lg::ArenaBrush& brush = loaded.arena.visualBrushes[index];
        if (brush.sourceEntityIndex == 0U && brush.sourceBrushIndex == sourceBrushIndex) {
          return &brush;
        }
      }
      return nullptr;
    };
    for (const std::uint32_t sourceBrushIndex : {1926U, 1927U, 1928U}) {
      failures += expect(
        findVisualBrush(sourceBrushIndex) != nullptr && findBrush(sourceBrushIndex) != nullptr,
        "teleporter frame visuals should retain links to their authoritative source clips"
      );
    }
    constexpr std::array<std::uint32_t, 3> replacementIndices = {1914U, 1919U, 1920U};
    for (const std::uint32_t sourceBrushIndex : replacementIndices) {
      const lg::ArenaBrush* replacement = findBrush(sourceBrushIndex);
      failures += expect(
        replacement != nullptr && replacement->faceCount == 6U,
        "each reviewed doorway main clip should be replaced by a full-height six-face bevel"
      );
    }
    constexpr std::array<std::uint32_t, 7> removedLipIndices = {
      1884U, 1886U, 1908U, 1909U, 1915U, 1918U, 1921U
    };
    for (const std::uint32_t sourceBrushIndex : removedLipIndices) {
      failures += expect(
        findWall(sourceBrushIndex) == nullptr && findBrush(sourceBrushIndex) == nullptr,
        "each reviewed doorway snag lip should be absent from authoritative collision"
      );
    }

    const auto scrapeDoorway = [&](std::uint32_t bevelIndex,
                                   lg::Vec3 start,
                                   lg::Vec3 heldVelocity) {
      const lg::ArenaBrush* bevel = findBrush(bevelIndex);
      if (bevel == nullptr) {
        return start;
      }
      lg::PlayerState player;
      player.position = start;
      for (int tick = 0; tick < 80; ++tick) {
        const lg::CollisionResult move = lg::slidePlayerArenaMove(
          loaded.arena, player, player.position, heldVelocity, lg::kFixedTickSeconds
        );
        player.position = move.position;
      }
      return player.position;
    };
    const lg::Vec3 southRight = scrapeDoorway(
      1914U, {5.8F, -43.2F, 10.5F}, {-8.0F, -1.0F, 0.0F}
    );
    const lg::Vec3 bentLeft = scrapeDoorway(
      1919U, {-18.6F, 0.35F, 7.3F}, {8.0F, 1.0F, 0.0F}
    );
    const lg::Vec3 bentRight = scrapeDoorway(
      1920U, {5.8F, 0.35F, 7.3F}, {-8.0F, 1.0F, 0.0F}
    );
    const lg::Vec3 southRightReverse = scrapeDoorway(
      1914U, {-1.0F, -43.2F, 10.5F}, {8.0F, -1.0F, 0.0F}
    );
    const lg::Vec3 bentLeftReverse = scrapeDoorway(
      1919U, {-13.4F, 0.35F, 7.3F}, {-8.0F, 1.0F, 0.0F}
    );
    const lg::Vec3 bentRightReverse = scrapeDoorway(
      1920U, {0.6F, 0.35F, 7.3F}, {8.0F, 1.0F, 0.0F}
    );
    const bool doorwayScrapesPass =
      southRight.x < 1.5F && bentLeft.x > -14.0F && bentRight.x < 1.5F &&
      southRightReverse.x > 3.0F && bentLeftReverse.x < -17.0F && bentRightReverse.x > 4.0F;
    if (!doorwayScrapesPass) {
      std::cerr << "doorway scrape positions: southRight=" << southRight.x << ',' << southRight.y
                << " bentLeft=" << bentLeft.x << ',' << bentLeft.y
                << " bentRight=" << bentRight.x << ',' << bentRight.y
                << " southRightReverse=" << southRightReverse.x << ',' << southRightReverse.y
                << " bentLeftReverse=" << bentLeftReverse.x << ',' << bentLeftReverse.y
                << " bentRightReverse=" << bentRightReverse.x << ',' << bentRightReverse.y << '\n';
    }
    failures += expect(
      doorwayScrapesPass,
      "held diagonal movement should scrape through every repaired doorway bevel from either direction"
    );

    const auto scrapeRoute = [&](lg::Vec3 start, lg::Vec3 heldVelocity) {
      lg::PlayerState player;
      player.position = start;
      for (int tick = 0; tick < 80; ++tick) {
        const lg::CollisionResult move = lg::slidePlayerArenaMove(
          loaded.arena, player, player.position, heldVelocity, lg::kFixedTickSeconds
        );
        player.position = move.position;
      }
      return player.position;
    };
    const std::array<lg::Vec3, 8> lipRouteResults = {{
      scrapeRoute({8.3F, 0.2F, 7.3F}, {8.0F, 1.0F, 0.0F}),
      scrapeRoute({8.3F, -1.8F, 7.3F}, {8.0F, -1.0F, 0.0F}),
      scrapeRoute({13.2F, 0.2F, 7.3F}, {-8.0F, 1.0F, 0.0F}),
      scrapeRoute({13.2F, -1.8F, 7.3F}, {-8.0F, -1.0F, 0.0F}),
      scrapeRoute({-21.3F, 0.2F, 7.3F}, {-8.0F, 1.0F, 0.0F}),
      scrapeRoute({-21.3F, -1.8F, 7.3F}, {-8.0F, -1.0F, 0.0F}),
      scrapeRoute({-25.7F, 0.2F, 7.3F}, {8.0F, 1.0F, 0.0F}),
      scrapeRoute({-25.7F, -1.8F, 7.3F}, {8.0F, -1.0F, 0.0F}),
    }};
    const bool lipRoutesPass =
      lipRouteResults[0].x > 11.0F && lipRouteResults[1].x > 11.0F &&
      lipRouteResults[2].x < 10.5F && lipRouteResults[3].x < 10.5F &&
      lipRouteResults[4].x < -24.0F && lipRouteResults[5].x < -24.0F &&
      lipRouteResults[6].x > -23.0F && lipRouteResults[7].x > -23.0F;
    if (!lipRoutesPass) {
      std::cerr << "doorway lip scrape x positions:";
      for (const lg::Vec3 result : lipRouteResults) {
        std::cerr << ' ' << result.x;
      }
      std::cerr << '\n';
    }
    failures += expect(
      lipRoutesPass,
      "held diagonal movement should pass both sides of each doorway after low lips are removed"
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
