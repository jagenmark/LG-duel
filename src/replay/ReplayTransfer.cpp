#include "replay/ReplayTransfer.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace lg::replay {
namespace {

constexpr std::size_t kChunkHeaderBytes = 15U;

void u16(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}
void u32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32U; shift += 8U)
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
bool readU16(const std::vector<std::uint8_t> &bytes, std::size_t &at,
             std::uint16_t &value) {
  if (at + 2U > bytes.size())
    return false;
  value = static_cast<std::uint16_t>(bytes[at]) |
          (static_cast<std::uint16_t>(bytes[at + 1U]) << 8U);
  at += 2U;
  return true;
}
bool readU32(const std::vector<std::uint8_t> &bytes, std::size_t &at,
             std::uint32_t &value) {
  if (at + 4U > bytes.size())
    return false;
  value = 0U;
  for (unsigned shift = 0; shift < 32U; shift += 8U)
    value |= static_cast<std::uint32_t>(bytes[at++]) << shift;
  return true;
}
bool validCancel(ReplayTransferCancelReason value) {
  return value >= ReplayTransferCancelReason::None &&
         value <= ReplayTransferCancelReason::Skipped;
}
bool header(const std::vector<std::uint8_t> &bytes,
            ReplayTransferPacketType expected, std::size_t size) {
  return bytes.size() == size && size <= kReplayTransferMaxDatagramBytes &&
         !bytes.empty() && bytes[0] == static_cast<std::uint8_t>(expected);
}

} // namespace

bool encodeReplayTransferBegin(const ReplayTransferBegin &value,
                               std::vector<std::uint8_t> &bytes) {
  bytes.clear();
  if (value.transferId == 0U || value.generation == 0U ||
      value.chunkCount == 0U || value.chunkCount > kReplayTransferMaxChunks ||
      value.byteCount == 0U || value.byteCount > kReplayTransferMaxSegmentBytes)
    return false;
  bytes.push_back(static_cast<std::uint8_t>(ReplayTransferPacketType::Begin));
  u32(bytes, value.transferId);
  u32(bytes, value.generation);
  u16(bytes, value.chunkCount);
  u32(bytes, value.byteCount);
  return true;
}
bool decodeReplayTransferBegin(const std::vector<std::uint8_t> &bytes,
                               ReplayTransferBegin &value) {
  std::size_t at = 1U;
  if (!header(bytes, ReplayTransferPacketType::Begin, 15U) ||
      !readU32(bytes, at, value.transferId) ||
      !readU32(bytes, at, value.generation) ||
      !readU16(bytes, at, value.chunkCount) ||
      !readU32(bytes, at, value.byteCount))
    return false;
  return value.transferId != 0U && value.generation != 0U &&
         value.chunkCount > 0U &&
         value.chunkCount <= kReplayTransferMaxChunks && value.byteCount > 0U &&
         value.byteCount <= kReplayTransferMaxSegmentBytes;
}
bool encodeReplayTransferChunk(const ReplayTransferChunk &value,
                               std::vector<std::uint8_t> &bytes) {
  bytes.clear();
  if (value.transferId == 0U || value.generation == 0U || value.count == 0U ||
      value.count > kReplayTransferMaxChunks || value.index >= value.count ||
      value.payload.empty() ||
      value.payload.size() >
          kReplayTransferMaxDatagramBytes - kChunkHeaderBytes)
    return false;
  bytes.push_back(static_cast<std::uint8_t>(ReplayTransferPacketType::Chunk));
  u32(bytes, value.transferId);
  u32(bytes, value.generation);
  u16(bytes, value.index);
  u16(bytes, value.count);
  u16(bytes, static_cast<std::uint16_t>(value.payload.size()));
  bytes.insert(bytes.end(), value.payload.begin(), value.payload.end());
  return bytes.size() <= kReplayTransferMaxDatagramBytes;
}
bool decodeReplayTransferChunk(const std::vector<std::uint8_t> &bytes,
                               ReplayTransferChunk &value) {
  std::size_t at = 1U;
  std::uint16_t length = 0;
  if (bytes.size() < kChunkHeaderBytes ||
      bytes.size() > kReplayTransferMaxDatagramBytes ||
      bytes[0] != static_cast<std::uint8_t>(ReplayTransferPacketType::Chunk) ||
      !readU32(bytes, at, value.transferId) ||
      !readU32(bytes, at, value.generation) ||
      !readU16(bytes, at, value.index) || !readU16(bytes, at, value.count) ||
      !readU16(bytes, at, length) || length == 0U ||
      at + length != bytes.size())
    return false;
  if (value.transferId == 0U || value.generation == 0U || value.count == 0U ||
      value.count > kReplayTransferMaxChunks || value.index >= value.count)
    return false;
  value.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                       bytes.end());
  return true;
}
bool encodeReplayTransferAck(const ReplayTransferAck &value,
                             std::vector<std::uint8_t> &bytes) {
  bytes.clear();
  if (value.transferId == 0U || value.generation == 0U ||
      value.index > kReplayTransferBeginAck)
    return false;
  bytes.push_back(static_cast<std::uint8_t>(ReplayTransferPacketType::Ack));
  u32(bytes, value.transferId);
  u32(bytes, value.generation);
  u16(bytes, value.index);
  return true;
}
bool decodeReplayTransferAck(const std::vector<std::uint8_t> &bytes,
                             ReplayTransferAck &value) {
  std::size_t at = 1U;
  return header(bytes, ReplayTransferPacketType::Ack, 11U) &&
         readU32(bytes, at, value.transferId) &&
         readU32(bytes, at, value.generation) &&
         readU16(bytes, at, value.index) && value.transferId != 0U &&
         value.generation != 0U && value.index <= kReplayTransferBeginAck;
}
bool encodeReplayTransferCancel(const ReplayTransferCancel &value,
                                std::vector<std::uint8_t> &bytes) {
  bytes.clear();
  if (value.transferId == 0U || value.generation == 0U ||
      !validCancel(value.reason) ||
      value.reason == ReplayTransferCancelReason::None)
    return false;
  bytes.push_back(static_cast<std::uint8_t>(ReplayTransferPacketType::Cancel));
  u32(bytes, value.transferId);
  u32(bytes, value.generation);
  bytes.push_back(static_cast<std::uint8_t>(value.reason));
  return true;
}
bool decodeReplayTransferCancel(const std::vector<std::uint8_t> &bytes,
                                ReplayTransferCancel &value) {
  std::size_t at = 1U;
  std::uint8_t reason = 0;
  if (!header(bytes, ReplayTransferPacketType::Cancel, 10U) ||
      !readU32(bytes, at, value.transferId) ||
      !readU32(bytes, at, value.generation) || at >= bytes.size())
    return false;
  reason = bytes[at];
  value.reason = static_cast<ReplayTransferCancelReason>(reason);
  return value.transferId != 0U && value.generation != 0U &&
         validCancel(value.reason) &&
         value.reason != ReplayTransferCancelReason::None;
}

bool ReplayTransferSender::begin(std::uint32_t id, std::uint32_t generation,
                                 std::vector<std::uint8_t> bytes,
                                 std::uint64_t now,
                                 ReplayTransferConfig config) {
  if (id == 0U || generation == 0U || bytes.empty() ||
      bytes.size() > kReplayTransferMaxSegmentBytes ||
      config.retryMilliseconds == 0U || config.timeoutMilliseconds == 0U ||
      config.minimumPacketIntervalMilliseconds == 0U)
    return false;
  const std::size_t payload =
      kReplayTransferMaxDatagramBytes - kChunkHeaderBytes;
  const std::size_t count = (bytes.size() + payload - 1U) / payload;
  if (count == 0U || count > kReplayTransferMaxChunks)
    return false;
  begin_ = {id, generation, static_cast<std::uint16_t>(count),
            static_cast<std::uint32_t>(bytes.size())};
  chunks_.clear();
  chunks_.reserve(count);
  for (std::size_t at = 0U; at < bytes.size(); at += payload)
    chunks_.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                         bytes.begin() + static_cast<std::ptrdiff_t>(std::min(
                                             bytes.size(), at + payload)));
  lastSent_.assign(count, std::numeric_limits<std::uint64_t>::max());
  acknowledged_.assign(count, false);
  config_ = config;
  started_ = now;
  lastPacket_ = 0U;
  beginLastSent_ = 0U;
  beginAcknowledged_ = false;
  cancelled_ = false;
  cancelSent_ = false;
  cancelReason_ = ReplayTransferCancelReason::None;
  retries_ = 0U;
  return true;
}

std::optional<std::vector<std::uint8_t>>
ReplayTransferSender::nextPacket(std::uint64_t now) {
  if (begin_.transferId == 0U || complete())
    return std::nullopt;
  if (!cancelled_ && now - started_ > config_.timeoutMilliseconds)
    cancel(ReplayTransferCancelReason::Timeout);
  if (lastPacket_ != 0U &&
      now - lastPacket_ < config_.minimumPacketIntervalMilliseconds)
    return std::nullopt;
  std::vector<std::uint8_t> wire;
  if (cancelled_) {
    if (cancelSent_ ||
        !encodeReplayTransferCancel(
            {begin_.transferId, begin_.generation, cancelReason_}, wire))
      return std::nullopt;
    cancelSent_ = true;
    lastPacket_ = now;
    return wire;
  }
  if (!beginAcknowledged_) {
    if (beginLastSent_ != 0U &&
        now - beginLastSent_ < config_.retryMilliseconds)
      return std::nullopt;
    if (beginLastSent_ != 0U)
      ++retries_;
    if (!encodeReplayTransferBegin(begin_, wire))
      return std::nullopt;
    beginLastSent_ = now;
    lastPacket_ = now;
    return wire;
  }
  for (std::size_t index = 0; index < chunks_.size(); ++index) {
    if (acknowledged_[index] ||
        (lastSent_[index] != std::numeric_limits<std::uint64_t>::max() &&
         now - lastSent_[index] < config_.retryMilliseconds))
      continue;
    if (lastSent_[index] != std::numeric_limits<std::uint64_t>::max())
      ++retries_;
    lastSent_[index] = now;
    if (!encodeReplayTransferChunk({begin_.transferId, begin_.generation,
                                    static_cast<std::uint16_t>(index),
                                    begin_.chunkCount, chunks_[index]},
                                   wire)) {
      cancel(ReplayTransferCancelReason::Invalid);
      return std::nullopt;
    }
    lastPacket_ = now;
    return wire;
  }
  return std::nullopt;
}
void ReplayTransferSender::acknowledge(const ReplayTransferAck &ack) {
  if (cancelled_ || ack.transferId != begin_.transferId ||
      ack.generation != begin_.generation)
    return;
  if (ack.index == kReplayTransferBeginAck)
    beginAcknowledged_ = true;
  else if (ack.index < acknowledged_.size())
    acknowledged_[ack.index] = true;
}
void ReplayTransferSender::cancel(ReplayTransferCancelReason reason) {
  if (!cancelled_ && reason != ReplayTransferCancelReason::None) {
    cancelled_ = true;
    cancelReason_ = reason;
  }
}
ReplayTransferStats ReplayTransferSender::stats() const {
  return {begin_.chunkCount,
          static_cast<std::uint16_t>(
              std::count(acknowledged_.begin(), acknowledged_.end(), true)),
          retries_, cancelled_};
}
bool ReplayTransferSender::complete() const {
  return !cancelled_ && !acknowledged_.empty() &&
         std::all_of(acknowledged_.begin(), acknowledged_.end(),
                     [](bool value) { return value; });
}

std::optional<ReplayTransferAck>
ReplayTransferReceiver::receiveBegin(const ReplayTransferBegin &begin) {
  const bool valid =
      begin.transferId != 0U && begin.generation != 0U &&
      begin.chunkCount > 0U && begin.chunkCount <= kReplayTransferMaxChunks &&
      begin.byteCount > 0U && begin.byteCount <= kReplayTransferMaxSegmentBytes;
  if (!valid) {
    if (active_ && begin.transferId == begin_.transferId &&
        begin.generation == begin_.generation)
      failed_ = true;
    return std::nullopt;
  }
  if (active_ && (begin.transferId != begin_.transferId ||
                  begin.generation != begin_.generation))
    return std::nullopt;
  if (active_ && (begin.chunkCount != begin_.chunkCount ||
                  begin.byteCount != begin_.byteCount)) {
    failed_ = true;
    return std::nullopt;
  }
  if (active_)
    return ReplayTransferAck{begin_.transferId, begin_.generation,
                             kReplayTransferBeginAck};
  begin_ = begin;
  chunks_.assign(begin.chunkCount, {});
  received_.assign(begin.chunkCount, false);
  bytes_ = 0U;
  active_ = true;
  failed_ = false;
  return ReplayTransferAck{begin_.transferId, begin_.generation,
                           kReplayTransferBeginAck};
}
std::optional<ReplayTransferAck>
ReplayTransferReceiver::receiveChunk(const ReplayTransferChunk &chunk) {
  if (!active_ || failed_)
    return std::nullopt;
  if (chunk.transferId != begin_.transferId ||
      chunk.generation != begin_.generation)
    return std::nullopt;
  if (chunk.count != begin_.chunkCount || chunk.index >= chunks_.size() ||
      chunk.payload.empty() ||
      chunk.payload.size() >
          kReplayTransferMaxDatagramBytes - kChunkHeaderBytes) {
    failed_ = true;
    return std::nullopt;
  }
  if (!received_[chunk.index]) {
    if (bytes_ + chunk.payload.size() > begin_.byteCount) {
      failed_ = true;
      return std::nullopt;
    }
    chunks_[chunk.index] = chunk.payload;
    received_[chunk.index] = true;
    bytes_ += chunk.payload.size();
  }
  return ReplayTransferAck{begin_.transferId, begin_.generation, chunk.index};
}
void ReplayTransferReceiver::cancel(const ReplayTransferCancel &cancel) {
  if (active_ && cancel.transferId == begin_.transferId &&
      cancel.generation == begin_.generation) {
    active_ = false;
    failed_ = true;
  }
}
std::optional<std::vector<std::uint8_t>>
ReplayTransferReceiver::takeCompleted() {
  if (!active_ || failed_ ||
      !std::all_of(received_.begin(), received_.end(),
                   [](bool value) { return value; }) ||
      bytes_ != begin_.byteCount)
    return std::nullopt;
  std::vector<std::uint8_t> bytes;
  bytes.reserve(bytes_);
  for (const auto &chunk : chunks_)
    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
  active_ = false;
  return bytes;
}
bool ReplayTransferReceiver::failed() const { return failed_; }
bool permitsRemoteKillcam(const ReplayMetadata &metadata,
                          bool localOrDeveloper) {
  return localOrDeveloper ||
         (metadata.visibility == ReplayVisibility::DuelOnly &&
          metadata.gameMode == GameMode::Duel);
}

} // namespace lg::replay
