#pragma once

#include "net/NetProtocol.hpp"

namespace lg {

class NetTransport {
public:
  virtual ~NetTransport() = default;

  virtual void sendCommand(const CommandPacket& packet) = 0;
  [[nodiscard]] virtual bool receiveCommand(CommandPacket& packet) = 0;

  virtual void sendSnapshot(const ServerSnapshot& snapshot) = 0;
  [[nodiscard]] virtual bool receiveSnapshot(ServerSnapshot& snapshot) = 0;
};

} // namespace lg
