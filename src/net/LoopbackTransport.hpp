#pragma once

#include "net/NetCodec.hpp"
#include "net/NetTransport.hpp"

#include <deque>

namespace lg {

inline constexpr std::size_t kMaxQueuedReplayTransferMessages = 256U;

class LoopbackTransport final : public NetTransport {
public:
  void sendCommand(const CommandPacket& packet) override;
  [[nodiscard]] bool receiveCommand(CommandPacket& packet) override;

  void sendSnapshot(const ServerSnapshot& snapshot) override;
  [[nodiscard]] bool receiveSnapshot(ServerSnapshot& snapshot) override;
  void sendProjectileUpdates(const ProjectileUpdatePacket& packet) override;
  [[nodiscard]] bool receiveProjectileUpdates(ProjectileUpdatePacket& packet) override;
  void publishChatHistory(const ChatHistory& history) override;
  [[nodiscard]] bool receiveChatHistory(ChatHistoryChunk& chunk) override;
  bool sendReplayTransferMessage(
      const replay::ReplayTransferMessage& message) override;
  [[nodiscard]] bool receiveReplayTransferMessage(
      replay::ReplayTransferMessage& message) override;

private:
  std::deque<WirePacket> commands_;
  std::deque<WirePacket> snapshots_;
  std::deque<WirePacket> projectileUpdates_;
  std::deque<WirePacket> chatHistory_;
  std::deque<WirePacket> replayTransfer_;
  std::uint32_t publishedChatSequence_ = 0;
};

} // namespace lg
