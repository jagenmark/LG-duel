#pragma once

#include "sim/Arena.hpp"
#include "sim/Movement.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace lg {

struct PredictionDiagnostics {
  float lastCorrectionDistance = 0.0F;
  std::uint32_t correctionCount = 0;
  std::size_t pendingCommandCount = 0;
};

class Prediction {
public:
  void initialize(const PlayerState& authoritativeState);
  void predict(
    const UserCommand& command,
    const Arena& arena,
    const MovementTuning& tuning,
    float fixedDt
  );
  void reconcile(
    const PlayerState& authoritativeState,
    bool hasAcknowledgedCommand,
    std::uint32_t acknowledgedCommand,
    const Arena& arena,
    const MovementTuning& tuning,
    float fixedDt
  );

  [[nodiscard]] bool initialized() const;
  [[nodiscard]] const PlayerState& player() const;
  [[nodiscard]] const PredictionDiagnostics& diagnostics() const;

private:
  PlayerState player_ = {};
  std::deque<UserCommand> pendingCommands_;
  PredictionDiagnostics diagnostics_ = {};
  bool initialized_ = false;
};

} // namespace lg
