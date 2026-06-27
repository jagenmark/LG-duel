#include "client/HitConfirmAudio.hpp"

namespace lg {
namespace {

[[nodiscard]] bool sameVec3(Vec3 lhs, Vec3 rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

} // namespace

bool sameWeaponFireEvent(
  const WeaponFireResult& lhs,
  const WeaponFireResult& rhs
) {
  return lhs.fired == rhs.fired &&
    lhs.hit == rhs.hit &&
    lhs.weapon == rhs.weapon &&
    lhs.damageApplied == rhs.damageApplied &&
    lhs.pelletCount == rhs.pelletCount &&
    lhs.pelletHitCount == rhs.pelletHitCount &&
    sameVec3(lhs.start, rhs.start) &&
    sameVec3(lhs.end, rhs.end) &&
    sameVec3(lhs.knockbackImpulse, rhs.knockbackImpulse);
}

bool sameRocketExplosionEvent(
  const RocketExplosionResult& lhs,
  const RocketExplosionResult& rhs
) {
  return lhs.active == rhs.active &&
    lhs.weapon == rhs.weapon &&
    lhs.ownerDamageApplied == rhs.ownerDamageApplied &&
    lhs.opponentDamageApplied == rhs.opponentDamageApplied &&
    lhs.radius == rhs.radius &&
    sameVec3(lhs.position, rhs.position);
}

bool sameFragEvent(const FragEvent& lhs, const FragEvent& rhs) {
  return lhs.active == rhs.active &&
    lhs.targetPlayerIndex == rhs.targetPlayerIndex;
}

int localWeaponFireHitConfirmDamage(const WeaponFireResult& fire) {
  if (!fire.fired || !fire.hit || fire.damageApplied <= 0) {
    return 0;
  }
  return fire.damageApplied;
}

WeaponFireAudioCue weaponFireAudioCue(Weapon weapon) {
  switch (weapon) {
  case Weapon::Railgun:
    return WeaponFireAudioCue::Railgun;
  case Weapon::RocketLauncher:
    return WeaponFireAudioCue::RocketLauncher;
  case Weapon::MachineGun:
    return WeaponFireAudioCue::MachineGun;
  case Weapon::Shotgun:
    return WeaponFireAudioCue::Shotgun;
  case Weapon::GrenadeLauncher:
    return WeaponFireAudioCue::GrenadeLauncher;
  case Weapon::PlasmaGun:
    return WeaponFireAudioCue::PlasmaGun;
  case Weapon::LightningGun:
    return WeaponFireAudioCue::None;
  }
  return WeaponFireAudioCue::None;
}

WeaponFireAudioEvent routeWeaponFireAudioEvent(
  const WeaponFireResult& fire,
  bool localWeaponEvent
) {
  WeaponFireAudioEvent event;
  if (!fire.fired) {
    return event;
  }

  event.cue = weaponFireAudioCue(fire.weapon);
  event.startsLocalRailCooldown =
    localWeaponEvent && fire.weapon == Weapon::Railgun;
  event.localHitConfirmDamage =
    localWeaponEvent ? localWeaponFireHitConfirmDamage(fire) : 0;
  return event;
}

bool shouldPlaySnapshotAudioEvent(
  bool hasLastEvent,
  bool sameEvent,
  std::uint32_t serverTick,
  std::uint32_t lastPlayedServerTick,
  std::uint32_t transientTicks
) {
  return !hasLastEvent ||
    !sameEvent ||
    serverTick - lastPlayedServerTick > transientTicks;
}

} // namespace lg
