#include "net/LoopbackTransport.hpp"

namespace lg {
void LoopbackTransport::sendCommand(const CommandPacket& packet) {
  WirePacket wire;
  if (encodeCommandPacket(packet, wire)) {
    commands_.push_back(wire);
  }
}

bool LoopbackTransport::receiveCommand(CommandPacket& packet) {
  if (commands_.empty()) {
    return false;
  }

  const WirePacket wire = commands_.front();
  commands_.pop_front();
  return decodeCommandPacket(wire, packet);
}

void LoopbackTransport::sendSnapshot(const ServerSnapshot& snapshot) {
  WirePacket wire;
  if (encodeServerSnapshot(snapshot, wire)) {
    snapshots_.push_back(wire);
  }
}

bool LoopbackTransport::receiveSnapshot(ServerSnapshot& snapshot) {
  if (snapshots_.empty()) {
    return false;
  }

  const WirePacket wire = snapshots_.front();
  snapshots_.pop_front();
  return decodeServerSnapshot(wire, snapshot);
}

} // namespace lg
