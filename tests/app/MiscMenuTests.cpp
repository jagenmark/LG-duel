#include "app/ClientCvars.hpp"
#include "app/MiscMenu.hpp"
#include "console/ConsoleSystem.hpp"

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

  std::vector<lg::MiscMenuItem> items = lg::miscMenuItems(console);
  failures +=
      expect(items.size() == static_cast<std::size_t>(lg::MiscMenuRow::Count) &&
                 items.front().label == "Weapon position" &&
                 items.front().value == "Center" && items.back().command,
             "misc menu should expose the approved tools and a close row");

  (void)lg::adjustMiscMenuValue(console, lg::MiscMenuRow::WeaponPosition, -1);
  failures += expect(console.getInt("r_weapon_pos") == 2,
                     "weapon position should wrap left from center");

  (void)lg::adjustMiscMenuValue(console, lg::MiscMenuRow::CollisionDebug, -1);
  failures +=
      expect(console.getInt("r_show_collision") == 5,
             "collision debug should wrap through every supported view");

  (void)lg::adjustMiscMenuValue(console, lg::MiscMenuRow::RendererPerformance,
                                1);
  failures +=
      expect(console.getBool("r_perf") && !console.getBool("r_perf_detail"),
             "renderer performance should enter summary mode");
  (void)lg::adjustMiscMenuValue(console, lg::MiscMenuRow::RendererPerformance,
                                1);
  failures +=
      expect(console.getBool("r_perf") && console.getBool("r_perf_detail"),
             "renderer performance should enter detailed mode");
  (void)lg::adjustMiscMenuValue(console, lg::MiscMenuRow::RendererPerformance,
                                1);
  failures +=
      expect(!console.getBool("r_perf") && !console.getBool("r_perf_detail"),
             "renderer performance should wrap back to off");

  (void)lg::adjustMiscMenuValue(console, lg::MiscMenuRow::NetGraph, -1);
  failures += expect(console.getInt("cl_netgraph") == 2,
                     "netgraph should wrap from off to expanded");
  failures += expect(
      console.getBool("r_damage_indicator"),
      "directional damage should default on");
  (void)lg::adjustMiscMenuValue(console, lg::MiscMenuRow::DamageIndicator, 1);
  failures += expect(
      !console.getBool("r_damage_indicator"),
      "damage direction should toggle off from the F11 menu");
  items = lg::miscMenuItems(console);
  failures += expect(
      items[static_cast<std::size_t>(lg::MiscMenuRow::DamageIndicator)].label ==
          "Damage direction" &&
        items[static_cast<std::size_t>(lg::MiscMenuRow::DamageIndicator)].value ==
          "Off" &&
        !items[static_cast<std::size_t>(lg::MiscMenuRow::DamageIndicator)]
           .description.empty(),
      "F11 should show the live damage-direction toggle and help text");
  (void)lg::adjustMiscMenuValue(console, lg::MiscMenuRow::DamageIndicator, -1);
  failures += expect(
      console.getBool("r_damage_indicator"),
      "damage direction should toggle back on from the F11 menu");
  failures +=
      expect(!lg::adjustMiscMenuValue(console, lg::MiscMenuRow::Close, 1),
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
