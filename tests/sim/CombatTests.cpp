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

float pitchToTargetZ(
  const lg::PlayerState& attacker,
  const lg::PlayerState& target,
  float eyeHeight,
  float targetZ
) {
  const lg::Vec3 start = lg::weaponMuzzlePosition(attacker, eyeHeight);
  const float dx = target.position.x - start.x;
  const float dy = target.position.y - start.y;
  const float horizontalDistance = std::sqrt((dx * dx) + (dy * dy));
  return std::atan2(targetZ - start.z, horizontalDistance);
}

float headAimZ(const lg::PlayerState& target) {
  return target.position.z + target.bounds.halfHeight * 0.76F;
}

float bodyAimZ(const lg::PlayerState& target) {
  return target.position.z;
}

lg::Vec3 directionToTargetPoint(
  const lg::PlayerState& attacker,
  float eyeHeight,
  lg::Vec3 targetPoint
) {
  const lg::Vec3 start = lg::weaponMuzzlePosition(attacker, eyeHeight);
  return lg::normalize(targetPoint - start);
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

lg::Arena coverArena() {
  lg::Arena arena;
  arena.walls[0] = {{-0.5F, -1.0F, 0.0F}, {0.5F, 1.0F, 2.0F}};
  arena.wallCount = 1;
  return arena;
}

} // namespace

int main() {
  int failures = 0;
  const lg::Arena arena;
  const lg::LightningGunTuning tuning;
  const lg::FreezeGunTuning freezeTuning;
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
weapon.rl.direct_hitbox_half_extent_xy 0.4821429
weapon.rl.direct_hitbox_half_extent_z 0.9
weapon.pg.direct_hitbox_half_extent_xy 0.5
weapon.pg.direct_hitbox_half_extent_z 1.1
weapon.rg.spawn_ammo 9
weapon.lg.spawn_ammo 123
weapon.fg.freeze_per_second 60
weapon.fg.decay_per_second 25
weapon.fg.max_slow_fraction 0.4
weapon.fg.spawn_ammo 124
weapon.fg.ice_pool_max_radius 2.5
weapon.fg.ice_pool_growth_per_second 12
weapon.fg.ice_pool_lifetime_seconds 3.5
weapon.fg.ice_pool_friction 0.8
weapon.fg.ice_pool_slope_gravity_scale 1.25
weapon.fg.ice_pool_control_scale 0.3
weapon.fg.ice_pool_merge_distance 1.2
)");

    failures += expect(loaded.ok, "balance config should parse projectile direct-hit AABB tuning");
    failures += expect(
      nearlyEqual(loaded.config.rocketLauncher.directHitboxHalfExtentXY, 0.4821429F) &&
        nearlyEqual(loaded.config.rocketLauncher.directHitboxHalfExtentZ, 0.9F) &&
        nearlyEqual(loaded.config.plasmaGun.directHitboxHalfExtentXY, 0.5F) &&
        nearlyEqual(loaded.config.plasmaGun.directHitboxHalfExtentZ, 1.1F) &&
        loaded.config.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Railgun)] == 9 &&
        loaded.config.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::LightningGun)] == 123 &&
        loaded.config.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::FreezeGun)] == 124 &&
        nearlyEqual(loaded.config.freezeGun.freezePerSecond, 60.0F) &&
        nearlyEqual(loaded.config.freezeGun.decayPerSecond, 25.0F) &&
        nearlyEqual(loaded.config.freezeGun.maxSlowFraction, 0.4F) &&
        nearlyEqual(loaded.config.icePool.maxRadius, 2.5F) &&
        nearlyEqual(loaded.config.icePool.growthPerSecond, 12.0F) &&
        nearlyEqual(loaded.config.icePool.lifetimeSeconds, 3.5F) &&
        nearlyEqual(loaded.config.icePool.friction, 0.8F) &&
        nearlyEqual(loaded.config.icePool.slopeGravityScale, 1.25F) &&
        nearlyEqual(loaded.config.icePool.controlScale, 0.3F) &&
        nearlyEqual(loaded.config.icePool.mergeDistance, 1.2F),
      "balance config should apply projectile and spawn ammo tuning"
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
jumppad.retrigger_cooldown_ms 200
pickup.health.small_amount 15
pickup.health.small_cooldown_ms 1000
pickup.health.large_amount 75
pickup.health.large_cooldown_ms 2000
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
        loaded.config.grenadeLauncher.cooldownTicks == 75 &&
        loaded.config.jumpPadRetriggerCooldownTicks == 25 &&
        loaded.config.smallHealthPickupAmount == 15 &&
        loaded.config.smallHealthPickupCooldownTicks == 125 &&
        loaded.config.largeHealthPickupAmount == 75 &&
        loaded.config.largeHealthPickupCooldownTicks == 250,
      "balance config should apply non-cvar grenade launcher, jumppad, and health pickup values"
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
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    float hitDistance = 0.0F;
    const bool hit = lg::tracePlayerProjectileDirectAabb(
      {0.0F, 0.0F, target.position.z},
      {1.0F, 0.0F, 0.0F},
      target,
      10.0F,
      {0.4821429F, 0.4821429F, 0.9F},
      hitDistance
    );

    failures += expect(hit, "projectile AABB trace should hit a segment through the box");
    failures += expect(
      nearlyEqual(hitDistance, 5.517857F),
      "projectile AABB trace should report the near box intersection"
    );
  }

  {
    lg::PlayerState target = playerAt(6.0F, 0.5F);
    float hitDistance = 0.0F;
    const bool hit = lg::tracePlayerProjectileDirectAabb(
      {0.0F, 0.0F, target.position.z},
      {1.0F, 0.0F, 0.0F},
      target,
      10.0F,
      {0.4821429F, 0.4821429F, 0.9F},
      hitDistance
    );

    failures += expect(!hit, "projectile AABB trace should miss outside the box");
  }

  {
    lg::PlayerState target = playerAt(6.0F, 0.45F);
    const lg::Vec3 origin = {0.0F, 0.0F, target.position.z + 0.85F};
    float cylinderHitDistance = 0.0F;
    float aabbHitDistance = 0.0F;
    const bool cylinderHit = lg::tracePlayerCylinder(
      origin,
      {1.0F, 0.0F, 0.0F},
      target,
      10.0F,
      cylinderHitDistance
    );
    const bool aabbHit = lg::tracePlayerProjectileDirectAabb(
      origin,
      {1.0F, 0.0F, 0.0F},
      target,
      10.0F,
      {0.4821429F, 0.4821429F, 0.9F},
      aabbHitDistance
    );

    failures += expect(
      !cylinderHit && aabbHit,
      "projectile AABB trace should hit a near-corner path that the player cylinder misses"
    );
    failures += expect(
      nearlyEqual(aabbHitDistance, 5.517857F),
      "near-corner projectile AABB hit should still report the near face"
    );
  }

  {
    lg::PlayerState targets[2] = {
      playerAt(4.0F, 0.0F),
      playerAt(7.0F, 0.0F),
    };
    std::size_t targetIndex = 2;
    float bestHitDistance = 10.0F;
    for (std::size_t index = 0; index < 2; ++index) {
      float hitDistance = 0.0F;
      if (
        lg::tracePlayerProjectileDirectAabb(
          {0.0F, 0.0F, targets[index].position.z},
          {1.0F, 0.0F, 0.0F},
          targets[index],
          bestHitDistance,
          {0.4821429F, 0.4821429F, 0.9F},
          hitDistance
        )
      ) {
        targetIndex = index;
        bestHitDistance = hitDistance;
      }
    }

    failures += expect(targetIndex == 0, "nearest projectile AABB target should win on the trace line");
    failures += expect(
      nearlyEqual(bestHitDistance, 3.517857F),
      "nearest projectile AABB target should keep its hit distance"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    const lg::PlayerState target = playerAt(6.0F, 0.0F);
    const lg::Vec3 origin =
      lg::weaponMuzzlePosition(attacker, tuning.eyeHeight);
    float bodyHitDistance = 0.0F;
    float horizontalHeadHitDistance = 0.0F;
    float bodyHeadHitDistance = 0.0F;
    float aimedHeadHitDistance = 0.0F;
    float leftEdgeHeadHitDistance = 0.0F;
    float rightEdgeHeadHitDistance = 0.0F;
    float topLeftHeadHitDistance = 0.0F;
    float topRightHeadHitDistance = 0.0F;
    const bool bodyHit = lg::tracePlayerCylinder(
      origin,
      {1.0F, 0.0F, 0.0F},
      target,
      10.0F,
      bodyHitDistance
    );
    const bool horizontalHeadHit = lg::tracePlayerHeadHitbox(
      origin,
      {1.0F, 0.0F, 0.0F},
      target,
      10.0F,
      horizontalHeadHitDistance
    );
    const float bodyPitch =
      pitchToTargetZ(attacker, target, tuning.eyeHeight, bodyAimZ(target));
    const bool bodyHeadHit = lg::tracePlayerHeadHitbox(
      origin,
      lg::cameraForward(0.0F, bodyPitch),
      target,
      10.0F,
      bodyHeadHitDistance
    );
    const float headPitch =
      pitchToTargetZ(attacker, target, tuning.eyeHeight, headAimZ(target));
    const bool aimedHeadHit = lg::tracePlayerHeadHitbox(
      origin,
      lg::cameraForward(0.0F, headPitch),
      target,
      10.0F,
      aimedHeadHitDistance
    );
    const float sideEdgeOffset = target.bounds.radius * 0.90F;
    const float topCornerZ =
      target.position.z + target.bounds.halfHeight * 0.96F;
    const bool leftEdgeHeadHit = lg::tracePlayerHeadHitbox(
      origin,
      directionToTargetPoint(
        attacker,
        tuning.eyeHeight,
        target.position + lg::Vec3{0.0F, sideEdgeOffset, target.bounds.halfHeight * 0.76F}
      ),
      target,
      10.0F,
      leftEdgeHeadHitDistance
    );
    const bool rightEdgeHeadHit = lg::tracePlayerHeadHitbox(
      origin,
      directionToTargetPoint(
        attacker,
        tuning.eyeHeight,
        target.position + lg::Vec3{0.0F, -sideEdgeOffset, target.bounds.halfHeight * 0.76F}
      ),
      target,
      10.0F,
      rightEdgeHeadHitDistance
    );
    const bool topLeftHeadHit = lg::tracePlayerHeadHitbox(
      origin,
      directionToTargetPoint(
        attacker,
        tuning.eyeHeight,
        {target.position.x, target.position.y + sideEdgeOffset, topCornerZ}
      ),
      target,
      10.0F,
      topLeftHeadHitDistance
    );
    const bool topRightHeadHit = lg::tracePlayerHeadHitbox(
      origin,
      directionToTargetPoint(
        attacker,
        tuning.eyeHeight,
        {target.position.x, target.position.y - sideEdgeOffset, topCornerZ}
      ),
      target,
      10.0F,
      topRightHeadHitDistance
    );

    failures += expect(bodyHit, "body cylinder trace should still hit the target");
    failures += expect(
      !bodyHeadHit && horizontalHeadHit && aimedHeadHit,
      "authoritative head hitbox should cover the visible head without covering torso aim"
    );
    failures += expect(
      leftEdgeHeadHit && rightEdgeHeadHit &&
        topLeftHeadHit && topRightHeadHit,
      "authoritative head hitbox should include the visible side and top-corner head areas"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    target.health = 200;
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
    const lg::Arena walledArena = coverArena();
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

    failures += expect(!result.hit, "central cover should block LG traces");
    failures += expect(
      result.end.x < -0.4F,
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
    command.viewPitchRadians =
      pitchToTargetZ(attacker, target, tuning.eyeHeight, bodyAimZ(target));

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
    target.health = 1000;
    lg::LightningGunState state;
    lg::UserCommand command;
    command.attack = true;
    command.viewPitchRadians =
      pitchToTargetZ(attacker, target, freezeTuning.eyeHeight, bodyAimZ(target));

    for (int tick = 0; tick < 125; ++tick) {
      const lg::LightningGunResult result = lg::simulateFreezeGun(
        attacker,
        target,
        command,
        arena,
        freezeTuning,
        state,
        lg::kFixedTickSeconds
      );
      failures += expect(result.hit, "sustained freeze beam should keep hitting");
      failures += expect(!result.headshot, "freeze beam aimed at the body should not headshot");
    }

    failures += expect(
      target.health == 880,
      "one second of default freeze gun contact should apply 120 DPS"
    );
    failures += expect(
      nearlyEqual(target.freezeLevel, 50.0F),
      "one second of freeze gun contact should add configured freeze at LG fire cadence"
    );
  }

  {
    lg::PlayerState player = playerAt(0.0F, 0.0F);
    player.freezeLevel = 50.0F;
    lg::decayPlayerFreezeLevel(player, freezeTuning, 0.5F);
    failures += expect(
      nearlyEqual(player.freezeLevel, 40.0F),
      "freeze level should decay at a constant configured rate"
    );
    failures += expect(
      nearlyEqual(lg::freezeMovementScale(player, freezeTuning), 0.84F),
      "freeze movement scale should be linear up to max slow fraction"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState lightningTarget = playerAt(6.0F, 0.0F);
    lg::PlayerState freezeTarget = playerAt(6.0F, 0.0F);
    lightningTarget.health = 1000;
    freezeTarget.health = 1000;
    lg::LightningGunState lightningState;
    lg::LightningGunState freezeState;
    lg::UserCommand command;
    command.attack = true;
    command.viewPitchRadians =
      pitchToTargetZ(attacker, lightningTarget, tuning.eyeHeight, headAimZ(lightningTarget));

    for (int tick = 0; tick < 125; ++tick) {
      const lg::LightningGunResult lightningResult = lg::simulateLightningGun(
        attacker,
        lightningTarget,
        command,
        arena,
        tuning,
        lightningState,
        lg::kFixedTickSeconds
      );
      const lg::LightningGunResult freezeResult = lg::simulateFreezeGun(
        attacker,
        freezeTarget,
        command,
        arena,
        freezeTuning,
        freezeState,
        lg::kFixedTickSeconds
      );
      failures += expect(
        lightningResult.headshot && freezeResult.headshot,
        "LG and FG beams aimed at the head should report headshots"
      );
    }

    failures += expect(
      lightningTarget.health == 760 &&
        freezeTarget.health == 760 &&
        nearlyEqual(freezeTarget.freezeLevel, 50.0F),
      "LG and FG headshots should double beam damage without changing freeze buildup"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;
    command.viewPitchRadians =
      pitchToTargetZ(attacker, target, railTuning.eyeHeight, bodyAimZ(target));
    const lg::WeaponFireResult result =
      lg::simulateRailgun(attacker, target, command, arena, railTuning);

    failures += expect(result.fired, "railgun attack should fire");
    failures += expect(result.hit, "railgun aimed at target should hit");
    failures += expect(!result.headshot, "railgun shot aimed at the body should not headshot");
    failures += expect(result.weapon == lg::Weapon::Railgun, "railgun result should identify weapon");
    failures += expect(result.damageApplied == 80, "railgun should apply QL-style 80 damage");
    failures += expect(target.health == 20, "railgun damage should reduce target health");
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    target.health = 200;
    lg::UserCommand command;
    command.attack = true;
    command.viewPitchRadians =
      pitchToTargetZ(attacker, target, railTuning.eyeHeight, headAimZ(target));
    const lg::WeaponFireResult result =
      lg::simulateRailgun(attacker, target, command, arena, railTuning);

    failures += expect(result.hit && result.headshot, "railgun should report headshots");
    failures += expect(result.damageApplied == 160, "railgun headshot should double damage");
    failures += expect(target.health == 40, "railgun headshot should reduce target health");
  }

  {
    const lg::Arena walledArena = coverArena();
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
    command.viewPitchRadians =
      pitchToTargetZ(attacker, target, machineGunTuning.eyeHeight, bodyAimZ(target));
    const lg::WeaponFireResult result =
      lg::simulateMachineGun(attacker, target, command, arena, machineGunTuning);

    failures += expect(result.fired, "machine gun attack should fire");
    failures += expect(result.hit, "machine gun aimed at target should hit");
    failures += expect(
      result.weapon == lg::Weapon::MachineGun,
      "machine gun result should identify weapon"
    );
    failures += expect(!result.headshot, "machine gun shot aimed at the body should not headshot");
    failures += expect(result.damageApplied == 5, "machine gun should apply 5 damage");
    failures += expect(target.health == 95, "machine gun damage should reduce target health");
    failures += expect(
      result.knockbackImpulse.x > 0.0F,
      "machine gun hit should produce forward knockback"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;
    command.viewPitchRadians =
      pitchToTargetZ(attacker, target, machineGunTuning.eyeHeight, headAimZ(target));
    const lg::WeaponFireResult result =
      lg::simulateMachineGun(attacker, target, command, arena, machineGunTuning);

    failures += expect(
      result.hit && result.headshot,
      "machine gun should report headshots"
    );
    failures += expect(result.damageApplied == 10, "machine gun headshot should double damage");
    failures += expect(target.health == 90, "machine gun headshot should reduce target health");
  }

  {
    const lg::Arena walledArena = coverArena();
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
    command.viewPitchRadians =
      pitchToTargetZ(attacker, firstTarget, shotgunTuning.eyeHeight, bodyAimZ(firstTarget));

    const lg::WeaponFireResult firstResult =
      lg::simulateShotgun(attacker, firstTarget, command, arena, shotgunTuning);
    const lg::WeaponFireResult secondResult =
      lg::simulateShotgun(attacker, secondTarget, command, arena, shotgunTuning);

    failures += expect(firstResult.fired, "shotgun attack should fire");
    failures += expect(firstResult.hit, "shotgun aimed at target should hit");
    failures += expect(!firstResult.headshot, "shotgun blast aimed at the body should not headshot");
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
    lg::PlayerState target = playerAt(6.0F, 0.0F);
    target.health = 300;
    lg::ShotgunTuning headshotShotgun = shotgunTuning;
    headshotShotgun.spreadRadians = 0.0F;
    lg::UserCommand command;
    command.attack = true;
    command.viewPitchRadians =
      pitchToTargetZ(attacker, target, headshotShotgun.eyeHeight, headAimZ(target));

    const lg::WeaponFireResult result =
      lg::simulateShotgun(attacker, target, command, arena, headshotShotgun);

    failures += expect(
      result.hit &&
        result.headshot &&
        result.pelletHeadshotCount == result.pelletCount,
      "zero-spread shotgun aimed at the head should headshot every pellet"
    );
    failures += expect(
      result.damageApplied == 200 && target.health == 100,
      "shotgun headshot pellets should double per-pellet damage"
    );
  }

  {
    const lg::PlayerState attacker = playerAt(0.0F, 0.0F);
    lg::PlayerState target = playerAt(2.0F, 0.0F);
    lg::UserCommand command;
    command.attack = true;
    command.viewPitchRadians =
      pitchToTargetZ(attacker, target, shotgunTuning.eyeHeight, bodyAimZ(target));

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
    const lg::Arena walledArena = coverArena();
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
