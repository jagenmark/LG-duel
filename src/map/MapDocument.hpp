#pragma once

#include "shared/Math.hpp"

#include <array>
#include <string>
#include <vector>

namespace lg {

struct MapProperty {
  std::string key;
  std::string value;
  int line = 0;
};

struct MapFace {
  std::array<Vec3, 3> points = {};
  std::string material;
  float xOffset = 0.0F;
  float yOffset = 0.0F;
  float rotationDegrees = 0.0F;
  float xScale = 1.0F;
  float yScale = 1.0F;
  int line = 0;
};

struct MapBrush {
  std::vector<MapFace> faces;
  int line = 0;
};

struct MapEntity {
  std::vector<MapProperty> properties;
  std::vector<MapBrush> brushes;
  int line = 0;

  [[nodiscard]] const std::string* property(std::string_view key) const;
};

struct MapDocument {
  std::vector<MapEntity> entities;
};

} // namespace lg
