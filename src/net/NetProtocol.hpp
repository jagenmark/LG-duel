#pragma once

#include "sim/Combat.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lg {

inline constexpr std::size_t kDuelPlayerCount = 2;

struct CommandPacket {
  std::uint8_t playerIndex = 0;
  UserCommand command = {};
  bool requestReset = false;
};

struct ServerSnapshot {
  std::uint32_t serverTick = 0;
  std::array<std::uint32_t, kDuelPlayerCount> acknowledgedCommand = {};
  std::array<bool, kDuelPlayerCount> hasAcknowledgedCommand = {};
  std::array<PlayerState, kDuelPlayerCount> players = {};
  std::array<LightningGunResult, kDuelPlayerCount> lightningGuns = {};
  std::array<std::uint32_t, kDuelPlayerCount> respawnTicksRemaining = {};
  bool playersColliding = false;
};

} // namespace lg
