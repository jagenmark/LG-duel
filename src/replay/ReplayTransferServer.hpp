#pragma once

#include "replay/ReplayTransfer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lg::replay {

struct ReplayTransferOutbound {
  std::uint8_t clientIndex = 0;
  ReplayTransferMessage message;
};

struct ReplayTransferServerConfig {
  std::size_t maximumSegmentBytes = kReplayTransferMaxSegmentBytes;
  ReplayTransferConfig transfer = {};
};

struct ReplayTransferServerStatus {
  bool active = false;
  std::uint8_t clientIndex = 0;
  std::uint32_t transferId = 0;
  std::uint32_t generation = 0;
  std::uint32_t sessionId = 0;
  std::uint32_t lethalSequence = 0;
  std::size_t bytes = 0;
  ReplayTransferStats stats = {};
};

// Server-only transfer state. It owns no sockets and performs no disk I/O;
// callers provide authenticated client/session data and send the returned
// messages through their transport after the authoritative tick.
class ReplayTransferServer {
public:
  explicit ReplayTransferServer(ReplayTransferServerConfig config = {});

  void configure(ReplayTransferServerConfig config);

  [[nodiscard]] bool start(
      std::uint8_t clientIndex,
      std::uint32_t sessionId,
      std::uint32_t generation,
      std::vector<std::uint8_t> bytes,
      std::uint64_t nowMilliseconds,
      std::string* error = nullptr,
      std::uint32_t lethalSequence = 0U);
  void receive(
      std::uint8_t clientIndex,
      std::uint32_t sessionId,
      const ReplayTransferMessage& message);
  void cancel(
      std::uint8_t clientIndex,
      std::uint32_t sessionId,
      ReplayTransferCancelReason reason);
  [[nodiscard]] std::vector<ReplayTransferOutbound> poll(
      std::uint64_t nowMilliseconds,
      std::size_t packetBudget);
  void clearClient(std::uint8_t clientIndex, std::uint32_t sessionId = 0U);
  void clear();

  [[nodiscard]] bool active(std::uint8_t clientIndex) const;
  [[nodiscard]] std::optional<ReplayTransferServerStatus> status(
      std::uint8_t clientIndex) const;
  [[nodiscard]] std::size_t activeCount() const;
  [[nodiscard]] std::uint32_t nextTransferId() const { return nextTransferId_; }

private:
  struct Slot {
    bool active = false;
    std::uint32_t sessionId = 0;
    std::uint32_t generation = 0;
    std::size_t bytes = 0;
    ReplayTransferSender sender;
  };

  ReplayTransferServerConfig config_ = {};
  std::array<Slot, kMaxNetworkClients> slots_ = {};
  std::uint32_t nextTransferId_ = 1U;
};

} // namespace lg::replay
