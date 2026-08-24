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
  [[nodiscard]] std::size_t selectedGroupIndex() const;

  void setRuntimeIdentity(
    std::string mapName,
    std::uint64_t mapIdentity,
    std::uint64_t balanceIdentity
  );
  [[nodiscard]] bool selectPreset(std::size_t index);
  [[nodiscard]] bool selectGroup(std::size_t index);
  [[nodiscard]] bool addGroup();
  [[nodiscard]] bool removeSelectedGroup();
  void edit(AimScenario scenario);
  [[nodiscard]] AimTrainerStoreReply saveAs(std::string name);
  [[nodiscard]] AimTrainerStoreReply overwrite();
  [[nodiscard]] AimTrainerStoreReply deleteSelected();
  [[nodiscard]] AimTrainerArmResult start();
  void tick(const UserCommand& command);
  [[nodiscard]] AimTrainerArmResult repeat();
  [[nodiscard]] AimTrainerArmResult restart();
  void abort();
  void consumePresentationEvents();

private:
  void normalizeDraft();
  void stampDraft();
  void refreshLeaderboard();

  AimTrainer& runtime_;
  AimTrainerStore& store_;
  std::vector<AimScenario> presets_;
  std::vector<AimTrainerResult> leaderboard_;
  AimScenario draft_;
  std::size_t selectedPreset_ = 0;
  std::size_t selectedGroup_ = 0;
  std::string warning_;
  std::string mapName_ = "aim_trainer";
  std::uint64_t mapIdentity_ = 0;
  std::uint64_t balanceIdentity_ = 0;
  bool hasRuntimeIdentity_ = false;
  bool recordedCurrentResult_ = false;
};

} // namespace lg
