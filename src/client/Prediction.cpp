#include "client/Prediction.hpp"

#include "shared/Math.hpp"
#include "shared/Sequence.hpp"

namespace lg {
namespace {

void applyDeadCommand(PlayerState& player, const UserCommand& command) {
  player.velocity = {};
  player.jumpHeld = false;
  player.dashHeld = false;
  player.dashActiveTicksRemaining = 0;
  player.crouched = false;
  player.sneaking = false;
  player.viewYawRadians = command.viewYawRadians;
  player.viewPitchRadians = command.viewPitchRadians;
}

} // namespace

void Prediction::initialize(const PlayerState& authoritativeState) {
  player_ = authoritativeState;
  pendingCommands_.clear();
  diagnostics_ = {};
  initialized_ = true;
}

void Prediction::predict(
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  const IcePoolArray& icePools,
  const IcePoolTuning& icePoolTuning,
  float fixedDt,
  const PlayerCollisionProxySet& collisionProxies,
  std::uint8_t localPlayerIndex
) {
  if (!initialized_) {
    return;
  }

  pendingCommands_.push_back({command, collisionProxies, localPlayerIndex});
  if (player_.health > 0) {
    simulateMovement(
      player_, command, arena, tuning, icePools, icePoolTuning, fixedDt,
      kDefaultJumpPadCooldownTicks, collisionProxies.span(), localPlayerIndex
    );
  } else {
    applyDeadCommand(player_, command);
  }
  diagnostics_.pendingCommandCount = pendingCommands_.size();
}

void Prediction::predict(
  const UserCommand& command,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt
) {
  predict(
    command, arena, tuning, IcePoolArray{}, IcePoolTuning{}, fixedDt,
    PlayerCollisionProxySet{}, 0
  );
}

void Prediction::reconcile(
  const PlayerState& authoritativeState,
  bool hasAcknowledgedCommand,
  std::uint32_t acknowledgedCommand,
  const Arena& arena,
  const MovementTuning& tuning,
  const IcePoolArray& icePools,
  const IcePoolTuning& icePoolTuning,
  float fixedDt
) {
  if (!initialized_) {
    initialize(authoritativeState);
    return;
  }

  const PlayerState previousPrediction = player_;
  if (hasAcknowledgedCommand) {
    // Drop commands through the wrap-safe server acknowledgement, then rebuild
    // prediction from authority by replaying only inputs the server has not seen.
    while (
      !pendingCommands_.empty() &&
      isSequenceAcknowledged(
        pendingCommands_.front().command.sequence, acknowledgedCommand
      )
    ) {
      pendingCommands_.pop_front();
    }
  }

  player_ = authoritativeState;
  for (const PendingPrediction& pending : pendingCommands_) {
    const UserCommand& command = pending.command;
    if (player_.health > 0) {
      simulateMovement(
        player_, command, arena, tuning, icePools, icePoolTuning, fixedDt,
        kDefaultJumpPadCooldownTicks, pending.collisionProxies.span(),
        pending.localPlayerIndex
      );
    } else {
      applyDeadCommand(player_, command);
    }
  }

  diagnostics_.lastCorrectionDistance = length(player_.position - previousPrediction.position);
  if (diagnostics_.lastCorrectionDistance > 0.0001F) {
    ++diagnostics_.correctionCount;
  }
  diagnostics_.pendingCommandCount = pendingCommands_.size();
}

void Prediction::reconcile(
  const PlayerState& authoritativeState,
  bool hasAcknowledgedCommand,
  std::uint32_t acknowledgedCommand,
  const Arena& arena,
  const MovementTuning& tuning,
  float fixedDt
) {
  reconcile(
    authoritativeState,
    hasAcknowledgedCommand,
    acknowledgedCommand,
    arena,
    tuning,
    IcePoolArray{},
    IcePoolTuning{},
    fixedDt
  );
}

bool Prediction::initialized() const {
  return initialized_;
}

const PlayerState& Prediction::player() const {
  return player_;
}

const PredictionDiagnostics& Prediction::diagnostics() const {
  return diagnostics_;
}

} // namespace lg
