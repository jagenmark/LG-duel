#pragma once

#include "scenario/ScenarioEvidence.hpp"
#include "scenario/ScenarioState.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lg::scenario {

inline constexpr std::size_t kMaxScenarioJournalEntries = 100000U;

struct ScenarioRunOptions {
  std::filesystem::path mapsDirectory = "maps";
  std::optional<std::uint32_t> repeat;
};

struct ScenarioRunResult {
  ScenarioEvidence evidence;
  bool passed = false;
  std::string error;
};

// Both headless and live runs use the same typed conversion before the
// authoritative server applies scenario state.
[[nodiscard]] ScenarioSetup scenarioSetup(const ScenarioDefinition& scenario);

class ScenarioEventJournal {
public:
  [[nodiscard]] bool observe(
    std::uint32_t run,
    std::uint32_t tick,
    const ScenarioState& before,
    const ScenarioState& after,
    const ServerSnapshot& snapshot,
    std::string& error
  );

  [[nodiscard]] const std::vector<EventEvidence>& events() const;

private:
  std::vector<EventEvidence> events_;
  std::vector<EventEvidence> priorTickEvents_;
  std::uint64_t sequence_ = 0;
};

[[nodiscard]] AssertionEvidence evaluateScenarioAssertion(
  std::size_t index,
  std::uint32_t run,
  std::uint32_t tick,
  const ScenarioAssertion& assertion,
  const ScenarioState& state,
  std::span<const EventEvidence> events
);

[[nodiscard]] std::string scenarioStateHash(const ScenarioState& state);
[[nodiscard]] dev::JsonValue scenarioStateJson(const ScenarioState& state);
[[nodiscard]] std::optional<std::uint32_t> firstDivergentTick(
  std::span<const std::string> reference,
  std::span<const std::string> candidate
);
[[nodiscard]] ScenarioRunResult runScenario(
  const ScenarioDefinition& scenario,
  const ScenarioRunOptions& options = {}
);

} // namespace lg::scenario
