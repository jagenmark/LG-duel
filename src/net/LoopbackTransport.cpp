#include "net/LoopbackTransport.hpp"

#include <algorithm>
#include <utility>

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
  if (encodeBoundedGameplaySnapshot(snapshot, wire)) {
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

void LoopbackTransport::sendProjectileUpdates(
  const ProjectileUpdatePacket& packet
) {
  WirePacket wire;
  if (encodeProjectileUpdatePacket(packet, wire)) {
    projectileUpdates_.push_back(std::move(wire));
  }
}

bool LoopbackTransport::receiveProjectileUpdates(
  ProjectileUpdatePacket& packet
) {
  if (projectileUpdates_.empty()) {
    return false;
  }
  const WirePacket wire = std::move(projectileUpdates_.front());
  projectileUpdates_.pop_front();
  return decodeProjectileUpdatePacket(wire, packet);
}

void LoopbackTransport::publishChatHistory(const ChatHistory& history) {
  if (history.messageCount == 0U) {
    return;
  }
  const std::uint32_t latest = history.messages[history.messageCount - 1U].sequence;
  if (latest == publishedChatSequence_) {
    return;
  }
  for (std::size_t first = 0; first < history.messageCount;
       first += kChatHistoryChunkCapacity) {
    ChatHistoryChunk chunk;
    chunk.oldestAvailableSequence = history.messages[0].sequence;
    chunk.latestSequence = latest;
    chunk.messageCount = static_cast<std::uint8_t>(std::min(
      kChatHistoryChunkCapacity,
      static_cast<std::size_t>(history.messageCount) - first
    ));
    for (std::size_t index = 0; index < chunk.messageCount; ++index) {
      chunk.messages[index] = history.messages[first + index];
    }
    WirePacket wire;
    if (encodeChatHistoryChunk(chunk, wire)) {
      chatHistory_.push_back(std::move(wire));
    }
  }
  publishedChatSequence_ = latest;
}

bool LoopbackTransport::receiveChatHistory(ChatHistoryChunk& chunk) {
  if (chatHistory_.empty()) {
    return false;
  }
  const WirePacket wire = std::move(chatHistory_.front());
  chatHistory_.pop_front();
  return decodeChatHistoryChunk(wire, chunk);
}

} // namespace lg
