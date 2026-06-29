#pragma once

#include "shared/Constants.hpp"
#include "shared/Math.hpp"
#include "sim/PlayerState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lg {

struct TextureProjection {
  Vec3 uAxis = {};
  Vec3 vAxis = {};
  float uOffset = 0.0F;
  float vOffset = 0.0F;
  float rotationDegrees = 0.0F;
  float uScale = 1.0F;
  float vScale = 1.0F;
  bool valid = false;
};

struct ArenaWall {
  Vec3 min = {};
  Vec3 max = {};
  std::uint32_t materialId = 0;
  std::array<std::uint32_t, 6> faceMaterialIds = {};
  std::array<TextureProjection, 6> faceTextureProjections = {};
};

struct ArenaBrushFace {
  static constexpr std::size_t kMaxVertices = 12;

  Vec3 normal = {};
  float distance = 0.0F;
  std::uint32_t materialId = 0;
  TextureProjection textureProjection = {};
  std::array<std::uint8_t, kMaxVertices> vertices = {};
  std::uint8_t vertexCount = 0;
};

struct ArenaBrush {
  static constexpr std::size_t kMaxFaces = 16;
  static constexpr std::size_t kMaxVertices = 32;

  Vec3 min = {};
  Vec3 max = {};
  std::uint32_t materialId = 0;
  std::array<Vec3, kMaxVertices> vertices = {};
  std::uint8_t vertexCount = 0;
  std::array<ArenaBrushFace, kMaxFaces> faces = {};
  std::uint8_t faceCount = 0;
};

struct Arena {
  static constexpr std::size_t kWallCount = 255;
  static constexpr std::size_t kBrushCount = 128;

  Vec3 min = {-12.0F, -12.0F, 0.0F};
  Vec3 max = {12.0F, 12.0F, 8.0F};
  std::array<ArenaWall, kWallCount> walls = {};
  std::size_t wallCount = 0;
  std::array<ArenaBrush, kBrushCount> brushes = {};
  std::size_t brushCount = 0;
  std::array<Vec3, kMaxPlayers> spawnPositions = {{
    {-3.0F, 0.0F, 0.0F},
    {3.0F, 0.0F, 0.0F},
    {0.0F, 3.0F, 0.0F},
    {0.0F, -3.0F, 0.0F},
    {-6.0F, 3.0F, 0.0F},
    {6.0F, 3.0F, 0.0F},
  }};
};

struct ArenaLoadResult {
  Arena arena = {};
  bool ok = false;
  std::string error;
};

[[nodiscard]] ArenaLoadResult loadArenaFromText(std::string_view text);
[[nodiscard]] ArenaLoadResult loadArenaFromMapText(std::string_view text);
[[nodiscard]] ArenaLoadResult loadArenaFromFile(const std::string& path);
[[nodiscard]] std::uint32_t arenaMaterialId(std::string_view material);
[[nodiscard]] Arena thunderstruckArena();

struct CollisionResult {
  Vec3 position = {};
  Vec3 velocity = {};
  bool onGround = false;
};

[[nodiscard]] CollisionResult resolvePlayerArenaCollision(
  const Arena& arena,
  const PlayerState& player,
  Vec3 requestedPosition,
  Vec3 requestedVelocity
);

} // namespace lg
