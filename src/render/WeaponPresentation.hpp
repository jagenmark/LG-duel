#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>

namespace lg {

struct MachineGunBarrelSpinTuning {
  float maximumRevolutionsPerSecond = 14.0F;
  float spinUpSeconds = 0.25F;
  float spinDownSeconds = 0.55F;
};

struct MachineGunBarrelSpinState {
  float angleRadians = 0.0F;
  float angularVelocityRadiansPerSecond = 0.0F;

  void update(
    bool motorDriven,
    float deltaSeconds,
    const MachineGunBarrelSpinTuning& tuning = {}
  ) {
    // This state is presentation-only. It responds immediately to local input
    // but never controls authoritative fire cadence, ammunition, or hitscan.
    const float dt = std::max(deltaSeconds, 0.0F);
    const float maximumSpeed = std::max(tuning.maximumRevolutionsPerSecond, 0.0F) *
      2.0F * std::numbers::pi_v<float>;
    const float targetSpeed = motorDriven ? maximumSpeed : 0.0F;
    const float responseSeconds = motorDriven
      ? tuning.spinUpSeconds
      : tuning.spinDownSeconds;
    const float acceleration = responseSeconds > 0.0F
      ? maximumSpeed / responseSeconds
      : maximumSpeed / std::max(dt, 0.000001F);
    const float maximumChange = acceleration * dt;
    if (angularVelocityRadiansPerSecond < targetSpeed) {
      angularVelocityRadiansPerSecond = std::min(
        angularVelocityRadiansPerSecond + maximumChange,
        targetSpeed
      );
    } else {
      angularVelocityRadiansPerSecond = std::max(
        angularVelocityRadiansPerSecond - maximumChange,
        targetSpeed
      );
    }

    angleRadians = std::fmod(
      angleRadians + angularVelocityRadiansPerSecond * dt,
      2.0F * std::numbers::pi_v<float>
    );
    if (angleRadians < 0.0F) {
      angleRadians += 2.0F * std::numbers::pi_v<float>;
    }
  }

  [[nodiscard]] float normalizedSpeed(
    const MachineGunBarrelSpinTuning& tuning = {}
  ) const {
    const float maximumSpeed = std::max(tuning.maximumRevolutionsPerSecond, 0.0F) *
      2.0F * std::numbers::pi_v<float>;
    return maximumSpeed > 0.0F
      ? std::clamp(angularVelocityRadiansPerSecond / maximumSpeed, 0.0F, 1.0F)
      : 0.0F;
  }
};

struct MachineGunFiringResponseState {
  static constexpr float kKickDurationSeconds = 0.085F;
  static constexpr float kVibrationDurationSeconds = 0.12F;

  float kickRemainingSeconds = 0.0F;
  float vibrationRemainingSeconds = 0.0F;
  float vibrationPhaseRadians = 0.0F;

  void triggerShot(std::uint32_t visualSeed) {
    kickRemainingSeconds = kKickDurationSeconds;
    vibrationRemainingSeconds = kVibrationDurationSeconds;
    vibrationPhaseRadians = std::fmod(
      vibrationPhaseRadians +
        static_cast<float>((visualSeed * 2654435761U) & 255U) *
          (2.0F * std::numbers::pi_v<float> / 256.0F),
      2.0F * std::numbers::pi_v<float>
    );
  }

  void update(float deltaSeconds, float normalizedBarrelSpeed) {
    const float dt = std::max(deltaSeconds, 0.0F);
    kickRemainingSeconds = std::max(0.0F, kickRemainingSeconds - dt);
    vibrationRemainingSeconds = std::max(0.0F, vibrationRemainingSeconds - dt);
    const float frequency = 34.0F +
      std::clamp(normalizedBarrelSpeed, 0.0F, 1.0F) * 18.0F;
    vibrationPhaseRadians = std::fmod(
      vibrationPhaseRadians +
        dt * frequency * 2.0F * std::numbers::pi_v<float>,
      2.0F * std::numbers::pi_v<float>
    );
  }

  [[nodiscard]] float kickAmount() const {
    const float normalized = std::clamp(
      kickRemainingSeconds / kKickDurationSeconds,
      0.0F,
      1.0F
    );
    return normalized * normalized;
  }

  [[nodiscard]] float vibrationAmount(float normalizedBarrelSpeed) const {
    const float envelope = std::clamp(
      vibrationRemainingSeconds / kVibrationDurationSeconds,
      0.0F,
      1.0F
    );
    return envelope * std::clamp(normalizedBarrelSpeed, 0.0F, 1.0F);
  }
};

struct RocketLauncherFiringResponseState {
  static constexpr float kDurationSeconds = 0.20F;

  float elapsedSeconds = kDurationSeconds;

  void triggerShot() {
    elapsedSeconds = 0.0F;
  }

  void update(float deltaSeconds) {
    elapsedSeconds = std::min(
      kDurationSeconds,
      elapsedSeconds + std::max(deltaSeconds, 0.0F)
    );
  }

  [[nodiscard]] bool active() const {
    return elapsedSeconds < kDurationSeconds;
  }

  [[nodiscard]] float mechanicalAmount() const {
    // Match the authored 60 Hz reference clip: snap to peak near frame 4,
    // cross a small overshoot, then settle without controlling fire cadence.
    if (elapsedSeconds <= 0.05F) {
      return elapsedSeconds / 0.05F;
    }
    if (elapsedSeconds <= 0.10F) {
      const float t = (elapsedSeconds - 0.05F) / 0.05F;
      return 1.0F + (-0.17F - 1.0F) * t;
    }
    const float t = std::clamp((elapsedSeconds - 0.10F) / 0.10F, 0.0F, 1.0F);
    return -0.17F * (1.0F - t) * (1.0F - t);
  }

  [[nodiscard]] float wholeWeaponRecoilAmount() const {
    if (elapsedSeconds <= 0.035F) {
      return elapsedSeconds / 0.035F;
    }
    const float t = std::clamp(
      (elapsedSeconds - 0.035F) / (kDurationSeconds - 0.035F),
      0.0F,
      1.0F
    );
    return (1.0F - t) * (1.0F - t);
  }
};

inline constexpr float kRevolverTracerLifetimeSeconds = 0.11F;
inline constexpr float kRevolverTracerMuzzleFollowSeconds = 0.055F;

struct RevolverTracerPresentation {
  float alpha = 0.0F;
  bool followMuzzle = false;
  bool active = false;
};

[[nodiscard]] inline RevolverTracerPresentation revolverTracerPresentation(
  float ageSeconds
) {
  const float age = std::max(ageSeconds, 0.0F);
  const float progress = std::clamp(
    age / kRevolverTracerLifetimeSeconds,
    0.0F,
    1.0F
  );
  return {
    std::pow(1.0F - progress, 1.35F),
    age <= kRevolverTracerMuzzleFollowSeconds,
    age < kRevolverTracerLifetimeSeconds,
  };
}

} // namespace lg
