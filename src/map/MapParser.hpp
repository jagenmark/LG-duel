#pragma once

#include "map/MapDocument.hpp"

#include <string>
#include <string_view>

namespace lg {

struct MapParseResult {
  MapDocument document = {};
  bool ok = false;
  std::string error;
};

[[nodiscard]] MapParseResult parseMapDocument(std::string_view text);

} // namespace lg
