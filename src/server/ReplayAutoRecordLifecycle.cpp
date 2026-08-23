#include "server/ReplayAutoRecordLifecycle.hpp"

namespace lg {

ReplayAutoRecordAction replayAutoRecordAction(
  const ReplayAutoRecordState& state
) {
  if (!state.automatic) {
    if (state.enabled && state.phase == MatchPhase::Live &&
        !state.recording && !state.hasStem && !state.savePending &&
        !state.startedThisMatch) {
      return ReplayAutoRecordAction::Start;
    }
    return ReplayAutoRecordAction::None;
  }

  if (!state.recording) return ReplayAutoRecordAction::None;
  if (!state.enabled || state.phase == MatchPhase::MatchEnd ||
      state.phase == MatchPhase::WaitingForPlayers ||
      state.phase == MatchPhase::WaitingForReady) {
    return ReplayAutoRecordAction::Stop;
  }
  return ReplayAutoRecordAction::None;
}

} // namespace lg
