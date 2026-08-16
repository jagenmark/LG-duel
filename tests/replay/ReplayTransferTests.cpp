#include "replay/ReplayTransfer.hpp"

#include "net/NetCodec.hpp"
#include "replay/ReplayCodec.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition)
    return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool decodeBegin(const std::vector<std::uint8_t> &wire,
                 lg::replay::ReplayTransferBegin &begin) {
  return lg::replay::decodeReplayTransferBegin(wire, begin);
}

bool decodeChunk(const std::vector<std::uint8_t> &wire,
                 lg::replay::ReplayTransferChunk &chunk) {
  return lg::replay::decodeReplayTransferChunk(wire, chunk);
}

lg::replay::ReplayDemo compactDuelDemo() {
  lg::replay::ReplayDemo demo;
  demo.metadata.protocolRevision = lg::kProtocolVersion;
  demo.metadata.initialServerTick = 100U;
  demo.metadata.mapRevision = 1U;
  demo.metadata.mapName = "compact_duel";
  demo.metadata.mapContentHash = 1U;
  demo.metadata.gameMode = lg::GameMode::Duel;
  demo.metadata.visibility = lg::replay::ReplayVisibility::DuelOnly;
  demo.metadata.gameplayConfigHash =
    lg::replay::canonicalGameplayConfigHash(demo.metadata.gameplayConfig);
  for (std::size_t index = 0U; index < demo.metadata.players.size(); ++index) {
    demo.metadata.players[index].slot = static_cast<std::uint8_t>(index);
  }
  demo.metadata.players[0].occupied = true;
  demo.metadata.players[1].occupied = true;

  lg::replay::ReplayCheckpoint checkpoint;
  checkpoint.serverTick = demo.metadata.initialServerTick;
  checkpoint.mapRevision = demo.metadata.mapRevision;
  checkpoint.projectileRevision = 1U;
  checkpoint.gameplayConfigHash = demo.metadata.gameplayConfigHash;
  checkpoint.spawnRandomState = 1U;
  checkpoint.match.gameMode = lg::GameMode::Duel;
  checkpoint.history.push_back({checkpoint.serverTick, {}});
  demo.checkpoints.push_back(checkpoint);
  demo.hashes.push_back(
      {checkpoint.serverTick, lg::replay::canonicalStateHash(checkpoint)});

  for (std::uint32_t offset = 0U; offset < 1000U; ++offset) {
    lg::replay::ReplayTickInput input;
    input.tick = demo.metadata.initialServerTick + offset;
    for (std::size_t slot = 0U; slot < 2U; ++slot) {
      lg::replay::ReplaySlotInput &value = input.slots[slot];
      value.present = true;
      value.hasCommand = true;
      value.receivedThisTick = true;
      value.command.sequence = offset + 1U;
      value.command.clientTick = 1000U + offset;
      value.command.forwardMove = (offset % 7U) < 4U ? 1.0F : -1.0F;
      value.command.rightMove = slot == 0U ? 0.25F : -0.25F;
      value.command.viewYawRadians = static_cast<float>(offset) * 0.01F;
      value.command.viewPitchRadians = static_cast<float>(slot) * -0.1F;
      value.command.attack = (offset % 11U) == 0U;
      value.command.weapon = lg::Weapon::MachineGun;
      value.viewedServerTick = input.tick;
      value.consumedActionEdges.attack = offset;
      value.consumedActionEdges.attackYawRadians =
          value.command.viewYawRadians;
      value.consumedActionEdges.attackWeapon = lg::Weapon::MachineGun;
    }
    demo.ticks.push_back(input);
  }
  return demo;
}

} // namespace

int main() {
  int failures = 0;
  std::vector<std::uint8_t> source(2500U);
  for (std::size_t index = 0U; index < source.size(); ++index) {
    source[index] = static_cast<std::uint8_t>(index & 0xffU);
  }

  lg::replay::ReplayTransferConfig config;
  config.retryMilliseconds = 10U;
  config.timeoutMilliseconds = 100U;
  config.minimumPacketIntervalMilliseconds = 1U;
  lg::replay::ReplayTransferSender sender;
  failures += expect(sender.begin(7U, 3U, source, 1U, config),
                     "bounded transfer should start");

  const std::optional<std::vector<std::uint8_t>> beginWire =
      sender.nextPacket(1U);
  failures += expect(beginWire.has_value() &&
                         beginWire->size() <=
                             lg::replay::kReplayTransferMaxDatagramBytes,
                     "begin must fit the datagram limit");
  lg::PacketType ordinaryPacketType = lg::PacketType::Snapshot;
  failures += expect(
      beginWire.has_value() &&
          !lg::inspectPacketType(*beginWire, ordinaryPacketType),
      "replay frames must not add bytes to the ordinary snapshot wire format");
  lg::replay::ReplayTransferBegin begin;
  failures += expect(beginWire.has_value() && decodeBegin(*beginWire, begin),
                     "begin should decode strictly");
  lg::replay::ReplayTransferReceiver receiver;
  const std::optional<lg::replay::ReplayTransferAck> beginAck =
      receiver.receiveBegin(begin);
  failures += expect(beginAck.has_value() &&
                         beginAck->index == lg::replay::kReplayTransferBeginAck,
                     "receiver should acknowledge a valid begin");
  if (beginAck.has_value())
    sender.acknowledge(*beginAck);
  failures += expect(
      !receiver.receiveChunk({99U, 1U, 0U, begin.chunkCount, {1U}})
              .has_value() &&
          !receiver.failed(),
      "a different transfer must not make the active replay fail closed");

  // Drop chunk zero, then deliver later chunks first.  The duplicate begin must
  // not reset already received chunks while a UDP sender retries it.
  const std::optional<std::vector<std::uint8_t>> chunk0Wire =
      sender.nextPacket(2U);
  lg::replay::ReplayTransferChunk chunk0;
  failures += expect(chunk0Wire.has_value() &&
                         decodeChunk(*chunk0Wire, chunk0) && chunk0.index == 0U,
                     "first data packet should be chunk zero");
  const std::optional<std::vector<std::uint8_t>> chunk1Wire =
      sender.nextPacket(3U);
  lg::replay::ReplayTransferChunk chunk1;
  failures +=
      expect(chunk1Wire.has_value() && decodeChunk(*chunk1Wire, chunk1) &&
                 chunk1.index == 1U,
             "later chunk should send while an earlier chunk waits for retry");
  const std::optional<lg::replay::ReplayTransferAck> chunk1Ack =
      receiver.receiveChunk(chunk1);
  const std::optional<lg::replay::ReplayTransferAck> duplicateChunk1Ack =
      receiver.receiveChunk(chunk1);
  failures += expect(chunk1Ack.has_value() && duplicateChunk1Ack.has_value(),
                     "duplicate chunks should be idempotent");
  if (chunk1Ack.has_value())
    sender.acknowledge(*chunk1Ack);

  const std::optional<std::vector<std::uint8_t>> chunk2Wire =
      sender.nextPacket(4U);
  lg::replay::ReplayTransferChunk chunk2;
  failures +=
      expect(chunk2Wire.has_value() && decodeChunk(*chunk2Wire, chunk2) &&
                 chunk2.index == 2U,
             "third chunk should send after the reordered second chunk");
  const std::optional<lg::replay::ReplayTransferAck> chunk2Ack =
      receiver.receiveChunk(chunk2);
  if (chunk2Ack.has_value())
    sender.acknowledge(*chunk2Ack);
  const std::optional<lg::replay::ReplayTransferAck> duplicateBeginAck =
      receiver.receiveBegin(begin);
  failures += expect(duplicateBeginAck.has_value(),
                     "duplicate begin should be idempotent");

  const std::optional<std::vector<std::uint8_t>> retryWire =
      sender.nextPacket(12U);
  lg::replay::ReplayTransferChunk retryChunk;
  failures +=
      expect(retryWire.has_value() && decodeChunk(*retryWire, retryChunk) &&
                 retryChunk.index == 0U,
             "lost chunk should retry after the configured interval");
  const std::optional<lg::replay::ReplayTransferAck> retryAck =
      receiver.receiveChunk(retryChunk);
  if (retryAck.has_value())
    sender.acknowledge(*retryAck);
  failures +=
      expect(sender.complete() && sender.stats().retries == 1U,
             "reordered delivery should complete after one lost-packet retry");
  const std::optional<std::vector<std::uint8_t>> result =
      receiver.takeCompleted();
  failures += expect(
      result.has_value() && *result == source,
      "completed transfer should restore the exact self-contained segment");

  lg::replay::ReplayTransferSender lostBeginSender;
  failures += expect(lostBeginSender.begin(8U, 4U, {1U}, 1U, config),
                     "begin-retry sender should start");
  const std::optional<std::vector<std::uint8_t>> lostBegin =
      lostBeginSender.nextPacket(1U);
  const std::optional<std::vector<std::uint8_t>> retriedBegin =
      lostBeginSender.nextPacket(11U);
  failures +=
      expect(lostBegin.has_value() && retriedBegin.has_value() &&
                 decodeBegin(*retriedBegin, begin) &&
                 lostBeginSender.stats().retries == 1U,
             "lost begin should retry before any gameplay bytes are sent");

  lg::replay::ReplayTransferSender timeoutSender;
  failures += expect(timeoutSender.begin(9U, 5U, {1U}, 1U, config),
                     "timeout sender should start");
  (void)timeoutSender.nextPacket(1U);
  const std::optional<std::vector<std::uint8_t>> timeoutWire =
      timeoutSender.nextPacket(102U);
  lg::replay::ReplayTransferCancel timeoutCancel;
  failures += expect(
      timeoutWire.has_value() &&
          lg::replay::decodeReplayTransferCancel(*timeoutWire, timeoutCancel) &&
          timeoutCancel.reason ==
              lg::replay::ReplayTransferCancelReason::Timeout,
      "unacknowledged transfer should cancel on timeout");

  lg::replay::ReplayTransferReceiver cancelledReceiver;
  const std::optional<lg::replay::ReplayTransferAck> cancelledBeginAck =
      cancelledReceiver.receiveBegin(begin);
  failures += expect(cancelledBeginAck.has_value(),
                     "receiver should accept a later stream");
  cancelledReceiver.cancel({begin.transferId, begin.generation,
                            lg::replay::ReplayTransferCancelReason::Skipped});
  failures +=
      expect(cancelledReceiver.failed() &&
                 !cancelledReceiver.takeCompleted().has_value(),
             "cancelled client replay must fail closed and skip playback");

  std::vector<std::uint8_t> oversized(
      lg::replay::kReplayTransferMaxDatagramBytes + 1U, 0U);
  lg::replay::ReplayTransferChunk invalidChunk;
  failures +=
      expect(!lg::replay::decodeReplayTransferChunk(oversized, invalidChunk),
             "oversized datagrams should reject before allocation");
  std::vector<std::uint8_t> wire;
  failures += expect(!lg::replay::encodeReplayTransferAck(
                         {7U, 3U,
                          static_cast<std::uint16_t>(
                              lg::replay::kReplayTransferBeginAck + 1U)},
                         wire),
                     "unknown ack indexes should reject");
  lg::replay::ReplayTransferConfig unboundedRate = config;
  unboundedRate.minimumPacketIntervalMilliseconds = 0U;
  failures += expect(!sender.begin(10U, 6U, {1U}, 1U, unboundedRate),
                     "transfer start should reject an unbounded send rate");

  lg::replay::ReplayMetadata metadata;
  metadata.gameMode = lg::GameMode::Duel;
  metadata.visibility = lg::replay::ReplayVisibility::DuelOnly;
  failures +=
      expect(lg::replay::permitsRemoteKillcam(metadata, false),
             "only duel metadata may permit an ordinary remote killcam");
  metadata.gameMode = lg::GameMode::ClanArena;
  failures += expect(!lg::replay::permitsRemoteKillcam(metadata, false) &&
                         lg::replay::permitsRemoteKillcam(metadata, true),
                     "team modes must stay local or developer-only until a "
                     "safe filter exists");
  metadata.visibility = lg::replay::ReplayVisibility::DeveloperFull;
  metadata.gameMode = lg::GameMode::Duel;
  failures += expect(
      !lg::replay::permitsRemoteKillcam(metadata, false),
      "developer-full metadata must not cross the ordinary remote boundary");

  lg::replay::ReplayTransferReceiver expiringReceiver({5U, 20U});
  failures += expect(
      expiringReceiver.receiveBegin({90U, 1U, 1U, 1U}, 1U).has_value() &&
          !expiringReceiver.expire(5U) && expiringReceiver.expire(6U),
      "a lost cancel should expire a stalled receiver instead of pinning it "
      "forever");
  failures += expect(
      expiringReceiver.receiveBegin({91U, 1U, 1U, 1U}, 7U).has_value() &&
          !expiringReceiver.failed(),
      "receiver expiry should reset state and accept a later transfer");

  const lg::replay::ReplayDemo duelDemo = compactDuelDemo();
  std::vector<std::uint8_t> duelBytes;
  lg::replay::ReplayDemo decodedDuel;
  failures += expect(
      lg::replay::encodeDemo(duelDemo, duelBytes) &&
          duelBytes.size() <= lg::replay::kReplayTransferMaxSegmentBytes,
      "an eight-second two-player duel segment must fit the bounded killcam "
      "transfer");
  constexpr std::size_t kTenMinutesAtEightSecondSamples = 75U;
  failures += expect(
      duelBytes.size() <=
          lg::replay::kMaxReplayBytes / kTenMinutesAtEightSecondSamples,
      "the measured sparse two-player sample should conservatively fit a "
      "ten-minute full-demo byte cap");
  failures += expect(
      lg::replay::decodeDemo(duelBytes, decodedDuel) &&
          decodedDuel.ticks.size() == 1000U &&
          decodedDuel.ticks[999].slots[1].command.sequence == 1000U,
      "compact replay input records should strictly round trip changing duel "
      "commands");

  lg::replay::ReplayTransferConfig segmentConfig;
  segmentConfig.retryMilliseconds = 1U;
  segmentConfig.timeoutMilliseconds = 100000U;
  segmentConfig.minimumPacketIntervalMilliseconds = 1U;
  lg::replay::ReplayTransferSender segmentSender;
  failures += expect(
      segmentSender.begin(77U, 9U, duelBytes, 1U, segmentConfig),
      "compact duel segment should start a bounded transfer");
  lg::replay::ReplayTransferReceiver segmentReceiver;
  std::uint64_t now = 1U;
  const std::optional<std::vector<std::uint8_t>> segmentBeginWire =
      segmentSender.nextPacket(now);
  lg::replay::ReplayTransferBegin segmentBegin;
  failures += expect(
      segmentBeginWire.has_value() &&
          segmentBeginWire->size() <=
              lg::replay::kReplayTransferMaxDatagramBytes &&
          lg::replay::decodeReplayTransferBegin(*segmentBeginWire,
                                                segmentBegin),
      "compact segment begin should remain a bounded datagram");
  const std::optional<lg::replay::ReplayTransferAck> segmentBeginAck =
      segmentReceiver.receiveBegin(segmentBegin, now);
  if (segmentBeginAck.has_value())
    segmentSender.acknowledge(*segmentBeginAck);
  std::size_t packetCount = 1U;
  while (!segmentSender.complete() &&
         packetCount <= lg::replay::kReplayTransferMaxChunks + 1U) {
    ++now;
    const std::optional<std::vector<std::uint8_t>> wirePacket =
        segmentSender.nextPacket(now);
    if (!wirePacket.has_value())
      continue;
    ++packetCount;
    lg::replay::ReplayTransferChunk chunk;
    failures += expect(
        wirePacket->size() <= lg::replay::kReplayTransferMaxDatagramBytes &&
            lg::replay::decodeReplayTransferChunk(*wirePacket, chunk),
        "every compact segment packet should stay within the UDP bound");
    const std::optional<lg::replay::ReplayTransferAck> acknowledgement =
        segmentReceiver.receiveChunk(chunk, now);
    if (acknowledgement.has_value())
      segmentSender.acknowledge(*acknowledgement);
  }
  const std::optional<std::vector<std::uint8_t>> reassembled =
      segmentReceiver.takeCompleted();
  lg::replay::ReplayDemo transferredDuel;
  failures += expect(
      segmentSender.complete() && reassembled.has_value() &&
          *reassembled == duelBytes &&
          lg::replay::decodeDemo(*reassembled, transferredDuel) &&
          transferredDuel.ticks.size() == 1000U,
      "packetized compact duel segment should reassemble and decode without "
      "widening transfer bounds");
  std::cout << "replay-transfer-measure compact-duel-bytes=" << duelBytes.size()
            << " compact-duel-chunks=" << segmentSender.stats().chunks
            << " compact-duel-ten-minute-upper-bytes="
            << duelBytes.size() * kTenMinutesAtEightSecondSamples << '\n';

  return failures == 0 ? 0 : 1;
}
