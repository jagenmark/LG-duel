#include "render/Sky.hpp"
#include "render/Renderer.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(lg::Vec3 first, lg::Vec3 second, float epsilon = 0.0001F) {
  return
    std::fabs(first.x - second.x) <= epsilon &&
    std::fabs(first.y - second.y) <= epsilon &&
    std::fabs(first.z - second.z) <= epsilon;
}

} // namespace

int main() {
  int failures = 0;

  {
    const lg::RendererFrameDiagnostics diagnostics;
    failures += expect(
      diagnostics.skyDrawCalls == 0 &&
        diagnostics.skyLoadedTextures == 0,
      "sky diagnostics must start at the clear-colour fallback state"
    );
  }

  {
    const lg::PerspectiveCamera first = lg::makePerspectiveCamera(
      {1.0F, 2.0F, 3.0F},
      0.2F,
      -0.1F,
      90.0F,
      16.0F / 9.0F
    );
    const lg::PerspectiveCamera translated = lg::makePerspectiveCamera(
      {101.0F, -42.0F, 900.0F},
      0.2F,
      -0.1F,
      90.0F,
      16.0F / 9.0F
    );
    const lg::PerspectiveCamera rotated = lg::makePerspectiveCamera(
      translated.position,
      0.7F,
      -0.1F,
      90.0F,
      16.0F / 9.0F
    );
    const lg::Vec3 ray = lg::skyRayDirection(first, 0.35F, -0.2F);
    failures += expect(
      nearlyEqual(ray, lg::skyRayDirection(translated, 0.35F, -0.2F)),
      "sky ray must ignore camera translation"
    );
    failures += expect(
      !nearlyEqual(ray, lg::skyRayDirection(rotated, 0.35F, -0.2F)),
      "sky ray must respond to camera rotation"
    );
  }

  {
    lg::SkyAssetLoadCache cache;
    failures += expect(
      !cache.shouldAttempt(lg::SkyId::None),
      "none must never start an asset load"
    );
    failures += expect(
      cache.shouldAttempt(lg::SkyId::Aurora),
      "new sky id should start one load"
    );
    cache.record(lg::SkyId::Aurora, false);
    failures += expect(
      !cache.shouldAttempt(lg::SkyId::Aurora) &&
        cache.state(lg::SkyId::Aurora) ==
          lg::SkyAssetLoadState::Failed,
      "failed sky load must stay cached"
    );
    failures += expect(
      cache.shouldAttempt(lg::SkyId::CrimsonSunset),
      "one failed id must not block another id"
    );
    cache.record(lg::SkyId::CrimsonSunset, true);
    failures += expect(
      !cache.shouldAttempt(lg::SkyId::CrimsonSunset) &&
        cache.state(lg::SkyId::CrimsonSunset) ==
          lg::SkyAssetLoadState::Loaded,
      "successful sky load must stay cached"
    );
  }

  {
    auto arena = std::make_unique<lg::Arena>();
    (void)arena->walls[0];
    (void)arena->visualWalls[0];
    arena->wallCount = 1;
    arena->brushCount = 1;
    arena->brushes[0].faceCount = 1;
    arena->visualWallCount = 1;
    arena->visualBrushCount = 1;
    arena->visualBrushes[0].faceCount = 1;
    const std::uint64_t baseline =
      lg::arenaSkySurfaceFingerprint(*arena);

    arena->walls[0].faceSurfaceKinds[2] = lg::ArenaSurfaceKind::Sky;
    failures += expect(
      lg::arenaSkySurfaceFingerprint(*arena) != baseline,
      "solid wall sky changes must invalidate the static world cache"
    );
    arena->walls[0].faceSurfaceKinds[2] =
      lg::ArenaSurfaceKind::Default;
    arena->brushes[0].faces[0].surfaceKind = lg::ArenaSurfaceKind::Sky;
    failures += expect(
      lg::arenaSkySurfaceFingerprint(*arena) != baseline,
      "solid brush sky changes must invalidate the static world cache"
    );
    arena->brushes[0].faces[0].surfaceKind =
      lg::ArenaSurfaceKind::Default;
    arena->visualWalls[0].faceSurfaceKinds[4] =
      lg::ArenaSurfaceKind::Sky;
    failures += expect(
      lg::arenaSkySurfaceFingerprint(*arena) != baseline,
      "visual wall sky changes must invalidate the static world cache"
    );
    arena->visualWalls[0].faceSurfaceKinds[4] =
      lg::ArenaSurfaceKind::Default;
    arena->visualBrushes[0].faces[0].surfaceKind =
      lg::ArenaSurfaceKind::Sky;
    failures += expect(
      lg::arenaSkySurfaceFingerprint(*arena) != baseline,
      "visual brush sky changes must invalidate the static world cache"
    );
  }

  return failures == 0 ? 0 : 1;
}
