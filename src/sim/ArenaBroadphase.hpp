#pragma once

#include "shared/Math.hpp"
#include "sim/Arena.hpp"

#include <bitset>
#include <cstdint>
#include <vector>

namespace lg {

struct ArenaBroadphasePrimitive {
  Vec3 min = {};
  Vec3 max = {};
  std::uint16_t index = 0;
  bool brush = false;
};

struct ArenaBroadphaseNode {
  Vec3 min = {};
  Vec3 max = {};
  std::uint32_t first = 0;
  std::uint16_t count = 0;
  std::uint32_t left = 0;
  std::uint32_t right = 0;
};

struct ArenaCollisionIndex {
  std::vector<ArenaBroadphasePrimitive> primitives;
  std::vector<ArenaBroadphaseNode> nodes;
  std::size_t sourceWallCount = 0;
  std::size_t sourceBrushCount = 0;
};

struct ArenaBroadphaseCandidates {
  std::bitset<Arena::kWallCount> walls;
  std::bitset<Arena::kBrushCount> brushes;
};

// The index is derived from immutable authored geometry. It is deliberately
// excluded from map hashes and network serialization.
void buildArenaCollisionIndex(Arena& arena);

[[nodiscard]] bool queryArenaCollisionIndex(
  const Arena& arena,
  Vec3 queryMin,
  Vec3 queryMax,
  ArenaBroadphaseCandidates& candidates
);

} // namespace lg
