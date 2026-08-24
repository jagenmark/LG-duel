#include "trainer/AimTrainerVideoSettings.hpp"

#include <algorithm>
#include <array>
#include <fstream>

namespace lg {

AimTrainerVideoSettingsLoadResult loadAimTrainerVideoSettings(
  const std::filesystem::path& path
) {
  AimTrainerVideoSettingsLoadResult result;
  std::ifstream input(path);
  if (!input) return result;

  int version = 0;
  int bloom = 1;
  int showViewModel = 1;
  int showBeam = 1;
  std::array<bool, 13> seen = {};
  std::string key;
  while (input >> key) {
    if (key == "version") {
      input >> version;
      seen[0] = true;
    } else if (key == "display_mode") {
      input >> result.settings.displayMode;
      seen[1] = true;
    } else if (key == "resolution_width") {
      input >> result.settings.resolutionWidth;
      seen[2] = true;
    } else if (key == "resolution_height") {
      input >> result.settings.resolutionHeight;
      seen[3] = true;
    } else if (key == "texture_filter") {
      input >> result.settings.textureFilter;
      seen[4] = true;
    } else if (key == "texture_anisotropy") {
      input >> result.settings.textureAnisotropy;
      seen[5] = true;
    } else if (key == "display_gamma") {
      input >> result.settings.displayGamma;
      seen[6] = true;
    } else if (key == "bloom") {
      input >> bloom;
      seen[7] = true;
    } else if (key == "anti_aliasing") {
      input >> result.settings.antiAliasing;
      seen[8] = true;
    } else if (key == "sun_shadows") {
      input >> result.settings.sunShadows;
      seen[9] = true;
    } else if (key == "point_lights") {
      input >> result.settings.pointLights;
      seen[10] = true;
    } else if (key == "show_viewmodel") {
      input >> showViewModel;
      seen[11] = true;
    } else if (key == "show_beam") {
      input >> showBeam;
      seen[12] = true;
    } else {
      std::string ignored;
      std::getline(input, ignored);
    }
    if (!input) {
      result.warning = "video settings are malformed; using defaults";
      result.settings = {};
      return result;
    }
  }
  const bool hasLegacyFields = std::all_of(
    seen.begin(), seen.begin() + 11, [](bool value) { return value; }
  );
  const bool hasPresentationFields = seen[11] && seen[12];
  if (!hasLegacyFields || (version != 1 && version != 2) ||
      (version == 2 && !hasPresentationFields)) {
    result.warning = "video settings are incomplete or use an unknown version; using defaults";
    result.settings = {};
    return result;
  }
  result.settings.displayMode = std::clamp(result.settings.displayMode, 0, 2);
  result.settings.resolutionWidth =
    std::clamp(result.settings.resolutionWidth, 640, 16384);
  result.settings.resolutionHeight =
    std::clamp(result.settings.resolutionHeight, 480, 16384);
  result.settings.textureFilter = std::clamp(result.settings.textureFilter, 0, 2);
  const std::array<int, 5> anisotropyValues = {1, 2, 4, 8, 16};
  if (std::find(
        anisotropyValues.begin(),
        anisotropyValues.end(),
        result.settings.textureAnisotropy
      ) == anisotropyValues.end()) {
    result.settings.textureAnisotropy = 8;
  }
  result.settings.displayGamma =
    std::clamp(result.settings.displayGamma, 0.5F, 1.5F);
  result.settings.bloom = bloom != 0;
  result.settings.antiAliasing = std::clamp(result.settings.antiAliasing, 0, 2);
  result.settings.sunShadows = std::clamp(result.settings.sunShadows, 0, 2);
  result.settings.pointLights = std::clamp(result.settings.pointLights, 0, 2);
  result.settings.showViewModel = showViewModel != 0;
  result.settings.showBeam = showBeam != 0;
  result.loaded = true;
  return result;
}

bool saveAimTrainerVideoSettings(
  const std::filesystem::path& path,
  const AimTrainerVideoSettings& settings,
  std::string& error
) {
  error.clear();
  std::error_code filesystemError;
  std::filesystem::create_directories(path.parent_path(), filesystemError);
  if (filesystemError) {
    error = "could not create video settings directory: " +
      filesystemError.message();
    return false;
  }
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      error = "could not open temporary video settings file";
      return false;
    }
    output
      << "version 2\n"
      << "display_mode " << settings.displayMode << '\n'
      << "resolution_width " << settings.resolutionWidth << '\n'
      << "resolution_height " << settings.resolutionHeight << '\n'
      << "texture_filter " << settings.textureFilter << '\n'
      << "texture_anisotropy " << settings.textureAnisotropy << '\n'
      << "display_gamma " << settings.displayGamma << '\n'
      << "bloom " << (settings.bloom ? 1 : 0) << '\n'
      << "anti_aliasing " << settings.antiAliasing << '\n'
      << "sun_shadows " << settings.sunShadows << '\n'
      << "point_lights " << settings.pointLights << '\n'
      << "show_viewmodel " << (settings.showViewModel ? 1 : 0) << '\n'
      << "show_beam " << (settings.showBeam ? 1 : 0) << '\n';
    if (!output) {
      error = "could not write temporary video settings file";
      return false;
    }
  }
  std::filesystem::rename(temporary, path, filesystemError);
  if (!filesystemError) return true;
  filesystemError.clear();
  std::filesystem::remove(path, filesystemError);
  filesystemError.clear();
  std::filesystem::rename(temporary, path, filesystemError);
  if (!filesystemError) return true;
  error = "could not replace video settings: " + filesystemError.message();
  std::error_code ignored;
  std::filesystem::remove(temporary, ignored);
  return false;
}

} // namespace lg
