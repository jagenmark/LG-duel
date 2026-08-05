#pragma once

#include "render/Scene3D.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

enum class GltfTextureColorSpace {
  Linear,
  Srgb,
};

enum class GltfAlbedoTextureMode : std::uint8_t {
  None,
  Multiply,
  Replace,
};

enum class GltfMaterialManifestStatus {
  NotFound,
  Valid,
  Invalid,
};

struct GltfMaterialTexture {
  std::string path;
  GltfTextureColorSpace colorSpace = GltfTextureColorSpace::Linear;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct GltfMaterialBinding {
  int materialIndex = -1;
  std::string expectedName;
  std::uint8_t flatTintWeight = 0;
  float atlasU = 0.5F;
  float atlasV = 0.5F;
};

struct GltfMaterialMetadata {
  GltfMaterialManifestStatus status = GltfMaterialManifestStatus::NotFound;
  std::string manifestPath;
  std::string diagnostic;
  bool forceOpaque = false;
  bool materialCells = false;
  GltfAlbedoTextureMode albedoMode = GltfAlbedoTextureMode::None;
  std::uint32_t atlasColumns = 0;
  std::uint32_t atlasRows = 0;
  GltfMaterialTexture albedo;
  GltfMaterialTexture packedMask;
  bool albedoFilePresent = false;
  bool packedMaskFilePresent = false;
  std::vector<GltfMaterialBinding> bindings;

  [[nodiscard]] bool valid() const {
    return status == GltfMaterialManifestStatus::Valid;
  }

  [[nodiscard]] bool hasAuthoredTextures() const {
    return valid() && !albedo.path.empty() && !packedMask.path.empty();
  }

  [[nodiscard]] bool authoredTextureFilesAvailable() const {
    return hasAuthoredTextures() && albedoFilePresent && packedMaskFilePresent;
  }
};

struct GltfMaterialQualityPlan {
  bool flatFallback = true;
  bool samplesAlbedo = false;
  bool samplesPackedMask = false;
  bool appliesAuthoredTeamTint = false;
  bool roughnessHighlights = false;
  bool metallicResponse = false;
  bool environmentResponse = false;
};

[[nodiscard]] constexpr GltfMaterialQualityPlan gltfMaterialQualityPlan(
  int materialQuality,
  bool hasAuthoredTextures
) {
  if (materialQuality <= 0 || !hasAuthoredTextures) {
    return {};
  }
  return {
    false,
    true,
    true,
    true,
    true,
    materialQuality >= 2,
    false,
  };
}

[[nodiscard]] constexpr std::uint32_t gltfTextureMipLevels(
  std::uint32_t width,
  std::uint32_t height
) {
  if (width == 0U || height == 0U) {
    return 0U;
  }
  std::uint32_t levels = 0U;
  while (true) {
    ++levels;
    if (width == 1U && height == 1U) {
      return levels;
    }
    if (width > 1U) {
      width /= 2U;
    }
    if (height > 1U) {
      height /= 2U;
    }
  }
}

[[nodiscard]] constexpr std::uint64_t gltfRgbaMipBytes(
  std::uint32_t width,
  std::uint32_t height
) {
  if (width == 0U || height == 0U) {
    return 0U;
  }
  std::uint64_t bytes = 0;
  while (true) {
    bytes += static_cast<std::uint64_t>(width) * height * 4U;
    if (width == 1U && height == 1U) {
      return bytes;
    }
    if (width > 1U) {
      width /= 2U;
    }
    if (height > 1U) {
      height /= 2U;
    }
  }
}

struct GltfMaterialResourcePlan {
  std::uint32_t sharedTextureAllocations = 0;
  std::uint64_t textureBytes = 0;
  std::uint32_t perInstanceTextureAllocations = 0;
  std::uint64_t perFrameTextureUploadBytes = 0;
};

[[nodiscard]] constexpr GltfMaterialResourcePlan gltfMaterialResourcePlan(
  const GltfMaterialMetadata& metadata
) {
  if (!metadata.hasAuthoredTextures()) {
    return {};
  }
  return {
    2U,
    gltfRgbaMipBytes(metadata.albedo.width, metadata.albedo.height) +
      gltfRgbaMipBytes(metadata.packedMask.width, metadata.packedMask.height),
    0U,
    0U,
  };
}

struct SkinnedModelTriangle {
  std::array<Vec3, 3> vertices;
  RenderColor color = {};
  bool tintable = false;
};

enum class SkinnedModelPoseMask {
  FullBody,
  UpperBody,
};

struct SkinnedModelPoseRequest {
  std::string_view animationName;
  float timeSeconds = 0.0F;
  float weight = 1.0F;
  SkinnedModelPoseMask mask = SkinnedModelPoseMask::FullBody;
};

class GltfSkinnedModel {
public:
  struct Matrix4 {
    std::array<float, 16> values = {
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
      0.0F, 0.0F, 0.0F, 1.0F,
    };
  };

  struct GpuVertex {
    Vec3 position = {};
    Vec3 normal = {0.0F, 0.0F, 1.0F};
    float u = 0.0F;
    float v = 0.0F;
    RenderColor color = {};
    std::uint8_t tintWeight = 0;
    std::uint8_t albedoTextureMode = 0;
    std::array<std::uint16_t, 4> joints = {};
    std::array<float, 4> weights = {};
  };

  struct Primitive {
    std::vector<GpuVertex> vertices;
    std::vector<std::uint32_t> indices;
    RenderColor color = {};
    bool tintable = false;
    bool skinned = false;
    int materialIndex = -1;
    float roughnessFactor = 1.0F;
    float metallicFactor = 0.0F;
    Vec3 emissiveFactor = {};
    bool opaque = true;
    GltfModelBounds localBounds = {};
  };

  struct NodePose {
    Vec3 translation = {};
    std::array<float, 4> rotation = {0.0F, 0.0F, 0.0F, 1.0F};
    Vec3 scale = {1.0F, 1.0F, 1.0F};
  };

  struct PoseScratch {
    std::vector<NodePose> sampledNodes;
    std::vector<Matrix4> localMatrices;
    std::vector<Matrix4> globalMatrices;
    std::vector<bool> resolved;
    std::vector<Matrix4> jointMatrices;
  };

  [[nodiscard]] bool load(std::string_view path);
  [[nodiscard]] bool loaded() const;
  [[nodiscard]] std::string_view sourcePath() const;
  [[nodiscard]] const std::vector<std::string>& animationNames() const;
  [[nodiscard]] const std::vector<std::string>& materialNames() const;
  [[nodiscard]] const GltfMaterialMetadata& materialMetadata() const;
  [[nodiscard]] const std::vector<Primitive>& primitives() const;
  [[nodiscard]] GltfModelBounds localBounds() const;
  [[nodiscard]] std::uint32_t jointCount() const;
  [[nodiscard]] bool hasSkin() const;
  [[nodiscard]] bool hasSkinnedPrimitives() const;
  [[nodiscard]] std::vector<SkinnedModelTriangle> triangles(
    const std::vector<SkinnedModelPoseRequest>& poses
  ) const;
  [[nodiscard]] bool appendBonePalette(
    std::span<const SkinnedModelPoseRequest> poses,
    std::vector<std::array<float, 16>>& out,
    PoseScratch& scratch,
    float upperBodyAimPitchRadians = 0.0F
  ) const;
  [[nodiscard]] bool nodeGlobalMatrix(
    std::string_view nodeName,
    const PoseScratch& scratch,
    Matrix4& out
  ) const;
  [[nodiscard]] bool appendBonePalette(
    std::initializer_list<SkinnedModelPoseRequest> poses,
    std::vector<std::array<float, 16>>& out,
    PoseScratch& scratch,
    float upperBodyAimPitchRadians = 0.0F
  ) const {
    return appendBonePalette(
      std::span<const SkinnedModelPoseRequest>(poses.begin(), poses.size()),
      out,
      scratch,
      upperBodyAimPitchRadians
    );
  }

  struct JointVertex {
    Vec3 position = {};
    std::array<std::uint16_t, 4> joints = {};
    std::array<float, 4> weights = {};
  };

  struct SourceTriangle {
    std::array<JointVertex, 3> vertices = {};
    RenderColor color = {};
    bool tintable = false;
  };

  struct Node {
    std::string name;
    int parent = -1;
    int mesh = -1;
    int skin = -1;
    Vec3 translation = {};
    std::array<float, 4> rotation = {0.0F, 0.0F, 0.0F, 1.0F};
    Vec3 scale = {1.0F, 1.0F, 1.0F};
  };

  enum class ChannelPath {
    Translation,
    Rotation,
    Scale,
  };

  enum class Interpolation {
    Linear,
    Step,
  };

  struct AnimationChannel {
    int node = -1;
    ChannelPath path = ChannelPath::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> inputTimes;
    std::vector<Vec3> vec3Outputs;
    std::vector<std::array<float, 4>> rotationOutputs;
  };

  struct Animation {
    std::string name;
    float duration = 0.0F;
    std::vector<AnimationChannel> channels;
  };

private:
  std::vector<std::string> animationNames_;
  std::vector<std::string> materialNames_;
  GltfMaterialMetadata materialMetadata_;
  std::vector<SkinnedModelTriangle> restTriangles_;
  std::vector<SourceTriangle> sourceTriangles_;
  std::vector<Primitive> primitives_;
  std::vector<Node> nodes_;
  std::vector<int> joints_;
  std::vector<Matrix4> inverseBindMatrices_;
  std::vector<Animation> animations_;
  std::string sourcePath_;
  GltfModelBounds localBounds_ = {};
  bool hasSkin_ = false;
  bool hasSkinnedPrimitives_ = false;
  bool loaded_ = false;
};

[[nodiscard]] GltfSkinnedModel& duelistMaleModel();
[[nodiscard]] GltfSkinnedModel& workerPlayerModel();

} // namespace lg
