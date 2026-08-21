#pragma once

#include "replay/ReplayTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <variant>
#include <vector>

namespace lg::replay {

inline constexpr std::size_t kReplayTransferMaxDatagramBytes = 1200U;
inline constexpr std::size_t kReplayTransferMaxSegmentBytes = 512U * 1024U;
inline constexpr std::uint16_t kReplayTransferMaxChunks = 512U;
// NetCodec owns a 12-byte normal packet header and the replay chunk payload
// has 23 bytes of fixed fields, including subtype and CRC.
inline constexpr std::size_t kReplayTransferMaxChunkPayloadBytes =
    kReplayTransferMaxDatagramBytes - 12U - 23U;
inline constexpr std::size_t kReplayTransferSha256Bytes = 32U;
// Acks use this reserved index for the begin packet. Chunk indexes stay below
// it.
inline constexpr std::uint16_t kReplayTransferBeginAck =
    kReplayTransferMaxChunks;

enum class ReplayTransferPacketType : std::uint8_t {
  Begin = 1,
  Chunk = 2,
  Ack = 3,
  Cancel = 4
};
enum class ReplayTransferCancelReason : std::uint8_t {
  None = 0,
  Timeout = 1,
  TooLarge = 2,
  Invalid = 3,
  Skipped = 4
};

struct ReplayTransferBegin {
  std::uint32_t transferId = 0;
  std::uint32_t generation = 0;
  std::uint16_t chunkCount = 0;
  std::uint32_t byteCount = 0;
  std::uint32_t sessionId = 1;
  std::array<std::uint8_t, kReplayTransferSha256Bytes> sha256 = {};
  std::uint32_t lethalSequence = 0;
};
struct ReplayTransferChunk {
  std::uint32_t transferId = 0;
  std::uint32_t generation = 0;
  std::uint16_t index = 0;
  std::uint16_t count = 0;
  std::vector<std::uint8_t> payload;
  std::uint32_t crc32 = 0;
  std::uint32_t sessionId = 1;
};
struct ReplayTransferAck {
  std::uint32_t transferId = 0;
  std::uint32_t generation = 0;
  std::uint16_t index = 0;
  std::uint32_t sessionId = 1;
};
struct ReplayTransferCancel {
  std::uint32_t transferId = 0;
  std::uint32_t generation = 0;
  ReplayTransferCancelReason reason = ReplayTransferCancelReason::None;
  std::uint32_t sessionId = 1;
};

using ReplayTransferMessage = std::variant<
    ReplayTransferBegin,
    ReplayTransferChunk,
    ReplayTransferAck,
    ReplayTransferCancel>;

[[nodiscard]] std::uint32_t replayTransferCrc32(
    const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::array<std::uint8_t, kReplayTransferSha256Bytes>
replayTransferSha256(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] bool encodeReplayTransferBegin(const ReplayTransferBegin &,
                                             std::vector<std::uint8_t> &);
[[nodiscard]] bool decodeReplayTransferBegin(const std::vector<std::uint8_t> &,
                                             ReplayTransferBegin &);
[[nodiscard]] bool encodeReplayTransferChunk(const ReplayTransferChunk &,
                                             std::vector<std::uint8_t> &);
[[nodiscard]] bool decodeReplayTransferChunk(const std::vector<std::uint8_t> &,
                                             ReplayTransferChunk &);
[[nodiscard]] bool encodeReplayTransferAck(const ReplayTransferAck &,
                                           std::vector<std::uint8_t> &);
[[nodiscard]] bool decodeReplayTransferAck(const std::vector<std::uint8_t> &,
                                           ReplayTransferAck &);
[[nodiscard]] bool encodeReplayTransferCancel(const ReplayTransferCancel &,
                                              std::vector<std::uint8_t> &);
[[nodiscard]] bool decodeReplayTransferCancel(const std::vector<std::uint8_t> &,
                                              ReplayTransferCancel &);

struct ReplayTransferConfig {
  std::uint32_t sessionId = 1U;
  std::uint32_t retryMilliseconds = 100U;
  std::uint32_t timeoutMilliseconds = 5000U;
  std::uint32_t minimumPacketIntervalMilliseconds = 2U;
};
struct ReplayTransferStats {
  std::uint16_t chunks = 0;
  std::uint16_t acknowledged = 0;
  std::uint32_t retries = 0;
  bool cancelled = false;
};

struct ReplayTransferReceiverConfig {
  std::uint32_t idleTimeoutMilliseconds = 500U;
  std::uint32_t overallTimeoutMilliseconds = 5000U;
  // Keep only transfer identity after successful assembly so a retransmitted
  // final chunk can recover a lost terminal ACK without retaining the demo.
  std::uint32_t completionAckLingerMilliseconds = 1000U;
};

class ReplayTransferSender {
public:
  [[nodiscard]] bool begin(std::uint32_t id, std::uint32_t generation,
                           std::vector<std::uint8_t> bytes,
                           std::uint64_t nowMilliseconds,
                           ReplayTransferConfig config = {},
                           std::uint32_t lethalSequence = 0U);
  [[nodiscard]] std::optional<ReplayTransferMessage>
  nextMessage(std::uint64_t nowMilliseconds);
  // Compatibility helper for the pre-NetCodec unit tests. Live transport must
  // use nextMessage and encodeReplayTransferPacket instead.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>>
  nextPacket(std::uint64_t nowMilliseconds);
  void acknowledge(const ReplayTransferAck &ack);
  void cancel(ReplayTransferCancelReason reason);
  [[nodiscard]] ReplayTransferStats stats() const;
  [[nodiscard]] bool complete() const;
  [[nodiscard]] const ReplayTransferBegin& beginMessage() const { return begin_; }

private:
  ReplayTransferBegin begin_ = {};
  std::vector<std::vector<std::uint8_t>> chunks_;
  std::vector<std::uint64_t> lastSent_;
  std::vector<bool> acknowledged_;
  ReplayTransferConfig config_ = {};
  std::uint64_t started_ = 0;
  std::uint64_t lastPacket_ = 0;
  std::uint64_t beginLastSent_ = 0;
  bool beginAcknowledged_ = false;
  bool cancelled_ = false;
  bool cancelSent_ = false;
  ReplayTransferCancelReason cancelReason_ = ReplayTransferCancelReason::None;
  std::uint32_t retries_ = 0;
};

class ReplayTransferReceiver {
public:
  explicit ReplayTransferReceiver(ReplayTransferReceiverConfig config = {});
  // A duplicate begin is idempotent and returns the begin ack without losing
  // chunks.
  [[nodiscard]] std::optional<ReplayTransferAck>
  receiveBegin(const ReplayTransferBegin &begin,
               std::uint64_t nowMilliseconds = 0U);
  [[nodiscard]] std::optional<ReplayTransferAck>
  receiveChunk(const ReplayTransferChunk &chunk,
               std::uint64_t nowMilliseconds = 0U);
  void cancel(const ReplayTransferCancel &cancel);
  [[nodiscard]] bool expire(std::uint64_t nowMilliseconds);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> takeCompleted();
  [[nodiscard]] bool failed() const;
  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] std::size_t receivedBytes() const { return bytes_; }
  [[nodiscard]] const ReplayTransferBegin& beginMessage() const { return begin_; }

private:
  ReplayTransferBegin begin_ = {};
  ReplayTransferBegin completedBegin_ = {};
  std::vector<std::vector<std::uint8_t>> chunks_;
  std::vector<bool> received_;
  std::size_t bytes_ = 0;
  ReplayTransferReceiverConfig config_ = {};
  std::uint64_t started_ = 0;
  std::uint64_t lastActivity_ = 0;
  std::uint64_t completedAt_ = 0;
  bool active_ = false;
  bool failed_ = false;
};

[[nodiscard]] bool permitsRemoteKillcam(const ReplayMetadata &metadata,
                                        bool localOrDeveloper);

} // namespace lg::replay
