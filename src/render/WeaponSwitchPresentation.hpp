#pragma once

#include "shared/Math.hpp"
#include "sim/UserCommand.hpp"

namespace lg {

// A caller owns the presentation timeline. This component never reads a clock
// and has no authority over weapon selection or fire eligibility.
inline constexpr float kWeaponSwitchPresentationSeconds = 0.16F;

struct WeaponSwitchPresentationOutput {
  Weapon displayedWeapon = Weapon::LightningGun;
  Weapon outgoingWeapon = Weapon::LightningGun;
  Weapon incomingWeapon = Weapon::LightningGun;
  // 0 is rest; 1 is the top of the Q3-style raise. It is in view-local up.
  float lift = 0.0F;
  // A small view-local pitch accompanies the dominant vertical movement.
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
  void observeAuthoritativeFire(Weapon firedWeapon);
  void reset();

  [[nodiscard]] bool initialized() const { return initialized_; }

private:
  [[nodiscard]] WeaponSwitchPresentationOutput sample() const;

  Weapon displayedWeapon_ = Weapon::LightningGun;
  Weapon outgoingWeapon_ = Weapon::LightningGun;
  Weapon incomingWeapon_ = Weapon::LightningGun;
  float elapsedSeconds_ = kWeaponSwitchPresentationSeconds;
  bool initialized_ = false;
  bool active_ = false;
};

} // namespace lg
