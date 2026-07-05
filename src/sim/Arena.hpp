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

inline constexpr float kDefaultJumpPadSpeed = 20.0F;
inline constexpr float kPlayerStepHeight = 0.45F;
inline constexpr float kMinWalkNormal = 0.7F;

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
  bool renderable = true;
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
  bool renderable = true;
  std::array<Vec3, kMaxVertices> vertices = {};
  std::uint8_t vertexCount = 0;
  std::array<ArenaBrushFace, kMaxFaces> faces = {};
  std::uint8_t faceCount = 0;
};

struct ArenaStaticLight {
  Vec3 position = {};
  Vec3 color = {1.0F, 1.0F, 1.0F};
  float intensity = 1.0F;
  float radius = 8.0F;
};

struct ArenaSunLight {
  // Direction the light rays travel in world space. A direction of {0, 0, -1}
  // shines downward; surface lighting uses dot(surfaceNormal, -direction).
  Vec3 direction = {0.25916052F, -0.43193421F, -0.86386842F};
  Vec3 color = {1.0F, 0.94117647F, 0.78431374F};
  float intensity = 0.7F;
  bool enabled = false;
};

struct ArenaJumpPad {
  Vec3 min = {};
  Vec3 max = {};
  Vec3 targetPosition = {};
  Vec3 launchVelocity = {0.0F, 0.0F, kDefaultJumpPadSpeed};
  float targetSpeed = 0.0F;
  bool hasTarget = false;
  bool hasTargetSpeed = false;
};

struct Arena {
  static constexpr std::size_t kWallCount = 255;
  static constexpr std::size_t kBrushCount = 128;
  static constexpr std::size_t kStaticLightCount = 64;
  static constexpr std::size_t kJumpPadCount = 32;

  Vec3 min = {-12.0F, -12.0F, 0.0F};
  Vec3 max = {12.0F, 12.0F, 8.0F};
  std::array<ArenaWall, kWallCount> walls = {};
  std::size_t wallCount = 0;
  std::array<ArenaBrush, kBrushCount> brushes = {};
  std::size_t brushCount = 0;
  std::array<ArenaStaticLight, kStaticLightCount> staticLights = {};
  std::size_t staticLightCount = 0;
  ArenaSunLight sunLight = {};
  std::array<ArenaJumpPad, kJumpPadCount> jumpPads = {};
  std::size_t jumpPadCount = 0;
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

struct CollisionResult {
  Vec3 position = {};
  Vec3 velocity = {};
  Vec3 groundNormal = {0.0F, 0.0F, 1.0F};
  bool onGround = false;
  bool groundPlane = false;
  bool blocked = false;
};

[[nodiscard]] CollisionResult slidePlayerArenaMove(
  const Arena& arena,
  const PlayerState& player,
  Vec3 start,
  Vec3 velocity,
  float fixedDt
);

[[nodiscard]] CollisionResult resolvePlayerArenaCollision(
  const Arena& arena,
  const PlayerState& player,
  Vec3 requestedPosition,
  Vec3 requestedVelocity
);

[[nodiscard]] CollisionResult resolvePlayerArenaCollisionFrom(
  const Arena& arena,
  const PlayerState& player,
  Vec3 previousPosition,
  Vec3 requestedPosition,
  Vec3 requestedVelocity
);

[[nodiscard]] bool playerPositionSolid(
  const Arena& arena,
  const PlayerState& player,
  Vec3 position
);

} // namespace lg
