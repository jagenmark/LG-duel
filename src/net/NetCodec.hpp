#pragma once

#include "net/NetProtocol.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg {

inline constexpr std::uint32_t kProtocolMagic = 0x4C474455U;
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kMaxPacketBytes = 512;

enum class PacketType : std::uint8_t {
  Command = 1,
  Snapshot = 2,
};

using WirePacket = std::vector<std::uint8_t>;

[[nodiscard]] bool encodeCommandPacket(const CommandPacket& packet, WirePacket& wire);
[[nodiscard]] bool decodeCommandPacket(const WirePacket& wire, CommandPacket& packet);

[[nodiscard]] bool encodeServerSnapshot(const ServerSnapshot& snapshot, WirePacket& wire);
[[nodiscard]] bool decodeServerSnapshot(const WirePacket& wire, ServerSnapshot& snapshot);

} // namespace lg
