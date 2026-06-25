#pragma once

#include "sim/Combat.hpp"
#include "sim/UserCommand.hpp"

namespace lg {

enum class WeaponFireAudioCue {
  None,
  Railgun,
  RocketLauncher,
  MachineGun,
  Shotgun,
  GrenadeLauncher,
  PlasmaGun,
};

struct WeaponFireAudioEvent {
  WeaponFireAudioCue cue = WeaponFireAudioCue::None;
  bool startsLocalRailCooldown = false;
  int localHitConfirmDamage = 0;
};

[[nodiscard]] bool sameWeaponFireEvent(
  const WeaponFireResult& lhs,
  const WeaponFireResult& rhs
);

[[nodiscard]] bool sameRocketExplosionEvent(
  const RocketExplosionResult& lhs,
  const RocketExplosionResult& rhs
);

[[nodiscard]] int localWeaponFireHitConfirmDamage(
  const WeaponFireResult& fire
);

[[nodiscard]] WeaponFireAudioCue weaponFireAudioCue(Weapon weapon);

[[nodiscard]] WeaponFireAudioEvent routeWeaponFireAudioEvent(
  const WeaponFireResult& fire,
  bool localWeaponEvent
);

[[nodiscard]] bool shouldPlaySnapshotAudioEvent(
  bool hasLastEvent,
  bool sameEvent,
  std::uint32_t serverTick,
  std::uint32_t lastPlayedServerTick,
  std::uint32_t transientTicks
);

} // namespace lg
