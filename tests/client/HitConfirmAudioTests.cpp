#include "client/HitConfirmAudio.hpp"

#include <iostream>
#include <string>

namespace {

int expect(bool condition, const std::string& message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::WeaponFireResult firstShot;
    firstShot.fired = true;
    firstShot.hit = true;
    firstShot.weapon = lg::Weapon::Shotgun;
    firstShot.damageApplied = 35;
    firstShot.pelletCount = lg::kShotgunPelletCount;
    firstShot.pelletHitCount = 7;
    firstShot.start = {1.0F, 2.0F, 3.0F};
    firstShot.end = {4.0F, 5.0F, 6.0F};

    lg::WeaponFireResult repeatedShot = firstShot;
    lg::WeaponFireResult differentPelletConfirmation = firstShot;
    differentPelletConfirmation.pelletHitCount = 8;
    differentPelletConfirmation.damageApplied = 40;

    failures += expect(
      lg::sameWeaponFireEvent(firstShot, repeatedShot),
      "identical shotgun fire should de-duplicate as the same event"
    );
    failures += expect(
      !lg::sameWeaponFireEvent(firstShot, differentPelletConfirmation),
      "shotgun pellet hit count should participate in event identity"
    );
  }

  {
    constexpr std::uint32_t transientTicks = 8;
    failures += expect(
      lg::shouldPlaySnapshotAudioEvent(false, true, 100, 100, transientTicks),
      "first snapshot audio event should play even when event payload matches defaults"
    );
    failures += expect(
      !lg::shouldPlaySnapshotAudioEvent(true, true, 104, 100, transientTicks),
      "same event should be suppressed while transient replay is still active"
    );
    failures += expect(
      lg::shouldPlaySnapshotAudioEvent(true, true, 200, 100, transientTicks),
      "same event should play again after the transient replay window"
    );
    failures += expect(
      lg::shouldPlaySnapshotAudioEvent(true, false, 104, 100, transientTicks),
      "changed event payload should play immediately"
    );
  }

  {
    lg::WeaponFireResult shotgunHit;
    shotgunHit.fired = true;
    shotgunHit.hit = true;
    shotgunHit.weapon = lg::Weapon::Shotgun;
    shotgunHit.damageApplied = 45;
    shotgunHit.pelletCount = lg::kShotgunPelletCount;
    shotgunHit.pelletHitCount = 9;

    failures += expect(
      lg::localWeaponFireHitConfirmDamage(shotgunHit) == 45,
      "shotgun hit confirm should use aggregate damage, not one cue per pellet"
    );
  }

  {
    lg::WeaponFireResult friendlyOrBlockedHit;
    friendlyOrBlockedHit.fired = true;
    friendlyOrBlockedHit.hit = true;
    friendlyOrBlockedHit.weapon = lg::Weapon::MachineGun;
    friendlyOrBlockedHit.damageApplied = 0;

    failures += expect(
      lg::localWeaponFireHitConfirmDamage(friendlyOrBlockedHit) == 0,
      "weapon hit confirm should require authoritative positive damage"
    );
  }

  {
    lg::WeaponFireResult plasmaHit;
    plasmaHit.fired = true;
    plasmaHit.hit = true;
    plasmaHit.weapon = lg::Weapon::PlasmaGun;
    plasmaHit.damageApplied = 20;

    failures += expect(
      lg::localWeaponFireHitConfirmDamage(plasmaHit) == 20,
      "future positive-damage weapon fire events should be eligible for hit confirm"
    );
  }

  {
    lg::WeaponFireResult machineGunFire;
    machineGunFire.fired = true;
    machineGunFire.weapon = lg::Weapon::MachineGun;
    const lg::WeaponFireAudioEvent event =
      lg::routeWeaponFireAudioEvent(machineGunFire, true);

    failures += expect(
      event.cue == lg::WeaponFireAudioCue::MachineGun,
      "machine gun fire should route to its fire cue"
    );
    failures += expect(
      !event.startsLocalRailCooldown && event.localHitConfirmDamage == 0,
      "machine gun fire should not reuse rail cooldown routing"
    );
  }

  {
    lg::WeaponFireResult shotgunFire;
    shotgunFire.fired = true;
    shotgunFire.weapon = lg::Weapon::Shotgun;
    const lg::WeaponFireAudioEvent event =
      lg::routeWeaponFireAudioEvent(shotgunFire, false);

    failures += expect(
      event.cue == lg::WeaponFireAudioCue::Shotgun,
      "remote shotgun fire should route to its fire cue"
    );
  }

  {
    lg::WeaponFireResult futureFire;
    futureFire.fired = true;
    futureFire.weapon = lg::Weapon::GrenadeLauncher;
    failures += expect(
      lg::routeWeaponFireAudioEvent(futureFire, true).cue ==
        lg::WeaponFireAudioCue::GrenadeLauncher,
      "grenade launcher fire events should have a cue when gameplay emits them"
    );

    futureFire.weapon = lg::Weapon::PlasmaGun;
    failures += expect(
      lg::routeWeaponFireAudioEvent(futureFire, false).cue ==
        lg::WeaponFireAudioCue::PlasmaGun,
      "plasma gun fire events should have a cue when gameplay emits them"
    );
  }

  {
    lg::WeaponFireResult railHit;
    railHit.fired = true;
    railHit.hit = true;
    railHit.weapon = lg::Weapon::Railgun;
    railHit.damageApplied = 80;
    railHit.headshot = true;
    const lg::WeaponFireAudioEvent event =
      lg::routeWeaponFireAudioEvent(railHit, true);

    failures += expect(
      event.cue == lg::WeaponFireAudioCue::Railgun &&
        event.startsLocalRailCooldown &&
        event.localHitConfirmDamage == 80 &&
        event.localHitConfirmHeadshot,
      "local rail headshots should keep rail fire, cooldown, and headshot hit-confirm routing"
    );
  }

  {
    lg::WeaponFireResult revolverHit;
    revolverHit.fired = true;
    revolverHit.hit = true;
    revolverHit.weapon = lg::Weapon::Revolver;
    revolverHit.damageApplied = 80;
    const lg::WeaponFireAudioEvent event =
      lg::routeWeaponFireAudioEvent(revolverHit, true);

    failures += expect(
      event.cue == lg::WeaponFireAudioCue::Revolver &&
        !event.startsLocalRailCooldown &&
        event.localHitConfirmDamage == 80,
      "revolver may reuse the fire sound without starting the Sniper cooldown chime"
    );
  }

  return failures == 0 ? 0 : 1;
}
