#pragma once

#include "net/NetCodec.hpp"
#include "net/NetTransport.hpp"

#include <deque>

namespace lg {

class LoopbackTransport final : public NetTransport {
public:
  void sendCommand(const CommandPacket& packet) override;
  [[nodiscard]] bool receiveCommand(CommandPacket& packet) override;

  void sendSnapshot(const ServerSnapshot& snapshot) override;
  [[nodiscard]] bool receiveSnapshot(ServerSnapshot& snapshot) override;
  void publishChatHistory(const ChatHistory& history) override;
  [[nodiscard]] bool receiveChatHistory(ChatHistoryChunk& chunk) override;

private:
  std::deque<WirePacket> commands_;
  std::deque<WirePacket> snapshots_;
  std::deque<WirePacket> chatHistory_;
  std::uint32_t publishedChatSequence_ = 0;
};

} // namespace lg
