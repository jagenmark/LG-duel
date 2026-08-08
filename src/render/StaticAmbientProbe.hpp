#pragma once

#include "shared/Math.hpp"
#include "sim/Arena.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lg {

struct StaticAmbientQualitySettings {
  bool enabled = false;
  std::uint8_t surfaceRayCount = 0;
  std::uint8_t probeRayCount = 0;
  std::uint8_t gridWidth = 0;
  std::uint8_t gridDepth = 0;
  std::uint8_t gridHeight = 0;
  float maxDistance = 0.0F;
  float minimumVisibility = 1.0F;
  float surfaceBias = 0.03F;
};

[[nodiscard]] StaticAmbientQualitySettings
staticAmbientQualitySettings(int quality);

// Static world light is stored as an sRGB byte and decoded in the vertex
// shader. Encode the visibility factor before multiplying that byte so the
// decoded ambient term matches the linear dynamic-light path.
[[nodiscard]] float encodedAmbientVisibilityScale(std::uint8_t visibility);

[[nodiscard]] std::uint64_t
visualAmbientOccluderFingerprint(const Arena &arena);

// Inputs that change probe bounds or generated ambient enclosure geometry.
// The renderer folds this into its revision-zero static-world cache key.
[[nodiscard]] std::uint64_t ambientProbeInputFingerprint(const Arena &arena);

struct StaticAmbientBakeStats {
  std::uint32_t raysCast = 0;
  std::uint32_t uniqueSamples = 0;
  std::uint32_t cacheHits = 0;
  std::uint8_t minimumVisibility = 255;
  std::uint8_t maximumVisibility = 0;
};

class StaticAmbientBaker {
public:
  StaticAmbientBaker(const Arena &arena, int quality);

  [[nodiscard]] std::uint8_t sample(Vec3 position, Vec3 normal);
  [[nodiscard]] const StaticAmbientBakeStats &stats() const;

private:
  const Arena *arena_ = nullptr;
  StaticAmbientQualitySettings settings_ = {};
  StaticAmbientBakeStats stats_ = {};
  std::unordered_map<std::uint64_t, std::uint8_t> cache_;
};

struct StaticAmbientProbeGrid {
  Vec3 minimum = {};
  Vec3 maximum = {};
  std::uint8_t width = 0;
  std::uint8_t depth = 0;
  std::uint8_t height = 0;
  std::vector<std::uint8_t> visibility;
  std::uint32_t raysCast = 0;
  std::uint8_t minimumVisibility = 255;
  std::uint8_t maximumVisibility = 255;
  std::uint64_t fingerprint = 0;
  float buildMilliseconds = 0.0F;

  [[nodiscard]] bool enabled() const;
  [[nodiscard]] std::uint32_t byteSize() const;
};

[[nodiscard]] StaticAmbientProbeGrid
bakeStaticAmbientProbeGrid(const Arena &arena, int quality);

[[nodiscard]] std::uint8_t
sampleStaticAmbientProbe(const StaticAmbientProbeGrid &grid, Vec3 position);

} // namespace lg
