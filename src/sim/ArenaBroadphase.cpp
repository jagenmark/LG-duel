#include "sim/ArenaBroadphase.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <numeric>

namespace lg {
namespace {

constexpr std::size_t kLeafSize = 8;
constexpr std::size_t kTraversalStackSize = 64;
thread_local ArenaBroadphaseProfile* gActiveProfile = nullptr;

[[nodiscard]] Vec3 minimum(Vec3 lhs, Vec3 rhs) {
  return {std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y), std::min(lhs.z, rhs.z)};
}

[[nodiscard]] Vec3 maximum(Vec3 lhs, Vec3 rhs) {
  return {std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y), std::max(lhs.z, rhs.z)};
}

[[nodiscard]] float axisValue(Vec3 value, int axis) {
  return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}

[[nodiscard]] bool overlaps(Vec3 lhsMin, Vec3 lhsMax, Vec3 rhsMin, Vec3 rhsMax) {
  return lhsMin.x <= rhsMax.x && lhsMax.x >= rhsMin.x &&
    lhsMin.y <= rhsMax.y && lhsMax.y >= rhsMin.y &&
    lhsMin.z <= rhsMax.z && lhsMax.z >= rhsMin.z;
}

[[nodiscard]] std::uint32_t buildNode(
  ArenaCollisionIndex& index,
  std::size_t begin,
  std::size_t end
) {
  const std::uint32_t nodeIndex = static_cast<std::uint32_t>(index.nodes.size());
  index.nodes.push_back({});
  Vec3 boundsMin = index.primitives[begin].min;
  Vec3 boundsMax = index.primitives[begin].max;
  Vec3 centroidMin = (boundsMin + boundsMax) * 0.5F;
  Vec3 centroidMax = centroidMin;
  for (std::size_t primitive = begin + 1U; primitive < end; ++primitive) {
    boundsMin = minimum(boundsMin, index.primitives[primitive].min);
    boundsMax = maximum(boundsMax, index.primitives[primitive].max);
    const Vec3 centroid = (index.primitives[primitive].min + index.primitives[primitive].max) * 0.5F;
    centroidMin = minimum(centroidMin, centroid);
    centroidMax = maximum(centroidMax, centroid);
  }
  ArenaBroadphaseNode& node = index.nodes[nodeIndex];
  node.min = boundsMin;
  node.max = boundsMax;
  const std::size_t count = end - begin;
  if (count <= kLeafSize) {
    node.first = static_cast<std::uint32_t>(begin);
    node.count = static_cast<std::uint16_t>(count);
    return nodeIndex;
  }

  const Vec3 extent = centroidMax - centroidMin;
  int axis = 0;
  if (extent.y > extent.x) axis = 1;
  if (axisValue(extent, 2) > axisValue(extent, axis)) axis = 2;
  std::stable_sort(
    index.primitives.begin() + static_cast<std::ptrdiff_t>(begin),
    index.primitives.begin() + static_cast<std::ptrdiff_t>(end),
    [axis](const ArenaBroadphasePrimitive& lhs, const ArenaBroadphasePrimitive& rhs) {
      const float lhsCentroid = axisValue((lhs.min + lhs.max) * 0.5F, axis);
      const float rhsCentroid = axisValue((rhs.min + rhs.max) * 0.5F, axis);
      if (lhsCentroid != rhsCentroid) return lhsCentroid < rhsCentroid;
      if (lhs.brush != rhs.brush) return lhs.brush < rhs.brush;
      return lhs.index < rhs.index;
    }
  );
  const std::size_t middle = begin + count / 2U;
  const std::uint32_t left = buildNode(index, begin, middle);
  const std::uint32_t right = buildNode(index, middle, end);
  // Recursive vector growth can invalidate references, so publish children by
  // index after both subtrees are complete.
  index.nodes[nodeIndex].left = left;
  index.nodes[nodeIndex].right = right;
  return nodeIndex;
}

void recordQueryProfile(
  ArenaBroadphaseProfile& profile,
  const Arena& arena,
  std::uint64_t nodesVisited,
  std::uint64_t candidatesReturned,
  bool fallback
) {
  ++profile.queryCount;
  profile.totalStaticSolids = std::max<std::uint64_t>(
    profile.totalStaticSolids,
    static_cast<std::uint64_t>(arena.wallCount + arena.brushCount)
  );
  profile.nodesVisited += nodesVisited;
  profile.candidatesReturned += candidatesReturned;
  profile.maxNodesVisited = std::max(profile.maxNodesVisited, nodesVisited);
  profile.maxCandidatesReturned = std::max(
    profile.maxCandidatesReturned,
    candidatesReturned
  );
  if (fallback) ++profile.fallbackCount;
}

template <bool Profile>
[[nodiscard]] bool queryArenaCollisionIndexImpl(
  const Arena& arena,
  Vec3 queryMin,
  Vec3 queryMax,
  ArenaBroadphaseCandidates& candidates
) {
  candidates = {};
  std::uint64_t nodesVisited = 0;
  std::uint64_t candidatesReturned = 0;
  const std::shared_ptr<const ArenaCollisionIndex>& index = arena.collisionIndex;
  if (!index || index->nodes.empty() || index->sourceWallCount != arena.wallCount ||
      index->sourceBrushCount != arena.brushCount) {
    if constexpr (Profile) {
      recordQueryProfile(*gActiveProfile, arena, nodesVisited, candidatesReturned, true);
    }
    return false;
  }
  std::array<std::uint32_t, kTraversalStackSize> stack = {};
  std::size_t stackSize = 1U;
  stack[0] = 0U;
  while (stackSize > 0U) {
    const ArenaBroadphaseNode& node = index->nodes[stack[--stackSize]];
    if constexpr (Profile) ++nodesVisited;
    if (!overlaps(node.min, node.max, queryMin, queryMax)) continue;
    if (node.count > 0U) {
      for (std::size_t offset = 0; offset < node.count; ++offset) {
        const ArenaBroadphasePrimitive& primitive = index->primitives[node.first + offset];
        if (!overlaps(primitive.min, primitive.max, queryMin, queryMax)) continue;
        if (primitive.brush) candidates.brushes.set(primitive.index);
        else candidates.walls.set(primitive.index);
        if constexpr (Profile) ++candidatesReturned;
      }
      continue;
    }
    if (stackSize + 2U > stack.size()) {
      candidates = {};
      if constexpr (Profile) {
        recordQueryProfile(*gActiveProfile, arena, nodesVisited, 0, true);
      }
      return false;
    }
    stack[stackSize++] = node.right;
    stack[stackSize++] = node.left;
  }
  if constexpr (Profile) {
    recordQueryProfile(*gActiveProfile, arena, nodesVisited, candidatesReturned, false);
  }
  return true;
}

} // namespace

ArenaBroadphaseProfileScope::ArenaBroadphaseProfileScope(
  ArenaBroadphaseProfile& profile
) : previous_(gActiveProfile) {
  gActiveProfile = &profile;
}

ArenaBroadphaseProfileScope::~ArenaBroadphaseProfileScope() {
  gActiveProfile = previous_;
}

void buildArenaCollisionIndex(Arena& arena) {
  auto index = std::make_shared<ArenaCollisionIndex>();
  index->sourceWallCount = arena.wallCount;
  index->sourceBrushCount = arena.brushCount;
  index->primitives.reserve(arena.wallCount + arena.brushCount);
  for (std::size_t wall = 0; wall < arena.wallCount; ++wall) {
    index->primitives.push_back({arena.walls[wall].min, arena.walls[wall].max,
      static_cast<std::uint16_t>(wall), false});
  }
  for (std::size_t brush = 0; brush < arena.brushCount; ++brush) {
    index->primitives.push_back({arena.brushes[brush].min, arena.brushes[brush].max,
      static_cast<std::uint16_t>(brush), true});
  }
  index->nodes.reserve(index->primitives.size() * 2U);
  if (!index->primitives.empty()) (void)buildNode(*index, 0U, index->primitives.size());
  arena.collisionIndex = std::move(index);
}

bool queryArenaCollisionIndex(
  const Arena& arena,
  Vec3 queryMin,
  Vec3 queryMax,
  ArenaBroadphaseCandidates& candidates
) {
  if (gActiveProfile != nullptr) {
    return queryArenaCollisionIndexImpl<true>(arena, queryMin, queryMax, candidates);
  }
  return queryArenaCollisionIndexImpl<false>(arena, queryMin, queryMax, candidates);
}

bool arenaBroadphaseProfilingEnabled() {
  return gActiveProfile != nullptr;
}

void recordArenaBroadphaseCandidateTests(std::uint64_t count) {
  if (gActiveProfile == nullptr) return;
  gActiveProfile->candidatesTested += count;
  gActiveProfile->maxCandidatesTested = std::max(
    gActiveProfile->maxCandidatesTested,
    count
  );
}

} // namespace lg
