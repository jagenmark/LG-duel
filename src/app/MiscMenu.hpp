#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lg {

class ConsoleSystem;

enum class MiscMenuRow : std::size_t {
  WeaponPosition = 0U,
  CollisionDebug,
  GroundDebug,
  LagCompensation,
  OutlineMask,
  FpsHud,
  FpsTitleBar,
  FrameStats,
  RendererPerformance,
  NetGraph,
  Close,
  Count,
};

struct MiscMenuItem {
  std::string label;
  std::string value;
  std::string description;
  bool command = false;
};

[[nodiscard]] std::vector<MiscMenuItem>
miscMenuItems(const ConsoleSystem &console);

[[nodiscard]] bool adjustMiscMenuValue(ConsoleSystem &console, MiscMenuRow row,
                                       int direction);

} // namespace lg
