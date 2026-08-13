#pragma once

#include <cstddef>

namespace lg {

struct HudRenderState;
struct ServerSnapshot;

void populateScoreboard(
  HudRenderState& hud,
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
);

void populateFreeForAllStanding(
  HudRenderState& hud,
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
);

} // namespace lg
