#include "render/StaticAmbientProbe.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::Arena enclosedArena() {
  lg::Arena arena;
  arena.min = {-4.0F, -4.0F, 0.0F};
  arena.max = {4.0F, 4.0F, 4.0F};
  arena.renderDefaultFloor = false;
  arena.skyId = lg::SkyId::CrimsonSunset;
  arena.walls[0] = {
      {-4.0F, -4.0F, 1.8F},
      {4.0F, 4.0F, 2.0F},
  };
  arena.wallCount = 1U;
  return arena;
}

lg::ArenaLoadResult loadEyeToEye() {
  for (const char *path : {"maps/eyetoeye.map", "../maps/eyetoeye.map",
                           "../../maps/eyetoeye.map",
                           "../../../maps/eyetoeye.map"}) {
    lg::ArenaLoadResult result = lg::loadArenaFromFile(path);
    if (result.ok) {
      return result;
    }
  }
  return {};
}

} // namespace

int main() {
  int failures = 0;

  lg::Arena openArena;
  openArena.min = {-4.0F, -4.0F, 0.0F};
  openArena.max = {4.0F, 4.0F, 4.0F};
  openArena.renderDefaultFloor = false;
  openArena.skyId = lg::SkyId::CrimsonSunset;
  openArena.wallCount = 0U;
  openArena.brushCount = 0U;
  const lg::StaticAmbientProbeGrid openGrid =
      lg::bakeStaticAmbientProbeGrid(openArena, 2);
  failures +=
      expect(openGrid.enabled() && openGrid.byteSize() == 13U * 13U * 7U &&
                 openGrid.raysCast == openGrid.byteSize() * 6U &&
                 openGrid.minimumVisibility == 255U &&
                 openGrid.maximumVisibility == 255U,
             "open high-quality grid stays fully visible and bounded");

  lg::Arena wideArena;
  wideArena.min = {-125.0F, -125.0F, -20.0F};
  wideArena.max = {125.0F, 125.0F, 20.0F};
  wideArena.renderDefaultFloor = false;
  wideArena.skyId = lg::SkyId::CrimsonSunset;
  wideArena.spawnCount = 2U;
  wideArena.spawnPositions[0] = {-4.0F, 0.0F, 0.0F};
  wideArena.spawnPositions[1] = {4.0F, 0.0F, 0.0F};
  wideArena.walls[0] = {{-5.0F, -5.0F, 2.8F}, {5.0F, 5.0F, 3.0F}};
  wideArena.wallCount = 1U;
  const lg::StaticAmbientProbeGrid wideGrid =
      lg::bakeStaticAmbientProbeGrid(wideArena, 2);
  failures += expect(
      wideGrid.maximum.x - wideGrid.minimum.x <= 28.0F &&
          wideGrid.maximum.y - wideGrid.minimum.y <= 20.0F &&
          lg::sampleStaticAmbientProbe(wideGrid, {0.0F, 0.0F, 0.0F}) < 255U,
      "probe bounds follow play anchors instead of oversized map bounds");

  const lg::ArenaLoadResult eyeToEye = loadEyeToEye();
  if (!eyeToEye.ok) {
    failures += expect(false, "EyeToEye map should load for probe coverage");
  } else {
    const lg::StaticAmbientProbeGrid eyeToEyeGrid =
        lg::bakeStaticAmbientProbeGrid(eyeToEye.arena, 2);
    failures += expect(
        eyeToEyeGrid.maximum.x - eyeToEyeGrid.minimum.x <
                eyeToEye.arena.max.x - eyeToEye.arena.min.x &&
            eyeToEyeGrid.maximum.y - eyeToEyeGrid.minimum.y <
                eyeToEye.arena.max.y - eyeToEye.arena.min.y &&
            (eyeToEyeGrid.maximum.x - eyeToEyeGrid.minimum.x) /
                    static_cast<float>(eyeToEyeGrid.width - 1U) <=
                5.5F &&
            (eyeToEyeGrid.maximum.y - eyeToEyeGrid.minimum.y) /
                    static_cast<float>(eyeToEyeGrid.depth - 1U) <=
                5.5F &&
            lg::sampleStaticAmbientProbe(eyeToEyeGrid,
                                         eyeToEye.arena.spawnPositions[0]) <
                255U,
        "EyeToEye player probes stay near play space and sample nearby geometry");
  }

  const lg::Arena enclosed = enclosedArena();
  const lg::StaticAmbientProbeGrid enclosedFirst =
      lg::bakeStaticAmbientProbeGrid(enclosed, 2);
  const lg::StaticAmbientProbeGrid enclosedSecond =
      lg::bakeStaticAmbientProbeGrid(enclosed, 2);
  failures +=
      expect(enclosedFirst.visibility == enclosedSecond.visibility &&
                 enclosedFirst.fingerprint == enclosedSecond.fingerprint,
             "probe bake is deterministic");

  lg::Arena cacheArena = enclosed;
  const std::uint64_t baseProbeInputs =
      lg::ambientProbeInputFingerprint(cacheArena);
  cacheArena.spawnPositions[0].x += 1.0F;
  const std::uint64_t movedSpawnInputs =
      lg::ambientProbeInputFingerprint(cacheArena);
  cacheArena.spawnPositions[0].x -= 1.0F;
  cacheArena.skyId = lg::SkyId::None;
  failures += expect(
      movedSpawnInputs != baseProbeInputs &&
          lg::ambientProbeInputFingerprint(cacheArena) != baseProbeInputs,
      "revision-zero cache inputs include probe anchors and fallback sky state");
  failures += expect(
      enclosedFirst.minimumVisibility < openGrid.minimumVisibility &&
          enclosedFirst.minimumVisibility >= 140U &&
          enclosedFirst.maximumVisibility <= 255U,
      "ceiling lowers probe visibility without exceeding the read floor");

  lg::Arena visualEnclosed = enclosed;
  visualEnclosed.visualWalls[0] = visualEnclosed.walls[0];
  visualEnclosed.visualWallCount = 1U;
  visualEnclosed.wallCount = 0U;
  lg::StaticAmbientBaker visualBaker(visualEnclosed, 2);
  failures +=
      expect(visualBaker.sample({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}) < 255U,
             "visual-only geometry casts ambient occlusion");

  lg::Arena visualFingerprintArena;
  visualFingerprintArena.visualBrushCount = 1U;
  visualFingerprintArena.visualBrushes[0].faceCount = 1U;
  visualFingerprintArena.visualBrushes[0].faces[0].normal = {0.0F, 0.0F, 1.0F};
  visualFingerprintArena.visualBrushes[0].faces[0].distance = 2.0F;
  const std::uint64_t firstVisualFingerprint =
      lg::visualAmbientOccluderFingerprint(visualFingerprintArena);
  visualFingerprintArena.visualBrushes[0].faces[0].normal = {0.0F, 1.0F, 0.0F};
  failures +=
      expect(lg::visualAmbientOccluderFingerprint(visualFingerprintArena) !=
                 firstVisualFingerprint,
             "visual occluder normals invalidate the ambient cache key");

  lg::StaticAmbientBaker openBaker(openArena, 2);
  lg::StaticAmbientBaker enclosedBaker(enclosed, 2);
  const std::uint8_t openSurface =
      openBaker.sample({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
  const std::uint8_t enclosedSurface =
      enclosedBaker.sample({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
  const std::uint8_t repeatedSurface =
      enclosedBaker.sample({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
  failures += expect(
      openSurface == 255U && enclosedSurface < openSurface &&
          enclosedSurface >= 140U && repeatedSurface == enclosedSurface &&
          enclosedBaker.stats().uniqueSamples == 1U &&
          enclosedBaker.stats().cacheHits == 1U &&
          enclosedBaker.stats().maximumVisibility == enclosedSurface,
      "surface bake is clamped and reuses exact samples");

  const float encodedVisibility =
      lg::encodedAmbientVisibilityScale(enclosedSurface);
  failures += expect(
      std::fabs(std::pow(encodedVisibility, 2.2F) -
                static_cast<float>(enclosedSurface) / 255.0F) < 0.001F,
      "static sRGB light bytes decode to the same linear visibility as probes");

  lg::Arena fallbackArena;
  fallbackArena.min = {-2.0F, -2.0F, 0.0F};
  fallbackArena.max = {2.0F, 2.0F, 2.0F};
  fallbackArena.spawnCount = 1U;
  fallbackArena.spawnPositions[0] = {0.0F, 0.0F, 0.0F};
  lg::StaticAmbientBaker fallbackBaker(fallbackArena, 2);
  failures += expect(
      fallbackBaker.sample({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}) < 255U,
      "generated fallback walls and ceiling cast ambient occlusion");

  lg::StaticAmbientBaker disabledBaker(enclosed, 0);
  failures += expect(
      disabledBaker.sample({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}) == 255U &&
          disabledBaker.stats().raysCast == 0U &&
          !lg::bakeStaticAmbientProbeGrid(enclosed, 0).enabled(),
      "quality zero has no bake or probe work");

  lg::StaticAmbientProbeGrid interpolationGrid;
  interpolationGrid.minimum = {0.0F, 0.0F, 0.0F};
  interpolationGrid.maximum = {1.0F, 1.0F, 1.0F};
  interpolationGrid.width = 2U;
  interpolationGrid.depth = 2U;
  interpolationGrid.height = 2U;
  interpolationGrid.visibility = {
      0U, 32U, 64U, 96U, 128U, 160U, 192U, 224U,
  };
  failures +=
      expect(lg::sampleStaticAmbientProbe(interpolationGrid,
                                          {0.0F, 0.0F, 0.0F}) == 0U &&
                 lg::sampleStaticAmbientProbe(interpolationGrid,
                                              {1.0F, 1.0F, 1.0F}) == 224U &&
                 lg::sampleStaticAmbientProbe(interpolationGrid,
                                              {0.5F, 0.5F, 0.5F}) == 112U &&
                 lg::sampleStaticAmbientProbe(interpolationGrid,
                                              {-1.0F, 0.0F, 0.0F}) == 0U,
             "probe samples clamp and interpolate");

  if (failures == 0) {
    std::cout << "Static ambient probe tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
