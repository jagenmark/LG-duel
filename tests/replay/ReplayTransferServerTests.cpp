#include "replay/ReplayTransferServer.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;
  lg::replay::ReplayTransferServerConfig config;
  config.maximumSegmentBytes = 4096U;
  config.transfer.retryMilliseconds = 1U;
  config.transfer.timeoutMilliseconds = 20U;
  config.transfer.minimumPacketIntervalMilliseconds = 1U;
  lg::replay::ReplayTransferServer server(config);
  const std::vector<std::uint8_t> bytes(1800U, 0x5aU);

  std::string error;
  failures += expect(
      server.start(0U, 42U, 7U, bytes, 1U, &error, 19U),
      "server transfer should accept a bounded segment");
  failures += expect(server.active(0U) && server.activeCount() == 1U,
                     "server should expose one active transfer");
  failures += expect(
      server.status(0U).has_value() &&
          server.status(0U)->sessionId == 42U &&
          server.status(0U)->generation == 7U &&
          server.status(0U)->lethalSequence == 19U &&
          server.status(0U)->bytes == bytes.size(),
      "server status should retain the authenticated transfer tuple");
  failures += expect(
      !server.start(0U, 42U, 8U, {1U}, 1U, &error),
      "one client should not run two transfers at once");

  const auto begin = server.poll(1U, 1U);
  failures += expect(
      begin.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferBegin>(
              begin.front().message),
      "server poll should enforce one outbound datagram");
  lg::replay::ReplayTransferReceiver receiver;
  if (!begin.empty()) {
    const auto* beginMessage =
        std::get_if<lg::replay::ReplayTransferBegin>(&begin.front().message);
    if (beginMessage != nullptr) {
      const auto acknowledgement = receiver.receiveBegin(*beginMessage, 1U);
      failures += expect(acknowledgement.has_value(),
                         "receiver should acknowledge the server begin");
      if (acknowledgement.has_value()) {
        server.receive(0U, 41U, *acknowledgement);
        failures += expect(
            server.status(0U)->stats.acknowledged == 0U,
            "stale session ACK should not advance the sender");
        server.receive(0U, 42U, *acknowledgement);
      }
    }
  }
  const auto chunk = server.poll(2U, 1U);
  failures += expect(
      chunk.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferChunk>(
              chunk.front().message),
      "server should send a chunk after the begin ACK");
  if (!chunk.empty()) {
    const auto* chunkMessage =
        std::get_if<lg::replay::ReplayTransferChunk>(&chunk.front().message);
    if (chunkMessage != nullptr) {
      const auto acknowledgement = receiver.receiveChunk(*chunkMessage, 2U);
      failures += expect(acknowledgement.has_value(),
                         "receiver should acknowledge the server chunk");
      if (acknowledgement.has_value()) {
        server.receive(0U, 42U, *acknowledgement);
      }
    }
  }
  for (std::uint64_t now = 3U; server.active(0U) && now < 10U; ++now) {
    const auto packets = server.poll(now, 1U);
    if (packets.empty()) continue;
    const auto* nextChunk =
        std::get_if<lg::replay::ReplayTransferChunk>(&packets.front().message);
    if (nextChunk == nullptr) continue;
    const auto acknowledgement = receiver.receiveChunk(*nextChunk, now);
    if (acknowledgement.has_value()) server.receive(0U, 42U, *acknowledgement);
  }
  failures += expect(!server.active(0U),
                     "server should release a completed transfer slot");
  failures += expect(receiver.takeCompleted().has_value(),
                     "receiver should reassemble the server transfer");

  failures += expect(
      !server.start(1U, 43U, 9U, std::vector<std::uint8_t>(4097U), 4U, &error),
      "server should reject a segment over its configured quota");
  failures += expect(server.start(1U, 43U, 9U, {1U}, 4U, &error),
                     "server should accept a later transfer after rejection");
  (void)server.poll(4U, 1U);
  server.cancel(1U, 43U, lg::replay::ReplayTransferCancelReason::Skipped);
  const auto cancel = server.poll(5U, 1U);
  failures += expect(
      cancel.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferCancel>(
              cancel.front().message) &&
          !server.active(1U),
      "server cancel should emit once and release the slot");

  return failures == 0 ? 0 : 1;
}
