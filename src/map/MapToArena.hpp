#pragma once

#include "map/MapDocument.hpp"
#include "sim/Arena.hpp"

namespace lg {

[[nodiscard]] ArenaLoadResult convertMapDocumentToArena(const MapDocument& document);
[[nodiscard]] ArenaLoadResult loadArenaFromMapText(std::string_view text);

} // namespace lg
