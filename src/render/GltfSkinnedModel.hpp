#pragma once

#include "render/Scene3D.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

struct SkinnedModelTriangle {
  std::array<Vec3, 3> vertices;
  RenderColor color = {};
  bool tintable = false;
};

struct SkinnedModelPoseRequest {
  std::string_view animationName;
  float timeSeconds = 0.0F;
  float weight = 1.0F;
};

class GltfSkinnedModel {
public:
  [[nodiscard]] bool load(std::string_view path);
  [[nodiscard]] bool loaded() const;
  [[nodiscard]] const std::vector<std::string>& animationNames() const;
  [[nodiscard]] std::vector<SkinnedModelTriangle> triangles(
    const std::vector<SkinnedModelPoseRequest>& poses
  ) const;

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
    Vec3 translation = {};
    std::array<float, 4> rotation = {0.0F, 0.0F, 0.0F, 1.0F};
    Vec3 scale = {1.0F, 1.0F, 1.0F};
  };

  struct Matrix4 {
    std::array<float, 16> values = {
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
      0.0F, 0.0F, 0.0F, 1.0F,
    };
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
  std::vector<SkinnedModelTriangle> restTriangles_;
  std::vector<SourceTriangle> sourceTriangles_;
  std::vector<Node> nodes_;
  std::vector<int> joints_;
  std::vector<Matrix4> inverseBindMatrices_;
  std::vector<Animation> animations_;
  std::string sourcePath_;
  bool loaded_ = false;
};

[[nodiscard]] GltfSkinnedModel& duelistMaleModel();

} // namespace lg
