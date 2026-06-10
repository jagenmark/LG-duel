#pragma once

#include "sim/Combat.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lg {

inline constexpr std::size_t kDuelPlayerCount = 2;
inline constexpr std::size_t kMaxBundledCommands = 3;

struct ConnectRequest {
  std::uint32_t clientNonce = 0;
};

struct ConnectAccept {
  std::uint32_t clientNonce = 0;
  std::uint8_t playerIndex = 0;
  std::uint32_t serverTick = 0;
};

struct CommandPacket {
  std::uint8_t playerIndex = 0;
  UserCommand command = {};
  bool requestReset = false;
};

struct CommandBundle {
  std::uint8_t commandCount = 0;
  std::array<CommandPacket, kMaxBundledCommands> commands = {};
};

struct PingPacket {
  std::uint32_t token = 0;
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
