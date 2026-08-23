#include "server/ReplayAutoRecordLifecycle.hpp"

#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;
  lg::ReplayAutoRecordState state;
  state.enabled = true;
  state.phase = lg::MatchPhase::WaitingForReady;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::None,
    "auto-record should wait for live play"
  );

  state.phase = lg::MatchPhase::Live;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::Start,
    "auto-record should start once when the match first enters live play"
  );
  state.automatic = true;
  state.recording = true;
  state.hasStem = true;
  state.startedThisMatch = true;

  state.phase = lg::MatchPhase::RoundEnd;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::None,
    "auto-record should keep the same file through round end"
  );
  state.phase = lg::MatchPhase::Countdown;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::None,
    "auto-record should keep the same file through the next countdown"
  );
  state.phase = lg::MatchPhase::Live;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::None,
    "auto-record should not start a second file for the next round"
  );
  state.phase = lg::MatchPhase::MatchEnd;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::Stop,
    "auto-record should stop once at the match boundary"
  );

  state.phase = lg::MatchPhase::Live;
  state.enabled = false;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::Stop,
    "turning auto-record off should stop its active recording"
  );

  state.enabled = true;
  state.automatic = false;
  state.recording = false;
  state.hasStem = false;
  state.startedThisMatch = true;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::None,
    "manual stop should not start a second automatic file in the same match"
  );

  state.startedThisMatch = false;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::Start,
    "the next match should allow one new automatic file"
  );

  state.savePending = true;
  failures += expect(
    lg::replayAutoRecordAction(state) == lg::ReplayAutoRecordAction::None,
    "auto-record should not start while the last match save is pending"
  );
  return failures == 0 ? 0 : 1;
}
