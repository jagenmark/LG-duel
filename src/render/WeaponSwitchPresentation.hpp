#pragma once

#include "shared/Math.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstdint>

namespace lg {

// A caller owns the presentation timeline. This component never reads a clock
// and has no authority over weapon selection or fire eligibility.
inline constexpr float kWeaponSwitchPresentationSeconds = 0.16F;

struct WeaponSwitchPresentationOutput {
  Weapon displayedWeapon = Weapon::LightningGun;
  Weapon outgoingWeapon = Weapon::LightningGun;
  Weapon incomingWeapon = Weapon::LightningGun;
  // 0 is rest; 1 is the hidden swap point. First person maps this down below
  // the frame, while third person maps it to an upward arm-and-weapon lift.
  float lift = 0.0F;
  // A small first-person pitch accompanies the drop.
  float pitchRadians = 0.0F;
  // Worker uses this procedural upper-body layer so its arms and weapon socket
  // rise together while its lower-body locomotion stays in the base pose.
  float upperBodyPitchRadians = 0.0F;
  float normalizedTime = 1.0F;
  bool active = false;
  bool incomingHalf = true;
};

// Pure curve sampling. The weapon swap happens only once at normalized 0.5.
[[nodiscard]] WeaponSwitchPresentationOutput sampleWeaponSwitchPresentation(
  Weapon outgoingWeapon,
  Weapon incomingWeapon,
  float normalizedTime,
  bool active
);

class WeaponSwitchPresentationController {
public:
  [[nodiscard]] WeaponSwitchPresentationOutput update(
    Weapon observedSelectedWeapon,
    float deltaSeconds,
    bool enabled = true
  );

  // A known fire from the incoming weapon must never appear to originate from
  // the outgoing weapon. This changes display presentation only.
  // visualEventKey is the authoritative fire event's stable visual seed. A
  // retained snapshot may report the same event more than once; duplicates
  // must not alter a later switch that happens to target the same weapon.
  [[nodiscard]] bool observeAuthoritativeFire(
    Weapon firedWeapon,
    std::uint32_t visualEventKey
  );

  // Continuous beam state has no one-shot fire key. Repeated observations are
  // safe because promotion only advances an active switch to its incoming half.
  void observeContinuousUse(Weapon activeWeapon);
  void reset();

  [[nodiscard]] bool initialized() const { return initialized_; }

private:
  [[nodiscard]] WeaponSwitchPresentationOutput sample() const;
  void promoteIncomingWeapon(Weapon weapon);

  struct FireEventKey {
    Weapon weapon = Weapon::LightningGun;
    std::uint32_t visualEventKey = 0;
    bool valid = false;
  };

  static constexpr std::size_t kFireEventHistoryCapacity = 16U;

  Weapon displayedWeapon_ = Weapon::LightningGun;
  Weapon outgoingWeapon_ = Weapon::LightningGun;
  Weapon incomingWeapon_ = Weapon::LightningGun;
  float elapsedSeconds_ = kWeaponSwitchPresentationSeconds;
  std::array<FireEventKey, kFireEventHistoryCapacity> fireEventHistory_ = {};
  std::size_t nextFireEventHistory_ = 0U;
  bool initialized_ = false;
  bool active_ = false;
};

} // namespace lg
