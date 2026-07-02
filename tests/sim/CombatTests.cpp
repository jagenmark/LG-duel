#include "shared/Constants.hpp"
#include "sim/Arena.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/Combat.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

constexpr float kHalfPi = 1.57079632679F;

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

lg::PlayerState playerAt(float x, float y) {
  lg::PlayerState player;
  player.position = {x, y, player.bounds.halfHeight};
  player.onGround = true;
  player.movementMode = lg::MovementMode::Grounded;
  return player;
}

lg::ArenaBrush convexBox(lg::Vec3 min, lg::Vec3 max) {
  lg::ArenaBrush brush;
  brush.min = min;
  brush.max = max;
  brush.faceCount = 6;
  brush.faces[0].normal = {-1.0F, 0.0F, 0.0F};
  brush.faces[0].distance = -min.x;
  brush.faces[1].normal = {1.0F, 0.0F, 0.0F};
  brush.faces[1].distance = max.x;
  brush.faces[2].normal = {0.0F, -1.0F, 0.0F};
  brush.faces[2].distance = -min.y;
  brush.faces[3].normal = {0.0F, 1.0F, 0.0F};
  brush.faces[3].distance = max.y;
  brush.faces[4].normal = {0.0F, 0.0F, -1.0F};
  brush.faces[4].distance = -min.z;
  brush.faces[5].normal = {0.0F, 0.0F, 1.0F};
  brush.faces[5].distance = max.z;
  return brush;
}

} // namespace

int main() {
  int failures = 0;
  const lg::Arena arena;
  const lg::LightningGunTuning tuning;
  const lg::HitscanTuning railTuning;
  const lg::MachineGunTuning machineGunTuning;
  const lg::ShotgunTuning shotgunTuning;

  {
    lg::Arena brushArena;
    brushArena.wallCount = 0;
    brushArena.brushCount = 2;
    brushArena.brushes[0] =
      convexBox({4.0F, -1.0F, 0.0F}, {5.0F, 1.0F, 2.0F});
    brushArena.brushes[1] =
      convexBox({4.0F, 4.0F, 0.0F}, {5.0F, 5.0F, 2.0F});

    const lg::WorldTrace trace =
      lg::traceWorld(brushArena, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}, 10.0F);

    failures += expect(
      nearlyEqual(trace.distance, 4.0F),
      "trace should still hit nearby convex brush when another convex brush is outside the segment bounds"
    );
  }

  {
    const lg::BalanceConfigLoadResult loaded =
      lg::loadBalanceConfigFromText(R"(version 1
weapon.gl.speed 20.5
weapon.gl.vertical_boost 6.25
weapon.gl.gravity 12.0
weapon.gl.bounce_damping 0.5
weapon.gl.rest_speed 0.6
weapon.gl.bounce_sound_min_speed 1.4
weapon.gl.projectile_radius 0.25
weapon.gl.projectile_hitbox_radius 0.2
weapon.gl.fuse_seconds 1.0
weapon.gl.radius 4.0
weapon.gl.cooldown_ticks 75
)");

    failures += expect(loaded.ok, "balance config should parse grenade launcher tuning");
    failures += expect(
      nearlyEqual(loaded.config.grenadeLauncher.speed, 20.5F) &&
        nearlyEqual(loaded.config.grenadeLauncher.verticalBoost, 6.25F) &&
        nearlyEqual(loaded.config.grenadeLauncher.gravity, 12.0F) &&
        nearlyEqual(loaded.config.grenadeLauncher.bounceDamping, 0.5F) &&
        nearlyEqual(loaded.config.grenadeLauncher.restSpeed, 0.6F) &&
        nearlyEqual(loaded.config.grenadeLauncher.bounceSoundMinSpeed, 1.4F) &&
        nearlyEqual(loaded.config.grenadeLauncher.projectileRadius, 0.25F) &&
        nearlyEqual(loaded.config.grenadeLauncher.projectileHitboxRadius, 0.2F) &&
        loaded.config.grenadeLauncher.fuseTicks == 125 &&
        nearlyEqual(loaded.config.grenadeLauncher.radius, 4.0F) &&
        loaded.config.grenadeLauncher.cooldownTicks == 75,
      "balance config should apply non-cvar grenade launcher values"
    );
  }

  {
    const lg::BalanceConfigLoadResult loaded =
      lg::loadBalanceConfigFromText(R"(version 1
weapon.gl.gravity -1
)");
    failures += expect(!loaded.ok, "balance config should reject out-of-range grenade values");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::LightningGunState state;
    lg::UserCommand command;
    command.attack = true;

    const lg::LightningGunResult result = lg::simulateLightningGun(
      attacker,
      target,
      command,
      arena,
      tuning,
      state,
      lg::kFixedTickSeconds
    );

    failures += expect(result.active, "attack input should activate the beam");
    failures += expect(result.hit, "beam aimed at target should hit");
    failures += expect(result.end.x < target.position.x, "beam should end at the near target surface");
    failures += expect(nearlyEqual(result.end.y, target.position.y), "centered beam hit should preserve y");
    failures += expect(result.knockbackImpulse.x > 0.0F, "beam hit should produce forward knockback");
    failures += expect(nearlyEqual(result.knockbackImpulse.z, 0.0F), "level beam knockback should be planar");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::LightningGunState state;
    lg::UserCommand command;
    command.attack = true;
    command.viewPitchRadians = 0.03F;

    const lg::LightningGunResult result = lg::simulateLightningGun(
      attacker,
      target,
      command,
      arena,
      tuning,
      state,
      lg::kFixedTickSeconds
    );

    failures += expect(result.hit, "slightly elevated beam should still hit target");
    failures += expect(result.knockbackImpulse.x > 0.0F, "3D knockback should include horizontal impulse");
    failures += expect(result.knockbackImpulse.z > 0.0F, "3D knockback should follow beam pitch");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::LightningGunState state;
    lg::UserCommand command;
    command.attack = true;
    command.viewYawRadians = kHalfPi;

    const lg::LightningGunResult result = lg::simulateLightningGun(
      attacker,
      target,
      command,
      arena,
      tuning,
      state,
      lg::kFixedTickSeconds
    );

    failures += expect(!result.hit, "beam aimed away from target should miss");
    failures += expect(target.health == 100, "missed beam should not damage target");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    target.position.z += target.bounds.halfHeight * 3.0F;
    lg::LightningGunState state;
    lg::UserCommand command;
    command.attack = true;

    const lg::LightningGunResult result = lg::simulateLightningGun(
      attacker,
      target,
      command,
      arena,
      tuning,
      state,
      lg::kFixedTickSeconds
    );

    failures += expect(
      !result.hit,
      "LG should miss a vertically separated target outside its 3D bounds"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::LightningGunState state;
    lg::UserCommand command;
    command.attack = true;
    lg::LightningGunTuning shortRangeTuning = tuning;
    shortRangeTuning.range = 4.0F;

    const lg::LightningGunResult result = lg::simulateLightningGun(
      attacker,
      target,
      command,
      arena,
      shortRangeTuning,
      state,
      lg::kFixedTickSeconds
    );

    failures += expect(!result.hit, "target beyond beam range should not be hit");
    failures += expect(nearlyEqual(result.end.x, 4.0F), "missed beam should end at configured range");
  }

  {
    const lg::Arena walledArena = lg::thunderstruckArena();
    const lg::PlayerState attacker = playerAt(-2.0F, 0.0F);
    lg::PlayerState target = playerAt(2.0F, 0.0F);
    lg::LightningGunState state;
    lg::UserCommand command;
    command.attack = true;

    const lg::LightningGunResult result = lg::simulateLightningGun(
      attacker,
      target,
      command,
      walledArena,
      tuning,
      state,
      lg::kFixedTickSeconds
    );

    failures += expect(!result.hit, "Thunderstruck central cover should block LG traces");
    failures += expect(
      result.end.x < -1.1F,
      "blocked beam should end at the cover surface"
    );
    failures += expect(target.health == 100, "wall-blocked LG should not damage target");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    target.health = 1000;
    lg::LightningGunState state;
    lg::UserCommand command;
    command.attack = true;

    for (int tick = 0; tick < 125; ++tick) {
      const lg::LightningGunResult result = lg::simulateLightningGun(
        attacker,
        target,
        command,
        arena,
        tuning,
        state,
        lg::kFixedTickSeconds
      );
      failures += expect(result.hit, "sustained centered beam should keep hitting");
    }

    failures += expect(target.health == 880, "one second of beam contact should apply configured LG DPS at 20 Hz");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState firstTarget = playerAt(6.0F, 0.0F);
    lg::PlayerState secondTarget = firstTarget;
    lg::LightningGunState firstState;
    lg::LightningGunState secondState;
    lg::UserCommand command;
    command.attack = true;

    for (int tick = 0; tick < 64; ++tick) {
      const lg::LightningGunResult firstResult = lg::simulateLightningGun(
        attacker,
        firstTarget,
        command,
        arena,
        tuning,
        firstState,
        lg::kFixedTickSeconds
      );
      const lg::LightningGunResult secondResult = lg::simulateLightningGun(
        attacker,
        secondTarget,
        command,
        arena,
        tuning,
        secondState,
        lg::kFixedTickSeconds
      );
      failures += expect(firstResult.hit == secondResult.hit, "replayed combat should match hit results");
    }

    failures += expect(firstTarget.health == secondTarget.health, "replayed combat should match target health");
    failures += expect(
      firstState.fractionalDamage == secondState.fractionalDamage,
      "replayed combat should match fractional damage"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;
    const lg::WeaponFireResult result =
      lg::simulateRailgun(attacker, target, command, arena, railTuning);

    failures += expect(result.fired, "railgun attack should fire");
    failures += expect(result.hit, "railgun aimed at target should hit");
    failures += expect(result.weapon == lg::Weapon::Railgun, "railgun result should identify weapon");
    failures += expect(result.damageApplied == 80, "railgun should apply QL-style 80 damage");
    failures += expect(target.health == 20, "railgun damage should reduce target health");
  }

  {
    const lg::Arena walledArena = lg::thunderstruckArena();
    const lg::PlayerState attacker = playerAt(-2.0F, 0.0F);
    lg::PlayerState target = playerAt(2.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;
    const lg::WeaponFireResult result =
      lg::simulateRailgun(attacker, target, command, walledArena, railTuning);

    failures += expect(!result.hit, "cover should block railgun traces");
    failures += expect(target.health == 100, "wall-blocked railgun should not damage target");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;
    const lg::WeaponFireResult result =
      lg::simulateMachineGun(attacker, target, command, arena, machineGunTuning);

    failures += expect(result.fired, "machine gun attack should fire");
    failures += expect(result.hit, "machine gun aimed at target should hit");
    failures += expect(
      result.weapon == lg::Weapon::MachineGun,
      "machine gun result should identify weapon"
    );
    failures += expect(result.damageApplied == 5, "machine gun should apply 5 damage");
    failures += expect(target.health == 95, "machine gun damage should reduce target health");
    failures += expect(
      result.knockbackImpulse.x > 0.0F,
      "machine gun hit should produce forward knockback"
    );
  }

  {
    const lg::Arena walledArena = lg::thunderstruckArena();
    const lg::PlayerState attacker = playerAt(-2.0F, 0.0F);
    lg::PlayerState target = playerAt(2.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;
    const lg::WeaponFireResult result =
      lg::simulateMachineGun(attacker, target, command, walledArena, machineGunTuning);

    failures += expect(!result.hit, "cover should block machine gun traces");
    failures += expect(target.health == 100, "wall-blocked machine gun should not damage target");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState firstTarget = playerAt(6.0F, 0.0F);
    lg::PlayerState secondTarget = playerAt(6.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;

    const lg::WeaponFireResult firstResult =
      lg::simulateShotgun(attacker, firstTarget, command, arena, shotgunTuning);
    const lg::WeaponFireResult secondResult =
      lg::simulateShotgun(attacker, secondTarget, command, arena, shotgunTuning);

    failures += expect(firstResult.fired, "shotgun attack should fire");
    failures += expect(firstResult.hit, "shotgun aimed at target should hit");
    failures += expect(firstResult.weapon == lg::Weapon::Shotgun, "shotgun result should identify weapon");
    failures += expect(
      firstResult.pelletCount == lg::kShotgunPelletCount,
      "shotgun should report the deterministic pellet count"
    );
    failures += expect(
      firstResult.pelletHitCount > 0 &&
        firstResult.pelletHitCount < firstResult.pelletCount,
      "shotgun at mid range should support partial pellet hits"
    );
    failures += expect(
      firstResult.pelletHitCount == secondResult.pelletHitCount &&
        firstResult.damageApplied == secondResult.damageApplied &&
        firstTarget.health == secondTarget.health,
      "replayed shotgun spread should be deterministic"
    );
    failures += expect(
      firstResult.damageApplied ==
        static_cast<int>(firstResult.pelletHitCount) * shotgunTuning.damagePerPellet,
      "shotgun damage should scale per pellet"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(2.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;

    const lg::WeaponFireResult result =
      lg::simulateShotgun(attacker, target, command, arena, shotgunTuning);

    failures += expect(
      result.pelletHitCount == result.pelletCount,
      "close shotgun blast should allow a full pellet hit"
    );
    failures += expect(result.damageApplied == 100, "full shotgun hit should apply 100 damage");
    failures += expect(target.health == 0, "full shotgun hit should be lethal to 100 health");
    failures += expect(result.knockbackImpulse.x > 0.0F, "shotgun hit should produce knockback");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;
    command.viewYawRadians = kHalfPi;

    const lg::WeaponFireResult result =
      lg::simulateShotgun(attacker, target, command, arena, shotgunTuning);

    failures += expect(!result.hit, "shotgun aimed away from target should miss");
    failures += expect(result.pelletHitCount == 0, "missed shotgun should report zero pellet hits");
    failures += expect(target.health == 100, "missed shotgun should not damage target");
  }

  {
    const lg::Arena walledArena = lg::thunderstruckArena();
    const lg::PlayerState attacker = playerAt(-2.0F, 0.0F);
    lg::PlayerState target = playerAt(2.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;

    const lg::WeaponFireResult result =
      lg::simulateShotgun(attacker, target, command, walledArena, shotgunTuning);

    failures += expect(!result.hit, "cover should block shotgun pellet traces");
    failures += expect(result.pelletHitCount == 0, "wall-blocked shotgun should report zero pellet hits");
    failures += expect(target.health == 100, "wall-blocked shotgun should not damage target");
  }

  return failures == 0 ? 0 : 1;
}
