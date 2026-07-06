#pragma once

#include "net/NetProtocol.hpp"

#include <array>
#include <cstdint>

namespace lg {

struct LocalHitFeedbackDedupeState {
  bool hasLastSequence = false;
  std::uint32_t lastSequence = 0;
};

struct LocalHitFeedbackBatch {
  bool active = false;
  bool lightningGunHit = false;
  bool headshotHit = false;
  std::array<bool, kDuelPlayerCount> hitTargets = {};
  std::array<bool, kDuelPlayerCount> headshotTargets = {};
  std::array<int, kDuelPlayerCount> damageByTarget = {};
};

[[nodiscard]] LocalHitFeedbackBatch consumeLocalHitFeedbackEvents(
  const std::array<LocalHitFeedbackEvent, kLocalHitFeedbackEventWindow>& events,
  LocalHitFeedbackDedupeState& state
);

} // namespace lg
