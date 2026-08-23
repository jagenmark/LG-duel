#pragma once

#include "replay/ReplayTransfer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace lg::replay {

struct KillcamClientReceiverStatus {
  bool active = false;
  bool failed = false;
  std::uint32_t transferId = 0;
  std::uint32_t generation = 0;
  std::uint32_t sessionId = 0;
  std::uint32_t lethalSequence = 0;
  std::size_t receivedBytes = 0;
  std::size_t expectedBytes = 0;
};

// Client-side transfer policy. It does not decode replay bytes or touch disk;
// callers submit takeCompleted() to ReplayIoService after the final ACK.
class KillcamClientReceiver {
public:
  explicit KillcamClientReceiver(ReplayTransferReceiverConfig config = {});

  [[nodiscard]] std::optional<ReplayTransferMessage> receive(
      const ReplayTransferMessage& message,
      std::uint64_t nowMilliseconds);
  [[nodiscard]] std::optional<ReplayTransferMessage> update(
      std::uint64_t nowMilliseconds);
  [[nodiscard]] std::optional<ReplayTransferMessage> cancel(
      ReplayTransferCancelReason reason);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> takeCompleted();
  [[nodiscard]] KillcamClientReceiverStatus status() const;
  [[nodiscard]] bool active() const { return receiver_.active(); }
  [[nodiscard]] bool failed() const { return failed_ || receiver_.failed(); }
  void bindSession(std::uint32_t sessionId);
  [[nodiscard]] std::uint32_t boundSession() const { return boundSessionId_; }
  void reset();

private:
  [[nodiscard]] std::optional<ReplayTransferMessage> cancelMessage(
      ReplayTransferCancelReason reason) const;

  ReplayTransferReceiverConfig config_ = {};
  ReplayTransferReceiver receiver_;
  std::optional<std::vector<std::uint8_t>> completed_ = {};
  std::optional<KillcamClientReceiverStatus> completedStatus_ = {};
  std::uint32_t boundSessionId_ = 0;
  bool failed_ = false;
};

} // namespace lg::replay
