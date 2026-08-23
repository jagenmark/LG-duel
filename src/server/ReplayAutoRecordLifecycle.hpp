#pragma once

#include "net/NetProtocol.hpp"

namespace lg {

enum class ReplayAutoRecordAction {
  None,
  Start,
  Stop,
};

struct ReplayAutoRecordState {
  bool enabled = false;
  bool automatic = false;
  bool recording = false;
  bool hasStem = false;
  bool savePending = false;
  bool startedThisMatch = false;
  MatchPhase phase = MatchPhase::WaitingForPlayers;
};

[[nodiscard]] ReplayAutoRecordAction replayAutoRecordAction(
  const ReplayAutoRecordState& state
);

} // namespace lg
