#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lg {

class ConsoleSystem;

enum class MiscMenuRow : std::size_t {
  WeaponPosition = 0U,
  HealthStyle,
  CollisionDebug,
  GroundDebug,
  LagCompensation,
  OutlineMask,
  FpsHud,
  FpsTitleBar,
  FrameStats,
  RendererPerformance,
  NetGraph,
  SoundWhenUnfocused,
  DamageIndicator,
  DamageIndicatorReset,
  DamageIndicatorApply,
  Close,
  Count,
};

struct MiscMenuDraft {
  bool pendingDamageIndicator = true;
  bool originalDamageIndicator = true;
};

struct MiscMenuItem {
  std::string label;
  std::string value;
  std::string description;
  bool command = false;
  bool changed = false;
};

[[nodiscard]] std::vector<MiscMenuItem>
miscMenuItems(const ConsoleSystem &console);

[[nodiscard]] std::vector<MiscMenuItem>
miscMenuItems(const ConsoleSystem &console, const MiscMenuDraft &draft);

void syncMiscMenuDraft(MiscMenuDraft &draft, const ConsoleSystem &console);

[[nodiscard]] bool miscMenuDraftChanged(const MiscMenuDraft &draft);

void resetMiscMenuDraft(MiscMenuDraft &draft);

void revertMiscMenuDraft(MiscMenuDraft &draft);

[[nodiscard]] bool applyMiscMenuDraft(ConsoleSystem &console,
                                      MiscMenuDraft &draft);

[[nodiscard]] bool adjustMiscMenuValue(ConsoleSystem &console, MiscMenuRow row,
                                       int direction, MiscMenuDraft &draft);

} // namespace lg
