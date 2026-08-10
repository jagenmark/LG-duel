#include "render/WeaponSwitchPresentation.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

[[nodiscard]] float easeOutCubic(float value) {
  const float inverse = 1.0F - std::clamp(value, 0.0F, 1.0F);
  return 1.0F - inverse * inverse * inverse;
}

[[nodiscard]] float easeInOut(float value) {
  const float clamped = std::clamp(value, 0.0F, 1.0F);
  return clamped * clamped * (3.0F - 2.0F * clamped);
}

} // namespace

WeaponSwitchPresentationOutput sampleWeaponSwitchPresentation(
  Weapon outgoingWeapon,
  Weapon incomingWeapon,
  float normalizedTime,
  bool active
) {
  WeaponSwitchPresentationOutput output;
  output.outgoingWeapon = outgoingWeapon;
  output.incomingWeapon = incomingWeapon;
  output.active = active;
  output.normalizedTime = active
    ? std::clamp(normalizedTime, 0.0F, 1.0F)
    : 1.0F;
  if (!active) {
    output.displayedWeapon = incomingWeapon;
    output.incomingHalf = true;
    return output;
  }

  constexpr float kOutgoingEnd = 0.45F;
  constexpr float kIncomingBegin = 0.55F;
  const float time = output.normalizedTime;
  output.incomingHalf = time >= 0.5F;
  output.displayedWeapon = output.incomingHalf ? incomingWeapon : outgoingWeapon;
  if (time < kOutgoingEnd) {
    output.lift = easeOutCubic(time / kOutgoingEnd);
  } else if (time <= kIncomingBegin) {
    output.lift = 1.0F;
  } else {
    output.lift = 1.0F - easeInOut((time - kIncomingBegin) / (1.0F - kIncomingBegin));
  }
  // This is intentionally restrained: raise/lower remains the readable cue.
  output.pitchRadians = -0.16F * output.lift;
  output.upperBodyPitchRadians = -0.68F * output.lift;
  return output;
}

WeaponSwitchPresentationOutput WeaponSwitchPresentationController::sample() const {
  return sampleWeaponSwitchPresentation(
    outgoingWeapon_,
    incomingWeapon_,
    elapsedSeconds_ / kWeaponSwitchPresentationSeconds,
    active_
  );
}

WeaponSwitchPresentationOutput WeaponSwitchPresentationController::update(
  Weapon observedSelectedWeapon,
  float deltaSeconds,
  bool enabled
) {
  if (!initialized_) {
    // The first snapshot establishes a rest state; joining must not animate.
    displayedWeapon_ = observedSelectedWeapon;
    outgoingWeapon_ = observedSelectedWeapon;
    incomingWeapon_ = observedSelectedWeapon;
    elapsedSeconds_ = kWeaponSwitchPresentationSeconds;
    initialized_ = true;
    active_ = false;
    return sample();
  }

  if (!enabled) {
    displayedWeapon_ = observedSelectedWeapon;
    outgoingWeapon_ = observedSelectedWeapon;
    incomingWeapon_ = observedSelectedWeapon;
    elapsedSeconds_ = kWeaponSwitchPresentationSeconds;
    active_ = false;
    return sample();
  }

  if (observedSelectedWeapon != incomingWeapon_) {
    // Retarget from the only weapon the viewer can currently see. There is no
    // queue and no hidden history, so repeated rapid selections stay bounded.
    displayedWeapon_ = sample().displayedWeapon;
    outgoingWeapon_ = displayedWeapon_;
    incomingWeapon_ = observedSelectedWeapon;
    elapsedSeconds_ = 0.0F;
    active_ = outgoingWeapon_ != incomingWeapon_;
  }

  if (active_) {
    elapsedSeconds_ = std::min(
      kWeaponSwitchPresentationSeconds,
      elapsedSeconds_ + std::max(0.0F, deltaSeconds)
    );
    if (elapsedSeconds_ >= kWeaponSwitchPresentationSeconds) {
      active_ = false;
      displayedWeapon_ = incomingWeapon_;
    }
  }
  return sample();
}

void WeaponSwitchPresentationController::observeAuthoritativeFire(Weapon firedWeapon) {
  if (!initialized_ || !active_ || firedWeapon != incomingWeapon_) {
    return;
  }
  // The incoming half begins after the apex plateau. Keep the visible weapon
  // aligned with the fired weapon without affecting simulation or networking.
  elapsedSeconds_ = std::max(
    elapsedSeconds_,
    kWeaponSwitchPresentationSeconds * 0.55F
  );
}

void WeaponSwitchPresentationController::reset() {
  *this = {};
}

} // namespace lg
