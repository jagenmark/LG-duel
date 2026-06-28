#include "render/Scene3D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace lg {
namespace {

constexpr float kQ3RunRoll = 0.005F;
constexpr float kQuakeUnitsPerProjectUnit = 40.0F;
constexpr float kDegreesToRadians = 0.01745329252F;
constexpr float kJumpPoseTorsoPitchRadians = 5.0F * kDegreesToRadians;
constexpr float kJumpPoseArmPitchRadians = 2.0F * kDegreesToRadians;
constexpr float kJumpPoseLegPitchRadians = -30.0F * kDegreesToRadians;

struct PlayerModelBasis {
  Vec3 forward = {};
  Vec3 right = {};
  Vec3 up = {};
  float radius = 0.0F;
  float halfHeight = 0.0F;
  float bottom = 0.0F;
  float height = 0.0F;
};

struct PlayerVisualPose {
  bool airborne = false;
};

struct WeaponModelFrame {
  PlayerModelBasis basis = {};
  Vec3 hand = {};
  float scale = 1.0F;
};

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

void addTexturedTriangle(
  Scene3D& scene,
  Vec3 first,
  Vec3 second,
  Vec3 third,
  std::array<std::array<float, 2>, 3> uv,
  RenderColor color,
  std::uint32_t materialId
) {
  std::vector<Vertex3D>& vertices =
    color.alpha == 255 ? scene.vertices : scene.translucentVertices;
  vertices.push_back({first, color, uv[0][0], uv[0][1], materialId});
  vertices.push_back({second, color, uv[1][0], uv[1][1], materialId});
  vertices.push_back({third, color, uv[2][0], uv[2][1], materialId});
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

void addTexturedQuad(
  Scene3D& scene,
  Vec3 first,
  Vec3 second,
  Vec3 third,
  Vec3 fourth,
  std::array<std::array<float, 2>, 4> uv,
  RenderColor color,
  std::uint32_t materialId
) {
  addTexturedTriangle(
    scene,
    first,
    second,
    third,
    {{{uv[0][0], uv[0][1]}, {uv[1][0], uv[1][1]}, {uv[2][0], uv[2][1]}}},
    color,
    materialId
  );
  addTexturedTriangle(
    scene,
    first,
    third,
    fourth,
    {{{uv[0][0], uv[0][1]}, {uv[2][0], uv[2][1]}, {uv[3][0], uv[3][1]}}},
    color,
    materialId
  );
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

void addSphereApprox(
  Scene3D& scene,
  Vec3 center,
  float radius,
  RenderColor color
) {
  constexpr std::size_t kSides = 14;
  constexpr float kTwoPi = 6.28318530718F;
  constexpr float kHalfPi = 1.57079632679F;
  constexpr std::size_t kBands = 6;
  for (std::size_t index = 0; index < kSides; ++index) {
    const float yaw =
      kTwoPi * static_cast<float>(index) / static_cast<float>(kSides);
    const float nextYaw =
      kTwoPi * static_cast<float>(index + 1) / static_cast<float>(kSides);
    for (std::size_t band = 0; band < kBands; ++band) {
      const float pitch =
        -kHalfPi + kHalfPi * 2.0F * static_cast<float>(band) / static_cast<float>(kBands);
      const float nextPitch =
        -kHalfPi + kHalfPi * 2.0F * static_cast<float>(band + 1) / static_cast<float>(kBands);
      const auto point = [center, radius](float yawAngle, float pitchAngle) {
        const float pitchCos = std::cos(pitchAngle);
        return Vec3{
          center.x + std::cos(yawAngle) * pitchCos * radius,
          center.y + std::sin(yawAngle) * pitchCos * radius,
          center.z + std::sin(pitchAngle) * radius,
        };
      };
      const float brightness = 0.55F + 0.35F * std::sin((pitch + kHalfPi) * 0.85F);
      addQuad(
        scene,
        point(yaw, pitch),
        point(nextYaw, pitch),
        point(nextYaw, nextPitch),
        point(yaw, nextPitch),
        scaleColor(color, brightness)
      );
    }
  }
}

[[nodiscard]] std::array<float, 2> faceUv(Vec3 point, int axis) {
  constexpr float kTextureWorldSize = 128.0F;
  switch (axis) {
  case 0:
    return {point.y / kTextureWorldSize, point.z / kTextureWorldSize};
  case 1:
    return {point.x / kTextureWorldSize, point.z / kTextureWorldSize};
  default:
    return {point.x / kTextureWorldSize, point.y / kTextureWorldSize};
  }
}

void addWallBox(Scene3D& scene, const ArenaWall& wall) {
  const Vec3 minimum = wall.min;
  const Vec3 maximum = wall.max;
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
    0.62F, 1.0F, 0.76F, 0.88F, 0.70F, 0.82F,
  };
  for (std::size_t index = 0; index < faces.size(); ++index) {
    const auto& face = faces[index];
    const int faceAxis = index < 2 ? 2 : index < 4 ? 1 : 0;
    const std::uint32_t materialId = index < wall.faceMaterialIds.size() &&
        wall.faceMaterialIds[index] != 0U
      ? wall.faceMaterialIds[index]
      : wall.materialId;
    addTexturedQuad(
      scene,
      corners[face[0]],
      corners[face[1]],
      corners[face[2]],
      corners[face[3]],
      {{
        faceUv(corners[face[0]], faceAxis),
        faceUv(corners[face[1]], faceAxis),
        faceUv(corners[face[2]], faceAxis),
        faceUv(corners[face[3]], faceAxis),
      }},
      scaleColor({255, 255, 255, 255}, brightness[index]),
      materialId
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

[[nodiscard]] float visualStepForRange(
  float minimum,
  float maximum,
  float baseStep,
  float maxDivisions
) {
  const float range = std::max(0.0F, maximum - minimum);
  if (range <= baseStep * maxDivisions) {
    return baseStep;
  }
  return std::ceil(range / maxDivisions);
}

void addFloorTreatment(Scene3D& scene, const Arena& arena) {
  constexpr float baseZ = 0.0F;
  constexpr float gridZ = 0.006F;
  constexpr float gridWidth = 0.012F;
  const float maxArenaRange = std::max(
    arena.max.x - arena.min.x,
    arena.max.y - arena.min.y
  );
  const float gridStep = visualStepForRange(0.0F, maxArenaRange, 1.0F, 96.0F);

  addFloorQuad(
    scene,
    arena.min.x,
    arena.min.y,
    arena.max.x,
    arena.max.y,
    baseZ,
    {42, 48, 55, 255}
  );

  for (float x = arena.min.x; x <= arena.max.x; x += gridStep) {
    addSegment(
      scene,
      {x, arena.min.y, arena.min.z + gridZ},
      {x, arena.max.y, arena.min.z + gridZ},
      gridWidth,
      {82, 94, 108, 255}
    );
  }
  for (float y = arena.min.y; y <= arena.max.y; y += gridStep) {
    addSegment(
      scene,
      {arena.min.x, y, arena.min.z + gridZ},
      {arena.max.x, y, arena.min.z + gridZ},
      gridWidth,
      {82, 94, 108, 255}
    );
  }

  addSegment(
    scene,
    {arena.min.x, 0.0F, arena.min.z + 0.018F},
    {arena.max.x, 0.0F, arena.min.z + 0.018F},
    0.04F,
    {120, 138, 156, 255}
  );
  addSegment(
    scene,
    {0.0F, arena.min.y, arena.min.z + 0.018F},
    {0.0F, arena.max.y, arena.min.z + 0.018F},
    0.04F,
    {120, 138, 156, 255}
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

void addOrientedWireBox(
  Scene3D& scene,
  Vec3 center,
  Vec3 halfExtents,
  Vec3 forward,
  Vec3 right,
  Vec3 up,
  float width,
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
  constexpr std::array<std::array<std::size_t, 2>, 12> edges = {{
    {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
    {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
    {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
  }};
  for (const auto& edge : edges) {
    addSegment(scene, corners[edge[0]], corners[edge[1]], width, color);
  }
}

[[nodiscard]] PlayerModelBasis playerModelBasis(
  const PlayerState& player,
  bool leanEnabled,
  float leanScale,
  float expansion
) {
  PlayerModelBasis basis;
  basis.radius = player.bounds.radius + std::max(0.0F, expansion);
  basis.halfHeight = player.bounds.halfHeight + std::max(0.0F, expansion);
  basis.bottom = player.position.z - basis.halfHeight;
  basis.height = basis.halfHeight * 2.0F;
  basis.forward = yawForward(player.viewYawRadians);
  const Vec3 baseRight = yawRight(player.viewYawRadians);
  const float lateralVelocity = dot(player.velocity, baseRight);
  const float rollDegrees = leanEnabled
    ? -lateralVelocity * kQuakeUnitsPerProjectUnit * kQ3RunRoll * leanScale
    : 0.0F;
  const float rollRadians = rollDegrees * kDegreesToRadians;
  const float rollCos = std::cos(rollRadians);
  const float rollSin = std::sin(rollRadians);
  const Vec3 worldUp = {0.0F, 0.0F, 1.0F};
  basis.right = normalize((baseRight * rollCos) + (worldUp * rollSin));
  basis.up = normalize((worldUp * rollCos) - (baseRight * rollSin));
  return basis;
}

[[nodiscard]] PlayerVisualPose makePlayerVisualPose(const PlayerState& player) {
  PlayerVisualPose pose;
  pose.airborne = !player.onGround && player.movementMode == MovementMode::Airborne;
  return pose;
}

[[nodiscard]] PlayerModelBasis pitchedBasis(
  PlayerModelBasis basis,
  float pitchRadians
) {
  const float pitchCos = std::cos(pitchRadians);
  const float pitchSin = std::sin(pitchRadians);
  const Vec3 forward = basis.forward;
  const Vec3 up = basis.up;
  basis.forward = normalize((forward * pitchCos) - (up * pitchSin));
  basis.up = normalize((up * pitchCos) + (forward * pitchSin));
  return basis;
}

template <typename AddPart>
void forEachPlayerModelPart(
  const PlayerState& player,
  bool leanEnabled,
  float leanScale,
  float expansion,
  AddPart addPart
) {
  const PlayerModelBasis basis =
    playerModelBasis(player, leanEnabled, leanScale, expansion);
  const PlayerVisualPose pose = makePlayerVisualPose(player);
  const auto part =
    [&](float forwardOffset,
        float rightOffset,
        float bottomRatio,
        float topRatio,
        float forwardRadius,
        float rightRadius,
        float pitchRadians = 0.0F) {
      const PlayerModelBasis partBasis = pitchRadians != 0.0F
        ? pitchedBasis(basis, pitchRadians)
        : basis;
      const float partBottom = basis.bottom + basis.height * bottomRatio;
      const float partTop = basis.bottom + basis.height * topRatio;
      addPart(
        player.position +
          basis.forward * (basis.radius * forwardOffset) +
          basis.right * (basis.radius * rightOffset) +
          basis.up * (((partBottom + partTop) * 0.5F) - player.position.z),
        {
          basis.radius * forwardRadius,
          basis.radius * rightRadius,
          (partTop - partBottom) * 0.5F,
        },
        partBasis.forward,
        partBasis.right,
        partBasis.up
      );
    };

  if (pose.airborne) {
    part(0.03F, 0.0F, 0.43F, 0.76F, 0.34F, 0.58F, kJumpPoseTorsoPitchRadians); // Torso
    part(-0.02F, 0.0F, 0.35F, 0.49F, 0.31F, 0.48F);                            // Hips
    part(0.0F, 0.0F, 0.78F, 1.0F, 0.34F, 0.36F);                                // Head
    part(0.02F, -0.74F, 0.41F, 0.72F, 0.20F, 0.20F, kJumpPoseArmPitchRadians);   // Left arm
    part(0.04F, 0.74F, 0.42F, 0.72F, 0.20F, 0.20F, kJumpPoseArmPitchRadians);    // Right arm
    part(-0.22F, -0.25F, 0.08F, 0.34F, 0.25F, 0.20F, kJumpPoseLegPitchRadians);  // Left leg
    part(-0.14F, 0.25F, 0.06F, 0.34F, 0.25F, 0.20F, kJumpPoseLegPitchRadians);   // Right leg
    return;
  }

  part(0.0F, 0.0F, 0.43F, 0.76F, 0.34F, 0.58F);  // Torso
  part(0.0F, 0.0F, 0.34F, 0.48F, 0.31F, 0.48F);  // Hips
  part(0.0F, 0.0F, 0.78F, 1.0F, 0.34F, 0.36F);   // Head
  part(0.0F, -0.74F, 0.38F, 0.72F, 0.20F, 0.20F); // Left arm
  part(0.0F, 0.74F, 0.38F, 0.72F, 0.20F, 0.20F);  // Right arm
  part(0.0F, -0.25F, 0.0F, 0.36F, 0.25F, 0.20F);  // Left leg
  part(0.0F, 0.25F, 0.0F, 0.36F, 0.25F, 0.20F);   // Right leg
}

void addPlayerModel(
  Scene3D& scene,
  const PlayerState& player,
  RenderColor color,
  bool leanEnabled,
  float leanScale
) {
  forEachPlayerModelPart(
    player,
    leanEnabled,
    leanScale,
    0.0F,
    [&](Vec3 center, Vec3 halfExtents, Vec3 forward, Vec3 right, Vec3 up) {
      addOrientedBox(scene, center, halfExtents, forward, right, up, color);
    }
  );
}

[[nodiscard]] WeaponModelFrame weaponModelFrame(
  const PlayerState& player,
  bool leanEnabled,
  float leanScale
) {
  constexpr CollisionBounds kDefaultBounds = {};
  WeaponModelFrame frame;
  frame.basis =
    playerModelBasis(player, leanEnabled, leanScale, 0.0F);
  frame.scale = std::clamp(
    (
      frame.basis.radius / kDefaultBounds.radius +
      frame.basis.halfHeight / kDefaultBounds.halfHeight
    ) * 0.5F,
    0.65F,
    1.8F
  );
  const PlayerVisualPose pose = makePlayerVisualPose(player);
  const float handForwardOffset = pose.airborne ? 0.22F : 0.18F;
  const float handRightOffset = 0.84F;
  const float handHeightRatio = pose.airborne ? 0.56F : 0.53F;
  frame.hand =
    player.position +
    frame.basis.forward * (frame.basis.radius * handForwardOffset) +
    frame.basis.right * (frame.basis.radius * handRightOffset) +
    frame.basis.up *
      ((frame.basis.bottom + frame.basis.height * handHeightRatio) -
        player.position.z);
  return frame;
}

[[nodiscard]] Vec3 weaponLocalPoint(
  const WeaponModelFrame& frame,
  float forward,
  float right,
  float up
) {
  return frame.hand +
    frame.basis.forward * (forward * frame.scale) +
    frame.basis.right * (right * frame.scale) +
    frame.basis.up * (up * frame.scale);
}

void addWeaponPart(
  Scene3D& scene,
  const WeaponModelFrame& frame,
  float forward,
  float right,
  float up,
  Vec3 halfExtents,
  RenderColor color
) {
  addOrientedBox(
    scene,
    weaponLocalPoint(frame, forward, right, up),
    halfExtents * frame.scale,
    frame.basis.forward,
    frame.basis.right,
    frame.basis.up,
    color
  );
}

void addWeaponStrut(
  Scene3D& scene,
  const WeaponModelFrame& frame,
  Vec3 start,
  Vec3 end,
  float width,
  RenderColor color
) {
  addSegment(
    scene,
    weaponLocalPoint(frame, start.x, start.y, start.z),
    weaponLocalPoint(frame, end.x, end.y, end.z),
    width * frame.scale,
    color
  );
}

void addLightningGunModel(Scene3D& scene, const WeaponModelFrame& frame) {
  const auto part =
    [&](float forward,
        float right,
        float up,
        Vec3 halfExtents,
        RenderColor color) {
      addWeaponPart(scene, frame, forward, right, up, halfExtents, color);
    };

  constexpr RenderColor darkMetal = {24, 31, 40, 255};
  constexpr RenderColor blueMetal = {38, 70, 92, 255};
  constexpr RenderColor energy = {54, 224, 255, 255};
  constexpr RenderColor hotEnergy = {178, 246, 255, 255};

  part(0.16F, 0.0F, 0.09F, {0.27F, 0.075F, 0.07F}, darkMetal);
  part(0.05F, 0.0F, 0.10F, {0.14F, 0.12F, 0.105F}, blueMetal);
  part(0.46F, 0.0F, 0.09F, {0.22F, 0.04F, 0.04F}, darkMetal);
  part(0.71F, 0.0F, 0.09F, {0.10F, 0.075F, 0.075F}, blueMetal);
  part(0.82F, 0.0F, 0.09F, {0.045F, 0.105F, 0.105F}, energy);
  part(0.18F, 0.0F, -0.08F, {0.06F, 0.045F, 0.16F}, darkMetal);
  part(0.24F, 0.0F, -0.22F, {0.05F, 0.04F, 0.08F}, darkMetal);
  part(0.05F, -0.095F, 0.205F, {0.13F, 0.018F, 0.02F}, energy);
  part(0.05F, 0.095F, 0.205F, {0.13F, 0.018F, 0.02F}, energy);
  part(0.35F, 0.0F, 0.155F, {0.20F, 0.02F, 0.018F}, hotEnergy);
  addWeaponStrut(scene, frame, {0.29F, -0.075F, 0.155F}, {0.58F, 0.075F, 0.155F}, 0.018F, energy);
}

void addMachineGunModel(Scene3D& scene, const WeaponModelFrame& frame) {
  constexpr RenderColor gunmetal = {32, 35, 38, 255};
  constexpr RenderColor steel = {82, 91, 98, 255};
  constexpr RenderColor belt = {208, 171, 82, 255};
  constexpr RenderColor muzzle = {168, 180, 188, 255};

  addWeaponPart(scene, frame, 0.14F, 0.0F, 0.07F, {0.20F, 0.08F, 0.07F}, gunmetal);
  addWeaponPart(scene, frame, 0.41F, -0.035F, 0.09F, {0.22F, 0.025F, 0.025F}, steel);
  addWeaponPart(scene, frame, 0.41F, 0.035F, 0.09F, {0.22F, 0.025F, 0.025F}, steel);
  addWeaponPart(scene, frame, 0.64F, 0.0F, 0.09F, {0.045F, 0.065F, 0.055F}, muzzle);
  addWeaponPart(scene, frame, 0.02F, 0.13F, 0.02F, {0.12F, 0.035F, 0.09F}, belt);
  addWeaponPart(scene, frame, 0.12F, 0.0F, -0.09F, {0.055F, 0.045F, 0.14F}, gunmetal);
}

void addShotgunModel(Scene3D& scene, const WeaponModelFrame& frame) {
  constexpr RenderColor darkSteel = {38, 34, 31, 255};
  constexpr RenderColor warmGrip = {104, 67, 42, 255};
  constexpr RenderColor brass = {196, 154, 74, 255};

  addWeaponPart(scene, frame, 0.12F, 0.0F, 0.07F, {0.20F, 0.09F, 0.075F}, warmGrip);
  addWeaponPart(scene, frame, 0.43F, -0.04F, 0.105F, {0.27F, 0.026F, 0.028F}, darkSteel);
  addWeaponPart(scene, frame, 0.43F, 0.04F, 0.105F, {0.27F, 0.026F, 0.028F}, darkSteel);
  addWeaponPart(scene, frame, 0.72F, -0.04F, 0.105F, {0.035F, 0.04F, 0.04F}, brass);
  addWeaponPart(scene, frame, 0.72F, 0.04F, 0.105F, {0.035F, 0.04F, 0.04F}, brass);
  addWeaponPart(scene, frame, 0.08F, 0.0F, -0.08F, {0.055F, 0.045F, 0.14F}, darkSteel);
}

void addGrenadeLauncherModel(Scene3D& scene, const WeaponModelFrame& frame) {
  constexpr RenderColor olive = {35, 69, 44, 255};
  constexpr RenderColor dark = {24, 30, 27, 255};
  constexpr RenderColor hazard = {220, 176, 74, 255};
  constexpr RenderColor muzzle = {68, 82, 74, 255};

  addWeaponPart(scene, frame, 0.10F, 0.0F, 0.08F, {0.18F, 0.105F, 0.095F}, olive);
  addWeaponPart(scene, frame, 0.30F, 0.0F, 0.08F, {0.12F, 0.145F, 0.145F}, dark);
  addWeaponPart(scene, frame, 0.30F, 0.0F, 0.225F, {0.08F, 0.09F, 0.025F}, hazard);
  addWeaponPart(scene, frame, 0.55F, 0.0F, 0.09F, {0.20F, 0.07F, 0.07F}, muzzle);
  addWeaponPart(scene, frame, 0.76F, 0.0F, 0.09F, {0.045F, 0.095F, 0.095F}, hazard);
  addWeaponPart(scene, frame, 0.12F, 0.0F, -0.09F, {0.055F, 0.045F, 0.14F}, dark);
}

void addRocketLauncherModel(Scene3D& scene, const WeaponModelFrame& frame) {
  constexpr RenderColor tube = {42, 49, 56, 255};
  constexpr RenderColor dark = {20, 24, 28, 255};
  constexpr RenderColor warning = {236, 122, 48, 255};
  constexpr RenderColor rim = {122, 137, 145, 255};

  addWeaponPart(scene, frame, 0.24F, 0.0F, 0.12F, {0.42F, 0.115F, 0.115F}, tube);
  addWeaponPart(scene, frame, -0.10F, 0.0F, 0.12F, {0.075F, 0.15F, 0.15F}, dark);
  addWeaponPart(scene, frame, 0.68F, 0.0F, 0.12F, {0.075F, 0.155F, 0.155F}, rim);
  addWeaponPart(scene, frame, 0.81F, 0.0F, 0.12F, {0.055F, 0.075F, 0.075F}, warning);
  addWeaponPart(scene, frame, 0.12F, 0.0F, -0.08F, {0.055F, 0.045F, 0.15F}, dark);
  addWeaponPart(scene, frame, 0.32F, -0.13F, 0.19F, {0.14F, 0.02F, 0.025F}, warning);
}

void addRailgunModel(Scene3D& scene, const WeaponModelFrame& frame) {
  constexpr RenderColor dark = {28, 26, 40, 255};
  constexpr RenderColor violet = {83, 69, 128, 255};
  constexpr RenderColor rail = {118, 229, 255, 255};
  constexpr RenderColor core = {255, 224, 118, 255};

  addWeaponPart(scene, frame, 0.14F, 0.0F, 0.09F, {0.24F, 0.075F, 0.07F}, dark);
  addWeaponPart(scene, frame, 0.48F, 0.0F, 0.09F, {0.30F, 0.045F, 0.045F}, violet);
  addWeaponPart(scene, frame, 0.78F, 0.0F, 0.09F, {0.045F, 0.075F, 0.075F}, core);
  addWeaponPart(scene, frame, 0.43F, -0.09F, 0.17F, {0.28F, 0.015F, 0.018F}, rail);
  addWeaponPart(scene, frame, 0.43F, 0.09F, 0.17F, {0.28F, 0.015F, 0.018F}, rail);
  addWeaponPart(scene, frame, 0.12F, 0.0F, -0.09F, {0.055F, 0.04F, 0.145F}, dark);
  addWeaponStrut(scene, frame, {0.22F, -0.08F, 0.02F}, {0.66F, 0.08F, 0.02F}, 0.016F, rail);
}

void addPlasmaGunModel(Scene3D& scene, const WeaponModelFrame& frame) {
  constexpr RenderColor dark = {26, 35, 39, 255};
  constexpr RenderColor teal = {43, 107, 103, 255};
  constexpr RenderColor plasma = {112, 255, 142, 255};
  constexpr RenderColor glow = {199, 255, 214, 255};

  addWeaponPart(scene, frame, 0.12F, 0.0F, 0.08F, {0.20F, 0.10F, 0.085F}, dark);
  addWeaponPart(scene, frame, 0.35F, 0.0F, 0.09F, {0.16F, 0.135F, 0.12F}, teal);
  addSphereApprox(scene, weaponLocalPoint(frame, 0.35F, 0.0F, 0.095F), 0.085F * frame.scale, plasma);
  addWeaponPart(scene, frame, 0.57F, 0.0F, 0.09F, {0.16F, 0.055F, 0.055F}, dark);
  addWeaponPart(scene, frame, 0.74F, 0.0F, 0.09F, {0.045F, 0.09F, 0.09F}, glow);
  addWeaponPart(scene, frame, 0.13F, 0.0F, -0.09F, {0.055F, 0.045F, 0.145F}, dark);
  addWeaponStrut(scene, frame, {0.20F, -0.11F, 0.17F}, {0.54F, -0.11F, 0.17F}, 0.018F, plasma);
  addWeaponStrut(scene, frame, {0.20F, 0.11F, 0.17F}, {0.54F, 0.11F, 0.17F}, 0.018F, plasma);
}

void addWeaponModel(
  Scene3D& scene,
  const PlayerState& player,
  Weapon weapon,
  RenderColor,
  bool leanEnabled,
  float leanScale
) {
  const WeaponModelFrame frame = weaponModelFrame(player, leanEnabled, leanScale);
  switch (weapon) {
  case Weapon::LightningGun:
    addLightningGunModel(scene, frame);
    break;
  case Weapon::Railgun:
    addRailgunModel(scene, frame);
    break;
  case Weapon::RocketLauncher:
    addRocketLauncherModel(scene, frame);
    break;
  case Weapon::MachineGun:
    addMachineGunModel(scene, frame);
    break;
  case Weapon::Shotgun:
    addShotgunModel(scene, frame);
    break;
  case Weapon::GrenadeLauncher:
    addGrenadeLauncherModel(scene, frame);
    break;
  case Weapon::PlasmaGun:
    addPlasmaGunModel(scene, frame);
    break;
  }
}

void addPlayerOutline(
  Scene3D& scene,
  const PlayerState& player,
  RenderColor color,
  bool leanEnabled,
  float leanScale,
  float outlineWidth
) {
  const float expansion = std::max(0.0F, outlineWidth);
  const float lineWidth = std::max(0.008F, expansion * 0.45F);
  forEachPlayerModelPart(
    player,
    leanEnabled,
    leanScale,
    expansion,
    [&](Vec3 center, Vec3 halfExtents, Vec3 forward, Vec3 right, Vec3 up) {
      addOrientedWireBox(
        scene,
        center,
        halfExtents,
        forward,
        right,
        up,
        lineWidth,
        color
      );
    }
  );
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
    addWallBox(scene, wall);
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
    const bool outlineEnabled = remote.teammate
      ? settings.teammateOutlineEnabled
      : settings.enemyOutlineEnabled;
    const float outlineWidth = remote.teammate
      ? settings.teammateOutlineWidth
      : settings.enemyOutlineWidth;
    const float outlineAlpha = remote.teammate
      ? settings.teammateOutlineAlpha
      : settings.enemyOutlineAlpha;
    if (
      outlineEnabled &&
      usesGeometryPlayerOutlineFallback(settings.playerOutlineStyle) &&
      outlineWidth > 0.0F &&
      outlineAlpha > 0.0F
    ) {
      addPlayerOutline(
        scene,
        remote.player,
        {
          remote.teammate
            ? settings.teammateOutlineRed
            : settings.enemyOutlineRed,
          remote.teammate
            ? settings.teammateOutlineGreen
            : settings.enemyOutlineGreen,
          remote.teammate
            ? settings.teammateOutlineBlue
            : settings.enemyOutlineBlue,
          static_cast<std::uint8_t>(
            std::clamp(outlineAlpha, 0.0F, 1.0F) * 255.0F
          ),
        },
        remote.teammate
          ? settings.teammateLeanEnabled
          : settings.enemyLeanEnabled,
        remote.teammate ? settings.teammateLeanScale : settings.enemyLeanScale,
        outlineWidth
      );
    }
    addPlayerModel(
      scene,
      remote.player,
      opponentColor,
      remote.teammate
        ? settings.teammateLeanEnabled
        : settings.enemyLeanEnabled,
      remote.teammate ? settings.teammateLeanScale : settings.enemyLeanScale
    );
    addWeaponModel(
      scene,
      remote.player,
      remote.selectedWeapon,
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
  for (const RocketProjectileSnapshot& projectile : rockets) {
    if (!projectile.active) {
      continue;
    }
    const float size = projectile.radius > 0.0F ? projectile.radius : 0.14F;
    if (projectile.weapon == Weapon::GrenadeLauncher) {
      addSphereApprox(scene, projectile.position, size, {8, 48, 18, 255});
      addWireBox(
        scene,
        projectile.position - Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
        projectile.position + Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
        0.012F,
        {255, 220, 100, 255}
      );
      continue;
    }
    addBox(
      scene,
      projectile.position - Vec3{size, size, size},
      projectile.position + Vec3{size, size, size},
      {255, 126, 40, 255}
    );
    addWireBox(
      scene,
      projectile.position - Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
      projectile.position + Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
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
    Weapon::LightningGun,
    settings.enemyHitAmount,
    1.0F,
    settings.hasRemotePlayer,
    false,
    {},
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
