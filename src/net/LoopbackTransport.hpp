#pragma once

#include "net/NetTransport.hpp"

#include <deque>

namespace lg {

class LoopbackTransport final : public NetTransport {
public:
  void sendCommand(const CommandPacket& packet) override;
  [[nodiscard]] bool receiveCommand(CommandPacket& packet) override;

  void sendSnapshot(const ServerSnapshot& snapshot) override;
  [[nodiscard]] bool receiveSnapshot(ServerSnapshot& snapshot) override;

private:
  std::deque<CommandPacket> commands_;
  std::deque<ServerSnapshot> snapshots_;
};

} // namespace lg
