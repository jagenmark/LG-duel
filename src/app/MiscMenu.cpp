#include "app/MiscMenu.hpp"

#include "console/ConsoleSystem.hpp"

#include <array>
#include <string>

namespace lg {
namespace {

[[nodiscard]] int wrappedValue(int value, int direction, int count) {
  const int offset = direction < 0 ? -1 : 1;
  return (value + offset + count) % count;
}

[[nodiscard]] std::string onOff(bool value) { return value ? "On" : "Off"; }

void setInt(ConsoleSystem &console, const char *name, int value) {
  (void)console.execute("set " + std::string(name) + ' ' +
                        std::to_string(value));
}

void setBool(ConsoleSystem &console, const char *name, bool value) {
  setInt(console, name, value ? 1 : 0);
}

[[nodiscard]] int rendererPerformanceMode(const ConsoleSystem &console) {
  if (!console.getBool("r_perf")) {
    return 0;
  }
  return console.getBool("r_perf_detail") ? 2 : 1;
}

void setRendererPerformanceMode(ConsoleSystem &console, int mode) {
  setBool(console, "r_perf", mode != 0);
  setBool(console, "r_perf_detail", mode == 2);
}

} // namespace

void syncMiscMenuDraft(MiscMenuDraft &draft, const ConsoleSystem &console) {
  draft.pendingDamageIndicator = console.getBool("r_damage_indicator");
  draft.originalDamageIndicator = draft.pendingDamageIndicator;
}

bool miscMenuDraftChanged(const MiscMenuDraft &draft) {
  return draft.pendingDamageIndicator != draft.originalDamageIndicator;
}

void resetMiscMenuDraft(MiscMenuDraft &draft) {
  draft.pendingDamageIndicator = true;
}

void revertMiscMenuDraft(MiscMenuDraft &draft) {
  draft.pendingDamageIndicator = draft.originalDamageIndicator;
}

bool applyMiscMenuDraft(ConsoleSystem &console, MiscMenuDraft &draft) {
  if (!miscMenuDraftChanged(draft)) {
    return false;
  }
  setBool(console, "r_damage_indicator", draft.pendingDamageIndicator);
  draft.originalDamageIndicator = draft.pendingDamageIndicator;
  return true;
}

std::vector<MiscMenuItem> miscMenuItems(const ConsoleSystem &console) {
  MiscMenuDraft draft;
  syncMiscMenuDraft(draft, console);
  return miscMenuItems(console, draft);
}

std::vector<MiscMenuItem> miscMenuItems(const ConsoleSystem &console,
                                        const MiscMenuDraft &draft) {
  constexpr std::array<const char *, 3> weaponPositions = {
      "Center",
      "Right",
      "Left",
  };
  constexpr std::array<const char *, 6> collisionModes = {
      "Off", "All", "Visible solids", "Player clip", "Weapon clip", "Triggers",
  };
  constexpr std::array<const char *, 3> groundDebugModes = {
      "Off",
      "Basic",
      "Detailed",
  };
  constexpr std::array<const char *, 3> performanceModes = {
      "Off",
      "Summary",
      "Detailed",
  };
  constexpr std::array<const char *, 3> netGraphModes = {
      "Off",
      "Compact",
      "Expanded",
  };

  return {
      {
          "Weapon position",
          weaponPositions[static_cast<std::size_t>(
              console.getInt("r_weapon_pos"))],
          "Moves the local first-person weapon between center, right, and "
          "left.",
          false,
      },
      {
          "Collision debug",
          collisionModes[static_cast<std::size_t>(
              console.getInt("r_show_collision"))],
          "Shows all collision, one collision group, or no collision overlay.",
          false,
      },
      {
          "Ground debug",
          groundDebugModes[static_cast<std::size_t>(
              console.getInt("cg_ground_debug"))],
          "Shows ground and slope data on the HUD.",
          false,
      },
      {
          "Lag compensation",
          onOff(console.getBool("cl_show_lagcomp")),
          "Shows rewind data when lag compensation runs.",
          false,
      },
      {
          "Outline mask",
          onOff(console.getBool("r_player_outline_debug_mask")),
          "Shows the raw player-outline mask in native outline mode.",
          false,
      },
      {
          "FPS counter",
          onOff(console.getBool("cl_showfps")),
          "Shows the FPS counter on the HUD.",
          false,
      },
      {
          "FPS title bar",
          onOff(console.getBool("cl_showfps_titlebar")),
          "Shows FPS, frame time, and the render back end in the title bar.",
          false,
      },
      {
          "Frame stats title bar",
          onOff(console.getBool("cl_show_frame_stats")),
          "Adds detailed frame pacing data to the title bar.",
          false,
      },
      {
          "Renderer performance",
          performanceModes[static_cast<std::size_t>(
              rendererPerformanceMode(console))],
          "Shows a short or detailed render performance HUD.",
          false,
      },
      {
          "Netgraph",
          netGraphModes[static_cast<std::size_t>(
              console.getInt("cl_netgraph"))],
          "Shows a compact network HUD or its full history graph.",
          false,
      },
      {
          "Damage direction",
          onOff(draft.pendingDamageIndicator),
          "Shows a restrained warning arc at the screen edge when damage comes from a direction.",
          false,
          miscMenuDraftChanged(draft),
      },
      {
          "Reset damage draft",
          "Default (On)",
          "Resets the damage-direction toggle to its default On state.",
          true,
      },
      {
          "Apply changes",
          miscMenuDraftChanged(draft) ? "Enter" : "No changes",
          "Applies the changed damage-direction setting and saves it.",
          true,
          miscMenuDraftChanged(draft),
      },
      {
          "Close / Revert draft",
          "Esc",
          "Closes this menu and restores the last applied damage setting.",
          true,
      },
  };
}

bool adjustMiscMenuValue(ConsoleSystem &console, MiscMenuRow row,
                         int direction, MiscMenuDraft &draft) {
  if (direction == 0 || row == MiscMenuRow::DamageIndicatorReset ||
      row == MiscMenuRow::DamageIndicatorApply || row == MiscMenuRow::Close ||
      row == MiscMenuRow::Count) {
    return false;
  }
  switch (row) {
  case MiscMenuRow::WeaponPosition:
    setInt(console, "r_weapon_pos",
           wrappedValue(console.getInt("r_weapon_pos"), direction, 3));
    return true;
  case MiscMenuRow::CollisionDebug:
    setInt(console, "r_show_collision",
           wrappedValue(console.getInt("r_show_collision"), direction, 6));
    return true;
  case MiscMenuRow::GroundDebug:
    setInt(console, "cg_ground_debug",
           wrappedValue(console.getInt("cg_ground_debug"), direction, 3));
    return true;
  case MiscMenuRow::LagCompensation:
    setBool(console, "cl_show_lagcomp", !console.getBool("cl_show_lagcomp"));
    return true;
  case MiscMenuRow::OutlineMask:
    setBool(console, "r_player_outline_debug_mask",
            !console.getBool("r_player_outline_debug_mask"));
    return true;
  case MiscMenuRow::FpsHud:
    setBool(console, "cl_showfps", !console.getBool("cl_showfps"));
    return true;
  case MiscMenuRow::FpsTitleBar:
    setBool(console, "cl_showfps_titlebar",
            !console.getBool("cl_showfps_titlebar"));
    return true;
  case MiscMenuRow::FrameStats:
    setBool(console, "cl_show_frame_stats",
            !console.getBool("cl_show_frame_stats"));
    return true;
  case MiscMenuRow::RendererPerformance: {
    const int mode =
        wrappedValue(rendererPerformanceMode(console), direction, 3);
    setRendererPerformanceMode(console, mode);
    return true;
  }
  case MiscMenuRow::NetGraph:
    setInt(console, "cl_netgraph",
           wrappedValue(console.getInt("cl_netgraph"), direction, 3));
    return true;
  case MiscMenuRow::DamageIndicator:
    draft.pendingDamageIndicator = !draft.pendingDamageIndicator;
    return true;
  case MiscMenuRow::DamageIndicatorReset:
  case MiscMenuRow::DamageIndicatorApply:
  case MiscMenuRow::Close:
  case MiscMenuRow::Count:
    return false;
  }
  return false;
}

} // namespace lg
