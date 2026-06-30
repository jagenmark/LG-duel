#include "render/Scene3D.hpp"
#include "render/BakedWeaponModels.hpp"
#include "render/GltfSkinnedModel.hpp"

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
constexpr float kTwoPi = 6.28318530718F;
constexpr float kQuarterTurnRadians = 1.57079632679F;
constexpr float kDuelistMaleHeight = 1.67400002F;
constexpr float kDuelistMaleHalfWidth = 0.42503331F;
constexpr float kDuelistMaleDepthCenter = 0.07100000F;

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

[[nodiscard]] RenderColor blendColor(
  RenderColor base,
  RenderColor tint,
  float tintAmount
) {
  const auto blend = [tintAmount](
                       std::uint8_t baseChannel,
                       std::uint8_t tintChannel
                     ) {
    return static_cast<std::uint8_t>(std::clamp(
      static_cast<float>(baseChannel) * (1.0F - tintAmount) +
        static_cast<float>(tintChannel) * tintAmount,
      0.0F,
      255.0F
    ));
  };
  return {
    blend(base.red, tint.red),
    blend(base.green, tint.green),
    blend(base.blue, tint.blue),
    tint.alpha,
  };
}

void addSegment(
  Scene3D& scene,
  Vec3 start,
  Vec3 end,
  float width,
  RenderColor color
);

[[nodiscard]] Vec3 cameraUp(float yawRadians, float pitchRadians);

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
  constexpr float kQuakeUnitsPerLgUnit = 40.0F;
  constexpr float kTextureQuakeUnits = 512.0F;
  constexpr float kTextureWorldSize = kTextureQuakeUnits / kQuakeUnitsPerLgUnit;
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

[[nodiscard]] float faceBrightness(Vec3 normal) {
  return std::clamp(
    0.68F + normal.z * 0.28F + std::fabs(normal.x) * 0.08F,
    0.48F,
    1.0F
  );
}

void addArenaBrush(Scene3D& scene, const ArenaBrush& brush) {
  for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
    const ArenaBrushFace& face = brush.faces[faceIndex];
    if (face.vertexCount < 3) {
      continue;
    }
    const std::uint32_t materialId = face.materialId != 0U ? face.materialId : brush.materialId;
    const RenderColor color = scaleColor({255, 255, 255, 255}, faceBrightness(face.normal));
    const int uvAxis =
      std::fabs(face.normal.z) >= std::fabs(face.normal.x) &&
        std::fabs(face.normal.z) >= std::fabs(face.normal.y)
      ? 2
      : std::fabs(face.normal.y) >= std::fabs(face.normal.x) ? 1 : 0;
    const Vec3 origin = brush.vertices[face.vertices[0]];
    for (std::uint8_t vertex = 1; vertex + 1 < face.vertexCount; ++vertex) {
      const Vec3 second = brush.vertices[face.vertices[vertex]];
      const Vec3 third = brush.vertices[face.vertices[vertex + 1U]];
      addTexturedTriangle(
        scene,
        origin,
        second,
        third,
        {{
          faceUv(origin, uvAxis),
          faceUv(second, uvAxis),
          faceUv(third, uvAxis),
        }},
        color,
        materialId
      );
    }
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

[[nodiscard]] Vec3 perpendicularRight(Vec3 forward) {
  Vec3 right = normalize(cross(forward, Vec3{0.0F, 0.0F, 1.0F}));
  if (length(right) <= 0.0001F) {
    right = {1.0F, 0.0F, 0.0F};
  }
  return right;
}

[[nodiscard]] Vec3 shotgunPelletVisualDirection(
  Vec3 forward,
  std::uint8_t pelletIndex,
  std::uint8_t pelletCount
) {
  if (pelletIndex == 0 || pelletCount <= 1U) {
    return forward;
  }
  constexpr float kGoldenAngle = 2.39996323F;
  constexpr float kVisualSpread = 0.13F;
  const Vec3 right = perpendicularRight(forward);
  const Vec3 up = normalize(cross(right, forward));
  const float normalizedRadius = std::sqrt(
    static_cast<float>(pelletIndex) /
    static_cast<float>(std::max<std::uint8_t>(1U, pelletCount - 1U))
  );
  const float angle = static_cast<float>(pelletIndex) * kGoldenAngle;
  return normalize(
    forward +
    (right * (std::cos(angle) * kVisualSpread * normalizedRadius)) +
    (up * (std::sin(angle) * kVisualSpread * normalizedRadius))
  );
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
  const PlayerModelBasis basis =
    playerModelBasis(player, false, leanScale, 0.0F);
  const PlayerVisualPose pose = makePlayerVisualPose(player);
  const float verticalScale = basis.height / kDuelistMaleHeight;
  const float horizontalScale = basis.radius / kDuelistMaleHalfWidth * 0.94F;
  const Vec3 base = player.position - basis.up * basis.halfHeight;
  const Vec3 lightDirection = normalize(Vec3{-0.35F, -0.45F, 0.82F});
  const float lateralVelocity = dot(player.velocity, yawRight(player.viewYawRadians));
  const float leanAmount = leanEnabled
    ? std::clamp(lateralVelocity / 8.0F * leanScale, -1.0F, 1.0F)
    : 0.0F;
  std::vector<SkinnedModelPoseRequest> poseRequests;
  if (pose.airborne) {
    const float jumpProgress = std::clamp((8.0F - player.velocity.z) / 16.0F, 0.0F, 1.0F);
    const float jumpTime = 0.3333333F + jumpProgress * 0.6666667F;
    poseRequests.push_back({"lg_duelist_jump", jumpTime, 1.0F});
  } else if (leanAmount > 0.02F) {
    poseRequests.push_back({"lg_duelist_lean_right", 0.5833333F, std::fabs(leanAmount)});
  } else if (leanAmount < -0.02F) {
    poseRequests.push_back({"lg_duelist_lean_left", 0.5833333F, std::fabs(leanAmount)});
  }

  const auto point = [&](Vec3 local) {
    return base +
      basis.forward * ((local.z - kDuelistMaleDepthCenter) * horizontalScale) +
      basis.right * (local.x * horizontalScale) +
      basis.up * (local.y * verticalScale);
  };

  const GltfSkinnedModel& model = duelistMaleModel();
  if (!model.loaded()) {
    forEachPlayerModelPart(
      player,
      leanEnabled,
      leanScale,
      0.0F,
      [&](Vec3 center, Vec3 halfExtents, Vec3 forward, Vec3 right, Vec3 up) {
        addOrientedBox(scene, center, halfExtents, forward, right, up, color);
      }
    );
    return;
  }

  for (const SkinnedModelTriangle& triangle : model.triangles(poseRequests)) {
    const Vec3 first = point(triangle.vertices[0]);
    const Vec3 second = point(triangle.vertices[1]);
    const Vec3 third = point(triangle.vertices[2]);
    const Vec3 normal = normalize(cross(second - first, third - first));
    const float brightness = std::clamp(
      0.70F +
        std::max(0.0F, dot(normal, lightDirection)) * 0.38F +
        std::fabs(normal.z) * 0.12F,
      0.62F,
      1.22F
    );
    RenderColor shaded = scaleColor(triangle.color, brightness);
    if (triangle.tintable) {
      shaded = blendColor(shaded, color, 0.70F);
    }
    shaded.alpha = triangle.tintable ? color.alpha : shaded.alpha;
    addTriangle(scene, first, second, third, shaded);
  }
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

template <std::size_t Count>
void addBakedWeaponModel(
  Scene3D& scene,
  const WeaponModelFrame& frame,
  const std::array<BakedWeaponModelTriangle, Count>& model,
  float modelScale,
  float rollRadians = 0.0F
) {
  const float rollCos = std::cos(rollRadians);
  const float rollSin = std::sin(rollRadians);
  const auto point = [&](Vec3 local) {
    local = {
      local.x,
      local.y * rollCos - local.z * rollSin,
      local.y * rollSin + local.z * rollCos,
    };
    return frame.hand +
      frame.basis.forward * (local.x * frame.scale * modelScale) +
      frame.basis.right * (local.y * frame.scale * modelScale) +
      frame.basis.up * (local.z * frame.scale * modelScale);
  };
  for (const BakedWeaponModelTriangle& triangle : model) {
    const Vec3 first = point(triangle.vertices[0]);
    const Vec3 second = point(triangle.vertices[1]);
    const Vec3 third = point(triangle.vertices[2]);
    const Vec3 normal = normalize(cross(second - first, third - first));
    const Vec3 lightDirection = normalize(Vec3{-0.35F, -0.45F, 0.82F});
    const float brightness = std::clamp(
      0.70F +
        std::max(0.0F, dot(normal, lightDirection)) * 0.42F +
        std::fabs(normal.z) * 0.16F,
      0.58F,
      1.30F
    );
    RenderColor color = scaleColor(triangle.color, brightness);
    color.alpha = 255;
    addTriangle(scene, first, second, third, color);
  }
}

[[nodiscard]] WeaponModelFrame firstPersonWeaponModelFrame(
  const PlayerState& player
) {
  WeaponModelFrame frame;
  frame.basis.forward =
    cameraForward(player.viewYawRadians, player.viewPitchRadians);
  frame.basis.right = yawRight(player.viewYawRadians);
  frame.basis.up = cameraUp(player.viewYawRadians, player.viewPitchRadians);
  constexpr CollisionBounds defaultBounds = {};
  const float eyeHeight =
    0.65F * (player.bounds.halfHeight / defaultBounds.halfHeight);
  const Vec3 eyePosition = player.position + Vec3{0.0F, 0.0F, eyeHeight};
  frame.hand =
    eyePosition +
    frame.basis.forward * 0.32F -
    frame.basis.up * 0.38F;
  frame.scale = 0.50F;
  return frame;
}

void addFirstPersonWeaponModel(
  Scene3D& scene,
  const PlayerState& player,
  Weapon weapon
) {
  const WeaponModelFrame frame = firstPersonWeaponModelFrame(player);
  switch (weapon) {
  case Weapon::MachineGun:
    addBakedWeaponModel(scene, frame, kMachineGunWeaponModel, 1.0F);
    break;
  case Weapon::Shotgun:
    addBakedWeaponModel(
      scene,
      frame,
      kShotgunWeaponModel,
      1.0F,
      kQuarterTurnRadians
    );
    break;
  default:
    break;
  }
}

void addLightningGunModel(Scene3D& scene, const WeaponModelFrame& frame) {
  constexpr RenderColor bodyDark = {14, 20, 29, 255};
  constexpr RenderColor panelTeal = {34, 76, 91, 255};
  constexpr RenderColor energyCyan = {31, 217, 244, 255};
  constexpr RenderColor energyHot = {202, 250, 255, 255};

  // Rear body: squat, heavy receiver anchored around the existing hand frame.
  addWeaponPart(scene, frame, 0.06F, 0.0F, 0.095F, {0.17F, 0.14F, 0.125F}, bodyDark);
  addWeaponPart(scene, frame, -0.08F, -0.055F, 0.115F, {0.075F, 0.105F, 0.09F}, bodyDark);
  addWeaponPart(scene, frame, 0.09F, 0.125F, 0.13F, {0.12F, 0.035F, 0.075F}, panelTeal);

  // Central chamber: raised teal casing with a small hot exposed core.
  addWeaponPart(scene, frame, 0.26F, 0.0F, 0.12F, {0.16F, 0.105F, 0.095F}, panelTeal);
  addWeaponPart(scene, frame, 0.28F, 0.0F, 0.205F, {0.105F, 0.055F, 0.026F}, energyCyan);
  addWeaponPart(scene, frame, 0.30F, 0.0F, 0.235F, {0.048F, 0.032F, 0.018F}, energyHot);

  // Forward body and barrel: long, square, clearly not a normal rifle tube.
  addWeaponPart(scene, frame, 0.50F, 0.0F, 0.105F, {0.22F, 0.06F, 0.058F}, bodyDark);
  addWeaponPart(scene, frame, 0.67F, 0.0F, 0.105F, {0.13F, 0.045F, 0.045F}, panelTeal);

  // Side energy rails: offset cyan forks for a readable lightning silhouette.
  addWeaponPart(scene, frame, 0.50F, -0.125F, 0.15F, {0.255F, 0.026F, 0.030F}, energyCyan);
  addWeaponPart(scene, frame, 0.50F, 0.125F, 0.15F, {0.255F, 0.026F, 0.030F}, energyCyan);
  addWeaponPart(scene, frame, 0.72F, -0.128F, 0.15F, {0.072F, 0.038F, 0.042F}, energyHot);
  addWeaponPart(scene, frame, 0.72F, 0.128F, 0.15F, {0.072F, 0.038F, 0.042F}, energyHot);

  // Forked emitter: two blunt prongs with an open lightning channel between.
  addWeaponPart(scene, frame, 0.80F, 0.0F, 0.105F, {0.040F, 0.120F, 0.100F}, bodyDark);
  addWeaponPart(scene, frame, 0.92F, -0.105F, 0.105F, {0.140F, 0.042F, 0.085F}, bodyDark);
  addWeaponPart(scene, frame, 0.92F, 0.105F, 0.105F, {0.140F, 0.042F, 0.085F}, bodyDark);
  addWeaponPart(scene, frame, 0.95F, -0.062F, 0.105F, {0.105F, 0.012F, 0.052F}, energyCyan);
  addWeaponPart(scene, frame, 0.95F, 0.062F, 0.105F, {0.105F, 0.012F, 0.052F}, energyCyan);
  addWeaponPart(scene, frame, 1.00F, 0.0F, 0.105F, {0.032F, 0.026F, 0.050F}, energyHot);

  // Grip and a couple of chunky accents to keep the PSX silhouette brutal.
  addWeaponPart(scene, frame, 0.12F, 0.0F, -0.095F, {0.065F, 0.050F, 0.165F}, bodyDark);
  addWeaponPart(scene, frame, 0.17F, 0.0F, -0.25F, {0.055F, 0.045F, 0.080F}, panelTeal);
  addWeaponPart(scene, frame, 0.32F, 0.0F, 0.275F, {0.24F, 0.028F, 0.025F}, bodyDark);
  addWeaponStrut(scene, frame, {0.25F, -0.105F, 0.02F}, {0.61F, -0.145F, 0.145F}, 0.018F, energyCyan);
  addWeaponStrut(scene, frame, {0.25F, 0.105F, 0.02F}, {0.61F, 0.145F, 0.145F}, 0.018F, energyCyan);
}

void addMachineGunModel(Scene3D& scene, const WeaponModelFrame& frame) {
  addBakedWeaponModel(scene, frame, kMachineGunWeaponModel, 0.78F);
}

void addShotgunModel(Scene3D& scene, const WeaponModelFrame& frame) {
  addBakedWeaponModel(
    scene,
    frame,
    kShotgunWeaponModel,
    0.78F,
    kQuarterTurnRadians
  );
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

[[nodiscard]] float thirdPersonWeaponVisualScale(Weapon weapon) {
  switch (weapon) {
  case Weapon::LightningGun:
    return 0.55F;
  case Weapon::RocketLauncher:
  case Weapon::GrenadeLauncher:
    return 0.68F;
  default:
    return 0.65F;
  }
}

void addWeaponModel(
  Scene3D& scene,
  const PlayerState& player,
  Weapon weapon,
  RenderColor,
  bool leanEnabled,
  float leanScale
) {
  WeaponModelFrame frame = weaponModelFrame(player, leanEnabled, leanScale);
  frame.scale *= thirdPersonWeaponVisualScale(weapon);
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

[[nodiscard]] float distanceSquared(Vec3 lhs, Vec3 rhs) {
  const Vec3 delta = lhs - rhs;
  return dot(delta, delta);
}

[[nodiscard]] Vec3 playerEyePosition(const PlayerState& player) {
  constexpr CollisionBounds defaultBounds = {};
  return player.position +
    Vec3{
      0.0F,
      0.0F,
      0.65F * (player.bounds.halfHeight / defaultBounds.halfHeight)
    };
}

[[nodiscard]] Vec3 cameraUp(float yawRadians, float pitchRadians) {
  return {
    -std::cos(yawRadians) * std::sin(pitchRadians),
    -std::sin(yawRadians) * std::sin(pitchRadians),
    std::cos(pitchRadians),
  };
}

[[nodiscard]] Vec3 firstPersonWeaponMuzzlePosition(const PlayerState& player) {
  return playerEyePosition(player) +
    cameraForward(player.viewYawRadians, player.viewPitchRadians) * 0.55F -
    cameraUp(player.viewYawRadians, player.viewPitchRadians) * 0.32F;
}

[[nodiscard]] Vec3 remoteWeaponModelPoint(
  Vec3 fallback,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings,
  float forward,
  float right,
  float up
) {
  if (playerIndex == static_cast<std::size_t>(settings.localPlayerIndex)) {
    return firstPersonWeaponMuzzlePosition(localPlayer);
  }
  if (playerIndex < remotePlayers.size() && remotePlayers[playerIndex].visible) {
    const RemotePlayerView& remote = remotePlayers[playerIndex];
    const bool leanEnabled = remote.teammate
      ? settings.teammateLeanEnabled
      : settings.enemyLeanEnabled;
    const float leanScale = remote.teammate
      ? settings.teammateLeanScale
      : settings.enemyLeanScale;
    const WeaponModelFrame frame =
      weaponModelFrame(remote.player, leanEnabled, leanScale);
    return weaponLocalPoint(frame, forward, right, up);
  }

  return fallback;
}

[[nodiscard]] Vec3 machineGunVisualSource(
  const WeaponFireResult& fire,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
) {
  const Vec3 forward = normalize(fire.end - fire.start);
  if (length(forward) <= 0.0001F) {
    return fire.start;
  }

  const float angle =
    static_cast<float>(fire.visualSeed % 6U) * (kTwoPi / 6.0F);
  if (playerIndex == static_cast<std::size_t>(settings.localPlayerIndex)) {
    return fire.start;
  }
  return remoteWeaponModelPoint(
    fire.start,
    localPlayer,
    remotePlayers,
    playerIndex,
    settings,
    0.64F,
    std::cos(angle) * 0.055F,
    0.09F + std::sin(angle) * 0.055F
  );
}

[[nodiscard]] Vec3 shotgunVisualSource(
  const WeaponFireResult& fire,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
) {
  if (!settings.shotgunWeaponModelStart) {
    return fire.start;
  }
  return remoteWeaponModelPoint(
    fire.start,
    localPlayer,
    remotePlayers,
    playerIndex,
    settings,
    0.62F,
    0.0F,
    0.115F
  );
}

[[nodiscard]] Vec3 projectileVisualPosition(
  const RocketProjectileSnapshot& projectile,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const RenderSettings& settings
) {
  if (projectile.weapon != Weapon::PlasmaGun) {
    return projectile.position;
  }
  const std::size_t owner = static_cast<std::size_t>(projectile.owner);
  if (owner == static_cast<std::size_t>(settings.localPlayerIndex)) {
    return projectile.position +
      (firstPersonWeaponMuzzlePosition(localPlayer) - playerEyePosition(localPlayer));
  }
  if (owner >= remotePlayers.size() || !remotePlayers[owner].visible) {
    return projectile.position;
  }
  const RemotePlayerView& remote = remotePlayers[owner];
  const Vec3 remoteEye = playerEyePosition(remote.player);
  const Vec3 localEye = playerEyePosition(localPlayer);
  if (distanceSquared(projectile.position, remoteEye) >= distanceSquared(projectile.position, localEye)) {
    return projectile.position;
  }
  const bool leanEnabled = remote.teammate
    ? settings.teammateLeanEnabled
    : settings.enemyLeanEnabled;
  const float leanScale = remote.teammate
    ? settings.teammateLeanScale
    : settings.enemyLeanScale;
  const WeaponModelFrame frame =
    weaponModelFrame(remote.player, leanEnabled, leanScale);
  return projectile.position +
    (weaponLocalPoint(frame, 0.74F, 0.0F, 0.09F) - remoteEye);
}

void addMachineGunMuzzleFlash(
  Scene3D& scene,
  const WeaponFireResult& fire,
  Vec3 visualStart
) {
  const Vec3 forward = normalize(fire.end - visualStart);
  if (length(forward) <= 0.0001F) {
    return;
  }
  const Vec3 right = perpendicularRight(forward);
  const Vec3 up = normalize(cross(right, forward));
  const Vec3 center = visualStart + forward * 0.13F;

  addSphereApprox(scene, center + forward * 0.030F, 0.045F, {255, 242, 176, 220});
  addSegment(scene, center - right * 0.060F, center + right * 0.060F, 0.024F, {255, 180, 72, 180});
  addSegment(scene, center - up * 0.045F, center + up * 0.045F, 0.020F, {255, 210, 104, 165});
}

void addMachineGunTrace(
  Scene3D& scene,
  const WeaponFireResult& fire,
  Vec3 visualStart
) {
  const Vec3 forward = normalize(fire.end - visualStart);
  const float distance = length(fire.end - visualStart);
  if (length(forward) <= 0.0001F || distance <= 0.01F) {
    return;
  }
  addSegment(
    scene,
    visualStart + forward * 0.22F,
    fire.end,
    fire.hit ? 0.016F : 0.010F,
    fire.hit ? RenderColor{255, 236, 150, 205} : RenderColor{255, 196, 92, 150}
  );
}

void addMachineGunImpactSpark(Scene3D& scene, const WeaponFireResult& fire) {
  const Vec3 forward = normalize(fire.end - fire.start);
  if (length(forward) <= 0.0001F) {
    return;
  }
  const Vec3 right = perpendicularRight(forward);
  const Vec3 up = normalize(cross(right, forward));
  const RenderColor core = fire.hit
    ? RenderColor{255, 94, 54, 220}
    : RenderColor{234, 206, 148, 175};
  const RenderColor spark = fire.hit
    ? RenderColor{255, 212, 116, 205}
    : RenderColor{188, 152, 92, 155};

  addSphereApprox(scene, fire.end - forward * 0.025F, fire.hit ? 0.060F : 0.045F, core);
  for (std::uint8_t index = 0; index < 4U; ++index) {
    const float angle = static_cast<float>(index) * (kTwoPi * 0.25F);
    const Vec3 direction =
      normalize((right * std::cos(angle)) + (up * std::sin(angle)) - forward * 0.35F);
    addSegment(
      scene,
      fire.end - forward * 0.015F,
      fire.end + direction * (fire.hit ? 0.18F : 0.12F),
      0.012F,
      spark
    );
  }
}

void addMachineGunFireVisuals(
  Scene3D& scene,
  const WeaponFireResult& fire,
  Vec3 visualStart
) {
  addMachineGunMuzzleFlash(scene, fire, visualStart);
  addMachineGunTrace(scene, fire, visualStart);
  addMachineGunImpactSpark(scene, fire);
}

void addShotgunMuzzleFlash(
  Scene3D& scene,
  const WeaponFireResult& fire,
  Vec3 visualStart
) {
  const Vec3 forward = normalize(fire.end - visualStart);
  if (length(forward) <= 0.0001F) {
    return;
  }
  const Vec3 right = perpendicularRight(forward);
  const Vec3 up = normalize(cross(right, forward));
  const Vec3 center = visualStart + forward * 0.18F;
  constexpr RenderColor hotCore = {255, 246, 178, 240};
  constexpr RenderColor warmEdge = {255, 132, 54, 205};

  addSphereApprox(scene, center + forward * 0.05F, 0.12F, hotCore);
  addSegment(scene, center - right * 0.20F, center + right * 0.20F, 0.07F, warmEdge);
  addSegment(scene, center - up * 0.15F, center + up * 0.15F, 0.055F, warmEdge);
  addSegment(
    scene,
    center - (right + up) * 0.12F,
    center + (right + up) * 0.12F,
    0.045F,
    {255, 205, 92, 210}
  );
}

void addShotgunPelletTraces(
  Scene3D& scene,
  const WeaponFireResult& fire,
  Vec3 visualStart
) {
  const Vec3 forward = normalize(fire.end - visualStart);
  const float distance = length(fire.end - visualStart);
  if (length(forward) <= 0.0001F || distance <= 0.01F) {
    return;
  }
  const std::uint8_t pelletCount = std::clamp<std::uint8_t>(
    fire.pelletCount == 0U ? 10U : fire.pelletCount,
    1U,
    12U
  );
  for (std::uint8_t pelletIndex = 0; pelletIndex < pelletCount; ++pelletIndex) {
    const Vec3 direction =
      shotgunPelletVisualDirection(forward, pelletIndex, pelletCount);
    const float traceDistance = distance * (pelletIndex == 0 ? 1.0F : 0.92F);
    const RenderColor color = pelletIndex == 0
      ? RenderColor{255, 236, 158, 210}
      : RenderColor{255, 196, 92, 145};
    addSegment(
      scene,
      visualStart + direction * 0.20F,
      visualStart + direction * traceDistance,
      pelletIndex == 0 ? 0.018F : 0.011F,
      color
    );
  }
}

void addShotgunImpactPuffs(
  Scene3D& scene,
  const WeaponFireResult& fire,
  Vec3 visualStart
) {
  const Vec3 forward = normalize(fire.end - visualStart);
  if (length(forward) <= 0.0001F) {
    return;
  }
  const Vec3 right = perpendicularRight(forward);
  const Vec3 up = normalize(cross(right, forward));
  const std::uint8_t puffCount = fire.hit
    ? std::clamp<std::uint8_t>(fire.pelletHitCount, 3U, 8U)
    : 5U;
  const RenderColor core = fire.hit
    ? RenderColor{255, 82, 56, 230}
    : RenderColor{238, 210, 154, 190};
  const RenderColor fleck = fire.hit
    ? RenderColor{255, 185, 112, 210}
    : RenderColor{166, 132, 91, 170};
  for (std::uint8_t index = 0; index < puffCount; ++index) {
    const float angle = static_cast<float>(index) / static_cast<float>(puffCount) * kTwoPi;
    const float radius = 0.055F + 0.018F * static_cast<float>(index % 3U);
    const Vec3 offset =
      (right * std::cos(angle) + up * std::sin(angle)) * (0.10F + radius);
    addSphereApprox(scene, fire.end + offset - forward * 0.03F, radius, index == 0 ? core : fleck);
  }
}

void addShotgunFireVisuals(
  Scene3D& scene,
  const WeaponFireResult& fire,
  Vec3 visualStart
) {
  addShotgunMuzzleFlash(scene, fire, visualStart);
  addShotgunPelletTraces(scene, fire, visualStart);
  addShotgunImpactPuffs(scene, fire, visualStart);
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
  addWireBox(scene, arena.min, arena.max, 0.025F, {120, 138, 156, 255});

  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    addWallBox(scene, wall);
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    addArenaBrush(scene, arena.brushes[index]);
  }

  if (settings.renderMode == 1) {
    addFirstPersonWeaponModel(scene, player, settings.localSelectedWeapon);
  }

  for (const RemotePlayerView& remote : remotePlayers) {
    if (!remote.visible) {
      continue;
    }
    ++scene.visibleRemotePlayers;
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
      settings.drawPlayerOutlines &&
      outlineEnabled &&
      usesGeometryPlayerOutlineFallback(settings.playerOutlineStyle) &&
      outlineWidth > 0.0F &&
      outlineAlpha > 0.0F
    ) {
      ++scene.playerOutlinesBuilt;
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
    if (settings.drawRemotePlayers) {
      ++scene.remoteBodyModelsBuilt;
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
    if (settings.drawRemoteWeapons) {
      ++scene.remoteWeaponModelsBuilt;
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
  for (std::size_t fireIndex = 0; fireIndex < weaponFires.size(); ++fireIndex) {
    const WeaponFireResult& fire = weaponFires[fireIndex];
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
    } else if (fire.weapon == Weapon::MachineGun) {
      addMachineGunFireVisuals(
        scene,
        fire,
        machineGunVisualSource(fire, player, remotePlayers, fireIndex, settings)
      );
    } else if (fire.weapon == Weapon::Shotgun) {
      addShotgunFireVisuals(
        scene,
        fire,
        shotgunVisualSource(fire, player, remotePlayers, fireIndex, settings)
      );
    }
  }
for (const RocketProjectileSnapshot& projectile : rockets) {
  if (!projectile.active) {
    continue;
  }

  const Vec3 projectilePosition =
    projectileVisualPosition(projectile, player, remotePlayers, settings);
  const float projectileSize =
    projectile.radius > 0.0F ? projectile.radius : 0.14F;

  if (projectile.weapon == Weapon::GrenadeLauncher) {
    addSphereApprox(
      scene,
      projectilePosition,
      projectileSize,
      {8, 48, 18, 255}
    );
    addWireBox(
      scene,
      projectilePosition - Vec3{
        projectileSize * 1.4F,
        projectileSize * 1.4F,
        projectileSize * 1.4F
      },
      projectilePosition + Vec3{
        projectileSize * 1.4F,
        projectileSize * 1.4F,
        projectileSize * 1.4F
      },
      0.012F,
      {255, 220, 100, 255}
    );
    continue;
  }

  if (projectile.weapon == Weapon::PlasmaGun) {
    addSphereApprox(scene, projectilePosition, 0.12F, {112, 255, 142, 255});
    addSphereApprox(scene, projectilePosition, 0.07F, {230, 255, 210, 255});
    continue;
  }

  constexpr float size = 0.14F;
  addBox(
      scene,
      projectilePosition - Vec3{size, size, size},
      projectilePosition + Vec3{size, size, size},
      {255, 126, 40, 255}
    );
    addWireBox(
      scene,
      projectilePosition - Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
      projectilePosition + Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
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
