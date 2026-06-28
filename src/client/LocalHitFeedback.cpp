#include "client/LocalHitFeedback.hpp"

#include "shared/Sequence.hpp"

#include <algorithm>

namespace lg {

LocalHitFeedbackBatch consumeLocalHitFeedbackEvents(
  const std::array<LocalHitFeedbackEvent, kLocalHitFeedbackEventWindow>& events,
  LocalHitFeedbackDedupeState& state
) {
  LocalHitFeedbackBatch batch;
  std::uint32_t newestSequence = state.lastSequence;
  for (const LocalHitFeedbackEvent& event : events) {
    if (
      !event.active ||
      (
        state.hasLastSequence &&
        !isSequenceNewer(event.sequence, state.lastSequence)
      )
    ) {
      continue;
    }
    batch.active = true;
    batch.lightningGunHit =
      batch.lightningGunHit || event.weapon == Weapon::LightningGun;
    if (event.targetPlayerIndex < kDuelPlayerCount) {
      batch.hitTargets[event.targetPlayerIndex] = true;
    }
    if (!state.hasLastSequence || isSequenceNewer(event.sequence, newestSequence)) {
      newestSequence = event.sequence;
    }
  }
  if (batch.active) {
    state.lastSequence = newestSequence;
    state.hasLastSequence = true;
  }
  return batch;
}

} // namespace lg
