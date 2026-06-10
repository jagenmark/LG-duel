#pragma once

#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/Movement.hpp"

#include <array>
#include <cstdint>
#include <deque>

namespace lg {

class ServerGame {
public:
  explicit ServerGame(NetTransport& transport);

  void tick(float fixedDt);
  void resetMatch();

  [[nodiscard]] const ServerSnapshot& snapshot() const;

private:
  struct HistoryFrame {
    std::uint32_t serverTick = 0;
    std::array<PlayerState, kDuelPlayerCount> players = {};
  };

  void receiveCommands();
  void updateRespawns();
  void respawnPlayer(std::size_t playerIndex);
  void recordHistory();
  [[nodiscard]] const HistoryFrame& historyFrameForTick(std::uint32_t serverTick) const;
  void publishSnapshot();

  NetTransport& transport_;
  Arena arena_ = {};
  MovementTuning movementTuning_ = {};
  LightningGunTuning lightningGunTuning_ = {};
  std::array<LightningGunState, kDuelPlayerCount> lightningGunStates_ = {};
  std::array<UserCommand, kDuelPlayerCount> commands_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> viewedServerTicks_ = {};
  std::array<bool, kDuelPlayerCount> hasCommand_ = {};
  std::deque<HistoryFrame> history_ = {};
  ServerSnapshot snapshot_ = {};
};

} // namespace lg
