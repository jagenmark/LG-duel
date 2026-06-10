#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <bit>
#include <cstdint>
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

bool equalFloat(float lhs, float rhs) {
  return std::bit_cast<std::uint32_t>(lhs) == std::bit_cast<std::uint32_t>(rhs);
}

bool equalVec3(lg::Vec3 lhs, lg::Vec3 rhs) {
  return equalFloat(lhs.x, rhs.x) &&
    equalFloat(lhs.y, rhs.y) &&
    equalFloat(lhs.z, rhs.z);
}

bool equalPlayer(const lg::PlayerState& lhs, const lg::PlayerState& rhs) {
  return equalVec3(lhs.position, rhs.position) &&
    equalVec3(lhs.velocity, rhs.velocity) &&
    equalFloat(lhs.viewYawRadians, rhs.viewYawRadians) &&
    equalFloat(lhs.viewPitchRadians, rhs.viewPitchRadians) &&
    lhs.health == rhs.health &&
    lhs.movementMode == rhs.movementMode &&
    lhs.onGround == rhs.onGround;
}

} // namespace

int main() {
  int failures = 0;
  lg::LoopbackTransport firstTransport;
  lg::LoopbackTransport secondTransport;
  lg::ServerGame firstServer(firstTransport);
  lg::ServerGame secondServer(secondTransport);

  for (std::uint32_t tick = 0; tick < 10000; ++tick) {
    for (std::uint8_t playerIndex = 0; playerIndex < 2; ++playerIndex) {
      lg::UserCommand command;
      command.sequence = tick;
      command.clientTick = tick;
      command.viewYawRadians = playerIndex == 0 ? 0.0F : 3.14159265359F;
      command.forwardMove = (tick / 125U) % 2U == 0U ? 1.0F : -1.0F;
      command.rightMove = (tick / 75U) % 2U == 0U ? 0.5F : -0.5F;
      command.jump = tick % 251U == 0U;
      command.attack = tick % 5U != 0U;

      const lg::CommandPacket packet{playerIndex, command, false};
      firstTransport.sendCommand(packet);
      secondTransport.sendCommand(packet);
    }

    firstServer.tick(lg::kFixedTickSeconds);
    secondServer.tick(lg::kFixedTickSeconds);
  }

  const lg::ServerSnapshot& first = firstServer.snapshot();
  const lg::ServerSnapshot& second = secondServer.snapshot();
  failures += expect(first.serverTick == 10000, "long simulation should advance expected tick count");
  failures += expect(first.serverTick == second.serverTick, "replayed servers should match tick");
  failures += expect(equalPlayer(first.players[0], second.players[0]), "player zero should be bit deterministic");
  failures += expect(equalPlayer(first.players[1], second.players[1]), "player one should be bit deterministic");
  failures += expect(
    first.respawnTicksRemaining == second.respawnTicksRemaining,
    "respawn state should be deterministic"
  );
  failures += expect(
    first.acknowledgedCommand == second.acknowledgedCommand,
    "command acknowledgements should be deterministic"
  );

  return failures == 0 ? 0 : 1;
}
