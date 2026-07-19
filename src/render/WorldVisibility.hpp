#pragma once

#include "render/Perspective.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace lg {

constexpr std::uint32_t kWorldVisibilityInvalidIndex = UINT32_MAX;
constexpr std::uint32_t kWorldVisibilityMaxTrianglesPerLeaf = 256U;

struct WorldVisibilityTriangle {
  Vec3 a = {};
  Vec3 b = {};
  Vec3 c = {};
  std::uint32_t materialId = 0;
};

struct WorldVisibilityAabb {
  Vec3 min = {};
  Vec3 max = {};
};

struct WorldVisibilityChunk {
  WorldVisibilityAabb bounds = {};
  std::uint32_t materialId = 0;
  // Offset into WorldVisibility::orderedTriangleIndices. Chunks are stored
  // material-major so the renderer can preserve its direct material batches.
  std::uint32_t firstTriangle = 0;
  std::uint32_t triangleCount = 0;
};

struct WorldVisibilityNode {
  WorldVisibilityAabb bounds = {};
  std::uint32_t leftChild = kWorldVisibilityInvalidIndex;
  std::uint32_t rightChild = kWorldVisibilityInvalidIndex;
  // Chunk references cover this node's entire DFS subtree. They remain
  // indirect because material-major GPU packing makes spatial chunks
  // non-contiguous in the final chunk array.
  std::uint32_t firstChunkReference = 0;
  std::uint32_t chunkReferenceCount = 0;

  [[nodiscard]] bool leaf() const {
    return leftChild == kWorldVisibilityInvalidIndex;
  }
};

struct WorldVisibility {
  std::vector<WorldVisibilityNode> nodes;
  std::vector<WorldVisibilityChunk> chunks;
  std::vector<std::uint32_t> nodeChunkIndices;
  std::vector<std::uint32_t> orderedTriangleIndices;
};

struct WorldVisibilityQueryScratch {
  std::vector<std::uint8_t> visibleChunks;
  std::vector<std::uint32_t> nodeStack;
  std::uint32_t testedNodes = 0;
  std::uint32_t visibleChunkCount = 0;
};

[[nodiscard]] WorldVisibility buildWorldVisibility(
  std::span<const WorldVisibilityTriangle> triangles
);

// Tests the near and four side planes only. Omitting a far plane is deliberate:
// the renderer has no gameplay-relevant world draw-distance cutoff.
void queryWorldVisibility(
  const WorldVisibility& visibility,
  const PerspectiveCamera& camera,
  WorldVisibilityQueryScratch& scratch
);

} // namespace lg
