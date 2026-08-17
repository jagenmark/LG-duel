#include "replay/KillcamServerCoordinator.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace lg::replay {
namespace {

constexpr std::size_t kMaxPendingEvents = 64U;

std::uint32_t readyTickFor(const ReplayLethalEvent& event,
                          std::uint32_t afterTicks) {
  if (afterTicks > std::numeric_limits<std::uint32_t>::max() - event.tick) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return event.tick + afterTicks;
}

void setError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

} // namespace

KillcamServerCoordinator::KillcamServerCoordinator(
    ServerGame& server,
    UdpServerTransport& transport)
    : server_(server),
      transport_(transport),
      io_(ReplayIoService::Config{1U}),
      transfers_(ReplayTransferServerConfig{}) {}

KillcamServerCoordinator::~KillcamServerCoordinator() { shutdown(); }

bool KillcamServerCoordinator::sameRollingConfig(
    const ReplayRollingBufferConfig& left,
    const ReplayRollingBufferConfig& right) const {
  return left.enabled == right.enabled &&
         left.retainedTicks == right.retainedTicks &&
         left.checkpointIntervalTicks == right.checkpointIntervalTicks &&
         left.hashIntervalTicks == right.hashIntervalTicks &&
         left.maximumBytes == right.maximumBytes;
}

bool KillcamServerCoordinator::configure(
    KillcamServerCoordinatorConfig config,
    std::string* error) {
  if (stopped_) {
    setError(error, "killcam coordinator is stopped");
    return false;
  }
  config.beforeTicks = std::clamp(config.beforeTicks, 1U, 3750U);
  config.afterTicks = std::min(config.afterTicks, 1250U);
  config.transferTimeoutMilliseconds =
      std::clamp(config.transferTimeoutMilliseconds, 100U, 30000U);
  config.maximumSegmentBytes = std::clamp<std::size_t>(
      config.maximumSegmentBytes, 1U, kReplayTransferMaxSegmentBytes);
  config.packetsPerTick = std::clamp<std::size_t>(config.packetsPerTick, 1U, 64U);
  config.rolling.enabled = config.enabled;
  config.rolling.retainedTicks = std::max(
      config.rolling.retainedTicks,
      config.beforeTicks + config.afterTicks + 250U);
  config.rolling.retainedTicks = std::min(config.rolling.retainedTicks, 10000U);
  config.rolling.maximumBytes = std::clamp<std::size_t>(
      config.rolling.maximumBytes, config.maximumSegmentBytes, 64U * 1024U * 1024U);

  const bool rollingChanged =
      !configured_ || config.enabled != config_.enabled ||
      !sameRollingConfig(config.rolling, config_.rolling);
  if (!config.enabled) {
    if (configured_ && config_.enabled) server_.endRollingReplay();
    pendingEvents_.clear();
    pendingEncodes_.clear();
    transfers_.clear();
    config_ = config;
    configured_ = true;
    if (error != nullptr) error->clear();
    return true;
  }

  if (rollingChanged) {
    if (server_.rollingReplayStats().enabled) server_.endRollingReplay();
    std::string rollingError;
    if (!server_.beginRollingReplay(config.rolling, &rollingError)) {
      setError(error, rollingError);
      return false;
    }
    pendingEvents_.clear();
    pendingEncodes_.clear();
    transfers_.clear();
  }
  if (!configured_ || config.transferTimeoutMilliseconds !=
                           config_.transferTimeoutMilliseconds ||
      config.maximumSegmentBytes != config_.maximumSegmentBytes) {
    ReplayTransferServerConfig transferConfig;
    transferConfig.maximumSegmentBytes = config.maximumSegmentBytes;
    transferConfig.transfer.timeoutMilliseconds =
        config.transferTimeoutMilliseconds;
    transferConfig.transfer.retryMilliseconds = 100U;
    transferConfig.transfer.minimumPacketIntervalMilliseconds = 2U;
    transfers_.configure(transferConfig);
  }
  config_ = config;
  configured_ = true;
  observedReplayGeneration_ = server_.replayGeneration();
  observedMapRevision_ = server_.snapshot().mapRevision;
  if (error != nullptr) error->clear();
  return true;
}

bool KillcamServerCoordinator::currentClientMatches(
    const PendingEvent& pending) const {
  const auto client = transport_.clientIndexForPlayer(pending.event.victim);
  return client.has_value() && *client == pending.clientIndex &&
         pending.event.replayGeneration == server_.replayGeneration() &&
         pending.mapRevision == server_.snapshot().mapRevision &&
         transport_.clientSession(pending.clientIndex) == pending.sessionId;
}

void KillcamServerCoordinator::invalidateStaleReplayState() {
  const std::uint32_t generation = server_.replayGeneration();
  const std::uint32_t mapRevision = server_.snapshot().mapRevision;
  if (observedReplayGeneration_ == 0U) {
    observedReplayGeneration_ = generation;
    observedMapRevision_ = mapRevision;
    return;
  }
  if (generation == observedReplayGeneration_ &&
      mapRevision == observedMapRevision_) {
    return;
  }

  // Dropping the job records makes any worker result stale. The bounded worker
  // may finish its current encode, but drainCompletedEncodes() cannot start it.
  pendingEvents_.clear();
  pendingEncodes_.clear();
  transfers_.clear();
  observedReplayGeneration_ = generation;
  observedMapRevision_ = mapRevision;
}

void KillcamServerCoordinator::drainCompletedEncodes(
    std::uint64_t nowMilliseconds) {
  while (const auto result = io_.poll()) {
    if (result->kind != ReplayIoService::JobKind::Encode) continue;
    const auto pending = pendingEncodes_.find(result->id);
    if (pending == pendingEncodes_.end()) continue;
    const PendingEncode request = pending->second;
    pendingEncodes_.erase(pending);
    if (!result->ok || result->bytes.empty() ||
        result->bytes.size() > config_.maximumSegmentBytes ||
        request.event.replayGeneration != server_.replayGeneration() ||
        request.mapRevision != server_.snapshot().mapRevision ||
        transport_.clientSession(request.clientIndex) != request.sessionId ||
        !server_.snapshot().connectedPlayers[request.event.victim] ||
        transport_.clientIndexForPlayer(request.event.victim) !=
            std::optional<std::uint8_t>(request.clientIndex)) {
      ++stats_.rejectedSegments;
      continue;
    }
    std::string error;
    if (transfers_.start(request.clientIndex, request.sessionId,
                         request.event.replayGeneration, result->bytes,
                         nowMilliseconds, &error, request.event.sequence)) {
      ++stats_.encodedSegments;
    } else {
      ++stats_.rejectedSegments;
    }
  }
}

void KillcamServerCoordinator::drainLethalEvents() {
  const std::vector<ReplayLethalEvent> events = server_.takeReplayLethalEvents();
  for (const ReplayLethalEvent& event : events) {
    if (!config_.enabled || server_.snapshot().gameMode != GameMode::Duel ||
        event.victim >= kDuelPlayerCount ||
        server_.isBotSlot(event.victim)) {
      ++stats_.skippedEvents;
      continue;
    }
    const auto client = transport_.clientIndexForPlayer(event.victim);
    if (!client.has_value()) {
      ++stats_.skippedEvents;
      continue;
    }
    const std::uint32_t session = transport_.clientSession(*client);
    if (session == 0U || server_.rollingReplayStats().generation !=
                             event.replayGeneration ||
        event.replayGeneration != server_.replayGeneration()) {
      ++stats_.skippedEvents;
      continue;
    }
    if (pendingEvents_.size() >= kMaxPendingEvents) pendingEvents_.pop_front();
    pendingEvents_.push_back({event, *client, session,
                              server_.snapshot().mapRevision,
                              readyTickFor(event, config_.afterTicks)});
    ++stats_.acceptedEvents;
  }
}

void KillcamServerCoordinator::startReadyEncodes() {
  const std::uint32_t currentTick = server_.snapshot().serverTick;
  for (auto iterator = pendingEvents_.begin();
       iterator != pendingEvents_.end();) {
    if (currentTick < iterator->readyTick) {
      ++iterator;
      continue;
    }
    PendingEvent pending = *iterator;
    iterator = pendingEvents_.erase(iterator);
    if (!currentClientMatches(pending)) {
      ++stats_.skippedEvents;
      continue;
    }
    std::string error;
    auto segment = server_.extractRollingReplaySegment(
        pending.event, config_.beforeTicks, config_.afterTicks, &error);
    if (!segment.has_value() ||
        !permitsRemoteKillcam(segment->metadata, false)) {
      ++stats_.skippedEvents;
      continue;
    }
    ReplayIoService::JobId job = 0;
    if (!io_.enqueueEncode(std::move(*segment), config_.maximumSegmentBytes,
                           job, &error)) {
      ++stats_.rejectedSegments;
      continue;
    }
    pendingEncodes_.emplace(job, PendingEncode{pending.clientIndex,
                                                pending.sessionId,
                                                pending.mapRevision,
                                                pending.event});
  }
}

void KillcamServerCoordinator::processClientMessages() {
  std::uint8_t clientIndex = 0U;
  ReplayTransferMessage message;
  while (transport_.receiveReplayTransferMessage(clientIndex, message)) {
    transfers_.receive(clientIndex, transport_.clientSession(clientIndex),
                       message);
  }
}

void KillcamServerCoordinator::expireDisconnectedTransfers() {
  for (std::size_t index = 0U; index < kMaxNetworkClients; ++index) {
    const auto current = transfers_.status(static_cast<std::uint8_t>(index));
    if (!current.has_value()) continue;
    const std::uint8_t clientIndex = static_cast<std::uint8_t>(index);
    const std::uint32_t sessionId = transport_.clientSession(clientIndex);
    if (sessionId != current->sessionId) {
      transfers_.clearClient(static_cast<std::uint8_t>(index));
    } else if (current->generation != server_.replayGeneration()) {
      // Keep the slot until poll() sends the typed cancellation. A silent
      // drop makes the receiver wait for its timeout after a reset.
      transfers_.cancel(clientIndex, current->sessionId,
                        ReplayTransferCancelReason::Invalid);
    }
  }
}

void KillcamServerCoordinator::sendDuePackets(std::uint64_t now) {
  const auto packets = transfers_.poll(now, config_.packetsPerTick);
  for (const ReplayTransferOutbound& packet : packets) {
    if (transport_.sendReplayTransferMessage(packet.clientIndex,
                                             packet.message)) {
      ++stats_.sentPackets;
    }
  }
}

void KillcamServerCoordinator::update(std::uint64_t now) {
  if (stopped_ || !configured_) return;
  invalidateStaleReplayState();
  drainCompletedEncodes(now);
  processClientMessages();
  expireDisconnectedTransfers();
  if (!config_.enabled) {
    (void)server_.takeReplayLethalEvents();
    sendDuePackets(now);
    return;
  }
  drainLethalEvents();
  startReadyEncodes();
  drainCompletedEncodes(now);
  sendDuePackets(now);
}

void KillcamServerCoordinator::shutdown() {
  if (stopped_) return;
  server_.endRollingReplay();
  pendingEvents_.clear();
  pendingEncodes_.clear();
  transfers_.clear();
  io_.shutdown();
  stopped_ = true;
}

KillcamServerCoordinatorStats KillcamServerCoordinator::stats() const {
  KillcamServerCoordinatorStats result = stats_;
  result.pendingEvents = pendingEvents_.size();
  result.pendingEncodes = pendingEncodes_.size();
  result.activeTransfers = transfers_.activeCount();
  return result;
}

} // namespace lg::replay
