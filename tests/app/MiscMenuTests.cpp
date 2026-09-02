#include "app/ClientCvars.hpp"
#include "app/MiscMenu.hpp"
#include "console/ConsoleSystem.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

int expect(bool condition, const char *message) {
  if (condition) {
    return 0;
  }
  std::cerr << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;
  lg::ConsoleSystem console;
  lg::registerClientCvars(console);

  lg::MiscMenuDraft draft;
  lg::syncMiscMenuDraft(draft, console);
  std::vector<lg::MiscMenuItem> items = lg::miscMenuItems(console, draft);
  failures +=
      expect(items.size() == static_cast<std::size_t>(lg::MiscMenuRow::Count) &&
                 items.front().label == "Weapon position" &&
                 items.front().value == "Center" && items.back().command,
             "misc menu should expose the approved tools and draft controls");

  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::WeaponPosition, -1, draft);
  failures += expect(console.getInt("r_weapon_pos") == 2,
                     "weapon position should wrap left from center");

  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::HealthStyle, -1, draft);
  failures += expect(
      console.getInt("cl_health_style") == 5 &&
        lg::miscMenuItems(console, draft)[static_cast<std::size_t>(
          lg::MiscMenuRow::HealthStyle)].value == "Outlined art",
      "health HUD should offer and wrap through all six layouts"
  );

  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::CollisionDebug, -1, draft);
  failures +=
      expect(console.getInt("r_show_collision") == 5,
             "collision debug should wrap through every supported view");

  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::RendererPerformance, 1, draft);
  failures +=
      expect(console.getBool("r_perf") && !console.getBool("r_perf_detail"),
             "renderer performance should enter summary mode");
  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::RendererPerformance, 1, draft);
  failures +=
      expect(console.getBool("r_perf") && console.getBool("r_perf_detail"),
             "renderer performance should enter detailed mode");
  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::RendererPerformance, 1, draft);
  failures +=
      expect(!console.getBool("r_perf") && !console.getBool("r_perf_detail"),
             "renderer performance should wrap back to off");

  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::NetGraph, -1, draft);
  failures += expect(console.getInt("cl_netgraph") == 2,
                     "netgraph should wrap from off to expanded");
  failures += expect(
      console.getBool("r_damage_indicator"),
      "directional damage should default on");

  failures += expect(
      !console.getBool("s_play_unfocused") &&
        items[static_cast<std::size_t>(
          lg::MiscMenuRow::SoundWhenUnfocused)].label ==
          "Sound when unfocused" &&
        items[static_cast<std::size_t>(
          lg::MiscMenuRow::SoundWhenUnfocused)].value == "Off",
      "F11 should show sound while unfocused as off by default");
  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::SoundWhenUnfocused, 1, draft);
  failures += expect(
      console.getBool("s_play_unfocused") &&
        lg::miscMenuItems(console, draft)[static_cast<std::size_t>(
          lg::MiscMenuRow::SoundWhenUnfocused)].value == "On",
      "F11 should toggle sound while unfocused");

  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::DamageIndicator, 1, draft);
  failures += expect(
      console.getBool("r_damage_indicator") &&
          !draft.pendingDamageIndicator &&
          lg::miscMenuDraftChanged(draft),
      "damage direction should change only in the F11 draft");
  items = lg::miscMenuItems(console, draft);
  failures += expect(
      items[static_cast<std::size_t>(lg::MiscMenuRow::DamageIndicator)].label ==
          "Damage direction" &&
        items[static_cast<std::size_t>(lg::MiscMenuRow::DamageIndicator)].value ==
          "Off" &&
        items[static_cast<std::size_t>(lg::MiscMenuRow::DamageIndicator)].changed &&
        items[static_cast<std::size_t>(lg::MiscMenuRow::DamageIndicatorApply)].value ==
          "Enter" &&
        !items[static_cast<std::size_t>(lg::MiscMenuRow::DamageIndicator)]
           .description.empty(),
      "F11 should show the pending damage toggle, change marker, and help text");

  failures += expect(
      lg::applyMiscMenuDraft(console, draft) &&
          !console.getBool("r_damage_indicator") &&
          !lg::miscMenuDraftChanged(draft),
      "Apply should commit the pending damage toggle and clear its change state");
  const std::vector<std::string> archived = console.archivedConfigLines();
  failures += expect(
      std::find(archived.begin(), archived.end(),
                "set r_damage_indicator 0") != archived.end(),
      "Apply should keep the archived damage toggle for config saving");
  failures += expect(
      !lg::applyMiscMenuDraft(console, draft),
      "Apply should not rewrite the cvar when the draft has no changes");

  lg::resetMiscMenuDraft(draft);
  failures += expect(
      draft.pendingDamageIndicator && lg::miscMenuDraftChanged(draft),
      "Reset should put the damage toggle at its default On value");
  lg::revertMiscMenuDraft(draft);
  failures += expect(
      !draft.pendingDamageIndicator && !lg::miscMenuDraftChanged(draft),
      "Close / Revert should discard an unapplied damage change");

  (void)lg::adjustMiscMenuValue(
      console, lg::MiscMenuRow::DamageIndicator, 1, draft);
  failures += expect(
      draft.pendingDamageIndicator && !console.getBool("r_damage_indicator"),
      "a canceled draft should leave the applied cvar unchanged");
  lg::revertMiscMenuDraft(draft);
  failures += expect(
      !draft.pendingDamageIndicator,
      "reverting after a second edit should restore the applied value");

  failures +=
      expect(!lg::adjustMiscMenuValue(
                 console, lg::MiscMenuRow::Close, 1, draft),
             "close row should not change a cvar");

  items = lg::miscMenuItems(console);
  failures +=
      expect(items[static_cast<std::size_t>(lg::MiscMenuRow::WeaponPosition)]
                     .value == "Left" &&
                 items[static_cast<std::size_t>(lg::MiscMenuRow::NetGraph)]
                         .value == "Expanded",
             "misc menu labels should reflect live cvar values");

  return failures == 0 ? 0 : 1;
}
