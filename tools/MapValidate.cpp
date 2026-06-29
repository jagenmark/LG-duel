#include "map/MapParser.hpp"
#include "sim/Arena.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] bool isMapFile(const std::filesystem::path& path) {
  const std::string extension = path.extension().string();
  return extension == ".lgmap" || extension == ".map";
}

[[nodiscard]] std::string normalizedTextureMaterial(std::string material) {
  std::replace(material.begin(), material.end(), '\\', '/');
  while (!material.empty() && material.front() == '/') {
    material.erase(material.begin());
  }
  return material;
}

[[nodiscard]] bool hasTextureExtension(const std::filesystem::path& path) {
  const std::string extension = path.extension().string();
  return extension == ".png" || extension == ".bmp" || extension == ".jpg";
}

[[nodiscard]] int validateMapTextures(
  const std::filesystem::path& mapPath,
  const std::filesystem::path& textureRoot
) {
  if (mapPath.extension() != ".map") {
    return 0;
  }

  std::ifstream file(mapPath);
  if (!file) {
    return 0;
  }
  std::ostringstream text;
  text << file.rdbuf();
  const lg::MapParseResult parsed = lg::parseMapDocument(text.str());
  if (!parsed.ok) {
    return 0;
  }

  int failures = 0;
  for (const lg::MapEntity& entity : parsed.document.entities) {
    for (const lg::MapBrush& brush : entity.brushes) {
      for (const lg::MapFace& face : brush.faces) {
        if (face.material.empty()) {
          continue;
        }
        std::filesystem::path texturePath =
          textureRoot / normalizedTextureMaterial(face.material);
        if (!hasTextureExtension(texturePath)) {
          texturePath += ".png";
        }
        if (!std::filesystem::is_regular_file(texturePath)) {
          std::cerr << "map ERROR: " << mapPath.string()
                    << ": line " << face.line
                    << ": texture not found: " << texturePath.string()
                    << '\n';
          ++failures;
        }
      }
    }
  }
  return failures;
}

[[nodiscard]] int validatePath(const std::filesystem::path& path) {
  if (std::filesystem::is_regular_file(path)) {
    if (!isMapFile(path)) {
      return 0;
    }
    const lg::ArenaLoadResult result = lg::loadArenaFromFile(path.string());
    if (result.ok) {
      std::cout << "map ok: " << path.string() << " boxes="
                << result.arena.wallCount << " brushes="
                << result.arena.brushCount << '\n';
      return validateMapTextures(path, path.parent_path().parent_path() / "textures");
    }
    std::cerr << "map ERROR: " << result.error << '\n';
    return 1;
  }

  if (!std::filesystem::is_directory(path)) {
    std::cerr << "map ERROR: path does not exist: " << path.string() << '\n';
    return 1;
  }

  int failures = 0;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(path)) {
    if (!entry.is_regular_file() || !isMapFile(entry.path())) {
      continue;
    }
    failures += validatePath(entry.path());
  }
  return failures;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: lg_duel_map_validate <map-file-or-directory> [...]\n";
    return 2;
  }

  int failures = 0;
  for (int index = 1; index < argc; ++index) {
    failures += validatePath(argv[index]);
  }
  return failures == 0 ? 0 : 1;
}
