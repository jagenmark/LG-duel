#include "render/WorldVisibility.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::WorldVisibilityTriangle triangleAt(
  lg::Vec3 center,
  std::uint32_t materialId = 1U
) {
  return {
    center + lg::Vec3{0.0F, -0.1F, -0.1F},
    center + lg::Vec3{0.0F, 0.1F, -0.1F},
    center + lg::Vec3{0.0F, 0.0F, 0.1F},
    materialId,
  };
}

bool sameVisibility(
  const lg::WorldVisibility& lhs,
  const lg::WorldVisibility& rhs
) {
  if (
    lhs.nodes.size() != rhs.nodes.size() ||
    lhs.chunks.size() != rhs.chunks.size() ||
    lhs.nodeChunkIndices != rhs.nodeChunkIndices ||
    lhs.orderedTriangleIndices != rhs.orderedTriangleIndices
  ) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.chunks.size(); ++index) {
    const lg::WorldVisibilityChunk& a = lhs.chunks[index];
    const lg::WorldVisibilityChunk& b = rhs.chunks[index];
    if (
      a.materialId != b.materialId ||
      a.firstTriangle != b.firstTriangle ||
      a.triangleCount != b.triangleCount
    ) {
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
  int failures = 0;

  const lg::WorldVisibility empty = lg::buildWorldVisibility({});
  failures += expect(
    empty.nodes.empty() && empty.chunks.empty() &&
      empty.orderedTriangleIndices.empty(),
    "an empty world should build an empty visibility tree"
  );

  std::vector<lg::WorldVisibilityTriangle> packedTriangles;
  packedTriangles.reserve(600U);
  for (std::uint32_t index = 0; index < 600U; ++index) {
    packedTriangles.push_back(triangleAt(
      {10.0F + static_cast<float>(index) * 0.01F, 0.0F, 0.0F},
      index % 2U == 0U ? 9U : 3U
    ));
  }
  const lg::WorldVisibility packed = lg::buildWorldVisibility(packedTriangles);
  const lg::WorldVisibility packedAgain =
    lg::buildWorldVisibility(packedTriangles);
  failures += expect(
    sameVisibility(packed, packedAgain),
    "BVH construction and triangle packing should be deterministic"
  );
  failures += expect(
    packed.orderedTriangleIndices.size() == packedTriangles.size(),
    "material-major packing should retain every source triangle exactly once"
  );
  std::vector<std::uint8_t> sourceSeen(packedTriangles.size(), 0U);
  for (std::uint32_t sourceIndex : packed.orderedTriangleIndices) {
    if (sourceIndex < sourceSeen.size()) {
      ++sourceSeen[sourceIndex];
    }
  }
  bool uniqueCoverage = true;
  for (std::uint8_t count : sourceSeen) {
    uniqueCoverage = uniqueCoverage && count == 1U;
  }
  failures += expect(
    uniqueCoverage,
    "packed triangle indices should be a permutation of the source stream"
  );
  bool materialMajor = true;
  bool boundedChunks = true;
  for (std::size_t index = 0; index < packed.chunks.size(); ++index) {
    boundedChunks = boundedChunks &&
      packed.chunks[index].triangleCount <=
        lg::kWorldVisibilityMaxTrianglesPerLeaf;
    if (index > 0U) {
      materialMajor = materialMajor &&
        packed.chunks[index - 1U].materialId <= packed.chunks[index].materialId;
    }
  }
  failures += expect(
    materialMajor && boundedChunks,
    "chunks should be material-major and never exceed the leaf triangle cap"
  );

  std::vector<lg::WorldVisibilityTriangle> splitTriangles;
  splitTriangles.reserve(512U);
  for (std::uint32_t index = 0; index < 256U; ++index) {
    splitTriangles.push_back(triangleAt({10.0F, static_cast<float>(index) * 0.001F, 0.0F}));
    splitTriangles.push_back(triangleAt({-10.0F, static_cast<float>(index) * 0.001F, 0.0F}));
  }
  const lg::WorldVisibility split = lg::buildWorldVisibility(splitTriangles);
  lg::WorldVisibilityQueryScratch scratch;
  const lg::PerspectiveCamera forwardCamera =
    lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 16.0F / 9.0F);
  lg::queryWorldVisibility(packed, forwardCamera, scratch);
  failures += expect(
    packed.nodes.size() > 1U && scratch.testedNodes == 1U &&
      scratch.visibleChunkCount == packed.chunks.size(),
    "a fully visible root should accept its subtree without visiting children"
  );
  lg::queryWorldVisibility(split, forwardCamera, scratch);
  failures += expect(
    split.nodes.size() == 3U && scratch.testedNodes == 3U &&
      scratch.visibleChunkCount == 1U,
    "the BVH should reject the spatial leaf wholly behind the camera"
  );

  const std::vector<lg::WorldVisibilityTriangle> behindOnly = {
    triangleAt({-2.0F, 0.0F, 0.0F}),
  };
  lg::queryWorldVisibility(
    lg::buildWorldVisibility(behindOnly),
    forwardCamera,
    scratch
  );
  failures += expect(
    scratch.visibleChunkCount == 0U,
    "a chunk wholly behind the near plane should be culled"
  );

  const std::vector<lg::WorldVisibilityTriangle> sideOnly = {
    triangleAt({4.0F, 20.0F, 0.0F}),
  };
  lg::queryWorldVisibility(
    lg::buildWorldVisibility(sideOnly),
    forwardCamera,
    scratch
  );
  failures += expect(
    scratch.visibleChunkCount == 0U,
    "a chunk wholly outside a side plane should be culled"
  );

  const std::vector<lg::WorldVisibilityTriangle> cameraStraddling = {
    {
      {-0.1F, -0.2F, -0.2F},
      {0.2F, 0.2F, -0.2F},
      {0.2F, 0.0F, 0.2F},
      1U,
    },
  };
  const lg::WorldVisibility straddling =
    lg::buildWorldVisibility(cameraStraddling);
  lg::queryWorldVisibility(straddling, forwardCamera, scratch);
  failures += expect(
    scratch.visibleChunkCount == 1U,
    "a chunk crossing the camera near plane must remain visible"
  );

  lg::PerspectiveCamera translatedCamera = forwardCamera;
  translatedCamera.position = {927.0F, -677.637F, 0.0F};
  translatedCamera.focalLength = 1.0F;
  translatedCamera.aspectRatio = 3.73205F;
  const std::vector<lg::WorldVisibilityTriangle> translatedBoundary = {
    {
      {927.2F, -676.8906F, -0.001F},
      {927.2F, -676.8905F, 0.001F},
      {927.201F, -676.89055F, 0.0F},
      1U,
    },
  };
  lg::queryWorldVisibility(
    lg::buildWorldVisibility(translatedBoundary),
    translatedCamera,
    scratch
  );
  failures += expect(
    scratch.visibleChunkCount == 1U,
    "camera-relative plane tests should retain translated boundary geometry"
  );

  lg::PerspectiveCamera invalidCamera = forwardCamera;
  invalidCamera.aspectRatio = 0.0F;
  lg::queryWorldVisibility(split, invalidCamera, scratch);
  failures += expect(
    scratch.visibleChunkCount == split.chunks.size(),
    "invalid projection state should fail open instead of hiding world geometry"
  );

  if (failures == 0) {
    std::cout << "World visibility tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
