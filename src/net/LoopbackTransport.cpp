#include "net/LoopbackTransport.hpp"

namespace lg {

void LoopbackTransport::sendCommand(const CommandPacket& packet) {
  commands_.push_back(packet);
}

bool LoopbackTransport::receiveCommand(CommandPacket& packet) {
  if (commands_.empty()) {
    return false;
  }

  packet = commands_.front();
  commands_.pop_front();
  return true;
}

void LoopbackTransport::sendSnapshot(const ServerSnapshot& snapshot) {
  snapshots_.push_back(snapshot);
}

bool LoopbackTransport::receiveSnapshot(ServerSnapshot& snapshot) {
  if (snapshots_.empty()) {
    return false;
  }

  snapshot = snapshots_.front();
  snapshots_.pop_front();
  return true;
}

} // namespace lg
