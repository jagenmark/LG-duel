#pragma once

#include "render/DrawList2D.hpp"
#include "render/Perspective.hpp"
#include "render/Renderer.hpp"

#include <vector>

namespace lg {

struct Vertex3D {
  Vec3 position = {};
  RenderColor color = {};
};

struct Scene3D {
  PerspectiveCamera camera = {};
  std::vector<Vertex3D> vertices;
  std::vector<Vertex3D> translucentVertices;
};

[[nodiscard]] Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const PlayerState& opponent,
  const LightningGunResult& localLightningGun,
  const LightningGunResult& opponentLightningGun,
  const RenderSettings& settings
);

} // namespace lg
