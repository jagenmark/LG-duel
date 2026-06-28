#include "shared/Constants.hpp"
#include "shared/Math.hpp"
#include "sim/Arena.hpp"
#include "sim/Movement.hpp"
#include "sim/MovementModes.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <cstdint>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

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

lg::PlayerState groundedPlayer() {
  lg::PlayerState player;
  player.position = {0.0F, 0.0F, player.bounds.halfHeight};
  player.onGround = true;
  player.movementMode = lg::MovementMode::Grounded;
  return player;
}

void runCommand(lg::PlayerState& player, const lg::UserCommand& command, int ticks) {
  const lg::Arena arena;
  const lg::MovementTuning tuning;

  for (int i = 0; i < ticks; ++i) {
    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
  }
}

void runCommand(
  lg::PlayerState& player,
  const lg::UserCommand& command,
  const lg::MovementTuning& tuning,
  int ticks
) {
  const lg::Arena arena;
  for (int i = 0; i < ticks; ++i) {
    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
  }
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::PlayerState player = groundedPlayer();
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, 10);

    failures += expect(player.position.x > 0.0F, "forward input should move player along yaw forward");
    failures += expect(player.velocity.x > 0.0F, "forward input should produce positive velocity");
    failures += expect(nearlyEqual(player.position.z, player.bounds.halfHeight), "grounded movement should stay on floor");
  }

  {
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    lg::MovementTuning lowAcceleration;
    lowAcceleration.groundAcceleration = 10.0F;
    lg::MovementTuning highAcceleration = lowAcceleration;
    highAcceleration.groundAcceleration = 160.0F;
    lg::PlayerState slow = groundedPlayer();
    lg::PlayerState fast = groundedPlayer();
    slow.position.x = -6.0F;
    fast.position.x = -6.0F;

    runCommand(slow, command, lowAcceleration, 5);
    runCommand(fast, command, highAcceleration, 5);

    failures += expect(
      fast.velocity.x > slow.velocity.x * 2.0F,
      "g_accel should materially change initial ground acceleration"
    );
  }

  {
    lg::UserCommand coast;
    lg::MovementTuning noFriction;
    noFriction.groundFriction = 0.0F;
    lg::MovementTuning highFriction = noFriction;
    highFriction.groundFriction = 20.0F;
    lg::PlayerState sliding = groundedPlayer();
    lg::PlayerState stopping = groundedPlayer();
    sliding.velocity.x = 8.0F;
    stopping.velocity.x = 8.0F;

    runCommand(sliding, coast, noFriction, 20);
    runCommand(stopping, coast, highFriction, 20);

    failures += expect(
      sliding.velocity.x > 7.9F && stopping.velocity.x < 0.3F,
      "g_friction should strongly affect grounded coasting without movement input"
    );
  }

  {
    lg::UserCommand coast;
    lg::MovementTuning lowStopSpeed;
    lowStopSpeed.groundFriction = 6.0F;
    lowStopSpeed.stopSpeed = 0.0F;
    lg::MovementTuning highStopSpeed = lowStopSpeed;
    highStopSpeed.stopSpeed = 5.0F;
    lg::PlayerState gradual = groundedPlayer();
    lg::PlayerState decisive = groundedPlayer();
    gradual.velocity.x = 1.0F;
    decisive.velocity.x = 1.0F;

    runCommand(gradual, coast, lowStopSpeed, 10);
    runCommand(decisive, coast, highStopSpeed, 10);

    failures += expect(
      gradual.velocity.x > 0.5F && decisive.velocity.x < 0.01F,
      "g_stopspeed should strengthen low-speed grounded braking"
    );
  }

  {
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    lg::MovementTuning lowAirAcceleration;
    lowAirAcceleration.airAcceleration = 1.0F;
    lg::MovementTuning highAirAcceleration = lowAirAcceleration;
    highAirAcceleration.airAcceleration = 24.0F;
    lg::PlayerState slow = groundedPlayer();
    lg::PlayerState fast = groundedPlayer();
    slow.position = {-6.0F, 0.0F, 4.0F};
    fast.position = slow.position;
    slow.onGround = false;
    fast.onGround = false;
    slow.movementMode = lg::MovementMode::Airborne;
    fast.movementMode = lg::MovementMode::Airborne;

    runCommand(slow, command, lowAirAcceleration, 5);
    runCommand(fast, command, highAirAcceleration, 5);

    failures += expect(
      fast.velocity.x > slow.velocity.x * 10.0F,
      "g_airaccel should materially change airborne acceleration"
    );
  }

  {
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    lg::MovementTuning q3;
    q3.airAcceleration = 0.0F;
    lg::MovementTuning qw = q3;
    qw.airControlEnabled = true;
    lg::PlayerState withoutControl = groundedPlayer();
    lg::PlayerState withControl = groundedPlayer();
    withoutControl.position = {-6.0F, 0.0F, 4.0F};
    withControl.position = withoutControl.position;
    withoutControl.velocity = {4.0F, 4.0F, 0.0F};
    withControl.velocity = withoutControl.velocity;
    withoutControl.onGround = false;
    withControl.onGround = false;
    withoutControl.movementMode = lg::MovementMode::Airborne;
    withControl.movementMode = lg::MovementMode::Airborne;

    runCommand(withoutControl, command, q3, 8);
    runCommand(withControl, command, qw, 8);

    failures += expect(
      withControl.velocity.x > withoutControl.velocity.x &&
        withControl.velocity.y < withoutControl.velocity.y,
      "g_aircontrol 1 should rotate airborne velocity toward forward input"
    );
  }

  {
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    lg::MovementTuning lowCap;
    lowCap.maxGroundSpeed = 2.0F;
    lg::MovementTuning highCap = lowCap;
    highCap.maxGroundSpeed = 12.0F;
    lg::PlayerState slow = groundedPlayer();
    lg::PlayerState fast = groundedPlayer();
    slow.position.x = -6.0F;
    fast.position.x = -6.0F;

    runCommand(slow, command, lowCap, 20);
    runCommand(fast, command, highCap, 20);

    failures += expect(
      slow.velocity.x <= 2.01F && fast.velocity.x >= 11.9F,
      "g_maxspeed should set the sustained ground speed cap"
    );
  }

  {
    lg::UserCommand run;
    run.forwardMove = 1.0F;
    lg::UserCommand crouch = run;
    crouch.crouch = true;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 1000.0F;
    tuning.crouchTransitionSpeed = 1000.0F;
    lg::PlayerState standing = groundedPlayer();
    lg::PlayerState crouched = groundedPlayer();

    runCommand(standing, run, tuning, 20);
    runCommand(crouched, crouch, tuning, 20);

    failures += expect(
      crouched.crouched &&
        nearlyEqual(crouched.crouchAmount, 1.0F) &&
        nearlyEqual(crouched.bounds.halfHeight, crouched.standingBounds.halfHeight) &&
        nearlyEqual(crouched.bounds.radius, crouched.standingBounds.radius),
      "holding crouch should keep standing collision bounds"
    );
    failures += expect(
      crouched.velocity.x > standing.velocity.x * 0.5F &&
        crouched.velocity.x < standing.velocity.x * 0.7F,
      "holding crouch should reduce sustained ground speed without feeling glued down"
    );
    failures += expect(
      nearlyEqual(
        crouched.position.z,
        standing.position.z
      ),
      "crouching should not resize or lower the gameplay box"
    );
  }

  {
    lg::UserCommand crouch;
    crouch.crouch = true;
    lg::UserCommand stand;
    lg::MovementTuning tuning;
    tuning.crouchTransitionSpeed = 1000.0F;
    lg::PlayerState player = groundedPlayer();

    runCommand(player, crouch, tuning, 1);
    runCommand(player, stand, tuning, 1);

    failures += expect(
      !player.crouched &&
        nearlyEqual(player.crouchAmount, 0.0F) &&
        nearlyEqual(player.bounds.halfHeight, player.standingBounds.halfHeight),
      "releasing crouch should restore standing bounds"
    );
  }

  {
    lg::PlayerState player = groundedPlayer();
    lg::UserCommand command;
    command.jump = true;
    command.upMove = 1.0F;

    runCommand(player, command, 1);

    failures += expect(player.position.z > player.bounds.halfHeight, "jump should lift player off floor");
    failures += expect(player.movementMode == lg::MovementMode::Airborne, "jump should enter airborne mode");
    failures += expect(player.velocity.z > 0.0F, "jump should produce positive vertical velocity");
  }

  {
    lg::MovementTuning tuning;
    tuning.groundFriction = 20.0F;
    lg::PlayerState jumping = groundedPlayer();
    lg::PlayerState coasting = groundedPlayer();
    jumping.velocity.x = 8.0F;
    coasting.velocity.x = 8.0F;
    lg::UserCommand jump;
    jump.jump = true;
    jump.upMove = 1.0F;
    lg::UserCommand idle;

    runCommand(jumping, jump, tuning, 1);
    runCommand(coasting, idle, tuning, 1);

    failures += expect(
      jumping.velocity.x > 7.99F && coasting.velocity.x < 7.0F,
      "accepted jumps should skip ground friction on the takeoff tick"
    );
  }

  {
    lg::PlayerState player = groundedPlayer();
    lg::UserCommand heldJump;
    heldJump.jump = true;
    heldJump.upMove = 1.0F;

    runCommand(player, heldJump, 200);

    failures += expect(
      player.onGround &&
        nearlyEqual(player.position.z, player.bounds.halfHeight) &&
        player.jumpHeld,
      "holding jump should not automatically jump again after landing"
    );

    lg::UserCommand releaseJump;
    runCommand(player, releaseJump, 1);
    failures += expect(
      !player.jumpHeld,
      "releasing jump should rearm the Q3-style jump latch"
    );

    runCommand(player, heldJump, 1);
    failures += expect(
      !player.onGround && player.velocity.z > 0.0F,
      "pressing jump again after release should start another jump"
    );
  }

  {
    lg::PlayerState player;
    player.position = {1.0F, 2.0F, 3.0F};
    lg::MovementTuning tuning;
    tuning.flightEnabled = true;
    tuning.flightDamping = 0.0F;
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    command.viewPitchRadians = 0.78539816339F;
    runCommand(player, command, tuning, 10);

    failures += expect(
      player.movementMode == lg::MovementMode::Flying,
      "enabled flight should enter the Flying movement mode"
    );
    failures += expect(
      player.velocity.x > 0.0F && player.velocity.z > 0.0F,
      "flight forward thrust should follow camera yaw and pitch"
    );
  }

  {
    lg::PlayerState ascending = groundedPlayer();
    lg::PlayerState descending = groundedPlayer();
    ascending.position.z = 4.0F;
    descending.position.z = 4.0F;
    lg::MovementTuning tuning;
    tuning.flightEnabled = true;
    tuning.flightDamping = 0.0F;
    lg::UserCommand up;
    up.upMove = 1.0F;
    lg::UserCommand down;
    down.upMove = -1.0F;

    runCommand(ascending, up, tuning, 5);
    runCommand(descending, down, tuning, 5);

    failures += expect(
      ascending.velocity.z > 0.0F && descending.velocity.z < 0.0F,
      "flight up/down input should thrust vertically in both directions"
    );
  }

  {
    lg::PlayerState player = groundedPlayer();
    player.position.z = 4.0F;
    lg::MovementTuning tuning;
    tuning.flightEnabled = true;
    tuning.flightDamping = 0.0F;
    lg::UserCommand idle;

    runCommand(player, idle, tuning, 20);

    failures += expect(
      nearlyEqual(player.position.z, 4.0F) &&
        nearlyEqual(player.velocity.z, 0.0F),
      "unrestricted flight should fully cancel gravity while idle"
    );
  }

  {
    lg::PlayerState player = groundedPlayer();
    player.position.z = 4.0F;
    lg::MovementTuning tuning;
    tuning.flightEnabled = true;
    tuning.flightAcceleration = 1000.0F;
    tuning.maxFlightSpeed = 5.0F;
    tuning.flightDamping = 0.0F;
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, tuning, 20);

    failures += expect(
      lg::length(player.velocity) <= 5.01F,
      "flight velocity should respect g_flightmaxspeed"
    );
  }

  {
    const lg::Arena arena;
    lg::PlayerState player = groundedPlayer();
    player.position.z = arena.max.z - player.bounds.halfHeight - 0.05F;
    lg::MovementTuning tuning;
    tuning.flightEnabled = true;
    tuning.flightAcceleration = 1000.0F;
    tuning.flightDamping = 0.0F;
    lg::UserCommand up;
    up.upMove = 1.0F;

    for (int tick = 0; tick < 10; ++tick) {
      lg::simulateMovement(
        player,
        up,
        arena,
        tuning,
        lg::kFixedTickSeconds
      );
    }

    failures += expect(
      player.position.z <= arena.max.z - player.bounds.halfHeight &&
        nearlyEqual(player.velocity.z, 0.0F),
      "flight should respect authoritative arena ceiling collision"
    );
  }

  {
    lg::PlayerState player = groundedPlayer();
    player.position.z = 4.0F;
    player.velocity = {8.0F, 0.0F, 0.0F};
    lg::MovementTuning tuning;
    tuning.flightEnabled = true;
    tuning.flightDamping = 10.0F;
    lg::UserCommand idle;

    runCommand(player, idle, tuning, 10);

    failures += expect(
      player.velocity.x < 4.0F,
      "g_flightdamping should reduce unpowered flight velocity"
    );
  }

  {
    lg::PlayerState player = groundedPlayer();
    player.position.z = 4.0F;
    player.movementMode = lg::MovementMode::Flying;
    player.onGround = false;
    lg::MovementTuning tuning;
    tuning.flightEnabled = false;
    lg::UserCommand idle;

    runCommand(player, idle, tuning, 1);

    failures += expect(
      player.movementMode == lg::MovementMode::Airborne &&
        player.velocity.z < 0.0F,
      "disabling flight should transition to airborne gravity"
    );
  }

  {
    lg::PlayerState first = groundedPlayer();
    lg::PlayerState second = first;
    first.position.z = 4.0F;
    second.position.z = 4.0F;
    lg::MovementTuning tuning;
    tuning.flightEnabled = true;

    for (int tick = 0; tick < 64; ++tick) {
      lg::UserCommand command;
      command.sequence = static_cast<std::uint32_t>(tick);
      command.viewYawRadians = static_cast<float>(tick) * 0.01F;
      command.viewPitchRadians = 0.35F;
      command.forwardMove = 1.0F;
      command.rightMove = tick >= 32 ? 1.0F : 0.0F;
      command.upMove = tick < 16 ? 1.0F : -0.25F;
      runCommand(first, command, tuning, 1);
      runCommand(second, command, tuning, 1);
    }

    failures += expect(
      nearlyEqual(first.position.x, second.position.x) &&
        nearlyEqual(first.position.y, second.position.y) &&
        nearlyEqual(first.position.z, second.position.z) &&
        nearlyEqual(first.velocity.x, second.velocity.x) &&
        nearlyEqual(first.velocity.y, second.velocity.y) &&
        nearlyEqual(first.velocity.z, second.velocity.z),
      "flight command replay should remain deterministic"
    );
  }

  {
    const lg::Arena arena = lg::thunderstruckArena();
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {-5.4F, 0.0F, player.bounds.halfHeight};
    player.velocity = {8.0F, 3.0F, 0.0F};
    lg::UserCommand command;

    lg::simulateMovement(
      player,
      command,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );

    failures += expect(
      player.position.x <= -5.34F,
      "Thunderstruck central cover should block player movement"
    );
    failures += expect(
      player.velocity.y > 0.0F,
      "internal wall collision should preserve tangential sliding velocity"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{-2.0F, -2.0F, 0.0F}, {2.0F, 2.0F, 2.0F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.0F, 0.0F, 4.0F};
    player.velocity.z = -4.0F;
    player.onGround = false;
    player.movementMode = lg::MovementMode::Airborne;
    lg::UserCommand command;

    for (int tick = 0; tick < 100 && !player.onGround; ++tick) {
      lg::simulateMovement(
        player,
        command,
        arena,
        tuning,
        lg::kFixedTickSeconds
      );
    }

    failures += expect(
      player.onGround &&
        nearlyEqual(player.position.z, 2.0F + player.bounds.halfHeight),
      "players should land on raised arena geometry"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -1.0F, 0.0F}, {1.5F, 1.0F, 0.4F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position.x = 0.1F;
    player.velocity.x = 8.0F;
    lg::UserCommand command;

    lg::simulateMovement(
      player,
      command,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );

    failures += expect(
      player.onGround &&
        nearlyEqual(player.position.z, 0.4F + player.bounds.halfHeight),
      "grounded players should step onto low stair geometry"
    );
  }

  {
    const lg::Arena arena = lg::thunderstruckArena();
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {-5.5F, -3.5F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.viewYawRadians = 3.14159265359F;
    command.forwardMove = 1.0F;

    for (int tick = 0; tick < 140; ++tick) {
      lg::simulateMovement(
        player,
        command,
        arena,
        tuning,
        lg::kFixedTickSeconds
      );
    }

    failures += expect(
      player.position.x < -10.0F &&
        nearlyEqual(player.position.z, 2.0F + player.bounds.halfHeight),
      "Thunderstruck stairs should lead from the lower court to the raised lane"
    );
  }

  {
    lg::PlayerState first = groundedPlayer();
    lg::PlayerState second = groundedPlayer();

    for (int tick = 0; tick < 64; ++tick) {
      lg::UserCommand command;
      command.sequence = static_cast<std::uint32_t>(tick);
      command.clientTick = static_cast<std::uint32_t>(tick);
      command.forwardMove = tick < 32 ? 1.0F : 0.0F;
      command.rightMove = tick >= 16 && tick < 48 ? 1.0F : 0.0F;
      command.jump = tick == 8;
      command.upMove = command.jump ? 1.0F : 0.0F;

      runCommand(first, command, 1);
      runCommand(second, command, 1);
    }

    failures += expect(nearlyEqual(first.position.x, second.position.x), "replayed command sequence should match x");
    failures += expect(nearlyEqual(first.position.y, second.position.y), "replayed command sequence should match y");
    failures += expect(nearlyEqual(first.position.z, second.position.z), "replayed command sequence should match z");
    failures += expect(nearlyEqual(first.velocity.x, second.velocity.x), "replayed command sequence should match velocity x");
    failures += expect(nearlyEqual(first.velocity.y, second.velocity.y), "replayed command sequence should match velocity y");
    failures += expect(nearlyEqual(first.velocity.z, second.velocity.z), "replayed command sequence should match velocity z");
  }

  return failures == 0 ? 0 : 1;
}
