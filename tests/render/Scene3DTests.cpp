#include "render/GltfSkinnedModel.hpp"
#include "render/Scene3D.hpp"
#include "render/WeaponPresentation.hpp"
#include "sim/Arena.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

bool sameColor(lg::RenderColor lhs, lg::RenderColor rhs) {
  return lhs.red == rhs.red &&
    lhs.green == rhs.green &&
    lhs.blue == rhs.blue &&
    lhs.alpha == rhs.alpha;
}

bool isEnemyModelColor(lg::RenderColor color) {
  return color.red >= 110 &&
    color.green <= 170 &&
    color.blue <= 180 &&
    color.alpha == 255;
}

bool finiteVec3(lg::Vec3 value) {
  return std::isfinite(value.x) &&
    std::isfinite(value.y) &&
    std::isfinite(value.z);
}

lg::Vec3 crossProduct(lg::Vec3 lhs, lg::Vec3 rhs) {
  return {
    (lhs.y * rhs.z) - (lhs.z * rhs.y),
    (lhs.z * rhs.x) - (lhs.x * rhs.z),
    (lhs.x * rhs.y) - (lhs.y * rhs.x),
  };
}

float contactShadowMaxRadius(const lg::Scene3D& scene) {
  if (scene.contactShadowVertices.empty()) {
    return 0.0F;
  }
  const lg::Vec3 center = scene.contactShadowVertices.front().position;
  float radius = 0.0F;
  for (const lg::Vertex3D& vertex : scene.contactShadowVertices) {
    radius = std::max(radius, lg::length(vertex.position - center));
  }
  return radius;
}

lg::ArenaBrush slopedTopBrush(
  float minX,
  float maxX,
  float zAtMinX,
  float zAtMaxX
) {
  lg::ArenaBrush brush;
  const float maxZ = std::max(zAtMinX, zAtMaxX);
  brush.min = {minX, -4.0F, 0.0F};
  brush.max = {maxX, 4.0F, maxZ};
  brush.vertexCount = 8;
  brush.vertices[0] = {minX, -4.0F, 0.0F};
  brush.vertices[1] = {maxX, -4.0F, 0.0F};
  brush.vertices[2] = {maxX, 4.0F, 0.0F};
  brush.vertices[3] = {minX, 4.0F, 0.0F};
  brush.vertices[4] = {minX, -4.0F, zAtMinX};
  brush.vertices[5] = {maxX, -4.0F, zAtMaxX};
  brush.vertices[6] = {maxX, 4.0F, zAtMaxX};
  brush.vertices[7] = {minX, 4.0F, zAtMinX};
  brush.faceCount = 6;
  brush.faces[0] = {{-1.0F, 0.0F, 0.0F}, -minX};
  brush.faces[0].vertices = {0, 3, 7, 4};
  brush.faces[0].vertexCount = 4;
  brush.faces[1] = {{1.0F, 0.0F, 0.0F}, maxX};
  brush.faces[1].vertices = {1, 5, 6, 2};
  brush.faces[1].vertexCount = 4;
  brush.faces[2] = {{0.0F, -1.0F, 0.0F}, 4.0F};
  brush.faces[2].vertices = {0, 4, 5, 1};
  brush.faces[2].vertexCount = 4;
  brush.faces[3] = {{0.0F, 1.0F, 0.0F}, 4.0F};
  brush.faces[3].vertices = {3, 2, 6, 7};
  brush.faces[3].vertexCount = 4;
  brush.faces[4] = {{0.0F, 0.0F, -1.0F}, 0.0F};
  brush.faces[4].vertices = {0, 1, 2, 3};
  brush.faces[4].vertexCount = 4;
  const float slope = (zAtMaxX - zAtMinX) / (maxX - minX);
  const lg::Vec3 topNormal = lg::normalize({-slope, 0.0F, 1.0F});
  brush.faces[5].normal = topNormal;
  brush.faces[5].distance =
    (topNormal.x * minX) + (topNormal.z * zAtMinX);
  brush.faces[5].vertices = {4, 5, 6, 7};
  brush.faces[5].vertexCount = 4;
  return brush;
}

const lg::SimpleRenderInstance* findSimpleMesh(
  const lg::Scene3D& scene,
  lg::MeshHandle mesh
) {
  const auto found = std::find_if(
    scene.simpleInstances.begin(),
    scene.simpleInstances.end(),
    [mesh](const lg::SimpleRenderInstance& instance) {
      return instance.mesh == mesh;
    }
  );
  return found == scene.simpleInstances.end() ? nullptr : &*found;
}

float largestBillboardScale(
  const lg::Scene3D& scene,
  lg::BillboardHandle billboard
) {
  float scale = 0.0F;
  for (const lg::SimpleRenderInstance& instance : scene.simpleInstances) {
    if (instance.billboard == billboard) {
      scale = std::max(scale, instance.scale.x);
    }
  }
  return scale;
}

std::size_t billboardCount(
  const lg::Scene3D& scene,
  lg::BillboardHandle billboard
) {
  return static_cast<std::size_t>(std::count_if(
    scene.simpleInstances.begin(),
    scene.simpleInstances.end(),
    [billboard](const lg::SimpleRenderInstance& instance) {
      return instance.billboard == billboard;
    }
  ));
}

lg::BoundingSphere legacyMaterialMeshBounds(
  std::span<const lg::WeaponMaterialVertex3D> vertices
) {
  if (vertices.empty()) {
    return {};
  }
  lg::Vec3 center = {};
  for (const lg::WeaponMaterialVertex3D& vertex : vertices) {
    center += vertex.position;
  }
  center *= 1.0F / static_cast<float>(vertices.size());
  float radius = 0.0F;
  for (const lg::WeaponMaterialVertex3D& vertex : vertices) {
    radius = std::max(radius, lg::length(vertex.position - center));
  }
  return {center, radius};
}

bool validStaticMeshBatches(const lg::Scene3D& scene) {
  std::size_t covered = 0U;
  for (std::size_t batchIndex = 0U;
       batchIndex < scene.staticMeshBatches.size();
       ++batchIndex) {
    const lg::StaticMeshBatch& batch = scene.staticMeshBatches[batchIndex];
    if (
      batch.firstInstance != covered ||
      batch.instanceCount == 0U ||
      covered + batch.instanceCount > scene.staticMeshInstances.size()
    ) {
      return false;
    }
    if (batchIndex > 0U) {
      const lg::StaticMeshBatch& previous = scene.staticMeshBatches[batchIndex - 1U];
      if (previous.mesh == batch.mesh && previous.pass == batch.pass) {
        return false;
      }
    }
    for (std::size_t offset = 0U; offset < batch.instanceCount; ++offset) {
      const lg::StaticMeshInstance& instance =
        scene.staticMeshInstances[covered + offset];
      if (instance.mesh != batch.mesh || instance.pass != batch.pass) {
        return false;
      }
    }
    covered += batch.instanceCount;
  }
  return covered == scene.staticMeshInstances.size() &&
    std::is_sorted(
      scene.staticMeshInstances.begin(),
      scene.staticMeshInstances.end(),
      [](const lg::StaticMeshInstance& lhs, const lg::StaticMeshInstance& rhs) {
        if (lhs.pass != rhs.pass) {
          return static_cast<int>(lhs.pass) < static_cast<int>(rhs.pass);
        }
        return static_cast<std::uint16_t>(lhs.mesh) <
          static_cast<std::uint16_t>(rhs.mesh);
      }
    );
}

bool validSimpleBatches(const lg::Scene3D& scene) {
  std::size_t covered = 0U;
  for (std::size_t batchIndex = 0U;
       batchIndex < scene.simpleBatches.size();
       ++batchIndex) {
    const lg::SimpleRenderBatch& batch = scene.simpleBatches[batchIndex];
    if (
      batch.firstInstance != covered ||
      batch.instanceCount == 0U ||
      covered + batch.instanceCount > scene.simpleInstances.size()
    ) {
      return false;
    }
    if (batchIndex > 0U) {
      const lg::SimpleRenderBatch& previous = scene.simpleBatches[batchIndex - 1U];
      if (
        previous.mesh == batch.mesh &&
        previous.billboard == batch.billboard &&
        previous.pass == batch.pass
      ) {
        return false;
      }
    }
    for (std::size_t offset = 0U; offset < batch.instanceCount; ++offset) {
      const lg::SimpleRenderInstance& instance = scene.simpleInstances[covered + offset];
      if (
        instance.mesh != batch.mesh ||
        instance.billboard != batch.billboard ||
        instance.pass != batch.pass
      ) {
        return false;
      }
    }
    covered += batch.instanceCount;
  }
  return covered == scene.simpleInstances.size() &&
    std::is_sorted(
      scene.simpleInstances.begin(),
      scene.simpleInstances.end(),
      [](const lg::SimpleRenderInstance& lhs, const lg::SimpleRenderInstance& rhs) {
        if (lhs.pass != rhs.pass) {
          return static_cast<int>(lhs.pass) < static_cast<int>(rhs.pass);
        }
        if (lhs.mesh != rhs.mesh) {
          return static_cast<std::uint16_t>(lhs.mesh) <
            static_cast<std::uint16_t>(rhs.mesh);
        }
        return static_cast<std::uint16_t>(lhs.billboard) <
          static_cast<std::uint16_t>(rhs.billboard);
      }
    );
}

lg::Vec3 transformPoint(const lg::StaticMeshInstance& instance, lg::Vec3 local) {
  return {
    lg::dot(instance.modelRow0, local) + instance.modelTranslation.x,
    lg::dot(instance.modelRow1, local) + instance.modelTranslation.y,
    lg::dot(instance.modelRow2, local) + instance.modelTranslation.z,
  };
}

std::size_t playerBoxInstanceCount(const lg::Scene3D& scene) {
  return static_cast<std::size_t>(std::count_if(
    scene.staticMeshInstances.begin(),
    scene.staticMeshInstances.end(),
    [](const lg::StaticMeshInstance& instance) {
      return instance.playerBoxBody &&
        instance.mesh == lg::MeshHandle::PlayerBoxCube;
    }
  ));
}

float maxPaletteDelta(
  std::span<const std::array<float, 16>> lhs,
  std::span<const std::array<float, 16>> rhs
) {
  const std::size_t count = std::min(lhs.size(), rhs.size());
  float delta = 0.0F;
  for (std::size_t index = 0; index < count; ++index) {
    for (std::size_t value = 0; value < 16U; ++value) {
      delta = std::max(delta, std::fabs(lhs[index][value] - rhs[index][value]));
    }
  }
  return delta;
}

float maxPaletteDeltaAtIndices(
  std::span<const std::array<float, 16>> lhs,
  std::span<const std::array<float, 16>> rhs,
  std::span<const std::size_t> indices
) {
  float delta = 0.0F;
  for (std::size_t index : indices) {
    if (index >= lhs.size() || index >= rhs.size()) {
      continue;
    }
    for (std::size_t value = 0; value < 16U; ++value) {
      delta = std::max(delta, std::fabs(lhs[index][value] - rhs[index][value]));
    }
  }
  return delta;
}

float maxVertexX(const lg::Scene3D& scene) {
  float result = -std::numeric_limits<float>::infinity();
  for (const lg::Vertex3D& vertex : scene.vertices) {
    result = std::max(result, vertex.position.x);
  }
  for (const lg::Vertex3D& vertex : scene.translucentVertices) {
    result = std::max(result, vertex.position.x);
  }
  return result;
}

bool hasAnyVertex(const lg::Scene3D& scene) {
  return !scene.vertices.empty() || !scene.translucentVertices.empty();
}

struct UvBounds {
  float minU = 0.0F;
  float maxU = 0.0F;
  float minV = 0.0F;
  float maxV = 0.0F;
  bool found = false;
};

UvBounds texturedWallUvBounds(
  const lg::TextureProjection& projection,
  float quakeSize = 128.0F
) {
  lg::Arena arena;
  arena.wallCount = 1;
  arena.walls[0].min = {0.0F, 0.0F, 0.0F};
  arena.walls[0].max = {quakeSize / 40.0F, quakeSize / 40.0F, 16.0F / 40.0F};
  const std::uint32_t materialId = lg::arenaMaterialId("128x128/test/checker");
  arena.walls[0].faceMaterialIds[1] = materialId;
  arena.walls[0].faceTextureProjections[1] = projection;

  const lg::Scene3D scene = lg::buildStaticWorldScene(arena);

  UvBounds bounds;
  for (const lg::Vertex3D& vertex : scene.vertices) {
    if (vertex.materialId != materialId) {
      continue;
    }
    if (!bounds.found) {
      bounds.minU = vertex.u;
      bounds.maxU = vertex.u;
      bounds.minV = vertex.v;
      bounds.maxV = vertex.v;
      bounds.found = true;
      continue;
    }
    bounds.minU = std::min(bounds.minU, vertex.u);
    bounds.maxU = std::max(bounds.maxU, vertex.u);
    bounds.minV = std::min(bounds.minV, vertex.v);
    bounds.maxV = std::max(bounds.maxV, vertex.v);
  }
  return bounds;
}

} // namespace

int main() {
  int failures = 0;
  failures += expect(
    lg::antiAliasingSampleCount(-1) == 1U &&
      lg::antiAliasingSampleCount(0) == 1U &&
      lg::antiAliasingSampleCount(1) == 2U &&
      lg::antiAliasingSampleCount(2) == 4U &&
      lg::antiAliasingSampleCount(99) == 4U,
    "AA quality should map only to 1x, 2x, and 4x"
  );
  failures += expect(
    lg::sunShadowMapSize(0) == 0U &&
      lg::sunShadowMapSize(1) == 1024U &&
      lg::sunShadowMapSize(2) == 2048U,
    "sun shadow quality should map to off, 1024, and 2048"
  );
  failures += expect(
    lg::livePointLightCapacity(0) == 8U &&
      lg::livePointLightCapacity(1) == 16U &&
      lg::livePointLightCapacity(2) == 32U,
    "point-light quality should retain combat slots and scale authored capacity"
  );
  const float flickerAtStart = lg::pointLightFlickerFactor(
    17U, 6.0F, 0.65F, 1.15F, 4.25
  );
  failures += expect(
    nearlyEqual(
      flickerAtStart,
      lg::pointLightFlickerFactor(17U, 6.0F, 0.65F, 1.15F, 4.25)
    ) &&
      flickerAtStart >= 0.65F &&
      flickerAtStart <= 1.15F &&
      !nearlyEqual(
        flickerAtStart,
        lg::pointLightFlickerFactor(18U, 6.0F, 0.65F, 1.15F, 4.25)
      ),
    "point-light flicker should be bounded, seeded, and deterministic"
  );
  failures += expect(
    lg::pointShadowFace({4.0F, 1.0F, -2.0F}) ==
        lg::PointShadowFace::PositiveX &&
      lg::pointShadowFace({-1.0F, -5.0F, 2.0F}) ==
        lg::PointShadowFace::NegativeY &&
      lg::pointShadowFace({1.0F, 2.0F, 6.0F}) ==
        lg::PointShadowFace::PositiveZ &&
      lg::pointShadowLayer(1U, lg::PointShadowFace::NegativeZ) == 11U,
    "point-shadow face and array-layer mapping should stay stable"
  );
  lg::PerspectiveCamera lightCamera;
  lightCamera.position = {};
  lightCamera.forward = {1.0F, 0.0F, 0.0F};
  lightCamera.right = {0.0F, -1.0F, 0.0F};
  lightCamera.up = {0.0F, 0.0F, 1.0F};
  lightCamera.focalLength = 1.0F;
  lightCamera.aspectRatio = 1.0F;
  const std::array<lg::LivePointLight, 3> rankedLights = {{
    {
      {-1.0F, 0.0F, 0.0F}, {1.0F, 0.2F, 0.1F}, 1.0F, 1.0F, 2.5F,
      0.0F, 1.0F, 0, 2U, true, true, true, true, false,
    },
    {
      {12.0F, 0.0F, 0.0F}, {0.1F, 1.0F, 0.2F}, 1.0F, 1.0F, 2.0F,
      0.0F, 1.0F, 50, 1U, true, false, false, false, false,
    },
    {
      {8.0F, 0.0F, 0.0F}, {0.2F, 0.1F, 1.0F}, 1.0F, 1.0F, 2.0F,
      0.0F, 1.0F, 0, 0U, true, false, false, false, false,
    },
  }};
  lg::PointLightSelectionStats rankedStats;
  const std::vector<lg::LivePointLight> selectedRankedLights =
    lg::selectLivePointLights(
      rankedLights,
      lightCamera,
      2U,
      &rankedStats
    );
  failures += expect(
    selectedRankedLights.size() == 2U &&
      selectedRankedLights[0].sourceIndex == 2U &&
      selectedRankedLights[1].sourceIndex == 1U &&
      rankedStats.closeRetained == 1U &&
      rankedStats.dropped == 1U,
    "point-light selection should retain a close behind-camera influence sphere"
  );
  const std::vector<lg::LivePointLight> closePointShadow =
    lg::selectPointShadowLights(
      std::span<const lg::LivePointLight>(
        selectedRankedLights.data(),
        1U
      ),
      lightCamera,
      1U
    );
  failures += expect(
    closePointShadow.size() == 1U &&
      closePointShadow[0].sourceIndex == 2U,
    "a close behind-camera caster should remain eligible for a shadow slot"
  );
  const std::array<lg::LivePointLight, 2> tieLights = {{
    {
      {8.0F, 1.0F, 0.0F}, {}, 1.0F, 1.0F, 2.0F, 0.0F, 1.0F,
      4, 9U, true, false, false, false, false,
    },
    {
      {8.0F, -1.0F, 0.0F}, {}, 1.0F, 1.0F, 2.0F, 0.0F, 1.0F,
      4, 3U, true, false, false, false, false,
    },
  }};
  const std::vector<lg::LivePointLight> selectedTieLight =
    lg::selectLivePointLights(tieLights, lightCamera, 1U);
  failures += expect(
    selectedTieLight.size() == 1U && selectedTieLight[0].sourceIndex == 3U,
    "point-light rank ties should use the stable authored source index"
  );
  const std::array<lg::LivePointLight, 2> visibilityCasters = {{
    {
      {-10.0F, 0.0F, 0.0F}, {}, 1.0F, 1.0F, 1.0F, 0.0F, 1.0F,
      100, 4U, true, true, true, false, false,
    },
    {
      {8.0F, 0.0F, 0.0F}, {}, 1.0F, 1.0F, 2.0F, 0.0F, 1.0F,
      0, 7U, true, true, true, false, false,
    },
  }};
  const std::vector<lg::LivePointLight> visibleCaster =
    lg::selectLivePointLights(visibilityCasters, lightCamera, 2U);
  const std::vector<lg::LivePointLight> visiblePointShadow =
    lg::selectPointShadowLights(visibleCaster, lightCamera, 1U);
  failures += expect(
    visibleCaster.size() == 1U &&
      visiblePointShadow.size() == 1U &&
      visiblePointShadow[0].sourceIndex == 7U,
    "an off-screen high-priority caster should not take a visible shadow slot"
  );
  const std::array<lg::LivePointLight, 2> flickerFrameA = {{
    {
      {8.0F, 0.0F, 0.0F}, {}, 0.1F, 2.0F, 2.0F, 0.0F, 1.0F,
      0, 5U, true, true, true, true, false,
    },
    {
      {8.0F, 1.0F, 0.0F}, {}, 4.0F, 1.0F, 2.0F, 0.0F, 1.0F,
      0, 6U, true, true, true, true, false,
    },
  }};
  std::array<lg::LivePointLight, 2> flickerFrameB = flickerFrameA;
  flickerFrameB[0].intensity = 4.0F;
  flickerFrameB[1].intensity = 0.1F;
  const std::vector<lg::LivePointLight> flickerSelectedA =
    lg::selectLivePointLights(flickerFrameA, lightCamera, 1U);
  const std::vector<lg::LivePointLight> flickerSelectedB =
    lg::selectLivePointLights(flickerFrameB, lightCamera, 1U);
  const std::vector<lg::LivePointLight> flickerShadowA =
    lg::selectPointShadowLights(flickerSelectedA, lightCamera, 1U);
  const std::vector<lg::LivePointLight> flickerShadowB =
    lg::selectPointShadowLights(flickerSelectedB, lightCamera, 1U);
  failures += expect(
    flickerSelectedA.size() == 1U &&
      flickerSelectedB.size() == 1U &&
      flickerShadowA.size() == 1U &&
      flickerShadowB.size() == 1U &&
      flickerSelectedA[0].sourceIndex == 5U &&
      flickerSelectedB[0].sourceIndex == 5U &&
      flickerShadowA[0].sourceIndex == 5U &&
      flickerShadowB[0].sourceIndex == 5U,
    "runtime flicker intensity should not change live or shadow selection"
  );
  lg::ArenaStaticLight bakedPointLight;
  lg::ArenaStaticLight flickeringPointLight;
  flickeringPointLight.flickerEnabled = true;
  lg::ArenaStaticLight shadowedPointLight;
  shadowedPointLight.castsShadows = true;
  failures += expect(
    lg::staticLightBakesIntoWorld(bakedPointLight) &&
      !lg::staticLightBakesIntoWorld(flickeringPointLight) &&
      !lg::staticLightBakesIntoWorld(shadowedPointLight),
    "flickering and shadowed lights must stay out of static world vertex light"
  );
  constexpr lg::FragmentResourceLayout instancedColorLayout =
    lg::instancedColorFragmentLayout();
  failures += expect(
    instancedColorLayout.samplers == 1U &&
      instancedColorLayout.uniformBuffers == 1U,
    "instanced color pipelines should declare point shadow and scene light resources"
  );
  constexpr lg::FragmentResourceLayout untexturedLightLayout =
    lg::untexturedSceneLightFragmentLayout();
  failures += expect(
    untexturedLightLayout.samplers == 0U &&
      untexturedLightLayout.uniformBuffers == 1U,
    "glow and bloom source pipelines should declare no sampler and one scene light uniform"
  );
  constexpr lg::FragmentResourceLayout sceneCompositeLayout =
    lg::sceneCompositeFragmentLayout();
  failures += expect(
    sceneCompositeLayout.samplers == 3U &&
      sceneCompositeLayout.uniformBuffers == 1U,
    "scene composite should bind scene, bloom, and view-model depth"
  );
  constexpr lg::FragmentResourceLayout sceneCompositeNoBloomLayout =
    lg::sceneCompositeNoBloomFragmentLayout();
  failures += expect(
    sceneCompositeNoBloomLayout.samplers == 1U &&
      sceneCompositeNoBloomLayout.uniformBuffers == 1U,
    "no-bloom composite should bind only the scene color"
  );
  constexpr std::array<lg::SimpleRenderBatch, 2> noBloomSources = {{
    {
      lg::MeshHandle::Invalid,
      lg::BillboardHandle::ExplosionHalo,
      lg::RenderPass::AdditiveGlow,
      0U,
      0U,
    },
    {
      lg::MeshHandle::RocketProjectile,
      lg::BillboardHandle::Invalid,
      lg::RenderPass::OpaqueWorld,
      0U,
      1U,
    },
  }};
  constexpr std::array<lg::SimpleRenderBatch, 1> bloomSources = {{
    {
      lg::MeshHandle::Invalid,
      lg::BillboardHandle::ExplosionHalo,
      lg::RenderPass::AdditiveGlow,
      0U,
      1U,
    },
  }};
  failures += expect(
    !lg::hasBloomSources(noBloomSources) &&
      lg::hasBloomSources(bloomSources) &&
      !lg::effectiveBloom(false, bloomSources) &&
      !lg::effectiveBloom(true, noBloomSources) &&
      lg::effectiveBloom(true, bloomSources),
    "bloom should run only when requested and an additive batch has instances"
  );
  failures += expect(
    lg::chooseSampledDepthFormat({true, true, true}) ==
      lg::SampledDepthFormatChoice::D32 &&
    lg::chooseSampledDepthFormat({false, true, true}) ==
      lg::SampledDepthFormatChoice::D24 &&
    lg::chooseSampledDepthFormat({false, false, true}) ==
      lg::SampledDepthFormatChoice::D16 &&
    lg::chooseSampledDepthFormat({false, false, false}) ==
      lg::SampledDepthFormatChoice::None,
    "depth format selection should require target and sampler support"
  );
  constexpr lg::AuxiliaryDepthPlan auxiliaryDepth =
    lg::buildAuxiliaryDepthPlan(true, true);
  failures += expect(
    auxiliaryDepth.enabled &&
      auxiliaryDepth.depthOnly &&
      auxiliaryDepth.sampleCount == 1U &&
      auxiliaryDepth.usesWorldCamera &&
      auxiliaryDepth.includesWorld &&
      auxiliaryDepth.includesStaticMeshes &&
      auxiliaryDepth.includesMaterialMeshes &&
      auxiliaryDepth.includesGltfPlayers &&
      auxiliaryDepth.includesSimpleInstances &&
      !lg::buildAuxiliaryDepthPlan(true, false).enabled,
    "auxiliary depth should use all opaque world paths or disable safely"
  );
  constexpr lg::SunShadowPassPlan noShadowPass =
    lg::buildSunShadowPassPlan(0U);
  constexpr lg::SunShadowPassPlan fullShadowPass =
    lg::buildSunShadowPassPlan(2048U);
  failures += expect(
    noShadowPass.textureSize == 1U &&
      !noShadowPass.renderShadowPass &&
      noShadowPass.useClearedFallback &&
      fullShadowPass.textureSize == 2048U &&
      fullShadowPass.renderShadowPass &&
      !fullShadowPass.useClearedFallback,
    "disabled shadows should use the cleared fallback without a frame pass"
  );
  constexpr lg::PointShadowPassPlan noPointShadowPass =
    lg::buildPointShadowPassPlan(0, 4U, false);
  constexpr lg::PointShadowPassPlan lowPointShadowPass =
    lg::buildPointShadowPassPlan(1, 4U, false);
  constexpr lg::PointShadowPassPlan cachedHighPointShadowPass =
    lg::buildPointShadowPassPlan(2, 4U, true);
  failures += expect(
    noPointShadowPass.useClearedFallback &&
      noPointShadowPass.lightCount == 0U &&
      lowPointShadowPass.textureSize == 256U &&
      lowPointShadowPass.lightCount == 1U &&
      lowPointShadowPass.layerCount == 6U &&
      lowPointShadowPass.renderCache &&
      cachedHighPointShadowPass.textureSize == 512U &&
      cachedHighPointShadowPass.lightCount == 2U &&
      cachedHighPointShadowPass.layerCount == 12U &&
      !cachedHighPointShadowPass.renderCache,
    "point-shadow quality should map to a bounded cached depth-array plan"
  );
  failures += expect(
    lg::classifyWorldMaterial("textures/metal/steel_plate").kind ==
      lg::WorldMaterialKind::Metal &&
    lg::classifyWorldMaterial("old_rusted_trim").kind ==
      lg::WorldMaterialKind::OxidizedMetal &&
    lg::classifyWorldMaterial("base_chain_grate").kind ==
      lg::WorldMaterialKind::Chain &&
    lg::classifyWorldMaterial("tech_machine_panel").kind ==
      lg::WorldMaterialKind::Tech &&
    lg::classifyWorldMaterial("castle/brick_wall").kind ==
      lg::WorldMaterialKind::Masonry &&
    lg::classifyWorldMaterial("wood/plank_floor").kind ==
      lg::WorldMaterialKind::Wood &&
    lg::classifyWorldMaterial("fx/amber_route_light").kind ==
      lg::WorldMaterialKind::Energy &&
    lg::classifyWorldMaterial("plain_unknown").kind ==
      lg::WorldMaterialKind::Generic,
    "world texture names should map to stable material traits"
  );
  const lg::WorldMaterialTraits metalTraits =
    lg::classifyWorldMaterial("metal/steel");
  const lg::WorldMaterialLightingPlan cheapMetal =
    lg::worldMaterialLightingPlan(metalTraits, 0);
  const lg::WorldMaterialLightingPlan mediumMetal =
    lg::worldMaterialLightingPlan(metalTraits, 1);
  const lg::WorldMaterialLightingPlan fullMetal =
    lg::worldMaterialLightingPlan(metalTraits, 2);
  const lg::WorldMaterialTraits energyTraits =
    lg::classifyWorldMaterial("energy/teleport");
  failures += expect(
    cheapMetal.specularScale == 0.0F &&
      mediumMetal.specularScale > 0.0F &&
      mediumMetal.specularScale < fullMetal.specularScale &&
      energyTraits.emissive > 0.0F &&
      lg::worldMaterialLightingPlan(energyTraits, 0).emissiveScale ==
        energyTraits.emissive,
    "material quality should gate specular but keep readable emissive tags"
  );
  failures += expect(
    lg::gltfShadowCasterPlan(3U, 5U, 0U).drawCalls == 0U &&
      lg::gltfShadowCasterPlan(3U, 5U, 2048U).instances == 3U &&
      lg::gltfShadowCasterPlan(3U, 5U, 2048U).drawCalls == 5U,
    "skinned shadow plan should reuse body instances only when shadows run"
  );
  const lg::PostProcessPlan bloomPlan =
    lg::buildPostProcessPlan(1921U, 1081U, true);
  failures += expect(
    bloomPlan.sceneWidth == 1921U &&
      bloomPlan.sceneHeight == 1081U &&
      bloomPlan.bloomWidth == 481U &&
      bloomPlan.bloomHeight == 271U &&
      bloomPlan.bloomPasses == 3U &&
      bloomPlan.bloomDepthRebuildPasses == 1U &&
      bloomPlan.bloomEnabled &&
      bloomPlan.bloomUsesWorldCamera &&
      bloomPlan.bloomMasksViewModel &&
      bloomPlan.sceneCompositePasses == 1U,
    "post process targets should keep scene size and round quarter bloom up"
  );
  const lg::PostProcessPlan noBloomPlan =
    lg::buildPostProcessPlan(1280U, 720U, false);
  failures += expect(
    noBloomPlan.bloomWidth == 0U &&
      noBloomPlan.bloomHeight == 0U &&
      noBloomPlan.bloomPasses == 0U &&
      noBloomPlan.bloomDepthRebuildPasses == 0U &&
      !noBloomPlan.bloomEnabled &&
      !noBloomPlan.bloomUsesWorldCamera &&
      !noBloomPlan.bloomMasksViewModel &&
      noBloomPlan.sceneCompositeOrder < noBloomPlan.outlineCompositeOrder &&
      noBloomPlan.outlineCompositeOrder < noBloomPlan.hudOrder &&
      bloomPlan.sceneCompositeOrder < bloomPlan.outlineCompositeOrder &&
      bloomPlan.outlineCompositeOrder < bloomPlan.hudOrder,
    "scene composite should run before outlines and HUD with bloom on or off"
  );
  constexpr lg::OutlineDepthPlan nativeSingleSampleOutline =
    lg::buildOutlineDepthPlan(true, true);
  constexpr lg::OutlineDepthPlan nativeMsaaOutline =
    lg::buildOutlineDepthPlan(true, false);
  constexpr lg::OutlineDepthPlan compatibilityOutline =
    lg::buildOutlineDepthPlan(false, true);
  failures += expect(
    nativeSingleSampleOutline.reuseWorldDepth &&
      !nativeSingleSampleOutline.rebuildDepth &&
      nativeSingleSampleOutline.passCount == 3U &&
      !nativeMsaaOutline.reuseWorldDepth &&
      nativeMsaaOutline.rebuildDepth &&
      nativeMsaaOutline.passCount == 6U &&
      !compatibilityOutline.reuseWorldDepth &&
      compatibilityOutline.rebuildDepth &&
      compatibilityOutline.passCount == 6U,
    "native MSAA outlines should rebuild one-sample depth"
  );
  const lg::DirectPresentInputs directInputs = {
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
  };
  const lg::DirectPresentPlan directPlan =
    lg::buildDirectPresentPlan(directInputs);
  failures += expect(
    directPlan.eligible &&
      directPlan.fallback == lg::DirectPresentFallbackReason::None,
    "direct present should accept only a fully safe frame"
  );
  const std::array rejectionCases = {
    std::pair{
      &lg::DirectPresentInputs::neutralGrade,
      lg::DirectPresentFallbackReason::ColorGrade,
    },
    std::pair{
      &lg::DirectPresentInputs::unitExposure,
      lg::DirectPresentFallbackReason::Exposure,
    },
    std::pair{
      &lg::DirectPresentInputs::singleSample,
      lg::DirectPresentFallbackReason::AntiAliasing,
    },
    std::pair{
      &lg::DirectPresentInputs::bloomDisabled,
      lg::DirectPresentFallbackReason::Bloom,
    },
    std::pair{
      &lg::DirectPresentInputs::sunShadowDisabled,
      lg::DirectPresentFallbackReason::SunShadow,
    },
    std::pair{
      &lg::DirectPresentInputs::competitiveQuality,
      lg::DirectPresentFallbackReason::QualityContract,
    },
    std::pair{
      &lg::DirectPresentInputs::outlineModeSupported,
      lg::DirectPresentFallbackReason::OutlineMode,
    },
    std::pair{
      &lg::DirectPresentInputs::contactShadowsEmpty,
      lg::DirectPresentFallbackReason::ContactShadows,
    },
    std::pair{
      &lg::DirectPresentInputs::translucentVerticesEmpty,
      lg::DirectPresentFallbackReason::TranslucentVertices,
    },
    std::pair{
      &lg::DirectPresentInputs::translucentEffectsEmpty,
      lg::DirectPresentFallbackReason::TranslucentEffects,
    },
    std::pair{
      &lg::DirectPresentInputs::simpleBatchesOpaque,
      lg::DirectPresentFallbackReason::SimpleBatchPass,
    },
    std::pair{
      &lg::DirectPresentInputs::activeTexturesOpaque,
      lg::DirectPresentFallbackReason::ActiveTextureAlpha,
    },
    std::pair{
      &lg::DirectPresentInputs::activeVerticesOpaque,
      lg::DirectPresentFallbackReason::ActiveVertexAlpha,
    },
    std::pair{
      &lg::DirectPresentInputs::activeInstancesOpaque,
      lg::DirectPresentFallbackReason::ActiveInstanceAlpha,
    },
    std::pair{
      &lg::DirectPresentInputs::playersOpaque,
      lg::DirectPresentFallbackReason::PlayerAlpha,
    },
    std::pair{
      &lg::DirectPresentInputs::viewModelOpaque,
      lg::DirectPresentFallbackReason::ViewModelAlpha,
    },
    std::pair{
      &lg::DirectPresentInputs::swapchainFormatSupported,
      lg::DirectPresentFallbackReason::SwapchainFormat,
    },
    std::pair{
      &lg::DirectPresentInputs::pipelinesReady,
      lg::DirectPresentFallbackReason::Pipelines,
    },
  };
  for (const auto& [field, reason] : rejectionCases) {
    lg::DirectPresentInputs unsafeInputs = directInputs;
    unsafeInputs.*field = false;
    const lg::DirectPresentPlan unsafePlan =
      lg::buildDirectPresentPlan(unsafeInputs);
    failures += expect(
      !unsafePlan.eligible && unsafePlan.fallback == reason,
      "direct present should reject each unsafe input"
    );
  }
  constexpr float oneDisplayByte = 1.0F / 255.0F;
  const auto neutralClearMatches = [](
                                     float linear,
                                     float direct) {
    return std::fabs(
      lg::directPresentDisplayChannel(linear) - direct
    ) <= oneDisplayByte;
  };
  failures += expect(
    nearlyEqual(lg::kDirectSdrClearColor.red, 0.047F) &&
      nearlyEqual(lg::kDirectSdrClearColor.green, 0.055F) &&
      nearlyEqual(lg::kDirectSdrClearColor.blue, 0.071F) &&
      neutralClearMatches(
        lg::kNeutralHdrSceneClearColor.red,
        lg::kDirectSdrClearColor.red
      ) &&
      neutralClearMatches(
        lg::kNeutralHdrSceneClearColor.green,
        lg::kDirectSdrClearColor.green
      ) &&
      neutralClearMatches(
        lg::kNeutralHdrSceneClearColor.blue,
        lg::kDirectSdrClearColor.blue
      ),
    "direct SDR and neutral fallback clears should match within one display byte"
  );
  const lg::PerspectiveCamera shadowCamera = lg::makePerspectiveCamera(
    {3.0F, 4.0F, 2.0F},
    0.25F,
    -0.1F,
    90.0F,
    16.0F / 9.0F
  );
  const lg::SunShadowProjection shadowProjection =
    lg::buildSunShadowProjection(
      shadowCamera,
      {0.25F, -0.45F, -0.86F},
      2
    );
  lg::PerspectiveCamera subTexelCamera = shadowCamera;
  const float shadowTexel =
    shadowProjection.halfExtent * 2.0F /
    static_cast<float>(shadowProjection.mapSize);
  subTexelCamera.position += shadowProjection.right * (shadowTexel * 0.2F);
  const lg::SunShadowProjection subTexelProjection =
    lg::buildSunShadowProjection(
      subTexelCamera,
      {0.25F, -0.45F, -0.86F},
      2
    );
  failures += expect(
    nearlyEqual(
      lg::dot(shadowProjection.origin, shadowProjection.right),
      lg::dot(subTexelProjection.origin, subTexelProjection.right),
      0.0001F
    ),
    "sun shadow projection should stay fixed for sub-texel camera motion"
  );
  lg::Arena faceArena;
  faceArena.renderDefaultFloor = false;
  faceArena.wallCount = 1;
  faceArena.walls[0].min = {0.0F, 0.0F, 0.0F};
  faceArena.walls[0].max = {2.0F, 2.0F, 1.0F};
  const std::uint32_t faceMaterial = lg::arenaMaterialId("test/face");
  faceArena.walls[0].materialId = faceMaterial;
  const lg::Scene3D faceScene = lg::buildStaticWorldScene(faceArena);
  const bool stableFaceData = std::all_of(
    faceScene.vertices.begin(),
    faceScene.vertices.end(),
    [faceMaterial](const lg::Vertex3D& vertex) {
      if (vertex.materialId != faceMaterial) {
        return true;
      }
      return finiteVec3(vertex.normal) &&
        lg::length(vertex.normal) > 0.99F &&
        vertex.materialSlot == faceMaterial;
    }
  );
  failures += expect(
    std::any_of(
      faceScene.vertices.begin(),
      faceScene.vertices.end(),
      [faceMaterial](const lg::Vertex3D& vertex) {
        return vertex.materialId == faceMaterial;
      }
    ) && stableFaceData,
    "static world faces should retain unit normals and material slots"
  );

  lg::Arena arena;
  arena.wallCount = 0;
  lg::PlayerState player;
  player.position = {1.0F, 2.0F, 0.9F};
  player.viewYawRadians = 0.0F;
  player.viewPitchRadians = 0.0F;
  player.movementMode = lg::MovementMode::Grounded;
  player.onGround = true;
  lg::PlayerState opponent;
  opponent.position = {4.0F, 2.0F, 0.9F};
  opponent.movementMode = lg::MovementMode::Grounded;
  opponent.onGround = true;
  lg::RenderSettings settings;
  settings.enemyOutlineRed = 31;
  settings.enemyOutlineGreen = 227;
  settings.enemyOutlineBlue = 19;
  settings.playerModel = 1;
  settings.contactShadowsEnabled = true;
  lg::LightningGunResult inactiveBeam;
  const std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> weaponFires = {};
  const std::array<lg::RocketExplosionResult, lg::kDuelPlayerCount> rocketExplosions = {};
  const std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> rockets = {};

  const lg::Scene3D baseScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    !baseScene.gltfPlayerModelInstances.empty() ||
      !baseScene.staticMeshInstances.empty() ||
      !baseScene.vertices.empty(),
    "perspective scene should emit renderable geometry or GPU model instances"
  );
  failures += expect(
    settings.playerOutlineStyle == lg::PlayerOutlineStyle::ScreenSpace &&
      !lg::usesGeometryPlayerOutlineFallback(settings.playerOutlineStyle),
    "SDL_GPU outline settings should prefer screen-space outlines over geometry fallback"
  );
  failures += expect(
    baseScene.visibleRemotePlayers == 1 &&
      baseScene.remoteBodyModelsBuilt == 1 &&
      baseScene.remoteWeaponModelsBuilt == 1 &&
      baseScene.playerOutlinesBuilt == 1 &&
      baseScene.outlinedPlayers == 1 &&
      baseScene.outlineMaskDraws.size() == 1U &&
      baseScene.normalPlayerBodyDynamicVertices == 0 &&
      baseScene.gltfPlayerModelStats.activeInstances == 1 &&
      baseScene.gltfPlayerModelStats.gpuSkinnedInstances == 1 &&
      baseScene.gltfPlayerModelStats.bodyBatches > 0 &&
      baseScene.gltfPlayerModelStats.legacyCpuSkinnedVertexUploadBytes == 0 &&
      baseScene.geometryOutlineDynamicVertices == 0 &&
      !baseScene.geometryOutlineFallbackUsed &&
      baseScene.remoteWeaponStats.instancesSubmitted == 1 &&
      baseScene.remoteWeaponStats.legacyDynamicVertices == 0,
    "GLB render settings should build visible remote body, remote weapon instance, and screen-space outline mask input"
  );
  lg::Arena sharedLightArena = arena;
  sharedLightArena.sunLight.enabled = true;
  sharedLightArena.sunLight.direction = {0.25F, -0.40F, -0.88F};
  sharedLightArena.sunLight.color = {0.82F, 0.90F, 1.0F};
  sharedLightArena.sunLight.intensity = 0.64F;
  sharedLightArena.ambientLight.color = {0.75F, 0.85F, 1.0F};
  sharedLightArena.ambientLight.intensity = 0.38F;
  lg::RenderSettings sharedLightSettings = settings;
  sharedLightSettings.materialQuality = 2;
  const lg::Scene3D sharedLightScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, sharedLightArena, player, opponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets,
    sharedLightSettings
  );
  failures += expect(
    nearlyEqual(sharedLightScene.lights.sunIntensity, 0.64F) &&
      nearlyEqual(sharedLightScene.lights.sunColor.x, 0.82F) &&
      nearlyEqual(sharedLightScene.lights.fillColor.x, 0.225F) &&
      nearlyEqual(sharedLightScene.lights.fillColor.y, 0.306F) &&
      nearlyEqual(sharedLightScene.lights.fillColor.z, 0.46F) &&
      nearlyEqual(sharedLightScene.lights.fillIntensity, 0.38F) &&
      sharedLightScene.lights.materialQuality == 2,
    "world, player, and weapon passes should share one scene light record"
  );
  failures += expect(
    baseScene.contactShadowVertices.size() == 48U &&
      baseScene.contactShadowVertices.front().color.alpha > 0 &&
      baseScene.contactShadowVertices[1].color.alpha == 0,
    "a grounded visible remote player should emit one soft contact shadow"
  );
  lg::RenderSettings noContactShadowSettings = settings;
  noContactShadowSettings.contactShadowsEnabled = false;
  const lg::Scene3D noContactShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, opponent, inactiveBeam, inactiveBeam,
    weaponFires, rocketExplosions, rockets, noContactShadowSettings
  );
  failures += expect(
    noContactShadowScene.contactShadowVertices.empty(),
    "disabled contact shadows should not emit blob geometry"
  );

  lg::PlayerState midHeightOpponent = opponent;
  midHeightOpponent.position.z += 0.75F;
  midHeightOpponent.onGround = false;
  midHeightOpponent.movementMode = lg::MovementMode::Airborne;
  const lg::Scene3D midHeightShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, midHeightOpponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, settings
  );
  lg::PlayerState nearCutoffOpponent = midHeightOpponent;
  nearCutoffOpponent.position.z = opponent.position.z + 1.49F;
  const lg::Scene3D nearCutoffShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, nearCutoffOpponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, settings
  );
  lg::PlayerState cutoffOpponent = midHeightOpponent;
  cutoffOpponent.position.z = opponent.position.z + 1.5F;
  const lg::Scene3D cutoffShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, cutoffOpponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, settings
  );
  failures += expect(
    midHeightShadowScene.contactShadowVertices.size() == 48U &&
      nearCutoffShadowScene.contactShadowVertices.size() == 48U &&
      midHeightShadowScene.contactShadowVertices.front().color.alpha <
        baseScene.contactShadowVertices.front().color.alpha &&
      midHeightShadowScene.contactShadowVertices.front().color.alpha == 41U &&
      nearCutoffShadowScene.contactShadowVertices.front().color.alpha > 0U &&
      nearCutoffShadowScene.contactShadowVertices.front().color.alpha <
        midHeightShadowScene.contactShadowVertices.front().color.alpha &&
      nearlyEqual(
        contactShadowMaxRadius(baseScene),
        contactShadowMaxRadius(midHeightShadowScene)
      ) &&
      nearlyEqual(
        contactShadowMaxRadius(baseScene),
        contactShadowMaxRadius(nearCutoffShadowScene)
      ) &&
      cutoffShadowScene.contactShadowVertices.empty(),
    "airborne contact shadows should keep their hitbox radius, fade smoothly, and stop at the 1.5-unit cutoff"
  );

  lg::PlayerState noGroundOpponent = opponent;
  noGroundOpponent.position.z = 4.0F;
  noGroundOpponent.onGround = true;
  const lg::Scene3D noGroundShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, noGroundOpponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, settings
  );
  failures += expect(
    noGroundShadowScene.contactShadowVertices.empty(),
    "a stale grounded flag without a receiving surface in range should not emit a shadow"
  );

  lg::Arena hiddenFloorArena = arena;
  hiddenFloorArena.renderDefaultFloor = false;
  const lg::Scene3D hiddenFloorShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, hiddenFloorArena, player, opponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, settings
  );
  lg::Arena playerClipFloorArena = hiddenFloorArena;
  playerClipFloorArena.min.z = -4.0F;
  playerClipFloorArena.wallCount = 1;
  playerClipFloorArena.walls[0].min = {3.0F, 1.0F, -1.0F};
  playerClipFloorArena.walls[0].max = {5.0F, 3.0F, 0.0F};
  playerClipFloorArena.walls[0].collisionKind =
    lg::ArenaCollisionKind::PlayerClip;
  playerClipFloorArena.walls[0].renderable = false;
  const lg::Scene3D playerClipFloorShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, playerClipFloorArena, player, opponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, settings
  );
  lg::Arena slopedPlayerClipArena = arena;
  slopedPlayerClipArena.min.z = -4.0F;
  slopedPlayerClipArena.brushCount = 1;
  slopedPlayerClipArena.brushes[0] =
    slopedTopBrush(2.0F, 6.0F, -0.5F, 0.5F);
  slopedPlayerClipArena.brushes[0].collisionKind =
    lg::ArenaCollisionKind::PlayerClip;
  slopedPlayerClipArena.brushes[0].renderable = false;
  const lg::ArenaBrushFace& playerClipSlopeTop =
    slopedPlayerClipArena.brushes[0].faces[5];
  lg::PlayerState slopedPlayerClipOpponent = opponent;
  const float playerClipSlopeSupport =
    slopedPlayerClipOpponent.bounds.radius *
      std::hypot(
        playerClipSlopeTop.normal.x,
        playerClipSlopeTop.normal.y
      ) +
    slopedPlayerClipOpponent.bounds.halfHeight *
      std::fabs(playerClipSlopeTop.normal.z);
  slopedPlayerClipOpponent.position.z =
    (
      playerClipSlopeTop.distance +
      playerClipSlopeSupport -
      (playerClipSlopeTop.normal.x * slopedPlayerClipOpponent.position.x) -
      (playerClipSlopeTop.normal.y * slopedPlayerClipOpponent.position.y)
    ) / playerClipSlopeTop.normal.z;
  const lg::Scene3D slopedPlayerClipShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, slopedPlayerClipArena, player,
    slopedPlayerClipOpponent, inactiveBeam, inactiveBeam, weaponFires,
    rocketExplosions, rockets, settings
  );
  failures += expect(
    hiddenFloorShadowScene.contactShadowVertices.empty() &&
      playerClipFloorShadowScene.contactShadowVertices.empty() &&
      slopedPlayerClipShadowScene.contactShadowVertices.empty(),
    "arena bounds and flat or sloped non-rendered playerclip should not receive contact shadows"
  );

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> hiddenShadowPlayers = {};
  hiddenShadowPlayers[1].player = opponent;
  hiddenShadowPlayers[1].visible = false;
  const lg::Scene3D hiddenShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, hiddenShadowPlayers, inactiveBeam,
    weaponFires, rocketExplosions, rockets, settings
  );
  failures += expect(
    hiddenShadowScene.contactShadowVertices.empty(),
    "hidden remote players should not emit contact shadows"
  );

  lg::Arena slopeArena = arena;
  slopeArena.brushCount = 1;
  slopeArena.brushes[0] = slopedTopBrush(2.0F, 6.0F, 0.5F, 1.5F);
  const lg::ArenaBrushFace& slopeTop = slopeArena.brushes[0].faces[5];
  lg::PlayerState slopeOpponent = opponent;
  const float slopePlanarSupport =
    slopeOpponent.bounds.radius *
    std::hypot(slopeTop.normal.x, slopeTop.normal.y);
  const float slopeExpandedDistance =
    slopeTop.distance +
    slopePlanarSupport +
    (slopeOpponent.bounds.halfHeight * std::fabs(slopeTop.normal.z));
  slopeOpponent.position.z =
    (
      slopeExpandedDistance -
      (slopeTop.normal.x * slopeOpponent.position.x) -
      (slopeTop.normal.y * slopeOpponent.position.y)
    ) / slopeTop.normal.z;
  const lg::Scene3D slopeShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, slopeArena, player, slopeOpponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, settings
  );
  const bool slopeVerticesOnPlane =
    slopeShadowScene.contactShadowVertices.size() == 48U &&
    std::all_of(
      slopeShadowScene.contactShadowVertices.begin(),
      slopeShadowScene.contactShadowVertices.end(),
      [&](const lg::Vertex3D& vertex) {
        const lg::Vec3 surfacePoint =
          vertex.position - (slopeTop.normal * 0.008F);
        return nearlyEqual(
          lg::dot(slopeTop.normal, surfacePoint),
          slopeTop.distance,
          0.002F
        );
      }
    );
  const bool slopeWindingMatchesSurface =
    slopeShadowScene.contactShadowVertices.size() >= 3U &&
    lg::dot(
      lg::normalize(crossProduct(
        slopeShadowScene.contactShadowVertices[1].position -
          slopeShadowScene.contactShadowVertices[0].position,
        slopeShadowScene.contactShadowVertices[2].position -
          slopeShadowScene.contactShadowVertices[0].position
      )),
      slopeTop.normal
    ) > 0.99F;
  failures += expect(
    slopeVerticesOnPlane && slopeWindingMatchesSurface,
    "a grounded slope shadow should follow the hit plane and keep the surface-facing winding"
  );

  lg::PlayerState wideOpponent = opponent;
  wideOpponent.bounds.radius *= 1.5F;
  const lg::Scene3D wideShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, wideOpponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, settings
  );
  failures += expect(
    wideShadowScene.contactShadowVertices.size() == 48U &&
      contactShadowMaxRadius(wideShadowScene) >
        contactShadowMaxRadius(baseScene) * 1.49F,
    "contact-shadow size should follow the player's collision radius"
  );

  lg::Arena sunShadowArena = arena;
  sunShadowArena.sunLight.enabled = true;
  lg::RenderSettings sunShadowSettings = settings;
  sunShadowSettings.sunShadowQuality = 2;
  const lg::Scene3D sunAndContactShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, sunShadowArena, player, opponent, inactiveBeam,
    inactiveBeam, weaponFires, rocketExplosions, rockets, sunShadowSettings
  );
  failures += expect(
    sunAndContactShadowScene.lights.shadow.mapSize == 2048U &&
      sunAndContactShadowScene.gltfPlayerModelStats.shadowCasterInstances == 1U &&
      sunAndContactShadowScene.gltfPlayerModelStats.shadowCasterDrawCalls > 0U &&
      sunAndContactShadowScene.contactShadowVertices.size() == 48U &&
      sunAndContactShadowScene.contactShadowVertices.front().color.alpha <
        baseScene.contactShadowVertices.front().color.alpha &&
      nearlyEqual(
        contactShadowMaxRadius(sunAndContactShadowScene),
        contactShadowMaxRadius(baseScene)
      ),
    "true glTF sun shadows should remain active while the fixed-size contact oval uses lower alpha"
  );

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> cappedShadowPlayers = {};
  for (std::size_t index = 0; index < cappedShadowPlayers.size(); ++index) {
    cappedShadowPlayers[index].player = opponent;
    cappedShadowPlayers[index].visible = true;
  }
  lg::RenderSettings cappedShadowSettings = settings;
  cappedShadowSettings.playerModel = 0;
  cappedShadowSettings.drawRemoteWeapons = false;
  cappedShadowSettings.drawPlayerOutlines = false;
  cappedShadowSettings.frustumCullRemotePlayers = false;
  const lg::Scene3D cappedShadowScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, cappedShadowPlayers, inactiveBeam,
    weaponFires, rocketExplosions, rockets, cappedShadowSettings
  );
  constexpr std::size_t kExpectedCappedShadowVertices =
    lg::kDuelPlayerCount * 16U * 3U;
  failures += expect(
    cappedShadowScene.contactShadowVertices.size() ==
        kExpectedCappedShadowVertices &&
      cappedShadowScene.contactShadowVertices.capacity() >=
        kExpectedCappedShadowVertices,
    "the fixed remote-player cap should bound contact shadows to one reserved 768-vertex buffer"
  );

  lg::RenderSettings workerSettings = settings;
  workerSettings.playerModel = 2;
  const lg::Scene3D workerScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, opponent, inactiveBeam, inactiveBeam,
    weaponFires, rocketExplosions, rockets, workerSettings
  );
  failures += expect(
    lg::workerPlayerModel().loaded() &&
      workerScene.gltfPlayerModelStats.activeInstances == 1U &&
      workerScene.gltfPlayerModelStats.gpuSkinnedInstances == 1U &&
      workerScene.remoteWeaponStats.instancesSubmitted == 1U,
    "Worker selection should load a skinned body and retain the remote weapon"
  );
  lg::GltfSkinnedModel::PoseScratch workerPoseScratch;
  std::vector<std::array<float, 16>> workerPalette;
  lg::GltfSkinnedModel::Matrix4 workerSocket;
  const bool workerSocketSampled = lg::workerPlayerModel().appendBonePalette(
    {{"Idle_Gun_TwoHanded", 0.8333333F, 1.0F}},
    workerPalette,
    workerPoseScratch
  ) && lg::workerPlayerModel().nodeGlobalMatrix(
    "weapon_socket", workerPoseScratch, workerSocket
  );
  failures += expect(
    workerSocketSampled &&
      std::all_of(
        workerSocket.values.begin(), workerSocket.values.end(),
        [](float value) { return std::isfinite(value); }
      ),
    "Worker two-handed pose should expose a finite animated weapon socket"
  );

  lg::RenderSettings noWeaponSettings = settings;
  noWeaponSettings.drawRemoteWeapons = false;
  const lg::Scene3D noWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    noWeaponSettings
  );
  failures += expect(
    noWeaponScene.visibleRemotePlayers == 1 &&
      noWeaponScene.remoteBodyModelsBuilt == 1 &&
      noWeaponScene.remoteWeaponModelsBuilt == 0 &&
      noWeaponScene.remoteWeaponStats.instancesSubmitted == 0 &&
      noWeaponScene.playerOutlinesBuilt == 1 &&
      noWeaponScene.outlineMaskDraws.size() == 1U,
    "disabled remote weapons should prevent only remote weapon model construction"
  );

  lg::RenderSettings noBodySettings = settings;
  noBodySettings.drawRemotePlayers = false;
  lg::LightningGunResult activeOpponentBeam;
  activeOpponentBeam.active = true;
  activeOpponentBeam.start = opponent.position;
  activeOpponentBeam.end = player.position;
  const lg::Scene3D noBodyNoBeamScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    noBodySettings
  );
  const lg::Scene3D noBodyBeamScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    activeOpponentBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    noBodySettings
  );
  failures += expect(
    noBodyBeamScene.visibleRemotePlayers == 1 &&
      noBodyBeamScene.remoteBodyModelsBuilt == 0 &&
      noBodyBeamScene.remoteWeaponModelsBuilt == 1 &&
      noBodyBeamScene.remoteWeaponStats.instancesSubmitted == 1 &&
      noBodyBeamScene.playerOutlinesBuilt == 0 &&
      noBodyBeamScene.outlineMaskDraws.empty() &&
      noBodyBeamScene.contactShadowVertices.empty() &&
      noBodyBeamScene.vertices.size() > noBodyNoBeamScene.vertices.size(),
    "disabled remote bodies should not suppress unrelated remote effects or scene data"
  );

  const lg::GltfSkinnedModel& duelistModel = lg::duelistMaleModel();
  bool primitivesFiniteAndSafe = duelistModel.loaded() &&
    !duelistModel.primitives().empty() &&
    duelistModel.hasSkin() &&
    duelistModel.hasSkinnedPrimitives() &&
    duelistModel.jointCount() > 0U;
  std::uint32_t primitiveVertexCount = 0;
  std::uint32_t primitiveIndexCount = 0;
  bool foundTintedClothPrimitive = false;
  bool foundUntintedNonClothPrimitive = false;
  for (const lg::GltfSkinnedModel::Primitive& primitive : duelistModel.primitives()) {
    primitivesFiniteAndSafe = primitivesFiniteAndSafe &&
      !primitive.vertices.empty() &&
      !primitive.indices.empty() &&
      finiteVec3(primitive.localBounds.min) &&
      finiteVec3(primitive.localBounds.max) &&
      primitive.localBounds.min.x <= primitive.localBounds.max.x &&
      primitive.localBounds.min.y <= primitive.localBounds.max.y &&
      primitive.localBounds.min.z <= primitive.localBounds.max.z;
    primitiveVertexCount += static_cast<std::uint32_t>(primitive.vertices.size());
    primitiveIndexCount += static_cast<std::uint32_t>(primitive.indices.size());
    bool primitiveHasTintedVertex = false;
    bool primitiveHasUntintedVertex = false;
    for (const lg::GltfSkinnedModel::GpuVertex& vertex : primitive.vertices) {
      primitiveHasTintedVertex = primitiveHasTintedVertex || vertex.tintWeight > 0U;
      primitiveHasUntintedVertex = primitiveHasUntintedVertex || vertex.tintWeight == 0U;
      float weightSum = 0.0F;
      for (std::size_t influence = 0; influence < vertex.weights.size(); ++influence) {
        const float weight = vertex.weights[influence];
        primitivesFiniteAndSafe = primitivesFiniteAndSafe &&
          std::isfinite(weight) &&
          weight >= 0.0F &&
          vertex.joints[influence] < duelistModel.jointCount();
        weightSum += weight;
      }
      primitivesFiniteAndSafe = primitivesFiniteAndSafe &&
        std::isfinite(vertex.position.x) &&
        std::isfinite(vertex.position.y) &&
        std::isfinite(vertex.position.z) &&
        std::isfinite(vertex.normal.x) &&
        std::isfinite(vertex.normal.y) &&
        std::isfinite(vertex.normal.z) &&
        (weightSum == 0.0F || nearlyEqual(weightSum, 1.0F, 0.001F));
    }
    foundTintedClothPrimitive =
      foundTintedClothPrimitive || (primitive.tintable && primitiveHasTintedVertex);
    foundUntintedNonClothPrimitive =
      foundUntintedNonClothPrimitive || (!primitive.tintable && primitiveHasUntintedVertex);
  }
  failures += expect(
    primitivesFiniteAndSafe &&
      primitiveVertexCount > 0U &&
      primitiveIndexCount > 0U &&
      foundTintedClothPrimitive &&
      foundUntintedNonClothPrimitive,
    "GLB duelist asset should load finite primitives with normalized weights and material tint masks"
  );
  constexpr std::array<std::string_view, 7> presentationClips = {{
    "RUN_BACK",
    "STRAFE_LEFT",
    "STRAFE_RIGHT",
    "START_FORWARD",
    "STOP_FORWARD",
    "LAND_LIGHT",
    "LAND_HEAVY",
  }};
  failures += expect(
    std::all_of(
      presentationClips.begin(),
      presentationClips.end(),
      [&duelistModel](std::string_view expected) {
        return std::find(
          duelistModel.animationNames().begin(),
          duelistModel.animationNames().end(),
          expected
        ) != duelistModel.animationNames().end();
      }
    ),
    "runtime GLB should contain every authored presentation clip"
  );
  failures += expect(
    baseScene.gltfPlayerModelStats.staticMeshGpuBytes == primitiveVertexCount * 64U &&
      baseScene.gltfPlayerModelStats.staticIndexGpuBytes == primitiveIndexCount * 4U &&
      baseScene.gltfPlayerModelStats.poseUploadBytes ==
        duelistModel.jointCount() * 64U,
    "GLB render metrics should report resident static bytes and compact per-player pose bytes"
  );
  const std::vector<lg::SkinnedModelTriangle> restPoseTriangles =
    duelistModel.triangles({});
  lg::Vec3 restMin = {
    std::numeric_limits<float>::max(),
    std::numeric_limits<float>::max(),
    std::numeric_limits<float>::max(),
  };
  lg::Vec3 restMax = {
    std::numeric_limits<float>::lowest(),
    std::numeric_limits<float>::lowest(),
    std::numeric_limits<float>::lowest(),
  };
  for (const lg::SkinnedModelTriangle& triangle : restPoseTriangles) {
    for (lg::Vec3 vertex : triangle.vertices) {
      restMin.x = std::min(restMin.x, vertex.x);
      restMin.y = std::min(restMin.y, vertex.y);
      restMin.z = std::min(restMin.z, vertex.z);
      restMax.x = std::max(restMax.x, vertex.x);
      restMax.y = std::max(restMax.y, vertex.y);
      restMax.z = std::max(restMax.z, vertex.z);
    }
  }
  const bool restPoseCompact =
    duelistModel.loaded() &&
    !restPoseTriangles.empty() &&
    restMin.x > -0.50F && restMax.x < 0.50F &&
    restMin.y > -0.02F && restMax.y < 1.72F &&
    restMin.z > -0.22F && restMax.z < 0.30F;
  if (!restPoseCompact) {
    std::cerr << "rest bounds min=(" << restMin.x << ", " << restMin.y << ", "
              << restMin.z << ") max=(" << restMax.x << ", " << restMax.y
              << ", " << restMax.z << ")\n";
  }
  failures += expect(
    restPoseCompact,
    "GLB duelist bind pose should resolve child nodes after their parents"
  );
  {
    lg::GltfSkinnedModel::PoseScratch runScratch;
    lg::GltfSkinnedModel::PoseScratch leanScratch;
    lg::GltfSkinnedModel::PoseScratch aimScratch;
    lg::GltfSkinnedModel::PoseScratch backScratch;
    lg::GltfSkinnedModel::PoseScratch leftScratch;
    lg::GltfSkinnedModel::PoseScratch rightScratch;
    std::vector<std::array<float, 16>> runPalette;
    std::vector<std::array<float, 16>> leanPalette;
    std::vector<std::array<float, 16>> aimPalette;
    std::vector<std::array<float, 16>> backPalette;
    std::vector<std::array<float, 16>> leftPalette;
    std::vector<std::array<float, 16>> rightPalette;
    const bool runSampled = duelistModel.appendBonePalette(
      {{"RUN", 0.25F, 1.0F}},
      runPalette,
      runScratch
    );
    const bool leanSampled = duelistModel.appendBonePalette(
      {{
        {"RUN", 0.25F, 1.0F},
        {"LEAN_LEFT", 0.5833333F, 1.0F, lg::SkinnedModelPoseMask::UpperBody},
      }},
      leanPalette,
      leanScratch
    );
    const bool aimSampled = duelistModel.appendBonePalette(
      {{"RUN", 0.25F, 1.0F}},
      aimPalette,
      aimScratch,
      0.78539816F
    );
    const bool backSampled = duelistModel.appendBonePalette(
      {{"RUN_BACK", 0.25F, 1.0F}}, backPalette, backScratch
    );
    const bool leftSampled = duelistModel.appendBonePalette(
      {{"STRAFE_LEFT", 0.25F, 1.0F}}, leftPalette, leftScratch
    );
    const bool rightSampled = duelistModel.appendBonePalette(
      {{"STRAFE_RIGHT", 0.25F, 1.0F}}, rightPalette, rightScratch
    );
    constexpr std::array<std::size_t, 6> legJoints = {{
      14U, 15U, 16U, 17U, 18U, 19U,
    }};
    constexpr std::array<std::size_t, 8> upperJoints = {{
      2U, 3U, 4U, 5U, 6U, 7U, 9U, 10U,
    }};
    failures += expect(
      runSampled &&
        leanSampled &&
        runPalette.size() == duelistModel.jointCount() &&
        leanPalette.size() == duelistModel.jointCount() &&
        maxPaletteDeltaAtIndices(runPalette, leanPalette, legJoints) <= 0.0001F &&
        maxPaletteDeltaAtIndices(runPalette, leanPalette, upperJoints) > 0.001F,
      "upper-body lean layer should preserve run leg joints while changing torso/arm joints"
    );
    failures += expect(
      aimSampled &&
        aimPalette.size() == duelistModel.jointCount() &&
        maxPaletteDeltaAtIndices(runPalette, aimPalette, legJoints) <= 0.0001F &&
        maxPaletteDeltaAtIndices(runPalette, aimPalette, upperJoints) > 0.001F,
      "aim pitch should change the upper-body palette without tilting locomotion legs"
    );
    failures += expect(
      backSampled && leftSampled && rightSampled &&
        maxPaletteDelta(runPalette, backPalette) > 0.001F &&
        maxPaletteDelta(runPalette, leftPalette) > 0.001F &&
        maxPaletteDelta(runPalette, rightPalette) > 0.001F &&
        maxPaletteDelta(leftPalette, rightPalette) > 0.001F,
      "authored directional clips should produce visibly distinct bone palettes"
    );
  }
  failures += expect(
    nearlyEqual(baseScene.camera.position.x, player.position.x) &&
      nearlyEqual(baseScene.camera.position.y, player.position.y) &&
      nearlyEqual(baseScene.camera.right.z, 0.0F) &&
      baseScene.camera.position.z > player.position.z &&
      nearlyEqual(baseScene.camera.forward.x, 1.0F),
    "scene camera should use the local player's first-person view"
  );

  arena.wallCount = 1;
  arena.walls[0] = {{3.0F, 1.0F, 0.0F}, {5.0F, 3.0F, 2.0F}};
  arena.walls[0].materialId =
    lg::arenaMaterialId("512x512/Brick/Brick_14-512x512");
  const lg::Scene3D texturedWallScene = lg::buildStaticWorldScene(arena);
  bool foundTexturedWallVertex = false;
  bool foundWallUvSpan = false;
  bool foundPrototypeWallAccent = false;
  float minimumWallU = 0.0F;
  float maximumWallU = 0.0F;
  float minimumWallV = 0.0F;
  float maximumWallV = 0.0F;
  float firstWallU = 0.0F;
  float firstWallV = 0.0F;
  bool capturedFirstWallUv = false;
  for (const lg::Vertex3D& vertex : texturedWallScene.vertices) {
    if (
      vertex.materialId == 0U &&
      (
        sameColor(vertex.color, {86, 176, 96, 255}) ||
        sameColor(vertex.color, {171, 235, 145, 255}) ||
        sameColor(vertex.color, {109, 195, 105, 255})
      )
    ) {
      foundPrototypeWallAccent = true;
    }
    if (vertex.materialId != arena.walls[0].materialId) {
      continue;
    }
    foundTexturedWallVertex = true;
    if (!capturedFirstWallUv) {
      firstWallU = vertex.u;
      firstWallV = vertex.v;
      minimumWallU = vertex.u;
      maximumWallU = vertex.u;
      minimumWallV = vertex.v;
      maximumWallV = vertex.v;
      capturedFirstWallUv = true;
      continue;
    }
    minimumWallU = std::min(minimumWallU, vertex.u);
    maximumWallU = std::max(maximumWallU, vertex.u);
    minimumWallV = std::min(minimumWallV, vertex.v);
    maximumWallV = std::max(maximumWallV, vertex.v);
    foundWallUvSpan = foundWallUvSpan ||
      !nearlyEqual(vertex.u, firstWallU) ||
      !nearlyEqual(vertex.v, firstWallV);
  }
  failures += expect(
    foundTexturedWallVertex,
    "wall scene geometry should preserve the wall material id"
  );
  failures += expect(
    foundWallUvSpan,
    "wall scene geometry should emit varying texture coordinates"
  );
  failures += expect(
    maximumWallU - minimumWallU > 0.1F || maximumWallV - minimumWallV > 0.1F,
    "wall texture coordinates should use TrenchBroom scale instead of stretched LG units"
  );
  failures += expect(
    !foundPrototypeWallAccent,
    "wall scene geometry should not emit old green prototype accents over textures"
  );
  arena.wallCount = 0;

  {
    lg::Arena floorArena;
    floorArena.min = {-2.0F, -2.0F, -2.0F};
    floorArena.max = {2.0F, 2.0F, 2.0F};
    floorArena.wallCount = 1;
    floorArena.walls[0] = {{-1.0F, -1.0F, -0.2F}, {1.0F, 1.0F, 0.0F}};
    floorArena.walls[0].materialId = lg::arenaMaterialId("test/imported-floor");
    const lg::Scene3D withDefaultFloor = lg::buildStaticWorldScene(floorArena);
    floorArena.renderDefaultFloor = false;
    const lg::Scene3D importedFloor = lg::buildStaticWorldScene(floorArena);
    const bool keptImportedFloor = std::any_of(
      importedFloor.vertices.begin(), importedFloor.vertices.end(),
      [&](const lg::Vertex3D& vertex) {
        return vertex.materialId == floorArena.walls[0].materialId;
      }
    );
    failures += expect(
      importedFloor.vertices.size() < withDefaultFloor.vertices.size() &&
        keptImportedFloor,
      "imported maps should omit the default z=0 floor but keep their source floor"
    );
  }

  {
    lg::Arena clipArena;
    const lg::Scene3D baselineStaticScene = lg::buildStaticWorldScene(clipArena);
    clipArena.wallCount = 1;
    clipArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    clipArena.walls[0].max = {1.0F, 1.0F, 1.0F};
    clipArena.walls[0].materialId = lg::arenaMaterialId("common/playerclip");
    clipArena.walls[0].renderable = false;
    const lg::Scene3D clippedStaticScene = lg::buildStaticWorldScene(clipArena);
    bool foundPlayerClipMaterial = false;
    for (const lg::Vertex3D& vertex : clippedStaticScene.vertices) {
      foundPlayerClipMaterial =
        foundPlayerClipMaterial || vertex.materialId == clipArena.walls[0].materialId;
    }
    failures += expect(
      clippedStaticScene.vertices.size() == baselineStaticScene.vertices.size() &&
        !foundPlayerClipMaterial,
      "non-renderable playerclip walls should not emit static world geometry"
    );
  }

  {
    lg::Arena skyArena;
    skyArena.wallCount = 1;
    skyArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    skyArena.walls[0].max = {1.0F, 1.0F, 1.0F};
    skyArena.walls[0].materialId = lg::arenaMaterialId("test/wall");
    const lg::Scene3D completeWall =
      lg::buildStaticWorldScene(skyArena);
    skyArena.walls[0].faceSurfaceKinds[3] =
      lg::ArenaSurfaceKind::Sky;
    const lg::Scene3D wallWithSky =
      lg::buildStaticWorldScene(skyArena);
    failures += expect(
      completeWall.vertices.size() == wallWithSky.vertices.size() + 6U,
      "one sky wall face should emit no static world triangles"
    );

    skyArena.wallCount = 0;
    skyArena.brushCount = 1;
    lg::ArenaBrush& brush = skyArena.brushes[0];
    brush.vertexCount = 3;
    brush.vertices[0] = {0.0F, 0.0F, 0.0F};
    brush.vertices[1] = {1.0F, 0.0F, 0.0F};
    brush.vertices[2] = {0.0F, 1.0F, 0.0F};
    brush.faceCount = 1;
    brush.faces[0].vertexCount = 3;
    brush.faces[0].vertices = {0U, 1U, 2U};
    brush.faces[0].normal = {0.0F, 0.0F, 1.0F};
    brush.faces[0].materialId = lg::arenaMaterialId("test/brush");
    const lg::Scene3D completeBrush =
      lg::buildStaticWorldScene(skyArena);
    brush.faces[0].surfaceKind = lg::ArenaSurfaceKind::Sky;
    const lg::Scene3D brushWithSky =
      lg::buildStaticWorldScene(skyArena);
    failures += expect(
      completeBrush.vertices.size() == brushWithSky.vertices.size() + 3U,
      "one sky convex face should enter no static world mesh"
    );
  }

  {
    lg::Arena visualArena;
    const lg::Scene3D baselineStaticScene = lg::buildStaticWorldScene(visualArena);
    visualArena.visualWallCount = 1;
    visualArena.visualWalls[0].min = {0.0F, 0.0F, 0.0F};
    visualArena.visualWalls[0].max = {1.0F, 1.0F, 1.0F};
    visualArena.visualWalls[0].materialId = lg::arenaMaterialId("patch/arch");
    visualArena.visualWalls[0].sourcePatchIndex = 46U;
    const lg::Scene3D visualStaticScene = lg::buildStaticWorldScene(visualArena);
    bool foundVisualMaterial = false;
    for (const lg::Vertex3D& vertex : visualStaticScene.vertices) {
      foundVisualMaterial = foundVisualMaterial ||
        vertex.materialId == visualArena.visualWalls[0].materialId;
    }
    failures += expect(
      visualStaticScene.vertices.size() > baselineStaticScene.vertices.size() &&
        foundVisualMaterial,
      "visual-only walls should emit textured static world geometry"
    );

    visualArena.visualBrushCount = 1;
    lg::ArenaBrush& visualBrush = visualArena.visualBrushes[0];
    visualBrush.vertexCount = 3;
    visualBrush.vertices[0] = {2.0F, 0.0F, 0.0F};
    visualBrush.vertices[1] = {3.0F, 0.0F, 0.0F};
    visualBrush.vertices[2] = {2.0F, 1.0F, 0.0F};
    visualBrush.faceCount = 1;
    visualBrush.faces[0].vertexCount = 3;
    visualBrush.faces[0].vertices = {0U, 1U, 2U};
    visualBrush.faces[0].normal = {0.0F, 0.0F, 1.0F};
    visualBrush.faces[0].materialId = lg::arenaMaterialId("patch/curve");
    const lg::Scene3D visualBrushScene = lg::buildStaticWorldScene(visualArena);
    const bool foundVisualBrush = std::any_of(
      visualBrushScene.vertices.begin(),
      visualBrushScene.vertices.end(),
      [&visualBrush](const lg::Vertex3D& vertex) {
        return vertex.materialId == visualBrush.faces[0].materialId;
      }
    );
    failures += expect(
      foundVisualBrush,
      "visual-only convex brushes should emit textured static world geometry"
    );

    lg::Scene3D collisionDebug;
    lg::appendCollisionDebugGeometry(collisionDebug, visualArena, 1);
    failures += expect(
      collisionDebug.translucentVertices.empty(),
      "visual-only geometry should stay absent from r_show_collision"
    );
  }

  {
    lg::Arena debugArena;
    debugArena.wallCount = 3;
    for (std::size_t index = 0; index < debugArena.wallCount; ++index) {
      debugArena.walls[index].min = {static_cast<float>(index) * 2.0F, 0.0F, 0.0F};
      debugArena.walls[index].max = {static_cast<float>(index) * 2.0F + 1.0F, 1.0F, 1.0F};
    }
    debugArena.walls[0].collisionKind = lg::ArenaCollisionKind::VisibleSolid;
    debugArena.walls[1].collisionKind = lg::ArenaCollisionKind::PlayerClip;
    debugArena.walls[2].collisionKind = lg::ArenaCollisionKind::WeaponClip;
    debugArena.jumpPadCount = 1;
    debugArena.jumpPads[0].min = {0.0F, 2.0F, 0.0F};
    debugArena.jumpPads[0].max = {1.0F, 3.0F, 1.0F};
    debugArena.teleportCount = 1;
    debugArena.teleports[0].min = {2.0F, 2.0F, 0.0F};
    debugArena.teleports[0].max = {3.0F, 3.0F, 1.0F};
    debugArena.mcguffin.hasRedBase = true;
    debugArena.mcguffin.redBase.min = {4.0F, 2.0F, 0.0F};
    debugArena.mcguffin.redBase.max = {5.0F, 3.0F, 1.0F};
    debugArena.mcguffin.hasBlueBase = true;
    debugArena.mcguffin.blueBase.min = {6.0F, 2.0F, 0.0F};
    debugArena.mcguffin.blueBase.max = {7.0F, 3.0F, 1.0F};

    lg::Scene3D allCollision;
    lg::appendCollisionDebugGeometry(allCollision, debugArena, 1);
    bool foundBlue = false;
    bool foundGreen = false;
    bool foundOrange = false;
    bool foundPurple = false;
    for (const lg::Vertex3D& vertex : allCollision.translucentVertices) {
      foundBlue = foundBlue ||
        (vertex.color.red == 64 && vertex.color.green == 160 && vertex.color.blue == 255);
      foundGreen = foundGreen ||
        (vertex.color.red == 72 && vertex.color.green == 255 && vertex.color.blue == 128);
      foundOrange = foundOrange ||
        (vertex.color.red == 255 && vertex.color.green == 156 && vertex.color.blue == 48);
      foundPurple = foundPurple ||
        (vertex.color.red == 208 && vertex.color.green == 96 && vertex.color.blue == 255);
    }
    failures += expect(
      foundBlue && foundGreen && foundOrange && foundPurple,
      "all-collision mode should color visible solids, playerclip, weapclip, and triggers distinctly"
    );

    lg::Scene3D playerClipOnly;
    lg::appendCollisionDebugGeometry(playerClipOnly, debugArena, 3);
    failures += expect(
      playerClipOnly.translucentVertices.size() == 36U &&
        std::all_of(
          playerClipOnly.translucentVertices.begin(),
          playerClipOnly.translucentVertices.end(),
          [](const lg::Vertex3D& vertex) {
            return vertex.color.red == 72 && vertex.color.green == 255 &&
              vertex.color.blue == 128;
          }
        ),
      "playerclip-only mode should exclude visible solids, weapclip, and triggers"
    );

    lg::Scene3D triggersOnly;
    lg::appendCollisionDebugGeometry(triggersOnly, debugArena, 5);
    failures += expect(
      triggersOnly.translucentVertices.size() == 4U * 36U &&
        std::all_of(
          triggersOnly.translucentVertices.begin(),
          triggersOnly.translucentVertices.end(),
          [](const lg::Vertex3D& vertex) {
            return vertex.color.red == 208 && vertex.color.green == 96 &&
              vertex.color.blue == 255;
          }
        ),
      "trigger mode should include jump pads, teleports, and both enabled McGuffin bases"
    );

    lg::Scene3D disabledCollision;
    lg::appendCollisionDebugGeometry(disabledCollision, debugArena, 0);
    failures += expect(
      disabledCollision.translucentVertices.empty(),
      "disabled collision visualization should emit no debug geometry"
    );
  }

  {
    lg::TextureProjection projection;
    projection.uAxis = {1.0F, 0.0F, 0.0F};
    projection.vAxis = {0.0F, -1.0F, 0.0F};
    projection.valid = true;
    const UvBounds bounds = texturedWallUvBounds(projection);
    failures += expect(bounds.found, "projected wall UV test should find textured face");
    failures += expect(
      nearlyEqual(bounds.maxU - bounds.minU, 128.0F) &&
        nearlyEqual(bounds.maxV - bounds.minV, 128.0F),
      "128-quake-unit face should produce one 128px texture repeat at scale 1"
    );
  }

  {
    lg::TextureProjection projection;
    projection.uAxis = {1.0F, 0.0F, 0.0F};
    projection.vAxis = {0.0F, -1.0F, 0.0F};
    projection.valid = true;
    const UvBounds bounds = texturedWallUvBounds(projection, 256.0F);
    failures += expect(
      nearlyEqual(bounds.maxU - bounds.minU, 256.0F),
      "256-quake-unit face should produce two 128px texture repeats at scale 1"
    );
  }

  {
    lg::TextureProjection projection;
    projection.uAxis = {1.0F, 0.0F, 0.0F};
    projection.vAxis = {0.0F, -1.0F, 0.0F};
    projection.uOffset = 16.0F;
    projection.vOffset = 32.0F;
    projection.valid = true;
    const UvBounds bounds = texturedWallUvBounds(projection);
    failures += expect(
      nearlyEqual(bounds.minU, 16.0F) && nearlyEqual(bounds.maxV, 32.0F),
      "texture offsets should shift generated UVs"
    );
  }

  {
    lg::TextureProjection projection;
    projection.uAxis = {0.0F, -1.0F, 0.0F};
    projection.vAxis = {1.0F, 0.0F, 0.0F};
    projection.valid = true;
    const UvBounds bounds = texturedWallUvBounds(projection);
    failures += expect(
      nearlyEqual(bounds.maxU - bounds.minU, 128.0F) &&
        nearlyEqual(bounds.maxV - bounds.minV, 128.0F) &&
        bounds.minU < -127.0F,
      "90-degree rotation should rotate generated UV axes"
    );
  }

  {
    lg::TextureProjection halfScale;
    halfScale.uAxis = {2.0F, 0.0F, 0.0F};
    halfScale.vAxis = {0.0F, -2.0F, 0.0F};
    halfScale.valid = true;
    const UvBounds halfBounds = texturedWallUvBounds(halfScale);
    lg::TextureProjection doubleScale;
    doubleScale.uAxis = {0.5F, 0.0F, 0.0F};
    doubleScale.vAxis = {0.0F, -0.5F, 0.0F};
    doubleScale.valid = true;
    const UvBounds doubleBounds = texturedWallUvBounds(doubleScale);
    failures += expect(
      nearlyEqual(halfBounds.maxU - halfBounds.minU, 256.0F) &&
        nearlyEqual(doubleBounds.maxU - doubleBounds.minU, 64.0F),
      "texture scale 0.5 and 2 should affect UV spans"
    );
  }

  {
    lg::Arena cubeArena;
    cubeArena.wallCount = 1;
    cubeArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    cubeArena.walls[0].max = {1.0F, 1.0F, 1.0F};
    for (std::size_t faceIndex = 0; faceIndex < cubeArena.walls[0].faceMaterialIds.size(); ++faceIndex) {
      cubeArena.walls[0].faceMaterialIds[faceIndex] =
        lg::arenaMaterialId("face_" + std::to_string(faceIndex));
      cubeArena.walls[0].faceTextureProjections[faceIndex].uAxis = {1.0F, 0.0F, 0.0F};
      cubeArena.walls[0].faceTextureProjections[faceIndex].vAxis = {0.0F, 1.0F, 0.0F};
      cubeArena.walls[0].faceTextureProjections[faceIndex].uOffset =
        static_cast<float>(faceIndex) * 10.0F;
      cubeArena.walls[0].faceTextureProjections[faceIndex].valid = true;
    }
    const lg::Scene3D cubeScene = lg::buildStaticWorldScene(cubeArena);
    std::array<bool, 6> foundFaceMaterial = {};
    std::array<bool, 6> foundFaceOffset = {};
    for (const lg::Vertex3D& vertex : cubeScene.vertices) {
      for (std::size_t faceIndex = 0; faceIndex < foundFaceMaterial.size(); ++faceIndex) {
        if (vertex.materialId == cubeArena.walls[0].faceMaterialIds[faceIndex]) {
          foundFaceMaterial[faceIndex] = true;
          foundFaceOffset[faceIndex] = foundFaceOffset[faceIndex] ||
            vertex.u >= static_cast<float>(faceIndex) * 10.0F;
        }
      }
    }
    failures += expect(
      std::all_of(foundFaceMaterial.begin(), foundFaceMaterial.end(), [](bool value) { return value; }),
      "six-material cube should render each face material"
    );
    failures += expect(
      std::all_of(foundFaceOffset.begin(), foundFaceOffset.end(), [](bool value) { return value; }),
      "six-material cube should keep each face projection offset"
    );
  }

  {
    lg::Arena ambientArena;
    ambientArena.wallCount = 1;
    ambientArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    ambientArena.walls[0].max = {1.0F, 1.0F, 1.0F};
    ambientArena.walls[0].materialId =
      lg::arenaMaterialId("ambient_static_wall");
    ambientArena.ambientLight.color = {0.5F, 0.75F, 1.0F};
    ambientArena.ambientLight.intensity = 0.4F;
    const lg::Scene3D ambientScene = lg::buildStaticWorldScene(ambientArena);
    bool foundExpectedTopColor = false;
    for (const lg::Vertex3D& vertex : ambientScene.vertices) {
      if (
        vertex.materialId == ambientArena.walls[0].materialId &&
        nearlyEqual(vertex.position.z, ambientArena.walls[0].max.z)
      ) {
        foundExpectedTopColor =
          vertex.color.red == 51 &&
          vertex.color.green == 76 &&
          vertex.color.blue == 102;
        if (foundExpectedTopColor) {
          break;
        }
      }
    }
    failures += expect(
      foundExpectedTopColor,
      "map ambient color and intensity should tint static world vertices"
    );
  }

  {
    lg::Arena litArena;
    litArena.wallCount = 1;
    litArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    litArena.walls[0].max = {4.0F, 4.0F, 1.0F};
    litArena.walls[0].materialId = lg::arenaMaterialId("lit_static_wall");
    litArena.staticLightCount = 1;
    litArena.staticLights[0].position = {0.2F, 0.2F, 3.0F};
    litArena.staticLights[0].color = {1.0F, 0.65F, 0.35F};
    litArena.staticLights[0].intensity = 2.5F;
    litArena.staticLights[0].radius = 7.0F;
    const lg::Scene3D litScene = lg::buildStaticWorldScene(litArena);
    int minTopRed = 255;
    int maxTopRed = 0;
    bool foundTintedTopVertex = false;
    for (const lg::Vertex3D& vertex : litScene.vertices) {
      if (
        vertex.materialId == litArena.walls[0].materialId &&
        nearlyEqual(vertex.position.z, litArena.walls[0].max.z)
      ) {
        minTopRed = std::min(minTopRed, static_cast<int>(vertex.color.red));
        maxTopRed = std::max(maxTopRed, static_cast<int>(vertex.color.red));
        foundTintedTopVertex = foundTintedTopVertex ||
          (
            vertex.color.red > vertex.color.green &&
            vertex.color.green > vertex.color.blue
          );
      }
    }
    failures += expect(
      maxTopRed > minTopRed + 20,
      "static lights should create per-vertex brightness variation"
    );
    failures += expect(
      foundTintedTopVertex,
      "static lights should tint world vertices with light color"
    );
  }

  {
    lg::Arena sunArena;
    sunArena.wallCount = 1;
    sunArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    sunArena.walls[0].max = {2.0F, 2.0F, 1.0F};
    const std::uint32_t topMaterial = lg::arenaMaterialId("sunlit_top");
    const std::uint32_t bottomMaterial = lg::arenaMaterialId("sunlit_bottom");
    sunArena.walls[0].faceMaterialIds[0] = bottomMaterial;
    sunArena.walls[0].faceMaterialIds[1] = topMaterial;
    sunArena.sunLight.enabled = true;
    sunArena.sunLight.direction = {0.0F, 0.0F, -1.0F};
    sunArena.sunLight.color = {1.0F, 1.0F, 1.0F};
    sunArena.sunLight.intensity = 0.7F;
    const lg::Scene3D sunScene = lg::buildStaticWorldScene(sunArena);
    sunArena.sunLight.enabled = false;
    const lg::Scene3D noSunScene = lg::buildStaticWorldScene(sunArena);
    const bool sameBakedColors =
      sunScene.vertices.size() == noSunScene.vertices.size() &&
      std::equal(
        sunScene.vertices.begin(),
        sunScene.vertices.end(),
        noSunScene.vertices.begin(),
        [](const lg::Vertex3D& lhs, const lg::Vertex3D& rhs) {
          return sameColor(lhs.color, rhs.color);
        }
      );
    failures += expect(
      sameBakedColors,
      "sun should not enter the CPU world light bake"
    );
  }

  {
    lg::Arena sunArena;
    sunArena.wallCount = 1;
    sunArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    sunArena.walls[0].max = {2.0F, 2.0F, 1.0F};
    const std::uint32_t facingMaterial = lg::arenaMaterialId("sun_facing_wall");
    const std::uint32_t awayMaterial = lg::arenaMaterialId("sun_away_wall");
    sunArena.walls[0].faceMaterialIds[5] = facingMaterial;
    sunArena.walls[0].faceMaterialIds[3] = awayMaterial;
    sunArena.sunLight.enabled = true;
    sunArena.sunLight.direction = {1.0F, 0.0F, 0.0F};
    sunArena.sunLight.color = {1.0F, 1.0F, 1.0F};
    sunArena.sunLight.intensity = 0.8F;
    const lg::Scene3D sunScene = lg::buildStaticWorldScene(sunArena);
    sunArena.sunLight.direction = {-1.0F, 0.0F, 0.0F};
    const lg::Scene3D reversedSunScene =
      lg::buildStaticWorldScene(sunArena);
    const bool sameBakedColors =
      sunScene.vertices.size() == reversedSunScene.vertices.size() &&
      std::equal(
        sunScene.vertices.begin(),
        sunScene.vertices.end(),
        reversedSunScene.vertices.begin(),
        [](const lg::Vertex3D& lhs, const lg::Vertex3D& rhs) {
          return sameColor(lhs.color, rhs.color);
        }
      );
    failures += expect(
      sameBakedColors,
      "sun direction should affect only fragment lighting"
    );
  }

  player.velocity = lg::yawRight(player.viewYawRadians) * 8.0F;
  const lg::Scene3D movingLocalScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    nearlyEqual(movingLocalScene.camera.right.z, 0.0F),
    "local velocity should not roll the first-person camera"
  );
  player.velocity = {};

  {
    lg::Arena largeArena;
    largeArena.min = {-145.0F, -97.0F, -33.0F};
    largeArena.max = {225.0F, 225.0F, 113.0F};
    largeArena.wallCount = 0;
    lg::PlayerState largeMapPlayer = player;
    largeMapPlayer.position = {0.0F, 0.0F, 1.0F};
    const lg::Scene3D largeScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      largeArena,
      largeMapPlayer,
      opponent,
      inactiveBeam,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      settings
    );
    failures += expect(
      largeScene.vertices.size() < 131072,
      "large TrenchBroom-scale arenas should keep perspective vertex count under the GPU buffer limit"
    );
  }

  opponent.velocity = lg::yawRight(opponent.viewYawRadians) * 8.0F;
  lg::RenderSettings leanSettings = settings;
  leanSettings.enemyLeanScale = 3.0F;
  const lg::Scene3D leanScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    leanSettings
  );
  leanSettings.enemyLeanEnabled = false;
  const lg::Scene3D leanDisabledScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    leanSettings
  );
  failures += expect(
    nearlyEqual(leanDisabledScene.camera.right.z, 0.0F) &&
      leanDisabledScene.gltfPlayerModelInstances.size() == 1U,
    "disabled enemy lean should keep the camera upright and avoid velocity roll"
  );
  failures += expect(
    leanScene.gltfPlayerModelInstances.size() == 1U &&
      leanScene.gltfBonePalette.size() == leanDisabledScene.gltfBonePalette.size() &&
      maxPaletteDelta(leanScene.gltfBonePalette, leanDisabledScene.gltfBonePalette) > 0.0001F,
    "enabled enemy lean should use the skinned GLB lean animation"
  );
  opponent.velocity = {};

  {
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> strafeRemotes = {};
    lg::PlayerState strafeRunner = opponent;
    strafeRunner.velocity = lg::yawRight(strafeRunner.viewYawRadians) * 8.0F;
    strafeRemotes[0] = {
      strafeRunner,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
      0.0F,
    };
    lg::RenderSettings strafeRunSettings = settings;
    strafeRunSettings.enemyLeanEnabled = false;
    const lg::Scene3D strafeRunStartScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      strafeRemotes,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      strafeRunSettings
    );
    strafeRemotes[0].animationTimeSeconds = 0.2F;
    const lg::Scene3D strafeRunLaterScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      strafeRemotes,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      strafeRunSettings
    );
    strafeRemotes[0].player.velocity =
      lg::yawRight(strafeRunner.viewYawRadians) * 0.5F;
    strafeRemotes[0].animationTimeSeconds = 0.0F;
    const lg::Scene3D slowStrafeStartScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      strafeRemotes,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      strafeRunSettings
    );
    strafeRemotes[0].animationTimeSeconds = 0.2F;
    const lg::Scene3D slowStrafeLaterScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      strafeRemotes,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      strafeRunSettings
    );
    constexpr std::array<std::size_t, 6> legJoints = {{
      14U, 15U, 16U, 17U, 18U, 19U,
    }};
    const float fullStrafeLegDelta = maxPaletteDeltaAtIndices(
      strafeRunStartScene.gltfBonePalette,
      strafeRunLaterScene.gltfBonePalette,
      legJoints
    );
    const float slowStrafeLegDelta = maxPaletteDeltaAtIndices(
      slowStrafeStartScene.gltfBonePalette,
      slowStrafeLaterScene.gltfBonePalette,
      legJoints
    );
    const float slowAndFullSamePhaseDelta = maxPaletteDeltaAtIndices(
      slowStrafeStartScene.gltfBonePalette,
      strafeRunStartScene.gltfBonePalette,
      legJoints
    );
    failures += expect(
      strafeRunStartScene.gltfBonePalette.size() == duelistModel.jointCount() &&
        strafeRunLaterScene.gltfBonePalette.size() == duelistModel.jointCount() &&
        fullStrafeLegDelta > 0.001F,
      "pure strafe velocity should advance GLB run leg animation over render time"
    );
    failures += expect(
      slowStrafeStartScene.gltfBonePalette.size() == duelistModel.jointCount() &&
        slowStrafeLaterScene.gltfBonePalette.size() == duelistModel.jointCount() &&
        slowAndFullSamePhaseDelta <= 0.0001F &&
        slowStrafeLegDelta < fullStrafeLegDelta * 0.15F,
      "slow strafe velocity should keep full stride shape but advance it more slowly"
    );
  }

  failures += expect(
    baseScene.gltfPlayerModelInstances.size() == 1U &&
      baseScene.gltfPlayerModelInstances.front().skinned &&
      baseScene.gltfBonePalette.size() == duelistModel.jointCount() &&
      baseScene.gltfPlayerModelStats.bodyBatches ==
        static_cast<std::uint32_t>(duelistModel.primitives().size()) &&
      baseScene.gltfPlayerModelStats.bodyDrawCalls ==
        baseScene.gltfPlayerModelStats.bodyBatches,
    "opponent should use the GPU-skinned GLB duelist mesh path"
  );
  lg::RenderSettings legacyModelSettings = settings;
  legacyModelSettings.playerModel = 0;
  const lg::Scene3D legacyModelScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyModelSettings
  );
  const lg::StaticMeshAsset* playerBoxCube =
    lg::staticMeshAsset(lg::MeshHandle::PlayerBoxCube);
  const lg::StaticMeshAsset* playerBoxCubeAgain =
    lg::staticMeshAsset(lg::MeshHandle::PlayerBoxCube);
  failures += expect(
    playerBoxCube != nullptr &&
      playerBoxCube == playerBoxCubeAgain &&
      playerBoxCube->vertices.size() == 36U &&
      playerBoxCube->localBounds.radius > 0.86F &&
      playerBoxCube->localBounds.radius < 0.87F,
    "procedural player boxes should reuse one centered static cube asset"
  );
  failures += expect(
    playerBoxInstanceCount(legacyModelScene) == 7U &&
      legacyModelScene.playerBoxStats.visiblePlayers == 1 &&
      legacyModelScene.playerBoxStats.instancesSubmitted == 7 &&
      legacyModelScene.playerBoxStats.instanceUploadBytes == 7U * 52U &&
      legacyModelScene.playerBoxStats.sharedCubeStaticGpuBytes == 36U * 24U &&
      legacyModelScene.normalPlayerBodyDynamicVertices == 0 &&
      legacyModelScene.playerBoxStats.legacyCpuGeneratedVertices == 0 &&
      legacyModelScene.playerBoxStats.legacyDynamicVertexUploadBytes == 0,
    "r_player_model 0 should submit seven compact cube instances and no legacy body vertices"
  );
  constexpr std::array<lg::PlayerBodyPartType, 7> expectedPlayerBoxOrder = {{
    lg::PlayerBodyPartType::Torso,
    lg::PlayerBodyPartType::Hips,
    lg::PlayerBodyPartType::Head,
    lg::PlayerBodyPartType::LeftArm,
    lg::PlayerBodyPartType::RightArm,
    lg::PlayerBodyPartType::LeftLeg,
    lg::PlayerBodyPartType::RightLeg,
  }};
  std::size_t playerBoxOrderIndex = 0U;
  bool stablePlayerBoxOrder = true;
  for (const lg::StaticMeshInstance& instance : legacyModelScene.staticMeshInstances) {
    if (!instance.playerBoxBody) {
      continue;
    }
    stablePlayerBoxOrder = stablePlayerBoxOrder &&
      playerBoxOrderIndex < expectedPlayerBoxOrder.size() &&
      instance.playerBodyPart == expectedPlayerBoxOrder[playerBoxOrderIndex];
    ++playerBoxOrderIndex;
  }
  failures += expect(
    stablePlayerBoxOrder && playerBoxOrderIndex == expectedPlayerBoxOrder.size(),
    "static mesh finalization should preserve submission order within one batch key"
  );
  bool foundTorso = false;
  bool foundHead = false;
  bool boxTransformsFinite = true;
  bool boxTintMatchesEnemy = false;
  for (const lg::StaticMeshInstance& instance : legacyModelScene.staticMeshInstances) {
    if (!instance.playerBoxBody) {
      continue;
    }
    foundTorso = foundTorso || instance.playerBodyPart == lg::PlayerBodyPartType::Torso;
    foundHead = foundHead || instance.playerBodyPart == lg::PlayerBodyPartType::Head;
    boxTintMatchesEnemy = boxTintMatchesEnemy || isEnemyModelColor(instance.color);
    boxTransformsFinite = boxTransformsFinite &&
      finiteVec3(instance.modelRow0) &&
      finiteVec3(instance.modelRow1) &&
      finiteVec3(instance.modelRow2) &&
      finiteVec3(instance.modelTranslation) &&
      finiteVec3(transformPoint(instance, {-0.5F, -0.5F, -0.5F})) &&
      finiteVec3(transformPoint(instance, {0.5F, 0.5F, 0.5F}));
  }
  failures += expect(
    foundTorso && foundHead && boxTransformsFinite && boxTintMatchesEnemy,
    "procedural box instances should preserve body-part metadata, finite transforms, and enemy tint"
  );
  failures += expect(
    legacyModelScene.outlineMaskDraws.size() == 1U &&
      legacyModelScene.outlineMaskDraws.front().mesh == lg::MeshHandle::PlayerBoxCube &&
      legacyModelScene.outlineMaskDraws.front().instanceCount == 7U &&
      legacyModelScene.outlineMaskDraws.front().vertexCount == 0U &&
      legacyModelScene.playerBoxStats.outlineMaskBatches == 1 &&
      legacyModelScene.playerBoxStats.outlineMaskDrawCalls == 1,
    "screen-space outline mask should consume the same procedural box cube instances"
  );

  lg::PlayerState airborneOpponent = opponent;
  airborneOpponent.movementMode = lg::MovementMode::Airborne;
  airborneOpponent.onGround = false;
  airborneOpponent.velocity.z = 4.0F;
  const lg::Scene3D airborneScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    airborneOpponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    airborneScene.gltfPlayerModelInstances.size() == baseScene.gltfPlayerModelInstances.size() &&
      !airborneScene.gltfBonePalette.empty() &&
      airborneScene.gltfBonePalette.size() == baseScene.gltfBonePalette.size() &&
      maxPaletteDelta(airborneScene.gltfBonePalette, baseScene.gltfBonePalette) > 0.0001F,
    "airborne opponent should use the skinned GLB jump animation"
  );

  const lg::OutlineMaskDraw& enemyMaskDraw = baseScene.outlineMaskDraws.front();
  failures += expect(
    enemyMaskDraw.state.group == lg::OutlineGroup::Enemy &&
      enemyMaskDraw.state.visibility == lg::OutlineVisibility::VisibleOnly &&
      nearlyEqual(enemyMaskDraw.state.widthPixels, settings.enemyOutlineWidth) &&
      nearlyEqual(enemyMaskDraw.state.alpha, settings.enemyOutlineAlpha) &&
      enemyMaskDraw.gltfPlayerModel &&
      enemyMaskDraw.gltfFirstInstance == 0U &&
      enemyMaskDraw.gltfInstanceCount == baseScene.gltfPlayerModelInstances.size() &&
      enemyMaskDraw.vertexCount == 0U &&
      enemyMaskDraw.instanceCount == 0U,
    "enabled enemy outline should reuse the GPU player model instance range as mask input"
  );

  lg::RenderSettings outlineDisabledSettings = settings;
  outlineDisabledSettings.enemyOutlineEnabled = false;
  const lg::Scene3D outlineDisabledScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    outlineDisabledSettings
  );
  failures += expect(
    outlineDisabledScene.playerOutlinesBuilt == 0 &&
      outlineDisabledScene.outlineMaskDraws.empty() &&
      outlineDisabledScene.geometryOutlineDynamicVertices == 0 &&
      outlineDisabledScene.vertices.size() == baseScene.vertices.size(),
    "disabled enemy outline should exclude the player from the outline mask without changing normal geometry"
  );

  lg::RenderSettings legacyOutlineSettings = settings;
  legacyOutlineSettings.playerOutlineStyle = lg::PlayerOutlineStyle::Geometry;
  const lg::Scene3D legacyOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyOutlineSettings
  );
  failures += expect(
    legacyOutlineScene.playerOutlinesBuilt == 1 &&
      legacyOutlineScene.outlineMaskDraws.empty() &&
      legacyOutlineScene.geometryOutlineFallbackUsed &&
      legacyOutlineScene.geometryOutlineDynamicVertices > 0 &&
      legacyOutlineScene.vertices.size() > baseScene.vertices.size(),
    "legacy geometry outline style should remain explicit fallback behavior"
  );

  lg::RenderSettings modeDisabledOutlineSettings = settings;
  modeDisabledOutlineSettings.playerOutlineMode = lg::PlayerOutlineMode::Disabled;
  const lg::Scene3D modeDisabledOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, opponent, inactiveBeam, inactiveBeam,
    weaponFires, rocketExplosions, rockets, modeDisabledOutlineSettings
  );
  failures += expect(
    modeDisabledOutlineScene.playerOutlinesBuilt == 0U &&
      modeDisabledOutlineScene.outlineMaskDraws.empty() &&
      modeDisabledOutlineScene.geometryOutlineDynamicVertices == 0U,
    "outline mode zero should gate geometry and screen-space outline work"
  );

  lg::RenderSettings nativeOutlineSettings = settings;
  nativeOutlineSettings.playerOutlineMode =
    lg::PlayerOutlineMode::NativeScreenSpace;
  nativeOutlineSettings.playerOutlineStyle = lg::PlayerOutlineStyle::Geometry;
  nativeOutlineSettings.playerOutlineWidth = 1.5F;
  const lg::Scene3D nativeOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, opponent, inactiveBeam, inactiveBeam,
    weaponFires, rocketExplosions, rockets, nativeOutlineSettings
  );
  failures += expect(
    !nativeOutlineScene.outlineMaskDraws.empty() &&
      nativeOutlineScene.outlineMaskDraws.size() >
        baseScene.outlineMaskDraws.size() &&
      !nativeOutlineScene.geometryOutlineFallbackUsed &&
      std::any_of(
        nativeOutlineScene.outlineMaskDraws.begin(),
        nativeOutlineScene.outlineMaskDraws.end(),
        [](const lg::OutlineMaskDraw& draw) {
          return draw.mesh != lg::MeshHandle::PlayerBoxCube &&
            draw.instanceCount > 0U;
        }
      ) &&
      nearlyEqual(
        nativeOutlineScene.outlineMaskDraws.front().state.widthPixels,
        1.5F
      ),
    "native outlines should reuse body geometry and add the held weapon silhouette"
  );

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> teammateOnlyPlayers = {};
  teammateOnlyPlayers[1] =
    lg::RemotePlayerView{
      opponent,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      true,
      {},
    };
  const lg::Scene3D teammateOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    teammateOnlyPlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    teammateOutlineScene.outlineMaskDraws.size() == 1U &&
      teammateOutlineScene.outlineMaskDraws.front().state.group ==
        lg::OutlineGroup::Teammate &&
      nearlyEqual(
        teammateOutlineScene.outlineMaskDraws.front().state.widthPixels,
        settings.teammateOutlineWidth
      ),
    "teammate and enemy outline mask groups should remain distinct"
  );
  const lg::Scene3D teammateBoxOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    teammateOnlyPlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyModelSettings
  );
  failures += expect(
    teammateBoxOutlineScene.outlineMaskDraws.size() == 1U &&
      teammateBoxOutlineScene.outlineMaskDraws.front().state.group ==
        lg::OutlineGroup::Teammate &&
      teammateBoxOutlineScene.outlineMaskDraws.front().mesh ==
        lg::MeshHandle::PlayerBoxCube &&
      teammateBoxOutlineScene.outlineMaskDraws.front().instanceCount == 7U,
    "teammate procedural box outline mask should keep teammate grouping on cube instances"
  );

  const lg::OutlineTargetDimensions oddOutlineDimensions =
    lg::outlineTargetDimensions(1921U, 1081U);
  failures += expect(
    oddOutlineDimensions.workWidth == 961U &&
      oddOutlineDimensions.workHeight == 541U &&
      nearlyEqual(lg::outlineWorkRadiusPixels(3.0F), 1.5F) &&
      nearlyEqual(lg::outlineWorkRadiusPixels(8.0F), 3.0F) &&
      lg::kOutlineFixedDilationKernelTaps == 49U,
    "screen-space outline widths should map to half-resolution radius with a fixed 7x7 kernel"
  );
  const lg::OutlineTargetDimensions nativeOddOutlineDimensions =
    lg::outlineTargetDimensions(1921U, 1081U, 1.0F);
  failures += expect(
    nativeOddOutlineDimensions.workWidth == 1921U &&
      nativeOddOutlineDimensions.workHeight == 1081U &&
      nearlyEqual(nativeOddOutlineDimensions.workScale, 1.0F) &&
      nearlyEqual(lg::outlineWorkRadiusPixels(1.5F, 1.0F), 1.5F) &&
      nearlyEqual(lg::outlineWorkRadiusPixels(3.0F, 1.0F), 3.0F),
    "native outline targets and radii should use exact output pixels"
  );

  const lg::OutlineWorkPlan noOutlinePlan = lg::buildOutlineWorkPlan(
    baseScene.camera,
    std::span<const lg::Vertex3D>(baseScene.vertices.data(), baseScene.vertices.size()),
    std::span<const lg::OutlineMaskDraw>(),
    1920U,
    1080U
  );
  failures += expect(
    !noOutlinePlan.hasWork &&
      noOutlinePlan.maskDrawCalls == 0U &&
      noOutlinePlan.dilationDrawCalls == 0U &&
      noOutlinePlan.compositeDrawCalls == 0U,
    "no outlined players should produce no work rectangle and no outline passes"
  );

  const lg::OutlineWorkPlan centeredOutlinePlan = lg::buildOutlineWorkPlan(
    baseScene.camera,
    std::span<const lg::Vertex3D>(baseScene.vertices.data(), baseScene.vertices.size()),
    std::span<const lg::StaticMeshInstance>(
      baseScene.staticMeshInstances.data(),
      baseScene.staticMeshInstances.size()
    ),
    std::span<const lg::GltfPlayerModelInstance>(
      baseScene.gltfPlayerModelInstances.data(),
      baseScene.gltfPlayerModelInstances.size()
    ),
    std::span<const lg::OutlineMaskDraw>(
      baseScene.outlineMaskDraws.data(),
      baseScene.outlineMaskDraws.size()
    ),
    1920U,
    1080U
  );
  failures += expect(
    centeredOutlinePlan.hasWork &&
      centeredOutlinePlan.dimensions.workWidth == 960U &&
      centeredOutlinePlan.dimensions.workHeight == 540U &&
      centeredOutlinePlan.workRect.valid() &&
      centeredOutlinePlan.finalRect.width < 1920 &&
      centeredOutlinePlan.finalRect.height < 1080 &&
      centeredOutlinePlan.maskDrawCalls == 1U &&
      centeredOutlinePlan.dilationDrawCalls == 1U &&
      centeredOutlinePlan.compositeDrawCalls == 1U,
    "centered outlined player should use a bounded half-resolution work rectangle"
  );
  const lg::OutlineWorkPlan nativeCenteredOutlinePlan = lg::buildOutlineWorkPlan(
    nativeOutlineScene.camera,
    std::span<const lg::Vertex3D>(
      nativeOutlineScene.vertices.data(), nativeOutlineScene.vertices.size()
    ),
    std::span<const lg::StaticMeshInstance>(
      nativeOutlineScene.staticMeshInstances.data(),
      nativeOutlineScene.staticMeshInstances.size()
    ),
    std::span<const lg::GltfPlayerModelInstance>(
      nativeOutlineScene.gltfPlayerModelInstances.data(),
      nativeOutlineScene.gltfPlayerModelInstances.size()
    ),
    std::span<const lg::OutlineMaskDraw>(
      nativeOutlineScene.outlineMaskDraws.data(),
      nativeOutlineScene.outlineMaskDraws.size()
    ),
    1921U,
    1081U,
    1.0F
  );
  failures += expect(
    nativeCenteredOutlinePlan.hasWork &&
      nativeCenteredOutlinePlan.dimensions.workWidth == 1921U &&
      nativeCenteredOutlinePlan.dimensions.workHeight == 1081U &&
      nearlyEqual(nativeCenteredOutlinePlan.maxWorkRadiusPixels, 1.5F),
    "native outline work plans should keep odd output size and final-pixel width"
  );
  const lg::OutlineWorkPlan centeredBoxOutlinePlan = lg::buildOutlineWorkPlan(
    legacyModelScene.camera,
    std::span<const lg::Vertex3D>(
      legacyModelScene.vertices.data(),
      legacyModelScene.vertices.size()
    ),
    std::span<const lg::StaticMeshInstance>(
      legacyModelScene.staticMeshInstances.data(),
      legacyModelScene.staticMeshInstances.size()
    ),
    std::span<const lg::OutlineMaskDraw>(
      legacyModelScene.outlineMaskDraws.data(),
      legacyModelScene.outlineMaskDraws.size()
    ),
    1920U,
    1080U
  );
  failures += expect(
    centeredBoxOutlinePlan.hasWork &&
      !centeredBoxOutlinePlan.conservativeFallback &&
      centeredBoxOutlinePlan.maskDrawCalls == 1U &&
      centeredBoxOutlinePlan.dilationDrawCalls == 1U &&
      centeredBoxOutlinePlan.compositeDrawCalls == 1U,
    "procedural box outline work plan should project static cube instance bounds"
  );

  lg::StaticMeshInstance longWeaponInstance =
    legacyModelScene.staticMeshInstances.front();
  longWeaponInstance.mesh = lg::MeshHandle::RemoteFreezeGunBody;
  longWeaponInstance.worldBounds.radius *= 4.0F;
  const std::array<lg::StaticMeshInstance, 1> longWeaponInstances = {{
    longWeaponInstance,
  }};
  const std::array<lg::OutlineMaskDraw, 1> longWeaponDraws = {{
    {
      0U,
      0U,
      enemyMaskDraw.state,
      lg::MeshHandle::RemoteFreezeGunBody,
      0U,
      1U,
    },
  }};
  const lg::OutlineWorkPlan longWeaponPlan = lg::buildOutlineWorkPlan(
    legacyModelScene.camera,
    std::span<const lg::Vertex3D>(),
    longWeaponInstances,
    std::span<const lg::GltfPlayerModelInstance>(),
    longWeaponDraws,
    1920U,
    1080U,
    1.0F
  );
  lg::ProjectedPoint longWeaponRightEdge;
  const bool longWeaponEdgeProjected = lg::projectPerspectivePoint(
    legacyModelScene.camera,
    longWeaponInstance.worldBounds.center +
      legacyModelScene.camera.right * longWeaponInstance.worldBounds.radius,
    longWeaponRightEdge
  );
  const float longWeaponRightPixel =
    (longWeaponRightEdge.x + 1.0F) * 0.5F * 1920.0F;
  failures += expect(
    longWeaponPlan.hasWork &&
      !longWeaponPlan.conservativeFallback &&
      longWeaponEdgeProjected &&
      static_cast<float>(
        longWeaponPlan.finalRect.x + longWeaponPlan.finalRect.width
      ) >= longWeaponRightPixel,
    "native outline work rect should contain the true bounds of a long weapon"
  );

  lg::PlayerState edgeOpponent = opponent;
  edgeOpponent.position = {4.0F, -3.0F, 0.9F};
  const lg::Scene3D edgeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    edgeOpponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  const lg::OutlineWorkPlan edgeOutlinePlan = lg::buildOutlineWorkPlan(
    edgeScene.camera,
    std::span<const lg::Vertex3D>(edgeScene.vertices.data(), edgeScene.vertices.size()),
    std::span<const lg::StaticMeshInstance>(
      edgeScene.staticMeshInstances.data(),
      edgeScene.staticMeshInstances.size()
    ),
    std::span<const lg::GltfPlayerModelInstance>(
      edgeScene.gltfPlayerModelInstances.data(),
      edgeScene.gltfPlayerModelInstances.size()
    ),
    std::span<const lg::OutlineMaskDraw>(
      edgeScene.outlineMaskDraws.data(),
      edgeScene.outlineMaskDraws.size()
    ),
    1920U,
    1080U
  );
  failures += expect(
    edgeOutlinePlan.hasWork &&
      edgeOutlinePlan.finalRect.x >= 0 &&
      edgeOutlinePlan.finalRect.x + edgeOutlinePlan.finalRect.width <= 1920 &&
      edgeOutlinePlan.workRect.x >= 0 &&
      edgeOutlinePlan.workRect.x + edgeOutlinePlan.workRect.width <= 960 &&
      edgeOutlinePlan.finalRect.width >=
        static_cast<int>(settings.enemyOutlineWidth + 4.0F),
    "screen-edge outlined player should receive a clamped rectangle with outline and filtering margin"
  );

  std::array<lg::Vertex3D, 1> invalidOutlineVertices = {{
    {baseScene.camera.position, {}, 0.0F, 0.0F, 0U},
  }};
  const std::array<lg::OutlineMaskDraw, 1> invalidOutlineDraws = {{
    {0U, 1U, enemyMaskDraw.state},
  }};
  const lg::OutlineWorkPlan invalidOutlinePlan = lg::buildOutlineWorkPlan(
    baseScene.camera,
    invalidOutlineVertices,
    invalidOutlineDraws,
    1920U,
    1080U
  );
  failures += expect(
    invalidOutlinePlan.hasWork &&
      invalidOutlinePlan.conservativeFallback &&
      invalidOutlinePlan.finalRect.x == 0 &&
      invalidOutlinePlan.finalRect.y == 0 &&
      invalidOutlinePlan.finalRect.width == 1920 &&
      invalidOutlinePlan.finalRect.height == 1080,
    "invalid or camera-intersecting outline bounds should use the conservative full-target fallback"
  );

  lg::RenderSettings isolationOutlineDisabledSettings = settings;
  isolationOutlineDisabledSettings.drawPlayerOutlines = false;
  const lg::Scene3D isolationOutlineDisabledScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    isolationOutlineDisabledSettings
  );
  failures += expect(
    isolationOutlineDisabledScene.visibleRemotePlayers == 1 &&
      isolationOutlineDisabledScene.remoteBodyModelsBuilt == 1 &&
      isolationOutlineDisabledScene.remoteWeaponModelsBuilt == 1 &&
      isolationOutlineDisabledScene.playerOutlinesBuilt == 0 &&
      isolationOutlineDisabledScene.outlineMaskDraws.empty(),
    "disabled player outlines should prevent only outline mask construction"
  );

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> remotePlayers = {};
  remotePlayers[1] =
    lg::RemotePlayerView{
      opponent,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  lg::PlayerState secondOpponent = opponent;
  secondOpponent.position.y += 3.0F;
  remotePlayers[2] =
    lg::RemotePlayerView{
      secondOpponent,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  const lg::Scene3D multiOpponentScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    remotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    multiOpponentScene.gltfPlayerModelInstances.size() == 2U &&
      multiOpponentScene.gltfPlayerModelInstances[0].firstBone !=
        multiOpponentScene.gltfPlayerModelInstances[1].firstBone &&
      multiOpponentScene.gltfPlayerModelInstances[0].boneCount ==
        duelistModel.jointCount() &&
      multiOpponentScene.gltfPlayerModelInstances[1].boneCount ==
        duelistModel.jointCount() &&
      multiOpponentScene.gltfPlayerModelStats.bodyBatches ==
        static_cast<std::uint32_t>(duelistModel.primitives().size()) &&
      multiOpponentScene.gltfPlayerModelStats.bodyDrawCalls ==
        multiOpponentScene.gltfPlayerModelStats.bodyBatches,
    "perspective scene should batch multiple GLB remote players by primitive"
  );
  failures += expect(
    multiOpponentScene.remoteWeaponStats.instancesSubmitted == 2 &&
      multiOpponentScene.remoteWeaponStats.batches == 1 &&
      multiOpponentScene.remoteWeaponStats.drawCalls == 1,
    "multiple remotes holding the same weapon should form one remote weapon batch"
  );
  const lg::Scene3D multiBoxOpponentScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    remotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyModelSettings
  );
  failures += expect(
    multiBoxOpponentScene.playerBoxStats.visiblePlayers == 2 &&
      multiBoxOpponentScene.playerBoxStats.instancesSubmitted == 14 &&
      multiBoxOpponentScene.playerBoxStats.opaqueBatches == 1 &&
      multiBoxOpponentScene.playerBoxStats.opaqueDrawCalls == 1 &&
      multiBoxOpponentScene.playerBoxStats.outlineMaskDrawCalls == 2 &&
      multiBoxOpponentScene.normalPlayerBodyDynamicVertices == 0,
    "two visible procedural box players should combine into one compatible cube body batch"
  );

  remotePlayers[2].selectedWeapon = lg::Weapon::RocketLauncher;
  const lg::Scene3D mixedWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    remotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    mixedWeaponScene.remoteWeaponStats.instancesSubmitted == 4 &&
      mixedWeaponScene.remoteWeaponStats.batches == 4 &&
      mixedWeaponScene.remoteWeaponStats.drawCalls == 4,
    "remotes holding different weapons should form separate remote weapon batches"
  );

  lg::PlayerState behindOpponent = opponent;
  behindOpponent.position = {-4.0F, 2.0F, 0.9F};
  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> behindRemotePlayers = {};
  behindRemotePlayers[1] =
    lg::RemotePlayerView{
      behindOpponent,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  const lg::Scene3D noRemoteScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount>{},
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  const lg::Scene3D culledBehindScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    behindRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
      culledBehindScene.remoteCandidates == 1 &&
      culledBehindScene.remoteFrustumVisible == 0 &&
      culledBehindScene.remoteFrustumCulled == 1 &&
      culledBehindScene.remoteBodyModelsBuilt == 0 &&
      culledBehindScene.remoteWeaponModelsBuilt == 0 &&
      culledBehindScene.remoteWeaponStats.candidates == 1 &&
      culledBehindScene.remoteWeaponStats.frustumCulled == 1 &&
      culledBehindScene.remoteWeaponStats.instancesSubmitted == 0 &&
      culledBehindScene.playerOutlinesBuilt == 0 &&
      culledBehindScene.vertices.size() == noRemoteScene.vertices.size(),
    "remote behind camera should not add body, weapon, or outline vertices when culling is enabled"
  );
  const lg::Scene3D culledBehindBoxScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    behindRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyModelSettings
  );
  failures += expect(
    culledBehindBoxScene.playerBoxStats.culledPlayers == 1 &&
      culledBehindBoxScene.playerBoxStats.visiblePlayers == 0 &&
      culledBehindBoxScene.playerBoxStats.instancesSubmitted == 0 &&
      playerBoxInstanceCount(culledBehindBoxScene) == 0U,
    "off-screen procedural box player should be culled before body-part instances are emitted"
  );
  lg::RenderSettings frustumCullDisabledSettings = settings;
  frustumCullDisabledSettings.frustumCullRemotePlayers = false;
  const lg::Scene3D uncullableBehindScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    behindRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    frustumCullDisabledSettings
  );
  failures += expect(
      uncullableBehindScene.remoteCandidates == 1 &&
      uncullableBehindScene.remoteFrustumVisible == 1 &&
      uncullableBehindScene.remoteFrustumCulled == 0 &&
      uncullableBehindScene.remoteBodyModelsBuilt == 1 &&
      uncullableBehindScene.remoteWeaponModelsBuilt == 1 &&
      uncullableBehindScene.remoteWeaponStats.instancesSubmitted == 1 &&
      uncullableBehindScene.playerOutlinesBuilt == 1 &&
      uncullableBehindScene.gltfPlayerModelInstances.size() == 1U,
    "r_frustum_cull 0 should preserve remote body, weapon, and outline construction"
  );

  for (lg::Weapon weapon : lg::kWeaponSlotOrder) {
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> weaponRemotePlayers = {};
    weaponRemotePlayers[1] =
      lg::RemotePlayerView{
        opponent,
        inactiveBeam,
        weapon,
        0.0F,
        1.0F,
        true,
        false,
        {},
      };
    const lg::Scene3D weaponScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      weaponRemotePlayers,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      settings
    );
    const lg::MeshHandle mesh = lg::remoteWeaponMeshHandle(weapon);
    const lg::StaticMeshAsset* asset = lg::staticMeshAsset(mesh);
    const lg::MaterialMeshAsset* materialAsset = lg::materialMeshAsset(mesh);
    bool foundWeaponInstance = false;
    lg::StaticMeshInstance foundWeapon = {};
    bool foundRevolverCylinder = weapon != lg::Weapon::Revolver;
    for (const lg::StaticMeshInstance& instance : weaponScene.staticMeshInstances) {
      if (instance.mesh == mesh && instance.pass == lg::RenderPass::OpaqueWorld) {
        foundWeaponInstance = true;
        foundWeapon = instance;
      }
      foundRevolverCylinder =
        foundRevolverCylinder ||
        (
          instance.mesh == lg::MeshHandle::RemoteRevolverCylinder &&
          instance.pass == lg::RenderPass::OpaqueWorld
        );
    }
    const std::uint32_t expectedInstances = weapon == lg::Weapon::RocketLauncher
      ? 3U
      : (
          weapon == lg::Weapon::Revolver ||
          weapon == lg::Weapon::MachineGun
        )
        ? 2U
        : (
            weapon == lg::Weapon::FreezeGun ||
            weapon == lg::Weapon::PlasmaGun
          )
          ? 3U
          : 1U;
    bool revolverGripAlignedAndSized = true;
    if (weapon == lg::Weapon::Revolver && foundWeaponInstance) {
      const lg::Vec3 grip = transformPoint(
        foundWeapon,
        {-0.23F, 0.0F, -0.24F}
      );
      const lg::Vec3 expectedHand = {
        opponent.position.x + opponent.bounds.radius * 0.18F,
        opponent.position.y - opponent.bounds.radius * 0.84F,
        opponent.position.z + opponent.bounds.halfHeight * 0.06F,
      };
      const float modelScale = lg::length(
        transformPoint(foundWeapon, {1.0F, 0.0F, 0.0F}) -
          foundWeapon.modelTranslation
      );
      revolverGripAlignedAndSized =
        lg::length(grip - expectedHand) < 0.001F &&
        nearlyEqual(modelScale, 0.45F);
    }
    bool plasmaGripAligned = true;
    if (weapon == lg::Weapon::PlasmaGun && foundWeaponInstance) {
      const lg::Vec3 grip = transformPoint(
        foundWeapon,
        lg::plasmaGunGripSocket()
      );
      const lg::Vec3 expectedHand = {
        opponent.position.x + opponent.bounds.radius * 0.18F,
        opponent.position.y - opponent.bounds.radius * 0.84F,
        opponent.position.z + opponent.bounds.halfHeight * 0.06F,
      };
      plasmaGripAligned = lg::length(grip - expectedHand) < 0.001F;
    }
    bool sniperGripAligned = true;
    if (weapon == lg::Weapon::Railgun && foundWeaponInstance) {
      const lg::Vec3 grip = transformPoint(
        foundWeapon,
        lg::sniperRifleGripSocket()
      );
      const lg::Vec3 expectedHand = {
        opponent.position.x + opponent.bounds.radius * 0.18F,
        opponent.position.y - opponent.bounds.radius * 0.84F,
        opponent.position.z + opponent.bounds.halfHeight * 0.06F,
      };
      sniperGripAligned =
        asset == nullptr &&
        materialAsset != nullptr &&
        materialAsset->vertices.size() == 1710U * 3U &&
        lg::length(grip - expectedHand) < 0.001F &&
        nearlyEqual(lg::length(foundWeapon.modelRow0), 1.15F);
    }
    failures += expect(
      mesh != lg::MeshHandle::Invalid &&
        (
          (asset != nullptr && !asset->vertices.empty()) ||
          (materialAsset != nullptr && !materialAsset->vertices.empty())
        ) &&
        foundWeaponInstance &&
        foundRevolverCylinder &&
        revolverGripAlignedAndSized &&
        plasmaGripAligned &&
        sniperGripAligned &&
        weaponScene.remoteWeaponStats.instancesSubmitted == expectedInstances &&
        weaponScene.remoteWeaponStats.legacyDynamicVertices == 0,
      "every playable weapon should map to its expected static mesh instances"
    );
  }

  const lg::MaterialMeshAsset* sniperMaterial =
    lg::materialMeshAsset(lg::MeshHandle::RemoteRailgun);
  bool hasSniperSteel = false;
  bool hasSniperGreenStock = false;
  if (sniperMaterial != nullptr) {
    for (const lg::WeaponMaterialVertex3D& vertex : sniperMaterial->vertices) {
      hasSniperSteel = hasSniperSteel || vertex.metallic > 0.7F;
      hasSniperGreenStock = hasSniperGreenStock ||
        (
          vertex.metallic < 0.1F &&
          vertex.baseColor.green > vertex.baseColor.red + 20U &&
          vertex.baseColor.green > vertex.baseColor.blue + 20U
        );
    }
  }
  failures += expect(
    hasSniperSteel && hasSniperGreenStock,
    "sniper material mesh should keep its steel and green stock parts"
  );

  const lg::MaterialMeshAsset* revolverMaterial =
    lg::materialMeshAsset(lg::MeshHandle::RemoteRevolverBody);
  bool hasPolishedMetal = false;
  bool hasNonMetallicWalnut = false;
  if (revolverMaterial != nullptr) {
    for (const lg::WeaponMaterialVertex3D& vertex : revolverMaterial->vertices) {
      hasPolishedMetal = hasPolishedMetal ||
        (vertex.metallic > 0.9F && vertex.roughness < 0.3F);
      hasNonMetallicWalnut = hasNonMetallicWalnut ||
        (
          vertex.metallic < 0.1F &&
          static_cast<float>(vertex.baseColor.red) >
            static_cast<float>(vertex.baseColor.green) * 1.5F &&
          vertex.baseColor.green > vertex.baseColor.blue
        );
    }
  }
  failures += expect(
    hasPolishedMetal && hasNonMetallicWalnut,
    "revolver material mesh should preserve contrasting steel and walnut properties"
  );

  const std::array<const lg::MaterialMeshAsset*, 2> machineGunMaterials = {{
    lg::materialMeshAsset(lg::MeshHandle::RemoteMachineGunBody),
    lg::materialMeshAsset(lg::MeshHandle::RemoteMachineGunBarrels),
  }};
  bool hasMachineGunSteel = false;
  bool hasMachineGunBrass = false;
  bool hasMachineGunCyanAccent = false;
  for (const lg::MaterialMeshAsset* machineGunMaterial : machineGunMaterials) {
    if (machineGunMaterial == nullptr) continue;
    for (const lg::WeaponMaterialVertex3D& vertex : machineGunMaterial->vertices) {
      hasMachineGunSteel = hasMachineGunSteel ||
        (vertex.metallic > 0.9F && vertex.roughness < 0.3F);
      hasMachineGunBrass = hasMachineGunBrass ||
        (
          vertex.metallic > 0.5F &&
          vertex.baseColor.red > vertex.baseColor.green &&
          vertex.baseColor.green > vertex.baseColor.blue
        );
      hasMachineGunCyanAccent = hasMachineGunCyanAccent ||
        (
          vertex.metallic < 0.1F &&
          vertex.baseColor.blue > vertex.baseColor.red * 2U
        );
    }
  }
  failures += expect(
    hasMachineGunSteel && hasMachineGunBrass && hasMachineGunCyanAccent,
    "machine-gun material mesh should preserve steel, brass, and cyan accents"
  );

  const std::array<const lg::MaterialMeshAsset*, 3> rocketLauncherMaterials = {{
    lg::materialMeshAsset(lg::MeshHandle::RemoteRocketLauncherBody),
    lg::materialMeshAsset(lg::MeshHandle::RemoteRocketLauncherRecoil),
    lg::materialMeshAsset(lg::MeshHandle::RemoteRocketLauncherLatch),
  }};
  bool hasRocketLauncherMetal = false;
  bool hasRocketLauncherRed = false;
  for (const lg::MaterialMeshAsset* material : rocketLauncherMaterials) {
    if (material == nullptr) continue;
    for (const lg::WeaponMaterialVertex3D& vertex : material->vertices) {
      hasRocketLauncherMetal = hasRocketLauncherMetal || vertex.metallic > 0.7F;
      hasRocketLauncherRed = hasRocketLauncherRed ||
        (
          vertex.baseColor.red > vertex.baseColor.green * 2U &&
          vertex.baseColor.red > vertex.baseColor.blue * 2U
        );
    }
  }
  failures += expect(
    hasRocketLauncherMetal && hasRocketLauncherRed,
    "rocket-launcher material meshes should preserve metal and red identification paint"
  );

  const std::array<lg::MeshHandle, 13> cachedBoundsMeshes = {{
    lg::MeshHandle::RemoteMachineGunBody,
    lg::MeshHandle::RemoteMachineGunBarrels,
    lg::MeshHandle::RemoteRevolverBody,
    lg::MeshHandle::RemoteRevolverCylinder,
    lg::MeshHandle::RemoteRocketLauncherBody,
    lg::MeshHandle::RemoteRocketLauncherRecoil,
    lg::MeshHandle::RemoteRocketLauncherLatch,
    lg::MeshHandle::RemoteFreezeGunBody,
    lg::MeshHandle::RemoteFreezeGunFocus,
    lg::MeshHandle::RemoteFreezeGunCoolant,
    lg::MeshHandle::RemotePlasmaGunBody,
    lg::MeshHandle::RemotePlasmaGunProngs,
    lg::MeshHandle::RemotePlasmaGunCore,
  }};
  bool cachedMaterialBoundsMatch = true;
  for (lg::MeshHandle mesh : cachedBoundsMeshes) {
    const lg::MaterialMeshAsset* material = lg::materialMeshAsset(mesh);
    if (material == nullptr) {
      cachedMaterialBoundsMatch = false;
      continue;
    }
    const lg::BoundingSphere previous = legacyMaterialMeshBounds(material->vertices);
    cachedMaterialBoundsMatch = cachedMaterialBoundsMatch &&
      material->localBounds.center.x == previous.center.x &&
      material->localBounds.center.y == previous.center.y &&
      material->localBounds.center.z == previous.center.z &&
      material->localBounds.radius == previous.radius;
  }
  failures += expect(
    cachedMaterialBoundsMatch,
    "cached material weapon bounds should match the prior two-pass calculation"
  );

  lg::LightningGunResult opponentBeam;
  opponentBeam.active = true;
  opponentBeam.start = opponent.position;
  opponentBeam.end = player.position;
  const lg::Scene3D beamScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    opponentBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    beamScene.vertices.size() > baseScene.vertices.size(),
    "active opponent lightning should add world-space beam geometry"
  );

  settings.enemyBeamAlpha = 0.5F;
  const lg::Scene3D translucentBeamScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    opponentBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    translucentBeamScene.vertices.size() == baseScene.vertices.size() &&
      !translucentBeamScene.translucentVertices.empty(),
    "transparent beam geometry should use the non-depth-writing batch"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> shotgunFires = {};
  shotgunFires[0].fired = true;
  shotgunFires[0].hit = true;
  shotgunFires[0].weapon = lg::Weapon::Shotgun;
  shotgunFires[0].start = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  shotgunFires[0].end = shotgunFires[0].start + lg::Vec3{8.0F, 0.0F, 0.0F};
  shotgunFires[0].pelletCount = lg::kShotgunPelletCount;
  shotgunFires[0].pelletHitCount = 5;
  const lg::Scene3D shotgunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    shotgunFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    shotgunScene.transientVfxStats.tracerInstancesSubmitted == 0 &&
      shotgunScene.transientVfxStats.legacyMachineGunShotgunVisualDraws == 0,
    "retained shotgun fire should not emit legacy dynamic tracer geometry"
  );
  lg::RenderSettings localShotgunWeaponStartSettings = settings;
  localShotgunWeaponStartSettings.shotgunWeaponModelStart = true;

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> machineGunFires = {};
  machineGunFires[0].fired = true;
  machineGunFires[0].hit = true;
  machineGunFires[0].weapon = lg::Weapon::MachineGun;
  machineGunFires[0].start = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  machineGunFires[0].end = machineGunFires[0].start + lg::Vec3{9.0F, 0.0F, 3.0F};
  machineGunFires[0].visualSeed = 1;
  const lg::Scene3D machineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    machineGunFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    machineGunScene.transientVfxStats.tracerInstancesSubmitted == 0 &&
      machineGunScene.transientVfxStats.legacyMachineGunShotgunVisualDraws == 0,
    "retained machine gun fire should not emit legacy dynamic tracer geometry"
  );
  std::array<lg::TransientTracer, 8> tracerInstances = {};
  tracerInstances[0] = {
    machineGunFires[0].start,
    machineGunFires[0].end,
    0.0F,
    0.05F,
    0.012F,
    {255, 220, 128, 180},
    machineGunFires[0].visualSeed,
    lg::TracerStyle::MachineGun,
  };
  const lg::Scene3D machineGunTracerScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    settings
  );
  failures += expect(
    machineGunTracerScene.transientVfxStats.tracerInstancesSubmitted == 1 &&
      machineGunTracerScene.transientVfxStats.activeMachineGunTracers == 1 &&
      machineGunTracerScene.translucentVertices.size() == 12U,
    "one active MG transient tracer should emit one transparent beam"
  );
  if (!machineGunTracerScene.translucentVertices.empty()) {
    float minTracerZ = machineGunTracerScene.translucentVertices.front().position.z;
    float maxTracerZ = minTracerZ;
    for (const lg::Vertex3D& vertex : machineGunTracerScene.translucentVertices) {
      minTracerZ = std::min(minTracerZ, vertex.position.z);
      maxTracerZ = std::max(maxTracerZ, vertex.position.z);
    }
    failures += expect(
      maxTracerZ - minTracerZ > 2.5F,
      "MG tracer beam geometry should preserve upward pitch"
    );
  }
  tracerInstances[0] = {
    machineGunFires[0].start,
    machineGunFires[0].start + lg::Vec3{0.16F, 0.0F, 0.0F},
    0.0F,
    0.045F,
    0.045F,
    {255, 188, 76, 235},
    machineGunFires[0].visualSeed,
    lg::TracerStyle::MachineGunMuzzleFlash,
  };
  const lg::Scene3D muzzleFlashScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    settings
  );
  const bool hasAdditiveMuzzleBillboard = std::any_of(
    muzzleFlashScene.simpleInstances.begin(),
    muzzleFlashScene.simpleInstances.end(),
    [](const lg::SimpleRenderInstance& instance) {
      return instance.billboard == lg::BillboardHandle::ExplosionFlash &&
        instance.pass == lg::RenderPass::AdditiveGlow;
    }
  );
  failures += expect(
    muzzleFlashScene.transientVfxStats.activeMachineGunMuzzleFlashes == 1 &&
      muzzleFlashScene.transientVfxStats.muzzleFlashInstancesSubmitted == 1 &&
      hasAdditiveMuzzleBillboard &&
      muzzleFlashScene.translucentVertices.size() == 12U,
    "MG muzzle flash should combine a directional flame with one additive billboard"
  );
  tracerInstances[0] = {
    machineGunFires[0].start,
    machineGunFires[0].start + lg::Vec3{0.22F, 0.0F, 0.0F},
    0.0F,
    0.068F,
    0.070F,
    {246, 92, 42, 238},
    411U,
    lg::TracerStyle::RocketLauncherMuzzleFlash,
  };
  lg::RenderSettings lowRocketMuzzleSettings = settings;
  lowRocketMuzzleSettings.combatEffectsQuality = 0;
  const lg::Scene3D lowRocketMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    lowRocketMuzzleSettings
  );
  lg::RenderSettings fullRocketMuzzleSettings = settings;
  fullRocketMuzzleSettings.combatEffectsQuality = 2;
  const lg::Scene3D fullRocketMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    fullRocketMuzzleSettings
  );
  lg::RenderSettings noBloomRocketMuzzleSettings = fullRocketMuzzleSettings;
  noBloomRocketMuzzleSettings.bloomEnabled = false;
  const lg::Scene3D noBloomRocketMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    noBloomRocketMuzzleSettings
  );
  failures += expect(
    lowRocketMuzzleScene.transientVfxStats.activeRocketLauncherMuzzleFlashes == 1U &&
      lowRocketMuzzleScene.transientVfxStats.muzzleFlashInstancesSubmitted == 1U &&
      lowRocketMuzzleScene.translucentVertices.size() == 24U &&
      billboardCount(
        lowRocketMuzzleScene,
        lg::BillboardHandle::ExplosionFlash
      ) == 1U &&
      billboardCount(
        lowRocketMuzzleScene,
        lg::BillboardHandle::ExplosionHalo
      ) == 0U,
    "low quality rocket muzzle should keep the compact flame and pale core without a halo"
  );
  failures += expect(
    fullRocketMuzzleScene.transientVfxStats.muzzleFlashInstancesSubmitted == 2U &&
      fullRocketMuzzleScene.translucentVertices.size() == 24U &&
      billboardCount(
        fullRocketMuzzleScene,
        lg::BillboardHandle::ExplosionFlash
      ) == 1U &&
      billboardCount(
        fullRocketMuzzleScene,
        lg::BillboardHandle::ExplosionHalo
      ) == 1U &&
      fullRocketMuzzleScene.transientVfxStats.transparentEffectsSubmitted == 4U,
    "full rocket muzzle should add one restrained halo to the two-part flame"
  );
  failures += expect(
    noBloomRocketMuzzleScene.simpleInstances.size() ==
        fullRocketMuzzleScene.simpleInstances.size() &&
      noBloomRocketMuzzleScene.translucentVertices.size() ==
        fullRocketMuzzleScene.translucentVertices.size() &&
      largestBillboardScale(
        noBloomRocketMuzzleScene,
        lg::BillboardHandle::ExplosionFlash
      ) == largestBillboardScale(
        fullRocketMuzzleScene,
        lg::BillboardHandle::ExplosionFlash
      ),
    "bloom off should preserve the readable rocket muzzle geometry"
  );
  std::array<lg::TransientEffect, 4> combatEffects = {};
  combatEffects[0] = {
    lg::TransientEffectType::MachineGunMuzzleLight,
    machineGunFires[0].start,
    0.01F,
    0.045F,
    1.0F,
    1.0F,
    {255, 154, 62, 255},
    10U,
  };
  combatEffects[0].intensity = 2.4F;
  combatEffects[0].radius = 3.2F;
  combatEffects[1] = {
    lg::TransientEffectType::MachineGunCasing,
    machineGunFires[0].start + lg::Vec3{1.0F, 0.15F, 0.0F},
    0.2F,
    2.4F,
    0.032F,
    0.032F,
    {216, 166, 70, 255},
    11U,
  };
  combatEffects[1].velocity = {1.0F, 2.5F, 1.5F};
  combatEffects[2] = {
    lg::TransientEffectType::BulletImpactFlash,
    machineGunFires[0].start + lg::Vec3{3.0F, 0.0F, 0.0F},
    0.01F,
    0.055F,
    0.07F,
    0.02F,
    {244, 186, 94, 215},
    12U,
  };
  combatEffects[3] = {
    lg::TransientEffectType::BulletDecal,
    machineGunFires[0].start + lg::Vec3{3.02F, 0.0F, 0.0F},
    0.5F,
    24.0F,
    0.04F,
    0.04F,
    {48, 42, 36, 190},
    13U,
  };
  combatEffects[3].normal = {-1.0F, 0.0F, 0.0F};
  combatEffects[3].direction = {0.0F, 1.0F, 0.0F};
  const lg::Scene3D combatEffectsScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(),
    combatEffects,
    settings
  );
  const bool combatEffectsSubmitted =
    combatEffectsScene.temporaryLights.size() == 1U &&
    combatEffectsScene.transientVfxStats.activeTemporaryLights == 1U &&
    combatEffectsScene.transientVfxStats.activeCasings == 1U &&
    combatEffectsScene.transientVfxStats.activeImpactParticles == 1U &&
    combatEffectsScene.transientVfxStats.activeBulletDecals == 1U &&
    combatEffectsScene.transientVfxStats.explosionCandidates == 0U &&
    combatEffectsScene.transientVfxStats.explosionFrustumCulled == 0U &&
    combatEffectsScene.transientVfxStats.explosionInstancesSubmitted == 0U;
  if (!combatEffectsSubmitted) {
    std::cerr << "combat effect stats: lights="
              << combatEffectsScene.transientVfxStats.activeTemporaryLights
              << " casings="
              << combatEffectsScene.transientVfxStats.activeCasings
              << " particles="
              << combatEffectsScene.transientVfxStats.activeImpactParticles
              << " decals="
              << combatEffectsScene.transientVfxStats.activeBulletDecals
              << '\n';
  }
  failures += expect(
    combatEffectsSubmitted,
    "typed combat effects should reach their bounded scene render paths"
  );

  for (std::size_t index = 0; index < 6U; ++index) {
    tracerInstances[index] = {
      shotgunFires[0].start,
      shotgunFires[0].start + lg::Vec3{4.0F, static_cast<float>(index) * 0.08F, 0.0F},
      0.0F,
      0.065F,
      0.008F,
      {230, 180, 96, 150},
      static_cast<std::uint32_t>(index),
      lg::TracerStyle::Shotgun,
    };
  }
  const lg::Scene3D shotgunTracerScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 6U),
    settings
  );
  failures += expect(
    shotgunTracerScene.transientVfxStats.tracerInstancesSubmitted == 6 &&
      shotgunTracerScene.transientVfxStats.activeShotgunTracers == 6 &&
      shotgunTracerScene.transientVfxStats.tracerBatches == 1 &&
      shotgunTracerScene.transientVfxStats.tracerDrawCalls == 1 &&
      shotgunTracerScene.translucentVertices.size() == 72U,
    "SG representative tracers should batch into one transparent draw"
  );
  const lg::Vec3 sharedPelletDirection = lg::shotgunPelletDirection(
    {1.0F, 0.0F, 0.0F},
    {0.0F, -1.0F, 0.0F},
    {0.0F, 0.0F, 1.0F},
    0.0872665F,
    5U
  );
  failures += expect(
    std::isfinite(sharedPelletDirection.x) &&
      std::isfinite(sharedPelletDirection.y) &&
      std::isfinite(sharedPelletDirection.z) &&
      lg::length(sharedPelletDirection) > 0.99F,
    "shotgun tracer directions should come from the finite shared pellet-spread helper"
  );
  tracerInstances[0].start = player.position + lg::Vec3{-100.0F, 0.0F, 0.0F};
  tracerInstances[0].end = player.position + lg::Vec3{-90.0F, 0.0F, 0.0F};
  const lg::Scene3D culledTracerScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    settings
  );
  failures += expect(
    culledTracerScene.transientVfxStats.tracerCandidates == 1 &&
      culledTracerScene.transientVfxStats.tracerFrustumCulled == 1 &&
      culledTracerScene.translucentVertices.empty(),
    "offscreen transient tracers should be frustum culled before geometry emission"
  );

  lg::RenderSettings localMachineGunSettings = settings;
  localMachineGunSettings.localSelectedWeapon = lg::Weapon::MachineGun;
  const lg::Scene3D localMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localMachineGunSettings
  );
  bool hasMachineGunBodyViewModel = false;
  bool hasMachineGunBarrelViewModel = false;
  for (const lg::StaticMeshInstance& instance : localMachineGunScene.staticMeshInstances) {
    hasMachineGunBodyViewModel = hasMachineGunBodyViewModel ||
      (instance.mesh == lg::MeshHandle::RemoteMachineGunBody &&
       instance.pass == lg::RenderPass::ViewModel);
    hasMachineGunBarrelViewModel = hasMachineGunBarrelViewModel ||
      (instance.mesh == lg::MeshHandle::RemoteMachineGunBarrels &&
       instance.pass == lg::RenderPass::ViewModel);
  }
  failures += expect(
    hasMachineGunBodyViewModel &&
      hasMachineGunBarrelViewModel &&
      localMachineGunScene.viewModelStats.drawCalls == 2 &&
      localMachineGunScene.viewModelStats.dynamicVertices == 0,
    "first-person machine gun should use separate static body and barrel viewmodel meshes"
  );

  lg::RenderSettings spinningMachineGunSettings = localMachineGunSettings;
  spinningMachineGunSettings.machineGunBarrelRotationRadians =
    3.14159265359F * 0.5F;
  const lg::Scene3D spinningMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    spinningMachineGunSettings
  );
  const auto findMachineGunPart = [](
    const lg::Scene3D& scene,
    lg::MeshHandle mesh
  ) -> const lg::StaticMeshInstance* {
    const auto found = std::find_if(
      scene.staticMeshInstances.begin(),
      scene.staticMeshInstances.end(),
      [mesh](const lg::StaticMeshInstance& instance) {
        return instance.mesh == mesh && instance.pass == lg::RenderPass::ViewModel;
      }
    );
    return found != scene.staticMeshInstances.end() ? &*found : nullptr;
  };
  const lg::StaticMeshInstance* idleBody = findMachineGunPart(
    localMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* spinningBody = findMachineGunPart(
    spinningMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* idleBarrels = findMachineGunPart(
    localMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  const lg::StaticMeshInstance* spinningBarrels = findMachineGunPart(
    spinningMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  const bool bodyUnchanged = idleBody != nullptr && spinningBody != nullptr &&
    nearlyEqual(idleBody->modelRow0.x, spinningBody->modelRow0.x) &&
    nearlyEqual(idleBody->modelRow1.y, spinningBody->modelRow1.y) &&
    nearlyEqual(idleBody->modelRow2.z, spinningBody->modelRow2.z);
  bool barrelsRotateAroundAuthoredPivot = false;
  if (idleBarrels != nullptr && spinningBarrels != nullptr) {
    const lg::Vec3 viewModelPivot = lg::machineGunBarrelPivot();
    const lg::Vec3 idlePivot = transformPoint(
      *idleBarrels,
      viewModelPivot
    );
    const lg::Vec3 spinningPivot = transformPoint(
      *spinningBarrels,
      viewModelPivot
    );
    barrelsRotateAroundAuthoredPivot =
      lg::length(idlePivot - spinningPivot) < 0.001F &&
      !nearlyEqual(idleBarrels->modelRow1.y, spinningBarrels->modelRow1.y);
  }
  failures += expect(
    bodyUnchanged && barrelsRotateAroundAuthoredPivot,
    "machine-gun barrel rotation should preserve its authored pivot and leave the body fixed"
  );
  lg::RenderSettings recoilingMachineGunSettings = localMachineGunSettings;
  recoilingMachineGunSettings.machineGunRecoilAmount = 1.0F;
  recoilingMachineGunSettings.machineGunVibrationAmount = 1.0F;
  recoilingMachineGunSettings.machineGunVibrationPhaseRadians = 0.75F;
  const lg::Scene3D recoilingMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    recoilingMachineGunSettings
  );
  const lg::StaticMeshInstance* recoilingBody = findMachineGunPart(
    recoilingMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* recoilingBarrels = findMachineGunPart(
    recoilingMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  bool authoredSocketsMatchAllWeaponPositions = true;
  for (int weaponPosition = 0; weaponPosition < 3; ++weaponPosition) {
    lg::RenderSettings positionedSettings = localMachineGunSettings;
    positionedSettings.weaponPosition = weaponPosition;
    const lg::Scene3D positionedScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      opponent,
      inactiveBeam,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      positionedSettings
    );
    const lg::StaticMeshInstance* positionedBody = findMachineGunPart(
      positionedScene,
      lg::MeshHandle::RemoteMachineGunBody
    );
    authoredSocketsMatchAllWeaponPositions =
      authoredSocketsMatchAllWeaponPositions &&
      positionedBody != nullptr &&
      lg::length(
        transformPoint(*positionedBody, lg::machineGunMuzzleSocket()) -
        lg::firstPersonMachineGunMuzzlePosition(player, positionedSettings)
      ) < 0.001F &&
      lg::length(
        transformPoint(*positionedBody, lg::machineGunCasingEjectSocket()) -
        lg::firstPersonMachineGunCasingEjectPosition(player, positionedSettings)
      ) < 0.001F;
  }
  failures += expect(
    authoredSocketsMatchAllWeaponPositions,
    "MG muzzle and casing origins should match their authored sockets in every weapon position"
  );
  failures += expect(
    idleBody != nullptr && idleBarrels != nullptr &&
      recoilingBody != nullptr && recoilingBarrels != nullptr &&
      lg::length(recoilingBody->modelTranslation - idleBody->modelTranslation) > 0.001F &&
      lg::length(
        recoilingBarrels->modelTranslation - idleBarrels->modelTranslation
      ) > 0.001F,
    "MG firing response should move body and barrels together without changing gameplay state"
  );

  lg::RenderSettings hiddenLocalWeaponSettings = localMachineGunSettings;
  hiddenLocalWeaponSettings.showOwnWeapons = false;
  const lg::Scene3D hiddenLocalWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    hiddenLocalWeaponSettings
  );
  bool hasHiddenMachineGunViewModel = false;
  for (const lg::StaticMeshInstance& instance :
       hiddenLocalWeaponScene.staticMeshInstances) {
    hasHiddenMachineGunViewModel =
      hasHiddenMachineGunViewModel ||
      (
        (instance.mesh == lg::MeshHandle::RemoteMachineGunBody ||
         instance.mesh == lg::MeshHandle::RemoteMachineGunBarrels) &&
        instance.pass == lg::RenderPass::ViewModel
      );
  }
  failures += expect(
    !hasHiddenMachineGunViewModel &&
      hiddenLocalWeaponScene.viewModelStats.drawCalls == 0 &&
      hiddenLocalWeaponScene.viewModelStats.dynamicVertices == 0,
    "r_show_weapons 0 should suppress local first-person weapon models"
  );

  lg::RenderSettings localShotgunSettings = settings;
  localShotgunSettings.localSelectedWeapon = lg::Weapon::Shotgun;
  const lg::Scene3D localShotgunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localShotgunSettings
  );
  bool hasShotgunViewModel = false;
  for (const lg::StaticMeshInstance& instance : localShotgunScene.staticMeshInstances) {
    hasShotgunViewModel =
      hasShotgunViewModel ||
      (
        instance.mesh == lg::MeshHandle::RemoteShotgun &&
        instance.pass == lg::RenderPass::ViewModel
      );
  }
  failures += expect(
    hasShotgunViewModel &&
      localShotgunScene.viewModelStats.drawCalls == 1 &&
      localShotgunScene.viewModelStats.dynamicVertices == 0,
    "first-person shotgun should use a static viewmodel mesh without dynamic vertices"
  );

  lg::RenderSettings localSniperSettings = settings;
  localSniperSettings.localSelectedWeapon = lg::Weapon::Railgun;
  const lg::Scene3D localSniperScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localSniperSettings
  );
  bool hasSniperViewModel = false;
  lg::StaticMeshInstance sniperViewModel = {};
  for (const lg::StaticMeshInstance& instance : localSniperScene.staticMeshInstances) {
    if (
      instance.mesh == lg::MeshHandle::RemoteRailgun &&
      instance.pass == lg::RenderPass::ViewModel
    ) {
      hasSniperViewModel = true;
      sniperViewModel = instance;
    }
  }
  const float sniperForwardScale = lg::length({
    sniperViewModel.modelRow0.x,
    sniperViewModel.modelRow1.x,
    sniperViewModel.modelRow2.x,
  });
  const float sniperWidthScale = lg::length({
    sniperViewModel.modelRow0.y,
    sniperViewModel.modelRow1.y,
    sniperViewModel.modelRow2.y,
  });
  const float sniperHeightScale = lg::length({
    sniperViewModel.modelRow0.z,
    sniperViewModel.modelRow1.z,
    sniperViewModel.modelRow2.z,
  });
  failures += expect(
    hasSniperViewModel &&
      nearlyEqual(sniperForwardScale, 0.725F) &&
      nearlyEqual(sniperWidthScale, 0.9425F) &&
      nearlyEqual(sniperHeightScale, 0.83375F) &&
      localSniperScene.viewModelStats.drawCalls == 1 &&
      localSniperScene.viewModelStats.dynamicVertices == 0,
    "first-person sniper should use its larger, thicker view-only mesh"
  );
  const lg::Vec3 localSniperMuzzle =
    lg::firstPersonSniperRifleMuzzlePosition(player, localSniperSettings);
  lg::RenderSettings swayedSniperSettings = localSniperSettings;
  swayedSniperSettings.viewModelPresentation.translation = {0.06F, -0.04F, 0.03F};
  swayedSniperSettings.viewModelPresentation.rotationRadians = {0.08F, -0.06F, 0.03F};
  swayedSniperSettings.viewModelPresentation.cameraTranslation =
    {0.025F, -0.015F, 0.010F};
  const lg::Scene3D swayedSniperScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    swayedSniperSettings
  );
  lg::StaticMeshInstance swayedSniperViewModel = {};
  bool hasSwayedSniperViewModel = false;
  for (const lg::StaticMeshInstance& instance : swayedSniperScene.staticMeshInstances) {
    if (
      instance.mesh == lg::MeshHandle::RemoteRailgun &&
      instance.pass == lg::RenderPass::ViewModel
    ) {
      swayedSniperViewModel = instance;
      hasSwayedSniperViewModel = true;
      break;
    }
  }
  const lg::Vec3 swayedSniperMuzzle =
    lg::firstPersonSniperRifleMuzzlePosition(player, swayedSniperSettings);
  failures += expect(
    lg::length(
      transformPoint(sniperViewModel, lg::sniperRifleMuzzleSocket()) -
      localSniperMuzzle
    ) < 0.001F &&
      hasSwayedSniperViewModel &&
      lg::length(
        transformPoint(swayedSniperViewModel, lg::sniperRifleMuzzleSocket()) -
        swayedSniperMuzzle
      ) < 0.001F &&
      lg::length(swayedSniperMuzzle - localSniperMuzzle) > 0.01F,
    "local sniper tracer origin should match the live authored socket through sway and recoil motion"
  );

  lg::RenderSettings localRevolverSettings = settings;
  localRevolverSettings.localSelectedWeapon = lg::Weapon::Revolver;
  const lg::Scene3D localRevolverScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localRevolverSettings
  );
  const lg::RenderSettings idleRevolverSettings = localRevolverSettings;
  localRevolverSettings.revolverRecoilAmount = 1.0F;
  localRevolverSettings.revolverCylinderRotationRadians =
    3.14159265359F / 3.0F;
  const lg::Scene3D recoiledRevolverScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localRevolverSettings
  );
  const auto findViewModel = [](const lg::Scene3D& scene, lg::MeshHandle mesh) {
    for (const lg::StaticMeshInstance& instance : scene.staticMeshInstances) {
      if (instance.mesh == mesh && instance.pass == lg::RenderPass::ViewModel) {
        return instance;
      }
    }
    return lg::StaticMeshInstance{};
  };
  const lg::StaticMeshInstance revolverBody = findViewModel(
    localRevolverScene,
    lg::MeshHandle::RemoteRevolverBody
  );
  const lg::StaticMeshInstance recoiledRevolverBody = findViewModel(
    recoiledRevolverScene,
    lg::MeshHandle::RemoteRevolverBody
  );
  const lg::StaticMeshInstance revolverCylinder = findViewModel(
    localRevolverScene,
    lg::MeshHandle::RemoteRevolverCylinder
  );
  const lg::StaticMeshInstance indexedRevolverCylinder = findViewModel(
    recoiledRevolverScene,
    lg::MeshHandle::RemoteRevolverCylinder
  );
  const float revolverViewModelScale = lg::length(revolverBody.modelRow0);
  const bool revolverSocketMatchesViewModel =
    lg::length(
      transformPoint(revolverBody, lg::revolverMuzzleSocket()) -
      lg::firstPersonRevolverMuzzlePosition(
        player,
        idleRevolverSettings
      )
    ) < 0.001F;
  failures += expect(
    revolverBody.mesh == lg::MeshHandle::RemoteRevolverBody &&
      revolverCylinder.mesh == lg::MeshHandle::RemoteRevolverCylinder &&
      nearlyEqual(revolverViewModelScale, 0.40F) &&
      revolverSocketMatchesViewModel &&
      localRevolverScene.viewModelStats.drawCalls == 2 &&
      lg::length(
        recoiledRevolverBody.modelTranslation - revolverBody.modelTranslation
      ) > 0.01F &&
      lg::length(
        indexedRevolverCylinder.modelRow1 - revolverCylinder.modelRow1
      ) > 0.01F,
    "first-person revolver should submit body and cylinder with recoil and one-step indexing transforms"
  );
  std::array<lg::TransientTracer, 1> revolverFlash = {{
    {
      lg::firstPersonRevolverMuzzlePosition(player, idleRevolverSettings),
      lg::firstPersonRevolverMuzzlePosition(player, idleRevolverSettings) +
        lg::Vec3{0.22F, 0.0F, 0.0F},
      0.0F,
      0.052F,
      0.062F,
      {255, 212, 118, 245},
      19U,
      lg::TracerStyle::RevolverMuzzleFlash,
    },
  }};
  const lg::Scene3D revolverFlashScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(revolverFlash),
    idleRevolverSettings
  );
  const std::size_t revolverFlashBillboards = static_cast<std::size_t>(
    std::count_if(
      revolverFlashScene.simpleInstances.begin(),
      revolverFlashScene.simpleInstances.end(),
      [](const lg::SimpleRenderInstance& instance) {
        return instance.pass == lg::RenderPass::AdditiveGlow &&
          (
            instance.billboard == lg::BillboardHandle::ExplosionFlash ||
            instance.billboard == lg::BillboardHandle::ExplosionHalo
          );
      }
    )
  );
  failures += expect(
    revolverFlashScene.transientVfxStats.activeRevolverMuzzleFlashes == 1U &&
      revolverFlashBillboards == 2U &&
      revolverFlashScene.translucentVertices.size() == 12U,
    "revolver muzzle flash should combine a warm directional flame, flash, and halo"
  );

  lg::RenderSettings localRocketLauncherSettings = settings;
  localRocketLauncherSettings.localSelectedWeapon = lg::Weapon::RocketLauncher;
  const lg::Scene3D idleRocketLauncherScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localRocketLauncherSettings
  );
  localRocketLauncherSettings.rocketLauncherMechanicalAmount = 1.0F;
  localRocketLauncherSettings.rocketLauncherRecoilAmount = 1.0F;
  const lg::Scene3D firingRocketLauncherScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localRocketLauncherSettings
  );
  const lg::StaticMeshInstance idleRocketBody = findViewModel(
    idleRocketLauncherScene,
    lg::MeshHandle::RemoteRocketLauncherBody
  );
  const lg::StaticMeshInstance idleRocketRecoil = findViewModel(
    idleRocketLauncherScene,
    lg::MeshHandle::RemoteRocketLauncherRecoil
  );
  const lg::StaticMeshInstance firingRocketBody = findViewModel(
    firingRocketLauncherScene,
    lg::MeshHandle::RemoteRocketLauncherBody
  );
  const lg::StaticMeshInstance firingRocketRecoil = findViewModel(
    firingRocketLauncherScene,
    lg::MeshHandle::RemoteRocketLauncherRecoil
  );
  failures += expect(
    idleRocketBody.mesh == lg::MeshHandle::RemoteRocketLauncherBody &&
      idleRocketRecoil.mesh == lg::MeshHandle::RemoteRocketLauncherRecoil &&
      idleRocketLauncherScene.viewModelStats.drawCalls == 3U &&
      lg::length(firingRocketBody.modelTranslation - idleRocketBody.modelTranslation) > 0.001F &&
      lg::length(firingRocketRecoil.modelTranslation - idleRocketRecoil.modelTranslation) > 0.001F &&
      lg::length(
        lg::firstPersonRocketLauncherMuzzlePosition(
          player,
          localRocketLauncherSettings
        ) -
        transformPoint(
          firingRocketRecoil,
          lg::rocketLauncherMuzzleSocket() - lg::Vec3{0.5F, 0.0F, 0.08F}
        )
      ) < 0.001F,
    "first-person rocket launcher should submit three animated material parts and preserve its muzzle socket"
  );
  lg::RemotePlayerView remoteRocketSocketView;
  remoteRocketSocketView.player = opponent;
  remoteRocketSocketView.selectedWeapon = lg::Weapon::RocketLauncher;
  remoteRocketSocketView.visible = true;
  const lg::Vec3 remoteRocketIdleMuzzle =
    lg::remoteRocketLauncherMuzzlePosition(
      remoteRocketSocketView,
      settings
    );
  remoteRocketSocketView.rocketLauncherMechanicalAmount = 1.0F;
  const lg::Vec3 remoteRocketMechanicalMuzzle =
    lg::remoteRocketLauncherMuzzlePosition(
      remoteRocketSocketView,
      settings
    );
  remoteRocketSocketView.player.viewYawRadians += 0.45F;
  const lg::Vec3 remoteRocketTurnedMuzzle =
    lg::remoteRocketLauncherMuzzlePosition(
      remoteRocketSocketView,
      settings
    );
  failures += expect(
    finiteVec3(remoteRocketIdleMuzzle) &&
      lg::length(remoteRocketMechanicalMuzzle - remoteRocketIdleMuzzle) >
        0.01F &&
      lg::length(remoteRocketTurnedMuzzle - remoteRocketMechanicalMuzzle) >
        0.01F,
    "remote rocket muzzle helper should follow mechanism motion and pose turns"
  );

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount>
    workerRocketRemotePlayers = {};
  workerRocketRemotePlayers[1] = remoteRocketSocketView;
  workerRocketRemotePlayers[1].player.viewYawRadians -= 0.45F;
  workerRocketRemotePlayers[1].rocketLauncherMechanicalAmount = 0.65F;
  workerRocketRemotePlayers[1].hasPresentation = true;
  workerRocketRemotePlayers[1].presentation.poseLayerCount = 1U;
  workerRocketRemotePlayers[1].presentation.poseLayers[0] = {
    "Idle_Gun_TwoHanded",
    0.0F,
    1.0F,
  };
  lg::RenderSettings workerRocketSettings = settings;
  workerRocketSettings.playerModel = 2;
  workerRocketSettings.frustumCullRemotePlayers = false;
  std::array<lg::Vec3, 2> workerRocketRenderedMuzzles = {};
  bool workerRocketSocketMatches = true;
  constexpr std::array<float, 2> kWorkerRocketFrameTimes = {
    0.1666667F,
    0.6666667F,
  };
  for (std::size_t frameIndex = 0;
       frameIndex < kWorkerRocketFrameTimes.size();
       ++frameIndex) {
    workerRocketRemotePlayers[1].presentation.poseLayers[0].timeSeconds =
      kWorkerRocketFrameTimes[frameIndex];
    const lg::Scene3D workerRocketScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      workerRocketRemotePlayers,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      workerRocketSettings
    );
    lg::StaticMeshInstance renderedRecoil = {};
    bool foundRenderedRecoil = false;
    for (const lg::StaticMeshInstance& instance :
         workerRocketScene.staticMeshInstances) {
      if (
        instance.mesh == lg::MeshHandle::RemoteRocketLauncherRecoil &&
        instance.pass == lg::RenderPass::OpaqueWorld
      ) {
        renderedRecoil = instance;
        foundRenderedRecoil = true;
        break;
      }
    }
    workerRocketRenderedMuzzles[frameIndex] = transformPoint(
      renderedRecoil,
      lg::rocketLauncherMuzzleSocket() - lg::Vec3{0.5F, 0.0F, 0.08F}
    );
    const lg::Vec3 effectMuzzle = lg::remoteRocketLauncherMuzzlePosition(
      workerRocketRemotePlayers[1],
      workerRocketSettings
    );
    workerRocketSocketMatches =
      workerRocketSocketMatches &&
      foundRenderedRecoil &&
      lg::length(effectMuzzle - workerRocketRenderedMuzzles[frameIndex]) <
        0.001F;
  }
  failures += expect(
    workerRocketSocketMatches &&
      lg::length(
        workerRocketRenderedMuzzles[1] - workerRocketRenderedMuzzles[0]
      ) > 0.001F,
    "Worker rocket muzzle effects should match its animated rendered weapon socket across frames"
  );

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount>
    workerRailRemotePlayers = workerRocketRemotePlayers;
  workerRailRemotePlayers[1].selectedWeapon = lg::Weapon::Railgun;
  std::array<lg::Vec3, 2> workerRailRenderedMuzzles = {};
  bool workerRailSocketMatches = true;
  for (std::size_t frameIndex = 0;
       frameIndex < kWorkerRocketFrameTimes.size();
       ++frameIndex) {
    workerRailRemotePlayers[1].presentation.poseLayers[0].timeSeconds =
      kWorkerRocketFrameTimes[frameIndex];
    const lg::Scene3D workerRailScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      workerRailRemotePlayers,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      workerRocketSettings
    );
    lg::StaticMeshInstance renderedRail = {};
    bool foundRenderedRail = false;
    for (const lg::StaticMeshInstance& instance : workerRailScene.staticMeshInstances) {
      if (
        instance.mesh == lg::MeshHandle::RemoteRailgun &&
        instance.pass == lg::RenderPass::OpaqueWorld
      ) {
        renderedRail = instance;
        foundRenderedRail = true;
        break;
      }
    }
    workerRailRenderedMuzzles[frameIndex] = transformPoint(
      renderedRail,
      lg::sniperRifleMuzzleSocket()
    );
    workerRailSocketMatches =
      workerRailSocketMatches &&
      foundRenderedRail &&
      lg::length(
        lg::remoteSniperRifleMuzzlePosition(
          workerRailRemotePlayers[1],
          workerRocketSettings
        ) - workerRailRenderedMuzzles[frameIndex]
      ) < 0.001F;
  }
  failures += expect(
    workerRailSocketMatches &&
      lg::length(
        workerRailRenderedMuzzles[1] - workerRailRenderedMuzzles[0]
      ) > 0.001F,
    "remote sniper tracer origin should match the live held-weapon socket across Worker poses"
  );

  lg::RenderSettings localPlasmaGunSettings = settings;
  localPlasmaGunSettings.localSelectedWeapon = lg::Weapon::PlasmaGun;
  const lg::Scene3D idlePlasmaGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    {},
    {},
    localPlasmaGunSettings
  );
  lg::RenderSettings firingPlasmaGunSettings = localPlasmaGunSettings;
  firingPlasmaGunSettings.plasmaGunContainmentAmount = 1.0F;
  const lg::Scene3D firingPlasmaGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    {},
    {},
    firingPlasmaGunSettings
  );
  const lg::StaticMeshInstance idlePlasmaBody = findViewModel(
    idlePlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunBody
  );
  const lg::StaticMeshInstance idlePlasmaProngs = findViewModel(
    idlePlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunProngs
  );
  const lg::StaticMeshInstance idlePlasmaCore = findViewModel(
    idlePlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunCore
  );
  const lg::StaticMeshInstance firingPlasmaProngs = findViewModel(
    firingPlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunProngs
  );
  const lg::StaticMeshInstance firingPlasmaCore = findViewModel(
    firingPlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunCore
  );
  failures += expect(
    idlePlasmaBody.mesh == lg::MeshHandle::RemotePlasmaGunBody &&
      idlePlasmaProngs.mesh == lg::MeshHandle::RemotePlasmaGunProngs &&
      idlePlasmaCore.mesh == lg::MeshHandle::RemotePlasmaGunCore &&
      idlePlasmaGunScene.viewModelStats.drawCalls == 3U,
    "first-person plasma gun should submit its three material meshes"
  );
  failures += expect(
    lg::length(
      transformPoint(idlePlasmaBody, lg::plasmaGunMuzzleSocket()) -
      lg::firstPersonPlasmaGunMuzzlePosition(player, localPlasmaGunSettings)
    ) < 0.001F,
    "first-person plasma projectile origin should match the authored muzzle socket"
  );
  failures += expect(
    lg::length(firingPlasmaCore.modelRow0) <
      lg::length(idlePlasmaCore.modelRow0) &&
      lg::length(
        firingPlasmaProngs.modelTranslation -
        idlePlasmaProngs.modelTranslation
      ) > 0.005F,
    "plasma firing response should contract the core and shift the containment prongs"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> remoteMachineGunFires = {};
  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> machineGunRemotePlayers = {};
  machineGunRemotePlayers[1] =
    lg::RemotePlayerView{
      opponent,
      inactiveBeam,
      lg::Weapon::MachineGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  remoteMachineGunFires[1].fired = true;
  remoteMachineGunFires[1].hit = true;
  remoteMachineGunFires[1].weapon = lg::Weapon::MachineGun;
  remoteMachineGunFires[1].start =
    opponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  remoteMachineGunFires[1].end =
    remoteMachineGunFires[1].start + lg::Vec3{-9.0F, 0.0F, 0.0F};
  remoteMachineGunFires[1].visualSeed = 1;
  const lg::Scene3D remoteMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    machineGunRemotePlayers,
    inactiveBeam,
    remoteMachineGunFires,
    rocketExplosions,
    rockets,
    settings
  );
  machineGunRemotePlayers[1].machineGunBarrelRotationRadians =
    3.14159265359F * 0.5F;
  remoteMachineGunFires[1].visualSeed = 4;
  const lg::Scene3D rotatedMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    machineGunRemotePlayers,
    inactiveBeam,
    remoteMachineGunFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    remoteMachineGunScene.transientVfxStats.tracerInstancesSubmitted == 0 &&
      rotatedMachineGunScene.transientVfxStats.tracerInstancesSubmitted == 0,
    "remote machine gun retained fires should wait for transient VFX consumption"
  );
  const auto findRemoteMachineGunPart = [](
    const lg::Scene3D& scene,
    lg::MeshHandle mesh
  ) -> const lg::StaticMeshInstance* {
    const auto found = std::find_if(
      scene.staticMeshInstances.begin(),
      scene.staticMeshInstances.end(),
      [mesh](const lg::StaticMeshInstance& instance) {
        return instance.mesh == mesh && instance.pass == lg::RenderPass::OpaqueWorld;
      }
    );
    return found != scene.staticMeshInstances.end() ? &*found : nullptr;
  };
  const lg::StaticMeshInstance* idleRemoteBody = findRemoteMachineGunPart(
    remoteMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* spinningRemoteBody = findRemoteMachineGunPart(
    rotatedMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* idleRemoteBarrels = findRemoteMachineGunPart(
    remoteMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  const lg::StaticMeshInstance* spinningRemoteBarrels = findRemoteMachineGunPart(
    rotatedMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  failures += expect(
    idleRemoteBody != nullptr && spinningRemoteBody != nullptr &&
      idleRemoteBarrels != nullptr && spinningRemoteBarrels != nullptr &&
      nearlyEqual(idleRemoteBody->modelRow1.y, spinningRemoteBody->modelRow1.y) &&
      !nearlyEqual(
        idleRemoteBarrels->modelRow1.y,
        spinningRemoteBarrels->modelRow1.y
      ),
    "remote machine-gun presentation angle should rotate only its barrel instance"
  );
  auto pitchedMachineGunPlayers = machineGunRemotePlayers;
  pitchedMachineGunPlayers[1].player.viewPitchRadians = 1.4F;
  const lg::Scene3D pitchedMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    pitchedMachineGunPlayers,
    inactiveBeam,
    remoteMachineGunFires,
    rocketExplosions,
    rockets,
    settings
  );
  const lg::StaticMeshInstance* pitchedRemoteBody = findRemoteMachineGunPart(
    pitchedMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  failures += expect(
    idleRemoteBody != nullptr && pitchedRemoteBody != nullptr &&
      pitchedRemoteBody->modelRow2.x > idleRemoteBody->modelRow2.x + 0.1F,
    "remote held weapon should visibly pitch toward the clamped head aim direction"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> remoteShotgunFires = {};
  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> shotgunRemotePlayers = {};
  lg::PlayerState shotgunOpponent = opponent;
  shotgunOpponent.viewYawRadians = 3.14159265359F;
  shotgunRemotePlayers[1] =
    lg::RemotePlayerView{
      shotgunOpponent,
      inactiveBeam,
      lg::Weapon::Shotgun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  remoteShotgunFires[1].fired = true;
  remoteShotgunFires[1].hit = true;
  remoteShotgunFires[1].weapon = lg::Weapon::Shotgun;
  remoteShotgunFires[1].start =
    shotgunOpponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  remoteShotgunFires[1].end =
    remoteShotgunFires[1].start + lg::Vec3{-8.0F, 0.0F, 0.0F};
  remoteShotgunFires[1].pelletCount = lg::kShotgunPelletCount;
  remoteShotgunFires[1].pelletHitCount = 5;
  lg::RenderSettings shotgunWeaponStartSettings = settings;
  shotgunWeaponStartSettings.shotgunWeaponModelStart = true;
  const lg::Scene3D remoteShotgunEyeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    remoteShotgunFires,
    rocketExplosions,
    rockets,
    settings
  );
  const lg::Scene3D remoteShotgunWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    remoteShotgunFires,
    rocketExplosions,
    rockets,
    shotgunWeaponStartSettings
  );
  failures += expect(
    remoteShotgunEyeScene.transientVfxStats.tracerInstancesSubmitted == 0 &&
      remoteShotgunWeaponScene.transientVfxStats.tracerInstancesSubmitted == 0,
    "remote shotgun retained fires should wait for transient VFX consumption"
  );

  lg::RenderSettings effectOnlySettings = settings;
  effectOnlySettings.showOwnWeapons = false;
  effectOnlySettings.drawRemotePlayers = false;
  effectOnlySettings.drawRemoteWeapons = false;

  lg::LightningGunResult remoteLightningBeam;
  remoteLightningBeam.active = true;
  remoteLightningBeam.start =
    shotgunOpponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  remoteLightningBeam.end =
    remoteLightningBeam.start + lg::Vec3{-8.0F, 0.0F, 0.0F};
  shotgunRemotePlayers[1].selectedWeapon = lg::Weapon::LightningGun;
  shotgunRemotePlayers[1].lightningGun = remoteLightningBeam;
  const lg::Scene3D remoteLightningMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    effectOnlySettings
  );
  failures += expect(
    hasAnyVertex(remoteLightningMuzzleScene) &&
      maxVertexX(remoteLightningMuzzleScene) <
        remoteLightningBeam.start.x - 0.2F,
    "remote lightning beam should start from the third-person weapon muzzle"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> remoteRailFires = {};
  remoteRailFires[1].fired = true;
  remoteRailFires[1].hit = true;
  remoteRailFires[1].weapon = lg::Weapon::Railgun;
  remoteRailFires[1].start =
    shotgunOpponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  remoteRailFires[1].end =
    remoteRailFires[1].start + lg::Vec3{-8.0F, 0.0F, 0.0F};
  shotgunRemotePlayers[1].selectedWeapon = lg::Weapon::Railgun;
  shotgunRemotePlayers[1].lightningGun = inactiveBeam;
  const lg::Scene3D remoteRailMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    remoteRailFires,
    rocketExplosions,
    rockets,
    effectOnlySettings
  );
  failures += expect(
    hasAnyVertex(remoteRailMuzzleScene) &&
      maxVertexX(remoteRailMuzzleScene) <
        remoteRailFires[1].start.x - 0.2F,
    "remote railgun beam should start from the third-person weapon muzzle"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> localRailSmokeFires = {};
  const lg::Vec3 localRailSmokeStart =
    lg::firstPersonSniperRifleMuzzlePosition(player, localSniperSettings);
  localRailSmokeFires[0].fired = true;
  localRailSmokeFires[0].weapon = lg::Weapon::Railgun;
  localRailSmokeFires[0].visualSeed = 117U;
  localRailSmokeFires[0].start = localRailSmokeStart;
  localRailSmokeFires[0].end = localRailSmokeStart + lg::Vec3{8.0F, 0.0F, 0.0F};
  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> noRailRemotes = {};
  lg::RenderSettings highRailSmokeSettings = localSniperSettings;
  highRailSmokeSettings.combatEffectsQuality = 2;
  const lg::Scene3D highRailSmokeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    noRailRemotes,
    inactiveBeam,
    localRailSmokeFires,
    rocketExplosions,
    rockets,
    highRailSmokeSettings
  );
  float maxRailSmokeDistance = 0.0F;
  std::uint8_t maximumRailSmokeAlpha = 0U;
  bool hasTransparentTail = false;
  bool hasNeutralGreySmoke = false;
  for (const lg::Vertex3D& vertex : highRailSmokeScene.translucentVertices) {
    maxRailSmokeDistance = std::max(
      maxRailSmokeDistance,
      lg::length(vertex.position - localRailSmokeStart)
    );
    maximumRailSmokeAlpha = std::max(maximumRailSmokeAlpha, vertex.color.alpha);
    hasTransparentTail = hasTransparentTail || vertex.color.alpha == 0U;
    hasNeutralGreySmoke = hasNeutralGreySmoke ||
      (
        vertex.color.red >= 120U &&
        std::abs(
          static_cast<int>(vertex.color.blue) -
          static_cast<int>(vertex.color.red)
        ) <= 12
      );
  }
  failures += expect(
    highRailSmokeScene.transientVfxStats.activeSniperSmokeTracers == 1U &&
      highRailSmokeScene.transientVfxStats.sniperSmokeTracerDynamicVertices == 24U &&
      highRailSmokeScene.translucentVertices.size() == 24U &&
      maxRailSmokeDistance <= lg::kSniperSmokeTracerMaximumLength + 0.05F &&
      maximumRailSmokeAlpha >= 200U && hasTransparentTail && hasNeutralGreySmoke,
    "sniper smoke should keep a short neutral-grey root-to-transparent trace"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> cameraRailSmokeFires = {};
  cameraRailSmokeFires[0] = localRailSmokeFires[0];
  cameraRailSmokeFires[0].start = {-8.0F, -8.0F, -8.0F};
  cameraRailSmokeFires[0].end = {8.0F, 8.0F, 8.0F};
  const float cameraStepOffset = 0.075F;
  lg::RenderSettings cameraRailSmokeSettings = swayedSniperSettings;
  cameraRailSmokeSettings.frustumCullRemotePlayers = false;
  const lg::Scene3D cameraRailSmokeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    noRailRemotes,
    inactiveBeam,
    cameraRailSmokeFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>{},
    std::span<const lg::IcePool>{},
    cameraRailSmokeSettings,
    cameraStepOffset
  );
  const lg::Vec3 expectedCameraRailMuzzle =
    lg::firstPersonSniperRifleMuzzlePosition(player, cameraRailSmokeSettings) +
    lg::Vec3{0.0F, 0.0F, cameraStepOffset};
  float nearestCameraRailVertex = std::numeric_limits<float>::infinity();
  for (const lg::Vertex3D& vertex : cameraRailSmokeScene.translucentVertices) {
    nearestCameraRailVertex = std::min(
      nearestCameraRailVertex,
      lg::length(vertex.position - expectedCameraRailMuzzle)
    );
  }
  failures += expect(
    cameraRailSmokeScene.transientVfxStats.activeSniperSmokeTracers == 1U &&
      nearestCameraRailVertex < 0.05F,
    "local sniper smoke should track the rendered camera and viewmodel socket"
  );

  lg::RenderSettings fadedRailSmokeSettings = highRailSmokeSettings;
  fadedRailSmokeSettings.sniperSmokeTracerAlpha[0] = 0.40F;
  const lg::Scene3D fadedRailSmokeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    noRailRemotes,
    inactiveBeam,
    localRailSmokeFires,
    rocketExplosions,
    rockets,
    fadedRailSmokeSettings
  );
  std::uint8_t fadedMaximumAlpha = 0U;
  for (const lg::Vertex3D& vertex : fadedRailSmokeScene.translucentVertices) {
    fadedMaximumAlpha = std::max(fadedMaximumAlpha, vertex.color.alpha);
  }
  lg::RenderSettings lowRailSmokeSettings = highRailSmokeSettings;
  lowRailSmokeSettings.combatEffectsQuality = 0;
  lowRailSmokeSettings.bloomEnabled = false;
  const lg::Scene3D lowRailSmokeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    noRailRemotes,
    inactiveBeam,
    localRailSmokeFires,
    rocketExplosions,
    rockets,
    lowRailSmokeSettings
  );
  failures += expect(
    fadedRailSmokeScene.transientVfxStats.sniperSmokeTracerDynamicVertices == 24U &&
      fadedMaximumAlpha > 0U && fadedMaximumAlpha < maximumRailSmokeAlpha &&
      lowRailSmokeScene.transientVfxStats.activeSniperSmokeTracers == 1U &&
      lowRailSmokeScene.transientVfxStats.sniperSmokeTracerDynamicVertices == 12U &&
      lowRailSmokeScene.translucentVertices.size() == 12U,
    "sniper smoke should fade smoothly and retain one clear low-quality trace without bloom"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> culledRailSmokeFires = {};
  // Use a non-local event so the renderer does not replace its test origin
  // with the local viewmodel socket.
  culledRailSmokeFires[1] = localRailSmokeFires[0];
  culledRailSmokeFires[1].start = player.position + lg::Vec3{-100.0F, 0.0F, 0.0F};
  culledRailSmokeFires[1].end =
    culledRailSmokeFires[1].start + lg::Vec3{-8.0F, 0.0F, 0.0F};
  const lg::Scene3D culledRailSmokeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    noRailRemotes,
    inactiveBeam,
    culledRailSmokeFires,
    rocketExplosions,
    rockets,
    highRailSmokeSettings
  );
  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> cappedRailSmokeFires = {};
  for (std::size_t index = 0; index < cappedRailSmokeFires.size(); ++index) {
    cappedRailSmokeFires[index] = localRailSmokeFires[0];
    cappedRailSmokeFires[index].visualSeed = 117U + static_cast<std::uint32_t>(index);
  }
  const lg::Scene3D cappedRailSmokeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    noRailRemotes,
    inactiveBeam,
    cappedRailSmokeFires,
    rocketExplosions,
    rockets,
    highRailSmokeSettings
  );
  const lg::Scene3D repeatedRailSmokeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    noRailRemotes,
    inactiveBeam,
    cappedRailSmokeFires,
    rocketExplosions,
    rockets,
    highRailSmokeSettings
  );
  const bool deterministicRailSmoke =
    cappedRailSmokeScene.translucentVertices.size() ==
      repeatedRailSmokeScene.translucentVertices.size() &&
    std::equal(
      cappedRailSmokeScene.translucentVertices.begin(),
      cappedRailSmokeScene.translucentVertices.end(),
      repeatedRailSmokeScene.translucentVertices.begin(),
      [](const lg::Vertex3D& lhs, const lg::Vertex3D& rhs) {
        return lhs.position.x == rhs.position.x &&
          lhs.position.y == rhs.position.y &&
          lhs.position.z == rhs.position.z &&
          sameColor(lhs.color, rhs.color);
      }
    );
  failures += expect(
    culledRailSmokeScene.transientVfxStats.activeSniperSmokeTracers == 1U &&
      culledRailSmokeScene.transientVfxStats.sniperSmokeTracerFrustumCulled == 1U &&
      culledRailSmokeScene.translucentVertices.empty() &&
      cappedRailSmokeScene.transientVfxStats.activeSniperSmokeTracers ==
        lg::kDuelPlayerCount &&
      cappedRailSmokeScene.transientVfxStats.sniperSmokeTracerDynamicVertices ==
        lg::kDuelPlayerCount * 24U &&
      cappedRailSmokeScene.translucentVertices.size() ==
        lg::kDuelPlayerCount * 24U &&
      deterministicRailSmoke,
    "sniper smoke should cull offscreen work, stay within the fixed fire cap, and use deterministic contiguous translucent geometry"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> remoteRevolverFires = {};
  remoteRevolverFires[1] = remoteRailFires[1];
  remoteRevolverFires[1].weapon = lg::Weapon::Revolver;
  shotgunRemotePlayers[1].selectedWeapon = lg::Weapon::Revolver;
  lg::RenderSettings revolverEffectSettings = effectOnlySettings;
  revolverEffectSettings.revolverTracerAlpha[1] = 0.5F;
  const lg::Scene3D remoteRevolverMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    remoteRevolverFires,
    rocketExplosions,
    rockets,
    revolverEffectSettings
  );
  bool hasWarmRevolverTracer = false;
  for (const lg::Vertex3D& vertex : remoteRevolverMuzzleScene.translucentVertices) {
    hasWarmRevolverTracer = hasWarmRevolverTracer ||
      (
        vertex.color.red > vertex.color.green &&
        vertex.color.green > vertex.color.blue &&
        vertex.color.alpha > 80U &&
        vertex.color.alpha < 205U
      );
  }
  failures += expect(
    hasAnyVertex(remoteRevolverMuzzleScene) &&
      hasWarmRevolverTracer &&
      maxVertexX(remoteRevolverMuzzleScene) <
        remoteRevolverFires[1].start.x - 0.2F,
    "remote revolver beam should start from the modeled barrel socket"
  );

  std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> plasmaRockets = {};
  const lg::ProjectileVisualDescriptor* plasmaDescriptor =
    lg::projectileVisualDescriptor(lg::ProjectileVisualType::Plasma);
  failures += expect(
    plasmaDescriptor != nullptr &&
      plasmaDescriptor->coreMesh != lg::MeshHandle::Invalid &&
      plasmaDescriptor->glowBillboard != lg::BillboardHandle::Invalid &&
      lg::staticMeshAsset(plasmaDescriptor->coreMesh) != nullptr &&
      lg::billboardAsset(plasmaDescriptor->glowBillboard) != nullptr,
    "plasma projectile visual descriptor should resolve to core mesh and glow billboard assets"
  );
  plasmaRockets[0].active = true;
  plasmaRockets[0].owner = 1;
  plasmaRockets[0].weapon = lg::Weapon::PlasmaGun;
  plasmaRockets[0].position =
    shotgunOpponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  plasmaRockets[0].velocity = {-50.0F, 0.0F, 0.0F};
  shotgunRemotePlayers[1].selectedWeapon = lg::Weapon::LightningGun;
  const lg::Scene3D plasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    settings
  );
  failures += expect(
    plasmaProjectileScene.projectileStats.projectilesActive == 1 &&
      plasmaProjectileScene.projectileStats.projectilesRendered == 1 &&
      plasmaProjectileScene.projectileStats.projectileCoreInstances == 1 &&
      plasmaProjectileScene.projectileStats.projectileGlowInstances == 1 &&
      plasmaProjectileScene.projectileStats.projectileMeshDrawCalls == 1 &&
      plasmaProjectileScene.projectileStats.projectileGlowDrawCalls == 1 &&
      plasmaProjectileScene.projectileStats.legacyProjectileDynamicVertices == 0 &&
      plasmaProjectileScene.simpleInstances.size() == 2U &&
      plasmaProjectileScene.simpleBatches.size() == 2U,
    "active plasma projectile should produce one core instance, one glow instance, and no legacy vertices"
  );
  failures += expect(
    plasmaProjectileScene.simpleInstances[0].position.x <
      plasmaRockets[0].position.x - 0.35F,
    "remote plasma projectile instances should render from the plasma gun model"
  );

  plasmaRockets[0].owner = 0;
  plasmaRockets[0].position =
    player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  const lg::Scene3D localPlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    localShotgunWeaponStartSettings
  );
  failures += expect(
    localPlasmaProjectileScene.simpleInstances.size() == 2U &&
      localPlasmaProjectileScene.simpleInstances[0].position.z <
        plasmaRockets[0].position.z - 0.15F,
    "local plasma projectile instances should render from the first-person weapon muzzle"
  );

  plasmaRockets[0].active = false;
  const lg::Scene3D inactivePlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    settings
  );
  failures += expect(
    inactivePlasmaProjectileScene.projectileStats.projectilesActive == 0 &&
      inactivePlasmaProjectileScene.simpleInstances.empty(),
    "inactive plasma projectile should produce no render instances"
  );

  plasmaRockets[0].active = true;
  plasmaRockets[0].owner = 0;
  plasmaRockets[0].position = player.position + lg::Vec3{-12.0F, 0.0F, 0.65F};
  const lg::Scene3D culledPlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    settings
  );
  failures += expect(
    culledPlasmaProjectileScene.projectileStats.projectilesActive == 1 &&
      culledPlasmaProjectileScene.projectileStats.projectilesFrustumCulled == 1 &&
      culledPlasmaProjectileScene.simpleInstances.empty(),
    "outside-frustum plasma projectile should produce no render instances when culling is enabled"
  );
  const lg::Scene3D uncullablePlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    frustumCullDisabledSettings
  );
  failures += expect(
    uncullablePlasmaProjectileScene.projectileStats.projectilesRendered == 1 &&
      uncullablePlasmaProjectileScene.simpleInstances.size() == 2U,
    "outside-frustum plasma projectile should produce instances when culling is disabled"
  );

  plasmaRockets[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  plasmaRockets[1] = plasmaRockets[0];
  plasmaRockets[1].position = player.position + lg::Vec3{4.0F, 0.25F, 0.65F};
  const lg::Scene3D multiPlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    settings
  );
  failures += expect(
    multiPlasmaProjectileScene.projectileStats.projectileCoreInstances == 2 &&
      multiPlasmaProjectileScene.projectileStats.projectileGlowInstances == 2 &&
      multiPlasmaProjectileScene.projectileStats.projectileMeshDrawCalls == 1 &&
      multiPlasmaProjectileScene.projectileStats.projectileGlowDrawCalls == 1 &&
      multiPlasmaProjectileScene.simpleBatches.size() == 2U,
    "multiple plasma projectiles should group into one core batch and one glow batch"
  );
  failures += expect(
    !multiPlasmaProjectileScene.staticMeshBatches.empty() &&
      validStaticMeshBatches(multiPlasmaProjectileScene) &&
      validSimpleBatches(multiPlasmaProjectileScene) &&
      multiPlasmaProjectileScene.simpleBatches[0].firstInstance == 0U &&
      multiPlasmaProjectileScene.simpleBatches[0].instanceCount == 2U &&
      multiPlasmaProjectileScene.simpleBatches[1].firstInstance == 2U &&
      multiPlasmaProjectileScene.simpleBatches[1].instanceCount == 2U,
    "final batches should cover sorted interleaved mesh and pass instances"
  );
  failures += expect(
    multiPlasmaProjectileScene.projectileStats.projectileInstanceUploadBytes ==
      plasmaProjectileScene.projectileStats.projectileInstanceUploadBytes * 2U,
    "projectile instance upload bytes should scale by instance count, not mesh vertex count"
  );

  const lg::ProjectileVisualDescriptor* rocketDescriptor =
    lg::projectileVisualDescriptor(lg::ProjectileVisualType::Rocket);
  const lg::ProjectileVisualDescriptor* grenadeDescriptor =
    lg::projectileVisualDescriptor(lg::ProjectileVisualType::Grenade);
  const lg::StaticMeshAsset* rocketProjectileAsset =
    rocketDescriptor != nullptr
      ? lg::staticMeshAsset(rocketDescriptor->coreMesh)
      : nullptr;
  bool rocketHasHexShoulder = false;
  bool rocketHasFrontCap = false;
  bool rocketHasRearCap = false;
  if (rocketProjectileAsset != nullptr) {
    for (const lg::Vertex3D& vertex : rocketProjectileAsset->vertices) {
      rocketHasHexShoulder =
        rocketHasHexShoulder ||
        (
          std::fabs(vertex.position.y - 0.11F) < 0.001F &&
          std::fabs(vertex.position.z - 0.190526F) < 0.001F
        );
      rocketHasFrontCap =
        rocketHasFrontCap ||
        (
          std::fabs(vertex.position.x - 0.60F) < 0.001F &&
          std::fabs(vertex.position.y) < 0.001F &&
          std::fabs(vertex.position.z) < 0.001F
        );
      rocketHasRearCap =
        rocketHasRearCap ||
        (
          std::fabs(vertex.position.x + 0.64F) < 0.001F &&
          std::fabs(vertex.position.y) < 0.001F &&
          std::fabs(vertex.position.z) < 0.001F
        );
    }
  }
  failures += expect(
    rocketDescriptor != nullptr &&
      rocketDescriptor->coreMesh == lg::MeshHandle::RocketProjectile &&
      rocketDescriptor->glowBillboard == lg::BillboardHandle::RocketFlame &&
      rocketProjectileAsset != nullptr &&
      rocketProjectileAsset->vertices.size() == 144U &&
      rocketProjectileAsset->localBounds.radius > 0.65F &&
      rocketProjectileAsset->localBounds.radius < 0.67F &&
      rocketHasHexShoulder &&
      rocketHasFrontCap &&
      rocketHasRearCap &&
      lg::billboardAsset(rocketDescriptor->glowBillboard) != nullptr,
    "rocket projectile should use one cached six-sided pill mesh with beveled caps"
  );
  failures += expect(
    grenadeDescriptor != nullptr &&
      grenadeDescriptor->coreMesh == lg::MeshHandle::GrenadeProjectile &&
      grenadeDescriptor->glowBillboard == lg::BillboardHandle::Invalid &&
      lg::staticMeshAsset(grenadeDescriptor->coreMesh) != nullptr,
    "grenade projectile descriptor should resolve to grenade mesh without glow billboard"
  );

  std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> rocketProjectiles = {};
  rocketProjectiles[0].active = true;
  rocketProjectiles[0].owner = 1;
  rocketProjectiles[0].weapon = lg::Weapon::RocketLauncher;
  rocketProjectiles[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  rocketProjectiles[0].velocity = {30.0F, 0.0F, 0.0F};
  const lg::Scene3D rocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  failures += expect(
    rocketProjectileScene.projectileStats.projectilesRendered == 1 &&
      rocketProjectileScene.projectileStats.rocketInstances == 1 &&
      rocketProjectileScene.projectileStats.projectileGlowInstances == 2 &&
      rocketProjectileScene.projectileStats.opaqueProjectileBatches == 1 &&
      rocketProjectileScene.projectileStats.additiveProjectileBatches == 1 &&
      rocketProjectileScene.projectileStats.legacyProjectileDynamicVertices == 0 &&
      rocketProjectileScene.simpleInstances.size() == 3U &&
      rocketProjectileScene.temporaryLights.size() == 1U &&
      rocketProjectileScene.transientVfxStats.activeTemporaryLights == 1U,
    "full quality rocket should produce one core, two exhaust layers, and one local light"
  );
  const lg::SimpleRenderInstance* rocketCore = findSimpleMesh(
    rocketProjectileScene,
    lg::MeshHandle::RocketProjectile
  );
  failures += expect(
    rocketCore != nullptr &&
      rocketCore->pass == lg::RenderPass::OpaqueWorld &&
      rocketCore->position.x <
      rocketProjectiles[0].position.x - 0.35F,
    "remote rocket opaque core should render from the rocket launcher barrel"
  );
  failures += expect(
    rocketCore != nullptr &&
      rocketProjectileScene.temporaryLights.size() == 1U &&
      std::fabs(
        rocketProjectileScene.temporaryLights[0].position.x -
          (rocketCore->position.x - 0.328F)
      ) < 0.001F &&
      std::fabs(
        rocketProjectileScene.temporaryLights[0].position.y -
          rocketCore->position.y
      ) < 0.001F &&
      std::fabs(
        rocketProjectileScene.temporaryLights[0].position.z -
          rocketCore->position.z
      ) < 0.001F &&
      rocketProjectileScene.temporaryLights[0].intensity > 1.14F &&
      rocketProjectileScene.temporaryLights[0].intensity < 1.16F &&
      rocketProjectileScene.temporaryLights[0].radius > 2.19F &&
      rocketProjectileScene.temporaryLights[0].radius < 2.21F,
    "rocket local light should stay on the rendered hot exhaust core"
  );
  failures += expect(
    rocketProjectileScene.simpleBatches.size() == 2U &&
      rocketProjectileScene.simpleBatches[0].mesh == lg::MeshHandle::RocketProjectile &&
      rocketProjectileScene.simpleBatches[0].pass == lg::RenderPass::OpaqueWorld &&
      rocketProjectileScene.simpleBatches[1].billboard == lg::BillboardHandle::RocketFlame &&
      rocketProjectileScene.simpleBatches[1].pass == lg::RenderPass::AdditiveGlow,
    "rocket projectile should use the rocket mesh opaque pass and flame additive pass"
  );
  lg::RenderSettings lowRocketProjectileSettings = settings;
  lowRocketProjectileSettings.combatEffectsQuality = 0;
  const lg::Scene3D lowRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    lowRocketProjectileSettings
  );
  lg::RenderSettings mediumRocketProjectileSettings = settings;
  mediumRocketProjectileSettings.combatEffectsQuality = 1;
  const lg::Scene3D mediumRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    mediumRocketProjectileSettings
  );
  lg::RenderSettings mediumMaterialRocketProjectileSettings = settings;
  mediumMaterialRocketProjectileSettings.materialQuality = 1;
  const lg::Scene3D mediumMaterialRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    mediumMaterialRocketProjectileSettings
  );
  lg::RenderSettings noBloomRocketProjectileSettings = settings;
  noBloomRocketProjectileSettings.bloomEnabled = false;
  const lg::Scene3D noBloomRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    noBloomRocketProjectileSettings
  );
  failures += expect(
    lowRocketProjectileScene.projectileStats.rocketInstances == 1U &&
      lowRocketProjectileScene.projectileStats.projectileGlowInstances == 1U &&
      lowRocketProjectileScene.simpleInstances.size() == 2U &&
      mediumRocketProjectileScene.projectileStats.rocketInstances == 1U &&
      mediumRocketProjectileScene.projectileStats.projectileGlowInstances == 2U &&
      mediumRocketProjectileScene.simpleInstances.size() == 3U &&
      lowRocketProjectileScene.temporaryLights.empty() &&
      mediumRocketProjectileScene.temporaryLights.empty() &&
      mediumMaterialRocketProjectileScene.temporaryLights.empty() &&
      largestBillboardScale(
        lowRocketProjectileScene,
        lg::BillboardHandle::RocketFlame
      ) < largestBillboardScale(
        mediumRocketProjectileScene,
        lg::BillboardHandle::RocketFlame
      ) &&
      largestBillboardScale(
        mediumRocketProjectileScene,
        lg::BillboardHandle::RocketFlame
      ) < largestBillboardScale(
        rocketProjectileScene,
        lg::BillboardHandle::RocketFlame
      ),
    "rocket quality levels should keep the opaque core while reducing exhaust layers and size"
  );
  failures += expect(
    noBloomRocketProjectileScene.projectileStats.rocketInstances ==
        rocketProjectileScene.projectileStats.rocketInstances &&
      noBloomRocketProjectileScene.projectileStats.projectileGlowInstances ==
        rocketProjectileScene.projectileStats.projectileGlowInstances &&
      noBloomRocketProjectileScene.simpleInstances.size() ==
        rocketProjectileScene.simpleInstances.size() &&
      largestBillboardScale(
        noBloomRocketProjectileScene,
        lg::BillboardHandle::RocketFlame
      ) == largestBillboardScale(
        rocketProjectileScene,
        lg::BillboardHandle::RocketFlame
      ),
    "bloom off should preserve the rocket core and exhaust geometry"
  );

  rocketProjectiles[0].velocity = {0.0F, 0.0F, 30.0F};
  const lg::Scene3D upwardRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  const lg::SimpleRenderInstance* upwardRocketCore = findSimpleMesh(
    upwardRocketProjectileScene,
    lg::MeshHandle::RocketProjectile
  );
  const bool upwardExhaustBehind = upwardRocketCore != nullptr &&
    std::all_of(
      upwardRocketProjectileScene.simpleInstances.begin(),
      upwardRocketProjectileScene.simpleInstances.end(),
      [upwardRocketCore](const lg::SimpleRenderInstance& instance) {
        return instance.billboard != lg::BillboardHandle::RocketFlame ||
          instance.position.z < upwardRocketCore->position.z - 0.25F;
      }
    );
  failures += expect(
    upwardRocketProjectileScene.simpleInstances.size() == 3U &&
      upwardRocketCore != nullptr &&
      std::fabs(upwardRocketCore->pitchRadians -
        (3.14159265359F * 0.5F)) < 0.001F &&
      upwardExhaustBehind,
    "upward rocket should pitch its opaque core and keep both exhaust layers behind it"
  );

  rocketProjectiles[0].velocity = {0.0F, 0.0F, -30.0F};
  const lg::Scene3D downwardRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  const lg::SimpleRenderInstance* downwardRocketCore = findSimpleMesh(
    downwardRocketProjectileScene,
    lg::MeshHandle::RocketProjectile
  );
  const bool downwardExhaustBehind = downwardRocketCore != nullptr &&
    std::all_of(
      downwardRocketProjectileScene.simpleInstances.begin(),
      downwardRocketProjectileScene.simpleInstances.end(),
      [downwardRocketCore](const lg::SimpleRenderInstance& instance) {
        return instance.billboard != lg::BillboardHandle::RocketFlame ||
          instance.position.z > downwardRocketCore->position.z + 0.25F;
      }
    );
  failures += expect(
    downwardRocketProjectileScene.simpleInstances.size() == 3U &&
      downwardRocketCore != nullptr &&
      std::fabs(downwardRocketCore->pitchRadians +
        (3.14159265359F * 0.5F)) < 0.001F &&
      downwardExhaustBehind,
    "downward rocket should pitch its opaque core and keep both exhaust layers behind it"
  );

  rocketProjectiles[0].owner = 0;
  rocketProjectiles[0].position = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  rocketProjectiles[0].velocity = {30.0F, 0.0F, 0.0F};
  const lg::Scene3D localRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    localShotgunWeaponStartSettings
  );
  const lg::SimpleRenderInstance* localRocketCore = findSimpleMesh(
    localRocketProjectileScene,
    lg::MeshHandle::RocketProjectile
  );
  failures += expect(
    localRocketProjectileScene.simpleInstances.size() == 3U &&
      localRocketCore != nullptr &&
      localRocketCore->position.z <
        rocketProjectiles[0].position.z - 0.15F,
    "local rocket projectile instances should render from the first-person weapon barrel"
  );
  lg::RenderSettings hiddenLocalProjectileSettings = localShotgunWeaponStartSettings;
  hiddenLocalProjectileSettings.showOwnWeapons = false;
  const lg::Scene3D hiddenLocalRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    hiddenLocalProjectileSettings
  );
  const lg::SimpleRenderInstance* hiddenLocalRocketCore = findSimpleMesh(
    hiddenLocalRocketProjectileScene,
    lg::MeshHandle::RocketProjectile
  );
  failures += expect(
    hiddenLocalRocketProjectileScene.simpleInstances.size() == 3U &&
      hiddenLocalRocketCore != nullptr &&
      localRocketCore != nullptr &&
      hiddenLocalRocketCore->position.z <
        localRocketCore->position.z - 0.15F,
    "r_show_weapons 0 should render local rocket projectiles from the bottom-center hidden weapon origin"
  );

  std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> grenadeProjectiles = {};
  grenadeProjectiles[0].active = true;
  grenadeProjectiles[0].owner = 1;
  grenadeProjectiles[0].weapon = lg::Weapon::GrenadeLauncher;
  grenadeProjectiles[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  grenadeProjectiles[0].velocity = {18.0F, 2.0F, 6.0F};
  const lg::Scene3D grenadeProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    grenadeProjectiles,
    settings
  );
  failures += expect(
    grenadeProjectileScene.projectileStats.projectilesRendered == 1 &&
      grenadeProjectileScene.projectileStats.grenadeInstances == 1 &&
      grenadeProjectileScene.projectileStats.projectileGlowInstances == 0 &&
      grenadeProjectileScene.projectileStats.opaqueProjectileBatches == 1 &&
      grenadeProjectileScene.projectileStats.additiveProjectileBatches == 0 &&
      grenadeProjectileScene.projectileStats.legacyProjectileDynamicVertices == 0 &&
      grenadeProjectileScene.simpleInstances.size() == 1U &&
      grenadeProjectileScene.simpleInstances[0].mesh == lg::MeshHandle::GrenadeProjectile &&
      grenadeProjectileScene.simpleInstances[0].pass == lg::RenderPass::OpaqueWorld &&
      grenadeProjectileScene.temporaryLights.empty(),
    "active grenade projectile should produce one opaque instance with no glow or local light"
  );

  grenadeProjectiles[0].velocity = {};
  const lg::Scene3D stillGrenadeProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    grenadeProjectiles,
    settings
  );
  failures += expect(
    stillGrenadeProjectileScene.simpleInstances.size() == 1U &&
      std::isfinite(stillGrenadeProjectileScene.simpleInstances[0].rotationRadians),
    "zero-velocity grenade projectile rotation should remain finite"
  );

  rocketProjectiles[0].position = player.position + lg::Vec3{-12.0F, 0.0F, 0.65F};
  const lg::Scene3D culledRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  grenadeProjectiles[0].position = player.position + lg::Vec3{-12.0F, 0.0F, 0.65F};
  const lg::Scene3D culledGrenadeProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    grenadeProjectiles,
    settings
  );
  failures += expect(
    culledRocketProjectileScene.projectileStats.projectilesFrustumCulled == 1 &&
      culledRocketProjectileScene.simpleInstances.empty() &&
      culledRocketProjectileScene.temporaryLights.empty() &&
      culledGrenadeProjectileScene.projectileStats.projectilesFrustumCulled == 1 &&
      culledGrenadeProjectileScene.simpleInstances.empty(),
    "frustum culling should exclude off-screen rocket and grenade projectiles"
  );

  rocketProjectiles[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  rocketProjectiles[1] = rocketProjectiles[0];
  rocketProjectiles[1].position = player.position + lg::Vec3{4.0F, 0.2F, 0.65F};
  const lg::Scene3D multiRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  failures += expect(
    multiRocketProjectileScene.projectileStats.rocketInstances == 2 &&
      multiRocketProjectileScene.projectileStats.projectileGlowInstances == 4 &&
      multiRocketProjectileScene.projectileStats.projectileMeshDrawCalls == 1 &&
      multiRocketProjectileScene.projectileStats.projectileGlowDrawCalls == 1 &&
      multiRocketProjectileScene.simpleBatches.size() == 2U &&
      multiRocketProjectileScene.simpleInstances.size() == 6U &&
      multiRocketProjectileScene.temporaryLights.size() == 2U,
    "multiple rockets should batch their visuals and each add a local light"
  );
  std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles>
    cappedLightRocketProjectiles = {};
  for (std::size_t index = 0; index < 5U; ++index) {
    cappedLightRocketProjectiles[index].active = true;
    cappedLightRocketProjectiles[index].owner = 1;
    cappedLightRocketProjectiles[index].weapon = lg::Weapon::RocketLauncher;
    cappedLightRocketProjectiles[index].position =
      player.position + lg::Vec3{
        7.0F - static_cast<float>(index),
        0.0F,
        0.65F,
      };
    cappedLightRocketProjectiles[index].velocity = {30.0F, 0.0F, 0.0F};
  }
  const lg::Scene3D cappedLightRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    cappedLightRocketProjectiles,
    settings
  );
  failures += expect(
    cappedLightRocketProjectileScene.temporaryLights.size() == 4U &&
      cappedLightRocketProjectileScene.transientVfxStats.activeTemporaryLights == 4U &&
      std::all_of(
        cappedLightRocketProjectileScene.temporaryLights.begin(),
        cappedLightRocketProjectileScene.temporaryLights.end(),
        [&player](const lg::TemporaryLight& light) {
          return light.position.x < player.position.x + 6.0F;
        }
      ),
    "rocket local lights should cap at the four nearest rendered projectiles"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> rocketFireOnly = {};
  rocketFireOnly[0].fired = true;
  rocketFireOnly[0].weapon = lg::Weapon::RocketLauncher;
  rocketFireOnly[0].start = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  rocketFireOnly[0].end = rocketFireOnly[0].start + lg::Vec3{1.2F, 0.0F, 0.0F};
  const lg::Scene3D rocketFireOnlyScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount>{},
    inactiveBeam,
    rocketFireOnly,
    rocketExplosions,
    rockets,
    settings
  );
  bool foundRocketFireLine = false;
  for (const lg::Vertex3D& vertex : rocketFireOnlyScene.vertices) {
    foundRocketFireLine =
      foundRocketFireLine ||
      (
        vertex.color.red >= 240 &&
        vertex.color.green >= 120 &&
        vertex.color.green <= 175 &&
        vertex.color.blue <= 90
      );
  }
  failures += expect(
    !foundRocketFireLine,
    "rocket launcher fire events should not draw a separate projectile line"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> plasmaFireOnly = {};
  plasmaFireOnly[0].fired = true;
  plasmaFireOnly[0].weapon = lg::Weapon::PlasmaGun;
  plasmaFireOnly[0].start = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  plasmaFireOnly[0].end = plasmaFireOnly[0].start + lg::Vec3{1.2F, 0.0F, 0.0F};
  const lg::Scene3D plasmaFireOnlyScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount>{},
    inactiveBeam,
    plasmaFireOnly,
    rocketExplosions,
    rockets,
    settings
  );
  bool foundPlasmaFireLine = false;
  for (const lg::Vertex3D& vertex : plasmaFireOnlyScene.vertices) {
    foundPlasmaFireLine =
      foundPlasmaFireLine ||
      (
        vertex.color.green >= 220 &&
        vertex.color.red <= 130 &&
        vertex.color.blue <= 170
      );
  }
  failures += expect(
    !foundPlasmaFireLine,
    "plasma gun fire events should not draw a separate beam line"
  );

  std::array<lg::TransientEffect, 8> explosionEffects = {};
  explosionEffects[0] = {
    lg::TransientEffectType::RocketExplosionFlash,
    player.position + lg::Vec3{3.0F, 0.0F, 0.65F},
    0.01F,
    0.05F,
    0.8F,
    1.6F,
    {255, 228, 132, 230},
    11U,
  };
  explosionEffects[1] = {
    lg::TransientEffectType::RocketExplosionCore,
    explosionEffects[0].position,
    0.04F,
    0.18F,
    0.7F,
    2.6F,
    {255, 112, 44, 200},
    12U,
  };
  explosionEffects[2] = {
    lg::TransientEffectType::RocketExplosionHalo,
    explosionEffects[0].position,
    0.02F,
    0.12F,
    1.5F,
    3.2F,
    {255, 72, 28, 82},
    13U,
  };
  const lg::Scene3D rocketExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 3U),
    settings
  );
  failures += expect(
    rocketExplosionScene.transientVfxStats.activeExplosionEffects == 3 &&
      rocketExplosionScene.transientVfxStats.explosionInstancesSubmitted == 5 &&
      rocketExplosionScene.transientVfxStats.explosionDrawCalls == 3 &&
      rocketExplosionScene.transientVfxStats.explosionOpaqueBatches == 1 &&
      rocketExplosionScene.transientVfxStats.explosionAdditiveBatches == 2 &&
      rocketExplosionScene.transientVfxStats.legacyWireframeExplosionDraws == 0,
    "rocket impact should batch three body facets beside its flash and halo"
  );
  std::uint32_t rocketExplosionCoreCount = 0;
  bool foundRocketExplosionFlash = false;
  bool foundRocketExplosionHalo = false;
  bool foundPaleRocketFlash = false;
  bool foundCoralRocketBody = false;
  float largestRocketBodyScale = 0.0F;
  float rocketFlashScale = 0.0F;
  float rocketHaloScale = 0.0F;
  for (const lg::SimpleRenderInstance& instance : rocketExplosionScene.simpleInstances) {
    if (instance.mesh == lg::MeshHandle::ExplosionCore) {
      ++rocketExplosionCoreCount;
      largestRocketBodyScale = std::max(
        largestRocketBodyScale,
        std::max({instance.scale.x, instance.scale.y, instance.scale.z})
      );
      foundCoralRocketBody =
        foundCoralRocketBody ||
        (
          instance.color.red > instance.color.green &&
          instance.color.green > instance.color.blue &&
          instance.pass == lg::RenderPass::OpaqueWorld &&
          (
            std::fabs(instance.scale.x - instance.scale.y) > 0.01F ||
            std::fabs(instance.scale.y - instance.scale.z) > 0.01F
          )
        );
    }
    foundRocketExplosionFlash =
      foundRocketExplosionFlash || instance.billboard == lg::BillboardHandle::ExplosionFlash;
    if (instance.billboard == lg::BillboardHandle::ExplosionFlash) {
      rocketFlashScale = instance.scale.x;
    }
    foundPaleRocketFlash =
      foundPaleRocketFlash ||
      (
        instance.billboard == lg::BillboardHandle::ExplosionFlash &&
        instance.color.red >= instance.color.green &&
        instance.color.green > instance.color.blue
      );
    foundRocketExplosionHalo =
      foundRocketExplosionHalo || instance.billboard == lg::BillboardHandle::ExplosionHalo;
    if (instance.billboard == lg::BillboardHandle::ExplosionHalo) {
      rocketHaloScale = instance.scale.x;
    }
  }
  failures += expect(
    rocketExplosionCoreCount == 3U &&
      foundRocketExplosionFlash &&
      foundRocketExplosionHalo &&
      foundPaleRocketFlash &&
      foundCoralRocketBody &&
      largestRocketBodyScale > 0.82F &&
      largestRocketBodyScale < 0.86F &&
      rocketFlashScale > 0.51F &&
      rocketFlashScale < 0.53F &&
      rocketHaloScale > 1.27F &&
      rocketHaloScale < 1.30F,
    "rocket impact should keep a compact pale core, bounded coral body, and restrained halo"
  );
  lg::RenderSettings noBloomExplosionSettings = settings;
  noBloomExplosionSettings.bloomEnabled = false;
  const lg::Scene3D noBloomRocketExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 3U),
    noBloomExplosionSettings
  );
  failures += expect(
    noBloomRocketExplosionScene.simpleInstances.size() ==
        rocketExplosionScene.simpleInstances.size() &&
      noBloomRocketExplosionScene.transientVfxStats.explosionOpaqueBatches ==
        rocketExplosionScene.transientVfxStats.explosionOpaqueBatches &&
      noBloomRocketExplosionScene.transientVfxStats.explosionInstancesSubmitted ==
        rocketExplosionScene.transientVfxStats.explosionInstancesSubmitted,
    "bloom off should keep the full opaque rocket body and its layered cue geometry"
  );
  explosionEffects[3] = {
    lg::TransientEffectType::RocketExplosionShard,
    explosionEffects[0].position + lg::Vec3{0.05F, 0.0F, 0.0F},
    0.02F,
    0.16F,
    0.020F,
    0.005F,
    {248, 126, 72, 190},
    14U,
  };
  explosionEffects[3].velocity = {2.0F, 0.2F, 0.6F};
  explosionEffects[4] = {
    lg::TransientEffectType::RocketExplosionSmoke,
    explosionEffects[0].position,
    0.02F,
    0.27F,
    0.18F,
    0.48F,
    {100, 104, 106, 58},
    15U,
  };
  explosionEffects[4].velocity = {0.1F, 0.0F, 0.4F};
  const lg::Scene3D rocketExplosionSecondaryScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 5U),
    settings
  );
  failures += expect(
    rocketExplosionSecondaryScene.transientVfxStats.activeExplosionEffects == 3U &&
      rocketExplosionSecondaryScene.transientVfxStats.activeImpactParticles == 2U &&
      rocketExplosionSecondaryScene.transientVfxStats.explosionInstancesSubmitted == 5U &&
      rocketExplosionSecondaryScene.transientVfxStats.transparentEffectsSubmitted == 4U &&
      rocketExplosionSecondaryScene.simpleInstances.size() == 5U &&
      !rocketExplosionSecondaryScene.translucentVertices.empty(),
    "rocket shards and smoke should stay bounded beside the layered blast"
  );

  lg::RenderSettings lowExplosionSettings = settings;
  lowExplosionSettings.combatEffectsQuality = 0;
  const lg::Scene3D lowExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 5U),
    lowExplosionSettings
  );
  lg::RenderSettings mediumExplosionSettings = settings;
  mediumExplosionSettings.combatEffectsQuality = 1;
  const lg::Scene3D mediumExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 5U),
    mediumExplosionSettings
  );
  failures += expect(
    lowExplosionScene.transientVfxStats.activeExplosionEffects == 2U &&
      lowExplosionScene.transientVfxStats.activeImpactParticles == 0U &&
      lowExplosionScene.transientVfxStats.explosionInstancesSubmitted == 4U &&
      lowExplosionScene.simpleInstances.size() == 4U &&
      lowExplosionScene.translucentVertices.empty() &&
      mediumExplosionScene.transientVfxStats.activeExplosionEffects == 3U &&
      mediumExplosionScene.transientVfxStats.activeImpactParticles == 0U &&
      mediumExplosionScene.transientVfxStats.explosionInstancesSubmitted == 5U &&
      mediumExplosionScene.simpleInstances.size() == 5U &&
      mediumExplosionScene.translucentVertices.empty(),
    "reduced rocket quality should keep flash and body while dropping secondary parts"
  );

  explosionEffects[0].type = lg::TransientEffectType::PlasmaExplosionFlash;
  explosionEffects[0].color = {122, 255, 184, 210};
  explosionEffects[0].finalScale = 0.55F;
  explosionEffects[1].type = lg::TransientEffectType::PlasmaExplosionCore;
  explosionEffects[1].color = {76, 248, 210, 185};
  explosionEffects[1].finalScale = 0.75F;
  explosionEffects[2].type = lg::TransientEffectType::PlasmaExplosionHalo;
  explosionEffects[2].color = {64, 255, 168, 88};
  explosionEffects[2].finalScale = 0.95F;
  const lg::Scene3D plasmaExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 3U),
    settings
  );
  failures += expect(
    plasmaExplosionScene.transientVfxStats.explosionInstancesSubmitted == 3 &&
      plasmaExplosionScene.simpleInstances.size() == 3U,
    "plasma explosion effects should submit a distinct compact three-instance burst"
  );

  explosionEffects[0] = {
    lg::TransientEffectType::GrenadeExplosionFlash,
    player.position + lg::Vec3{3.0F, 0.2F, 0.65F},
    0.01F,
    0.05F,
    0.7F,
    1.4F,
    {255, 224, 104, 220},
    21U,
  };
  explosionEffects[1] = {
    lg::TransientEffectType::GrenadeExplosionCore,
    explosionEffects[0].position,
    0.04F,
    0.20F,
    0.8F,
    2.7F,
    {255, 178, 66, 190},
    22U,
  };
  const lg::Scene3D grenadeExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 2U),
    settings
  );
  failures += expect(
    grenadeExplosionScene.transientVfxStats.explosionInstancesSubmitted == 2 &&
      grenadeExplosionScene.transientVfxStats.explosionDrawCalls == 2,
    "grenade detonation should use the bounded flash plus amber core burst"
  );

  explosionEffects[0] = {
    lg::TransientEffectType::RocketExplosionCore,
    player.position + lg::Vec3{3.0F, 0.0F, 0.65F},
    0.02F,
    0.20F,
    std::numeric_limits<float>::infinity(),
    10000.0F,
    {255, 112, 44, 200},
    31U,
  };
  const lg::Scene3D clampedExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 1U),
    settings
  );
  failures += expect(
    clampedExplosionScene.simpleInstances.size() == 3U &&
      std::isfinite(clampedExplosionScene.simpleInstances[0].scale.x) &&
      clampedExplosionScene.simpleInstances[0].scale.x <= 8.8F,
    "explosion effect scale should remain finite and clamped for unusual input"
  );

  explosionEffects[0].ageSeconds = explosionEffects[0].lifetimeSeconds;
  const lg::Scene3D expiredExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 1U),
    settings
  );
  failures += expect(
    expiredExplosionScene.transientVfxStats.explosionInstancesSubmitted == 0,
    "expired explosion effects should not submit render instances"
  );

  for (std::size_t index = 0; index < 6U; ++index) {
    explosionEffects[index] = {
      index % 3U == 0
        ? lg::TransientEffectType::RocketExplosionFlash
        : index % 3U == 1
          ? lg::TransientEffectType::RocketExplosionCore
          : lg::TransientEffectType::RocketExplosionHalo,
      player.position + lg::Vec3{3.0F + static_cast<float>(index) * 0.1F, 0.0F, 0.65F},
      0.01F,
      0.16F,
      0.6F,
      1.8F,
      {255, 180, 80, 180},
      static_cast<std::uint32_t>(index),
    };
  }
  const lg::Scene3D multiExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 6U),
    settings
  );
  failures += expect(
    multiExplosionScene.transientVfxStats.explosionInstancesSubmitted == 10 &&
      multiExplosionScene.transientVfxStats.explosionDrawCalls == 3,
    "multiple overlapping explosions should batch into bounded reusable VFX draws"
  );

  std::array<lg::RocketExplosionResult, lg::kDuelPlayerCount> retainedExplosions = {};
  retainedExplosions[0].active = true;
  retainedExplosions[0].weapon = lg::Weapon::RocketLauncher;
  retainedExplosions[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  retainedExplosions[0].radius = 3.0F;
  retainedExplosions[0].sequence = 99U;
  const lg::Scene3D retainedExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    retainedExplosions,
    rockets,
    settings
  );
  failures += expect(
    retainedExplosionScene.transientVfxStats.legacyWireframeExplosionDraws == 0 &&
      retainedExplosionScene.simpleInstances.empty(),
    "retained authoritative explosions should not draw legacy wireframe boxes directly"
  );

  return failures == 0 ? 0 : 1;
}
