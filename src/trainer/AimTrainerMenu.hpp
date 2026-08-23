#pragma once

#include "trainer/AimTrainer.hpp"
#include "trainer/AimTrainerStore.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace lg {

// GUI-facing state and commands. A renderer may draw this model with any UI
// system; menu viewing does not touch disk or mutate a run.
class AimTrainerMenu {
public:
  AimTrainerMenu(AimTrainer& runtime, AimTrainerStore& store);

  void reloadPresets();
  [[nodiscard]] const std::vector<AimScenario>& presets() const;
  [[nodiscard]] const AimScenario& draft() const;
  [[nodiscard]] std::size_t selectedPresetIndex() const;
  [[nodiscard]] const AimTrainerFrame& frame() const;
  [[nodiscard]] const std::vector<AimTrainerResult>& leaderboard() const;
  [[nodiscard]] const std::string& warning() const;

  [[nodiscard]] bool selectPreset(std::size_t index);
  void edit(AimScenario scenario);
  [[nodiscard]] AimTrainerStoreReply saveAs(std::string name);
  [[nodiscard]] AimTrainerStoreReply overwrite();
  [[nodiscard]] AimTrainerStoreReply deleteSelected();
  [[nodiscard]] AimTrainerArmResult start();
  void tick(const UserCommand& command);
  [[nodiscard]] AimTrainerArmResult repeat();
  void abort();

private:
  void refreshLeaderboard();

  AimTrainer& runtime_;
  AimTrainerStore& store_;
  std::vector<AimScenario> presets_;
  std::vector<AimTrainerResult> leaderboard_;
  AimScenario draft_;
  std::size_t selectedPreset_ = 0;
  std::string warning_;
  bool recordedCurrentResult_ = false;
};

} // namespace lg
