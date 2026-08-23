#pragma once

#include "trainer/AimTrainerMenu.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

enum class AimTrainerEditorField : std::uint8_t {
  Preset,
  PresetName,
  SaveAs,
  Overwrite,
  DeletePreset,
  Start,
  Abort,
  Duration,
  PlayerMovement,
  WeaponPolicy,
  ForcedWeapon,
  InfiniteAmmo,
  ScoreMode,
  HitScore,
  DamageScore,
  ClearScore,
  Seed,
  AllowedWeapon,
  MapIdentity,
  BalanceIdentity,
  TargetCap,
  Group,
  AddGroup,
  RemoveGroup,
  GroupName,
  Visual,
  Count,
  Radius,
  Color,
  Life,
  Health,
  RespawnDelay,
  SpawnMode,
  FixedSpawns,
  RandomMinimum,
  RandomMaximum,
  Motion,
  StrafeSpeed,
  StrafeDirection,
  WaypointInterval,
  Result,
  Leaderboard,
};

struct AimTrainerEditorRow {
  AimTrainerEditorField field = AimTrainerEditorField::Preset;
  std::uint8_t component = 0;
  std::string label;
  std::string value;
  bool command = false;
  bool editable = false;
};

class AimTrainerEditor {
public:
  explicit AimTrainerEditor(AimTrainerMenu& menu);

  [[nodiscard]] bool open() const;
  void setOpen(bool open);
  [[nodiscard]] std::size_t selectedRow() const;
  [[nodiscard]] std::size_t scrollRows() const;
  void setScrollRows(std::size_t rows);
  [[nodiscard]] bool editingText() const;
  [[nodiscard]] const std::string& textInput() const;
  [[nodiscard]] const std::string& message() const;
  [[nodiscard]] std::vector<AimTrainerEditorRow> rows() const;

  void selectRow(std::size_t row);
  void moveSelection(int amount);
  [[nodiscard]] bool adjustSelected(int direction);
  [[nodiscard]] bool activateSelected();
  void insertText(std::string_view text);
  void backspace();
  [[nodiscard]] bool commitText();
  void cancelText();

private:
  [[nodiscard]] bool adjust(const AimTrainerEditorRow& row, int direction);
  [[nodiscard]] bool activate(const AimTrainerEditorRow& row);
  [[nodiscard]] bool applyText(const AimTrainerEditorRow& row, std::string_view text);
  void beginText(const AimTrainerEditorRow& row);
  void clampSelection();

  AimTrainerMenu& menu_;
  bool open_ = true;
  std::size_t selectedRow_ = 0;
  std::size_t scrollRows_ = 0;
  std::optional<AimTrainerEditorRow> textRow_;
  std::string textInput_;
  std::string message_;
};

} // namespace lg
