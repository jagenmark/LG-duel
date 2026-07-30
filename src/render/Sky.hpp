#pragma once

#include "render/Perspective.hpp"
#include "sim/Arena.hpp"

#include <array>
#include <cstddef>

namespace lg {

enum class SkyAssetLoadState : std::uint8_t {
  NotAttempted = 0,
  Loaded = 1,
  Failed = 2,
};

class SkyAssetLoadCache {
public:
  [[nodiscard]] bool shouldAttempt(SkyId sky) const {
    return validIndex(sky) &&
      states_[static_cast<std::size_t>(sky)] ==
        SkyAssetLoadState::NotAttempted;
  }

  void record(SkyId sky, bool loaded) {
    if (validIndex(sky)) {
      states_[static_cast<std::size_t>(sky)] = loaded
        ? SkyAssetLoadState::Loaded
        : SkyAssetLoadState::Failed;
    }
  }

  [[nodiscard]] SkyAssetLoadState state(SkyId sky) const {
    return validIndex(sky)
      ? states_[static_cast<std::size_t>(sky)]
      : SkyAssetLoadState::Failed;
  }

private:
  [[nodiscard]] static constexpr bool validIndex(SkyId sky) {
    const std::size_t index = static_cast<std::size_t>(sky);
    return sky != SkyId::None && index < 3U;
  }

  std::array<SkyAssetLoadState, 3> states_ = {};
};

[[nodiscard]] inline std::uint64_t arenaSkySurfaceFingerprint(
  const Arena& arena
) {
  // The static mesh cache must change when a face starts or stops emitting.
  // Hash active faces only; unused fixed-capacity storage has no meaning.
  std::uint64_t hash = 1469598103934665603ULL;
  const auto add = [&hash](ArenaSurfaceKind kind) {
    hash ^= static_cast<std::uint8_t>(kind);
    hash *= 1099511628211ULL;
  };
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    for (ArenaSurfaceKind kind : arena.walls[index].faceSurfaceKinds) {
      add(kind);
    }
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const ArenaBrush& brush = arena.brushes[index];
    for (std::uint8_t face = 0; face < brush.faceCount; ++face) {
      add(brush.faces[face].surfaceKind);
    }
  }
  for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
    for (ArenaSurfaceKind kind : arena.visualWalls[index].faceSurfaceKinds) {
      add(kind);
    }
  }
  for (std::size_t index = 0; index < arena.visualBrushCount; ++index) {
    const ArenaBrush& brush = arena.visualBrushes[index];
    for (std::uint8_t face = 0; face < brush.faceCount; ++face) {
      add(brush.faces[face].surfaceKind);
    }
  }
  return hash;
}

[[nodiscard]] inline Vec3 skyRayDirection(
  const PerspectiveCamera& camera,
  float normalizedDeviceX,
  float normalizedDeviceY
) {
  // Sky lookup is rotational only. Camera position must never enter this
  // calculation, or ordinary movement would make the sky appear nearby.
  return normalize(
    camera.forward +
    camera.right *
      (normalizedDeviceX * camera.aspectRatio / camera.focalLength) +
    camera.up * (normalizedDeviceY / camera.focalLength)
  );
}

} // namespace lg
