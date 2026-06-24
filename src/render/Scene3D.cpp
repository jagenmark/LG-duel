#include "render/Scene3D.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace lg {
namespace {

constexpr float kQ3RunRoll = 0.005F;
constexpr float kQuakeUnitsPerProjectUnit = 40.0F;
constexpr float kDegreesToRadians = 0.01745329252F;

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs) {
  return {
    lhs.y * rhs.z - lhs.z * rhs.y,
    lhs.z * rhs.x - lhs.x * rhs.z,
    lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

[[nodiscard]] std::uint8_t blendChannel(
  std::uint8_t base,
  std::uint8_t highlight,
  float amount
) {
  return static_cast<std::uint8_t>(
    std::clamp(
      static_cast<float>(base) +
        (static_cast<float>(highlight) - static_cast<float>(base)) * amount,
      0.0F,
      255.0F
    )
  );
}

[[nodiscard]] RenderColor scaleColor(RenderColor color, float amount) {
  return {
    static_cast<std::uint8_t>(std::clamp(
      static_cast<float>(color.red) * amount,
      0.0F,
      255.0F
    )),
    static_cast<std::uint8_t>(std::clamp(
      static_cast<float>(color.green) * amount,
      0.0F,
      255.0F
    )),
    static_cast<std::uint8_t>(std::clamp(
      static_cast<float>(color.blue) * amount,
      0.0F,
      255.0F
    )),
    color.alpha,
  };
}

void addSegment(
  Scene3D& scene,
  Vec3 start,
  Vec3 end,
  float width,
  RenderColor color
);

void addTriangle(
  Scene3D& scene,
  Vec3 first,
  Vec3 second,
  Vec3 third,
  RenderColor color
) {
  std::vector<Vertex3D>& vertices =
    color.alpha == 255 ? scene.vertices : scene.translucentVertices;
  vertices.push_back({first, color});
  vertices.push_back({second, color});
  vertices.push_back({third, color});
}

void addQuad(
  Scene3D& scene,
  Vec3 first,
  Vec3 second,
  Vec3 third,
  Vec3 fourth,
  RenderColor color
) {
  addTriangle(scene, first, second, third, color);
  addTriangle(scene, first, third, fourth, color);
}

void addBox(
  Scene3D& scene,
  Vec3 minimum,
  Vec3 maximum,
  RenderColor color
) {
  const std::array<Vec3, 8> corners = {{
    {minimum.x, minimum.y, minimum.z},
    {maximum.x, minimum.y, minimum.z},
    {maximum.x, maximum.y, minimum.z},
    {minimum.x, maximum.y, minimum.z},
    {minimum.x, minimum.y, maximum.z},
    {maximum.x, minimum.y, maximum.z},
    {maximum.x, maximum.y, maximum.z},
    {minimum.x, maximum.y, maximum.z},
  }};
  constexpr std::array<std::array<std::size_t, 4>, 6> faces = {{
    {{0, 3, 2, 1}},
    {{4, 5, 6, 7}},
    {{0, 1, 5, 4}},
    {{1, 2, 6, 5}},
    {{2, 3, 7, 6}},
    {{3, 0, 4, 7}},
  }};
  constexpr std::array<float, 6> brightness = {
    0.55F, 1.0F, 0.72F, 0.86F, 0.66F, 0.78F,
  };
  for (std::size_t index = 0; index < faces.size(); ++index) {
    const auto& face = faces[index];
    addQuad(
      scene,
      corners[face[0]],
      corners[face[1]],
      corners[face[2]],
      corners[face[3]],
      scaleColor(color, brightness[index])
    );
  }
}

void addWallBox(Scene3D& scene, Vec3 minimum, Vec3 maximum) {
  const std::array<Vec3, 8> corners = {{
    {minimum.x, minimum.y, minimum.z},
    {maximum.x, minimum.y, minimum.z},
    {maximum.x, maximum.y, minimum.z},
    {minimum.x, maximum.y, minimum.z},
    {minimum.x, minimum.y, maximum.z},
    {maximum.x, minimum.y, maximum.z},
    {maximum.x, maximum.y, maximum.z},
    {minimum.x, maximum.y, maximum.z},
  }};
  constexpr std::array<std::array<std::size_t, 4>, 6> faces = {{
    {{0, 3, 2, 1}},
    {{4, 5, 6, 7}},
    {{0, 1, 5, 4}},
    {{1, 2, 6, 5}},
    {{2, 3, 7, 6}},
    {{3, 0, 4, 7}},
  }};
  constexpr std::array<RenderColor, 6> colors = {{
    {91, 63, 39, 255},
    {86, 176, 96, 255},
    {126, 87, 50, 255},
    {146, 101, 58, 255},
    {107, 73, 44, 255},
    {133, 91, 53, 255},
  }};
  for (std::size_t index = 0; index < faces.size(); ++index) {
    const auto& face = faces[index];
    addQuad(
      scene,
      corners[face[0]],
      corners[face[1]],
      corners[face[2]],
      corners[face[3]],
      colors[index]
    );
  }
}

void addFloorQuad(
  Scene3D& scene,
  float minX,
  float minY,
  float maxX,
  float maxY,
  float z,
  RenderColor color
) {
  addQuad(
    scene,
    {minX, minY, z},
    {maxX, minY, z},
    {maxX, maxY, z},
    {minX, maxY, z},
    color
  );
}

void addFloorTreatment(Scene3D& scene, const Arena& arena) {
  constexpr float baseZ = 0.0F;
  constexpr float tileZ = 0.001F;
  constexpr float laneZ = 0.002F;
  constexpr float gridZ = 0.006F;
  constexpr float gridWidth = 0.012F;
  constexpr float tileSize = 2.0F;

  addFloorQuad(
    scene,
    arena.min.x,
    arena.min.y,
    arena.max.x,
    arena.max.y,
    baseZ,
    {56, 138, 70, 255}
  );

  for (float x = arena.min.x; x < arena.max.x; x += tileSize) {
    for (float y = arena.min.y; y < arena.max.y; y += tileSize) {
      const int checker =
        static_cast<int>(std::floor((x - arena.min.x) / tileSize)) +
        static_cast<int>(std::floor((y - arena.min.y) / tileSize));
      const RenderColor color = checker % 2 == 0
        ? RenderColor{67, 158, 76, 255}
        : RenderColor{74, 174, 84, 255};
      addFloorQuad(
        scene,
        x,
        y,
        std::min(x + tileSize, arena.max.x),
        std::min(y + tileSize, arena.max.y),
        tileZ,
        color
      );
    }
  }

  const float laneHalfWidth = 1.15F;
  addFloorQuad(
    scene,
    arena.min.x,
    -laneHalfWidth,
    arena.max.x,
    laneHalfWidth,
    laneZ,
    {186, 151, 91, 255}
  );
  addFloorQuad(
    scene,
    -laneHalfWidth,
    arena.min.y,
    laneHalfWidth,
    arena.max.y,
    laneZ,
    {197, 163, 101, 255}
  );

  constexpr std::array<Vec3, 6> flowerPatches = {{
    {-10.5F, -8.5F, 0.0F},
    {-7.5F, 8.0F, 0.0F},
    {-2.8F, -9.2F, 0.0F},
    {3.4F, 8.8F, 0.0F},
    {8.4F, -8.0F, 0.0F},
    {11.2F, 5.6F, 0.0F},
  }};
  constexpr std::array<RenderColor, 3> flowerColors = {{
    {255, 224, 102, 255},
    {255, 142, 180, 255},
    {144, 213, 255, 255},
  }};
  for (std::size_t index = 0; index < flowerPatches.size(); ++index) {
    const Vec3 patch = flowerPatches[index];
    if (
      patch.x < arena.min.x || patch.x > arena.max.x ||
      patch.y < arena.min.y || patch.y > arena.max.y
    ) {
      continue;
    }
    addFloorQuad(
      scene,
      patch.x - 0.18F,
      patch.y - 0.18F,
      patch.x + 0.18F,
      patch.y + 0.18F,
      arena.min.z + 0.004F,
      flowerColors[index % flowerColors.size()]
    );
  }

  for (float x = arena.min.x; x <= arena.max.x; x += 1.0F) {
    addSegment(
      scene,
      {x, arena.min.y, arena.min.z + gridZ},
      {x, arena.max.y, arena.min.z + gridZ},
      gridWidth,
      {109, 195, 105, 255}
    );
  }
  for (float y = arena.min.y; y <= arena.max.y; y += 1.0F) {
    addSegment(
      scene,
      {arena.min.x, y, arena.min.z + gridZ},
      {arena.max.x, y, arena.min.z + gridZ},
      gridWidth,
      {109, 195, 105, 255}
    );
  }

  addSegment(
    scene,
    {arena.min.x, 0.0F, arena.min.z + 0.018F},
    {arena.max.x, 0.0F, arena.min.z + 0.018F},
    0.04F,
    {236, 205, 126, 255}
  );
  addSegment(
    scene,
    {0.0F, arena.min.y, arena.min.z + 0.018F},
    {0.0F, arena.max.y, arena.min.z + 0.018F},
    0.04F,
    {236, 205, 126, 255}
  );
}

void addArenaBoundaryWalls(Scene3D& scene, const Arena& arena) {
  constexpr RenderColor nearWall = {68, 151, 218, 255};
  constexpr RenderColor farWall = {80, 170, 235, 255};
  constexpr RenderColor sideWall = {74, 161, 226, 255};
  constexpr RenderColor ceiling = {73, 158, 226, 255};
  addQuad(
    scene,
    {arena.min.x, arena.min.y, arena.min.z},
    {arena.max.x, arena.min.y, arena.min.z},
    {arena.max.x, arena.min.y, arena.max.z},
    {arena.min.x, arena.min.y, arena.max.z},
    nearWall
  );
  addQuad(
    scene,
    {arena.max.x, arena.max.y, arena.min.z},
    {arena.min.x, arena.max.y, arena.min.z},
    {arena.min.x, arena.max.y, arena.max.z},
    {arena.max.x, arena.max.y, arena.max.z},
    farWall
  );
  addQuad(
    scene,
    {arena.min.x, arena.max.y, arena.min.z},
    {arena.min.x, arena.min.y, arena.min.z},
    {arena.min.x, arena.min.y, arena.max.z},
    {arena.min.x, arena.max.y, arena.max.z},
    sideWall
  );
  addQuad(
    scene,
    {arena.max.x, arena.min.y, arena.min.z},
    {arena.max.x, arena.max.y, arena.min.z},
    {arena.max.x, arena.max.y, arena.max.z},
    {arena.max.x, arena.min.y, arena.max.z},
    sideWall
  );
  addQuad(
    scene,
    {arena.min.x, arena.min.y, arena.max.z},
    {arena.max.x, arena.min.y, arena.max.z},
    {arena.max.x, arena.max.y, arena.max.z},
    {arena.min.x, arena.max.y, arena.max.z},
    ceiling
  );
}

void addWallAccents(Scene3D& scene, const ArenaWall& wall) {
  const Vec3 topMin = {
    wall.min.x,
    wall.min.y,
    wall.max.z + 0.01F,
  };
  const Vec3 topMax = {
    wall.max.x,
    wall.max.y,
    wall.max.z + 0.08F,
  };
  addBox(scene, topMin, topMax, {86, 176, 96, 255});

  constexpr float bandWidth = 0.026F;
  constexpr RenderColor bandColor = {171, 235, 145, 255};
  const float lowerBandZ = wall.min.z + 0.32F;
  const float upperBandZ = std::max(wall.min.z + 0.34F, wall.max.z - 0.24F);
  const auto addPerimeterBand = [&](float z, RenderColor color) {
    addSegment(scene, {wall.min.x, wall.min.y, z}, {wall.max.x, wall.min.y, z}, bandWidth, color);
    addSegment(scene, {wall.max.x, wall.min.y, z}, {wall.max.x, wall.max.y, z}, bandWidth, color);
    addSegment(scene, {wall.max.x, wall.max.y, z}, {wall.min.x, wall.max.y, z}, bandWidth, color);
    addSegment(scene, {wall.min.x, wall.max.y, z}, {wall.min.x, wall.min.y, z}, bandWidth, color);
  };
  addPerimeterBand(lowerBandZ, scaleColor(bandColor, 0.75F));
  addPerimeterBand(upperBandZ, bandColor);
}

void addOrientedBox(
  Scene3D& scene,
  Vec3 center,
  Vec3 halfExtents,
  Vec3 forward,
  Vec3 right,
  Vec3 up,
  RenderColor color
) {
  const Vec3 forwardExtent = forward * halfExtents.x;
  const Vec3 rightExtent = right * halfExtents.y;
  const Vec3 upExtent = up * halfExtents.z;
  const std::array<Vec3, 8> corners = {{
    center - forwardExtent - rightExtent - upExtent,
    center + forwardExtent - rightExtent - upExtent,
    center + forwardExtent + rightExtent - upExtent,
    center - forwardExtent + rightExtent - upExtent,
    center - forwardExtent - rightExtent + upExtent,
    center + forwardExtent - rightExtent + upExtent,
    center + forwardExtent + rightExtent + upExtent,
    center - forwardExtent + rightExtent + upExtent,
  }};
  constexpr std::array<std::array<std::size_t, 4>, 6> faces = {{
    {{0, 3, 2, 1}},
    {{4, 5, 6, 7}},
    {{0, 1, 5, 4}},
    {{1, 2, 6, 5}},
    {{2, 3, 7, 6}},
    {{3, 0, 4, 7}},
  }};
  constexpr std::array<float, 6> brightness = {
    0.55F, 1.0F, 0.72F, 0.86F, 0.66F, 0.78F,
  };
  for (std::size_t index = 0; index < faces.size(); ++index) {
    const auto& face = faces[index];
    addQuad(
      scene,
      corners[face[0]],
      corners[face[1]],
      corners[face[2]],
      corners[face[3]],
      scaleColor(color, brightness[index])
    );
  }
}

void addPlayerModel(
  Scene3D& scene,
  const PlayerState& player,
  RenderColor color,
  bool leanEnabled,
  float leanScale
) {
  const float radius = player.bounds.radius;
  const float halfHeight = player.bounds.halfHeight;
  const float bottom = player.position.z - halfHeight;
  const float height = halfHeight * 2.0F;
  const Vec3 forward = yawForward(player.viewYawRadians);
  const Vec3 baseRight = yawRight(player.viewYawRadians);
  const float lateralVelocity = dot(player.velocity, baseRight);
  const float rollDegrees = leanEnabled
    ? -lateralVelocity * kQuakeUnitsPerProjectUnit * kQ3RunRoll * leanScale
    : 0.0F;
  const float rollRadians = rollDegrees * kDegreesToRadians;
  const float rollCos = std::cos(rollRadians);
  const float rollSin = std::sin(rollRadians);
  const Vec3 worldUp = {0.0F, 0.0F, 1.0F};
  const Vec3 right =
    normalize((baseRight * rollCos) + (worldUp * rollSin));
  const Vec3 up =
    normalize((worldUp * rollCos) - (baseRight * rollSin));
  const auto part =
    [&](float forwardOffset,
        float rightOffset,
        float bottomRatio,
        float topRatio,
        float forwardRadius,
        float rightRadius) {
      const float partBottom = bottom + height * bottomRatio;
      const float partTop = bottom + height * topRatio;
      addOrientedBox(
        scene,
        player.position +
          forward * (radius * forwardOffset) +
          right * (radius * rightOffset) +
          up * (((partBottom + partTop) * 0.5F) - player.position.z),
        {
          radius * forwardRadius,
          radius * rightRadius,
          (partTop - partBottom) * 0.5F,
        },
        forward,
        right,
        up,
        color
      );
    };

  part(0.0F, 0.0F, 0.43F, 0.76F, 0.34F, 0.58F);  // Torso
  part(0.0F, 0.0F, 0.34F, 0.48F, 0.31F, 0.48F);  // Hips
  part(0.0F, 0.0F, 0.78F, 1.0F, 0.34F, 0.36F);   // Head
  part(0.0F, -0.74F, 0.38F, 0.72F, 0.20F, 0.20F); // Left arm
  part(0.0F, 0.74F, 0.38F, 0.72F, 0.20F, 0.20F);  // Right arm
  part(0.0F, -0.25F, 0.0F, 0.36F, 0.25F, 0.20F);  // Left leg
  part(0.0F, 0.25F, 0.0F, 0.36F, 0.25F, 0.20F);   // Right leg
}

void addSegment(
  Scene3D& scene,
  Vec3 start,
  Vec3 end,
  float width,
  RenderColor color
) {
  const Vec3 direction = normalize(end - start);
  if (length(direction) <= 0.0001F) {
    return;
  }
  Vec3 side = normalize(cross(direction, {0.0F, 0.0F, 1.0F}));
  if (length(side) <= 0.0001F) {
    side = {1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(side, direction));
  const float halfWidth = width * 0.5F;
  side *= halfWidth;
  const Vec3 vertical = up * halfWidth;

  const std::array<Vec3, 4> startCorners = {{
    start + side + vertical,
    start - side + vertical,
    start - side - vertical,
    start + side - vertical,
  }};
  const std::array<Vec3, 4> endCorners = {{
    end + side + vertical,
    end - side + vertical,
    end - side - vertical,
    end + side - vertical,
  }};
  addQuad(
    scene,
    startCorners[0],
    endCorners[0],
    endCorners[1],
    startCorners[1],
    color
  );
  addQuad(
    scene,
    startCorners[1],
    endCorners[1],
    endCorners[2],
    startCorners[2],
    scaleColor(color, 0.8F)
  );
  addQuad(
    scene,
    startCorners[2],
    endCorners[2],
    endCorners[3],
    startCorners[3],
    scaleColor(color, 0.65F)
  );
  addQuad(
    scene,
    startCorners[3],
    endCorners[3],
    endCorners[0],
    startCorners[0],
    scaleColor(color, 0.9F)
  );
}

void addWireBox(
  Scene3D& scene,
  Vec3 minimum,
  Vec3 maximum,
  float width,
  RenderColor color
) {
  const std::array<Vec3, 8> corners = {{
    {minimum.x, minimum.y, minimum.z},
    {maximum.x, minimum.y, minimum.z},
    {maximum.x, maximum.y, minimum.z},
    {minimum.x, maximum.y, minimum.z},
    {minimum.x, minimum.y, maximum.z},
    {maximum.x, minimum.y, maximum.z},
    {maximum.x, maximum.y, maximum.z},
    {minimum.x, maximum.y, maximum.z},
  }};
  constexpr std::array<std::array<std::size_t, 2>, 12> edges = {{
    {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
    {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
    {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
  }};
  for (const auto& edge : edges) {
    addSegment(scene, corners[edge[0]], corners[edge[1]], width, color);
  }
}

} // namespace

Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const RenderSettings& settings
) {
  constexpr CollisionBounds defaultBounds = {};
  const float eyeHeight =
    0.65F * (player.bounds.halfHeight / defaultBounds.halfHeight);
  const Vec3 cameraPosition =
    player.position + Vec3{0.0F, 0.0F, eyeHeight};

  Scene3D scene;
  scene.camera = makePerspectiveCamera(
    cameraPosition,
    player.viewYawRadians,
    player.viewPitchRadians,
    settings.fieldOfView,
    aspectRatio
  );
  scene.vertices.reserve(4096);
  scene.translucentVertices.reserve(256);

  addFloorTreatment(scene, arena);
  addArenaBoundaryWalls(scene, arena);
  addWireBox(scene, arena.min, arena.max, 0.025F, {127, 202, 111, 255});

  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    addWallBox(scene, wall.min, wall.max);
    addWallAccents(scene, wall);
    addWireBox(scene, wall.min, wall.max, 0.018F, {127, 202, 111, 255});
    for (float z = wall.min.z + 1.0F; z < wall.max.z; z += 1.0F) {
      addSegment(
        scene,
        {wall.min.x, wall.min.y, z},
        {wall.max.x, wall.min.y, z},
        0.012F,
        {109, 195, 105, 255}
      );
      addSegment(
        scene,
        {wall.max.x, wall.max.y, z},
        {wall.min.x, wall.max.y, z},
        0.012F,
        {109, 195, 105, 255}
      );
    }
  }

  for (const RemotePlayerView& remote : remotePlayers) {
    if (!remote.visible) {
      continue;
    }
    const float hitAmount = remote.teammate
      ? 0.0F
      : std::clamp(remote.enemyHitAmount, 0.0F, 1.0F);
    const std::uint8_t red =
      remote.teammate ? settings.teammateRed : settings.enemyRed;
    const std::uint8_t green =
      remote.teammate ? settings.teammateGreen : settings.enemyGreen;
    const std::uint8_t blue =
      remote.teammate ? settings.teammateBlue : settings.enemyBlue;
    const RenderColor opponentColor = {
      blendChannel(
        red,
        settings.enemyHitRed,
        hitAmount
      ),
      blendChannel(
        green,
        settings.enemyHitGreen,
        hitAmount
      ),
      blendChannel(
        blue,
        settings.enemyHitBlue,
        hitAmount
      ),
      static_cast<std::uint8_t>(
        std::clamp(
          remote.teammate ? settings.teammateAlpha : settings.enemyAlpha,
          0.0F,
          1.0F
        ) * 255.0F
      ),
    };
    addPlayerModel(
      scene,
      remote.player,
      opponentColor,
      remote.teammate
        ? settings.teammateLeanEnabled
        : settings.enemyLeanEnabled,
      remote.teammate ? settings.teammateLeanScale : settings.enemyLeanScale
    );
  }

  if (settings.showLagCompensation && localLightningGun.hasRewindDebug) {
    const auto addBounds =
      [&](Vec3 position, CollisionBounds bounds, RenderColor color) {
        addWireBox(
          scene,
          {
            position.x - bounds.radius,
            position.y - bounds.radius,
            position.z - bounds.halfHeight,
          },
          {
            position.x + bounds.radius,
            position.y + bounds.radius,
            position.z + bounds.halfHeight,
          },
          0.02F,
          color
        );
      };
    addBounds(
      localLightningGun.currentTargetPosition,
      localLightningGun.currentTargetBounds,
      {64, 220, 255, 255}
    );
    addBounds(
      localLightningGun.rewoundTargetPosition,
      localLightningGun.rewoundTargetBounds,
      {255, 190, 64, 255}
    );
  }

  for (const RemotePlayerView& remote : remotePlayers) {
    if (!remote.visible || !remote.lightningGun.active) {
      continue;
    }
    const float pulse = std::clamp(settings.beamPulse, -1.0F, 1.0F);
    const float brightness = 1.0F + pulse * 0.05F;
    const float beamWidth = remote.teammate
      ? settings.teammateBeamWidth
      : settings.enemyBeamWidth;
    const float beamAlpha = remote.teammate
      ? settings.teammateBeamAlpha
      : settings.enemyBeamAlpha;
    addSegment(
      scene,
      remote.lightningGun.start,
      remote.lightningGun.end,
      std::max(0.015F, beamWidth * (1.0F + pulse * 0.04F) * 0.012F),
      scaleColor({
        remote.teammate ? settings.teammateBeamRed : settings.enemyBeamRed,
        remote.teammate ? settings.teammateBeamGreen : settings.enemyBeamGreen,
        remote.teammate ? settings.teammateBeamBlue : settings.enemyBeamBlue,
        static_cast<std::uint8_t>(
          std::clamp(beamAlpha, 0.0F, 1.0F) * 255.0F
        ),
      }, brightness)
    );
  }
  for (const WeaponFireResult& fire : weaponFires) {
    if (!fire.fired) {
      continue;
    }
    if (fire.weapon == Weapon::Railgun) {
      addSegment(
        scene,
        fire.start,
        fire.end,
        fire.hit ? 0.045F : 0.03F,
        fire.hit ? RenderColor{255, 248, 180, 255} : RenderColor{128, 230, 255, 235}
      );
    } else if (fire.weapon == Weapon::RocketLauncher) {
      addSegment(
        scene,
        fire.start,
        fire.end,
        0.04F,
        {255, 150, 70, 255}
      );
    }
  }
  for (const RocketProjectileSnapshot& rocket : rockets) {
    if (!rocket.active) {
      continue;
    }
    constexpr float size = 0.14F;
    addBox(
      scene,
      rocket.position - Vec3{size, size, size},
      rocket.position + Vec3{size, size, size},
      {255, 126, 40, 255}
    );
    addWireBox(
      scene,
      rocket.position - Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
      rocket.position + Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
      0.012F,
      {255, 220, 100, 255}
    );
  }
  for (const RocketExplosionResult& explosion : rocketExplosions) {
    if (!explosion.active) {
      continue;
    }
    const Vec3 radius{explosion.radius, explosion.radius, explosion.radius};
    addWireBox(
      scene,
      explosion.position - radius,
      explosion.position + radius,
      0.025F,
      {255, 185, 80, 220}
    );
  }
  (void)localLightningGun;

  return scene;
}

Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const PlayerState& opponent,
  const LightningGunResult& localLightningGun,
  const LightningGunResult& opponentLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const RenderSettings& settings
) {
  std::array<RemotePlayerView, kDuelPlayerCount> remotePlayers = {};
  remotePlayers[0] = RemotePlayerView{
    opponent,
    opponentLightningGun,
    settings.enemyHitAmount,
    1.0F,
    settings.hasRemotePlayer,
  };
  return buildPerspectiveScene(
    aspectRatio,
    arena,
    player,
    remotePlayers,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
}

} // namespace lg
