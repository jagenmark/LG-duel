#include "replay/ClientKillcamCoordinator.hpp"

#include "net/NetProtocol.hpp"
#include "replay/KillcamClientReceiver.hpp"
#include "replay/RemoteKillcamPending.hpp"
#include "replay/ReplayIoService.hpp"
#include "replay/ReplayRuntime.hpp"
#include "sim/WeaponCatalog.hpp"

#include <deque>
#include <utility>

namespace lg::replay {
namespace {

struct RemoteKillcamContext {
  std::uint32_t sessionId = 0U;
  std::uint32_t mapRevision = 0U;
  std::uint32_t mapContentHash = 0U;
  std::uint32_t generation = 0U;
  std::uint32_t lethalSequence = 0U;
  std::uint8_t localPlayerIndex = kNoAssignedPlayer;
  MatchPhase matchPhase = MatchPhase::WaitingForPlayers;
  std::uint8_t roundWinner = 255U;
  std::uint8_t matchWinner = 255U;
  Team roundWinningTeam = Team::None;
  Team matchWinningTeam = Team::None;
  bool localWasDead = false;
  bool localWasRespawning = false;
  bool deathSnapshotBound = false;
};

RemoteKillcamIdentity identityFor(const RemoteKillcamContext& context) {
  return {
    context.sessionId,
    context.generation,
    context.lethalSequence,
    context.localPlayerIndex,
  };
}

bool hasLocalBody(
  const ClientKillcamLiveView& live,
  std::uint8_t expectedPlayer
) {
  return live.connected && !live.spectator && live.sessionId != 0U &&
    live.playerIndex < kDuelPlayerCount && live.snapshot != nullptr &&
    live.snapshot->hasLocalClientState && !live.snapshot->localSpectator &&
    live.snapshot->localPlayerIndex == live.playerIndex &&
    expectedPlayer == live.playerIndex;
}

void bindDeathSnapshot(
  RemoteKillcamContext& context,
  const ClientKillcamLiveView& live
) {
  if (context.deathSnapshotBound ||
      live.sessionId != context.sessionId ||
      !hasLocalBody(live, context.localPlayerIndex)) {
    return;
  }
  const ServerSnapshot& snapshot = *live.snapshot;
  const std::size_t player = live.playerIndex;
  const bool dead = snapshot.players[player].health <= 0;
  const bool respawning = snapshot.respawnTicksRemaining[player] > 0U;
  if (!dead && !respawning) return;

  context.mapRevision = snapshot.mapRevision;
  context.mapContentHash = snapshot.map.contentHash;
  context.matchPhase = snapshot.matchPhase;
  context.roundWinner = snapshot.roundWinner;
  context.matchWinner = snapshot.matchWinner;
  context.roundWinningTeam = snapshot.roundWinningTeam;
  context.matchWinningTeam = snapshot.matchWinningTeam;
  context.localWasDead = dead;
  context.localWasRespawning = respawning;
  context.deathSnapshotBound = true;
}

bool liveStateMatches(
  const RemoteKillcamContext& context,
  const ClientKillcamLiveView& live
) {
  if (live.sessionId != context.sessionId ||
      !context.deathSnapshotBound ||
      !hasLocalBody(live, context.localPlayerIndex)) {
    return false;
  }
  const ServerSnapshot& snapshot = *live.snapshot;
  const std::size_t player = live.playerIndex;
  return snapshot.gameMode == GameMode::Duel &&
    snapshot.mapRevision == context.mapRevision &&
    snapshot.map.contentHash == context.mapContentHash &&
    snapshot.matchPhase == context.matchPhase &&
    snapshot.roundWinner == context.roundWinner &&
    snapshot.matchWinner == context.matchWinner &&
    snapshot.roundWinningTeam == context.roundWinningTeam &&
    snapshot.matchWinningTeam == context.matchWinningTeam &&
    (snapshot.players[player].health <= 0) == context.localWasDead &&
    (snapshot.respawnTicksRemaining[player] > 0U) ==
      context.localWasRespawning;
}

} // namespace

struct ClientKillcamCoordinator::Impl {
  explicit Impl(std::filesystem::path maps)
      : mapDirectory(std::move(maps)) {}

  std::filesystem::path mapDirectory;
  KillcamClientReceiver receiver;
  ReplayIoService io;
  std::optional<ReplayIoService::JobId> pendingDecode;
  std::optional<RemoteKillcamIdentity> pendingDecodeIdentity;
  PendingRemoteKillcamPlayback pendingPlayback;
  std::optional<RemoteKillcamContext> context;
  std::unique_ptr<ReplayRuntime> runtime;
  std::deque<ReplayTransferMessage> outbound;
  std::deque<std::string> messages;
  bool presentationReset = false;

  [[nodiscard]] bool flowActive() const {
    return receiver.active() || pendingDecode.has_value() ||
      pendingPlayback.hasDecoded() || context.has_value() ||
      (runtime != nullptr && runtime->started());
  }

  void stopRuntime() {
    if (runtime != nullptr && runtime->started()) runtime->stop();
    if (runtime != nullptr) {
      runtime.reset();
      presentationReset = true;
    }
  }

  void clearPending() {
    pendingDecode.reset();
    pendingDecodeIdentity.reset();
    pendingPlayback.reset();
    context.reset();
  }

  void clearAll() {
    stopRuntime();
    clearPending();
    receiver.reset();
  }

  void makeContext(
    const KillcamClientReceiverStatus& transfer,
    const ClientKillcamLiveView& live
  ) {
    stopRuntime();
    pendingDecode.reset();
    pendingDecodeIdentity.reset();
    pendingPlayback.reset();
    RemoteKillcamContext next;
    next.sessionId = transfer.sessionId;
    next.generation = transfer.generation;
    next.lethalSequence = transfer.lethalSequence;
    next.localPlayerIndex = live.playerIndex < kDuelPlayerCount
      ? static_cast<std::uint8_t>(live.playerIndex)
      : kNoAssignedPlayer;
    bindDeathSnapshot(next, live);
    context = std::move(next);
    pendingPlayback.begin(identityFor(*context));
  }

  void queueCompletedDecode() {
    std::optional<std::vector<std::uint8_t>> bytes = receiver.takeCompleted();
    if (!bytes.has_value()) return;
    if (!context.has_value()) {
      const KillcamClientReceiverStatus transfer = receiver.status();
      RemoteKillcamContext next;
      next.sessionId = transfer.sessionId;
      next.generation = transfer.generation;
      next.lethalSequence = transfer.lethalSequence;
      context = std::move(next);
    }
    const RemoteKillcamIdentity identity = identityFor(*context);
    pendingPlayback.begin(identity);
    if (!identity.valid()) {
      messages.emplace_back("killcam rejected: transfer identity is invalid");
      clearPending();
      return;
    }
    if (pendingDecode.has_value()) {
      messages.emplace_back("killcam skipped: replay decode is already pending");
      return;
    }

    ReplayIoService::JobId job = 0U;
    std::string error;
    if (!io.enqueueDecode(
          std::move(*bytes),
          job,
          &error,
          kRemoteKillcamMaxDecodedResidentBytes,
          kRemoteKillcamMaxDecodedTicks
        )) {
      messages.emplace_back("killcam decode rejected: " + error);
      clearPending();
      return;
    }
    pendingDecode = job;
    pendingDecodeIdentity = identity;
  }

  void handleIoResult(
    ReplayIoService::Result& result,
    std::uint64_t nowMilliseconds
  ) {
    if (result.id != pendingDecode.value_or(0U)) return;
    const std::optional<RemoteKillcamIdentity> decodeIdentity =
      pendingDecodeIdentity;
    pendingDecode.reset();
    pendingDecodeIdentity.reset();
    const auto clearMatching = [&]() {
      if (decodeIdentity.has_value() && context.has_value() &&
          identityFor(*context) == *decodeIdentity) {
        pendingPlayback.reset();
        context.reset();
      }
    };

    if (!result.ok || !result.demo.has_value()) {
      messages.emplace_back("killcam decode failed: " + result.error);
      clearMatching();
      return;
    }
    if (!decodeIdentity.has_value() || !decodeIdentity->valid()) {
      messages.emplace_back("killcam rejected: missing transfer identity");
      clearMatching();
      return;
    }

    std::uint8_t followSlot = kNoReplayPlayer;
    std::string error;
    if (!validateRemoteKillcamPlayback(
          *result.demo,
          decodeIdentity->victim,
          decodeIdentity->generation,
          decodeIdentity->lethalSequence,
          followSlot,
          &error
        )) {
      messages.emplace_back("killcam rejected: " + error);
      clearMatching();
      return;
    }
    if (!pendingPlayback.storeDecoded(
          *decodeIdentity,
          std::move(*result.demo),
          followSlot,
          nowMilliseconds
        )) {
      messages.emplace_back("killcam rejected: stale decoded transfer");
    }
  }

  void tryStart(
    const ClientKillcamLiveView& live,
    std::uint64_t nowMilliseconds
  ) {
    if (pendingPlayback.discardIfExpired(nowMilliseconds)) {
      context.reset();
      messages.emplace_back(
        "killcam rejected: authoritative death snapshot did not arrive"
      );
      return;
    }
    if (!context.has_value()) return;
    bindDeathSnapshot(*context, live);
    const RemoteKillcamIdentity identity = identityFor(*context);
    std::optional<RemoteKillcamDecodedPlayback> decoded =
      pendingPlayback.takeReady(
        identity,
        context->deathSnapshotBound,
        nowMilliseconds
      );
    if (!decoded.has_value()) return;

    std::optional<ReplayLethalEvent> lethal;
    for (const ReplayLethalEvent& event : decoded->demo.lethalEvents) {
      if (event.victim == identity.victim &&
          event.replayGeneration == identity.generation &&
          event.sequence == identity.lethalSequence) {
        lethal = event;
        break;
      }
    }
    const ServerSnapshot* snapshot = live.snapshot;
    const bool trusted = liveStateMatches(*context, live) &&
      snapshot != nullptr && lethal.has_value() && context->mapRevision != 0U &&
      decoded->demo.metadata.mapRevision == context->mapRevision &&
      decoded->demo.metadata.mapName == snapshot->map.mapName &&
      decoded->demo.metadata.mapContentHash == snapshot->map.contentHash &&
      decoded->demo.metadata.mapContentHash == context->mapContentHash &&
      permitsRemoteKillcam(decoded->demo.metadata, false);
    if (!trusted) {
      messages.emplace_back(
        "killcam rejected: stale, cross-match, or unauthorized replay"
      );
      context.reset();
      return;
    }

    ReplayRuntimeConfig config;
    config.mapDirectory = mapDirectory.string();
    config.autoplay = true;
    config.initialFollowSlot = decoded->followSlot;
    auto candidate = std::make_unique<ReplayRuntime>(
      std::move(decoded->demo),
      std::move(config)
    );
    std::string error;
    if (!candidate->start(&error)) {
      messages.emplace_back("killcam playback failed: " + error);
      context.reset();
      return;
    }
    runtime = std::move(candidate);
    presentationReset = true;
    messages.emplace_back("killcam playback started");
  }
};

ClientKillcamCoordinator::ClientKillcamCoordinator(
  std::filesystem::path mapDirectory
) : impl_(std::make_unique<Impl>(std::move(mapDirectory))) {}

ClientKillcamCoordinator::~ClientKillcamCoordinator() = default;

void ClientKillcamCoordinator::receiveTransfer(
  const ReplayTransferMessage& message,
  const ClientKillcamLiveView& live,
  std::uint64_t nowMilliseconds
) {
  if (!live.connected || live.sessionId == 0U) return;
  if (impl_->receiver.boundSession() != live.sessionId) {
    if (impl_->flowActive()) impl_->clearAll();
    impl_->receiver.bindSession(live.sessionId);
  }
  const KillcamClientReceiverStatus transfer = impl_->receiver.status();
  const auto* cancel = std::get_if<ReplayTransferCancel>(&message);
  const bool matchingServerCancel = cancel != nullptr &&
    cancel->reason != ReplayTransferCancelReason::None &&
    impl_->context.has_value() && transfer.transferId != 0U &&
    cancel->transferId == transfer.transferId &&
    cancel->generation == transfer.generation &&
    cancel->sessionId == transfer.sessionId &&
    impl_->context->sessionId == transfer.sessionId &&
    impl_->context->generation == transfer.generation &&
    impl_->context->lethalSequence == transfer.lethalSequence;
  const bool transferWasActive = impl_->receiver.active();
  if (const auto response = impl_->receiver.receive(message, nowMilliseconds);
      response.has_value()) {
    impl_->outbound.push_back(*response);
  }
  if (matchingServerCancel) {
    impl_->clearAll();
    impl_->receiver.bindSession(live.sessionId);
    impl_->messages.emplace_back("killcam transfer canceled by server");
    return;
  }
  if (impl_->receiver.failed()) {
    impl_->clearAll();
    impl_->receiver.bindSession(live.sessionId);
    impl_->messages.emplace_back("killcam transfer rejected");
    return;
  }
  if (std::holds_alternative<ReplayTransferBegin>(message) &&
      impl_->receiver.active()) {
    impl_->makeContext(impl_->receiver.status(), live);
  }
  impl_->queueCompletedDecode();
  if (transferWasActive && !impl_->receiver.active() &&
      !impl_->pendingDecode.has_value() && impl_->context.has_value()) {
    impl_->clearPending();
    impl_->messages.emplace_back("killcam transfer rejected");
  }
}

void ClientKillcamCoordinator::update(
  const ClientKillcamLiveView& live,
  std::uint64_t nowMilliseconds,
  double elapsedSeconds
) {
  if (!live.connected || live.sessionId == 0U) {
    if (impl_->flowActive()) impl_->clearAll();
    return;
  }
  if (impl_->receiver.boundSession() != live.sessionId) {
    if (impl_->flowActive()) impl_->clearAll();
    impl_->receiver.bindSession(live.sessionId);
  }
  while (std::optional<ReplayIoService::Result> result = impl_->io.poll()) {
    impl_->handleIoResult(*result, nowMilliseconds);
  }
  if (const auto response = impl_->receiver.update(nowMilliseconds);
      response.has_value()) {
    impl_->outbound.push_back(*response);
    impl_->clearPending();
  }
  if (impl_->context.has_value()) {
    bindDeathSnapshot(*impl_->context, live);
  }
  impl_->tryStart(live, nowMilliseconds);

  if (impl_->runtime == nullptr || !impl_->runtime->active()) return;
  if (!impl_->context.has_value() ||
      !liveStateMatches(*impl_->context, live)) {
    impl_->stopRuntime();
    impl_->context.reset();
    impl_->messages.emplace_back(
      "killcam stopped: live session or body changed"
    );
    return;
  }

  std::string error;
  if (!impl_->runtime->advance(elapsedSeconds, &error)) {
    impl_->messages.emplace_back(
      "killcam playback stopped: " +
      (error.empty() ? impl_->runtime->lastError() : error)
    );
  }
  if (!impl_->runtime->active()) {
    impl_->stopRuntime();
    impl_->context.reset();
  }
}

bool ClientKillcamCoordinator::skip() {
  const bool hadRemote = impl_->flowActive();
  if (const auto response = impl_->receiver.cancel(
        ReplayTransferCancelReason::Skipped
      ); response.has_value()) {
    impl_->outbound.push_back(*response);
  }
  impl_->clearAll();
  return hadRemote;
}

bool ClientKillcamCoordinator::commandAllowed(
  ClientReplayCommand command
) const {
  return !impl_->flowActive() || command == ClientReplayCommand::KillcamSkip;
}

ClientKillcamStatus ClientKillcamCoordinator::status() const {
  return {
    impl_->runtime != nullptr && impl_->runtime->started(),
    impl_->receiver.active(),
    impl_->pendingDecode.has_value(),
    impl_->context.has_value(),
  };
}

ClientKillcamHud ClientKillcamCoordinator::hud() const {
  ClientKillcamHud result;
  if (impl_->runtime == nullptr || !impl_->runtime->started() ||
      !impl_->context.has_value()) {
    return result;
  }
  const RemoteKillcamIdentity identity = identityFor(*impl_->context);
  const ReplayLethalEvent* lethal = nullptr;
  for (const ReplayLethalEvent& event : impl_->runtime->demo().lethalEvents) {
    if (event.replayGeneration == identity.generation &&
        event.sequence == identity.lethalSequence &&
        event.victim == identity.victim) {
      lethal = &event;
      break;
    }
  }
  if (lethal == nullptr) return result;

  result.active = true;
  result.killer = "WORLD";
  if (lethal->killer < kDuelPlayerCount && lethal->killer != lethal->victim) {
    result.killer = impl_->runtime->snapshot().playerNames[lethal->killer];
  } else if (lethal->kind == LethalKind::Self) {
    result.killer = "SELF";
  }
  result.weapon = std::string(weaponShortName(lethal->weapon));
  result.cause = "DIRECT";
  switch (lethal->kind) {
  case LethalKind::Splash:
    result.cause = "SPLASH DAMAGE";
    break;
  case LethalKind::Self:
    result.cause = "SELF KILL";
    break;
  case LethalKind::World:
    result.cause = "WORLD / ENVIRONMENT";
    break;
  case LethalKind::Direct:
    break;
  }
  result.progress = impl_->runtime->state().progress;
  return result;
}

ReplayRuntime* ClientKillcamCoordinator::runtime() {
  return impl_->runtime.get();
}

const ReplayRuntime* ClientKillcamCoordinator::runtime() const {
  return impl_->runtime.get();
}

std::optional<ReplayTransferMessage> ClientKillcamCoordinator::takeOutbound() {
  if (impl_->outbound.empty()) return std::nullopt;
  ReplayTransferMessage result = std::move(impl_->outbound.front());
  impl_->outbound.pop_front();
  return result;
}

std::optional<std::string> ClientKillcamCoordinator::takeMessage() {
  if (impl_->messages.empty()) return std::nullopt;
  std::string result = std::move(impl_->messages.front());
  impl_->messages.pop_front();
  return result;
}

bool ClientKillcamCoordinator::takePresentationReset() {
  const bool result = impl_->presentationReset;
  impl_->presentationReset = false;
  return result;
}

} // namespace lg::replay
