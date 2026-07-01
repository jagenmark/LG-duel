#pragma once

#include "net/NetProtocol.hpp"

#include <cstddef>
#include <cstdint>

namespace lg {

struct SnapshotDiagnostics {
  std::uint32_t snapshotPacketsDecoded = 0;
  float snapshotDecodeMilliseconds = 0.0F;
  std::uint32_t snapshotsApplied = 0;
  float snapshotApplyMilliseconds = 0.0F;
  std::size_t snapshotQueueDepth = 0;
};

class NetTransport {
public:
  virtual ~NetTransport() = default;

  virtual void sendCommand(const CommandPacket& packet) = 0;
  [[nodiscard]] virtual bool receiveCommand(CommandPacket& packet) = 0;

  virtual void sendSnapshot(const ServerSnapshot& snapshot) = 0;
  [[nodiscard]] virtual bool receiveSnapshot(ServerSnapshot& snapshot) = 0;
  [[nodiscard]] virtual SnapshotDiagnostics snapshotDiagnostics() const {
    return {};
  }
};

} // namespace lg
