#include "render/Scene3D.hpp"

#include "benchmark/BenchmarkTiming.hpp"
#include "render/BakedWeaponModels.hpp"
#include "render/BakedMachineGunModel.hpp"
#include "render/BakedRevolverModel.hpp"
#include "render/BakedRocketLauncherModel.hpp"
#include "render/BakedFreezeGunModel.hpp"
#include "render/BakedPlasmaGunModel.hpp"
#include "render/GltfSkinnedModel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>

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
constexpr float kStaticLightAmbient = 0.18F;
constexpr float kSunWrapMinimum = 0.15F;
constexpr float kStaticLightMax = 2.0F;
constexpr float kLegacyOutlineWorldUnitsPerPixel = 0.015F;
constexpr std::uint32_t kSimpleInstanceUploadBytes = 40U;
constexpr std::uint32_t kStaticMeshInstanceUploadBytes = 52U;
constexpr std::uint32_t kStaticMeshVertexUploadBytes = 24U;
constexpr std::uint32_t kGltfPlayerModelVertexGpuBytes = 64U;
constexpr std::uint32_t kGltfPlayerModelIndexGpuBytes = 4U;
constexpr std::uint32_t kGltfBonePaletteEntryBytes = 64U;
constexpr Vec3 kRevolverGripSocket = {-0.23F, 0.0F, -0.24F};
constexpr float kRocketLauncherViewModelScale = 0.66F;
constexpr float kRocketLauncherViewModelForwardOffset = -0.16F;
constexpr float kRocketLauncherViewModelUpOffset = -0.15F;

// Centered unit cube, local coordinates [-0.5, 0.5] on every axis. Player
// cuboids use per-instance basis columns scaled to the desired full extents.
constexpr std::array<Vertex3D, 36> kPlayerBoxCubeMeshVertices = {{
  {{-0.5F, -0.5F, -0.5F}, {140, 140, 140, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F,  0.5F, -0.5F}, {140, 140, 140, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F, -0.5F}, {140, 140, 140, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F, -0.5F}, {140, 140, 140, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F, -0.5F}, {140, 140, 140, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F, -0.5F, -0.5F}, {140, 140, 140, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F,  0.5F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F, -0.5F,  0.5F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F,  0.5F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F,  0.5F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F,  0.5F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F,  0.5F,  0.5F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F, -0.5F}, {183, 183, 183, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F, -0.5F, -0.5F}, {183, 183, 183, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F, -0.5F,  0.5F}, {183, 183, 183, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F, -0.5F}, {183, 183, 183, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F, -0.5F,  0.5F}, {183, 183, 183, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F,  0.5F}, {183, 183, 183, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F, -0.5F, -0.5F}, {219, 219, 219, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F, -0.5F}, {219, 219, 219, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F,  0.5F}, {219, 219, 219, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F, -0.5F, -0.5F}, {219, 219, 219, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F,  0.5F}, {219, 219, 219, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F, -0.5F,  0.5F}, {219, 219, 219, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F, -0.5F}, {168, 168, 168, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F,  0.5F, -0.5F}, {168, 168, 168, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F,  0.5F,  0.5F}, {168, 168, 168, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F, -0.5F}, {168, 168, 168, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F,  0.5F,  0.5F}, {168, 168, 168, 255}, 0.0F, 0.0F, 0U},
  {{ 0.5F,  0.5F,  0.5F}, {168, 168, 168, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F,  0.5F, -0.5F}, {198, 198, 198, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F, -0.5F}, {198, 198, 198, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F,  0.5F}, {198, 198, 198, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F,  0.5F, -0.5F}, {198, 198, 198, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F, -0.5F,  0.5F}, {198, 198, 198, 255}, 0.0F, 0.0F, 0U},
  {{-0.5F,  0.5F,  0.5F}, {198, 198, 198, 255}, 0.0F, 0.0F, 0U},
}};

constexpr StaticMeshAsset kPlayerBoxCubeAsset = {
  MeshHandle::PlayerBoxCube,
  std::span<const Vertex3D>(
    kPlayerBoxCubeMeshVertices.data(),
    kPlayerBoxCubeMeshVertices.size()
  ),
  {{}, 0.8660254F},
  RenderPass::OpaqueWorld,
};

constexpr std::array<Vertex3D, 24> kPlasmaCoreMeshVertices = {{
  {{0.0F, 0.0F, 1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{1.0F, 0.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 0.0F, 1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{-1.0F, 0.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 0.0F, 1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{-1.0F, 0.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, -1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 0.0F, 1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, -1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{1.0F, 0.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 0.0F, -1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{1.0F, 0.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 0.0F, -1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{-1.0F, 0.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 0.0F, -1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, -1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{-1.0F, 0.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, 0.0F, -1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{1.0F, 0.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{0.0F, -1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
}};

constexpr StaticMeshAsset kPlasmaCoreAsset = {
  MeshHandle::PlasmaCore,
  std::span<const Vertex3D>(kPlasmaCoreMeshVertices.data(), kPlasmaCoreMeshVertices.size()),
  {{}, 1.0F},
  RenderPass::OpaqueWorld,
};

constexpr StaticMeshAsset kExplosionCoreAsset = {
  MeshHandle::ExplosionCore,
  std::span<const Vertex3D>(kPlasmaCoreMeshVertices.data(), kPlasmaCoreMeshVertices.size()),
  {{}, 1.0F},
  RenderPass::OpaqueWorld,
};

constexpr std::array<Vertex3D, 36> kRocketProjectileMeshVertices = {{
  {{-0.85F, -0.18F, -0.18F}, {170, 176, 170, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F, -0.18F}, {170, 176, 170, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F, -0.18F}, {170, 176, 170, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F, -0.18F, -0.18F}, {170, 176, 170, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F, -0.18F}, {170, 176, 170, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F,  0.18F, -0.18F}, {170, 176, 170, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F, -0.18F,  0.18F}, {220, 224, 210, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F,  0.18F}, {220, 224, 210, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F,  0.18F}, {220, 224, 210, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F, -0.18F,  0.18F}, {220, 224, 210, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F,  0.18F,  0.18F}, {220, 224, 210, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F,  0.18F}, {220, 224, 210, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F, -0.18F, -0.18F}, {148, 152, 148, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F,  0.18F}, {148, 152, 148, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F, -0.18F}, {148, 152, 148, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F, -0.18F, -0.18F}, {148, 152, 148, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F, -0.18F,  0.18F}, {148, 152, 148, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F,  0.18F}, {148, 152, 148, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F,  0.18F, -0.18F}, {196, 202, 190, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F, -0.18F}, {196, 202, 190, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F,  0.18F}, {196, 202, 190, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F,  0.18F, -0.18F}, {196, 202, 190, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F,  0.18F}, {196, 202, 190, 255}, 0.0F, 0.0F, 0U},
  {{-0.85F,  0.18F,  0.18F}, {196, 202, 190, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F, -0.18F}, {230, 214, 150, 255}, 0.0F, 0.0F, 0U},
  {{ 0.82F,  0.0F,  0.0F}, {230, 214, 150, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F, -0.18F}, {230, 214, 150, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F, -0.18F}, {244, 228, 162, 255}, 0.0F, 0.0F, 0U},
  {{ 0.82F,  0.0F,  0.0F}, {244, 228, 162, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F,  0.18F}, {244, 228, 162, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F,  0.18F,  0.18F}, {230, 214, 150, 255}, 0.0F, 0.0F, 0U},
  {{ 0.82F,  0.0F,  0.0F}, {230, 214, 150, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F,  0.18F}, {230, 214, 150, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F,  0.18F}, {244, 228, 162, 255}, 0.0F, 0.0F, 0U},
  {{ 0.82F,  0.0F,  0.0F}, {244, 228, 162, 255}, 0.0F, 0.0F, 0U},
  {{ 0.45F, -0.18F, -0.18F}, {244, 228, 162, 255}, 0.0F, 0.0F, 0U},
}};

constexpr std::array<Vertex3D, 24> kGrenadeProjectileMeshVertices = {{
  {{ 0.0F,  0.0F,  0.72F}, {58, 86, 46, 255}, 0.0F, 0.0F, 0U},
  {{ 0.58F,  0.0F,  0.0F}, {58, 86, 46, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.58F,  0.0F}, {58, 86, 46, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.0F,  0.72F}, {48, 72, 38, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.58F,  0.0F}, {48, 72, 38, 255}, 0.0F, 0.0F, 0U},
  {{-0.58F,  0.0F,  0.0F}, {48, 72, 38, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.0F,  0.72F}, {42, 64, 34, 255}, 0.0F, 0.0F, 0U},
  {{-0.58F,  0.0F,  0.0F}, {42, 64, 34, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F, -0.58F,  0.0F}, {42, 64, 34, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.0F,  0.72F}, {66, 98, 52, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F, -0.58F,  0.0F}, {66, 98, 52, 255}, 0.0F, 0.0F, 0U},
  {{ 0.58F,  0.0F,  0.0F}, {66, 98, 52, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.0F, -0.72F}, {34, 54, 30, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.58F,  0.0F}, {34, 54, 30, 255}, 0.0F, 0.0F, 0U},
  {{ 0.58F,  0.0F,  0.0F}, {34, 54, 30, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.0F, -0.72F}, {38, 60, 32, 255}, 0.0F, 0.0F, 0U},
  {{-0.58F,  0.0F,  0.0F}, {38, 60, 32, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.58F,  0.0F}, {38, 60, 32, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.0F, -0.72F}, {30, 48, 26, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F, -0.58F,  0.0F}, {30, 48, 26, 255}, 0.0F, 0.0F, 0U},
  {{-0.58F,  0.0F,  0.0F}, {30, 48, 26, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F,  0.0F, -0.72F}, {46, 70, 36, 255}, 0.0F, 0.0F, 0U},
  {{ 0.58F,  0.0F,  0.0F}, {46, 70, 36, 255}, 0.0F, 0.0F, 0U},
  {{ 0.0F, -0.58F,  0.0F}, {46, 70, 36, 255}, 0.0F, 0.0F, 0U},
}};

constexpr StaticMeshAsset kRocketProjectileAsset = {
  MeshHandle::RocketProjectile,
  std::span<const Vertex3D>(
    kRocketProjectileMeshVertices.data(),
    kRocketProjectileMeshVertices.size()
  ),
  {{}, 0.92F},
  RenderPass::OpaqueWorld,
};

constexpr StaticMeshAsset kGrenadeProjectileAsset = {
  MeshHandle::GrenadeProjectile,
  std::span<const Vertex3D>(
    kGrenadeProjectileMeshVertices.data(),
    kGrenadeProjectileMeshVertices.size()
  ),
  {{}, 0.72F},
  RenderPass::OpaqueWorld,
};

constexpr std::array<Vertex3D, 12> kTracerBeamMeshVertices = {{
  {{0.0F, -1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{1.0F, -1.0F, 0.0F}, {255, 255, 255, 255}, 1.0F, 0.0F, 0U},
  {{1.0F,  1.0F, 0.0F}, {255, 255, 255, 255}, 1.0F, 1.0F, 0U},
  {{0.0F, -1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{1.0F,  1.0F, 0.0F}, {255, 255, 255, 255}, 1.0F, 1.0F, 0U},
  {{0.0F,  1.0F, 0.0F}, {255, 255, 255, 255}, 0.0F, 1.0F, 0U},
  {{0.0F, 0.0F, -1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{1.0F, 0.0F, -1.0F}, {255, 255, 255, 255}, 1.0F, 0.0F, 0U},
  {{1.0F, 0.0F,  1.0F}, {255, 255, 255, 255}, 1.0F, 1.0F, 0U},
  {{0.0F, 0.0F, -1.0F}, {255, 255, 255, 255}, 0.0F, 0.0F, 0U},
  {{1.0F, 0.0F,  1.0F}, {255, 255, 255, 255}, 1.0F, 1.0F, 0U},
  {{0.0F, 0.0F,  1.0F}, {255, 255, 255, 255}, 0.0F, 1.0F, 0U},
}};

constexpr StaticMeshAsset kMachineGunTracerAsset = {
  MeshHandle::MachineGunTracer,
  std::span<const Vertex3D>(kTracerBeamMeshVertices.data(), kTracerBeamMeshVertices.size()),
  {{0.5F, 0.0F, 0.0F}, 1.12F},
  RenderPass::TranslucentWorld,
};

constexpr StaticMeshAsset kShotgunTracerAsset = {
  MeshHandle::ShotgunTracer,
  std::span<const Vertex3D>(kTracerBeamMeshVertices.data(), kTracerBeamMeshVertices.size()),
  {{0.5F, 0.0F, 0.0F}, 1.12F},
  RenderPass::TranslucentWorld,
};

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs);
[[nodiscard]] RenderColor scaleColor(RenderColor color, float amount);

template <std::size_t Count>
[[nodiscard]] std::vector<Vertex3D> bakedWeaponVertices(
  const std::array<BakedWeaponModelTriangle, Count>& model,
  float modelScale,
  float rollRadians = 0.0F
) {
  std::vector<Vertex3D> vertices;
  vertices.reserve(model.size() * 3U);
  const float rollCos = std::cos(rollRadians);
  const float rollSin = std::sin(rollRadians);
  const Vec3 lightDirection = normalize(Vec3{-0.35F, -0.45F, 0.82F});
  const auto point = [&](Vec3 local) {
    local = {
      local.x,
      local.y * rollCos - local.z * rollSin,
      local.y * rollSin + local.z * rollCos,
    };
    return local * modelScale;
  };
  for (const BakedWeaponModelTriangle& triangle : model) {
    const Vec3 first = point(triangle.vertices[0]);
    const Vec3 second = point(triangle.vertices[1]);
    const Vec3 third = point(triangle.vertices[2]);
    const Vec3 normal = normalize(cross(second - first, third - first));
    const float brightness = std::clamp(
      0.70F +
        std::max(0.0F, dot(normal, lightDirection)) * 0.42F +
        std::fabs(normal.z) * 0.16F,
      0.58F,
      1.30F
    );
    RenderColor color = scaleColor(triangle.color, brightness);
    color.alpha = 255;
    vertices.push_back({first, color, 0.0F, 0.0F, 0U});
    vertices.push_back({second, color, 0.0F, 0.0F, 0U});
    vertices.push_back({third, color, 0.0F, 0.0F, 0U});
  }
  return vertices;
}

template <std::size_t Count>
[[nodiscard]] std::vector<WeaponMaterialVertex3D> bakedMaterialWeaponVertices(
  const std::array<BakedWeaponMaterialTriangle, Count>& model,
  float modelScale = 1.0F
) {
  std::vector<WeaponMaterialVertex3D> vertices;
  vertices.reserve(model.size() * 3U);
  for (const BakedWeaponMaterialTriangle& triangle : model) {
    for (const BakedWeaponMaterialVertex& vertex : triangle.vertices) {
      vertices.push_back({
        vertex.position * modelScale,
        normalize(vertex.normal),
        triangle.baseColor,
        triangle.metallic,
        triangle.roughness,
      });
    }
  }
  return vertices;
}

[[nodiscard]] BoundingSphere meshBounds(std::span<const Vertex3D> vertices) {
  if (vertices.empty()) {
    return {};
  }
  Vec3 minimum = vertices.front().position;
  Vec3 maximum = vertices.front().position;
  for (const Vertex3D& vertex : vertices) {
    minimum.x = std::min(minimum.x, vertex.position.x);
    minimum.y = std::min(minimum.y, vertex.position.y);
    minimum.z = std::min(minimum.z, vertex.position.z);
    maximum.x = std::max(maximum.x, vertex.position.x);
    maximum.y = std::max(maximum.y, vertex.position.y);
    maximum.z = std::max(maximum.z, vertex.position.z);
  }
  const Vec3 center = (minimum + maximum) * 0.5F;
  float radius = 0.0F;
  for (const Vertex3D& vertex : vertices) {
    radius = std::max(radius, length(vertex.position - center));
  }
  return {center, radius};
}

[[nodiscard]] BoundingSphere materialMeshBounds(
  std::span<const WeaponMaterialVertex3D> vertices
) {
  if (vertices.empty()) {
    return {};
  }
  Vec3 center = {};
  for (const WeaponMaterialVertex3D& vertex : vertices) {
    center += vertex.position;
  }
  center *= 1.0F / static_cast<float>(vertices.size());
  float radius = 0.0F;
  for (const WeaponMaterialVertex3D& vertex : vertices) {
    radius = std::max(radius, length(vertex.position - center));
  }
  return {center, radius};
}

constexpr BillboardAsset kPlasmaGlowAsset = {
  BillboardHandle::PlasmaGlow,
  {{}, 1.0F},
  RenderPass::AdditiveGlow,
};

constexpr BillboardAsset kRocketFlameAsset = {
  BillboardHandle::RocketFlame,
  {{}, 1.0F},
  RenderPass::AdditiveGlow,
};

constexpr BillboardAsset kExplosionFlashAsset = {
  BillboardHandle::ExplosionFlash,
  {{}, 1.0F},
  RenderPass::AdditiveGlow,
};

constexpr BillboardAsset kExplosionHaloAsset = {
  BillboardHandle::ExplosionHalo,
  {{}, 1.0F},
  RenderPass::AdditiveGlow,
};

constexpr ProjectileVisualDescriptor kPlasmaProjectileVisual = {
  ProjectileVisualType::Plasma,
  MeshHandle::PlasmaCore,
  BillboardHandle::PlasmaGlow,
  {132, 255, 154, 255},
  {96, 255, 132, 150},
  0.125F,
  0.37F,
  true,
};

constexpr ProjectileVisualDescriptor kRocketProjectileVisual = {
  ProjectileVisualType::Rocket,
  MeshHandle::RocketProjectile,
  BillboardHandle::RocketFlame,
  {255, 255, 255, 255},
  {255, 126, 48, 132},
  0.40F,
  0.50F,
  true,
};

constexpr ProjectileVisualDescriptor kGrenadeProjectileVisual = {
  ProjectileVisualType::Grenade,
  MeshHandle::GrenadeProjectile,
  BillboardHandle::Invalid,
  {255, 255, 255, 255},
  {},
  0.30F,
  0.0F,
  false,
};

[[nodiscard]] bool textureDebugEnabled() {
  const char* value = std::getenv("LG_DUEL_TEXTURE_DEBUG");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

[[nodiscard]] bool lightDebugEnabled() {
  const char* value = std::getenv("LG_DUEL_LIGHT_DEBUG");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void logTextureFace(
  std::string_view kind,
  std::uint32_t materialId,
  const TextureProjection& projection,
  const std::array<std::array<float, 2>, 4>& uv,
  int fallbackAxis
) {
  static int loggedFaces = 0;
  static int loggedProjectedFaces = 0;
  if (!textureDebugEnabled()) {
    return;
  }
  if (projection.valid) {
    if (loggedProjectedFaces >= 10) {
      return;
    }
    ++loggedProjectedFaces;
  } else if (loggedFaces >= 10) {
    return;
  }
  ++loggedFaces;
  std::cerr
    << "LG_DUEL_TEXTURE_PIPELINE_V2 scene face#" << loggedFaces
    << " kind=" << kind
    << " materialId=" << materialId
    << " hasTextureProjection=" << (projection.valid ? "true" : "false")
    << " fallbackAxis=" << fallbackAxis
    << " uAxis=" << projection.uAxis.x << ',' << projection.uAxis.y << ',' << projection.uAxis.z
    << " vAxis=" << projection.vAxis.x << ',' << projection.vAxis.y << ',' << projection.vAxis.z
    << " offset=" << projection.uOffset << ',' << projection.vOffset
    << " rotation=" << projection.rotationDegrees
    << " scale=" << projection.uScale << ',' << projection.vScale
    << " uv=(" << uv[0][0] << ',' << uv[0][1] << ")"
    << " (" << uv[1][0] << ',' << uv[1][1] << ")"
    << " (" << uv[2][0] << ',' << uv[2][1] << ")"
    << " (" << uv[3][0] << ',' << uv[3][1] << ")\n";
}

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
  bool crouched = false;
  bool sneaking = false;
};

struct WeaponModelFrame {
  PlayerModelBasis basis = {};
  Vec3 hand = {};
  float scale = 1.0F;
};

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs);
[[nodiscard]] RenderColor scaleColor(RenderColor color, float amount);

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
  std::array<RenderColor, 3> colors,
  std::uint32_t materialId
) {
  scene.vertices.push_back({first, colors[0], uv[0][0], uv[0][1], materialId});
  scene.vertices.push_back({second, colors[1], uv[1][0], uv[1][1], materialId});
  scene.vertices.push_back({third, colors[2], uv[2][0], uv[2][1], materialId});
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

void addIcePoolDisk(Scene3D& scene, const IcePool& pool) {
  if (!pool.active || pool.radius <= 0.0F || pool.lifetimeSeconds <= 0.0F) {
    return;
  }

  Vec3 tangentA = cross(pool.normal, {0.0F, 0.0F, 1.0F});
  if (length(tangentA) <= 0.0001F) {
    tangentA = {1.0F, 0.0F, 0.0F};
  } else {
    tangentA = normalize(tangentA);
  }
  const Vec3 tangentB = normalize(cross(pool.normal, tangentA));
  const Vec3 center = pool.center + pool.normal * 0.012F;
  const RenderColor color = {154, 232, 255, 88};
  constexpr int kSegments = 24;
  for (int index = 0; index < kSegments; ++index) {
    const float firstAngle =
      (static_cast<float>(index) / static_cast<float>(kSegments)) * kTwoPi;
    const float secondAngle =
      (static_cast<float>(index + 1) / static_cast<float>(kSegments)) * kTwoPi;
    const Vec3 first =
      center +
      tangentA * (std::cos(firstAngle) * pool.radius) +
      tangentB * (std::sin(firstAngle) * pool.radius);
    const Vec3 second =
      center +
      tangentA * (std::cos(secondAngle) * pool.radius) +
      tangentB * (std::sin(secondAngle) * pool.radius);
    addTriangle(scene, center, first, second, color);
  }
}

void addTexturedQuad(
  Scene3D& scene,
  Vec3 first,
  Vec3 second,
  Vec3 third,
  Vec3 fourth,
  std::array<std::array<float, 2>, 4> uv,
  std::array<RenderColor, 4> colors,
  std::uint32_t materialId
) {
  addTexturedTriangle(
    scene,
    first,
    second,
    third,
    {{{uv[0][0], uv[0][1]}, {uv[1][0], uv[1][1]}, {uv[2][0], uv[2][1]}}},
    {{colors[0], colors[1], colors[2]}},
    materialId
  );
  addTexturedTriangle(
    scene,
    first,
    third,
    fourth,
    {{{uv[0][0], uv[0][1]}, {uv[2][0], uv[2][1]}, {uv[3][0], uv[3][1]}}},
    {{colors[0], colors[2], colors[3]}},
    materialId
  );
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

[[nodiscard]] std::array<float, 2> fallbackFaceUv(Vec3 point, int axis) {
  constexpr float kQuakeUnitsPerLgUnit = 40.0F;
  const Vec3 quakePoint = point * kQuakeUnitsPerLgUnit;
  switch (axis) {
  case 0:
    return {quakePoint.y, quakePoint.z};
  case 1:
    return {quakePoint.x, quakePoint.z};
  default:
    return {quakePoint.x, quakePoint.y};
  }
}

[[nodiscard]] std::array<float, 2> projectedFaceUv(
  Vec3 point,
  const TextureProjection& projection,
  int fallbackAxis
) {
  if (!projection.valid) {
    return fallbackFaceUv(point, fallbackAxis);
  }
  constexpr float kQuakeUnitsPerLgUnit = 40.0F;
  const Vec3 quakePoint = point * kQuakeUnitsPerLgUnit;
  return {
    dot(quakePoint, projection.uAxis) + projection.uOffset,
    dot(quakePoint, projection.vAxis) + projection.vOffset,
  };
}

[[nodiscard]] Vec3 wallFaceNormal(std::size_t faceIndex) {
  switch (faceIndex) {
  case 0:
    return {0.0F, 0.0F, -1.0F};
  case 1:
    return {0.0F, 0.0F, 1.0F};
  case 2:
    return {0.0F, -1.0F, 0.0F};
  case 3:
    return {1.0F, 0.0F, 0.0F};
  case 4:
    return {0.0F, 1.0F, 0.0F};
  default:
    return {-1.0F, 0.0F, 0.0F};
  }
}

[[nodiscard]] RenderColor shadeStaticVertex(
  const Arena& arena,
  Vec3 position,
  Vec3 normal,
  RenderColor base
) {
  if (arena.staticLightCount == 0 && !arena.sunLight.enabled) {
    return base;
  }

  Vec3 lightColor = {
    static_cast<float>(base.red) * kStaticLightAmbient,
    static_cast<float>(base.green) * kStaticLightAmbient,
    static_cast<float>(base.blue) * kStaticLightAmbient,
  };
  for (std::size_t index = 0; index < arena.staticLightCount; ++index) {
    const ArenaStaticLight& light = arena.staticLights[index];
    const Vec3 toLight = light.position - position;
    const float distance = length(toLight);
    if (distance <= 0.0001F || distance >= light.radius) {
      continue;
    }
    const Vec3 direction = toLight / distance;
    const float facing = std::max(0.0F, dot(normal, direction));
    const float distanceFactor = 1.0F - std::clamp(distance / light.radius, 0.0F, 1.0F);
    const float contribution = light.intensity * facing * distanceFactor * distanceFactor;
    lightColor.x += static_cast<float>(base.red) * light.color.x * contribution;
    lightColor.y += static_cast<float>(base.green) * light.color.y * contribution;
    lightColor.z += static_cast<float>(base.blue) * light.color.z * contribution;
  }
  if (arena.sunLight.enabled) {
    const ArenaSunLight& sun = arena.sunLight;
    // Sun direction is the direction rays travel; negate it for the incoming
    // light vector from the surface toward the sun.
    const float sunFactor =
      std::max(dot(normal, sun.direction * -1.0F), kSunWrapMinimum);
    const float contribution = sun.intensity * sunFactor;
    lightColor.x += static_cast<float>(base.red) * sun.color.x * contribution;
    lightColor.y += static_cast<float>(base.green) * sun.color.y * contribution;
    lightColor.z += static_cast<float>(base.blue) * sun.color.z * contribution;
  }
  const float maxChannel = 255.0F * kStaticLightMax;
  return {
    static_cast<std::uint8_t>(std::min(std::clamp(lightColor.x, 0.0F, maxChannel), 255.0F)),
    static_cast<std::uint8_t>(std::min(std::clamp(lightColor.y, 0.0F, maxChannel), 255.0F)),
    static_cast<std::uint8_t>(std::min(std::clamp(lightColor.z, 0.0F, maxChannel), 255.0F)),
    base.alpha,
  };
}

void addWallBox(Scene3D& scene, const Arena& arena, const ArenaWall& wall) {
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
    const std::array<std::array<float, 2>, 4> uv = {{
      projectedFaceUv(corners[face[0]], wall.faceTextureProjections[index], faceAxis),
      projectedFaceUv(corners[face[1]], wall.faceTextureProjections[index], faceAxis),
      projectedFaceUv(corners[face[2]], wall.faceTextureProjections[index], faceAxis),
      projectedFaceUv(corners[face[3]], wall.faceTextureProjections[index], faceAxis),
    }};
    const RenderColor baseColor = scaleColor({255, 255, 255, 255}, brightness[index]);
    const Vec3 normal = wallFaceNormal(index);
    const std::array<RenderColor, 4> colors = {{
      shadeStaticVertex(arena, corners[face[0]], normal, baseColor),
      shadeStaticVertex(arena, corners[face[1]], normal, baseColor),
      shadeStaticVertex(arena, corners[face[2]], normal, baseColor),
      shadeStaticVertex(arena, corners[face[3]], normal, baseColor),
    }};
    logTextureFace("ArenaWall", materialId, wall.faceTextureProjections[index], uv, faceAxis);
    addTexturedQuad(
      scene,
      corners[face[0]],
      corners[face[1]],
      corners[face[2]],
      corners[face[3]],
      uv,
      colors,
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

void addArenaBrush(Scene3D& scene, const Arena& arena, const ArenaBrush& brush) {
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
      const std::array<std::array<float, 2>, 4> uv = {{
        projectedFaceUv(origin, face.textureProjection, uvAxis),
        projectedFaceUv(second, face.textureProjection, uvAxis),
        projectedFaceUv(third, face.textureProjection, uvAxis),
        projectedFaceUv(third, face.textureProjection, uvAxis),
      }};
      const std::array<RenderColor, 3> colors = {{
        shadeStaticVertex(arena, origin, face.normal, color),
        shadeStaticVertex(arena, second, face.normal, color),
        shadeStaticVertex(arena, third, face.normal, color),
      }};
      logTextureFace("ArenaBrush", materialId, face.textureProjection, uv, uvAxis);
      addTexturedTriangle(
        scene,
        origin,
        second,
        third,
        {{uv[0], uv[1], uv[2]}},
        colors,
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

void addHealthPickup(Scene3D& scene, const ArenaHealthPickup& pickup) {
  const bool large = pickup.type == HealthPickupType::Large;
  const Vec3 bodyHalfExtents = large
    ? Vec3{0.34F, 0.34F, 0.18F}
    : Vec3{0.24F, 0.24F, 0.14F};
  const Vec3 center = pickup.position + Vec3{0.0F, 0.0F, bodyHalfExtents.z};
  constexpr Vec3 forward = {1.0F, 0.0F, 0.0F};
  constexpr Vec3 right = {0.0F, 1.0F, 0.0F};
  constexpr Vec3 up = {0.0F, 0.0F, 1.0F};
  addOrientedBox(scene, center, bodyHalfExtents, forward, right, up, {245, 247, 248, 255});

  const float crossZ = bodyHalfExtents.z + 0.012F;
  const float stripLong = bodyHalfExtents.x * 0.58F;
  const float stripShort = bodyHalfExtents.x * 0.16F;
  addOrientedBox(
    scene,
    center + Vec3{0.0F, 0.0F, crossZ},
    {stripLong, stripShort, 0.018F},
    forward,
    right,
    up,
    {218, 34, 48, 255}
  );
  addOrientedBox(
    scene,
    center + Vec3{0.0F, 0.0F, crossZ + 0.004F},
    {stripShort, stripLong, 0.018F},
    forward,
    right,
    up,
    {218, 34, 48, 255}
  );
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
  pose.crouched = player.crouched;
  pose.sneaking = player.sneaking;
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
    [&](PlayerBodyPartType bodyPart,
        float forwardOffset,
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
          partBasis.up,
          bodyPart
      );
    };

  if (pose.airborne) {
    part(PlayerBodyPartType::Torso, 0.03F, 0.0F, 0.43F, 0.76F, 0.34F, 0.58F, kJumpPoseTorsoPitchRadians);
    part(PlayerBodyPartType::Hips, -0.02F, 0.0F, 0.35F, 0.49F, 0.31F, 0.48F);
    part(PlayerBodyPartType::Head, 0.0F, 0.0F, 0.78F, 1.0F, 0.34F, 0.36F);
    part(PlayerBodyPartType::LeftArm, 0.02F, -0.74F, 0.41F, 0.72F, 0.20F, 0.20F, kJumpPoseArmPitchRadians);
    part(PlayerBodyPartType::RightArm, 0.04F, 0.74F, 0.42F, 0.72F, 0.20F, 0.20F, kJumpPoseArmPitchRadians);
    part(PlayerBodyPartType::LeftLeg, -0.22F, -0.25F, 0.08F, 0.34F, 0.25F, 0.20F, kJumpPoseLegPitchRadians);
    part(PlayerBodyPartType::RightLeg, -0.14F, 0.25F, 0.06F, 0.34F, 0.25F, 0.20F, kJumpPoseLegPitchRadians);
    return;
  }

  if (pose.crouched) {
    part(PlayerBodyPartType::Torso, 0.05F, 0.0F, 0.39F, 0.73F, 0.36F, 0.58F, -8.0F * kDegreesToRadians);
    part(PlayerBodyPartType::Hips, -0.04F, 0.0F, 0.30F, 0.47F, 0.33F, 0.50F);
    part(PlayerBodyPartType::Head, 0.08F, 0.0F, 0.73F, 1.0F, 0.34F, 0.36F, -6.0F * kDegreesToRadians);
    part(PlayerBodyPartType::LeftArm, 0.04F, -0.74F, 0.34F, 0.69F, 0.20F, 0.20F, -5.0F * kDegreesToRadians);
    part(PlayerBodyPartType::RightArm, 0.04F, 0.74F, 0.34F, 0.69F, 0.20F, 0.20F, -5.0F * kDegreesToRadians);
    part(PlayerBodyPartType::LeftLeg, -0.18F, -0.25F, 0.0F, 0.33F, 0.28F, 0.20F, -35.0F * kDegreesToRadians);
    part(PlayerBodyPartType::RightLeg, 0.02F, 0.25F, 0.0F, 0.33F, 0.28F, 0.20F, -18.0F * kDegreesToRadians);
    return;
  }

  part(PlayerBodyPartType::Torso, 0.0F, 0.0F, 0.43F, 0.76F, 0.34F, 0.58F);
  part(PlayerBodyPartType::Hips, 0.0F, 0.0F, 0.34F, 0.48F, 0.31F, 0.48F);
  part(PlayerBodyPartType::Head, 0.0F, 0.0F, 0.78F, 1.0F, 0.34F, 0.36F);
  part(PlayerBodyPartType::LeftArm, 0.0F, -0.74F, 0.38F, 0.72F, 0.20F, 0.20F);
  part(PlayerBodyPartType::RightArm, 0.0F, 0.74F, 0.38F, 0.72F, 0.20F, 0.20F);
  part(PlayerBodyPartType::LeftLeg, 0.0F, -0.25F, 0.0F, 0.36F, 0.25F, 0.20F);
  part(PlayerBodyPartType::RightLeg, 0.0F, 0.25F, 0.0F, 0.36F, 0.25F, 0.20F);
}

struct DuelistPoseRequests {
  std::array<SkinnedModelPoseRequest, 8> values = {};
  std::size_t count = 0;

  void push(SkinnedModelPoseRequest request) {
    if (count < values.size()) values[count++] = request;
  }

  [[nodiscard]] std::span<const SkinnedModelPoseRequest> span() const {
    return {values.data(), count};
  }
};

[[nodiscard]] DuelistPoseRequests duelistPoseRequests(
  const PlayerState& player,
  bool leanEnabled,
  float leanScale,
  float animationTimeSeconds
) {
  const PlayerVisualPose pose = makePlayerVisualPose(player);
  const float lateralVelocity = dot(player.velocity, yawRight(player.viewYawRadians));
  const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);
  constexpr float kLocomotionDeadzoneSpeed =
    10.0F / kQuakeUnitsPerProjectUnit;
  constexpr float kReferenceRunSpeed =
    320.0F / kQuakeUnitsPerProjectUnit;
  const bool locomotionActive = horizontalSpeed > kLocomotionDeadzoneSpeed;
  const float leanAmount = leanEnabled
    ? std::clamp(lateralVelocity / 8.0F * leanScale, -1.0F, 1.0F)
    : 0.0F;
  const auto locomotionCycleTime =
    [animationTimeSeconds, horizontalSpeed](float cycleSeconds) {
      // Locomotion phase follows render time and speed, not input direction:
      // below 320 Quake units/sec the stride stays readable while cadence
      // slows down instead of shrinking into a tiny full-sprint shuffle.
      const float cycle = std::max(0.0001F, cycleSeconds);
      const float playbackRate = std::clamp(
        horizontalSpeed / kReferenceRunSpeed,
        0.0F,
        2.0F
      );
      const float phase =
        std::max(0.0F, animationTimeSeconds) * playbackRate;
      return std::fmod(phase, cycle);
    };
  DuelistPoseRequests poseRequests;
  if (pose.airborne) {
    if (player.velocity.z >= 0.0F) {
      const float jumpProgress = std::clamp((8.0F - player.velocity.z) / 8.0F, 0.0F, 1.0F);
      const float jumpTime = jumpProgress * 0.5833333F;
      poseRequests.push({"JUMP", jumpTime, 1.0F});
      poseRequests.push({"lg_duelist_jump", jumpTime, 1.0F});
    } else {
      const float fallProgress = std::clamp(-player.velocity.z / 12.0F, 0.0F, 1.0F);
      poseRequests.push({"FALL", fallProgress * 0.8333333F, 1.0F});
      poseRequests.push({"lg_duelist_jump", 0.9F, 1.0F});
    }
  } else if (pose.crouched) {
    poseRequests.push({"DUCKING", 0.4166667F, 1.0F});
    if (locomotionActive) {
      poseRequests.push({
        "CROUCH_WALK",
        locomotionCycleTime(1.0F),
        1.0F,
      });
    }
    poseRequests.push({"lg_duelist_crouch", 0.5833333F, 1.0F});
  } else if (pose.sneaking) {
    poseRequests.push({"IDLE", 0.8333333F, 1.0F});
    if (locomotionActive) {
      poseRequests.push({
        "SNEAK",
        locomotionCycleTime(1.3333333F),
        1.0F,
      });
    }
    poseRequests.push({"lg_duelist_sneak", 0.5833333F, 1.0F});
  } else {
    poseRequests.push({"IDLE", 0.8333333F, 1.0F});
    if (locomotionActive) {
      poseRequests.push({"RUN", locomotionCycleTime(1.0F), 1.0F});
    }
  }
  const auto addUpperBodyLean =
    [&poseRequests](std::string_view animationName, float weight) {
      poseRequests.push({
        animationName,
        0.5833333F,
        weight,
        SkinnedModelPoseMask::UpperBody,
      });
    };
  if (leanAmount > 0.02F) {
    // Lean is an additive presentation layer over locomotion: keep run/crouch
    // owning root, pelvis, and legs so strafing does not erase foot motion.
    addUpperBodyLean("LEAN_LEFT", std::fabs(leanAmount));
    addUpperBodyLean("lg_duelist_lean_left", std::fabs(leanAmount));
  } else if (leanAmount < -0.02F) {
    addUpperBodyLean("LEAN_RIGHT", std::fabs(leanAmount));
    addUpperBodyLean("lg_duelist_lean_right", std::fabs(leanAmount));
  }
  return poseRequests;
}

void addGltfPlayerModelInstance(
  Scene3D& scene,
  const GltfSkinnedModel& model,
  const PlayerState& player,
  RenderColor color,
  bool leanEnabled,
  float leanScale,
  float animationTimeSeconds,
  const PlayerPresentationFrame* presentation,
  std::uint8_t playerIndex,
  OutlineState outlineState,
  bool outlined,
  GltfSkinnedModel::PoseScratch& poseScratch
) {
  if (!model.loaded() || model.primitives().empty()) {
    return;
  }

  const PlayerModelBasis basis =
    playerModelBasis(player, false, leanScale, 0.0F);
  const float verticalScale = basis.height / kDuelistMaleHeight;
  const float horizontalScale = basis.radius / kDuelistMaleHalfWidth * 0.94F;
  const Vec3 base = player.position - basis.up * basis.halfHeight;
  const Vec3 translation = base -
    basis.forward * (kDuelistMaleDepthCenter * horizontalScale);
  const std::uint32_t firstBone =
    static_cast<std::uint32_t>(scene.gltfBonePalette.size());
  DuelistPoseRequests poseRequests;
  if (presentation != nullptr && presentation->poseLayerCount > 0U) {
    for (std::size_t index = 0; index < presentation->poseLayerCount; ++index) {
      const PlayerPoseLayer& layer = presentation->poseLayers[index];
      if (layer.mask == PlayerPoseLayerMask::UpperBody && !leanEnabled) {
        continue;
      }
      float weight = layer.weight;
      if (index == 0U && presentation->diagnostics.previousBlendWeight > 0.0F) {
        // The sampler blends each request over the accumulated pose. Establish
        // the previous clip fully, then blend the new clip by transition alpha.
        weight = 1.0F;
      }
      poseRequests.push({
        layer.animationName,
        layer.timeSeconds,
        weight,
        layer.mask == PlayerPoseLayerMask::UpperBody
          ? SkinnedModelPoseMask::UpperBody
          : SkinnedModelPoseMask::FullBody,
      });
    }
  } else {
    poseRequests = duelistPoseRequests(player, leanEnabled, leanScale, animationTimeSeconds);
  }
  const float aimPitch = presentation != nullptr
    ? presentation->torsoAimPitchRadians
    : std::clamp(player.viewPitchRadians, -0.78539816F, 0.78539816F);
  if (!model.appendBonePalette(
        poseRequests.span(),
        scene.gltfBonePalette,
        poseScratch,
        aimPitch
      )) {
    return;
  }
  const std::uint32_t boneCount =
    static_cast<std::uint32_t>(scene.gltfBonePalette.size()) - firstBone;
  GltfPlayerModelInstance instance;
  instance.modelRow0 = {
    basis.right.x * horizontalScale,
    basis.up.x * verticalScale,
    basis.forward.x * horizontalScale,
  };
  instance.modelRow1 = {
    basis.right.y * horizontalScale,
    basis.up.y * verticalScale,
    basis.forward.y * horizontalScale,
  };
  instance.modelRow2 = {
    basis.right.z * horizontalScale,
    basis.up.z * verticalScale,
    basis.forward.z * horizontalScale,
  };
  instance.modelTranslation = translation;
  instance.color = color;
  instance.localBounds = model.localBounds();
  instance.firstBone = firstBone;
  instance.boneCount = boneCount;
  instance.playerIndex = playerIndex;
  instance.outlineState = outlineState;
  instance.skinned = model.hasSkinnedPrimitives() && boneCount > 0U;
  instance.outlined = outlined;
  scene.gltfPlayerModelInstances.push_back(instance);
  ++scene.gltfPlayerModelStats.activeInstances;
  if (instance.skinned) {
    ++scene.gltfPlayerModelStats.gpuSkinnedInstances;
  } else {
    ++scene.gltfPlayerModelStats.rigidFallbackInstances;
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
  const float aimPitch = std::clamp(player.viewPitchRadians, -0.78539816F, 0.78539816F);
  const float pitchCos = std::cos(aimPitch);
  const float pitchSin = std::sin(aimPitch);
  const Vec3 horizontalForward = frame.basis.forward;
  const Vec3 horizontalUp = frame.basis.up;
  frame.basis.forward = normalize(
    horizontalForward * pitchCos + horizontalUp * pitchSin
  );
  frame.basis.up = normalize(
    horizontalUp * pitchCos - horizontalForward * pitchSin
  );
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
  const PlayerState& player,
  int weaponPosition,
  const ViewModelPresentationOutput& presentation
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
  if (weaponPosition == 1) {
    frame.hand += frame.basis.right * 0.30F;
  } else if (weaponPosition == 2) {
    frame.hand -= frame.basis.right * 0.30F;
  }
  frame.scale = 0.50F;
  // Apply motion in the already-current camera basis. It can move or rotate the
  // rendered weapon, but never changes the camera, crosshair, or gameplay aim.
  frame.hand += frame.basis.forward * presentation.translation.x;
  frame.hand += frame.basis.right * presentation.translation.y;
  frame.hand += frame.basis.up * presentation.translation.z;
  const auto rotatePair = [](Vec3& first, Vec3& second, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const Vec3 oldFirst = first;
    const Vec3 oldSecond = second;
    first = normalize(oldFirst * cosine - oldSecond * sine);
    second = normalize(oldFirst * sine + oldSecond * cosine);
  };
  rotatePair(frame.basis.forward, frame.basis.up, presentation.rotationRadians.x);
  rotatePair(frame.basis.forward, frame.basis.right, presentation.rotationRadians.y);
  rotatePair(frame.basis.right, frame.basis.up, presentation.rotationRadians.z);
  return frame;
}

void addFirstPersonWeaponModel(
  Scene3D& scene,
  const PlayerState& player,
  Weapon weapon,
  const RenderSettings& settings
) ;

[[nodiscard]] StaticMeshInstance weaponMeshInstance(
  MeshHandle mesh,
  RenderPass pass,
  const WeaponModelFrame& frame,
  float instanceScale,
  RenderColor color
) {
  const StaticMeshAsset* asset = staticMeshAsset(mesh);
  const MaterialMeshAsset* materialAsset = materialMeshAsset(mesh);
  const float scale = frame.scale * instanceScale;
  const Vec3 row0 = {
    frame.basis.forward.x * scale,
    frame.basis.right.x * scale,
    frame.basis.up.x * scale,
  };
  const Vec3 row1 = {
    frame.basis.forward.y * scale,
    frame.basis.right.y * scale,
    frame.basis.up.y * scale,
  };
  const Vec3 row2 = {
    frame.basis.forward.z * scale,
    frame.basis.right.z * scale,
    frame.basis.up.z * scale,
  };
  const float boundsScale = std::max(
    std::max(length(frame.basis.forward * scale), length(frame.basis.right * scale)),
    length(frame.basis.up * scale)
  );
  const float localRadius = materialAsset != nullptr
    ? materialAsset->localBounds.radius
    : (asset != nullptr ? asset->localBounds.radius : 0.0F);
  const float radius = localRadius * boundsScale;
  return {
    mesh,
    pass,
    row0,
    row1,
    row2,
    frame.hand,
    color,
    {frame.hand, radius},
  };
}

[[nodiscard]] StaticMeshInstance weaponMeshInstance(
  MeshHandle mesh,
  RenderPass pass,
  const WeaponModelFrame& frame,
  RenderColor color
) {
  return weaponMeshInstance(mesh, pass, frame, 1.0F, color);
}

void appendStaticMeshInstance(Scene3D& scene, const StaticMeshInstance& instance) {
  scene.staticMeshInstances.push_back(instance);
}

[[nodiscard]] WeaponModelFrame revolverRecoilFrame(
  WeaponModelFrame frame,
  float amount
) {
  const float recoil = std::clamp(amount, 0.0F, 1.0F);
  frame.hand -= frame.basis.forward * (0.060F * frame.scale * recoil);
  frame.hand += frame.basis.up * (0.025F * frame.scale * recoil);
  const float pitch = -8.0F * kDegreesToRadians * recoil;
  const float pitchCos = std::cos(pitch);
  const float pitchSin = std::sin(pitch);
  const Vec3 oldForward = frame.basis.forward;
  const Vec3 oldUp = frame.basis.up;
  frame.basis.forward = normalize(oldForward * pitchCos - oldUp * pitchSin);
  frame.basis.up = normalize(oldForward * pitchSin + oldUp * pitchCos);
  return frame;
}

[[nodiscard]] WeaponModelFrame revolverCylinderFrame(
  const WeaponModelFrame& frame,
  float rotationRadians
) {
  WeaponModelFrame cylinder = frame;
  cylinder.hand = weaponLocalPoint(frame, 0.025F, 0.0F, 0.085F);
  const float rotationCos = std::cos(rotationRadians);
  const float rotationSin = std::sin(rotationRadians);
  const Vec3 oldRight = frame.basis.right;
  const Vec3 oldUp = frame.basis.up;
  cylinder.basis.right = normalize(oldRight * rotationCos + oldUp * rotationSin);
  cylinder.basis.up = normalize(oldRight * -rotationSin + oldUp * rotationCos);
  return cylinder;
}

[[nodiscard]] WeaponModelFrame revolverGripAlignedFrame(WeaponModelFrame frame) {
  // WeaponModelFrame::hand is the player hand target. Move the authored grip
  // socket onto that point instead of incorrectly placing the model origin
  // there.
  frame.hand -= frame.basis.forward * (kRevolverGripSocket.x * frame.scale);
  frame.hand -= frame.basis.right * (kRevolverGripSocket.y * frame.scale);
  frame.hand -= frame.basis.up * (kRevolverGripSocket.z * frame.scale);
  return frame;
}

void appendRevolverInstances(
  Scene3D& scene,
  const WeaponModelFrame& frame,
  RenderPass pass,
  float cylinderRotationRadians
) {
  appendStaticMeshInstance(
    scene,
    weaponMeshInstance(
      MeshHandle::RemoteRevolverBody,
      pass,
      frame,
      {255, 255, 255, 255}
    )
  );
  appendStaticMeshInstance(
    scene,
    weaponMeshInstance(
      MeshHandle::RemoteRevolverCylinder,
      pass,
      revolverCylinderFrame(frame, cylinderRotationRadians),
      {255, 255, 255, 255}
    )
  );
}

[[nodiscard]] WeaponModelFrame machineGunBarrelFrame(
  const WeaponModelFrame& frame,
  float rotationRadians,
  float instanceScale
) {
  WeaponModelFrame barrels = frame;
  const Vec3 scaledPivot = kMachineGunBarrelPivot * instanceScale;
  const Vec3 pivotWorld = weaponLocalPoint(
    frame,
    scaledPivot.x,
    scaledPivot.y,
    scaledPivot.z
  );
  const float rotationCos = std::cos(rotationRadians);
  const float rotationSin = std::sin(rotationRadians);
  const Vec3 oldRight = frame.basis.right;
  const Vec3 oldUp = frame.basis.up;
  barrels.basis.right = normalize(oldRight * rotationCos + oldUp * rotationSin);
  barrels.basis.up = normalize(oldRight * -rotationSin + oldUp * rotationCos);
  // The body and barrel exports share weapon coordinates. Re-anchor the
  // rotated instance so the authored spin pivot stays fixed without wobble.
  barrels.hand = pivotWorld -
    barrels.basis.forward * (scaledPivot.x * frame.scale) -
    barrels.basis.right * (scaledPivot.y * frame.scale) -
    barrels.basis.up * (scaledPivot.z * frame.scale);
  return barrels;
}

void appendMachineGunInstances(
  Scene3D& scene,
  const WeaponModelFrame& frame,
  RenderPass pass,
  float barrelRotationRadians,
  float instanceScale = 1.0F
) {
  appendStaticMeshInstance(
    scene,
    weaponMeshInstance(
      MeshHandle::RemoteMachineGunBody,
      pass,
      frame,
      instanceScale,
      {255, 255, 255, 255}
    )
  );
  appendStaticMeshInstance(
    scene,
    weaponMeshInstance(
      MeshHandle::RemoteMachineGunBarrels,
      pass,
      machineGunBarrelFrame(frame, barrelRotationRadians, instanceScale),
      instanceScale,
      {255, 255, 255, 255}
    )
  );
}

[[nodiscard]] WeaponModelFrame rocketLauncherGripAlignedFrame(
  WeaponModelFrame frame
) {
  frame.hand -= frame.basis.forward * (kRocketLauncherGripSocket.x * frame.scale);
  frame.hand -= frame.basis.right * (kRocketLauncherGripSocket.y * frame.scale);
  frame.hand -= frame.basis.up * (kRocketLauncherGripSocket.z * frame.scale);
  return frame;
}

[[nodiscard]] WeaponModelFrame rocketLauncherRecoilFrame(
  WeaponModelFrame frame,
  float recoilAmount
) {
  const float recoil = std::clamp(recoilAmount, 0.0F, 1.0F);
  frame.hand -= frame.basis.forward * (0.085F * frame.scale * recoil);
  frame.hand += frame.basis.up * (0.030F * frame.scale * recoil);
  const float pitch = -5.5F * kDegreesToRadians * recoil;
  const float pitchCos = std::cos(pitch);
  const float pitchSin = std::sin(pitch);
  const Vec3 oldForward = frame.basis.forward;
  const Vec3 oldUp = frame.basis.up;
  frame.basis.forward = normalize(oldForward * pitchCos - oldUp * pitchSin);
  frame.basis.up = normalize(oldForward * pitchSin + oldUp * pitchCos);
  return frame;
}

[[nodiscard]] WeaponModelFrame rocketLauncherPartFrame(
  const WeaponModelFrame& frame,
  Vec3 pivot,
  float forwardOffset,
  float pitchRadians
) {
  WeaponModelFrame part = frame;
  part.hand = weaponLocalPoint(frame, pivot.x + forwardOffset, pivot.y, pivot.z);
  const float pitchCos = std::cos(pitchRadians);
  const float pitchSin = std::sin(pitchRadians);
  const Vec3 oldForward = frame.basis.forward;
  const Vec3 oldUp = frame.basis.up;
  part.basis.forward = normalize(oldForward * pitchCos - oldUp * pitchSin);
  part.basis.up = normalize(oldForward * pitchSin + oldUp * pitchCos);
  return part;
}

void appendRocketLauncherInstances(
  Scene3D& scene,
  const WeaponModelFrame& frame,
  RenderPass pass,
  float mechanicalAmount
) {
  appendStaticMeshInstance(
    scene,
    weaponMeshInstance(
      MeshHandle::RemoteRocketLauncherBody,
      pass,
      frame,
      {255, 255, 255, 255}
    )
  );
  appendStaticMeshInstance(
    scene,
    weaponMeshInstance(
      MeshHandle::RemoteRocketLauncherRecoil,
      pass,
      rocketLauncherPartFrame(
        frame,
        kRocketLauncherRecoilPivot,
        -0.052F * mechanicalAmount,
        0.0F
      ),
      {255, 255, 255, 255}
    )
  );
  appendStaticMeshInstance(
    scene,
    weaponMeshInstance(
      MeshHandle::RemoteRocketLauncherLatch,
      pass,
      rocketLauncherPartFrame(
        frame,
        kRocketLauncherLatchPivot,
        0.0F,
        -8.0F * kDegreesToRadians * mechanicalAmount
      ),
      {255, 255, 255, 255}
    )
  );
}

void appendFreezeGunInstances(
  Scene3D& scene,
  const WeaponModelFrame& frame,
  RenderPass pass,
  float firingAmount,
  float coolantPulse,
  float vibrationPhaseRadians
) {
  appendStaticMeshInstance(scene, weaponMeshInstance(
    MeshHandle::RemoteFreezeGunBody, pass, frame, {255, 255, 255, 255}));
  WeaponModelFrame focus = frame;
  // Only the optical core moves. Keeping the body and muzzle socket stable
  // prevents presentation motion from implying a different gameplay trace.
  const float active = std::clamp(firingAmount, 0.0F, 1.0F);
  focus.hand += focus.basis.forward * (0.035F * frame.scale * active);
  focus.hand += focus.basis.right * (
    std::sin(vibrationPhaseRadians) * 0.0025F * frame.scale * active
  );
  focus.hand += focus.basis.up * (
    std::cos(vibrationPhaseRadians * 1.31F) * 0.0018F * frame.scale * active
  );
  appendStaticMeshInstance(scene, weaponMeshInstance(
    MeshHandle::RemoteFreezeGunFocus, pass, focus, {255, 255, 255, 255}));

  WeaponModelFrame coolant = frame;
  coolant.hand = weaponLocalPoint(
    frame,
    kFreezeGunCoolantPivot.x,
    kFreezeGunCoolantPivot.y,
    kFreezeGunCoolantPivot.z
  );
  coolant.scale *= 1.0F + 0.018F * std::clamp(coolantPulse, 0.0F, 1.0F);
  appendStaticMeshInstance(scene, weaponMeshInstance(
    MeshHandle::RemoteFreezeGunCoolant, pass, coolant, {255, 255, 255, 255}));
}

[[nodiscard]] WeaponModelFrame plasmaGunGripAlignedFrame(
  WeaponModelFrame frame
) {
  // WeaponModelFrame::hand is the hand target, so place the authored primary
  // grip there before applying presentation-only containment transforms.
  frame.hand -= frame.basis.forward * (kPlasmaGunGripSocket.x * frame.scale);
  frame.hand -= frame.basis.right * (kPlasmaGunGripSocket.y * frame.scale);
  frame.hand -= frame.basis.up * (kPlasmaGunGripSocket.z * frame.scale);
  return frame;
}

void appendPlasmaGunInstances(
  Scene3D& scene,
  const WeaponModelFrame& frame,
  RenderPass pass,
  float containmentAmount
) {
  appendStaticMeshInstance(scene, weaponMeshInstance(
    MeshHandle::RemotePlasmaGunBody, pass, frame, {255, 255, 255, 255}));

  const float response = std::clamp(containmentAmount, 0.0F, 1.0F);
  WeaponModelFrame prongs = frame;
  prongs.hand = weaponLocalPoint(
    frame,
    kPlasmaGunProngPivot.x,
    kPlasmaGunProngPivot.y,
    kPlasmaGunProngPivot.z
  );
  prongs.hand -= prongs.basis.forward * (0.018F * frame.scale * response);
  appendStaticMeshInstance(scene, weaponMeshInstance(
    MeshHandle::RemotePlasmaGunProngs, pass, prongs, {255, 255, 255, 255}));

  WeaponModelFrame core = frame;
  core.hand = weaponLocalPoint(
    frame,
    kPlasmaGunCorePivot.x,
    kPlasmaGunCorePivot.y,
    kPlasmaGunCorePivot.z
  );
  core.scale *= 1.0F - 0.22F * response;
  // The modeled socket stays fixed while the energy source and cage react.
  // This keeps the visual flourish separate from authoritative projectiles.
  appendStaticMeshInstance(scene, weaponMeshInstance(
    MeshHandle::RemotePlasmaGunCore, pass, core, {255, 255, 255, 255}));
}

[[nodiscard]] WeaponModelFrame freezeGunViewModelFrame(
  WeaponModelFrame frame,
  int weaponPosition
) {
  const float sideOffset = weaponPosition == 1
    ? 0.30F
    : (weaponPosition == 2 ? -0.30F : 0.0F);
  // Aim the authored +X axis toward a point on the crosshair ray. Side
  // presets therefore toe inward symmetrically and the model naturally
  // continues into the beam instead of merely sharing its muzzle position.
  constexpr float kConvergenceDepthFromHand = 2.56F;
  constexpr float kVerticalRiseFromHand = 0.38F;
  const Vec3 aimForward = normalize(
    frame.basis.forward * kConvergenceDepthFromHand +
    frame.basis.up * kVerticalRiseFromHand -
    frame.basis.right * sideOffset
  );
  const Vec3 aimRight = normalize(
    frame.basis.right - aimForward * dot(frame.basis.right, aimForward)
  );
  frame.basis.forward = aimForward;
  frame.basis.right = aimRight;
  frame.basis.up = normalize(cross(aimRight, aimForward));
  // The extended emitter should read clearly without reaching farther up the
  // screen than the revolver, which is the longest standard viewmodel.
  frame.hand -= frame.basis.up * 0.220F;
  return frame;
}

[[nodiscard]] WeaponModelFrame machineGunFiringFrame(
  WeaponModelFrame frame,
  const RenderSettings& settings
) {
  const float recoil = std::clamp(settings.machineGunRecoilAmount, 0.0F, 1.0F);
  const float vibration = std::clamp(
    settings.machineGunVibrationAmount,
    0.0F,
    1.0F
  );
  frame.hand -= frame.basis.forward * (0.020F * frame.scale * recoil);
  frame.hand += frame.basis.up * (0.004F * frame.scale * recoil);
  frame.hand += frame.basis.right * (
    std::sin(settings.machineGunVibrationPhaseRadians) *
    0.0035F * frame.scale * vibration
  );
  frame.hand += frame.basis.up * (
    std::cos(settings.machineGunVibrationPhaseRadians * 1.7F) *
    0.0025F * frame.scale * vibration
  );

  const float pitch = -1.25F * kDegreesToRadians * recoil;
  const float pitchCos = std::cos(pitch);
  const float pitchSin = std::sin(pitch);
  const Vec3 oldForward = frame.basis.forward;
  const Vec3 oldUp = frame.basis.up;
  frame.basis.forward = normalize(oldForward * pitchCos - oldUp * pitchSin);
  frame.basis.up = normalize(oldForward * pitchSin + oldUp * pitchCos);
  return frame;
}

void addFirstPersonWeaponModel(
  Scene3D& scene,
  const PlayerState& player,
  Weapon weapon,
  const RenderSettings& settings
) {
  const WeaponModelFrame frame =
    firstPersonWeaponModelFrame(
      player,
      settings.weaponPosition,
      settings.viewModelPresentation
    );
  switch (weapon) {
  case Weapon::MachineGun: {
    WeaponModelFrame weaponFrame = frame;
    weaponFrame.hand -= weaponFrame.basis.forward * 0.10F;
    weaponFrame = machineGunFiringFrame(weaponFrame, settings);
    appendMachineGunInstances(
      scene,
      weaponFrame,
      RenderPass::ViewModel,
      settings.machineGunBarrelRotationRadians,
      1.0F / 0.78F
    );
    scene.viewModelStats.drawCalls += 2U;
    break;
  }
  case Weapon::Shotgun: {
    WeaponModelFrame weaponFrame = frame;
    weaponFrame.hand -= weaponFrame.basis.forward * 0.10F;
    appendStaticMeshInstance(
      scene,
      weaponMeshInstance(
        MeshHandle::RemoteShotgun,
        RenderPass::ViewModel,
        weaponFrame,
        1.0F / 0.78F,
        {255, 255, 255, 255}
      )
    );
    ++scene.viewModelStats.drawCalls;
    break;
  }
  case Weapon::Revolver: {
    WeaponModelFrame revolverFrame = frame;
    // Give the long revolver more breathing room in first person: move it
    // farther down the view axis and reduce its authored mesh scale.
    revolverFrame.hand += revolverFrame.basis.forward * 0.16F;
    revolverFrame.scale *= 0.80F;
    const WeaponModelFrame recoilFrame = revolverRecoilFrame(
      revolverFrame,
      settings.revolverRecoilAmount
    );
    appendRevolverInstances(
      scene,
      recoilFrame,
      RenderPass::ViewModel,
      settings.revolverCylinderRotationRadians
    );
    scene.viewModelStats.drawCalls += 2U;
    break;
  }
  case Weapon::RocketLauncher: {
    WeaponModelFrame weaponFrame = frame;
    weaponFrame.scale *= kRocketLauncherViewModelScale;
    // Apply camera-relative placement before grip alignment. The authored
    // grip sits far behind and below the model origin, so the generic frame
    // otherwise pushes this long weapon too far forward and high on screen.
    weaponFrame.hand += weaponFrame.basis.forward *
      kRocketLauncherViewModelForwardOffset;
    weaponFrame.hand += weaponFrame.basis.up *
      kRocketLauncherViewModelUpOffset;
    weaponFrame = rocketLauncherGripAlignedFrame(weaponFrame);
    weaponFrame = rocketLauncherRecoilFrame(
      weaponFrame,
      settings.rocketLauncherRecoilAmount
    );
    appendRocketLauncherInstances(
      scene,
      weaponFrame,
      RenderPass::ViewModel,
      settings.rocketLauncherMechanicalAmount
    );
    scene.viewModelStats.drawCalls += 3U;
    break;
  }
  case Weapon::FreezeGun: {
    WeaponModelFrame weaponFrame = frame;
    weaponFrame.scale *= 0.82F;
    weaponFrame.hand -= weaponFrame.basis.forward * 0.08F;
    weaponFrame = freezeGunViewModelFrame(
      weaponFrame,
      settings.weaponPosition
    );
    appendFreezeGunInstances(
      scene,
      weaponFrame,
      RenderPass::ViewModel,
      settings.freezeGunFiringAmount,
      settings.freezeGunCoolantPulse,
      settings.freezeGunVibrationPhaseRadians
    );
    scene.viewModelStats.drawCalls += 3U;
    break;
  }
  case Weapon::PlasmaGun: {
    WeaponModelFrame weaponFrame = frame;
    weaponFrame.scale *= 0.88F;
    weaponFrame.hand -= weaponFrame.basis.forward * 0.06F;
    // The viewmodel has no rendered hands and uses its own screen-space anchor.
    // Applying the third-person grip correction here would lift the gun by the
    // authored grip offset and push it unnecessarily close to the camera.
    appendPlasmaGunInstances(
      scene,
      weaponFrame,
      RenderPass::ViewModel,
      settings.plasmaGunContainmentAmount
    );
    scene.viewModelStats.drawCalls += 3U;
    break;
  }
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

[[nodiscard]] float thirdPersonWeaponVisualScale(Weapon weapon) {
  switch (weapon) {
  case Weapon::LightningGun:
  case Weapon::FreezeGun:
    return 0.55F;
  case Weapon::RocketLauncher:
  case Weapon::GrenadeLauncher:
  case Weapon::PlasmaGun:
    // The baked RL and PG assets have a much bulkier silhouette than the
    // established third-person weapons, so keep their world models readable.
    return 0.50F;
  case Weapon::Revolver:
    return 0.45F;
  default:
    return 0.65F;
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
    [&](Vec3 center,
        Vec3 halfExtents,
        Vec3 forward,
        Vec3 right,
        Vec3 up,
        PlayerBodyPartType) {
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

void addFreezeBeamParticles(
  Scene3D& scene,
  Vec3 start,
  Vec3 end,
  float pulse
) {
  const Vec3 beam = end - start;
  const float beamLength = length(beam);
  if (beamLength <= 0.0001F) {
    return;
  }
  const Vec3 direction = beam / beamLength;
  Vec3 side = normalize(cross(direction, {0.0F, 0.0F, 1.0F}));
  if (length(side) <= 0.0001F) {
    side = {1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(side, direction));
  constexpr int kParticleCount = 18;
  const float pulse01 = (std::clamp(pulse, -1.0F, 1.0F) + 1.0F) * 0.5F;
  for (int index = 0; index < kParticleCount; ++index) {
    const float t = (static_cast<float>(index) + 0.45F) /
      static_cast<float>(kParticleCount);
    const float phase = static_cast<float>(index) * 2.171F + pulse * 1.9F;
    const float swirl = 0.055F + 0.03F * std::sin(phase * 0.73F);
    const Vec3 center =
      start + beam * t +
      side * (std::cos(phase) * swirl) +
      up * (std::sin(phase) * swirl);
    const float radius =
      0.025F + 0.009F * static_cast<float>((index * 5) % 4) +
      pulse01 * 0.015F;
    const std::uint8_t alpha = static_cast<std::uint8_t>(
      std::clamp(38.0F + pulse01 * 38.0F + std::sin(phase) * 20.0F, 20.0F, 96.0F)
    );
    const RenderColor color = {
      static_cast<std::uint8_t>(218),
      static_cast<std::uint8_t>(248),
      static_cast<std::uint8_t>(255),
      alpha,
    };
    addQuad(
      scene,
      center + up * radius,
      center + side * radius,
      center - up * radius,
      center - side * radius,
      color
    );
  }
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

[[nodiscard]] Vec3 hiddenWeaponVisualOrigin(const PlayerState& player) {
  return playerEyePosition(player) +
    cameraForward(player.viewYawRadians, player.viewPitchRadians) * 0.24F -
    cameraUp(player.viewYawRadians, player.viewPitchRadians) * 0.54F;
}

// Visual-only offset: authoritative hitscan traces still use the server start/end.
[[nodiscard]] Vec3 remoteHitscanMuzzlePosition(
  const RemotePlayerView& remote,
  Weapon weapon,
  const RenderSettings& settings
) {
  const bool leanEnabled = remote.teammate
    ? settings.teammateLeanEnabled
    : settings.enemyLeanEnabled;
  const float leanScale = remote.teammate
    ? settings.teammateLeanScale
    : settings.enemyLeanScale;
  WeaponModelFrame frame =
    weaponModelFrame(remote.player, leanEnabled, leanScale);
  frame.scale *= thirdPersonWeaponVisualScale(weapon);
  if (weapon == Weapon::Revolver) {
    frame = revolverGripAlignedFrame(frame);
  }
  switch (weapon) {
  case Weapon::LightningGun:
  case Weapon::FreezeGun:
    return weaponLocalPoint(frame, 1.00F, 0.0F, 0.105F);
  case Weapon::Railgun:
    return weaponLocalPoint(frame, 0.78F, 0.0F, 0.09F);
  case Weapon::Revolver:
    return weaponLocalPoint(frame, 0.68F, 0.0F, 0.155F);
  default:
    return playerEyePosition(remote.player);
  }
}

[[nodiscard]] Vec3 projectileVisualPosition(
  const RocketProjectileSnapshot& projectile,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const RenderSettings& settings
) {
  const std::size_t owner = static_cast<std::size_t>(projectile.owner);
  if (owner == static_cast<std::size_t>(settings.localPlayerIndex)) {
    const Vec3 localEye = playerEyePosition(localPlayer);
    if (!settings.showOwnWeapons) {
      const float blend = 1.0F - std::clamp(
        length(projectile.position - localEye) / 1.20F,
        0.0F,
        1.0F
      );
      return projectile.position +
        (hiddenWeaponVisualOrigin(localPlayer) - localEye) * blend;
    }
    if (
      projectile.weapon == Weapon::PlasmaGun ||
      projectile.weapon == Weapon::RocketLauncher
    ) {
      const Vec3 muzzle = projectile.weapon == Weapon::RocketLauncher
        ? firstPersonRocketLauncherMuzzlePosition(localPlayer, settings)
        : firstPersonPlasmaGunMuzzlePosition(localPlayer, settings);
      // Begin at the presentation socket, then converge quickly to the
      // authoritative projectile so the visible trajectory remains centered.
      const float blend = 1.0F - std::clamp(
        length(projectile.position - localEye) / 1.20F,
        0.0F,
        1.0F
      );
      return projectile.position +
        (muzzle - localEye) * blend;
    }
    return projectile.position;
  }
  if (
    projectile.weapon != Weapon::PlasmaGun &&
    projectile.weapon != Weapon::RocketLauncher
  ) {
    return projectile.position;
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
  WeaponModelFrame frame = weaponModelFrame(remote.player, leanEnabled, leanScale);
  frame.scale *= thirdPersonWeaponVisualScale(projectile.weapon);
  Vec3 muzzle;
  if (projectile.weapon == Weapon::RocketLauncher) {
    frame = rocketLauncherGripAlignedFrame(frame);
    muzzle = weaponLocalPoint(
      frame,
      kRocketLauncherMuzzleSocket.x -
        0.052F * remote.rocketLauncherMechanicalAmount,
      kRocketLauncherMuzzleSocket.y,
      kRocketLauncherMuzzleSocket.z
    );
  } else {
    frame = plasmaGunGripAlignedFrame(frame);
    muzzle = weaponLocalPoint(
      frame,
      kPlasmaGunMuzzleSocket.x,
      kPlasmaGunMuzzleSocket.y,
      kPlasmaGunMuzzleSocket.z
    );
  }
  const float blend = 1.0F - std::clamp(
    length(projectile.position - remoteEye) / 1.20F,
    0.0F,
    1.0F
  );
  return projectile.position + (muzzle - remoteEye) * blend;
}

void addLayeredFreezeBeam(
  Scene3D& scene,
  Vec3 start,
  Vec3 end,
  float phaseRadians,
  float firingAmount,
  float activationFlash
) {
  const Vec3 beam = end - start;
  const float beamLength = length(beam);
  if (beamLength <= 0.0001F) {
    return;
  }
  const Vec3 direction = beam / beamLength;
  Vec3 side = normalize(cross(direction, {0.0F, 0.0F, 1.0F}));
  if (length(side) <= 0.0001F) {
    side = {1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(side, direction));
  const float active = std::clamp(firingAmount, 0.0F, 1.0F);

  // The exact authoritative trace is always the brightest, straightest layer.
  addSegment(scene, start, end, 0.020F, {238, 253, 255, 245});
  addSegment(scene, start, end, 0.075F, {92, 211, 255, 46});

  constexpr int kSegments = 14;
  for (int strand = 0; strand < 2; ++strand) {
    Vec3 previous = start;
    for (int index = 1; index <= kSegments; ++index) {
      const float t = static_cast<float>(index) / static_cast<float>(kSegments);
      const float envelope = std::sin(t * kTwoPi * 0.5F);
      const float phase = phaseRadians * 0.42F +
        static_cast<float>(index) * 1.37F +
        static_cast<float>(strand) * kTwoPi * 0.5F;
      const Vec3 displacement =
        side * (std::sin(phase) * 0.035F * envelope) +
        up * (std::cos(phase * 0.73F) * 0.024F * envelope);
      const Vec3 current = start + beam * t + displacement;
      addSegment(scene, previous, current, 0.012F, {132, 229, 255, 82});
      previous = current;
    }
  }

  addFreezeBeamParticles(scene, start, end, std::sin(phaseRadians));
  const float flash = std::clamp(activationFlash, 0.0F, 1.0F);
  addSphereApprox(
    scene,
    start,
    0.055F + 0.035F * flash + 0.012F * active,
    {205, 249, 255, static_cast<std::uint8_t>(95.0F + flash * 95.0F)}
  );

  // Sparse vapor vents sideways/backward from the focusing forks.
  for (int index = 0; index < 6; ++index) {
    const float seed = static_cast<float>(index) * 2.31F + phaseRadians * 0.18F;
    const float distance = 0.035F + static_cast<float>(index) * 0.018F;
    const Vec3 center = start - direction * distance +
      side * (std::sin(seed) * (0.045F + distance)) +
      up * (std::cos(seed * 0.83F) * 0.035F);
    const float radius = 0.018F + static_cast<float>(index % 3) * 0.006F;
    addQuad(
      scene,
      center + up * radius,
      center + side * radius,
      center - up * radius,
      center - side * radius,
      {218, 248, 255, static_cast<std::uint8_t>(28 + index * 5)}
    );
  }
}

constexpr float kRemotePlayerVisualCullMargin = 0.35F;

[[nodiscard]] Vec3 remotePlayerVisualSphereCenter(
  const PlayerState& player
) {
  return player.position;
}

[[nodiscard]] float remotePlayerVisualSphereRadius(
  const PlayerState& player
) {
  const float baseRadius = std::sqrt(
    player.bounds.radius * player.bounds.radius +
      player.bounds.halfHeight * player.bounds.halfHeight
  );
  // Covers held weapon reach, expanded outline width, modest pose/lean
  // displacement, and the default floating healthbar/name anchor above the body.
  return baseRadius + kRemotePlayerVisualCullMargin;
}

} // namespace

void appendCollisionDebugGeometry(
  Scene3D& scene,
  const Arena& arena,
  int mode
) {
  if (mode <= 0 || mode > 5) {
    return;
  }

  constexpr RenderColor visibleSolidColor = {64, 160, 255, 72};
  constexpr RenderColor playerClipColor = {72, 255, 128, 96};
  constexpr RenderColor weaponClipColor = {255, 156, 48, 104};
  constexpr RenderColor triggerColor = {208, 96, 255, 110};
  constexpr float kSurfaceOffset = 0.006F;

  const auto includes = [mode](int category) {
    return mode == 1 || mode == category;
  };
  const auto colorFor = [&](ArenaCollisionKind kind) {
    switch (kind) {
      case ArenaCollisionKind::VisibleSolid: return visibleSolidColor;
      case ArenaCollisionKind::PlayerClip: return playerClipColor;
      case ArenaCollisionKind::WeaponClip: return weaponClipColor;
    }
    return visibleSolidColor;
  };
  const auto categoryFor = [](ArenaCollisionKind kind) {
    switch (kind) {
      case ArenaCollisionKind::VisibleSolid: return 2;
      case ArenaCollisionKind::PlayerClip: return 3;
      case ArenaCollisionKind::WeaponClip: return 4;
    }
    return 2;
  };
  const auto addBox = [&](Vec3 minimum, Vec3 maximum, RenderColor color) {
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
      {{0, 3, 2, 1}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}},
      {{1, 2, 6, 5}}, {{2, 3, 7, 6}}, {{3, 0, 4, 7}},
    }};
    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
      const Vec3 offset = wallFaceNormal(faceIndex) * kSurfaceOffset;
      const auto& face = faces[faceIndex];
      addQuad(
        scene,
        corners[face[0]] + offset,
        corners[face[1]] + offset,
        corners[face[2]] + offset,
        corners[face[3]] + offset,
        color
      );
    }
  };

  // The overlay is intentionally rebuilt only while the debug cvar is active.
  // It observes the exact authoritative hulls but never participates in traces.
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    if (includes(categoryFor(wall.collisionKind))) {
      addBox(wall.min, wall.max, colorFor(wall.collisionKind));
    }
  }
  for (std::size_t brushIndex = 0; brushIndex < arena.brushCount; ++brushIndex) {
    const ArenaBrush& brush = arena.brushes[brushIndex];
    if (!includes(categoryFor(brush.collisionKind))) {
      continue;
    }
    const RenderColor color = colorFor(brush.collisionKind);
    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      const ArenaBrushFace& face = brush.faces[faceIndex];
      if (face.vertexCount < 3U) {
        continue;
      }
      const Vec3 origin =
        brush.vertices[face.vertices[0]] + face.normal * kSurfaceOffset;
      for (std::uint8_t vertex = 1U; vertex + 1U < face.vertexCount; ++vertex) {
        addTriangle(
          scene,
          origin,
          brush.vertices[face.vertices[vertex]] + face.normal * kSurfaceOffset,
          brush.vertices[face.vertices[vertex + 1U]] + face.normal * kSurfaceOffset,
          color
        );
      }
    }
  }
  if (includes(5)) {
    for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
      addBox(arena.jumpPads[index].min, arena.jumpPads[index].max, triggerColor);
    }
    for (std::size_t index = 0; index < arena.teleportCount; ++index) {
      addBox(arena.teleports[index].min, arena.teleports[index].max, triggerColor);
    }
    if (arena.mcguffin.hasRedBase) {
      addBox(arena.mcguffin.redBase.min, arena.mcguffin.redBase.max, triggerColor);
    }
    if (arena.mcguffin.hasBlueBase) {
      addBox(arena.mcguffin.blueBase.min, arena.mcguffin.blueBase.max, triggerColor);
    }
  }
}

[[nodiscard]] std::vector<Vertex3D> proceduralWeaponVertices(Weapon weapon);

const StaticMeshAsset* staticMeshAsset(MeshHandle handle) {
  static const std::vector<Vertex3D> shotgunVertices =
    bakedWeaponVertices(kShotgunWeaponModel, 0.78F, kQuarterTurnRadians);
  static const std::vector<Vertex3D> grenadeLauncherVertices =
    proceduralWeaponVertices(Weapon::GrenadeLauncher);
  static const std::vector<Vertex3D> lightningGunVertices =
    proceduralWeaponVertices(Weapon::LightningGun);
  static const std::vector<Vertex3D> railgunVertices =
    proceduralWeaponVertices(Weapon::Railgun);
  static const StaticMeshAsset shotgunAsset = {
    MeshHandle::RemoteShotgun,
    std::span<const Vertex3D>(shotgunVertices.data(), shotgunVertices.size()),
    meshBounds(shotgunVertices),
    RenderPass::OpaqueWorld,
  };
  static const StaticMeshAsset grenadeLauncherAsset = {
    MeshHandle::RemoteGrenadeLauncher,
    std::span<const Vertex3D>(grenadeLauncherVertices.data(), grenadeLauncherVertices.size()),
    meshBounds(grenadeLauncherVertices),
    RenderPass::OpaqueWorld,
  };
  static const StaticMeshAsset lightningGunAsset = {
    MeshHandle::RemoteLightningGun,
    std::span<const Vertex3D>(lightningGunVertices.data(), lightningGunVertices.size()),
    meshBounds(lightningGunVertices),
    RenderPass::OpaqueWorld,
  };
  static const StaticMeshAsset railgunAsset = {
    MeshHandle::RemoteRailgun,
    std::span<const Vertex3D>(railgunVertices.data(), railgunVertices.size()),
    meshBounds(railgunVertices),
    RenderPass::OpaqueWorld,
  };
  switch (handle) {
  case MeshHandle::PlayerBoxCube:
    return &kPlayerBoxCubeAsset;
  case MeshHandle::PlasmaCore:
    return &kPlasmaCoreAsset;
  case MeshHandle::ExplosionCore:
    return &kExplosionCoreAsset;
  case MeshHandle::RocketProjectile:
    return &kRocketProjectileAsset;
  case MeshHandle::GrenadeProjectile:
    return &kGrenadeProjectileAsset;
  case MeshHandle::MachineGunTracer:
    return &kMachineGunTracerAsset;
  case MeshHandle::ShotgunTracer:
    return &kShotgunTracerAsset;
  case MeshHandle::RemoteMachineGunBody:
  case MeshHandle::RemoteMachineGunBarrels:
    return nullptr;
  case MeshHandle::RemoteShotgun:
    return &shotgunAsset;
  case MeshHandle::RemoteGrenadeLauncher:
    return &grenadeLauncherAsset;
  case MeshHandle::RemoteRocketLauncherBody:
  case MeshHandle::RemoteRocketLauncherRecoil:
  case MeshHandle::RemoteRocketLauncherLatch:
  case MeshHandle::RemoteFreezeGunBody:
  case MeshHandle::RemoteFreezeGunFocus:
  case MeshHandle::RemoteFreezeGunCoolant:
  case MeshHandle::RemotePlasmaGunBody:
  case MeshHandle::RemotePlasmaGunProngs:
  case MeshHandle::RemotePlasmaGunCore:
    return nullptr;
  case MeshHandle::RemoteLightningGun:
    return &lightningGunAsset;
  case MeshHandle::RemoteRailgun:
    return &railgunAsset;
  case MeshHandle::RemoteRevolverBody:
  case MeshHandle::RemoteRevolverCylinder:
    return nullptr;
  case MeshHandle::Invalid:
    break;
  }
  return nullptr;
}

const MaterialMeshAsset* materialMeshAsset(MeshHandle handle) {
  static const std::vector<WeaponMaterialVertex3D> machineGunBodyVertices =
    bakedMaterialWeaponVertices(kMachineGunBodyMaterialModel, 0.78F);
  static const std::vector<WeaponMaterialVertex3D> machineGunBarrelVertices =
    bakedMaterialWeaponVertices(kMachineGunBarrelMaterialModel, 0.78F);
  static const std::vector<WeaponMaterialVertex3D> revolverBodyVertices =
    bakedMaterialWeaponVertices(kRevolverBodyModel);
  static const std::vector<WeaponMaterialVertex3D> revolverCylinderVertices =
    bakedMaterialWeaponVertices(kRevolverCylinderModel);
  static const std::vector<WeaponMaterialVertex3D> rocketLauncherBodyVertices =
    bakedMaterialWeaponVertices(kRocketLauncherBodyMaterialModel);
  static const std::vector<WeaponMaterialVertex3D> rocketLauncherRecoilVertices =
    bakedMaterialWeaponVertices(kRocketLauncherRecoilMaterialModel);
  static const std::vector<WeaponMaterialVertex3D> rocketLauncherLatchVertices =
    bakedMaterialWeaponVertices(kRocketLauncherLatchMaterialModel);
  static const std::vector<WeaponMaterialVertex3D> freezeGunBodyVertices =
    bakedMaterialWeaponVertices(kFreezeGunBodyMaterialModel);
  static const std::vector<WeaponMaterialVertex3D> freezeGunFocusVertices =
    bakedMaterialWeaponVertices(kFreezeGunFocusMaterialModel);
  static const std::vector<WeaponMaterialVertex3D> freezeGunCoolantVertices =
    bakedMaterialWeaponVertices(kFreezeGunCoolantMaterialModel);
  static const std::vector<WeaponMaterialVertex3D> plasmaGunBodyVertices =
    bakedMaterialWeaponVertices(kPlasmaGunBodyMaterialModel);
  static const std::vector<WeaponMaterialVertex3D> plasmaGunProngVertices =
    bakedMaterialWeaponVertices(kPlasmaGunProngMaterialModel);
  static const std::vector<WeaponMaterialVertex3D> plasmaGunCoreVertices =
    bakedMaterialWeaponVertices(kPlasmaGunCoreMaterialModel);
  static const MaterialMeshAsset body = {
    MeshHandle::RemoteRevolverBody,
    revolverBodyVertices,
    materialMeshBounds(revolverBodyVertices),
  };
  static const MaterialMeshAsset cylinder = {
    MeshHandle::RemoteRevolverCylinder,
    revolverCylinderVertices,
    materialMeshBounds(revolverCylinderVertices),
  };
  static const MaterialMeshAsset machineGunBody = {
    MeshHandle::RemoteMachineGunBody,
    machineGunBodyVertices,
    materialMeshBounds(machineGunBodyVertices),
  };
  static const MaterialMeshAsset machineGunBarrels = {
    MeshHandle::RemoteMachineGunBarrels,
    machineGunBarrelVertices,
    materialMeshBounds(machineGunBarrelVertices),
  };
  static const MaterialMeshAsset rocketLauncherBody = {
    MeshHandle::RemoteRocketLauncherBody,
    rocketLauncherBodyVertices,
    materialMeshBounds(rocketLauncherBodyVertices),
  };
  static const MaterialMeshAsset rocketLauncherRecoil = {
    MeshHandle::RemoteRocketLauncherRecoil,
    rocketLauncherRecoilVertices,
    materialMeshBounds(rocketLauncherRecoilVertices),
  };
  static const MaterialMeshAsset rocketLauncherLatch = {
    MeshHandle::RemoteRocketLauncherLatch,
    rocketLauncherLatchVertices,
    materialMeshBounds(rocketLauncherLatchVertices),
  };
  static const MaterialMeshAsset freezeGunBody = {
    MeshHandle::RemoteFreezeGunBody,
    freezeGunBodyVertices,
    materialMeshBounds(freezeGunBodyVertices),
  };
  static const MaterialMeshAsset freezeGunFocus = {
    MeshHandle::RemoteFreezeGunFocus,
    freezeGunFocusVertices,
    materialMeshBounds(freezeGunFocusVertices),
  };
  static const MaterialMeshAsset freezeGunCoolant = {
    MeshHandle::RemoteFreezeGunCoolant,
    freezeGunCoolantVertices,
    materialMeshBounds(freezeGunCoolantVertices),
  };
  static const MaterialMeshAsset plasmaGunBody = {
    MeshHandle::RemotePlasmaGunBody,
    plasmaGunBodyVertices,
    materialMeshBounds(plasmaGunBodyVertices),
  };
  static const MaterialMeshAsset plasmaGunProngs = {
    MeshHandle::RemotePlasmaGunProngs,
    plasmaGunProngVertices,
    materialMeshBounds(plasmaGunProngVertices),
  };
  static const MaterialMeshAsset plasmaGunCore = {
    MeshHandle::RemotePlasmaGunCore,
    plasmaGunCoreVertices,
    materialMeshBounds(plasmaGunCoreVertices),
  };
  if (handle == MeshHandle::RemoteMachineGunBody) {
    return &machineGunBody;
  }
  if (handle == MeshHandle::RemoteMachineGunBarrels) {
    return &machineGunBarrels;
  }
  if (handle == MeshHandle::RemoteRevolverBody) {
    return &body;
  }
  if (handle == MeshHandle::RemoteRevolverCylinder) {
    return &cylinder;
  }
  if (handle == MeshHandle::RemoteRocketLauncherBody) {
    return &rocketLauncherBody;
  }
  if (handle == MeshHandle::RemoteRocketLauncherRecoil) {
    return &rocketLauncherRecoil;
  }
  if (handle == MeshHandle::RemoteRocketLauncherLatch) {
    return &rocketLauncherLatch;
  }
  if (handle == MeshHandle::RemoteFreezeGunBody) {
    return &freezeGunBody;
  }
  if (handle == MeshHandle::RemoteFreezeGunFocus) {
    return &freezeGunFocus;
  }
  if (handle == MeshHandle::RemoteFreezeGunCoolant) {
    return &freezeGunCoolant;
  }
  if (handle == MeshHandle::RemotePlasmaGunBody) {
    return &plasmaGunBody;
  }
  if (handle == MeshHandle::RemotePlasmaGunProngs) {
    return &plasmaGunProngs;
  }
  if (handle == MeshHandle::RemotePlasmaGunCore) {
    return &plasmaGunCore;
  }
  return nullptr;
}

MeshHandle remoteWeaponMeshHandle(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun:
    return MeshHandle::RemoteMachineGunBody;
  case Weapon::Shotgun:
    return MeshHandle::RemoteShotgun;
  case Weapon::GrenadeLauncher:
    return MeshHandle::RemoteGrenadeLauncher;
  case Weapon::RocketLauncher:
    return MeshHandle::RemoteRocketLauncherBody;
  case Weapon::LightningGun:
    return MeshHandle::RemoteLightningGun;
  case Weapon::FreezeGun:
    return MeshHandle::RemoteFreezeGunBody;
  case Weapon::Railgun:
    return MeshHandle::RemoteRailgun;
  case Weapon::PlasmaGun:
    return MeshHandle::RemotePlasmaGunBody;
  case Weapon::Revolver:
    return MeshHandle::RemoteRevolverBody;
  }
  return MeshHandle::Invalid;
}

Vec3 machineGunBarrelPivot() {
  return kMachineGunBarrelPivot;
}

Vec3 machineGunMuzzleSocket() {
  return kMachineGunMuzzleSocket;
}

Vec3 plasmaGunMuzzleSocket() {
  return kPlasmaGunMuzzleSocket;
}

Vec3 plasmaGunGripSocket() {
  return kPlasmaGunGripSocket;
}

Vec3 firstPersonPlasmaGunMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
) {
  WeaponModelFrame frame = firstPersonWeaponModelFrame(
    player,
    settings.weaponPosition,
    settings.viewModelPresentation
  );
  frame.scale *= 0.88F;
  frame.hand -= frame.basis.forward * 0.06F;
  return weaponLocalPoint(
    frame,
    kPlasmaGunMuzzleSocket.x,
    kPlasmaGunMuzzleSocket.y,
    kPlasmaGunMuzzleSocket.z
  );
}

Vec3 firstPersonMachineGunMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
) {
  WeaponModelFrame frame = firstPersonWeaponModelFrame(
    player,
    settings.weaponPosition,
    settings.viewModelPresentation
  );
  frame.hand -= frame.basis.forward * 0.10F;
  frame = machineGunFiringFrame(frame, settings);
  const Vec3 muzzle = kMachineGunMuzzleSocket * (1.0F / 0.78F);
  return weaponLocalPoint(frame, muzzle.x, muzzle.y, muzzle.z);
}

Vec3 revolverMuzzleSocket() {
  return kRevolverMuzzleSocket;
}

Vec3 firstPersonRevolverMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
) {
  WeaponModelFrame frame = firstPersonWeaponModelFrame(
    player,
    settings.weaponPosition,
    settings.viewModelPresentation
  );
  frame.hand += frame.basis.forward * 0.16F;
  frame.scale *= 0.80F;
  frame = revolverRecoilFrame(frame, settings.revolverRecoilAmount);
  return weaponLocalPoint(
    frame,
    kRevolverMuzzleSocket.x,
    kRevolverMuzzleSocket.y,
    kRevolverMuzzleSocket.z
  );
}

Vec3 rocketLauncherMuzzleSocket() {
  return kRocketLauncherMuzzleSocket;
}

Vec3 rocketLauncherGripSocket() {
  return kRocketLauncherGripSocket;
}

Vec3 firstPersonRocketLauncherMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
) {
  WeaponModelFrame frame = firstPersonWeaponModelFrame(
    player,
    settings.weaponPosition,
    settings.viewModelPresentation
  );
  frame.scale *= kRocketLauncherViewModelScale;
  frame.hand += frame.basis.forward * kRocketLauncherViewModelForwardOffset;
  frame.hand += frame.basis.up * kRocketLauncherViewModelUpOffset;
  frame = rocketLauncherGripAlignedFrame(frame);
  frame = rocketLauncherRecoilFrame(frame, settings.rocketLauncherRecoilAmount);
  return weaponLocalPoint(
    frame,
    kRocketLauncherMuzzleSocket.x -
      0.052F * settings.rocketLauncherMechanicalAmount,
    kRocketLauncherMuzzleSocket.y,
    kRocketLauncherMuzzleSocket.z
  );
}

Vec3 firstPersonFreezeGunMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
) {
  WeaponModelFrame frame = firstPersonWeaponModelFrame(
    player,
    settings.weaponPosition,
    settings.viewModelPresentation
  );
  frame.scale *= 0.82F;
  frame.hand -= frame.basis.forward * 0.08F;
  frame = freezeGunViewModelFrame(frame, settings.weaponPosition);
  return weaponLocalPoint(
    frame,
    kFreezeGunMuzzleSocket.x,
    kFreezeGunMuzzleSocket.y,
    kFreezeGunMuzzleSocket.z
  );
}

const BillboardAsset* billboardAsset(BillboardHandle handle) {
  switch (handle) {
  case BillboardHandle::PlasmaGlow:
    return &kPlasmaGlowAsset;
  case BillboardHandle::RocketFlame:
    return &kRocketFlameAsset;
  case BillboardHandle::ExplosionFlash:
    return &kExplosionFlashAsset;
  case BillboardHandle::ExplosionHalo:
    return &kExplosionHaloAsset;
  case BillboardHandle::Invalid:
    break;
  }
  return nullptr;
}

const ProjectileVisualDescriptor* projectileVisualDescriptor(
  ProjectileVisualType type
) {
  switch (type) {
  case ProjectileVisualType::Plasma:
    return &kPlasmaProjectileVisual;
  case ProjectileVisualType::Rocket:
    return &kRocketProjectileVisual;
  case ProjectileVisualType::Grenade:
    return &kGrenadeProjectileVisual;
  }
  return nullptr;
}

ProjectileVisualType projectileVisualTypeForWeapon(Weapon weapon) {
  switch (weapon) {
  case Weapon::PlasmaGun:
    return ProjectileVisualType::Plasma;
  case Weapon::GrenadeLauncher:
    return ProjectileVisualType::Grenade;
  case Weapon::RocketLauncher:
  case Weapon::LightningGun:
  case Weapon::Railgun:
  case Weapon::MachineGun:
  case Weapon::Shotgun:
  case Weapon::FreezeGun:
  case Weapon::Revolver:
  default:
    return ProjectileVisualType::Rocket;
  }
}

void addRemoteWeaponInstance(
  Scene3D& scene,
  const PlayerState& player,
  Weapon weapon,
  bool leanEnabled,
  float leanScale,
  float machineGunBarrelRotationRadians,
  float rocketLauncherMechanicalAmount,
  float freezeGunFiringAmount,
  float freezeGunCoolantPulse,
  float freezeGunVibrationPhaseRadians,
  float plasmaGunContainmentAmount
) {
  WeaponModelFrame frame = weaponModelFrame(player, leanEnabled, leanScale);
  frame.scale *= thirdPersonWeaponVisualScale(weapon);
  if (weapon == Weapon::Revolver) {
    frame = revolverGripAlignedFrame(frame);
    appendRevolverInstances(scene, frame, RenderPass::OpaqueWorld, 0.0F);
    scene.remoteWeaponStats.instancesSubmitted += 2U;
    return;
  }
  if (weapon == Weapon::MachineGun) {
    appendMachineGunInstances(
      scene,
      frame,
      RenderPass::OpaqueWorld,
      machineGunBarrelRotationRadians
    );
    scene.remoteWeaponStats.instancesSubmitted += 2U;
    return;
  }
  if (weapon == Weapon::RocketLauncher) {
    frame = rocketLauncherGripAlignedFrame(frame);
    appendRocketLauncherInstances(
      scene,
      frame,
      RenderPass::OpaqueWorld,
      rocketLauncherMechanicalAmount
    );
    scene.remoteWeaponStats.instancesSubmitted += 3U;
    return;
  }
  if (weapon == Weapon::FreezeGun) {
    appendFreezeGunInstances(
      scene,
      frame,
      RenderPass::OpaqueWorld,
      freezeGunFiringAmount,
      freezeGunCoolantPulse,
      freezeGunVibrationPhaseRadians
    );
    scene.remoteWeaponStats.instancesSubmitted += 3U;
    return;
  }
  if (weapon == Weapon::PlasmaGun) {
    frame = plasmaGunGripAlignedFrame(frame);
    appendPlasmaGunInstances(
      scene,
      frame,
      RenderPass::OpaqueWorld,
      plasmaGunContainmentAmount
    );
    scene.remoteWeaponStats.instancesSubmitted += 3U;
    return;
  }
  MeshHandle mesh = remoteWeaponMeshHandle(weapon);
  if (mesh == MeshHandle::Invalid) {
    return;
  }
  appendStaticMeshInstance(
    scene,
    weaponMeshInstance(mesh, RenderPass::OpaqueWorld, frame, {255, 255, 255, 255})
  );
  ++scene.remoteWeaponStats.instancesSubmitted;
}

[[nodiscard]] std::vector<Vertex3D> proceduralWeaponVertices(Weapon weapon) {
  Scene3D meshScene;
  WeaponModelFrame frame;
  frame.basis.forward = {1.0F, 0.0F, 0.0F};
  frame.basis.right = {0.0F, 1.0F, 0.0F};
  frame.basis.up = {0.0F, 0.0F, 1.0F};
  frame.hand = {};
  frame.scale = 1.0F;
  switch (weapon) {
  case Weapon::LightningGun:
  case Weapon::FreezeGun:
    addLightningGunModel(meshScene, frame);
    break;
  case Weapon::GrenadeLauncher:
    addGrenadeLauncherModel(meshScene, frame);
    break;
  case Weapon::RocketLauncher:
    addRocketLauncherModel(meshScene, frame);
    break;
  case Weapon::Railgun:
    addRailgunModel(meshScene, frame);
    break;
  case Weapon::MachineGun:
  case Weapon::Shotgun:
  case Weapon::PlasmaGun:
  case Weapon::Revolver:
    break;
  }
  return meshScene.vertices;
}

namespace {

[[nodiscard]] bool sameSimpleBatchKey(
  const SimpleRenderBatch& batch,
  const SimpleRenderInstance& instance
) {
  return batch.mesh == instance.mesh &&
    batch.billboard == instance.billboard &&
    batch.pass == instance.pass;
}

void appendSimpleInstance(Scene3D& scene, const SimpleRenderInstance& instance) {
  scene.simpleInstances.push_back(instance);
}

[[nodiscard]] bool finiteVec3(Vec3 value) {
  return std::isfinite(value.x) &&
    std::isfinite(value.y) &&
    std::isfinite(value.z);
}

[[nodiscard]] StaticMeshInstance playerBoxMeshInstance(
  Vec3 center,
  Vec3 halfExtents,
  Vec3 forward,
  Vec3 right,
  Vec3 up,
  RenderColor color,
  PlayerBodyPartType bodyPart,
  std::uint8_t playerIndex,
  OutlineState outlineState,
  bool outlined
) {
  const Vec3 forwardColumn = forward * (halfExtents.x * 2.0F);
  const Vec3 rightColumn = right * (halfExtents.y * 2.0F);
  const Vec3 upColumn = up * (halfExtents.z * 2.0F);
  return {
    MeshHandle::PlayerBoxCube,
    RenderPass::OpaqueWorld,
    {forwardColumn.x, rightColumn.x, upColumn.x},
    {forwardColumn.y, rightColumn.y, upColumn.y},
    {forwardColumn.z, rightColumn.z, upColumn.z},
    center,
    color,
    {center, length(halfExtents)},
    bodyPart,
    playerIndex,
    outlineState,
    true,
    outlined,
  };
}

void addPlayerBoxInstances(
  Scene3D& scene,
  const PlayerState& player,
  RenderColor color,
  bool leanEnabled,
  float leanScale,
  std::uint8_t playerIndex,
  OutlineState outlineState,
  bool outlined
) {
  forEachPlayerModelPart(
    player,
    leanEnabled,
    leanScale,
    0.0F,
    [&](Vec3 center,
        Vec3 halfExtents,
        Vec3 forward,
        Vec3 right,
        Vec3 up,
        PlayerBodyPartType bodyPart) {
      if (
        !finiteVec3(center) ||
        !finiteVec3(halfExtents) ||
        !finiteVec3(forward) ||
        !finiteVec3(right) ||
        !finiteVec3(up) ||
        halfExtents.x <= 0.0F ||
        halfExtents.y <= 0.0F ||
        halfExtents.z <= 0.0F
      ) {
        return;
      }
      appendStaticMeshInstance(
        scene,
        playerBoxMeshInstance(
          center,
          halfExtents,
          forward,
          right,
          up,
          color,
          bodyPart,
          playerIndex,
          outlineState,
          outlined
        )
      );
      ++scene.playerBoxStats.instancesSubmitted;
    }
  );
}

[[nodiscard]] float projectileVelocityYaw(Vec3 velocity) {
  if (std::fabs(velocity.x) <= 0.0001F && std::fabs(velocity.y) <= 0.0001F) {
    return 0.0F;
  }
  const float yaw = std::atan2(velocity.y, velocity.x);
  return std::isfinite(yaw) ? yaw : 0.0F;
}

[[nodiscard]] float projectileVelocityPitch(Vec3 velocity) {
  const float horizontalSpeed = std::hypot(velocity.x, velocity.y);
  if (horizontalSpeed <= 0.0001F && std::fabs(velocity.z) <= 0.0001F) {
    return 0.0F;
  }
  const float pitch = std::atan2(velocity.z, horizontalSpeed);
  return std::isfinite(pitch) ? pitch : 0.0F;
}

[[nodiscard]] Vec3 projectileVelocityForward(Vec3 velocity) {
  const float speed = length(velocity);
  if (speed <= 0.0001F || !std::isfinite(speed)) {
    return yawForward(projectileVelocityYaw(velocity));
  }
  return velocity * (1.0F / speed);
}

[[nodiscard]] float projectileRotationRadians(
  const RocketProjectileSnapshot& projectile,
  std::size_t projectileIndex
) {
  const float yaw = projectileVelocityYaw(projectile.velocity);
  if (projectile.weapon != Weapon::GrenadeLauncher) {
    return yaw;
  }
  const float velocityPhase =
    projectile.position.x * 0.73F +
    projectile.position.y * 1.17F +
    projectile.position.z * 0.41F +
    length(projectile.velocity) * 0.019F +
    static_cast<float>(projectileIndex) * 0.67F;
  const float rotation = yaw + std::fmod(velocityPhase, kTwoPi);
  return std::isfinite(rotation) ? rotation : yaw;
}

void countProjectileCoreInstance(
  ProjectileRenderStats& stats,
  ProjectileVisualType type
) {
  ++stats.projectileCoreInstances;
  switch (type) {
  case ProjectileVisualType::Plasma:
    ++stats.plasmaInstances;
    break;
  case ProjectileVisualType::Rocket:
    ++stats.rocketInstances;
    break;
  case ProjectileVisualType::Grenade:
    ++stats.grenadeInstances;
    break;
  }
}

void addTransientTracerGeometry(
  Scene3D& scene,
  const TransientTracer& tracer,
  Vec3 direction,
  float tracerLength,
  RenderColor color
) {
  Vec3 right = normalize(cross(direction, {0.0F, 0.0F, 1.0F}));
  if (length(right) <= 0.0001F) {
    right = {1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(right, direction));
  const float width = std::max(0.002F, tracer.width);
  for (const Vertex3D& local : kTracerBeamMeshVertices) {
    scene.translucentVertices.push_back({
      tracer.start +
        direction * (local.position.x * tracerLength) +
        right * (local.position.y * width) +
        up * (local.position.z * width),
      color,
      local.u,
      local.v,
      local.materialId,
    });
  }
}

void addTransientTracerInstances(
  Scene3D& scene,
  std::span<const TransientTracer> tracers,
  const RenderSettings& settings
) {
  scene.transientVfxStats.activeEffects +=
    static_cast<std::uint32_t>(tracers.size());
  for (const TransientTracer& tracer : tracers) {
    if (tracer.style == TracerStyle::Shotgun) {
      ++scene.transientVfxStats.activeShotgunTracers;
    } else if (tracer.style == TracerStyle::MachineGunMuzzleFlash) {
      ++scene.transientVfxStats.activeMachineGunMuzzleFlashes;
    } else if (tracer.style == TracerStyle::RevolverMuzzleFlash) {
      ++scene.transientVfxStats.activeRevolverMuzzleFlashes;
    } else if (tracer.style == TracerStyle::RocketLauncherMuzzleFlash) {
      ++scene.transientVfxStats.activeRocketLauncherMuzzleFlashes;
    } else {
      ++scene.transientVfxStats.activeMachineGunTracers;
    }
    ++scene.transientVfxStats.tracerCandidates;
    const Vec3 delta = tracer.end - tracer.start;
    const float tracerLength = length(delta);
    if (tracerLength <= 0.001F || !std::isfinite(tracerLength)) {
      continue;
    }
    const Vec3 direction = delta * (1.0F / tracerLength);
    const Vec3 center = (tracer.start + tracer.end) * 0.5F;
    const float radius = tracerLength * 0.5F + std::max(0.01F, tracer.width);
    if (
      settings.frustumCullRemotePlayers &&
      !sphereIntersectsPerspectiveFrustum(scene.camera, center, radius)
    ) {
      ++scene.transientVfxStats.tracerFrustumCulled;
      continue;
    }
    const float fade = std::clamp(
      1.0F - tracer.ageSeconds / std::max(0.001F, tracer.lifetimeSeconds),
      0.0F,
      1.0F
    );
    RenderColor color = tracer.color;
    color.alpha = static_cast<std::uint8_t>(std::clamp(
      static_cast<float>(color.alpha) * fade,
      0.0F,
      255.0F
    ));
    addTransientTracerGeometry(scene, tracer, direction, tracerLength, color);
    ++scene.transientVfxStats.tracerInstancesSubmitted;
    if (
      tracer.style == TracerStyle::MachineGunMuzzleFlash ||
      tracer.style == TracerStyle::RevolverMuzzleFlash ||
      tracer.style == TracerStyle::RocketLauncherMuzzleFlash
    ) {
      const float styleScale = tracer.style == TracerStyle::RocketLauncherMuzzleFlash
        ? 4.0F
        : (tracer.style == TracerStyle::RevolverMuzzleFlash ? 3.4F : 2.8F);
      const float flashScale = std::max(0.002F, tracer.width) * styleScale;
      appendSimpleInstance(
        scene,
        {
          MeshHandle::Invalid,
          BillboardHandle::ExplosionFlash,
          RenderPass::AdditiveGlow,
          tracer.start,
          {flashScale, flashScale, flashScale},
          static_cast<float>(tracer.seed & 15U) * (kTwoPi / 16.0F),
          0.0F,
          color,
          fade,
          {tracer.start, flashScale},
        }
      );
      ++scene.transientVfxStats.muzzleFlashInstancesSubmitted;
      if (
        tracer.style == TracerStyle::RocketLauncherMuzzleFlash ||
        tracer.style == TracerStyle::RevolverMuzzleFlash
      ) {
        const bool revolver = tracer.style == TracerStyle::RevolverMuzzleFlash;
        RenderColor haloColor = revolver
          ? RenderColor{255, 128, 42, color.alpha}
          : RenderColor{255, 68, 18, color.alpha};
        haloColor.alpha = static_cast<std::uint8_t>(
          static_cast<float>(haloColor.alpha) * 0.55F
        );
        const float haloScale = flashScale * (revolver ? 1.55F : 1.85F);
        appendSimpleInstance(
          scene,
          {
            MeshHandle::Invalid,
            BillboardHandle::ExplosionHalo,
            RenderPass::AdditiveGlow,
            tracer.start,
            {haloScale, haloScale, haloScale},
            static_cast<float>((tracer.seed + 5U) & 15U) * (kTwoPi / 16.0F),
            0.0F,
            haloColor,
            fade,
            {tracer.start, haloScale},
          }
        );
        ++scene.transientVfxStats.muzzleFlashInstancesSubmitted;
      }
    }
    scene.transientVfxStats.tracerInstanceUploadBytes +=
      static_cast<std::uint32_t>(
        kTracerBeamMeshVertices.size() * kStaticMeshVertexUploadBytes
      );
  }
}

[[nodiscard]] float effectNormalizedAge(const TransientEffect& effect) {
  if (
    effect.lifetimeSeconds <= 0.001F ||
    !std::isfinite(effect.ageSeconds) ||
    !std::isfinite(effect.lifetimeSeconds)
  ) {
    return 1.0F;
  }
  return std::clamp(effect.ageSeconds / effect.lifetimeSeconds, 0.0F, 1.0F);
}

[[nodiscard]] bool effectUsesCoreMesh(TransientEffectType type) {
  switch (type) {
  case TransientEffectType::RocketExplosionCore:
  case TransientEffectType::PlasmaExplosionCore:
  case TransientEffectType::GrenadeExplosionCore:
    return true;
  case TransientEffectType::RocketExplosionFlash:
  case TransientEffectType::RocketExplosionHalo:
  case TransientEffectType::PlasmaExplosionFlash:
  case TransientEffectType::PlasmaExplosionHalo:
  case TransientEffectType::GrenadeExplosionFlash:
    return false;
  }
  return false;
}

[[nodiscard]] BillboardHandle effectBillboard(TransientEffectType type) {
  switch (type) {
  case TransientEffectType::RocketExplosionFlash:
  case TransientEffectType::PlasmaExplosionFlash:
  case TransientEffectType::GrenadeExplosionFlash:
    return BillboardHandle::ExplosionFlash;
  case TransientEffectType::RocketExplosionHalo:
  case TransientEffectType::PlasmaExplosionHalo:
    return BillboardHandle::ExplosionHalo;
  case TransientEffectType::RocketExplosionCore:
  case TransientEffectType::PlasmaExplosionCore:
  case TransientEffectType::GrenadeExplosionCore:
    break;
  }
  return BillboardHandle::Invalid;
}

void addTransientEffectInstances(
  Scene3D& scene,
  std::span<const TransientEffect> effects,
  const RenderSettings& settings
) {
  scene.transientVfxStats.activeEffects += static_cast<std::uint32_t>(effects.size());
  scene.transientVfxStats.activeExplosionEffects =
    static_cast<std::uint32_t>(effects.size());
  for (const TransientEffect& effect : effects) {
    ++scene.transientVfxStats.explosionCandidates;
    if (
      !std::isfinite(effect.position.x) ||
      !std::isfinite(effect.position.y) ||
      !std::isfinite(effect.position.z)
    ) {
      continue;
    }
    const float t = effectNormalizedAge(effect);
    const float initialScale = std::isfinite(effect.initialScale)
      ? std::clamp(effect.initialScale, 0.01F, 8.0F)
      : 0.1F;
    const float finalScale = std::isfinite(effect.finalScale)
      ? std::clamp(effect.finalScale, 0.01F, 8.0F)
      : initialScale;
    const float scale = initialScale + (finalScale - initialScale) * t;
    const float fade = (1.0F - t) * (1.0F - t);
    if (scale <= 0.0F || fade <= 0.0F || !std::isfinite(scale)) {
      continue;
    }
    if (
      settings.frustumCullRemotePlayers &&
      !sphereIntersectsPerspectiveFrustum(scene.camera, effect.position, scale)
    ) {
      ++scene.transientVfxStats.explosionFrustumCulled;
      continue;
    }
    RenderColor color = effect.color;
    color.alpha = static_cast<std::uint8_t>(std::clamp(
      static_cast<float>(color.alpha) * fade,
      0.0F,
      255.0F
    ));
    const bool coreMesh = effectUsesCoreMesh(effect.type);
    appendSimpleInstance(
      scene,
      {
        coreMesh ? MeshHandle::ExplosionCore : MeshHandle::Invalid,
        coreMesh ? BillboardHandle::Invalid : effectBillboard(effect.type),
        coreMesh ? RenderPass::OpaqueWorld : RenderPass::AdditiveGlow,
        effect.position,
        {scale, scale, scale},
        static_cast<float>((effect.seed * 2654435761U) & 1023U) *
          (kTwoPi / 1024.0F),
        0.0F,
        color,
        t,
        {effect.position, scale},
      }
    );
    ++scene.transientVfxStats.explosionInstancesSubmitted;
  }
}

void finalizeStaticMeshBatches(Scene3D& scene) {
  // Stable ordering groups compatible draw state while preserving submission
  // order inside a mesh/pass group for deterministic presentation and diagnostics.
  std::stable_sort(
    scene.staticMeshInstances.begin(),
    scene.staticMeshInstances.end(),
    [](const StaticMeshInstance& lhs, const StaticMeshInstance& rhs) {
      if (lhs.pass != rhs.pass) {
        return static_cast<int>(lhs.pass) < static_cast<int>(rhs.pass);
      }
      return static_cast<std::uint16_t>(lhs.mesh) <
        static_cast<std::uint16_t>(rhs.mesh);
    }
  );
  scene.staticMeshBatches.clear();
  for (std::uint32_t index = 0;
       index < static_cast<std::uint32_t>(scene.staticMeshInstances.size());
       ++index) {
    const StaticMeshInstance& instance = scene.staticMeshInstances[index];
    if (
      !scene.staticMeshBatches.empty() &&
      scene.staticMeshBatches.back().mesh == instance.mesh &&
      scene.staticMeshBatches.back().pass == instance.pass
    ) {
      ++scene.staticMeshBatches.back().instanceCount;
      continue;
    }
    scene.staticMeshBatches.push_back({
      instance.mesh,
      instance.pass,
      index,
      1U,
    });
  }
  scene.remoteWeaponStats.instanceUploadBytes =
    scene.remoteWeaponStats.instancesSubmitted * kStaticMeshInstanceUploadBytes;
  scene.playerBoxStats.instanceUploadBytes =
    scene.playerBoxStats.instancesSubmitted * kStaticMeshInstanceUploadBytes;
  scene.playerBoxStats.sharedCubeStaticGpuBytes =
    static_cast<std::uint32_t>(
      kPlayerBoxCubeMeshVertices.size() * kStaticMeshVertexUploadBytes
    );
  for (const StaticMeshBatch& batch : scene.staticMeshBatches) {
    if (batch.instanceCount == 0U) {
      continue;
    }
    if (batch.pass != RenderPass::OpaqueWorld) {
      continue;
    }
    if (batch.mesh == MeshHandle::PlayerBoxCube) {
      ++scene.playerBoxStats.opaqueBatches;
      ++scene.playerBoxStats.opaqueDrawCalls;
    } else if (
      batch.mesh == MeshHandle::RemoteMachineGunBody ||
      batch.mesh == MeshHandle::RemoteMachineGunBarrels ||
      batch.mesh == MeshHandle::RemoteShotgun ||
      batch.mesh == MeshHandle::RemoteGrenadeLauncher ||
      batch.mesh == MeshHandle::RemoteRocketLauncherBody ||
      batch.mesh == MeshHandle::RemoteRocketLauncherRecoil ||
      batch.mesh == MeshHandle::RemoteRocketLauncherLatch ||
      batch.mesh == MeshHandle::RemoteLightningGun ||
      batch.mesh == MeshHandle::RemoteRailgun ||
      batch.mesh == MeshHandle::RemotePlasmaGunBody ||
      batch.mesh == MeshHandle::RemotePlasmaGunProngs ||
      batch.mesh == MeshHandle::RemotePlasmaGunCore ||
      batch.mesh == MeshHandle::RemoteRevolverBody ||
      batch.mesh == MeshHandle::RemoteRevolverCylinder
    ) {
      ++scene.remoteWeaponStats.batches;
      ++scene.remoteWeaponStats.drawCalls;
    }
  }

  std::uint32_t runFirst = 0;
  std::uint32_t runCount = 0;
  std::uint8_t runPlayerIndex = 0;
  OutlineState runState = {};
  const auto flushRun = [&]() {
    if (runCount == 0U) {
      return;
    }
    scene.outlineMaskDraws.push_back({
      0U,
      0U,
      runState,
      MeshHandle::PlayerBoxCube,
      runFirst,
      runCount,
    });
    ++scene.playerOutlinesBuilt;
    ++scene.outlinedPlayers;
    ++scene.playerBoxStats.outlineMaskBatches;
    ++scene.playerBoxStats.outlineMaskDrawCalls;
    runCount = 0;
  };
  for (
    std::uint32_t index = 0;
    index < static_cast<std::uint32_t>(scene.staticMeshInstances.size());
    ++index
  ) {
    // Outline instances can share a mask draw only while their player identity
    // and complete outline state form one contiguous run in the sorted buffer.
    const StaticMeshInstance& instance = scene.staticMeshInstances[index];
    if (
      !instance.playerBoxBody ||
      !instance.playerBoxOutlined ||
      instance.mesh != MeshHandle::PlayerBoxCube ||
      instance.pass != RenderPass::OpaqueWorld
    ) {
      flushRun();
      continue;
    }
    if (
      runCount == 0U ||
      (
        instance.playerIndex == runPlayerIndex &&
        instance.outlineState.group == runState.group &&
        instance.outlineState.visibility == runState.visibility &&
        instance.outlineState.widthPixels == runState.widthPixels &&
        instance.outlineState.alpha == runState.alpha &&
        instance.outlineState.pulse == runState.pulse
      )
    ) {
      if (runCount == 0U) {
        runFirst = index;
        runPlayerIndex = instance.playerIndex;
        runState = instance.outlineState;
      }
      ++runCount;
    } else {
      flushRun();
      runFirst = index;
      runPlayerIndex = instance.playerIndex;
      runState = instance.outlineState;
      runCount = 1U;
    }
  }
  flushRun();
}

void finalizeGltfPlayerModelBatches(
  Scene3D& scene,
  const GltfSkinnedModel* model
) {
  if (
    model == nullptr ||
    !model->loaded()
  ) {
    return;
  }

  std::uint32_t vertexBytes = 0;
  std::uint32_t indexBytes = 0;
  for (const GltfSkinnedModel::Primitive& primitive : model->primitives()) {
    vertexBytes += static_cast<std::uint32_t>(
      primitive.vertices.size() * kGltfPlayerModelVertexGpuBytes
    );
    indexBytes += static_cast<std::uint32_t>(
      primitive.indices.size() * kGltfPlayerModelIndexGpuBytes
    );
  }
  scene.gltfPlayerModelStats.staticMeshGpuBytes = vertexBytes;
  scene.gltfPlayerModelStats.staticIndexGpuBytes = indexBytes;
  scene.gltfPlayerModelStats.bonePaletteEntriesUploaded =
    static_cast<std::uint32_t>(scene.gltfBonePalette.size());
  scene.gltfPlayerModelStats.poseUploadBytes =
    scene.gltfPlayerModelStats.bonePaletteEntriesUploaded *
    kGltfBonePaletteEntryBytes;
  scene.gltfPlayerModelStats.legacyCpuSkinnedVertexUploadBytes = 0;

  if (scene.gltfPlayerModelInstances.empty()) {
    return;
  }

  std::uint32_t primitiveCount = 0;
  for (const GltfSkinnedModel::Primitive& primitive : model->primitives()) {
    // Geometry is static on the GPU; each primitive is drawn across the shared
    // instance range, with per-instance bone-palette offsets selecting poses.
    if (!primitive.vertices.empty() && !primitive.indices.empty()) {
      scene.gltfPlayerModelBatches.push_back({
        primitiveCount,
        0U,
        static_cast<std::uint32_t>(scene.gltfPlayerModelInstances.size()),
      });
      ++scene.gltfPlayerModelStats.bodyBatches;
      ++scene.gltfPlayerModelStats.bodyDrawCalls;
    }
    ++primitiveCount;
  }

  std::uint32_t runFirst = 0;
  std::uint32_t runCount = 0;
  std::uint8_t runPlayerIndex = 0;
  OutlineState runState = {};
  const auto flushRun = [&]() {
    if (runCount == 0U) {
      return;
    }
    scene.outlineMaskDraws.push_back({
      0U,
      0U,
      runState,
      MeshHandle::Invalid,
      0U,
      0U,
      true,
      runFirst,
      runCount,
    });
    ++scene.playerOutlinesBuilt;
    ++scene.outlinedPlayers;
    ++scene.gltfPlayerModelStats.outlineMaskBatches;
    scene.gltfPlayerModelStats.outlineMaskDrawCalls +=
      scene.gltfPlayerModelStats.bodyDrawCalls;
    runCount = 0;
  };

  for (
    std::uint32_t index = 0;
    index < static_cast<std::uint32_t>(scene.gltfPlayerModelInstances.size());
    ++index
  ) {
    const GltfPlayerModelInstance& instance = scene.gltfPlayerModelInstances[index];
    if (!instance.outlined) {
      flushRun();
      continue;
    }
    if (
      runCount == 0U ||
      (
        instance.playerIndex == runPlayerIndex &&
        instance.outlineState.group == runState.group &&
        instance.outlineState.visibility == runState.visibility &&
        instance.outlineState.widthPixels == runState.widthPixels &&
        instance.outlineState.alpha == runState.alpha &&
        instance.outlineState.pulse == runState.pulse
      )
    ) {
      if (runCount == 0U) {
        runFirst = index;
        runPlayerIndex = instance.playerIndex;
        runState = instance.outlineState;
      }
      ++runCount;
    } else {
      flushRun();
      runFirst = index;
      runPlayerIndex = instance.playerIndex;
      runState = instance.outlineState;
      runCount = 1U;
    }
  }
  flushRun();
}

void addProjectileInstances(
  Scene3D& scene,
  const RocketProjectileSnapshot& projectile,
  std::size_t projectileIndex,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const RenderSettings& settings
) {
  const ProjectileVisualDescriptor* descriptor =
    projectileVisualDescriptor(projectileVisualTypeForWeapon(projectile.weapon));
  if (descriptor == nullptr) {
    return;
  }

  const Vec3 position =
    projectileVisualPosition(projectile, player, remotePlayers, settings);
  const float pulseSeed = static_cast<float>(projectileIndex) * 0.371F;
  const float rotation = projectileRotationRadians(projectile, projectileIndex);
  const float cullRadius =
    std::max(descriptor->coreScale, descriptor->glowScale);
  if (
    settings.frustumCullRemotePlayers &&
    !sphereIntersectsPerspectiveFrustum(scene.camera, position, cullRadius)
  ) {
    // Cull core and glow together using the larger radius so presentation parts
    // cannot pop independently at the edge of the camera frustum.
    ++scene.projectileStats.projectilesFrustumCulled;
    return;
  }

  ++scene.projectileStats.projectilesRendered;
  if (descriptor->coreMesh != MeshHandle::Invalid) {
    appendSimpleInstance(
      scene,
      {
        descriptor->coreMesh,
        BillboardHandle::Invalid,
        RenderPass::OpaqueWorld,
        position,
        {descriptor->coreScale, descriptor->coreScale, descriptor->coreScale},
        rotation,
        descriptor->type == ProjectileVisualType::Rocket
          ? projectileVelocityPitch(projectile.velocity)
          : 0.0F,
        descriptor->coreColor,
        pulseSeed,
        {position, descriptor->coreScale},
      }
    );
    countProjectileCoreInstance(scene.projectileStats, descriptor->type);
  }
  if (descriptor->glowBillboard != BillboardHandle::Invalid) {
    const Vec3 projectileForward = descriptor->type == ProjectileVisualType::Rocket
      ? projectileVelocityForward(projectile.velocity)
      : yawForward(rotation);
    const Vec3 glowPosition = descriptor->type == ProjectileVisualType::Rocket
      ? position - projectileForward * (descriptor->coreScale * 0.9F)
      : position;
    appendSimpleInstance(
      scene,
      {
        MeshHandle::Invalid,
        descriptor->glowBillboard,
        descriptor->usesAdditiveGlow ? RenderPass::AdditiveGlow : RenderPass::TranslucentWorld,
        glowPosition,
        {descriptor->glowScale, descriptor->glowScale, descriptor->glowScale},
        0.0F,
        0.0F,
        descriptor->glowColor,
        pulseSeed,
        {glowPosition, descriptor->glowScale},
      }
    );
    ++scene.projectileStats.projectileGlowInstances;
  }
}

void finalizeProjectileInstanceStats(Scene3D& scene) {
  std::sort(
    scene.simpleInstances.begin(),
    scene.simpleInstances.end(),
    [](const SimpleRenderInstance& lhs, const SimpleRenderInstance& rhs) {
      if (lhs.pass != rhs.pass) {
        return static_cast<int>(lhs.pass) < static_cast<int>(rhs.pass);
      }
      if (lhs.mesh != rhs.mesh) {
        return static_cast<std::uint16_t>(lhs.mesh) < static_cast<std::uint16_t>(rhs.mesh);
      }
      return static_cast<std::uint16_t>(lhs.billboard) <
        static_cast<std::uint16_t>(rhs.billboard);
    }
  );
  scene.simpleBatches.clear();
  for (const SimpleRenderInstance& instance : scene.simpleInstances) {
    const std::uint32_t index =
      static_cast<std::uint32_t>(scene.simpleBatches.empty()
        ? 0U
        : scene.simpleBatches.back().firstInstance + scene.simpleBatches.back().instanceCount);
    if (!scene.simpleBatches.empty() &&
        sameSimpleBatchKey(scene.simpleBatches.back(), instance)) {
      ++scene.simpleBatches.back().instanceCount;
      continue;
    }
    scene.simpleBatches.push_back({
      instance.mesh,
      instance.billboard,
      instance.pass,
      index,
      1U,
    });
  }
  scene.projectileStats.projectileInstanceUploadBytes =
    (
      scene.projectileStats.projectileCoreInstances +
      scene.projectileStats.projectileGlowInstances
    ) * kSimpleInstanceUploadBytes;
  scene.transientVfxStats.tracerInstanceUploadBytes =
    scene.transientVfxStats.tracerInstanceUploadBytes == 0U
      ? scene.transientVfxStats.tracerInstancesSubmitted *
        static_cast<std::uint32_t>(
          kTracerBeamMeshVertices.size() * kStaticMeshVertexUploadBytes
        )
      : scene.transientVfxStats.tracerInstanceUploadBytes;
  if (scene.transientVfxStats.tracerInstancesSubmitted > 0U) {
    scene.transientVfxStats.tracerBatches = 1U;
    scene.transientVfxStats.tracerDrawCalls = 1U;
  }
  scene.transientVfxStats.explosionInstanceUploadBytes =
    scene.transientVfxStats.explosionInstancesSubmitted *
    kSimpleInstanceUploadBytes;
  for (const SimpleRenderBatch& batch : scene.simpleBatches) {
    if (batch.instanceCount == 0U) {
      continue;
    }
    if (batch.mesh != MeshHandle::Invalid) {
      if (
        batch.mesh == MeshHandle::MachineGunTracer ||
        batch.mesh == MeshHandle::ShotgunTracer
      ) {
        if (scene.transientVfxStats.tracerBatches == 0U) {
          ++scene.transientVfxStats.tracerBatches;
          ++scene.transientVfxStats.tracerDrawCalls;
        }
      } else if (batch.mesh == MeshHandle::ExplosionCore) {
        ++scene.transientVfxStats.explosionOpaqueBatches;
        ++scene.transientVfxStats.explosionDrawCalls;
      } else {
        ++scene.projectileStats.projectileMeshDrawCalls;
      }
      if (batch.pass == RenderPass::OpaqueWorld) {
        ++scene.projectileStats.opaqueProjectileBatches;
      }
    }
    if (batch.billboard != BillboardHandle::Invalid) {
      if (
        batch.billboard == BillboardHandle::ExplosionFlash ||
        batch.billboard == BillboardHandle::ExplosionHalo
      ) {
        ++scene.transientVfxStats.explosionAdditiveBatches;
        ++scene.transientVfxStats.explosionDrawCalls;
      } else {
        ++scene.projectileStats.projectileGlowDrawCalls;
        if (batch.pass == RenderPass::AdditiveGlow) {
          ++scene.projectileStats.additiveProjectileBatches;
        }
      }
    }
  }
}

} // namespace

[[nodiscard]] std::array<bool, Arena::kHealthPickupCount> allHealthPickupsAvailable() {
  std::array<bool, Arena::kHealthPickupCount> available = {};
  available.fill(true);
  return available;
}

Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
  std::span<const IcePool> icePools,
  const RenderSettings& settings,
  float cameraVerticalOffset
) {
  return buildPerspectiveScene(
    aspectRatio,
    arena,
    player,
    remotePlayers,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    allHealthPickupsAvailable(),
    transientTracers,
    transientEffects,
    icePools,
    settings,
    cameraVerticalOffset
  );
}

Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const std::array<bool, Arena::kHealthPickupCount>& healthPickupAvailable,
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
  std::span<const IcePool> icePools,
  const RenderSettings& settings,
  float cameraVerticalOffset
) {
  constexpr CollisionBounds defaultBounds = {};
  const float eyeHeight =
    0.65F * (player.bounds.halfHeight / defaultBounds.halfHeight);
  const Vec3 cameraMotion =
    cameraForward(player.viewYawRadians, player.viewPitchRadians) *
      settings.viewModelPresentation.cameraTranslation.x +
    yawRight(player.viewYawRadians) *
      settings.viewModelPresentation.cameraTranslation.y +
    cameraUp(player.viewYawRadians, player.viewPitchRadians) *
      settings.viewModelPresentation.cameraTranslation.z;
  const Vec3 cameraPosition = player.position + cameraMotion +
    Vec3{0.0F, 0.0F, eyeHeight + cameraVerticalOffset};

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
  scene.outlineMaskDraws.reserve(kDuelPlayerCount);
  scene.gltfPlayerModelInstances.reserve(kDuelPlayerCount);
  scene.gltfBonePalette.reserve(kDuelPlayerCount * 64U);
  appendCollisionDebugGeometry(scene, arena, settings.showCollision);
  GltfSkinnedModel::PoseScratch gltfPoseScratch;
  const GltfSkinnedModel* gltfPlayerModel =
    settings.playerModel == 1 ? &duelistMaleModel() : nullptr;
  if (gltfPlayerModel != nullptr && gltfPlayerModel->loaded()) {
    scene.gltfBonePalette.reserve(
      kDuelPlayerCount *
      std::max<std::uint32_t>(1U, gltfPlayerModel->jointCount())
    );
  }

  if (settings.showOwnWeapons) {
    PlayerState viewModelPlayer = player;
    viewModelPlayer.position.z += cameraVerticalOffset;
    viewModelPlayer.position += cameraMotion;
    addFirstPersonWeaponModel(
      scene,
      viewModelPlayer,
      settings.localSelectedWeapon,
      settings
    );
  }

  for (const IcePool& pool : icePools) {
    addIcePoolDisk(scene, pool);
  }

  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    if (!healthPickupAvailable[index]) {
      continue;
    }
    if (
      sphereIntersectsPerspectiveFrustum(
        scene.camera,
        arena.healthPickups[index].position + Vec3{0.0F, 0.0F, 0.2F},
        arena.healthPickups[index].type == HealthPickupType::Large ? 0.55F : 0.42F
      )
    ) {
      addHealthPickup(scene, arena.healthPickups[index]);
    }
  }

  for (std::size_t remoteIndex = 0; remoteIndex < remotePlayers.size(); ++remoteIndex) {
    const RemotePlayerView& remote = remotePlayers[remoteIndex];
    if (!remote.visible) {
      continue;
    }
    ++scene.remoteCandidates;
    if (settings.drawRemoteWeapons) {
      ++scene.remoteWeaponStats.candidates;
    }
    const bool renderVisible =
      !settings.frustumCullRemotePlayers ||
      sphereIntersectsPerspectiveFrustum(
        scene.camera,
        remotePlayerVisualSphereCenter(remote.player),
        remotePlayerVisualSphereRadius(remote.player)
      );
    const bool usePlayerBoxModel =
      settings.drawRemotePlayers &&
      (
        settings.playerModel != 1 ||
        gltfPlayerModel == nullptr ||
        !gltfPlayerModel->loaded()
      );
    const bool useGltfPlayerModel =
      settings.drawRemotePlayers && !usePlayerBoxModel;
    scene.remoteRenderVisible[remoteIndex] = renderVisible;
    if (!renderVisible) {
      ++scene.remoteFrustumCulled;
      if (settings.drawRemotePlayers && usePlayerBoxModel) {
        ++scene.playerBoxStats.culledPlayers;
      }
      if (useGltfPlayerModel) {
        ++scene.gltfPlayerModelStats.frustumCulled;
      }
      if (settings.drawRemoteWeapons) {
        ++scene.remoteWeaponStats.frustumCulled;
      }
      continue;
    }
    ++scene.remoteFrustumVisible;
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
    const OutlineState outlineState = {
      remote.teammate ? OutlineGroup::Teammate : OutlineGroup::Enemy,
      outlineEnabled ? OutlineVisibility::VisibleOnly : OutlineVisibility::None,
      outlineWidth,
      std::clamp(outlineAlpha, 0.0F, 1.0F),
      hitAmount,
    };
    const bool wantsOutline =
      settings.drawPlayerOutlines &&
      outlineEnabled &&
      outlineState.group != OutlineGroup::None &&
      outlineState.visibility != OutlineVisibility::None &&
      outlineState.widthPixels > 0.0F &&
      outlineState.alpha > 0.0F;
    if (
      wantsOutline &&
      usesGeometryPlayerOutlineFallback(settings.playerOutlineStyle) &&
      settings.drawRemotePlayers
    ) {
      ++scene.playerOutlinesBuilt;
      const std::size_t outlineStart = scene.vertices.size();
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
        outlineWidth * kLegacyOutlineWorldUnitsPerPixel
      );
      scene.geometryOutlineDynamicVertices +=
        static_cast<std::uint32_t>(scene.vertices.size() - outlineStart);
      scene.geometryOutlineFallbackUsed = true;
    }
    if (settings.drawRemotePlayers) {
      benchmark::ScopedTiming animationTiming(
        benchmark::TimingSubsystem::Animation
      );
      ++scene.remoteBodyModelsBuilt;
      if (usePlayerBoxModel) {
        ++scene.playerBoxStats.visiblePlayers;
        addPlayerBoxInstances(
          scene,
          remote.player,
          opponentColor,
          remote.teammate
            ? settings.teammateLeanEnabled
            : settings.enemyLeanEnabled,
          remote.teammate ? settings.teammateLeanScale : settings.enemyLeanScale,
          static_cast<std::uint8_t>(remoteIndex),
          outlineState,
          wantsOutline &&
            settings.playerOutlineStyle == PlayerOutlineStyle::ScreenSpace
        );
      } else {
        addGltfPlayerModelInstance(
          scene,
          *gltfPlayerModel,
          remote.player,
          opponentColor,
          remote.teammate
            ? settings.teammateLeanEnabled
            : settings.enemyLeanEnabled,
          remote.teammate ? settings.teammateLeanScale : settings.enemyLeanScale,
          remote.animationTimeSeconds,
          remote.hasPresentation ? &remote.presentation : nullptr,
          static_cast<std::uint8_t>(remoteIndex),
          outlineState,
          wantsOutline &&
            settings.playerOutlineStyle == PlayerOutlineStyle::ScreenSpace,
          gltfPoseScratch
        );
      }
    }
    if (settings.drawRemoteWeapons) {
      ++scene.remoteWeaponModelsBuilt;
      addRemoteWeaponInstance(
        scene,
        remote.player,
        remote.selectedWeapon,
        remote.teammate
          ? settings.teammateLeanEnabled
          : settings.enemyLeanEnabled,
        remote.teammate ? settings.teammateLeanScale : settings.enemyLeanScale,
        remote.machineGunBarrelRotationRadians,
        remote.rocketLauncherMechanicalAmount,
        remote.freezeGunFiringAmount,
        remote.freezeGunCoolantPulse,
        remote.freezeGunVibrationPhaseRadians,
        remote.plasmaGunContainmentAmount
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
    const bool freezeBeam = remote.selectedWeapon == Weapon::FreezeGun;
    const std::uint8_t beamRed = freezeBeam
      ? static_cast<std::uint8_t>(154)
      : (remote.teammate ? settings.teammateBeamRed : settings.enemyBeamRed);
    const std::uint8_t beamGreen = freezeBeam
      ? static_cast<std::uint8_t>(232)
      : (remote.teammate
        ? settings.teammateBeamGreen
        : settings.enemyBeamGreen);
    const std::uint8_t beamBlue = freezeBeam
      ? static_cast<std::uint8_t>(255)
      : (remote.teammate ? settings.teammateBeamBlue : settings.enemyBeamBlue);
    const Vec3 visualStart =
      remoteHitscanMuzzlePosition(remote, remote.selectedWeapon, settings);
    if (freezeBeam) {
      addLayeredFreezeBeam(
        scene,
        visualStart,
        remote.lightningGun.end,
        settings.beamPhaseRadians,
        remote.freezeGunFiringAmount,
        remote.freezeGunActivationFlashAmount
      );
    } else {
      addSegment(
        scene,
        visualStart,
        remote.lightningGun.end,
        std::max(0.015F, beamWidth * (1.0F + pulse * 0.04F) * 0.012F),
        scaleColor({
          beamRed,
          beamGreen,
          beamBlue,
          static_cast<std::uint8_t>(
            std::clamp(beamAlpha, 0.0F, 1.0F) * 255.0F
          ),
        }, brightness)
      );
    }
  }
  for (std::size_t fireIndex = 0; fireIndex < weaponFires.size(); ++fireIndex) {
    const WeaponFireResult& fire = weaponFires[fireIndex];
    if (!fire.fired) {
      continue;
    }
    if (fire.weapon == Weapon::Railgun || fire.weapon == Weapon::Revolver) {
      const Vec3 visualStart =
        fireIndex < remotePlayers.size() && remotePlayers[fireIndex].visible
          ? remoteHitscanMuzzlePosition(
              remotePlayers[fireIndex],
              fire.weapon,
              settings
            )
          : fire.start;
      if (fire.weapon == Weapon::Revolver) {
        const float alpha = fireIndex < settings.revolverTracerAlpha.size()
          ? std::clamp(settings.revolverTracerAlpha[fireIndex], 0.0F, 1.0F)
          : 1.0F;
        RenderColor outer = {244, 182, 82, static_cast<std::uint8_t>(205.0F * alpha)};
        RenderColor core = {255, 246, 210, static_cast<std::uint8_t>(245.0F * alpha)};
        addSegment(scene, visualStart, fire.end, fire.hit ? 0.036F : 0.028F, outer);
        addSegment(scene, visualStart, fire.end, fire.hit ? 0.014F : 0.010F, core);
      } else {
        addSegment(
          scene,
          visualStart,
          fire.end,
          fire.hit ? 0.045F : 0.03F,
          fire.hit
            ? RenderColor{255, 248, 180, 255}
            : RenderColor{128, 230, 255, 235}
        );
      }
    }
  }
  addTransientTracerInstances(scene, transientTracers, settings);
  addTransientEffectInstances(scene, transientEffects, settings);
  for (std::size_t projectileIndex = 0; projectileIndex < rockets.size(); ++projectileIndex) {
    const RocketProjectileSnapshot& projectile = rockets[projectileIndex];
    if (!projectile.active) {
      continue;
    }
    ++scene.projectileStats.projectilesActive;

    addProjectileInstances(
      scene,
      projectile,
      projectileIndex,
      player,
      remotePlayers,
      settings
    );
  }
  (void)rocketExplosions;
  (void)localLightningGun;
  finalizeStaticMeshBatches(scene);
  finalizeGltfPlayerModelBatches(scene, gltfPlayerModel);
  finalizeProjectileInstanceStats(scene);

  return scene;
}

Scene3D buildStaticWorldScene(const Arena& arena) {
  const auto buildStart = std::chrono::steady_clock::now();
  Scene3D scene;
  scene.vertices.reserve(
    512U +
    arena.wallCount * 36U +
    arena.brushCount * ArenaBrush::kMaxFaces * 12U +
    arena.visualWallCount * 36U +
    arena.visualBrushCount * ArenaBrush::kMaxFaces * 12U
  );

  if (arena.renderDefaultFloor) {
    addFloorTreatment(scene, arena);
  }
  addArenaBoundaryWalls(scene, arena);
  addWireBox(scene, arena.min, arena.max, 0.025F, {120, 138, 156, 255});

  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    if (!arena.walls[index].renderable) {
      continue;
    }
    addWallBox(scene, arena, arena.walls[index]);
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    if (!arena.brushes[index].renderable) {
      continue;
    }
    addArenaBrush(scene, arena, arena.brushes[index]);
  }
  // These arrays are deliberately consumed only by static-world rendering;
  // collision debug and authoritative simulation iterate the solid arrays.
  for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
    addWallBox(scene, arena, arena.visualWalls[index]);
  }
  for (std::size_t index = 0; index < arena.visualBrushCount; ++index) {
    addArenaBrush(scene, arena, arena.visualBrushes[index]);
  }

  if (lightDebugEnabled()) {
    const auto buildEnd = std::chrono::steady_clock::now();
    const float buildMs =
      std::chrono::duration<float, std::milli>(buildEnd - buildStart).count();
    std::cerr
      << "LG_DUEL_LIGHT_DEBUG static lighting"
      << " sunEnabled=" << (arena.sunLight.enabled ? "true" : "false")
      << " sunDirection=" << arena.sunLight.direction.x << ','
      << arena.sunLight.direction.y << ',' << arena.sunLight.direction.z
      << " sunIntensity=" << arena.sunLight.intensity
      << " sunColor=" << arena.sunLight.color.x << ','
      << arena.sunLight.color.y << ',' << arena.sunLight.color.z
      << " ambient=" << kStaticLightAmbient
      << " buildMs=" << buildMs
      << '\n';
  }

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
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
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
    0.0F,
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
    allHealthPickupsAvailable(),
    transientTracers,
    transientEffects,
    std::span<const IcePool>{},
    settings
  );
}

Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
  const RenderSettings& settings
) {
  return buildPerspectiveScene(
    aspectRatio,
    arena,
    player,
    remotePlayers,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    transientTracers,
    transientEffects,
    std::span<const IcePool>{},
    settings
  );
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
  std::span<const TransientTracer> transientTracers,
  const RenderSettings& settings
) {
  return buildPerspectiveScene(
    aspectRatio,
    arena,
    player,
    opponent,
    localLightningGun,
    opponentLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    transientTracers,
    std::span<const TransientEffect>{},
    settings
  );
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
  return buildPerspectiveScene(
    aspectRatio,
    arena,
    player,
    opponent,
    localLightningGun,
    opponentLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const TransientTracer>{},
    std::span<const TransientEffect>{},
    settings
  );
}

Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  std::span<const TransientTracer> transientTracers,
  const RenderSettings& settings
) {
  return buildPerspectiveScene(
    aspectRatio,
    arena,
    player,
    remotePlayers,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    allHealthPickupsAvailable(),
    transientTracers,
    std::span<const TransientEffect>{},
    std::span<const IcePool>{},
    settings
  );
}

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
  return buildPerspectiveScene(
    aspectRatio,
    arena,
    player,
    remotePlayers,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    allHealthPickupsAvailable(),
    std::span<const TransientTracer>{},
    std::span<const TransientEffect>{},
    std::span<const IcePool>{},
    settings
  );
}

} // namespace lg
