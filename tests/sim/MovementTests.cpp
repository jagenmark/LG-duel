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
#include <string>
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

lg::ArenaBrush cutUndersideBrushStep(float minX, float maxX, float topZ, float bottomRise) {
  lg::ArenaBrush brush;
  brush.min = {minX, -1.0F, 0.0F};
  brush.max = {maxX, 1.0F, topZ};
  brush.faceCount = 6;
  brush.faces[0].normal = {-1.0F, 0.0F, 0.0F};
  brush.faces[0].distance = -minX;
  brush.faces[1].normal = {1.0F, 0.0F, 0.0F};
  brush.faces[1].distance = maxX;
  brush.faces[2].normal = {0.0F, -1.0F, 0.0F};
  brush.faces[2].distance = 1.0F;
  brush.faces[3].normal = {0.0F, 1.0F, 0.0F};
  brush.faces[3].distance = 1.0F;
  brush.faces[4].normal = {0.0F, 0.0F, 1.0F};
  brush.faces[4].distance = topZ;

  const float slope = bottomRise / (maxX - minX);
  const lg::Vec3 bottomNormal = lg::normalize({slope, 0.0F, -1.0F});
  brush.faces[5].normal = bottomNormal;
  brush.faces[5].distance = bottomNormal.x * minX;
  return brush;
}

lg::ArenaBrush slopedTopBrush(
  float minX,
  float maxX,
  float zAtMinX,
  float zAtMaxX
) {
  lg::ArenaBrush brush;
  const float maxZ = std::max(zAtMinX, zAtMaxX);
  brush.min = {minX, -1.0F, 0.0F};
  brush.max = {maxX, 1.0F, maxZ};
  brush.faceCount = 6;
  brush.faces[0].normal = {-1.0F, 0.0F, 0.0F};
  brush.faces[0].distance = -minX;
  brush.faces[1].normal = {1.0F, 0.0F, 0.0F};
  brush.faces[1].distance = maxX;
  brush.faces[2].normal = {0.0F, -1.0F, 0.0F};
  brush.faces[2].distance = 1.0F;
  brush.faces[3].normal = {0.0F, 1.0F, 0.0F};
  brush.faces[3].distance = 1.0F;
  brush.faces[4].normal = {0.0F, 0.0F, -1.0F};
  brush.faces[4].distance = 0.0F;

  const float slope = (zAtMaxX - zAtMinX) / (maxX - minX);
  const lg::Vec3 topNormal = lg::normalize({-slope, 0.0F, 1.0F});
  brush.faces[5].normal = topNormal;
  brush.faces[5].distance = (topNormal.x * minX) + (topNormal.z * zAtMinX);
  return brush;
}

float slopedTopZ(const lg::ArenaBrush& brush, float x) {
  const lg::ArenaBrushFace& face = brush.faces[5];
  return (face.distance - (face.normal.x * x)) / face.normal.z;
}

lg::Arena arenaWithBrush(const lg::ArenaBrush& brush) {
  lg::Arena arena;
  arena.brushes[0] = brush;
  arena.brushCount = 1;
  return arena;
}

lg::Arena arenaWithCentralCover() {
  lg::Arena arena;
  arena.walls[0] = {{-5.0F, -0.9F, 0.0F}, {-3.0F, 0.9F, 1.2F}};
  arena.wallCount = 1;
  return arena;
}

lg::Arena arenaWithWestStairs() {
  lg::Arena arena;
  arena.walls[0] = {{-6.8F, -5.0F, 0.0F}, {-6.0F, -2.0F, 0.4F}};
  arena.walls[1] = {{-7.6F, -5.0F, 0.0F}, {-6.8F, -2.0F, 0.8F}};
  arena.walls[2] = {{-8.4F, -5.0F, 0.0F}, {-7.6F, -2.0F, 1.2F}};
  arena.walls[3] = {{-9.2F, -5.0F, 0.0F}, {-8.4F, -2.0F, 1.6F}};
  arena.walls[4] = {{-10.0F, -5.0F, 0.0F}, {-9.2F, -2.0F, 2.0F}};
  arena.walls[5] = {{-15.0F, -7.0F, 0.0F}, {-10.0F, 6.5F, 2.0F}};
  arena.wallCount = 6;
  return arena;
}

lg::Arena arenaWithRaisedDeck() {
  lg::Arena arena;
  arena.walls[0] = {{-10.0F, -10.0F, 0.0F}, {-6.0F, -8.0F, 2.0F}};
  arena.wallCount = 1;
  return arena;
}

float riseForAngle(float angleDegrees, float run) {
  constexpr float kPi = 3.14159265358979323846F;
  return std::tan(angleDegrees * kPi / 180.0F) * run;
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
  const lg::Arena& arena,
  const lg::MovementTuning& tuning,
  int ticks
) {
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

std::string basicMapWithBrush(std::string brush) {
  return
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "\"lg_bounds_min\" \"-160 -160 -40\"\n"
    "\"lg_bounds_max\" \"160 160 120\"\n" +
    brush +
    "}\n"
    "{\n"
    "\"classname\" \"lg_spawn\"\n"
    "\"origin\" \"-120 0 40\"\n"
    "}\n"
    "{\n"
    "\"classname\" \"lg_spawn\"\n"
    "\"origin\" \"120 0 40\"\n"
    "}\n";
}

lg::ArenaLoadResult loadArenaFixture(const std::string& path) {
  lg::ArenaLoadResult loaded = lg::loadArenaFromFile(path);
  if (loaded.ok) {
    return loaded;
  }
  loaded = lg::loadArenaFromFile("../" + path);
  if (loaded.ok) {
    return loaded;
  }
  loaded = lg::loadArenaFromFile("../../" + path);
  if (loaded.ok) {
    return loaded;
  }
  return lg::loadArenaFromFile("../../../" + path);
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
    lg::MovementTuning tuning;
    tuning.groundFriction = 20.0F;
    lg::PlayerState normal = groundedPlayer();
    lg::PlayerState disabledTimer = groundedPlayer();
    normal.velocity.x = 8.0F;
    disabledTimer.velocity.x = 8.0F;
    disabledTimer.knockbackTicksRemaining = 0;

    runCommand(normal, coast, tuning, 1);
    runCommand(disabledTimer, coast, tuning, 1);

    failures += expect(
      nearlyEqual(normal.velocity.x, disabledTimer.velocity.x),
      "disabled knockback timer should preserve normal grounded friction"
    );
  }

  {
    lg::UserCommand coast;
    lg::MovementTuning tuning;
    tuning.groundFriction = 20.0F;
    lg::PlayerState normal = groundedPlayer();
    lg::PlayerState knocked = groundedPlayer();
    normal.velocity.x = 8.0F;
    knocked.velocity.x = 8.0F;
    knocked.knockbackTicksRemaining = 2;

    runCommand(normal, coast, tuning, 1);
    runCommand(knocked, coast, tuning, 1);

    failures += expect(
      knocked.velocity.x > normal.velocity.x + 1.0F,
      "active knockback timer should skip grounded friction"
    );
    failures += expect(
      knocked.onGround &&
        knocked.movementMode == lg::MovementMode::Grounded,
      "active knockback timer should preserve physical grounded state"
    );
    failures += expect(
      knocked.knockbackTicksRemaining == 1,
      "knockback timer should decrement after the affected movement tick"
    );
  }

  {
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 100.0F;
    tuning.airAcceleration = 1.0F;
    tuning.maxGroundSpeed = 8.0F;
    tuning.maxAirSpeed = 8.0F;
    lg::PlayerState normal = groundedPlayer();
    lg::PlayerState knocked = groundedPlayer();
    knocked.knockbackTicksRemaining = 1;

    runCommand(normal, command, tuning, 1);
    runCommand(knocked, command, tuning, 1);

    failures += expect(
      normal.velocity.x > knocked.velocity.x * 10.0F,
      "grounded knockback timer should use air acceleration instead of ground acceleration"
    );
    failures += expect(
      knocked.knockbackTicksRemaining == 0,
      "one tick of knockback timer should expire after exactly one movement tick"
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
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 100.0F;
    lg::PlayerState normal = groundedPlayer();
    lg::PlayerState frozen = groundedPlayer();
    frozen.freezeLevel = 100.0F;

    runCommand(normal, command, tuning, 20);
    runCommand(frozen, command, tuning, 20);

    failures += expect(
      frozen.position.x < normal.position.x * 0.75F,
      "full freeze should slow grounded horizontal displacement"
    );
  }

  {
    lg::UserCommand idle;
    lg::PlayerState normal = groundedPlayer();
    lg::PlayerState frozen = groundedPlayer();
    normal.position.z = 5.0F;
    frozen.position.z = 5.0F;
    normal.onGround = false;
    frozen.onGround = false;
    normal.movementMode = lg::MovementMode::Airborne;
    frozen.movementMode = lg::MovementMode::Airborne;
    frozen.freezeLevel = 100.0F;

    runCommand(normal, idle, 20);
    runCommand(frozen, idle, 20);

    failures += expect(
      frozen.position.z > normal.position.z,
      "full freeze should slow airborne falling on the vertical axis"
    );
  }

  {
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    command.crouch = true;
    command.upMove = -1.0F;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 100.0F;
    lg::PlayerState player = groundedPlayer();
    const float standingHalfHeight = player.bounds.halfHeight;

    runCommand(player, command, tuning, 20);

    failures += expect(
      player.crouched &&
        player.bounds.halfHeight < standingHalfHeight &&
        nearlyEqual(player.position.z, player.bounds.halfHeight) &&
        player.velocity.x < tuning.maxGroundSpeed * 0.5F,
      "movedown should crouch, lower the grounded hitbox, and reduce ground speed when flight is off"
    );

    command.crouch = false;
    command.upMove = 0.0F;
    command.forwardMove = 0.0F;
    runCommand(player, command, tuning, 2);
    failures += expect(
      !player.crouched && nearlyEqual(player.bounds.halfHeight, standingHalfHeight),
      "releasing crouch should restore standing height when there is room"
    );
  }

  {
    lg::UserCommand command;
    command.forwardMove = 1.0F;
    command.sneak = true;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 100.0F;
    lg::PlayerState player = groundedPlayer();
    const float standingHalfHeight = player.bounds.halfHeight;

    runCommand(player, command, tuning, 20);

    failures += expect(
      player.sneaking &&
        !player.crouched &&
        nearlyEqual(player.bounds.halfHeight, standingHalfHeight) &&
        player.velocity.x < tuning.maxGroundSpeed * 0.7F,
      "sneak should reduce grounded speed without changing crouch height"
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
    lg::UserCommand command;
    command.crouch = true;
    command.upMove = -1.0F;

    runCommand(player, command, tuning, 5);

    failures += expect(
      player.movementMode == lg::MovementMode::Flying &&
        !player.crouched &&
        player.velocity.z < 0.0F,
      "movedown should stay flight descent instead of crouching when flight is enabled"
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
    const lg::Arena arena = arenaWithCentralCover();
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
      "central cover should block player movement"
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
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -1.0F, 0.0F}, {1.2F, 1.0F, 0.3F}};
    arena.walls[1] = {{1.2F, -1.0F, 0.0F}, {1.9F, 1.0F, 0.6F}};
    arena.walls[2] = {{1.9F, -1.0F, 0.0F}, {2.6F, 1.0F, 0.9F}};
    arena.walls[3] = {{2.6F, -1.0F, 0.0F}, {8.0F, 1.0F, 0.9F}};
    arena.wallCount = 4;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position.x = 0.1F;
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 80);

    failures += expect(
      player.position.x > 2.0F &&
        player.onGround &&
        nearlyEqual(player.position.z, 0.9F + player.bounds.halfHeight),
      "grounded players should walk up boxed stairs no taller than stepheight"
    );
  }

  {
    lg::Arena arena;
    for (std::size_t index = 0; index < 64; ++index) {
      const float x0 = 0.5F + (static_cast<float>(index) * 0.35F);
      arena.walls[index] = {
        {x0, -1.0F, 0.0F},
        {x0 + 0.35F, 1.0F, 0.08F * static_cast<float>(index + 1)},
      };
    }
    arena.wallCount = 64;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight};
    lg::UserCommand idleCommand;
    runCommand(player, idleCommand, arena, tuning, 20);
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 120);
    const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);

    failures += expect(
      player.position.x > 4.5F &&
        horizontalSpeed > 7.5F &&
        player.onGround,
      "tiny box stairs should accelerate from rest at the base to normal max speed"
    );
  }

  {
    lg::Arena arena;
    for (std::size_t index = 0; index < 64; ++index) {
      const float x0 = 0.5F + (static_cast<float>(index) * 0.23F);
      arena.walls[index] = {
        {x0, -1.0F, 0.0F},
        {x0 + 0.23F, 1.0F, 0.17F * static_cast<float>(index + 1)},
      };
    }
    arena.wallCount = 64;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.45F, 0.0F, player.bounds.halfHeight};
    lg::UserCommand idleCommand;
    runCommand(player, idleCommand, arena, tuning, 20);
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 40);
    const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);

    failures += expect(
      player.position.x > 2.0F &&
        horizontalSpeed > 7.5F &&
        player.onGround,
      "dense narrow stairs should accelerate from rest at the base without getting stuck"
    );
  }

  {
    lg::Arena arena;
    arena.brushes[0] = cutUndersideBrushStep(0.5F, 1.2F, 0.3F, 0.15F);
    arena.brushes[1] = cutUndersideBrushStep(1.2F, 1.9F, 0.6F, 0.15F);
    arena.brushes[2] = cutUndersideBrushStep(1.9F, 2.6F, 0.9F, 0.15F);
    arena.brushes[3] = cutUndersideBrushStep(2.6F, 4.0F, 1.2F, 0.15F);
    arena.brushCount = 4;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 40);
    const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);

    failures += expect(
      player.position.x > 2.0F &&
        player.position.z >= 0.9F + player.bounds.halfHeight - 0.01F &&
        horizontalSpeed > 7.5F &&
        player.onGround,
      "brush stairs with diagonally cut undersides should step by their walkable tops"
    );
  }

  {
    lg::Arena arena;
    arena.brushes[0] = cutUndersideBrushStep(0.5F, 1.2F, 0.3F, 0.15F);
    arena.brushCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight + 0.04F};
    player.velocity = {8.0F, 0.0F, 1.0F};
    player.onGround = false;
    player.movementMode = lg::MovementMode::Airborne;
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      player.position.x > 0.16F &&
        player.velocity.x > 7.5F &&
        !player.onGround,
      "rising bhop into a low brush stair should keep horizontal speed"
    );
  }

  {
    lg::Arena arena;
    arena.brushes[0] = cutUndersideBrushStep(0.5F, 1.2F, 0.3F, 0.15F);
    arena.brushCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight + 0.25F};
    player.velocity = {8.0F, 0.0F, -2.0F};
    player.onGround = false;
    player.movementMode = lg::MovementMode::Airborne;
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      player.position.x > 0.16F &&
        player.velocity.x > 7.5F,
      "falling bhop into a low brush stair should keep horizontal speed"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -10.0F, 0.0F}, {1.2F, 10.0F, 0.3F}};
    arena.walls[1] = {{1.2F, -10.0F, 0.0F}, {1.9F, 10.0F, 0.6F}};
    arena.walls[2] = {{1.9F, -10.0F, 0.0F}, {2.6F, 10.0F, 0.9F}};
    arena.wallCount = 3;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.9F, -3.0F, 0.3F + player.bounds.halfHeight};
    lg::UserCommand command;
    command.viewYawRadians = 1.48352981F;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 140);

    failures += expect(
      player.position.x > 1.25F &&
        player.position.y > 0.5F &&
        player.onGround &&
        player.position.z >= 0.6F + player.bounds.halfHeight - 0.01F,
      "angled movement along a stair tread should still step up when reaching the next riser"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -1.0F, 0.0F}, {1.2F, 1.0F, 0.3F}};
    arena.walls[1] = {{1.2F, -1.0F, 0.0F}, {1.9F, 1.0F, 0.6F}};
    arena.walls[2] = {{1.9F, -1.0F, 0.0F}, {2.6F, 1.0F, 0.9F}};
    arena.walls[3] = {{2.6F, -1.0F, 0.0F}, {4.0F, 1.0F, 1.2F}};
    arena.wallCount = 4;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 18);
    const float preJumpHorizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);
    command.jump = true;
    command.upMove = 1.0F;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
    const float postJumpHorizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);

    failures += expect(
      !player.onGround &&
        player.velocity.z > 0.0F &&
        postJumpHorizontalSpeed > 7.5F &&
        postJumpHorizontalSpeed > preJumpHorizontalSpeed * 0.95F &&
        player.velocity.x > 7.5F &&
        std::fabs(player.velocity.y) < 0.001F,
      "jumping while running up stairs should preserve horizontal speed and movement angle"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -1.0F, 0.0F}, {1.2F, 1.0F, 0.3F}};
    arena.walls[1] = {{1.2F, -1.0F, 0.0F}, {1.9F, 1.0F, 0.6F}};
    arena.walls[2] = {{1.9F, -1.0F, 0.0F}, {2.6F, 1.0F, 0.9F}};
    arena.walls[3] = {{2.6F, -1.0F, 0.0F}, {4.0F, 1.0F, 1.2F}};
    arena.wallCount = 4;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 10);
    command.jump = true;
    command.upMove = 1.0F;
    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
    command.jump = false;
    command.upMove = 0.0F;

    float minimumHorizontalSpeed = 1000.0F;
    for (int tick = 0; tick < 40; ++tick) {
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      minimumHorizontalSpeed =
        std::min(minimumHorizontalSpeed, std::hypot(player.velocity.x, player.velocity.y));
    }

    failures += expect(
      player.position.x > 2.6F &&
        player.position.z > 0.9F + player.bounds.halfHeight &&
        minimumHorizontalSpeed > 6.5F,
      "bhopping into low stairs should step over risers without getting stuck"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -1.0F, 0.0F}, {1.2F, 1.0F, 0.3F}};
    arena.walls[1] = {{1.2F, -1.0F, 0.0F}, {1.9F, 1.0F, 0.6F}};
    arena.walls[2] = {{1.9F, -1.0F, 0.0F}, {2.6F, 1.0F, 0.9F}};
    arena.walls[3] = {{2.6F, -1.0F, 0.0F}, {4.0F, 1.0F, 1.2F}};
    arena.wallCount = 4;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 10);
    int acceptedJumps = 0;
    float minimumHorizontalSpeed = 1000.0F;
    for (int tick = 0; tick < 90; ++tick) {
      command.jump = player.onGround;
      command.upMove = command.jump ? 1.0F : 0.0F;
      const bool wasOnGround = player.onGround;
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      if (command.jump && wasOnGround && !player.onGround) {
        ++acceptedJumps;
      }
      if (!command.jump) {
        player.jumpHeld = false;
      }
      minimumHorizontalSpeed =
        std::min(minimumHorizontalSpeed, std::hypot(player.velocity.x, player.velocity.y));
    }

    failures += expect(
      acceptedJumps >= 2 &&
        player.position.x > 3.0F &&
        minimumHorizontalSpeed > 6.0F,
      "repeated bhops should climb low stairs without losing horizontal speed"
    );
  }

  {
    lg::Arena arena;
    for (std::size_t index = 0; index < 8; ++index) {
      const float x0 = 0.5F + (static_cast<float>(index) * 0.34F);
      arena.walls[index] = {
        {x0, -1.0F, 0.0F},
        {x0 + 0.34F, 1.0F, 0.42F * static_cast<float>(index + 1)},
      };
    }
    arena.walls[8] = {{3.22F, -1.0F, 0.0F}, {9.0F, 1.0F, 3.36F}};
    arena.wallCount = 9;
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 10);
    int acceptedJumps = 0;
    float minimumHorizontalSpeed = 1000.0F;
    for (int tick = 0; tick < 90; ++tick) {
      command.jump = player.onGround;
      command.upMove = command.jump ? 1.0F : 0.0F;
      const bool wasOnGround = player.onGround;
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      if (command.jump && wasOnGround && !player.onGround) {
        ++acceptedJumps;
      }
      if (!command.jump) {
        player.jumpHeld = false;
      }
      const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);
      minimumHorizontalSpeed = std::min(minimumHorizontalSpeed, horizontalSpeed);
    }

    failures += expect(
      acceptedJumps >= 2 &&
        player.position.x > 2.5F &&
        player.position.z > 2.0F + player.bounds.halfHeight &&
        minimumHorizontalSpeed > 5.5F,
      "repeated bhops should climb steep stairs near max stepheight"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -1.0F, 0.0F}, {1.2F, 1.0F, 0.3F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.45F, 0.0F, player.bounds.halfHeight + 0.04F};
    player.velocity = {8.0F, 0.0F, 0.0F};
    player.onGround = false;
    player.movementMode = lg::MovementMode::Airborne;
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      player.position.x > 0.5F &&
        player.velocity.x > 7.5F,
      "airborne bhop already touching a low stair riser should still step over it"
    );
  }

  {
    const lg::Arena arena = arenaWithWestStairs();
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {-5.5F, -3.5F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.viewYawRadians = 3.14159265F;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 160);

    failures += expect(
      player.position.x < -9.0F &&
        player.onGround &&
        player.position.z > 2.0F + player.bounds.halfHeight - 0.01F,
      "players should climb explicit box stairs"
    );
  }

  {
    const lg::Arena arena = arenaWithWestStairs();
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {-5.5F, -3.5F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.viewYawRadians = 3.14159265F;
    command.forwardMove = 1.0F;

    runCommand(player, command, arena, tuning, 18);
    command.jump = true;
    command.upMove = 1.0F;
    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
    command.jump = false;
    command.upMove = 0.0F;

    float minimumHorizontalSpeed = 1000.0F;
    for (int tick = 0; tick < 60; ++tick) {
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      minimumHorizontalSpeed =
        std::min(minimumHorizontalSpeed, std::hypot(player.velocity.x, player.velocity.y));
    }

    failures += expect(
      player.position.x < -9.0F &&
        player.position.z > 1.5F + player.bounds.halfHeight &&
        minimumHorizontalSpeed > 6.5F,
      "bhopping up explicit stairs should not snag on stair risers"
    );
  }

  {
    lg::ArenaLoadResult loaded = loadArenaFixture("maps/thunderstruck.map");
    failures += expect(loaded.ok, "thunderstruck.map should load for file-backed movement regressions");
    if (loaded.ok) {
      const lg::Arena& arena = loaded.arena;
      const lg::MovementTuning tuning;
      lg::PlayerState player = groundedPlayer();
      player.position = arena.spawnPositions[0];
      player.position.z += player.bounds.halfHeight;
      lg::UserCommand command;
      command.jump = true;
      command.upMove = 1.0F;

      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      command.jump = false;
      command.upMove = 0.0F;
      runCommand(player, command, arena, tuning, 8);

      failures += expect(
        !player.onGround &&
          player.position.z > arena.spawnPositions[0].z + player.bounds.halfHeight + 0.2F &&
          player.velocity.z > 0.0F,
        "jumping from thunderstruck.map spawn geometry should stay airborne after takeoff"
      );
    }
  }

  {
    lg::Arena arena;
    arena.walls[0] = {
      {0.5F, -1.0F, 0.0F},
      {1.5F, 1.0F, lg::kPlayerStepHeight + 0.2F},
    };
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
      player.position.x <= 0.151F &&
        player.onGround &&
        nearlyEqual(player.position.z, player.bounds.halfHeight),
      "grounded players should not step onto ledges higher than stepheight"
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
    command.jump = true;
    command.upMove = 1.0F;

    lg::simulateMovement(
      player,
      command,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );

    failures += expect(
      !player.onGround &&
        player.position.z > player.bounds.halfHeight &&
        player.velocity.z > 0.0F &&
        player.velocity.x > 7.9F,
      "jumping near stairs should take off without losing horizontal velocity"
    );

    command.jump = false;
    command.upMove = 0.0F;
    lg::simulateMovement(
      player,
      command,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );

    failures += expect(
      !player.onGround &&
        player.velocity.z > 0.0F &&
        player.velocity.x > 7.9F &&
        std::fabs(player.velocity.y) < 0.001F,
      "airborne stair contact after jumping should not kill or rotate horizontal velocity"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -1.0F, 0.0F}, {1.5F, 1.0F, 0.4F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight + 0.2F};
    player.velocity = {8.0F, 0.0F, -3.0F};
    player.onGround = false;
    player.movementMode = lg::MovementMode::Airborne;
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
        nearlyEqual(player.position.z, 0.4F + player.bounds.halfHeight) &&
        player.velocity.x > 7.9F &&
        std::fabs(player.velocity.y) < 0.001F,
      "jumping head-on into a low stair should land on it without killing or rotating horizontal velocity"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{-2.0F, -2.0F, 0.0F}, {2.0F, 2.0F, 0.4F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.0F, 0.0F, 0.4F + player.bounds.halfHeight};
    lg::UserCommand command;
    command.jump = true;
    command.upMove = 1.0F;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      !player.onGround &&
        player.position.z > 0.4F + player.bounds.halfHeight &&
        player.velocity.z > 0.0F,
      "jumping from a box floor should not be clipped back onto the box"
    );
  }

  {
    const lg::Arena arena = arenaWithRaisedDeck();
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {-8.0F, -9.0F, 2.0F + player.bounds.halfHeight};
    lg::UserCommand command;
    command.jump = true;
    command.upMove = 1.0F;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
    command.jump = false;
    command.upMove = 0.0F;
    runCommand(player, command, arena, tuning, 8);

    failures += expect(
      !player.onGround &&
        player.position.z > 2.0F + player.bounds.halfHeight + 0.2F &&
        player.velocity.z > 0.0F,
      "jumping from a raised deck should stay airborne after takeoff"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{-2.0F, -2.0F, 0.0F}, {2.0F, 2.0F, 0.4F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.0F, 0.0F, 0.4F + player.bounds.halfHeight};
    player.velocity.z = 9.0F;
    player.knockbackTicksRemaining = 2;
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      !player.onGround &&
        player.position.z > 0.4F + player.bounds.halfHeight &&
        player.velocity.z > 0.0F,
      "upward knockback from a box floor should not be clipped back onto the box"
    );
  }

  {
    const lg::Arena arena = arenaWithRaisedDeck();
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {-8.0F, -9.0F, 2.0F + player.bounds.halfHeight};
    player.velocity.z = 9.0F;
    player.knockbackTicksRemaining = 2;
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
    runCommand(player, command, arena, tuning, 8);

    failures += expect(
      !player.onGround &&
        player.position.z > 2.0F + player.bounds.halfHeight + 0.2F &&
        player.velocity.z > 0.0F,
      "upward knockback from a raised deck should stay airborne after takeoff"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{-2.0F, -2.0F, 0.0F}, {2.0F, 2.0F, 0.4F}};
    arena.wallCount = 1;
    arena.jumpPads[0].min = {-1.0F, -1.0F, 0.4F};
    arena.jumpPads[0].max = {1.0F, 1.0F, 1.2F};
    arena.jumpPads[0].launchVelocity = {0.0F, 0.0F, 12.0F};
    arena.jumpPadCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.0F, 0.0F, 0.4F + player.bounds.halfHeight};
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      !player.onGround &&
        player.velocity.z > 11.9F &&
        player.movementMode == lg::MovementMode::Airborne,
      "jumppad on a box floor should launch instead of being clipped to ground"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, -1.0F, 0.0F}, {1.5F, 1.0F, 0.4F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, 0.0F, player.bounds.halfHeight + 0.2F};
    player.velocity.x = 8.0F;
    player.velocity.z = 3.0F;
    player.onGround = false;
    player.movementMode = lg::MovementMode::Airborne;
    lg::UserCommand command;

    lg::simulateMovement(
      player,
      command,
      arena,
      tuning,
      lg::kFixedTickSeconds
    );

    failures += expect(
      !player.onGround &&
        player.velocity.z > 0.0F &&
        player.velocity.x > 7.9F &&
        std::fabs(player.velocity.y) < 0.001F,
      "upward airborne stair contact should preserve horizontal velocity and stay airborne"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{1.0F, -4.0F, 0.0F}, {2.0F, 4.0F, 4.0F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.55F, 0.0F, player.bounds.halfHeight};
    player.velocity = {20.0F, 8.0F, 0.0F};
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      player.position.x <= 0.651F &&
        player.position.y > 0.02F &&
        std::fabs(player.velocity.x) < 0.05F &&
        player.velocity.y > 7.0F,
      "diagonal wall impact should preserve tangential slide velocity"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{0.5F, 0.3F, 0.0F}, {1.5F, 1.3F, 0.4F}};
    arena.wallCount = 1;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.1F, -0.1F, player.bounds.halfHeight + 0.35F};
    player.velocity = {8.0F, 4.0F, 3.0F};
    player.onGround = false;
    player.movementMode = lg::MovementMode::Airborne;
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      !player.onGround &&
        player.position.y > -0.08F &&
        player.velocity.y > 3.0F,
      "airborne diagonal stair-side impact should slide instead of zeroing horizontal velocity"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{1.0F, -4.0F, 0.0F}, {2.0F, 4.0F, 4.0F}};
    arena.walls[1] = {{-4.0F, 1.0F, 0.0F}, {4.0F, 2.0F, 4.0F}};
    arena.wallCount = 2;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.62F, 0.62F, player.bounds.halfHeight};
    player.velocity = {20.0F, 20.0F, 0.0F};
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);

    failures += expect(
      player.position.x <= 0.651F &&
        player.position.y <= 0.651F &&
        nearlyEqual(player.velocity.x, 0.0F) &&
        nearlyEqual(player.velocity.y, 0.0F),
      "90-degree corner impact should stop without tunneling"
    );
  }

  {
    lg::Arena arena;
    arena.walls[0] = {{1.0F, -4.0F, 0.0F}, {2.0F, 0.0F, 4.0F}};
    arena.walls[1] = {{1.0F, 0.0F, 0.0F}, {2.0F, 4.0F, 4.0F}};
    arena.wallCount = 2;
    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {0.55F, -0.8F, player.bounds.halfHeight};
    player.velocity = {4.0F, 12.0F, 0.0F};
    lg::UserCommand command;

    for (int tick = 0; tick < 40; ++tick) {
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
    }

    failures += expect(
      player.position.y > 0.4F &&
        player.position.x <= 0.651F &&
        player.velocity.y > 1.0F,
      "adjacent coplanar wall pieces should not snag sliding movement"
    );
  }

  {
    lg::Arena arena;
    arena.min = {-4.0F, -4.0F, 0.0F};
    arena.max = {4.0F, 4.0F, 40.0F};
    arena.jumpPadCount = 1;
    arena.jumpPads[0].min = {-1.0F, -1.0F, 0.0F};
    arena.jumpPads[0].max = {1.0F, 1.0F, 40.0F};
    arena.jumpPads[0].launchVelocity = {2.0F, 0.0F, 10.0F};

    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    lg::UserCommand command;

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds, 3);
    failures += expect(!player.onGround, "jumppad trigger should force airborne state");
    failures += expect(
      player.movementMode == lg::MovementMode::Airborne,
      "jumppad trigger should force airborne movement mode"
    );
    failures += expect(
      nearlyEqual(player.velocity.x, 2.0F) &&
        nearlyEqual(player.velocity.z, 10.0F),
      "jumppad trigger should set launch velocity"
    );
    failures += expect(
      player.jumpPadCooldownTicksRemaining == 3,
      "jumppad trigger should arm retrigger cooldown"
    );

    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds, 3);
    failures += expect(
      player.jumpPadCooldownTicksRemaining == 2 &&
        player.velocity.z < 10.0F,
      "jumppad cooldown should prevent immediate retrigger"
    );
    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds, 3);
    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds, 3);
    failures += expect(
      player.jumpPadCooldownTicksRemaining == 0 &&
        player.velocity.z < 10.0F,
      "jumppad cooldown should count down without relaunching on the expiry tick"
    );
    lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds, 3);
    failures += expect(
      player.jumpPadCooldownTicksRemaining == 3 &&
        nearlyEqual(player.velocity.z, 10.0F),
      "jumppad should retrigger after cooldown expires while still overlapping"
    );
  }

  {
    const lg::Arena arena = arenaWithWestStairs();
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
      "explicit stairs should lead from the lower court to the raised lane"
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

  {
    const float run = 4.0F;
    const lg::ArenaBrush ramp =
      slopedTopBrush(-2.0F, 2.0F, 1.8F, 1.8F - riseForAngle(15.0F, run));
    const lg::Arena arena = arenaWithBrush(ramp);
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {-1.5F, 0.0F, slopedTopZ(ramp, -1.5F) + player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    const float startZ = player.position.z;
    int airborneTicks = 0;
    for (int tick = 0; tick < 30; ++tick) {
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      if (!player.onGround || player.movementMode != lg::MovementMode::Grounded) {
        ++airborneTicks;
      }
    }

    failures += expect(airborneTicks == 0, "15 degree downhill ramp should not flicker airborne");
    failures += expect(player.onGround, "15 degree downhill ramp should remain walkable ground");
    failures += expect(player.position.z < startZ - 0.1F, "15 degree downhill ramp should move player downward along the plane");
  }

  {
    const float run = 4.0F;
    const lg::ArenaBrush ramp =
      slopedTopBrush(-2.0F, 2.0F, 3.0F, 3.0F - riseForAngle(30.0F, run));
    const lg::Arena arena = arenaWithBrush(ramp);
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {-1.5F, 0.0F, slopedTopZ(ramp, -1.5F) + player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    const float startZ = player.position.z;
    int airborneTicks = 0;
    for (int tick = 0; tick < 24; ++tick) {
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      if (!player.onGround || player.movementMode != lg::MovementMode::Grounded) {
        ++airborneTicks;
      }
    }

    failures += expect(airborneTicks == 0, "30 degree downhill ramp should stay grounded");
    failures += expect(player.position.z < startZ - 0.25F, "30 degree downhill ramp should follow z downward");
  }

  {
    const float run = 4.0F;
    const lg::ArenaBrush ramp =
      slopedTopBrush(-2.0F, 2.0F, 3.0F, 3.0F - riseForAngle(30.0F, run));
    const lg::Arena arena = arenaWithBrush(ramp);
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {-0.25F, 0.0F, slopedTopZ(ramp, -0.25F) + player.bounds.halfHeight};
    lg::UserCommand command;
    command.viewYawRadians = 0.78539816339F;

    const lg::Vec3 startPosition = player.position;
    for (int tick = 0; tick < 60; ++tick) {
      command.rightMove = tick % 2 == 0 ? 1.0F : -1.0F;
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
    }

    const lg::Vec3 horizontalDelta = {
      player.position.x - startPosition.x,
      player.position.y - startPosition.y,
      0.0F,
    };
    const float forwardDistance = lg::dot(horizontalDelta, lg::yawForward(command.viewYawRadians));
    failures += expect(
      std::fabs(forwardDistance) < 0.12F,
      "alternating AD on an angled slope should not turn into forward/back movement"
    );
  }

  {
    const float run = 4.0F;
    const lg::ArenaBrush ramp =
      slopedTopBrush(-2.0F, 2.0F, 3.0F, 3.0F - riseForAngle(30.0F, run));
    const lg::Arena arena = arenaWithBrush(ramp);
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {-0.25F, 0.0F, slopedTopZ(ramp, -0.25F) + player.bounds.halfHeight};
    lg::UserCommand command;
    command.viewYawRadians = 1.57079632679F;
    command.rightMove = 1.0F;

    const lg::Vec3 startPosition = player.position;
    for (int tick = 0; tick < 30; ++tick) {
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
    }

    const lg::Vec3 horizontalDelta = {
      player.position.x - startPosition.x,
      player.position.y - startPosition.y,
      0.0F,
    };
    const float forwardDistance = lg::dot(horizontalDelta, lg::yawForward(command.viewYawRadians));
    const float strafeDistance = lg::dot(horizontalDelta, lg::yawRight(command.viewYawRadians));
    failures += expect(
      std::fabs(forwardDistance) < 0.05F && strafeDistance > 0.1F,
      "pure AD across a slope should stay camera-sideways instead of forward/back"
    );
  }

  {
    const lg::ArenaLoadResult loaded = loadArenaFixture("maps/stairs.map");
    failures += expect(loaded.ok, "stairs map should load for slope strafe regression");

    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {6.16667F, 8.4F, -23.05F};
    lg::UserCommand command;
    command.rightMove = 1.0F;

    const lg::Vec3 startPosition = player.position;
    for (int tick = 0; tick < 30; ++tick) {
      lg::simulateMovement(player, command, loaded.arena, tuning, lg::kFixedTickSeconds);
    }

    const lg::Vec3 horizontalDelta = {
      player.position.x - startPosition.x,
      player.position.y - startPosition.y,
      0.0F,
    };
    const float strafeDistance = lg::dot(horizontalDelta, lg::yawRight(command.viewYawRadians));
    failures += expect(
      strafeDistance > 0.5F && player.onGround,
      "pure AD on stairs map ramp should remain grounded and move sideways"
    );
  }

  {
    const lg::ArenaLoadResult loaded = loadArenaFixture("maps/stairs.map");
    failures += expect(loaded.ok, "stairs map should load for released strafe ramp regression");

    lg::PlayerState player = groundedPlayer();
    player.position = {16.006F, 9.058F, -22.783F};
    player.velocity = {};
    lg::UserCommand command;
    command.viewYawRadians = 0.0F;
    command.rightMove = 1.0F;

    const lg::Vec3 startPosition = player.position;
    float maxDownRampDrift = 0.0F;
    float maxTickDownRampDrift = 0.0F;
    int airborneTicks = 0;
    for (int tick = 0; tick < 30; ++tick) {
      if (tick == 10) {
        command.rightMove = 0.0F;
      }
      const lg::Vec3 before = player.position;
      lg::simulateMovement(
        player,
        command,
        loaded.arena,
        lg::MovementTuning{},
        lg::kFixedTickSeconds
      );
      maxDownRampDrift = std::max(maxDownRampDrift, startPosition.x - player.position.x);
      maxTickDownRampDrift =
        std::max(maxTickDownRampDrift, before.x - player.position.x);
      if (!player.onGround || player.movementMode != lg::MovementMode::Grounded) {
        ++airborneTicks;
      }
    }

    failures += expect(
      airborneTicks == 0 &&
        maxDownRampDrift < 0.02F &&
        maxTickDownRampDrift < 0.005F,
      "releasing pure AD on a stairs map ramp should not create down-ramp slide"
    );
  }

  {
    const lg::ArenaLoadResult loaded = loadArenaFixture("maps/stairs.map");
    failures += expect(loaded.ok, "stairs map should load for uphill release ramp regression");

    lg::PlayerState player = groundedPlayer();
    player.position = {18.071F, 8.132F, -22.257F};
    player.velocity = {-3.633F, -0.539F, -0.925F};
    lg::UserCommand command;
    command.viewYawRadians = 8.4F * 3.14159265358979323846F / 180.0F;
    command.viewPitchRadians = -16.2F * 3.14159265358979323846F / 180.0F;

    float previousDownRampStep = 1000.0F;
    float maxStepIncrease = 0.0F;
    int airborneTicks = 0;
    for (int tick = 0; tick < 20; ++tick) {
      const lg::Vec3 before = player.position;
      lg::simulateMovement(
        player,
        command,
        loaded.arena,
        lg::MovementTuning{},
        lg::kFixedTickSeconds
      );
      const float downRampStep = before.x - player.position.x;
      maxStepIncrease =
        std::max(maxStepIncrease, downRampStep - previousDownRampStep);
      previousDownRampStep = downRampStep;
      if (!player.onGround || player.movementMode != lg::MovementMode::Grounded) {
        ++airborneTicks;
      }
    }

    failures += expect(
      airborneTicks == 0 && maxStepIncrease < 0.002F,
      "releasing uphill input on a ramp should coast down smoothly without snap-down jitter"
    );
  }

  {
    const lg::ArenaLoadResult loaded = loadArenaFixture("maps/stairs.map");
    failures += expect(loaded.ok, "stairs map should load for ramp entry regression");

    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {3.75F, 8.4F, -25.2F + player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    int airborneTicks = 0;
    float minimumX = player.position.x;
    for (int tick = 0; tick < 50; ++tick) {
      lg::simulateMovement(player, command, loaded.arena, tuning, lg::kFixedTickSeconds);
      minimumX = std::min(minimumX, player.position.x);
      if (!player.onGround || player.movementMode != lg::MovementMode::Grounded) {
        ++airborneTicks;
      }
    }

    failures += expect(
      airborneTicks == 0 &&
        minimumX >= 3.74F &&
        player.position.x > 5.5F &&
        player.position.z > -23.6F,
      "entering a stairs map ramp from flat ground should stay grounded and climb"
    );
  }

  {
    const float run = 4.0F;
    const lg::ArenaBrush ramp =
      slopedTopBrush(-2.0F, 2.0F, 0.0F, riseForAngle(15.0F, run));
    const lg::Arena arena = arenaWithBrush(ramp);
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 10.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {-2.35F, 0.0F, player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    float minimumX = player.position.x;
    float highestZ = player.position.z;
    int airborneTicks = 0;
    for (int tick = 0; tick < 40; ++tick) {
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      minimumX = std::min(minimumX, player.position.x);
      highestZ = std::max(highestZ, player.position.z);
      if (!player.onGround || player.movementMode != lg::MovementMode::Grounded) {
        ++airborneTicks;
      }
    }

    failures += expect(
      airborneTicks == 0 &&
        minimumX >= -2.36F &&
        player.position.x > -1.7F &&
        highestZ > player.bounds.halfHeight + 0.08F,
      "entering a flush ramp from flat ground at low speed should climb without sliding back"
    );
  }

  {
    const float run = 4.0F;
    const lg::ArenaBrush ramp =
      slopedTopBrush(-2.0F, 2.0F, 4.2F, 4.2F - riseForAngle(44.0F, run));
    const lg::Arena arena = arenaWithBrush(ramp);
    lg::MovementTuning tuning;
    tuning.groundAcceleration = 80.0F;
    lg::PlayerState player = groundedPlayer();
    player.position = {-1.7F, 0.0F, slopedTopZ(ramp, -1.7F) + player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    int airborneTicks = 0;
    for (int tick = 0; tick < 12; ++tick) {
      lg::simulateMovement(player, command, arena, tuning, lg::kFixedTickSeconds);
      if (!player.onGround || player.movementMode != lg::MovementMode::Grounded) {
        ++airborneTicks;
      }
    }

    failures += expect(airborneTicks == 0, "44 degree ramp should still be walkable");
    failures += expect(player.position.x > -1.2F, "44 degree walkable ramp should allow forward movement");
  }

  {
    const float run = 2.0F;
    const lg::ArenaBrush ramp =
      slopedTopBrush(-1.0F, 1.0F, 3.0F, 3.0F - riseForAngle(50.0F, run));
    const lg::Arena arena = arenaWithBrush(ramp);
    lg::PlayerState player = groundedPlayer();
    player.position = {-0.5F, 0.0F, slopedTopZ(ramp, -0.5F) + player.bounds.halfHeight};
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    lg::simulateMovement(player, command, arena, lg::MovementTuning{}, lg::kFixedTickSeconds);

    failures += expect(!player.onGround, "50 degree ramp should not count as walking ground");
    failures += expect(
      player.movementMode == lg::MovementMode::Airborne,
      "50 degree ramp should be treated as steep slope rather than grounded walk"
    );
  }

  {
    const float run = 4.0F;
    const float highZ = 3.0F;
    const float lowZ = highZ - riseForAngle(30.0F, run);
    const lg::ArenaBrush ramp = slopedTopBrush(-2.0F, 2.0F, highZ, lowZ);
    const lg::Arena arena = arenaWithBrush(ramp);
    lg::PlayerState player = groundedPlayer();
    player.position = {-1.7F, 0.0F, slopedTopZ(ramp, -1.7F) + player.bounds.halfHeight};
    const float slope = (lowZ - highZ) / run;
    player.velocity = lg::normalize(lg::Vec3{1.0F, 0.0F, slope}) * 8.0F;
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    int airborneTicks = 0;
    for (int tick = 0; tick < 20; ++tick) {
      lg::simulateMovement(player, command, arena, lg::MovementTuning{}, lg::kFixedTickSeconds);
      if (!player.onGround || player.movementMode != lg::MovementMode::Grounded) {
        ++airborneTicks;
      }
    }

    const float radiusOffset =
      player.bounds.radius *
      std::sqrt(
        (ramp.faces[5].normal.x * ramp.faces[5].normal.x) +
        (ramp.faces[5].normal.y * ramp.faces[5].normal.y)
      ) /
      ramp.faces[5].normal.z;
    const float expectedZ =
      slopedTopZ(ramp, player.position.x) + player.bounds.halfHeight + radiusOffset;
    const float startZ =
      slopedTopZ(ramp, -1.7F) + player.bounds.halfHeight + radiusOffset;
    failures += expect(airborneTicks == 0, "fast g_maxspeed downhill ramp movement should not get short air ticks");
    failures += expect(
      player.position.z < startZ - 0.25F && std::fabs(player.position.z - expectedZ) < 0.05F,
      "fast downhill ramp movement should follow the plane downward"
    );
  }

  {
    const std::string rampBrush =
      "{\n"
      "( -160 -80 0 ) ( -160 80 0 ) ( -160 80 8 ) stone 0 0 0 1 1\n"
      "( 160 -80 0 ) ( 160 -80 48 ) ( 160 80 48 ) stone 0 0 0 1 1\n"
      "( -160 -80 0 ) ( 160 -80 0 ) ( 160 -80 48 ) stone 0 0 0 1 1\n"
      "( -160 80 0 ) ( -160 80 8 ) ( 160 80 48 ) stone 0 0 0 1 1\n"
      "( -160 -80 0 ) ( -160 80 0 ) ( 160 80 0 ) stone 0 0 0 1 1\n"
      "( -160 -80 8 ) ( 160 -80 48 ) ( 160 80 48 ) stone 0 0 0 1 1\n"
      "}\n";
    const lg::ArenaLoadResult loaded = lg::loadArenaFromMapText(basicMapWithBrush(rampBrush));
    failures += expect(loaded.ok, "sloped convex brush map should load");
    failures += expect(loaded.arena.brushCount == 1, "sloped convex brush should import as brush geometry");

    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {-3.5F, 0.0F, 0.2F + player.bounds.halfHeight};
    player.onGround = true;
    player.movementMode = lg::MovementMode::Grounded;
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    float highestPositionZ = player.position.z;
    int airborneTicks = 0;
    for (int tick = 0; tick < 40; ++tick) {
      lg::simulateMovement(
        player,
        command,
        loaded.arena,
        tuning,
        lg::kFixedTickSeconds
      );
      highestPositionZ = std::max(highestPositionZ, player.position.z);
      if (!player.onGround) {
        ++airborneTicks;
      }
    }

    failures += expect(player.position.x > -2.5F, "player should move across sloped brush");
    failures += expect(
      highestPositionZ > player.bounds.halfHeight + 0.5F,
      "player should walk up sloped brush instead of being pushed away"
    );
    failures += expect(
      airborneTicks == 0,
      "running up a smooth sloped brush should not repeatedly leave ground"
    );

    command.forwardMove = -1.0F;
    float maxTickDistance = 0.0F;
    float maxTickDrop = 0.0F;
    for (int tick = 0; tick < 20; ++tick) {
      const lg::Vec3 before = player.position;
      lg::simulateMovement(
        player,
        command,
        loaded.arena,
        tuning,
        lg::kFixedTickSeconds
      );
      const lg::Vec3 delta = player.position - before;
      maxTickDistance = std::max(maxTickDistance, lg::length(delta));
      maxTickDrop = std::max(maxTickDrop, before.z - player.position.z);
    }

    failures += expect(
      maxTickDistance < 0.2F &&
        maxTickDrop < 0.08F &&
        player.onGround,
      "reversing direction on a smooth sloped brush should not teleport down the ramp"
    );

    lg::PlayerState descendingPlayer = groundedPlayer();
    descendingPlayer.position = {1.5F, 0.0F, 1.9F};
    descendingPlayer.onGround = true;
    descendingPlayer.movementMode = lg::MovementMode::Grounded;
    command.forwardMove = -1.0F;
    airborneTicks = 0;
    maxTickDrop = 0.0F;
    maxTickDistance = 0.0F;
    for (int tick = 0; tick < 40; ++tick) {
      const lg::Vec3 before = descendingPlayer.position;
      lg::simulateMovement(
        descendingPlayer,
        command,
        loaded.arena,
        tuning,
        lg::kFixedTickSeconds
      );
      if (!descendingPlayer.onGround) {
        ++airborneTicks;
      }
      const lg::Vec3 delta = descendingPlayer.position - before;
      maxTickDistance = std::max(maxTickDistance, lg::length(delta));
      maxTickDrop = std::max(maxTickDrop, before.z - descendingPlayer.position.z);
    }

    failures += expect(
      airborneTicks == 0 &&
        descendingPlayer.onGround &&
        descendingPlayer.position.x < -0.5F &&
        maxTickDistance < 0.2F &&
        maxTickDrop < 0.08F,
      "walking down a smooth sloped brush should follow the ramp instead of falling off it"
    );
  }

  {
    const std::string rampBrush =
      "{\n"
      "( -160 -80 0 ) ( -160 80 0 ) ( -160 80 8 ) textures/common/playerclip 0 0 0 1 1\n"
      "( 160 -80 0 ) ( 160 -80 48 ) ( 160 80 48 ) textures/common/playerclip 0 0 0 1 1\n"
      "( -160 -80 0 ) ( 160 -80 0 ) ( 160 -80 48 ) textures/common/playerclip 0 0 0 1 1\n"
      "( -160 80 0 ) ( -160 80 8 ) ( 160 80 48 ) textures/common/playerclip 0 0 0 1 1\n"
      "( -160 -80 0 ) ( -160 80 0 ) ( 160 80 0 ) textures/common/playerclip 0 0 0 1 1\n"
      "( -160 -80 8 ) ( 160 -80 48 ) ( 160 80 48 ) textures/common/playerclip 0 0 0 1 1\n"
      "}\n";
    const lg::ArenaLoadResult loaded = lg::loadArenaFromMapText(basicMapWithBrush(rampBrush));
    failures += expect(loaded.ok, "sloped playerclip brush map should load");
    failures += expect(loaded.arena.brushCount == 1, "sloped playerclip brush should import as collision geometry");
    failures += expect(!loaded.arena.brushes[0].renderable, "sloped playerclip brush should be non-renderable");

    const lg::MovementTuning tuning;
    lg::PlayerState player = groundedPlayer();
    player.position = {-3.5F, 0.0F, 0.2F + player.bounds.halfHeight};
    player.onGround = true;
    player.movementMode = lg::MovementMode::Grounded;
    lg::UserCommand command;
    command.forwardMove = 1.0F;

    float highestPositionZ = player.position.z;
    int airborneTicks = 0;
    for (int tick = 0; tick < 40; ++tick) {
      lg::simulateMovement(
        player,
        command,
        loaded.arena,
        tuning,
        lg::kFixedTickSeconds
      );
      highestPositionZ = std::max(highestPositionZ, player.position.z);
      if (!player.onGround) {
        ++airborneTicks;
      }
    }

    failures += expect(player.position.x > -2.5F, "player should move across sloped playerclip brush");
    failures += expect(
      highestPositionZ > player.bounds.halfHeight + 0.5F,
      "player should walk smoothly up a sloped playerclip ramp"
    );
    failures += expect(
      airborneTicks == 0,
      "running up a smooth sloped playerclip ramp should not repeatedly leave ground"
    );
  }

  {
    lg::Arena squareArena;
    lg::PlayerState squarePlayer = groundedPlayer();
    squarePlayer.velocity.z = 2.2F;
    squarePlayer.knockbackTicksRemaining = 2;
    lg::UserCommand command;
    lg::simulateMovement(
      squarePlayer,
      command,
      squareArena,
      lg::MovementTuning{},
      lg::kFixedTickSeconds
    );

    const std::string triangularRampBrush =
      "{\n"
      "( -80 -80 0 ) ( -80 -80 32 ) ( -80 80 32 ) stone 0 0 0 1 1\n"
      "( -80 -80 0 ) ( 80 -80 0 ) ( -80 -80 32 ) stone 0 0 0 1 1\n"
      "( 80 -80 0 ) ( 80 80 0 ) ( 80 80 48 ) stone 0 0 0 1 1\n"
      "( -80 80 0 ) ( -80 80 32 ) ( 80 80 48 ) stone 0 0 0 1 1\n"
      "( -80 -80 0 ) ( -80 80 0 ) ( 80 80 0 ) stone 0 0 0 1 1\n"
      "( -80 -80 32 ) ( 80 -80 0 ) ( 80 80 48 ) stone 0 0 0 1 1\n"
      "}\n";
    const lg::ArenaLoadResult loaded =
      lg::loadArenaFromMapText(basicMapWithBrush(triangularRampBrush));
    failures += expect(loaded.ok, "triangular sloped brush map should load");
    failures += expect(
      loaded.arena.brushCount == 1,
      "triangular sloped brush should import as brush geometry"
    );

    lg::PlayerState brushPlayer = groundedPlayer();
    brushPlayer.position = {-0.1F, 0.0F, brushPlayer.bounds.halfHeight + 0.45F};
    brushPlayer.onGround = false;
    brushPlayer.movementMode = lg::MovementMode::Airborne;
    for (int tick = 0; tick < 80 && !brushPlayer.onGround; ++tick) {
      lg::simulateMovement(
        brushPlayer,
        command,
        loaded.arena,
        lg::MovementTuning{},
        lg::kFixedTickSeconds
      );
    }
    failures += expect(brushPlayer.onGround, "test player should settle onto triangular sloped brush");

    const float brushStartZ = brushPlayer.position.z;
    const lg::PlayerState settledBrushPlayer = brushPlayer;
    brushPlayer.velocity.z = 2.2F;
    brushPlayer.knockbackTicksRemaining = 2;
    lg::simulateMovement(
      brushPlayer,
      command,
      loaded.arena,
      lg::MovementTuning{},
      lg::kFixedTickSeconds
    );
    runCommand(brushPlayer, command, loaded.arena, lg::MovementTuning{}, 8);

    failures += expect(
      squarePlayer.position.z > squarePlayer.bounds.halfHeight &&
        squarePlayer.velocity.z > 2.0F &&
        !squarePlayer.onGround,
      "upward knockback should immediately lift off a square floor"
    );
    failures += expect(
      brushPlayer.position.z > brushStartZ + 0.05F &&
        brushPlayer.velocity.z > 0.0F &&
        !brushPlayer.onGround,
      "upward knockback should stay airborne after lifting off a triangular sloped brush"
    );

    lg::PlayerState jumpingBrushPlayer = settledBrushPlayer;
    jumpingBrushPlayer.onGround = true;
    jumpingBrushPlayer.movementMode = lg::MovementMode::Grounded;
    lg::UserCommand jumpCommand;
    jumpCommand.jump = true;
    jumpCommand.upMove = 1.0F;
    lg::simulateMovement(
      jumpingBrushPlayer,
      jumpCommand,
      loaded.arena,
      lg::MovementTuning{},
      lg::kFixedTickSeconds
    );
    jumpCommand.jump = false;
    jumpCommand.upMove = 0.0F;
    runCommand(jumpingBrushPlayer, jumpCommand, loaded.arena, lg::MovementTuning{}, 8);

    failures += expect(
      jumpingBrushPlayer.position.z > brushStartZ + 0.05F &&
        !jumpingBrushPlayer.onGround,
      "jumping from a triangular sloped brush should stay airborne after takeoff"
    );
  }

  return failures == 0 ? 0 : 1;
}
