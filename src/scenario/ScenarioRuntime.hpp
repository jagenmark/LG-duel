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
