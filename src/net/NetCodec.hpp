#pragma once

#include "net/NetProtocol.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg {

inline constexpr std::uint32_t kProtocolMagic = 0x4C474455U;
inline constexpr std::uint16_t kProtocolVersion = 58;
inline constexpr std::size_t kMaxPacketBytes = 65535;
inline constexpr std::size_t kMaxUdpApplicationDatagramBytes = 1200;

enum class PacketType : std::uint8_t {
  ConnectRequest = 1,
  ConnectAccept = 2,
  Command = 3,
  CommandBundle = 4,
  Snapshot = 5,
  Ping = 6,
  Pong = 7,
  Disconnect = 8,
  ChatHistory = 9,
  ChatHistoryAck = 10,
  CombatStats = 11,
  ProjectileUpdates = 12,
};

using WirePacket = std::vector<std::uint8_t>;

[[nodiscard]] bool inspectPacketType(const WirePacket& wire, PacketType& type);

[[nodiscard]] bool encodeConnectRequest(const ConnectRequest& packet, WirePacket& wire);
[[nodiscard]] bool decodeConnectRequest(const WirePacket& wire, ConnectRequest& packet);

[[nodiscard]] bool encodeConnectAccept(const ConnectAccept& packet, WirePacket& wire);
[[nodiscard]] bool decodeConnectAccept(const WirePacket& wire, ConnectAccept& packet);

[[nodiscard]] bool encodeCommandPacket(const CommandPacket& packet, WirePacket& wire);
[[nodiscard]] bool decodeCommandPacket(const WirePacket& wire, CommandPacket& packet);

[[nodiscard]] bool encodeCommandBundle(const CommandBundle& bundle, WirePacket& wire);
[[nodiscard]] bool decodeCommandBundle(const WirePacket& wire, CommandBundle& bundle);

[[nodiscard]] bool encodeServerSnapshot(const ServerSnapshot& snapshot, WirePacket& wire);
// Gameplay transports use this path so the same low-priority fields are
// dropped in the same order when a valid snapshot exceeds one UDP datagram.
// The input stays unchanged. encodeServerSnapshot remains the strict, lossless
// codec entry point.
[[nodiscard]] bool encodeBoundedGameplaySnapshot(
  const ServerSnapshot& snapshot,
  WirePacket& wire
);
[[nodiscard]] bool decodeServerSnapshot(const WirePacket& wire, ServerSnapshot& snapshot);

[[nodiscard]] bool encodePingPacket(PacketType type, const PingPacket& packet, WirePacket& wire);
[[nodiscard]] bool decodePingPacket(
  const WirePacket& wire,
  PacketType expectedType,
  PingPacket& packet
);

[[nodiscard]] bool encodeDisconnectPacket(
  const DisconnectPacket& packet,
  WirePacket& wire
);
[[nodiscard]] bool decodeDisconnectPacket(
  const WirePacket& wire,
  DisconnectPacket& packet
);

[[nodiscard]] bool encodeChatHistoryChunk(
  const ChatHistoryChunk& packet,
  WirePacket& wire
);
[[nodiscard]] bool decodeChatHistoryChunk(
  const WirePacket& wire,
  ChatHistoryChunk& packet
);
[[nodiscard]] bool encodeChatHistoryAck(
  const ChatHistoryAck& packet,
  WirePacket& wire
);
[[nodiscard]] bool decodeChatHistoryAck(
  const WirePacket& wire,
  ChatHistoryAck& packet
);
[[nodiscard]] bool encodeCombatStatsPacket(
  const CombatStatsPacket& packet,
  WirePacket& wire
);
[[nodiscard]] bool decodeCombatStatsPacket(
  const WirePacket& wire,
  CombatStatsPacket& packet
);
[[nodiscard]] bool encodeProjectileUpdatePacket(
  const ProjectileUpdatePacket& packet,
  WirePacket& wire
);
[[nodiscard]] bool decodeProjectileUpdatePacket(
  const WirePacket& wire,
  ProjectileUpdatePacket& packet
);

} // namespace lg
