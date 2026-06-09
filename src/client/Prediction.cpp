#include "client/Prediction.hpp"

#include "shared/Math.hpp"
#include "shared/Sequence.hpp"

namespace lg {

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
  float fixedDt
) {
  if (!initialized_) {
    return;
  }

  pendingCommands_.push_back(command);
  if (player_.health > 0) {
    simulateMovement(player_, command, arena, tuning, fixedDt);
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
  if (!initialized_) {
    initialize(authoritativeState);
    return;
  }

  const PlayerState previousPrediction = player_;
  if (hasAcknowledgedCommand) {
    while (
      !pendingCommands_.empty() &&
      isSequenceAcknowledged(pendingCommands_.front().sequence, acknowledgedCommand)
    ) {
      pendingCommands_.pop_front();
    }
  }

  player_ = authoritativeState;
  for (const UserCommand& command : pendingCommands_) {
    if (player_.health > 0) {
      simulateMovement(player_, command, arena, tuning, fixedDt);
    }
  }

  diagnostics_.lastCorrectionDistance = length(player_.position - previousPrediction.position);
  if (diagnostics_.lastCorrectionDistance > 0.0001F) {
    ++diagnostics_.correctionCount;
  }
  diagnostics_.pendingCommandCount = pendingCommands_.size();
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
