#include "trainer/AimTrainerMenu.hpp"

#include <algorithm>
#include <utility>

namespace lg {

AimTrainerMenu::AimTrainerMenu(AimTrainer& runtime, AimTrainerStore& store)
  : runtime_(runtime), store_(store) {
  reloadPresets();
}

void AimTrainerMenu::reloadPresets() {
  AimTrainerPresetList loaded = store_.loadPresets();
  presets_ = std::move(loaded.presets);
  warning_ = std::move(loaded.warning);
  if (presets_.empty()) presets_ = AimTrainerStore::builtInPresets();
  selectedPreset_ = std::min(selectedPreset_, presets_.size() - 1U);
  draft_ = presets_[selectedPreset_];
  refreshLeaderboard();
}

const std::vector<AimScenario>& AimTrainerMenu::presets() const { return presets_; }
const AimScenario& AimTrainerMenu::draft() const { return draft_; }
std::size_t AimTrainerMenu::selectedPresetIndex() const { return selectedPreset_; }
const AimTrainerFrame& AimTrainerMenu::frame() const { return runtime_.view(); }
const std::vector<AimTrainerResult>& AimTrainerMenu::leaderboard() const { return leaderboard_; }
const std::string& AimTrainerMenu::warning() const { return warning_; }

bool AimTrainerMenu::selectPreset(std::size_t index) {
  if (index >= presets_.size() || runtime_.view().phase == AimTrainerPhase::Running) return false;
  selectedPreset_ = index;
  draft_ = presets_[index];
  refreshLeaderboard();
  return true;
}

void AimTrainerMenu::edit(AimScenario scenario) {
  if (runtime_.view().phase == AimTrainerPhase::Running) return;
  draft_ = std::move(scenario);
  refreshLeaderboard();
}

AimTrainerStoreReply AimTrainerMenu::saveAs(std::string name) {
  draft_.name = std::move(name);
  const AimTrainerStoreReply saved = store_.savePreset(draft_, false);
  if (!saved.ok) warning_ = saved.warning;
  else reloadPresets();
  return saved;
}

AimTrainerStoreReply AimTrainerMenu::overwrite() {
  const AimTrainerStoreReply saved = store_.savePreset(draft_, true);
  if (!saved.ok) warning_ = saved.warning;
  else reloadPresets();
  return saved;
}

AimTrainerStoreReply AimTrainerMenu::deleteSelected() {
  const AimTrainerStoreReply removed = store_.deletePreset(draft_.name);
  if (!removed.ok) warning_ = removed.warning;
  else reloadPresets();
  return removed;
}

AimTrainerArmResult AimTrainerMenu::start() {
  const AimTrainerArmResult armed = runtime_.arm(draft_);
  if (!armed.ok) return armed;
  if (!runtime_.start()) return {false, "could not start trainer"};
  recordedCurrentResult_ = false;
  return {true, {}};
}

void AimTrainerMenu::tick(const UserCommand& command) {
  const AimTrainerFrame& current = runtime_.tick(command);
  if (!current.naturalCompletion || recordedCurrentResult_) return;
  recordedCurrentResult_ = true;
  const AimTrainerStoreReply stored = store_.recordNaturalResult(current.result);
  if (!stored.ok) {
    warning_ = stored.warning;
    runtime_.markStorageWarning(warning_);
  }
  refreshLeaderboard();
}

AimTrainerArmResult AimTrainerMenu::repeat() {
  return start();
}

void AimTrainerMenu::abort() {
  runtime_.abort();
}

void AimTrainerMenu::refreshLeaderboard() {
  std::string storageWarning;
  leaderboard_ = store_.leaderboard(AimTrainer::scenarioFingerprint(draft_), &storageWarning);
  if (!storageWarning.empty()) warning_ = std::move(storageWarning);
}

} // namespace lg
