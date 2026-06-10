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
  void setConnectedPlayers(
    const std::array<bool, kDuelPlayerCount>& connectedPlayers
  );
  void setMatchRules(const MatchRules& rules);

  [[nodiscard]] const ServerSnapshot& snapshot() const;
  [[nodiscard]] const MatchRules& matchRules() const;

private:
  struct HistoryFrame {
    std::uint32_t serverTick = 0;
    std::array<PlayerState, kDuelPlayerCount> players = {};
  };

  void receiveCommands();
  void respawnPlayer(std::size_t playerIndex);
  void respawnRound();
  void updateMatchState();
  void beginCountdown();
  void beginRoundEnd(std::size_t winnerIndex);
  void beginMatchEnd(std::size_t winnerIndex);
  [[nodiscard]] bool enoughPlayersConnected() const;
  [[nodiscard]] bool allConnectedPlayersReady() const;
  void recordHistory();
  [[nodiscard]] const HistoryFrame& historyFrameForTick(std::uint32_t serverTick) const;
  void publishSnapshot();

  NetTransport& transport_;
  Arena arena_ = thunderstruckArena();
  MovementTuning movementTuning_ = {};
  LightningGunTuning lightningGunTuning_ = {};
  std::array<LightningGunState, kDuelPlayerCount> lightningGunStates_ = {};
  std::array<UserCommand, kDuelPlayerCount> commands_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> viewedServerTicks_ = {};
  std::array<bool, kDuelPlayerCount> hasCommand_ = {};
  std::deque<HistoryFrame> history_ = {};
  MatchRules matchRules_ = {};
  ServerSnapshot snapshot_ = {};
};

} // namespace lg
