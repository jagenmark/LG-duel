#pragma once

#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/Movement.hpp"

#include <array>
#include <cstdint>

namespace lg {

class ServerGame {
public:
  explicit ServerGame(NetTransport& transport);

  void tick(float fixedDt);
  void resetMatch();

  [[nodiscard]] const ServerSnapshot& snapshot() const;

private:
  void receiveCommands();
  void updateRespawns();
  void respawnPlayer(std::size_t playerIndex);
  void publishSnapshot();

  NetTransport& transport_;
  Arena arena_ = {};
  MovementTuning movementTuning_ = {};
  LightningGunTuning lightningGunTuning_ = {};
  std::array<LightningGunState, kDuelPlayerCount> lightningGunStates_ = {};
  std::array<UserCommand, kDuelPlayerCount> commands_ = {};
  std::array<bool, kDuelPlayerCount> hasCommand_ = {};
  ServerSnapshot snapshot_ = {};
};

} // namespace lg
