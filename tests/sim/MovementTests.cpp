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
    lg::PlayerState player;
    player.position = {1.0F, 2.0F, 3.0F};
    player.velocity = {1.0F, 2.0F, 3.0F};
    player.movementMode = lg::MovementMode::Flying;

    lg::UserCommand command;
    runCommand(player, command, 1);

    failures += expect(player.movementMode == lg::MovementMode::Flying, "flying mode should be representable in dispatch");
    failures += expect(player.position.z > 3.0F, "flying placeholder should preserve full 3D velocity");
    failures += expect(nearlyEqual(player.velocity.z, 3.0F), "flying placeholder should not apply grounded gravity");
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
