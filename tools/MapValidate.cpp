#include "map/MapParser.hpp"
#include "server/BotAi.hpp"
#include "sim/Arena.hpp"
#include "sim/ArenaBroadphase.hpp"
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

[[nodiscard]] std::size_t routeReachCount(
  const lg::BotNavigationMap& map,
  std::size_t start
) {
  if (start >= map.nodeCount) return 0U;
  std::array<bool, lg::BotNavigationMap::kMaxNodes> seen = {};
  std::array<std::size_t, lg::BotNavigationMap::kMaxNodes> queue = {};
  std::size_t head = 0;
  std::size_t tail = 0;
  queue[tail++] = start;
  seen[start] = true;
  while (head < tail) {
    const std::size_t current = queue[head++];
    for (std::size_t linkIndex = 0; linkIndex < map.linkCount; ++linkIndex) {
      const lg::BotNavLink& link = map.links[linkIndex];
      if (link.from == current && !seen[link.to]) {
        seen[link.to] = true;
        queue[tail++] = link.to;
      }
    }
  }
  return tail;
}

[[nodiscard]] bool hasDirectLink(
  const lg::BotNavigationMap& map,
  std::size_t from,
  std::size_t to,
  lg::BotNavLinkKind kind
) {
  for (std::size_t index = 0; index < map.linkCount; ++index) {
    const lg::BotNavLink& link = map.links[index];
    if (link.from == from && link.to == to && link.kind == kind) return true;
  }
  return false;
}

[[nodiscard]] const char* specialFailureStageName(lg::BotNavSpecialFailureStage stage) {
  switch (stage) {
  case lg::BotNavSpecialFailureStage::None: return "none";
  case lg::BotNavSpecialFailureStage::EntrySearch: return "entry-search";
  case lg::BotNavSpecialFailureStage::TriggerActivation: return "trigger";
  case lg::BotNavSpecialFailureStage::Landing: return "landing";
  case lg::BotNavSpecialFailureStage::NodeCapacity: return "node-cap";
  }
  return "unknown";
}

[[nodiscard]] const char* anchorKindName(lg::BotNavAnchorKind kind) {
  switch (kind) {
  case lg::BotNavAnchorKind::Spawn: return "spawn";
  case lg::BotNavAnchorKind::TeamSpawn: return "team-spawn";
  case lg::BotNavAnchorKind::Health: return "health";
  case lg::BotNavAnchorKind::NeutralObjective: return "neutral";
  case lg::BotNavAnchorKind::RedBase: return "red-base";
  case lg::BotNavAnchorKind::BlueBase: return "blue-base";
  case lg::BotNavAnchorKind::JumpPadEntry: return "jump-entry";
  case lg::BotNavAnchorKind::JumpPadLanding: return "jump-landing";
  case lg::BotNavAnchorKind::TeleportEntry: return "teleport-entry";
  case lg::BotNavAnchorKind::TeleportLanding: return "teleport-landing";
  }
  return "unknown";
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
    for (std::size_t index = 0; index < map.requiredAnchorCount; ++index) {
      const lg::BotNavRequiredAnchor& anchor = map.requiredAnchors[index];
      if (anchor.node >= map.nodeCount) {
        std::cerr << "nav ERROR: " << mapPath.string() << ": missing "
          << anchorKindName(anchor.kind) << '[' << anchor.sourceIndex << "] anchor\n";
      }
    }
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
  const auto nodeForHealthAnchor = [&](std::size_t index) {
    if (index < map.healthApproachEntryNodes.size() &&
        map.healthApproachEntryNodes[index] < map.nodeCount &&
        map.healthAnchorNodes[index] < map.nodeCount) {
      return static_cast<std::size_t>(map.healthAnchorNodes[index]);
    }
    return nodeForGroundAnchor(arena.healthPickups[index].position);
  };
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    const std::size_t entry = map.healthApproachEntryNodes[index];
    const std::size_t landing = nodeForHealthAnchor(index);
    if (entry < map.nodeCount) {
      const bool attached = landing < map.nodeCount &&
        (hasDirectLink(map, entry, landing, lg::BotNavLinkKind::Walk) ||
         hasDirectLink(map, entry, landing, lg::BotNavLinkKind::Jump));
      if (!attached) {
        std::cerr << "nav ERROR: " << mapPath.string() << ": health " << index
          << " has no simulated approach attachment\n";
        ++failures;
      }
    } else {
      requireNearbyNode("health", arena.healthPickups[index].position);
    }
  }
  // Normal deathmatch spawns are gameplay regions. Team spawn groups can be
  // intentionally isolated, so the validator checks each group separately.
  const auto validateSpawnGroup = [&](const std::vector<std::size_t>& nodes,
                                      std::string_view label) {
    if (nodes.size() < 2U) return;
    for (std::size_t index = 1; index < nodes.size(); ++index) {
      if (!hasRoute(map, nodes[0], nodes[index]) && !hasRoute(map, nodes[index], nodes[0])) {
        std::cerr << "nav ERROR: " << mapPath.string() << ": " << label
          << " regions are disconnected (node " << nodes[0] << " at ("
          << map.nodes[nodes[0]].position.x << ',' << map.nodes[nodes[0]].position.y << ','
          << map.nodes[nodes[0]].position.z << ") vs node " << nodes[index] << " at ("
          << map.nodes[nodes[index]].position.x << ',' << map.nodes[nodes[index]].position.y
          << ',' << map.nodes[nodes[index]].position.z << ")); reachable="
          << routeReachCount(map, nodes[0]) << '/' << map.nodeCount << " vs "
          << routeReachCount(map, nodes[index]) << '/' << map.nodeCount << "\n";
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
  // These checks follow edge direction. A player must be able to leave every
  // authored spawn for a real health or objective anchor; a reverse-only pad
  // route does not satisfy that gameplay need.
  const auto appendUniqueNode = [](std::vector<std::size_t>& nodes, std::size_t node) {
    if (std::find(nodes.begin(), nodes.end(), node) == nodes.end()) nodes.push_back(node);
  };
  std::vector<std::size_t> sourceNodes;
  for (const std::size_t node : deathmatchNodes) appendUniqueNode(sourceNodes, node);
  for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
    appendUniqueNode(sourceNodes, nodeForGroundAnchor(arena.teamSpawns[index].position));
  }
  std::vector<std::size_t> targetNodes;
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    appendUniqueNode(targetNodes, nodeForHealthAnchor(index));
  }
  if (arena.mcguffin.hasNeutralSpawn) {
    appendUniqueNode(targetNodes, nodeForGroundAnchor(arena.mcguffin.neutralSpawn));
  }
  const auto baseNode = [&](const lg::ArenaMcGuffinBase& base) {
    return nodeForGroundAnchor({(base.min.x + base.max.x) * 0.5F,
      (base.min.y + base.max.y) * 0.5F, std::max(base.min.z, arena.min.z)});
  };
  if (arena.mcguffin.hasRedBase) appendUniqueNode(targetNodes, baseNode(arena.mcguffin.redBase));
  if (arena.mcguffin.hasBlueBase) appendUniqueNode(targetNodes, baseNode(arena.mcguffin.blueBase));
  std::size_t missingDirectedRoutes = 0;
  constexpr std::size_t kRouteDiagnosticLimit = 12U;
  for (std::size_t source : sourceNodes) {
    for (std::size_t target : targetNodes) {
      if (source >= map.nodeCount || target >= map.nodeCount || !hasRoute(map, source, target)) {
        if (missingDirectedRoutes++ < kRouteDiagnosticLimit) {
          std::cerr << "nav ERROR: " << mapPath.string() << ": no directed route from spawn node "
            << source << " to gameplay node " << target << " (reachable="
            << routeReachCount(map, source) << '/' << map.nodeCount << ")\n";
        }
      }
    }
  }
  if (missingDirectedRoutes > 0U) {
    std::cerr << "nav ERROR: " << mapPath.string() << ": " << missingDirectedRoutes
      << " directed spawn-to-gameplay routes missing across " << sourceNodes.size()
      << " unique spawn nodes and " << targetNodes.size() << " unique gameplay nodes\n";
    ++failures;
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
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const lg::BotNavSpecialRoute& route = map.jumpPadRoutes[index];
    if (!route.verified || route.entryNode >= map.nodeCount || route.exitNode >= map.nodeCount ||
        !hasDirectLink(map, route.entryNode, route.exitNode, lg::BotNavLinkKind::JumpPad)) {
      std::cerr << "nav ERROR: " << mapPath.string() << ": jump pad " << index
        << " has no simulated directed entry-to-landing route (stage="
        << specialFailureStageName(route.failureStage) << ")\n";
      ++failures;
    }
  }
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const lg::BotNavSpecialRoute& route = map.teleportRoutes[index];
    if (!route.verified || route.entryNode >= map.nodeCount || route.exitNode >= map.nodeCount ||
        !hasDirectLink(map, route.entryNode, route.exitNode, lg::BotNavLinkKind::Teleport)) {
      std::cerr << "nav ERROR: " << mapPath.string() << ": teleport " << index
        << " has no simulated directed entry-to-landing route (stage="
        << specialFailureStageName(route.failureStage) << ")\n";
      ++failures;
    }
  }
  std::cout << "nav " << (failures == 0 ? "PASS: " : "FAIL: ") << mapPath.string()
    << " nodes=" << map.nodeCount << " links=" << map.linkCount
    << " anchors=" << map.requiredAnchorCount
    << " missing_anchors=" << map.missingRequiredAnchorCount
    << " local_links=" << map.localLinkCount
    << " local_trials=" << map.localTraversalTrials
    << " local_rejects=" << map.localBroadphaseRejects
    << " region_seeds=" << map.regionSeedCount
    << " region_work=" << map.regionExpansionWork
    << " region_work_exhausted=" << map.regionWorkExhausted
    << " region_task_capacity=" << map.regionTaskCapacityReached
    << " region_nodes=" << map.regionNodeCount
    << " node_capacity_rejects=" << map.nodeCapacityRejects
    << " link_capacity_rejects=" << map.linkCapacityRejects
    << " grounded_rejects=" << map.localGroundedRejects
    << " traversal_rejects=" << map.localTraversalRejects
    << " broadphase_retries=" << map.localBroadphaseRetries
    << " health_approach_grounded=" << map.healthApproachGroundedCandidates
    << " health_approach_trials=" << map.healthApproachSimulationTrials
    << " surface_approach_trials=" << map.surfaceApproachProbeTrials
    << " surface_approach_links=" << map.surfaceApproachProbeLinks
    << " surface_approach_targets=" << map.surfaceApproachTargetCount
    << " surface_approach_bridge_trials=" << map.surfaceApproachBridgeTrials
    << " surface_approach_bridge_links=" << map.surfaceApproachBridgeLinks
    << " surface_approach_flood_nodes=" << map.surfaceApproachFloodNodes
    << " surface_approach_flood_work=" << map.surfaceApproachFloodWork
    << " surface_approach_flood_exhausted=" << map.surfaceApproachFloodExhausted
    << " surface_drop_trials=" << map.surfaceDropProbeTrials
    << " surface_drop_links=" << map.surfaceDropProbeLinks
    << " root_unreachable_anchors=" << map.unreachableAnchorNodes
    << " specials=pads:" << padLinks << '/' << arena.jumpPadCount
    << ",teleports:" << teleportLinks << '/' << arena.teleportCount << '\n';
  if (failures != 0 || map.nodeCapacityRejects != 0U || map.regionWorkExhausted ||
      map.regionTaskCapacityReached) {
    std::cout << "nav DIAG: " << mapPath.string() << " anchors=";
    for (std::size_t index = 0; index < map.semanticAnchorCount; ++index) {
      const lg::BotNavAnchorReach& reach = map.anchorReach[index];
      if (index != 0U) std::cout << ',';
      std::cout << index << ':' << reach.node << '/' << reach.weakComponent << '/' <<
        reach.directedReach;
    }
    std::cout << '\n';
  }
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
      // Match the server map boundary. The broadphase changes only which
      // collision candidates movement checks; it does not bypass any nav or
      // trigger proof, and keeps strict validation practical on dense maps.
      lg::buildArenaCollisionIndex(result.arena);
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
