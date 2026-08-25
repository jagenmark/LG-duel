#pragma once

#include <filesystem>
#include <string>

namespace lg {

struct AimTrainerVideoSettings {
  int displayMode = 0;
  int resolutionWidth = 1280;
  int resolutionHeight = 720;
  int textureFilter = 2;
  int textureAnisotropy = 8;
  float displayGamma = 1.0F;
  bool bloom = true;
  int antiAliasing = 0;
  int sunShadows = 0;
  int pointLights = 1;
  bool showViewModel = true;
  bool showBeam = true;
};

struct AimTrainerVideoSettingsLoadResult {
  AimTrainerVideoSettings settings;
  bool loaded = false;
  std::string warning;
};

[[nodiscard]] AimTrainerVideoSettingsLoadResult loadAimTrainerVideoSettings(
  const std::filesystem::path& path
);

[[nodiscard]] bool saveAimTrainerVideoSettings(
  const std::filesystem::path& path,
  const AimTrainerVideoSettings& settings,
  std::string& error
);

} // namespace lg
