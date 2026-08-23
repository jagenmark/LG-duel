#include "trainer/AimTrainerMenu.hpp"

#include <algorithm>
#include <utility>

namespace lg {

AimTrainerMenu::AimTrainerMenu(AimTrainer& runtime, AimTrainerStore& store)
  : runtime_(runtime), store_(store) {
  reloadPresets();
}

void AimTrainerMenu::reloadPresets() {
  const std::string previousName = draft_.name;
  AimTrainerPresetList loaded = store_.loadPresets();
  presets_ = std::move(loaded.presets);
  warning_ = std::move(loaded.warning);
  if (presets_.empty()) presets_ = AimTrainerStore::builtInPresets();
  const auto previous = std::find_if(
    presets_.begin(), presets_.end(),
    [&previousName](const AimScenario& preset) { return preset.name == previousName; }
  );
  if (previous != presets_.end()) {
    selectedPreset_ = static_cast<std::size_t>(previous - presets_.begin());
  } else {
    selectedPreset_ = std::min(selectedPreset_, presets_.size() - 1U);
  }
  draft_ = presets_[selectedPreset_];
  normalizeDraft();
  stampDraft();
  refreshLeaderboard();
}

const std::vector<AimScenario>& AimTrainerMenu::presets() const { return presets_; }
const AimScenario& AimTrainerMenu::draft() const { return draft_; }
std::size_t AimTrainerMenu::selectedPresetIndex() const { return selectedPreset_; }
const AimTrainerFrame& AimTrainerMenu::frame() const { return runtime_.view(); }
const std::vector<AimTrainerResult>& AimTrainerMenu::leaderboard() const { return leaderboard_; }
const std::string& AimTrainerMenu::warning() const { return warning_; }
std::size_t AimTrainerMenu::selectedGroupIndex() const { return selectedGroup_; }

void AimTrainerMenu::setRuntimeIdentity(
  std::string mapName,
  std::uint64_t mapIdentity,
  std::uint64_t balanceIdentity
) {
  mapName_ = std::move(mapName);
  mapIdentity_ = mapIdentity;
  balanceIdentity_ = balanceIdentity;
  hasRuntimeIdentity_ = true;
  stampDraft();
  refreshLeaderboard();
}

bool AimTrainerMenu::selectPreset(std::size_t index) {
  if (index >= presets_.size() || runtime_.view().phase == AimTrainerPhase::Running) return false;
  selectedPreset_ = index;
  draft_ = presets_[index];
  selectedGroup_ = 0;
  normalizeDraft();
  stampDraft();
  refreshLeaderboard();
  return true;
}

bool AimTrainerMenu::selectGroup(std::size_t index) {
  if (runtime_.view().phase == AimTrainerPhase::Running || index >= draft_.groups.size()) {
    return false;
  }
  selectedGroup_ = index;
  return true;
}

bool AimTrainerMenu::addGroup() {
  if (runtime_.view().phase == AimTrainerPhase::Running ||
      draft_.groups.size() >= AimScenario::kMaxGroups) {
    return false;
  }
  AimTargetGroup added = draft_.groups.empty()
    ? AimTargetGroup{}
    : draft_.groups[selectedGroup_];
  added.name = "target " + std::to_string(draft_.groups.size() + 1U);
  draft_.groups.push_back(std::move(added));
  selectedGroup_ = draft_.groups.size() - 1U;
  stampDraft();
  refreshLeaderboard();
  return true;
}

bool AimTrainerMenu::removeSelectedGroup() {
  if (runtime_.view().phase == AimTrainerPhase::Running || draft_.groups.size() <= 1U ||
      selectedGroup_ >= draft_.groups.size()) {
    return false;
  }
  draft_.groups.erase(draft_.groups.begin() + static_cast<std::ptrdiff_t>(selectedGroup_));
  selectedGroup_ = std::min(selectedGroup_, draft_.groups.size() - 1U);
  stampDraft();
  refreshLeaderboard();
  return true;
}

void AimTrainerMenu::edit(AimScenario scenario) {
  if (runtime_.view().phase == AimTrainerPhase::Running) return;
  draft_ = std::move(scenario);
  normalizeDraft();
  stampDraft();
  refreshLeaderboard();
}

AimTrainerStoreReply AimTrainerMenu::saveAs(std::string name) {
  stampDraft();
  draft_.name = std::move(name);
  const AimTrainerStoreReply saved = store_.savePreset(draft_, false);
  if (!saved.ok) {
    warning_ = saved.warning;
  } else {
    const std::string notice = saved.warning;
    reloadPresets();
    if (!notice.empty()) warning_ = notice;
  }
  return saved;
}

AimTrainerStoreReply AimTrainerMenu::overwrite() {
  stampDraft();
  const AimTrainerStoreReply saved = store_.savePreset(draft_, true);
  if (!saved.ok) {
    warning_ = saved.warning;
  } else {
    const std::string notice = saved.warning;
    reloadPresets();
    if (!notice.empty()) warning_ = notice;
  }
  return saved;
}

AimTrainerStoreReply AimTrainerMenu::deleteSelected() {
  const AimTrainerStoreReply removed = store_.deletePreset(draft_.name);
  if (!removed.ok) warning_ = removed.warning;
  else reloadPresets();
  stampDraft();
  return removed;
}

AimTrainerArmResult AimTrainerMenu::start() {
  normalizeDraft();
  stampDraft();
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

void AimTrainerMenu::consumePresentationEvents() {
  runtime_.consumePresentationEvents();
}

void AimTrainerMenu::normalizeDraft() {
  if (draft_.groups.empty()) draft_.groups.push_back(AimTargetGroup{});
  if (draft_.groups.size() > AimScenario::kMaxGroups) {
    draft_.groups.resize(AimScenario::kMaxGroups);
  }
  selectedGroup_ = std::min(selectedGroup_, draft_.groups.size() - 1U);
  for (std::size_t index = 0; index < draft_.groups.size(); ++index) {
    AimTargetGroup& group = draft_.groups[index];
    if (group.name.empty()) group.name = "target " + std::to_string(index + 1U);
    group.count = std::clamp(group.count, 1U, AimScenario::kMaxTargetsPerGroup);
    if (group.fixedSpawns.empty()) group.fixedSpawns.push_back({5.0F, 0.0F, 1.5F});
  }
}

void AimTrainerMenu::stampDraft() {
  if (!hasRuntimeIdentity_) return;
  draft_.mapName = mapName_;
  draft_.mapIdentity = mapIdentity_;
  draft_.balanceIdentity = balanceIdentity_;
}

void AimTrainerMenu::refreshLeaderboard() {
  std::string storageWarning;
  leaderboard_ = store_.leaderboard(AimTrainer::scenarioFingerprint(draft_), &storageWarning);
  if (!storageWarning.empty()) warning_ = std::move(storageWarning);
}

} // namespace lg
