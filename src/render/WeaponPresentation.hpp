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

struct MachineGunMuzzleFlashEnvelope {
  float flameAlpha = 0.0F;
  float coreAlpha = 0.0F;
  float coreScale = 0.0F;
};

inline constexpr float kMachineGunMuzzleFlashDurationSeconds = 0.13F;

[[nodiscard]] inline MachineGunMuzzleFlashEnvelope machineGunMuzzleFlashEnvelope(
  float ageSeconds,
  std::uint32_t visualSeed
) {
  if (
    !std::isfinite(ageSeconds) ||
    ageSeconds < 0.0F ||
    ageSeconds >= kMachineGunMuzzleFlashDurationSeconds
  ) {
    return {};
  }
  // Keep each shot crisp, then retain a small seeded core until the next
  // normal 13-tick shot. The carry-over avoids an all-black gap without
  // turning held fire into a constant glow.
  const float sharp = std::pow(std::clamp(
    1.0F - ageSeconds / 0.055F,
    0.0F,
    1.0F
  ), 1.65F);
  const float carry = std::pow(std::clamp(
    1.0F - ageSeconds / kMachineGunMuzzleFlashDurationSeconds,
    0.0F,
    1.0F
  ), 1.10F);
  const float variation = 0.92F +
    static_cast<float>((visualSeed >> 3U) & 3U) * 0.025F;
  return {
    std::clamp(0.12F * carry * variation + 0.88F * sharp, 0.0F, 1.0F),
    std::clamp(0.40F * carry * variation + 0.60F * sharp, 0.0F, 1.0F),
    0.76F + 0.24F * sharp + (variation - 0.92F) * 0.40F,
  };
}

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

struct FreezeGunFiringResponseState {
  static constexpr float kAttackSeconds = 0.045F;
  static constexpr float kReleaseSeconds = 0.16F;
  static constexpr float kActivationFlashSeconds = 0.09F;

  float amount = 0.0F;
  float activationFlashRemainingSeconds = 0.0F;
  float phaseRadians = 0.0F;
  bool wasDriven = false;

  void update(bool driven, float deltaSeconds) {
    const float dt = std::max(deltaSeconds, 0.0F);
    if (driven && !wasDriven) {
      activationFlashRemainingSeconds = kActivationFlashSeconds;
    }
    wasDriven = driven;
    const float response = driven ? kAttackSeconds : kReleaseSeconds;
    const float target = driven ? 1.0F : 0.0F;
    const float blend = response > 0.0F
      ? 1.0F - std::exp(-dt / response)
      : 1.0F;
    amount += (target - amount) * std::clamp(blend, 0.0F, 1.0F);
    if (!driven && amount < 0.0005F) {
      amount = 0.0F;
    }
    activationFlashRemainingSeconds = std::max(
      0.0F,
      activationFlashRemainingSeconds - dt
    );
    phaseRadians = std::fmod(
      phaseRadians + dt * (10.0F + amount * 7.0F) * 2.0F * std::numbers::pi_v<float>,
      2.0F * std::numbers::pi_v<float>
    );
  }

  [[nodiscard]] float activationFlashAmount() const {
    const float t = std::clamp(
      activationFlashRemainingSeconds / kActivationFlashSeconds,
      0.0F,
      1.0F
    );
    return t * t;
  }

  [[nodiscard]] float coolantPulse() const {
    return amount * (0.5F + 0.5F * std::sin(phaseRadians * 0.45F));
  }
};

struct PlasmaGunFiringResponseState {
  static constexpr float kDurationSeconds = 0.16F;

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

  [[nodiscard]] float containmentAmount() const {
    const float remaining = std::clamp(
      1.0F - elapsedSeconds / kDurationSeconds,
      0.0F,
      1.0F
    );
    // A sharp contraction followed by a smooth settle reads at plasma cadence
    // without making the presentation state part of projectile simulation.
    return remaining * remaining;
  }
};

inline constexpr float kRevolverTracerLifetimeSeconds = 0.11F;

struct RevolverTracerPresentation {
  float alpha = 0.0F;
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
    age < kRevolverTracerLifetimeSeconds,
  };
}

// This is presentation-only. The fired Railgun trace keeps the event's
// world-space start/end pair while the client shows only a compact trace.
inline constexpr float kSniperSmokeTracerLifetimeSeconds = 0.085F;
inline constexpr float kSniperSmokeTracerMaximumLength = 2.40F;

struct SniperSmokeTracerPresentation {
  float alpha = 0.0F;
  bool active = false;
};

[[nodiscard]] inline SniperSmokeTracerPresentation sniperSmokeTracerPresentation(
  float ageSeconds
) {
  if (!std::isfinite(ageSeconds)) {
    return {};
  }
  const float age = std::max(ageSeconds, 0.0F);
  const float progress = std::clamp(
    age / kSniperSmokeTracerLifetimeSeconds,
    0.0F,
    1.0F
  );
  return {
    std::pow(1.0F - progress, 1.55F),
    age < kSniperSmokeTracerLifetimeSeconds,
  };
}

} // namespace lg
