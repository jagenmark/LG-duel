#pragma once

#include "trainer/AimTrainer.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lg {

struct AimTrainerStoreReply {
  bool ok = false;
  std::string warning;
};

struct AimTrainerPresetList {
  std::vector<AimScenario> presets;
  std::string warning;
};

// Owns only versioned local JSON below the SDL preference root supplied by the
// client app. Storage faults are reported but never alter a completed run.
class AimTrainerStore {
public:
  static constexpr std::uint32_t kStorageVersion = 1;

  explicit AimTrainerStore(std::filesystem::path preferenceRoot);

  [[nodiscard]] AimTrainerPresetList loadPresets() const;
  [[nodiscard]] AimTrainerStoreReply savePreset(const AimScenario& scenario, bool overwrite);
  [[nodiscard]] AimTrainerStoreReply deletePreset(const std::string& name);
  [[nodiscard]] std::vector<AimTrainerResult> leaderboard(
    std::uint64_t scenarioFingerprint,
    std::string* warning = nullptr
  ) const;
  [[nodiscard]] AimTrainerStoreReply recordNaturalResult(const AimTrainerResult& result);
  [[nodiscard]] std::filesystem::path directory() const;

  [[nodiscard]] static std::vector<AimScenario> builtInPresets();

private:
  [[nodiscard]] std::filesystem::path presetsPath() const;
  [[nodiscard]] std::filesystem::path resultsPath() const;

  std::filesystem::path root_;
};

} // namespace lg
