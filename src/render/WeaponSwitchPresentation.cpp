#include "render/WeaponSwitchPresentation.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

[[nodiscard]] float outgoingTimeForLift(float lift) {
  // The drop is linear, so its inverse stays exact. Retargeting from this time
  // keeps the current weapon at the same height without a rest-frame pop.
  return 0.45F * std::clamp(lift, 0.0F, 1.0F);
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
    output.lift = time / kOutgoingEnd;
  } else if (time <= kIncomingBegin) {
    output.lift = 1.0F;
  } else {
    output.lift = (1.0F - time) / (1.0F - kIncomingBegin);
  }
  // This is intentionally restrained: the vertical drop remains the cue.
  output.pitchRadians = 0.12F * output.lift;
  // Worker pose space uses positive pitch to raise its forward axis. The
  // weapon socket is sampled after this layer, so arms and weapon rise as one.
  output.upperBodyPitchRadians = 0.68F * output.lift;
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
    const WeaponSwitchPresentationOutput current = sample();
    displayedWeapon_ = current.displayedWeapon;
    outgoingWeapon_ = displayedWeapon_;
    incomingWeapon_ = observedSelectedWeapon;
    active_ = outgoingWeapon_ != incomingWeapon_;
    elapsedSeconds_ = active_
      ? outgoingTimeForLift(current.lift) * kWeaponSwitchPresentationSeconds
      : kWeaponSwitchPresentationSeconds;
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

bool WeaponSwitchPresentationController::observeAuthoritativeFire(
  Weapon firedWeapon,
  std::uint32_t visualEventKey
) {
  for (const FireEventKey& key : fireEventHistory_) {
    if (
      key.valid &&
      key.weapon == firedWeapon &&
      key.visualEventKey == visualEventKey
    ) {
      return false;
    }
  }
  fireEventHistory_[nextFireEventHistory_ % fireEventHistory_.size()] = {
    firedWeapon,
    visualEventKey,
    true,
  };
  ++nextFireEventHistory_;
  promoteIncomingWeapon(firedWeapon);
  return true;
}

void WeaponSwitchPresentationController::observeContinuousUse(Weapon activeWeapon) {
  promoteIncomingWeapon(activeWeapon);
}

void WeaponSwitchPresentationController::promoteIncomingWeapon(Weapon weapon) {
  if (!initialized_ || !active_ || weapon != incomingWeapon_) return;
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
