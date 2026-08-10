#include "map/MapParser.hpp"
#include "server/BotAi.hpp"
#include "sim/Arena.hpp"
#include "sim/Movement.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool isMapFile(const std::filesystem::path& path) {
  const std::string extension = path.extension().string();
  return extension == ".map";
}

[[nodiscard]] std::string normalizedTextureMaterial(std::string material) {
  std::replace(material.begin(), material.end(), '\\', '/');
  while (!material.empty() && material.front() == '/') {
    material.erase(material.begin());
  }
  constexpr std::string_view prefix = "textures/";
  if (material.rfind(prefix, 0) == 0) {
    material.erase(0, prefix.size());
  }
  return material;
}

[[nodiscard]] bool hasTextureExtension(const std::filesystem::path& path) {
  const std::string extension = path.extension().string();
  return extension == ".png" || extension == ".bmp" || extension == ".jpg";
}

[[nodiscard]] bool needsNoRuntimeTexture(std::string material) {
  std::replace(material.begin(), material.end(), '\\', '/');
  while (!material.empty() && material.front() == '/') {
    material.erase(material.begin());
  }
  std::transform(
    material.begin(),
    material.end(),
    material.begin(),
    [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    }
  );
  constexpr std::string_view prefix = "textures/";
  if (material.rfind(prefix, 0) == 0) {
    material.erase(0, prefix.size());
  }
  return material == "common/clip" || material == "common/playerclip" ||
    material == "common/weapclip" || material == "common/sky";
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
        if (needsNoRuntimeTexture(face.material)) {
          continue;
        }
        const std::string material = normalizedTextureMaterial(face.material);
        std::filesystem::path texturePath =
          textureRoot / material;
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

[[nodiscard]] bool hasRoute(
  const lg::BotNavigationMap& map,
  std::size_t start,
  std::size_t target
) {
  if (start >= map.nodeCount || target >= map.nodeCount) return false;
  std::array<bool, lg::BotNavigationMap::kMaxNodes> seen = {};
  std::array<std::size_t, lg::BotNavigationMap::kMaxNodes> queue = {};
  std::size_t head = 0;
  std::size_t tail = 0;
  queue[tail++] = start;
  seen[start] = true;
  while (head < tail) {
    const std::size_t current = queue[head++];
    if (current == target) return true;
    for (std::size_t index = 0; index < map.linkCount; ++index) {
      const lg::BotNavLink& link = map.links[index];
      if (link.from == current && !seen[link.to]) {
        seen[link.to] = true;
        queue[tail++] = link.to;
      }
    }
  }
  return false;
}

[[nodiscard]] int validateNavigation(
  const std::filesystem::path& mapPath,
  const lg::Arena& arena
) {
  const lg::BotNavigationMap map = lg::buildBotNavigationMap(
    arena, lg::MovementTuning{}, lg::CollisionBounds{}
  );
  int failures = 0;
  if (!map.requiredAnchorsComplete) {
    std::cerr << "nav ERROR: " << mapPath.string() << ": "
      << map.missingRequiredAnchorCount << " of " << map.requiredAnchorCount
      << " required gameplay anchors could not be reserved or grounded\n";
    ++failures;
  }
  if (map.nodeCount == 0U || map.linkCount == 0U) {
    std::cerr << "nav ERROR: " << mapPath.string() << ": empty navigation graph\n";
    return failures + 1;
  }
  const auto nodeForGroundAnchor = [&](lg::Vec3 position) {
    position.z += lg::CollisionBounds{}.halfHeight;
    return lg::nearestBotNavNode(map, position);
  };
  const auto requireNearbyNode = [&](std::string_view kind, lg::Vec3 position) {
    const std::size_t node = nodeForGroundAnchor(position);
    // Spawn origins may sit above their first legal landing surface. Anchor
    // proximity is horizontal; buildBotNavigationMap already proves the node
    // itself rests through normal collision/movement.
    if (node >= map.nodeCount || std::hypot(map.nodes[node].position.x - position.x,
        map.nodes[node].position.y - position.y) > 1.25F) {
      std::cerr << "nav ERROR: " << mapPath.string() << ": no grounded " << kind
        << " anchor near (" << position.x << ',' << position.y << ',' << position.z << ")\n";
      ++failures;
    }
  };
  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    requireNearbyNode("spawn", arena.spawnPositions[index]);
  }
  for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
    requireNearbyNode("team spawn", arena.teamSpawns[index].position);
  }
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    requireNearbyNode("health", arena.healthPickups[index].position);
  }
  // Normal deathmatch spawns are gameplay regions. Team spawn groups can be
  // intentionally isolated, so the validator checks each group separately.
  const auto validateSpawnGroup = [&](const std::vector<std::size_t>& nodes,
                                      std::string_view label) {
    if (nodes.size() < 2U) return;
    for (std::size_t index = 1; index < nodes.size(); ++index) {
      if (!hasRoute(map, nodes[0], nodes[index]) && !hasRoute(map, nodes[index], nodes[0])) {
        std::cerr << "nav ERROR: " << mapPath.string() << ": " << label
          << " regions are disconnected\n";
        ++failures;
        break;
      }
    }
  };
  std::vector<std::size_t> deathmatchNodes;
  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    deathmatchNodes.push_back(nodeForGroundAnchor(arena.spawnPositions[index]));
  }
  validateSpawnGroup(deathmatchNodes, "deathmatch spawn");
  for (const lg::ArenaSpawnGroup group : {lg::ArenaSpawnGroup::RedBase,
       lg::ArenaSpawnGroup::BlueBase, lg::ArenaSpawnGroup::None}) {
    std::vector<std::size_t> groupNodes;
    for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
      if (arena.teamSpawns[index].group == group) {
        groupNodes.push_back(nodeForGroundAnchor(arena.teamSpawns[index].position));
      }
    }
    validateSpawnGroup(groupNodes, "team spawn");
  }
  std::size_t padLinks = 0;
  std::size_t teleportLinks = 0;
  for (std::size_t index = 0; index < map.linkCount; ++index) {
    padLinks += map.links[index].kind == lg::BotNavLinkKind::JumpPad ? 1U : 0U;
    teleportLinks += map.links[index].kind == lg::BotNavLinkKind::Teleport ? 1U : 0U;
  }
  if (padLinks < arena.jumpPadCount || teleportLinks < arena.teleportCount) {
    std::cerr << "nav ERROR: " << mapPath.string() << ": special links pads=" << padLinks
      << '/' << arena.jumpPadCount << " teleports=" << teleportLinks << '/'
      << arena.teleportCount << '\n';
    ++failures;
  }
  std::cout << "nav ok: " << mapPath.string() << " nodes=" << map.nodeCount
    << " links=" << map.linkCount << " anchors=" << map.requiredAnchorCount << '\n';
  return failures;
}

[[nodiscard]] int validatePath(const std::filesystem::path& path, bool validateNav) {
  if (std::filesystem::is_regular_file(path)) {
    if (!isMapFile(path)) {
      return 0;
    }
    lg::ArenaLoadResult result;
    lg::loadArenaFromFile(path.string(), result);
    if (result.ok) {
      std::cout << "map ok: " << path.string() << " boxes="
                << result.arena.wallCount << " brushes="
                << result.arena.brushCount << '\n';
      const int textureFailures = validateMapTextures(
        path, path.parent_path().parent_path() / "textures"
      );
      return textureFailures + (validateNav ? validateNavigation(path, result.arena) : 0);
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
    failures += validatePath(entry.path(), validateNav);
  }
  return failures;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: lg_duel_map_validate [--nav] <map-file-or-directory> [...]\n";
    return 2;
  }

  bool validateNav = false;
  int failures = 0;
  for (int index = 1; index < argc; ++index) {
    if (std::string_view(argv[index]) == "--nav") {
      validateNav = true;
      continue;
    }
    failures += validatePath(argv[index], validateNav);
  }
  return failures == 0 ? 0 : 1;
}
