#pragma once

#include "scenario/ScenarioRuntime.hpp"
#include "scenario/ScenarioSchema.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

class ServerGame;

namespace scenario {

struct LiveScenarioOptions {
  std::filesystem::path scenarioPath;
  std::filesystem::path runDirectory;
  std::string token;
};

struct LiveScenarioFinishRequest {
  std::uint32_t minimumCommandSequence = 0;
};

enum class LiveScenarioUpdate {
  WaitingForRoster,
  WaitingForStart,
  Running,
  Complete,
  Failed,
};

[[nodiscard]] bool isSafeLiveScenarioToken(std::string_view token);
[[nodiscard]] bool validateLiveScenarioOptions(
  const LiveScenarioOptions& options,
  std::string& error
);
[[nodiscard]] bool parseLiveScenarioFinishRequest(
  std::string_view json,
  std::string_view expectedToken,
  LiveScenarioFinishRequest& request,
  std::string& error
);

// This session exists only when the server starts with the full live-scenario
// option set. The normal server does not capture state, derive events, or touch
// scenario files.
class LiveScenarioSession {
public:
  [[nodiscard]] static std::optional<LiveScenarioSession> load(
    LiveScenarioOptions options,
    std::string& error
  );

  LiveScenarioSession(
    LiveScenarioOptions options,
    ScenarioDefinition scenario
  );

  [[nodiscard]] LiveScenarioUpdate beforeServerTick(
    ServerGame& game,
    const std::array<bool, kDuelPlayerCount>& connectedPlayers,
    const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
  );
  [[nodiscard]] LiveScenarioUpdate afterServerTick(ServerGame& game);

  [[nodiscard]] bool started() const;
  [[nodiscard]] bool complete() const;
  [[nodiscard]] bool failed() const;
  [[nodiscard]] std::uint32_t relativeTick() const;
  [[nodiscard]] std::size_t realPlayerSlot() const;
  [[nodiscard]] const std::string& error() const;

private:
  [[nodiscard]] bool validateScenario();
  [[nodiscard]] bool begin(
    ServerGame& game,
    const std::array<bool, kDuelPlayerCount>& connectedPlayers,
    const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
  );
  [[nodiscard]] bool pollStartRequest(
    ServerGame& game,
    const std::array<bool, kDuelPlayerCount>& connectedPlayers,
    const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
  );
  [[nodiscard]] bool applySetup(
    ServerGame& game,
    const std::array<bool, kDuelPlayerCount>& connectedPlayers,
    const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
  );
  [[nodiscard]] bool writeReady(const ScenarioState& state);
  [[nodiscard]] bool writeCheckpoint(
    const ScenarioState& state,
    const ServerSnapshot& snapshot
  );
  [[nodiscard]] bool writeResult(
    const ScenarioState& state,
    const ServerSnapshot& snapshot,
    std::string_view reason,
    bool passed
  );
  [[nodiscard]] bool pollFinishRequest(
    const ScenarioState& state,
    const ServerSnapshot& snapshot
  );
  void evaluateAuthoritativeAssertions(
    const ScenarioState& state,
    bool completion
  );
  void observeConsumedEdges(const ScenarioState& state);
  [[nodiscard]] bool writeImmutableJson(
    const std::filesystem::path& path,
    const dev::JsonValue& value
  );
  void fail(std::string message);

  LiveScenarioOptions options_;
  ScenarioDefinition scenario_;
  ScenarioEventJournal events_;
  std::vector<AssertionEvidence> assertions_;
  std::vector<bool> assertionEvaluated_;
  std::array<std::uint32_t, 6> consumedEdgeCounts_ = {};
  ScenarioState priorState_;
  std::optional<LiveScenarioFinishRequest> finishRequest_;
  std::size_t realPlayerSlot_ = kDuelPlayerCount;
  std::uint32_t relativeTick_ = 0;
  std::uint32_t startReadFailures_ = 0;
  std::uint32_t finishReadFailures_ = 0;
  bool valid_ = false;
  bool started_ = false;
  bool armed_ = false;
  bool complete_ = false;
  bool failed_ = false;
  bool completionAssertionsEvaluated_ = false;
  std::string error_;
};

} // namespace scenario
} // namespace lg
