#include "sim/GameplayCvars.hpp"

#include "shared/Constants.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace lg {

void registerGameplayCvars(ConsoleSystem& console, CvarFlag flags) {
  console.registerCvar({"g_accel", "Authoritative ground acceleration; affects time to reach g_maxspeed.", 10.0F, flags, 0.0F, 1000.0F, "10"});
  console.registerCvar({"g_airaccel", "Authoritative air acceleration.", 1.0F, flags, 0.0F, 1000.0F, "1"});
  console.registerCvar({"g_aircontrol", "Enable QuakeWorld-style air control while holding forward.", false, flags, {}, {}});
  console.registerCvar({"g_friction", "Authoritative grounded coasting friction; release movement to evaluate it.", 6.0F, flags, 0.0F, 100.0F, "6"});
  console.registerCvar({"g_stopspeed", "Minimum speed used when calculating grounded friction.", 2.5F, flags, 0.0F, 100.0F, "2.5 (pm_stopspeed 100)"});
  console.registerCvar({"g_maxspeed", "Authoritative sustained ground and air speed cap.", 8.0F, flags, 0.1F, 100.0F, "8 (g_speed 320)"});
  console.registerCvar({"g_lg_knockback", "Authoritative LG knockback magnitude per second.", 1000.0F, flags, 0.0F, kMaxLightningKnockback, "1000"});
  console.registerCvar({"g_lg_fire_hz", "Authoritative lightning gun damage instances per second.", 20.0F, flags, kMinLightningFireHz, kMaxLightningFireHz});
  console.registerCvar({"g_rl_knockback", "Authoritative rocket knockback on the Q3 g_knockback scale.", 1000.0F, flags, 0.0F, kMaxRocketKnockback, "1000"});
  console.registerCvar({"g_knockback_time_ms", "Authoritative Q3-style knockback movement timer in milliseconds; 0 disables the special movement state.", 100, flags, 0.0F, 250.0F, "100"});
  console.registerCvar({"g_sg_damage", "Authoritative shotgun damage per pellet.", 5, flags, 1.0F, 500.0F});
  console.registerCvar({"g_mg_damage", "Authoritative machine gun damage per shot.", 5, flags, 1.0F, 500.0F});
  console.registerCvar({"g_lg_damage", "Authoritative lightning gun damage per second, distributed over g_lg_fire_hz instances.", 120, flags, 1.0F, 500.0F});
  console.registerCvar({"g_rg_damage", "Authoritative railgun damage per shot.", 80, flags, 1.0F, 500.0F});
  console.registerCvar({"g_rl_damage", "Authoritative rocket/grenade direct and max splash damage.", 100, flags, 1.0F, 500.0F});
  console.registerCvar({"g_pg_damage", "Authoritative plasma gun direct hit damage.", 20, flags, 1.0F, 500.0F});
  console.registerCvar({"g_vampirism", "Heal by this multiple of authoritative damage dealt.", 0.0F, flags, 0.0F, 2.0F});
  console.registerCvar({"g_selfdamage", "Percent of self splash damage you take.", 100.0F, flags, 0.0F, 100.0F});
  console.registerCvar({"g_healthamount", "Authoritative player health amount on spawn and round start.", 100, flags, 1.0F, 100000.0F});
  console.registerCvar({"g_weaponswitching", "Authoritative weapon switching rules: ql, cpma, or crazy.", std::string("crazy"), flags});
  console.registerCvar({"g_flight", "Enable unrestricted flight symmetrically for both players.", false, flags, {}, {}});
  console.registerCvar({"g_flightaccel", "Authoritative flight thrust acceleration.", 32.0F, flags, 0.0F, 1000.0F});
  console.registerCvar({"g_flightmaxspeed", "Authoritative maximum flight speed.", 12.0F, flags, 0.1F, 100.0F});
  console.registerCvar({"g_flightdamping", "Authoritative flight velocity damping.", 2.0F, flags, 0.0F, 100.0F});
  console.registerCvar({"g_playersize_xy", "Authoritative player X/Y radius scale.", 1.0F, flags, 0.5F, 3.0F});
  console.registerCvar({"g_playersize_z", "Authoritative player height scale.", 1.0F, flags, 0.5F, 3.0F});
}

MovementTuning movementTuningFromCvars(const ConsoleSystem& console) {
  MovementTuning tuning;
  tuning.flightEnabled = console.getBool("g_flight");
  tuning.groundAcceleration = console.getFloat("g_accel");
  tuning.airAcceleration = console.getFloat("g_airaccel");
  tuning.airControlEnabled = console.getBool("g_aircontrol");
  tuning.groundFriction = console.getFloat("g_friction");
  tuning.stopSpeed = console.getFloat("g_stopspeed");
  tuning.maxGroundSpeed = console.getFloat("g_maxspeed");
  tuning.maxAirSpeed = tuning.maxGroundSpeed;
  tuning.flightAcceleration = console.getFloat("g_flightaccel");
  tuning.maxFlightSpeed = console.getFloat("g_flightmaxspeed");
  tuning.flightDamping = console.getFloat("g_flightdamping");
  tuning.flightGravityCancel = 1.0F;
  return tuning;
}

WeaponDamageTuning weaponDamageTuningFromCvars(const ConsoleSystem& console) {
  return {
    console.getInt("g_sg_damage"),
    console.getInt("g_mg_damage"),
    console.getInt("g_lg_damage"),
    console.getInt("g_rg_damage"),
    console.getInt("g_rl_damage"),
    console.getInt("g_pg_damage"),
  };
}

std::uint8_t selfDamagePercentFromCvars(const ConsoleSystem& console) {
  return static_cast<std::uint8_t>(
    std::clamp(console.getFloat("g_selfdamage"), 0.0F, 100.0F) + 0.5F
  );
}

std::int32_t healthAmountFromCvars(const ConsoleSystem& console) {
  return std::clamp(console.getInt("g_healthamount"), 1, 100000);
}

std::int32_t knockbackTimeMsFromCvars(const ConsoleSystem& console) {
  return std::clamp(console.getInt("g_knockback_time_ms"), 0, 250);
}

std::uint16_t knockbackTimeMsToTicks(std::int32_t milliseconds) {
  const int clampedMilliseconds = std::clamp(milliseconds, 0, 250);
  if (clampedMilliseconds == 0) {
    return 0;
  }
  return static_cast<std::uint16_t>(
    std::ceil(
      (static_cast<float>(clampedMilliseconds) * kFixedTickRate) / 1000.0F
    )
  );
}

WeaponSwitchingMode weaponSwitchingModeFromCvars(const ConsoleSystem& console) {
  std::string value = console.getString("g_weaponswitching");
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  if (value == "ql" || value == "0") {
    return WeaponSwitchingMode::Ql;
  }
  if (value == "cpma" || value == "1") {
    return WeaponSwitchingMode::Cpma;
  }
  return WeaponSwitchingMode::Crazy;
}

} // namespace lg
