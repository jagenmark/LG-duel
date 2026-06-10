#include "shared/Constants.hpp"
#include "sim/Arena.hpp"
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

} // namespace

int main() {
  int failures = 0;
  const lg::Arena arena;
  const lg::LightningGunTuning tuning;

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
    const lg::PlayerState attacker = playerAt(-4.0F, -6.0F);
    lg::PlayerState target = playerAt(4.0F, -6.0F);
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

    failures += expect(!result.hit, "Thunderstruck divider should block LG traces");
    failures += expect(
      result.end.x < -0.9F,
      "blocked beam should end at the divider surface"
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

    failures += expect(target.health == 920, "one second of beam contact should apply configured DPS");
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

  return failures == 0 ? 0 : 1;
}
