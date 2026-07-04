#pragma once

#include "console/ConsoleSystem.hpp"
#include "net/NetProtocol.hpp"

#include <cstdint>

namespace lg {

void registerGameplayCvars(ConsoleSystem& console, CvarFlag flags);

[[nodiscard]] MovementTuning movementTuningFromCvars(const ConsoleSystem& console);
[[nodiscard]] WeaponDamageTuning weaponDamageTuningFromCvars(const ConsoleSystem& console);
[[nodiscard]] std::uint8_t selfDamagePercentFromCvars(const ConsoleSystem& console);
[[nodiscard]] std::int32_t healthAmountFromCvars(const ConsoleSystem& console);
[[nodiscard]] std::int32_t knockbackTimeMsFromCvars(const ConsoleSystem& console);
[[nodiscard]] bool infiniteAmmoFromCvars(const ConsoleSystem& console);
[[nodiscard]] std::uint16_t knockbackTimeMsToTicks(std::int32_t milliseconds);
[[nodiscard]] WeaponSwitchingMode weaponSwitchingModeFromCvars(const ConsoleSystem& console);

} // namespace lg
