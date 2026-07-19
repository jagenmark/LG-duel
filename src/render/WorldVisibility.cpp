#include "render/WorldVisibility.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace lg {
namespace {

struct BuildTriangle {
  WorldVisibilityAabb bounds = {};
  Vec3 centroid = {};
  std::uint32_t materialId = 0;
  std::uint32_t sourceIndex = 0;
};

struct TemporaryChunk {
  WorldVisibilityAabb bounds = {};
  std::uint32_t materialId = 0;
  std::uint32_t leafOrder = 0;
  std::vector<std::uint32_t> triangleIndices;
};

[[nodiscard]] WorldVisibilityAabb triangleBounds(
  const WorldVisibilityTriangle& triangle
) {
  return {
    {
      std::min({triangle.a.x, triangle.b.x, triangle.c.x}),
      std::min({triangle.a.y, triangle.b.y, triangle.c.y}),
      std::min({triangle.a.z, triangle.b.z, triangle.c.z}),
    },
    {
      std::max({triangle.a.x, triangle.b.x, triangle.c.x}),
      std::max({triangle.a.y, triangle.b.y, triangle.c.y}),
      std::max({triangle.a.z, triangle.b.z, triangle.c.z}),
    },
  };
}

[[nodiscard]] WorldVisibilityAabb emptyBounds() {
  const float infinity = std::numeric_limits<float>::infinity();
  return {{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
}

void include(WorldVisibilityAabb& destination, const WorldVisibilityAabb& value) {
  destination.min.x = std::min(destination.min.x, value.min.x);
  destination.min.y = std::min(destination.min.y, value.min.y);
  destination.min.z = std::min(destination.min.z, value.min.z);
  destination.max.x = std::max(destination.max.x, value.max.x);
  destination.max.y = std::max(destination.max.y, value.max.y);
  destination.max.z = std::max(destination.max.z, value.max.z);
}

[[nodiscard]] float axisValue(Vec3 value, std::uint32_t axis) {
  if (axis == 0U) {
    return value.x;
  }
  if (axis == 1U) {
    return value.y;
  }
  return value.z;
}

class VisibilityBuilder {
public:
  explicit VisibilityBuilder(std::span<const WorldVisibilityTriangle> source) {
    triangles_.reserve(source.size());
    indices_.resize(source.size());
    std::iota(indices_.begin(), indices_.end(), 0U);
    for (std::uint32_t index = 0; index < source.size(); ++index) {
      const WorldVisibilityAabb bounds = triangleBounds(source[index]);
      triangles_.push_back({
        bounds,
        (bounds.min + bounds.max) * 0.5F,
        source[index].materialId,
        index,
      });
    }
    const std::size_t estimatedLeaves =
      (source.size() + kWorldVisibilityMaxTrianglesPerLeaf - 1U) /
      kWorldVisibilityMaxTrianglesPerLeaf;
    result_.nodes.reserve(estimatedLeaves * 2U);
    temporaryChunks_.reserve(estimatedLeaves);
  }

  [[nodiscard]] WorldVisibility build() {
    if (indices_.empty()) {
      return {};
    }
    (void)buildNode(0U, static_cast<std::uint32_t>(indices_.size()));
    finalizeChunks();
    return std::move(result_);
  }

private:
  [[nodiscard]] std::uint32_t buildNode(
    std::uint32_t first,
    std::uint32_t count
  ) {
    WorldVisibilityAabb bounds = emptyBounds();
    WorldVisibilityAabb centroidBounds = emptyBounds();
    for (std::uint32_t offset = 0; offset < count; ++offset) {
      const BuildTriangle& triangle = triangles_[indices_[first + offset]];
      include(bounds, triangle.bounds);
      include(centroidBounds, {triangle.centroid, triangle.centroid});
    }

    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(result_.nodes.size());
    result_.nodes.push_back({bounds});
    if (count <= kWorldVisibilityMaxTrianglesPerLeaf) {
      buildLeaf(nodeIndex, first, count);
      return nodeIndex;
    }

    const Vec3 extent = centroidBounds.max - centroidBounds.min;
    std::uint32_t axis = 0U;
    if (extent.y > extent.x) {
      axis = 1U;
    }
    if (axisValue(extent, 2U) > axisValue(extent, axis)) {
      axis = 2U;
    }
    const std::uint32_t middle = first + (count / 2U);
    // Source index is a total-order tie breaker, making the median split stable
    // even when large coplanar surfaces share a centroid on the split axis.
    std::nth_element(
      indices_.begin() + first,
      indices_.begin() + middle,
      indices_.begin() + first + count,
      [this, axis](std::uint32_t lhs, std::uint32_t rhs) {
        const float lhsValue = axisValue(triangles_[lhs].centroid, axis);
        const float rhsValue = axisValue(triangles_[rhs].centroid, axis);
        return lhsValue != rhsValue
          ? lhsValue < rhsValue
          : triangles_[lhs].sourceIndex < triangles_[rhs].sourceIndex;
      }
    );
    const std::uint32_t firstChunkReference =
      static_cast<std::uint32_t>(temporaryChunkReferences_.size());
    const std::uint32_t left = buildNode(first, middle - first);
    const std::uint32_t right = buildNode(middle, first + count - middle);
    result_.nodes[nodeIndex].leftChild = left;
    result_.nodes[nodeIndex].rightChild = right;
    // Leaf references append in DFS order, so every internal subtree is one
    // contiguous slice that can be accepted without visiting its children.
    result_.nodes[nodeIndex].firstChunkReference = firstChunkReference;
    result_.nodes[nodeIndex].chunkReferenceCount =
      static_cast<std::uint32_t>(temporaryChunkReferences_.size()) -
      firstChunkReference;
    return nodeIndex;
  }

  void buildLeaf(std::uint32_t nodeIndex, std::uint32_t first, std::uint32_t count) {
    auto begin = indices_.begin() + first;
    auto end = begin + count;
    std::sort(begin, end, [this](std::uint32_t lhs, std::uint32_t rhs) {
      const BuildTriangle& a = triangles_[lhs];
      const BuildTriangle& b = triangles_[rhs];
      return a.materialId != b.materialId
        ? a.materialId < b.materialId
        : a.sourceIndex < b.sourceIndex;
    });

    WorldVisibilityNode& node = result_.nodes[nodeIndex];
    node.firstChunkReference = static_cast<std::uint32_t>(temporaryChunkReferences_.size());
    std::uint32_t offset = 0U;
    while (offset < count) {
      const std::uint32_t materialId = triangles_[indices_[first + offset]].materialId;
      TemporaryChunk chunk;
      chunk.bounds = emptyBounds();
      chunk.materialId = materialId;
      chunk.leafOrder = static_cast<std::uint32_t>(temporaryChunks_.size());
      while (
        offset < count &&
        triangles_[indices_[first + offset]].materialId == materialId
      ) {
        const BuildTriangle& triangle = triangles_[indices_[first + offset]];
        include(chunk.bounds, triangle.bounds);
        chunk.triangleIndices.push_back(triangle.sourceIndex);
        ++offset;
      }
      temporaryChunkReferences_.push_back(chunk.leafOrder);
      temporaryChunks_.push_back(std::move(chunk));
      ++node.chunkReferenceCount;
    }
  }

  void finalizeChunks() {
    std::vector<std::uint32_t> chunkOrder(temporaryChunks_.size());
    std::iota(chunkOrder.begin(), chunkOrder.end(), 0U);
    std::sort(chunkOrder.begin(), chunkOrder.end(), [this](std::uint32_t lhs, std::uint32_t rhs) {
      const TemporaryChunk& a = temporaryChunks_[lhs];
      const TemporaryChunk& b = temporaryChunks_[rhs];
      return a.materialId != b.materialId
        ? a.materialId < b.materialId
        : a.leafOrder < b.leafOrder;
    });

    std::vector<std::uint32_t> remap(temporaryChunks_.size());
    result_.chunks.reserve(temporaryChunks_.size());
    result_.orderedTriangleIndices.reserve(triangles_.size());
    for (std::uint32_t oldIndex : chunkOrder) {
      const TemporaryChunk& source = temporaryChunks_[oldIndex];
      const std::uint32_t newIndex = static_cast<std::uint32_t>(result_.chunks.size());
      remap[oldIndex] = newIndex;
      const std::uint32_t firstTriangle =
        static_cast<std::uint32_t>(result_.orderedTriangleIndices.size());
      result_.orderedTriangleIndices.insert(
        result_.orderedTriangleIndices.end(),
        source.triangleIndices.begin(),
        source.triangleIndices.end()
      );
      result_.chunks.push_back({
        source.bounds,
        source.materialId,
        firstTriangle,
        static_cast<std::uint32_t>(source.triangleIndices.size()),
      });
    }
    result_.nodeChunkIndices.reserve(temporaryChunkReferences_.size());
    for (std::uint32_t oldIndex : temporaryChunkReferences_) {
      result_.nodeChunkIndices.push_back(remap[oldIndex]);
    }
  }

  std::vector<BuildTriangle> triangles_;
  std::vector<std::uint32_t> indices_;
  std::vector<TemporaryChunk> temporaryChunks_;
  std::vector<std::uint32_t> temporaryChunkReferences_;
  WorldVisibility result_;
};

struct Plane {
  Vec3 normal = {};
  float offset = 0.0F;
};

enum class FrustumClassification {
  Outside,
  Intersecting,
  Inside,
};

[[nodiscard]] FrustumClassification classifyFrustum(
  const WorldVisibilityAabb& bounds,
  const std::array<Plane, 5>& planes,
  Vec3 cameraPosition
) {
  bool intersects = false;
  for (const Plane& plane : planes) {
    const Vec3 positive = Vec3{
      plane.normal.x >= 0.0F ? bounds.max.x : bounds.min.x,
      plane.normal.y >= 0.0F ? bounds.max.y : bounds.min.y,
      plane.normal.z >= 0.0F ? bounds.max.z : bounds.min.z,
    } - cameraPosition;
    if (dot(plane.normal, positive) + plane.offset < 0.0F) {
      return FrustumClassification::Outside;
    }
    const Vec3 negative = Vec3{
      plane.normal.x >= 0.0F ? bounds.min.x : bounds.max.x,
      plane.normal.y >= 0.0F ? bounds.min.y : bounds.max.y,
      plane.normal.z >= 0.0F ? bounds.min.z : bounds.max.z,
    } - cameraPosition;
    if (dot(plane.normal, negative) + plane.offset < 0.0F) {
      intersects = true;
    }
  }
  return intersects
    ? FrustumClassification::Intersecting
    : FrustumClassification::Inside;
}

[[nodiscard]] bool finite(Vec3 value) {
  return std::isfinite(value.x) &&
    std::isfinite(value.y) &&
    std::isfinite(value.z);
}

[[nodiscard]] bool validDirection(Vec3 value) {
  const float squaredLength = dot(value, value);
  return finite(value) &&
    std::isfinite(squaredLength) &&
    squaredLength > 0.000001F;
}

} // namespace

WorldVisibility buildWorldVisibility(
  std::span<const WorldVisibilityTriangle> triangles
) {
  return VisibilityBuilder(triangles).build();
}

void queryWorldVisibility(
  const WorldVisibility& visibility,
  const PerspectiveCamera& camera,
  WorldVisibilityQueryScratch& scratch
) {
  scratch.visibleChunks.resize(visibility.chunks.size());
  std::fill(scratch.visibleChunks.begin(), scratch.visibleChunks.end(), std::uint8_t{0});
  scratch.nodeStack.clear();
  scratch.testedNodes = 0U;
  scratch.visibleChunkCount = 0U;
  if (visibility.nodes.empty()) {
    return;
  }

  if (
    !finite(camera.position) ||
    !validDirection(camera.forward) ||
    !validDirection(camera.right) ||
    !validDirection(camera.up) ||
    !std::isfinite(camera.focalLength) ||
    !std::isfinite(camera.aspectRatio) ||
    !std::isfinite(camera.nearPlane) ||
    camera.focalLength <= 0.0F ||
    camera.aspectRatio <= 0.0F ||
    camera.nearPlane < 0.0F
  ) {
    std::fill(scratch.visibleChunks.begin(), scratch.visibleChunks.end(), std::uint8_t{1});
    scratch.visibleChunkCount = static_cast<std::uint32_t>(visibility.chunks.size());
    return;
  }

  const float horizontalScale = camera.aspectRatio / camera.focalLength;
  const float verticalScale = 1.0F / camera.focalLength;
  const std::array<Plane, 5> planes = {
    Plane{camera.forward, -camera.nearPlane},
    Plane{camera.right + camera.forward * horizontalScale, 0.0F},
    Plane{camera.forward * horizontalScale - camera.right, 0.0F},
    Plane{camera.up + camera.forward * verticalScale, 0.0F},
    Plane{camera.forward * verticalScale - camera.up, 0.0F},
  };

  scratch.nodeStack.reserve(visibility.nodes.size());
  scratch.nodeStack.push_back(0U);
  while (!scratch.nodeStack.empty()) {
    const std::uint32_t nodeIndex = scratch.nodeStack.back();
    scratch.nodeStack.pop_back();
    const WorldVisibilityNode& node = visibility.nodes[nodeIndex];
    ++scratch.testedNodes;
    const FrustumClassification classification =
      classifyFrustum(node.bounds, planes, camera.position);
    if (classification == FrustumClassification::Outside) {
      continue;
    }
    if (
      classification == FrustumClassification::Intersecting &&
      !node.leaf()
    ) {
      // Push right first so traversal order remains deterministic and left-first.
      scratch.nodeStack.push_back(node.rightChild);
      scratch.nodeStack.push_back(node.leftChild);
      continue;
    }
    // Fully-inside internal nodes accept their contiguous DFS reference slice;
    // intersecting leaves remain conservative and accept the whole leaf.
    for (std::uint32_t offset = 0; offset < node.chunkReferenceCount; ++offset) {
      const std::uint32_t chunkIndex =
        visibility.nodeChunkIndices[node.firstChunkReference + offset];
      if (scratch.visibleChunks[chunkIndex] == 0U) {
        scratch.visibleChunks[chunkIndex] = 1U;
        ++scratch.visibleChunkCount;
      }
    }
  }
}

} // namespace lg
