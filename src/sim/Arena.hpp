#pragma once

#include "shared/Constants.hpp"
#include "shared/Math.hpp"
#include "sim/PlayerState.hpp"
#include "sim/GameMode.hpp"

#include <array>
#include <span>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

struct ArenaCollisionIndex;

inline constexpr float kDefaultJumpPadSpeed = 20.0F;
inline constexpr float kPlayerStepHeight = 0.45F;
inline constexpr float kMinWalkNormal = 0.7F;
inline constexpr std::uint32_t kInvalidSourceGeometryIndex = UINT32_MAX;

// Collision classes remain behaviorally identical until player/projectile
// masks are separated. Keeping the authored distinction now makes imported
// geometry auditable without changing server-authoritative traces.
enum class ArenaCollisionKind : std::uint8_t {
  VisibleSolid = 0,
  PlayerClip = 1,
  WeaponClip = 2,
};

enum class SkyId : std::uint8_t {
  None = 0,
  Aurora = 1,
  CrimsonSunset = 2,
};

enum class ArenaSurfaceKind : std::uint8_t {
  Default = 0,
  Sky = 1,
};

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
  std::array<ArenaSurfaceKind, 6> faceSurfaceKinds = {};
  ArenaCollisionKind collisionKind = ArenaCollisionKind::VisibleSolid;
  std::uint32_t sourceEntityIndex = kInvalidSourceGeometryIndex;
  std::uint32_t sourceBrushIndex = kInvalidSourceGeometryIndex;
  std::uint32_t sourcePatchIndex = kInvalidSourceGeometryIndex;
  std::uint32_t sourcePatchPieceIndex = kInvalidSourceGeometryIndex;
  bool renderable = true;
};

struct ArenaBrushFace {
  static constexpr std::size_t kMaxVertices = 12;

  Vec3 normal = {};
  float distance = 0.0F;
  std::uint32_t materialId = 0;
  TextureProjection textureProjection = {};
  ArenaSurfaceKind surfaceKind = ArenaSurfaceKind::Default;
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
  ArenaCollisionKind collisionKind = ArenaCollisionKind::VisibleSolid;
  std::uint32_t sourceEntityIndex = kInvalidSourceGeometryIndex;
  std::uint32_t sourceBrushIndex = kInvalidSourceGeometryIndex;
  std::uint32_t sourcePatchIndex = kInvalidSourceGeometryIndex;
  std::uint32_t sourcePatchPieceIndex = kInvalidSourceGeometryIndex;
};

struct ArenaStaticLight {
  Vec3 position = {};
  Vec3 color = {1.0F, 1.0F, 1.0F};
  float intensity = 1.0F;
  float radius = 8.0F;
  // A zero source radius keeps the authored light as an ideal point source.
  // The renderer may use a larger value to soften cached point-light shadows.
  float sourceRadius = 0.0F;
  // The renderer ranks this signed author hint when its finite shadow budget
  // cannot keep every eligible light.
  std::int16_t priority = 0;
  bool castsShadows = false;
  bool flickerEnabled = false;
  std::uint32_t flickerSeed = 0;
  float flickerFrequencyHz = 0.0F;
  float flickerMinFactor = 1.0F;
  float flickerMaxFactor = 1.0F;
};

struct ArenaAmbientLight {
  Vec3 color = {1.0F, 1.0F, 1.0F};
  float intensity = 0.30F;
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

struct ArenaTeleport {
  Vec3 min = {};
  Vec3 max = {};
  Vec3 destination = {};
  Vec3 exitVelocity = {};
};

enum class HealthPickupType : std::uint8_t {
  Small = 0,
  Large = 1,
};

struct ArenaHealthPickup {
  Vec3 position = {};
  HealthPickupType type = HealthPickupType::Small;
};

// Server pickup collection and map navigation must agree on this boundary.
// Equality counts as a touch, matching the server's strict "outside" test.
inline constexpr float kHealthPickupTouchRadius = 0.7F;
inline constexpr float kHealthPickupTouchHalfHeight = 0.8F;

[[nodiscard]] inline bool playerTouchesHealthPickup(
  CollisionBounds bounds,
  Vec3 position,
  const ArenaHealthPickup& pickup
) {
  const Vec3 delta = position - pickup.position;
  const float touchRadius = bounds.radius + kHealthPickupTouchRadius;
  const float touchHalfHeight = bounds.halfHeight + kHealthPickupTouchHalfHeight;
  return (delta.x * delta.x) + (delta.y * delta.y) <= touchRadius * touchRadius &&
    delta.z >= -touchHalfHeight && delta.z <= touchHalfHeight;
}

struct ArenaMcGuffinBase {
  Vec3 min = {};
  Vec3 max = {};
  Team team = Team::None;
};

struct ArenaMcGuffinLayout {
  Vec3 neutralSpawn = {};
  ArenaMcGuffinBase redBase = {};
  ArenaMcGuffinBase blueBase = {};
  bool hasNeutralSpawn = false;
  bool hasRedBase = false;
  bool hasBlueBase = false;
};

enum class ArenaSpawnGroup : std::uint8_t {
  None = 0,
  RedBase = 1,
  BlueBase = 2,
};

struct ArenaTeamSpawn {
  Vec3 position = {};
  float yawRadians = 0.0F;
  ArenaSpawnGroup group = ArenaSpawnGroup::None;
};

template <typename T, std::size_t MaxCount>
class BoundedArenaStorage {
public:
  // Indexed writes materialize only the required prefix on the heap. Arena's
  // active count must never expose an element that has not first been written.
  [[nodiscard]] T& operator[](std::size_t index) {
    if (index >= MaxCount) {
      throw std::out_of_range("arena storage capacity exceeded");
    }
    if (index >= elements_.size()) {
      elements_.resize(index + 1U);
    }
    return elements_[index];
  }

  [[nodiscard]] const T& operator[](std::size_t index) const {
    return elements_[index];
  }

private:
  std::vector<T> elements_;
};

struct Arena {
  static constexpr std::size_t kWallCount = 2048;
  static constexpr std::size_t kBrushCount = 1024;
  static constexpr std::size_t kVisualWallCount = 2048;
  static constexpr std::size_t kVisualBrushCount = 1024;
  static constexpr std::size_t kStaticLightCount = 96;
  static constexpr std::size_t kJumpPadCount = 48;
  static constexpr std::size_t kTeleportCount = 16;
  static constexpr std::size_t kHealthPickupCount = 32;
  static constexpr std::size_t kSpawnCount = 32;
  static constexpr std::size_t kTeamSpawnCount = 32;

  Vec3 min = {-12.0F, -12.0F, 0.0F};
  Vec3 max = {12.0F, 12.0F, 8.0F};
  // Hand-made test maps use the plain ground grid. Source-bound imports own
  // every visible floor and must not receive a second face at world z = 0.
  bool renderDefaultFloor = true;
  SkyId skyId = SkyId::None;
  // Active prefixes preserve authored order. Geometry is heap-backed to keep
  // Arena small on the stack, but collision still scales linearly with counts.
  BoundedArenaStorage<ArenaWall, kWallCount> walls = {};
  std::size_t wallCount = 0;
  BoundedArenaStorage<ArenaBrush, kBrushCount> brushes = {};
  std::size_t brushCount = 0;
  // Visual geometry is map content but never authoritative collision. Keeping
  // it in disjoint storage makes accidental movement/trace participation hard.
  BoundedArenaStorage<ArenaWall, kVisualWallCount> visualWalls = {};
  std::size_t visualWallCount = 0;
  BoundedArenaStorage<ArenaBrush, kVisualBrushCount> visualBrushes = {};
  std::size_t visualBrushCount = 0;
  std::array<ArenaStaticLight, kStaticLightCount> staticLights = {};
  std::size_t staticLightCount = 0;
  ArenaAmbientLight ambientLight = {};
  ArenaSunLight sunLight = {};
  std::array<ArenaJumpPad, kJumpPadCount> jumpPads = {};
  std::size_t jumpPadCount = 0;
  std::array<ArenaTeleport, kTeleportCount> teleports = {};
  std::size_t teleportCount = 0;
  std::array<ArenaHealthPickup, kHealthPickupCount> healthPickups = {};
  std::size_t healthPickupCount = 0;
  std::array<Vec3, kSpawnCount> spawnPositions = {{
    {-3.0F, 0.0F, 0.0F},
    {3.0F, 0.0F, 0.0F},
    {0.0F, 3.0F, 0.0F},
    {0.0F, -3.0F, 0.0F},
    {-6.0F, 3.0F, 0.0F},
    {6.0F, 3.0F, 0.0F},
  }};
  // Deathmatch spawns are authored content, not player slots. Only this
  // prefix participates in validation, hashing, and server selection.
  std::size_t spawnCount = 6;
  // Team tags are optional for legacy modes, but McGuffin validates that both
  // playable teams have at least one authored spawn.
  std::array<Team, kSpawnCount> spawnTeams = {};
  // Team modes use a larger physical spawn pool. Groups describe map sides,
  // not permanent teams, so ownership swaps do not require rewriting the map.
  std::array<ArenaTeamSpawn, kTeamSpawnCount> teamSpawns = {};
  std::size_t teamSpawnCount = 0;
  ArenaMcGuffinLayout mcguffin = {};
  // Optional immutable acceleration data. Directly constructed test arenas
  // remain valid and use the linear oracle until finalized by a map boundary.
  std::shared_ptr<const ArenaCollisionIndex> collisionIndex;
};

// Used before a server selects a packaged map and by clients that receive that
// built-in arena descriptor during connection setup.
[[nodiscard]] Arena makeDefaultServerArena();
[[nodiscard]] bool hasValidMcGuffinLayout(const Arena& arena);
[[nodiscard]] bool pointInsideMcGuffinBase(Vec3 point, const ArenaMcGuffinBase& base);

struct ArenaLoadResult {
  Arena arena = {};
  bool ok = false;
  std::string error;
};

[[nodiscard]] ArenaLoadResult loadArenaFromText(std::string_view text);
[[nodiscard]] ArenaLoadResult loadArenaFromMapText(std::string_view text);
bool loadArenaFromFile(const std::string& path, ArenaLoadResult& result);
[[nodiscard]] ArenaLoadResult loadArenaFromFile(const std::string& path);
[[nodiscard]] std::uint32_t arenaMaterialId(std::string_view material);

struct CollisionResult {
  Vec3 position = {};
  Vec3 velocity = {};
  Vec3 groundNormal = {0.0F, 0.0F, 1.0F};
  bool onGround = false;
  bool groundPlane = false;
  bool blocked = false;
  std::uint8_t hitFlags = 0;
};

enum class MovementHitFlags : std::uint8_t {
  None = 0,
  Arena = 1U << 0U,
  Player = 1U << 1U,
};

struct PlayerCollisionProxy {
  std::uint8_t playerIndex = 0;
  Vec3 position = {};
  CollisionBounds bounds = {};
};

struct PlayerCollisionProxySet {
  std::array<PlayerCollisionProxy, kMaxPlayers - 1U> proxies = {};
  std::uint8_t count = 0;
  std::uint32_t presentationServerTick = 0;
  std::uint32_t mapRevision = 0;

  [[nodiscard]] std::span<const PlayerCollisionProxy> span() const {
    return {proxies.data(), count};
  }
};

[[nodiscard]] bool isPlayerCollisionEligible(
  bool connected,
  bool bot,
  bool participating,
  const PlayerState& player
);

[[nodiscard]] bool hasMovementHitFlag(
  const CollisionResult& result,
  MovementHitFlags flag
);

[[nodiscard]] CollisionResult slidePlayerArenaMove(
  const Arena& arena,
  const PlayerState& player,
  Vec3 start,
  Vec3 velocity,
  float fixedDt
);

[[nodiscard]] CollisionResult slidePlayerMove(
  const Arena& arena,
  std::span<const PlayerCollisionProxy> proxies,
  const PlayerState& player,
  std::uint8_t playerIndex,
  Vec3 start,
  Vec3 velocity,
  float fixedDt
);

[[nodiscard]] bool playerPositionOverlapsProxy(
  const PlayerState& player,
  Vec3 position,
  const PlayerCollisionProxy& proxy
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
