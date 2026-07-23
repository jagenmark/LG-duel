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
  console.registerCvar({"g_dash_targetspeed", "Authoritative dash target speed along the locked input direction.", 11.5F, flags, 0.0F, 100.0F, "11.5 (460 UPS)"});
  console.registerCvar({"g_dash_maxspeed", "Authoritative cap for speed created by dash without clamping existing skilled speed.", 12.5F, flags, 0.0F, 100.0F, "12.5 (500 UPS)"});
  console.registerCvar({"g_dash_accel", "Authoritative dash acceleration in project units per second.", 200.0F, flags, 0.0F, 1000.0F, "200 (8000 UPS/s)"});
  console.registerCvar({"g_dash_duration", "Authoritative dash active acceleration window in seconds.", 0.10F, flags, 0.0F, 2.0F});
  console.registerCvar({"g_dash_cooldown", "Authoritative dash cooldown after dash start in seconds.", 0.85F, flags, 0.0F, 10.0F});
  console.registerCvar({"g_dash_groundhop", "Authoritative vertical velocity floor for a grounded dash hop.", 3.25F, flags, 0.0F, 100.0F, "3.25 (130 UPS)"});
  console.registerCvar({"g_dash_airhop", "Authoritative vertical velocity floor for an airborne dash correction.", 1.875F, flags, 0.0F, 100.0F, "1.875 (75 UPS)"});
  console.registerCvar({"g_lg_knockback", "Authoritative LG knockback magnitude per second.", 1000.0F, flags, 0.0F, kMaxLightningKnockback, "1000"});
  console.registerCvar({"g_lg_fire_hz", "Authoritative lightning/freeze gun beam instances per second.", 20.0F, flags, kMinLightningFireHz, kMaxLightningFireHz});
  console.registerCvar({"g_rl_knockback", "Authoritative rocket knockback on the Q3 g_knockback scale.", 1000.0F, flags, 0.0F, kMaxRocketKnockback, "1000"});
  console.registerCvar({"g_knockback_time_ms", "Authoritative Q3-style knockback movement timer in milliseconds; 0 disables the special movement state.", 100, flags, 0.0F, 250.0F, "100"});
  console.registerCvar({"g_sg_damage", "Authoritative shotgun damage per pellet.", 5, flags, 1.0F, 500.0F});
  console.registerCvar({"g_mg_damage", "Authoritative machine gun damage per shot.", 5, flags, 1.0F, 500.0F});
  console.registerCvar({"g_lg_damage", "Authoritative lightning gun damage per second, distributed over g_lg_fire_hz instances.", 120, flags, 1.0F, 500.0F});
  console.registerCvar({"g_fg_damage", "Authoritative freeze gun damage per second, distributed over g_lg_fire_hz instances.", 120, flags, 1.0F, 500.0F});
  console.registerCvar({"g_rg_damage", "Authoritative Sniper Rifle damage before scoped charge.", 50, flags, 1.0F, 500.0F});
  console.registerCvar({"g_rl_damage", "Authoritative rocket/grenade direct and max splash damage.", 100, flags, 1.0F, 500.0F});
  console.registerCvar({"g_pg_damage", "Authoritative plasma gun direct hit damage.", 20, flags, 1.0F, 500.0F});
  console.registerCvar({"g_vampirism", "Heal by this multiple of authoritative damage dealt.", 0.0F, flags, 0.0F, 2.0F});
  console.registerCvar({"g_selfdamage", "Percent of self splash damage you take.", 100.0F, flags, 0.0F, 100.0F});
  console.registerCvar({"g_healthamount", "Authoritative player health amount on spawn and round start.", 100, flags, 1.0F, 100000.0F});
  console.registerCvar({"g_infiniteammo", "Use infinite weapon ammo; 0 consumes per-weapon spawn ammo from balance.cfg.", true, flags, {}, {}});
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
  tuning.dashTargetSpeed = console.getFloat("g_dash_targetspeed");
  tuning.dashMaxSpeed = console.getFloat("g_dash_maxspeed");
  tuning.dashAcceleration = console.getFloat("g_dash_accel");
  tuning.dashDuration = console.getFloat("g_dash_duration");
  tuning.dashCooldown = console.getFloat("g_dash_cooldown");
  tuning.dashGroundHopVelocity = console.getFloat("g_dash_groundhop");
  tuning.dashAirHopVelocity = console.getFloat("g_dash_airhop");
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
    console.getInt("g_fg_damage"),
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

bool infiniteAmmoFromCvars(const ConsoleSystem& console) {
  return console.getBool("g_infiniteammo");
}

std::uint16_t knockbackTimeMsToTicks(std::int32_t milliseconds) {
  const int clampedMilliseconds = std::clamp(milliseconds, 0, 250);
  if (clampedMilliseconds == 0) {
    return 0;
  }
  // Round up so every positive duration affects at least one full fixed tick;
  // truncation would systematically shorten the configured movement window.
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
  // Registration constrains normal values; Crazy is also the compatibility
  // fallback for its name/numeric alias and any legacy value reaching this layer.
  return WeaponSwitchingMode::Crazy;
}

} // namespace lg
