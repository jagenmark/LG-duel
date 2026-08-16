#include "replay/ReplayTransfer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace lg::replay {
namespace {

constexpr std::size_t kLegacyChunkHeaderBytes = 23U;

void u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

bool readU16(const std::vector<std::uint8_t>& bytes, std::size_t& at,
             std::uint16_t& value) {
  if (at + 2U > bytes.size()) return false;
  value = static_cast<std::uint16_t>(bytes[at]) |
          (static_cast<std::uint16_t>(bytes[at + 1U]) << 8U);
  at += 2U;
  return true;
}

bool readU32(const std::vector<std::uint8_t>& bytes, std::size_t& at,
             std::uint32_t& value) {
  if (at + 4U > bytes.size()) return false;
  value = 0U;
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    value |= static_cast<std::uint32_t>(bytes[at++]) << shift;
  }
  return true;
}

bool validCancel(ReplayTransferCancelReason value) {
  return value >= ReplayTransferCancelReason::None &&
         value <= ReplayTransferCancelReason::Skipped;
}

bool nonZeroDigest(
    const std::array<std::uint8_t, kReplayTransferSha256Bytes>& digest) {
  return std::any_of(digest.begin(), digest.end(), [](std::uint8_t byte) {
    return byte != 0U;
  });
}

bool validBegin(const ReplayTransferBegin& value) {
  if (value.transferId == 0U || value.generation == 0U ||
      value.sessionId == 0U || value.chunkCount == 0U ||
      value.chunkCount > kReplayTransferMaxChunks || value.byteCount == 0U ||
      value.byteCount > kReplayTransferMaxSegmentBytes ||
      !nonZeroDigest(value.sha256)) {
    return false;
  }
  const std::size_t expected =
      (static_cast<std::size_t>(value.byteCount) +
       kReplayTransferMaxChunkPayloadBytes - 1U) /
      kReplayTransferMaxChunkPayloadBytes;
  return expected == value.chunkCount && expected <= kReplayTransferMaxChunks;
}

bool validChunk(const ReplayTransferChunk& value) {
  return value.transferId != 0U && value.generation != 0U &&
         value.sessionId != 0U && value.count > 0U &&
         value.count <= kReplayTransferMaxChunks && value.index < value.count &&
         !value.payload.empty() &&
         value.payload.size() <= kReplayTransferMaxChunkPayloadBytes &&
         value.crc32 == replayTransferCrc32(value.payload);
}

bool validAck(const ReplayTransferAck& value) {
  return value.transferId != 0U && value.generation != 0U &&
         value.sessionId != 0U && value.index <= kReplayTransferBeginAck;
}

bool validCancel(const ReplayTransferCancel& value) {
  return value.transferId != 0U && value.generation != 0U &&
         value.sessionId != 0U && validCancel(value.reason) &&
         value.reason != ReplayTransferCancelReason::None;
}

bool legacyHeader(const std::vector<std::uint8_t>& bytes,
                  ReplayTransferPacketType expected, std::size_t minimum) {
  return bytes.size() >= minimum && bytes.size() <= kReplayTransferMaxDatagramBytes &&
         !bytes.empty() &&
         bytes[0] == static_cast<std::uint8_t>(expected);
}

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::array<std::uint32_t, 8> kSha256Initial = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

constexpr std::uint32_t rotateRight(std::uint32_t value, unsigned count) {
  return (value >> count) | (value << (32U - count));
}

void shaTransform(std::array<std::uint32_t, 8>& state,
                  const std::uint8_t* block) {
  std::array<std::uint32_t, 64> words = {};
  for (std::size_t index = 0U; index < 16U; ++index) {
    const std::size_t offset = index * 4U;
    words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                   (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                   static_cast<std::uint32_t>(block[offset + 3U]);
  }
  for (std::size_t index = 16U; index < words.size(); ++index) {
    const std::uint32_t s0 = rotateRight(words[index - 15U], 7U) ^
                             rotateRight(words[index - 15U], 18U) ^
                             (words[index - 15U] >> 3U);
    const std::uint32_t s1 = rotateRight(words[index - 2U], 17U) ^
                             rotateRight(words[index - 2U], 19U) ^
                             (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
  }

  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  std::uint32_t e = state[4];
  std::uint32_t f = state[5];
  std::uint32_t g = state[6];
  std::uint32_t h = state[7];
  for (std::size_t index = 0U; index < words.size(); ++index) {
    const std::uint32_t s1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^
                             rotateRight(e, 25U);
    const std::uint32_t choice = (e & f) ^ ((~e) & g);
    const std::uint32_t temporary1 =
        h + s1 + choice + kSha256RoundConstants[index] + words[index];
    const std::uint32_t s0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^
                             rotateRight(a, 22U);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temporary2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

} // namespace

std::uint32_t replayTransferCrc32(const std::vector<std::uint8_t>& bytes) {
  std::uint32_t crc = 0xffffffffU;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
      const std::uint32_t mask = -(crc & 1U);
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

std::array<std::uint8_t, kReplayTransferSha256Bytes>
replayTransferSha256(const std::vector<std::uint8_t>& bytes) {
  std::array<std::uint32_t, 8> state = kSha256Initial;
  std::array<std::uint8_t, 64> block = {};
  std::size_t offset = 0U;
  while (bytes.size() - offset >= block.size()) {
    shaTransform(state, bytes.data() + offset);
    offset += block.size();
  }
  const std::size_t remainder = bytes.size() - offset;
  std::fill(block.begin(), block.end(), 0U);
  if (remainder > 0U) {
    std::copy_n(bytes.data() + offset, remainder, block.data());
  }
  block[remainder] = 0x80U;
  const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8U;
  if (remainder >= 56U) {
    shaTransform(state, block.data());
    std::fill(block.begin(), block.end(), 0U);
  }
  for (unsigned index = 0U; index < 8U; ++index) {
    block[63U - index] = static_cast<std::uint8_t>(bitLength >> (index * 8U));
  }
  shaTransform(state, block.data());

  std::array<std::uint8_t, kReplayTransferSha256Bytes> digest = {};
  for (std::size_t index = 0U; index < state.size(); ++index) {
    digest[index * 4U] = static_cast<std::uint8_t>(state[index] >> 24U);
    digest[index * 4U + 1U] = static_cast<std::uint8_t>(state[index] >> 16U);
    digest[index * 4U + 2U] = static_cast<std::uint8_t>(state[index] >> 8U);
    digest[index * 4U + 3U] = static_cast<std::uint8_t>(state[index]);
  }
  return digest;
}

bool encodeReplayTransferBegin(const ReplayTransferBegin& value,
                               std::vector<std::uint8_t>& bytes) {
  bytes.clear();
  if (!validBegin(value)) return false;
  bytes.push_back(static_cast<std::uint8_t>(ReplayTransferPacketType::Begin));
  u32(bytes, value.transferId);
  u32(bytes, value.generation);
  u16(bytes, value.chunkCount);
  u32(bytes, value.byteCount);
  u32(bytes, value.sessionId);
  bytes.insert(bytes.end(), value.sha256.begin(), value.sha256.end());
  return true;
}

bool decodeReplayTransferBegin(const std::vector<std::uint8_t>& bytes,
                               ReplayTransferBegin& value) {
  if (!legacyHeader(bytes, ReplayTransferPacketType::Begin, 51U)) return false;
  std::size_t at = 1U;
  ReplayTransferBegin decoded;
  if (!readU32(bytes, at, decoded.transferId) ||
      !readU32(bytes, at, decoded.generation) ||
      !readU16(bytes, at, decoded.chunkCount) ||
      !readU32(bytes, at, decoded.byteCount) ||
      !readU32(bytes, at, decoded.sessionId) ||
      at + decoded.sha256.size() != bytes.size()) {
    return false;
  }
  std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end(),
            decoded.sha256.begin());
  if (!validBegin(decoded)) return false;
  value = decoded;
  return true;
}

bool encodeReplayTransferChunk(const ReplayTransferChunk& value,
                               std::vector<std::uint8_t>& bytes) {
  bytes.clear();
  if (!validChunk(value) || value.payload.size() >
                                  kReplayTransferMaxDatagramBytes -
                                      kLegacyChunkHeaderBytes) {
    return false;
  }
  bytes.push_back(static_cast<std::uint8_t>(ReplayTransferPacketType::Chunk));
  u32(bytes, value.transferId);
  u32(bytes, value.generation);
  u16(bytes, value.index);
  u16(bytes, value.count);
  u16(bytes, static_cast<std::uint16_t>(value.payload.size()));
  u32(bytes, value.crc32);
  u32(bytes, value.sessionId);
  bytes.insert(bytes.end(), value.payload.begin(), value.payload.end());
  return true;
}

bool decodeReplayTransferChunk(const std::vector<std::uint8_t>& bytes,
                               ReplayTransferChunk& value) {
  if (!legacyHeader(bytes, ReplayTransferPacketType::Chunk,
                    kLegacyChunkHeaderBytes)) {
    return false;
  }
  std::size_t at = 1U;
  ReplayTransferChunk decoded;
  std::uint16_t length = 0U;
  if (!readU32(bytes, at, decoded.transferId) ||
      !readU32(bytes, at, decoded.generation) ||
      !readU16(bytes, at, decoded.index) ||
      !readU16(bytes, at, decoded.count) || !readU16(bytes, at, length) ||
      !readU32(bytes, at, decoded.crc32) ||
      !readU32(bytes, at, decoded.sessionId) || length == 0U ||
      at + length != bytes.size() || length > kReplayTransferMaxChunkPayloadBytes) {
    return false;
  }
  decoded.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                         bytes.end());
  if (!validChunk(decoded)) return false;
  value = std::move(decoded);
  return true;
}

bool encodeReplayTransferAck(const ReplayTransferAck& value,
                             std::vector<std::uint8_t>& bytes) {
  bytes.clear();
  if (!validAck(value)) return false;
  bytes.push_back(static_cast<std::uint8_t>(ReplayTransferPacketType::Ack));
  u32(bytes, value.transferId);
  u32(bytes, value.generation);
  u16(bytes, value.index);
  u32(bytes, value.sessionId);
  return true;
}

bool decodeReplayTransferAck(const std::vector<std::uint8_t>& bytes,
                             ReplayTransferAck& value) {
  if (bytes.size() != 15U ||
      !legacyHeader(bytes, ReplayTransferPacketType::Ack, 15U)) {
    return false;
  }
  std::size_t at = 1U;
  ReplayTransferAck decoded;
  if (!readU32(bytes, at, decoded.transferId) ||
      !readU32(bytes, at, decoded.generation) ||
      !readU16(bytes, at, decoded.index) ||
      !readU32(bytes, at, decoded.sessionId) || !validAck(decoded)) {
    return false;
  }
  value = decoded;
  return true;
}

bool encodeReplayTransferCancel(const ReplayTransferCancel& value,
                                std::vector<std::uint8_t>& bytes) {
  bytes.clear();
  if (!validCancel(value)) return false;
  bytes.push_back(static_cast<std::uint8_t>(ReplayTransferPacketType::Cancel));
  u32(bytes, value.transferId);
  u32(bytes, value.generation);
  bytes.push_back(static_cast<std::uint8_t>(value.reason));
  u32(bytes, value.sessionId);
  return true;
}

bool decodeReplayTransferCancel(const std::vector<std::uint8_t>& bytes,
                                ReplayTransferCancel& value) {
  if (bytes.size() != 14U ||
      !legacyHeader(bytes, ReplayTransferPacketType::Cancel, 14U)) {
    return false;
  }
  std::size_t at = 1U;
  ReplayTransferCancel decoded;
  std::uint8_t reason = 0U;
  if (!readU32(bytes, at, decoded.transferId) ||
      !readU32(bytes, at, decoded.generation) || at >= bytes.size()) {
    return false;
  }
  reason = bytes[at++];
  decoded.reason = static_cast<ReplayTransferCancelReason>(reason);
  if (!readU32(bytes, at, decoded.sessionId) || at != bytes.size() ||
      !validCancel(decoded)) {
    return false;
  }
  value = decoded;
  return true;
}

bool ReplayTransferSender::begin(std::uint32_t id, std::uint32_t generation,
                                 std::vector<std::uint8_t> bytes,
                                 std::uint64_t now,
                                 ReplayTransferConfig config) {
  if (id == 0U || generation == 0U || config.sessionId == 0U || bytes.empty() ||
      bytes.size() > kReplayTransferMaxSegmentBytes ||
      config.retryMilliseconds == 0U || config.timeoutMilliseconds == 0U ||
      config.minimumPacketIntervalMilliseconds == 0U) {
    return false;
  }
  const std::size_t count =
      (bytes.size() + kReplayTransferMaxChunkPayloadBytes - 1U) /
      kReplayTransferMaxChunkPayloadBytes;
  if (count == 0U || count > kReplayTransferMaxChunks) return false;
  begin_ = {id,
            generation,
            static_cast<std::uint16_t>(count),
            static_cast<std::uint32_t>(bytes.size()),
            config.sessionId,
            replayTransferSha256(bytes)};
  chunks_.clear();
  chunks_.reserve(count);
  for (std::size_t at = 0U; at < bytes.size();
       at += kReplayTransferMaxChunkPayloadBytes) {
    chunks_.emplace_back(
        bytes.begin() + static_cast<std::ptrdiff_t>(at),
        bytes.begin() + static_cast<std::ptrdiff_t>(
                            std::min(bytes.size(),
                                     at + kReplayTransferMaxChunkPayloadBytes)));
  }
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

std::optional<ReplayTransferMessage>
ReplayTransferSender::nextMessage(std::uint64_t now) {
  if (begin_.transferId == 0U || complete()) return std::nullopt;
  if (!cancelled_ && now >= started_ &&
      now - started_ > config_.timeoutMilliseconds) {
    cancel(ReplayTransferCancelReason::Timeout);
  }
  if (lastPacket_ != 0U && now >= lastPacket_ &&
      now - lastPacket_ < config_.minimumPacketIntervalMilliseconds) {
    return std::nullopt;
  }
  if (cancelled_) {
    if (cancelSent_) return std::nullopt;
    cancelSent_ = true;
    lastPacket_ = now;
    return ReplayTransferCancel{begin_.transferId, begin_.generation,
                                cancelReason_, begin_.sessionId};
  }
  if (!beginAcknowledged_) {
    if (beginLastSent_ != 0U && now >= beginLastSent_ &&
        now - beginLastSent_ < config_.retryMilliseconds) {
      return std::nullopt;
    }
    if (beginLastSent_ != 0U) ++retries_;
    beginLastSent_ = now;
    lastPacket_ = now;
    return begin_;
  }
  for (std::size_t index = 0U; index < chunks_.size(); ++index) {
    if (acknowledged_[index] ||
        (lastSent_[index] != std::numeric_limits<std::uint64_t>::max() &&
         now >= lastSent_[index] &&
         now - lastSent_[index] < config_.retryMilliseconds)) {
      continue;
    }
    if (lastSent_[index] != std::numeric_limits<std::uint64_t>::max()) ++retries_;
    lastSent_[index] = now;
    lastPacket_ = now;
    ReplayTransferChunk chunk;
    chunk.transferId = begin_.transferId;
    chunk.generation = begin_.generation;
    chunk.index = static_cast<std::uint16_t>(index);
    chunk.count = begin_.chunkCount;
    chunk.payload = chunks_[index];
    chunk.crc32 = replayTransferCrc32(chunk.payload);
    chunk.sessionId = begin_.sessionId;
    return chunk;
  }
  return std::nullopt;
}

std::optional<std::vector<std::uint8_t>>
ReplayTransferSender::nextPacket(std::uint64_t now) {
  const auto message = nextMessage(now);
  if (!message.has_value()) return std::nullopt;
  return std::visit(
      [](const auto& value) -> std::optional<std::vector<std::uint8_t>> {
        std::vector<std::uint8_t> bytes;
        bool ok = false;
        using Message = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Message, ReplayTransferBegin>) {
          ok = encodeReplayTransferBegin(value, bytes);
        } else if constexpr (std::is_same_v<Message, ReplayTransferChunk>) {
          ok = encodeReplayTransferChunk(value, bytes);
        } else if constexpr (std::is_same_v<Message, ReplayTransferAck>) {
          ok = encodeReplayTransferAck(value, bytes);
        } else if constexpr (std::is_same_v<Message, ReplayTransferCancel>) {
          ok = encodeReplayTransferCancel(value, bytes);
        }
        return ok ? std::optional<std::vector<std::uint8_t>>(std::move(bytes))
                  : std::nullopt;
      },
      *message);
}

void ReplayTransferSender::acknowledge(const ReplayTransferAck& ack) {
  if (cancelled_ || ack.transferId != begin_.transferId ||
      ack.generation != begin_.generation || ack.sessionId != begin_.sessionId) {
    return;
  }
  if (ack.index == kReplayTransferBeginAck) {
    beginAcknowledged_ = true;
  } else if (ack.index < acknowledged_.size()) {
    acknowledged_[ack.index] = true;
  }
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

ReplayTransferReceiver::ReplayTransferReceiver(ReplayTransferReceiverConfig config)
    : config_(config) {
  if (config_.idleTimeoutMilliseconds == 0U) config_.idleTimeoutMilliseconds = 1U;
  if (config_.overallTimeoutMilliseconds < config_.idleTimeoutMilliseconds) {
    config_.overallTimeoutMilliseconds = config_.idleTimeoutMilliseconds;
  }
}

std::optional<ReplayTransferAck>
ReplayTransferReceiver::receiveBegin(const ReplayTransferBegin& begin,
                                     std::uint64_t now) {
  if (!validBegin(begin)) {
    if (active_ && begin.transferId == begin_.transferId &&
        begin.generation == begin_.generation &&
        begin.sessionId == begin_.sessionId) {
      failed_ = true;
    }
    return std::nullopt;
  }
  if (active_ && (begin.transferId != begin_.transferId ||
                  begin.generation != begin_.generation ||
                  begin.sessionId != begin_.sessionId)) {
    return std::nullopt;
  }
  if (active_ && (begin.chunkCount != begin_.chunkCount ||
                  begin.byteCount != begin_.byteCount ||
                  begin.sha256 != begin_.sha256)) {
    failed_ = true;
    return std::nullopt;
  }
  if (active_) {
    if (failed_) return std::nullopt;
    lastActivity_ = now;
    return ReplayTransferAck{begin_.transferId, begin_.generation,
                             kReplayTransferBeginAck, begin_.sessionId};
  }
  begin_ = begin;
  chunks_.assign(begin.chunkCount, {});
  received_.assign(begin.chunkCount, false);
  bytes_ = 0U;
  active_ = true;
  failed_ = false;
  started_ = now;
  lastActivity_ = now;
  return ReplayTransferAck{begin.transferId, begin.generation,
                           kReplayTransferBeginAck, begin.sessionId};
}

std::optional<ReplayTransferAck>
ReplayTransferReceiver::receiveChunk(const ReplayTransferChunk& chunk,
                                      std::uint64_t now) {
  if (!active_ || failed_ || chunk.transferId != begin_.transferId ||
      chunk.generation != begin_.generation ||
      chunk.sessionId != begin_.sessionId) {
    return std::nullopt;
  }
  if (chunk.count != begin_.chunkCount || chunk.index >= chunks_.size() ||
      !validChunk(chunk)) {
    failed_ = true;
    return std::nullopt;
  }
  if (!received_[chunk.index]) {
    if (bytes_ > begin_.byteCount ||
        chunk.payload.size() > begin_.byteCount - bytes_) {
      failed_ = true;
      return std::nullopt;
    }
    chunks_[chunk.index] = chunk.payload;
    received_[chunk.index] = true;
    bytes_ += chunk.payload.size();
  }
  lastActivity_ = now;
  return ReplayTransferAck{begin_.transferId, begin_.generation, chunk.index,
                           begin_.sessionId};
}

void ReplayTransferReceiver::cancel(const ReplayTransferCancel& cancel) {
  if (active_ && cancel.transferId == begin_.transferId &&
      cancel.generation == begin_.generation &&
      cancel.sessionId == begin_.sessionId) {
    active_ = false;
    failed_ = true;
  }
}

bool ReplayTransferReceiver::expire(std::uint64_t now) {
  if (!active_ || now < started_ || now < lastActivity_) return false;
  if (now - started_ < config_.overallTimeoutMilliseconds &&
      now - lastActivity_ < config_.idleTimeoutMilliseconds) {
    return false;
  }
  begin_ = {};
  chunks_.clear();
  received_.clear();
  bytes_ = 0U;
  active_ = false;
  failed_ = true;
  return true;
}

std::optional<std::vector<std::uint8_t>>
ReplayTransferReceiver::takeCompleted() {
  if (!active_ || failed_ || bytes_ != begin_.byteCount ||
      !std::all_of(received_.begin(), received_.end(),
                   [](bool value) { return value; })) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> result;
  result.reserve(bytes_);
  for (const auto& chunk : chunks_) {
    result.insert(result.end(), chunk.begin(), chunk.end());
  }
  if (result.size() != begin_.byteCount ||
      replayTransferSha256(result) != begin_.sha256) {
    active_ = false;
    failed_ = true;
    return std::nullopt;
  }
  active_ = false;
  chunks_.clear();
  received_.clear();
  return result;
}

bool ReplayTransferReceiver::failed() const { return failed_; }

bool permitsRemoteKillcam(const ReplayMetadata& metadata,
                          bool localOrDeveloper) {
  if (localOrDeveloper) return true;
  return metadata.visibility == ReplayVisibility::DuelOnly &&
         metadata.gameMode == GameMode::Duel;
}

} // namespace lg::replay
