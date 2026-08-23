#pragma once

#include "render/Scene3D.hpp"
#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace lg {

struct LocalTracerAim {
  std::uint32_t sequence = 0;
  float yawRadians = 0.0F;
  float pitchRadians = 0.0F;
  bool active = false;
};

class LocalTracerAimHistory {
public:
  void remember(const UserCommand& command);

  [[nodiscard]] bool find(
    std::uint32_t sequence,
    float& yawRadians,
    float& pitchRadians
  ) const;

private:
  static constexpr std::size_t kHistorySize = 128;
  std::array<LocalTracerAim, kHistorySize> entries_ = {};
  std::size_t next_ = 0;
};

struct CapturedMachineGunTracerPresentation {
  bool active = false;
  TransientTracer longTracer = {};
  TransientTracer muzzleCue = {};
};

struct TransientTracerAttachment {
  bool followMuzzle = false;
  Weapon weapon = Weapon::MachineGun;
  std::uint32_t seed = 0;
  std::uint8_t playerIndex = 0;
};

class TransientTracerPool {
public:
  static constexpr std::size_t kCapacity = 128;

  void update(float dt);

  void add(
    const TransientTracer& tracer,
    bool followMuzzle,
    Weapon weapon,
    std::uint32_t seed,
    std::uint8_t playerIndex
  );

  void addCapturedMachineGunPresentation(
    const CapturedMachineGunTracerPresentation& presentation,
    std::uint32_t seed,
    std::uint8_t playerIndex
  );

  template <typename ResolveLiveMuzzle>
  void fillActive(
    std::vector<TransientTracer>& result,
    int combatEffectsQuality,
    ResolveLiveMuzzle&& resolveLiveMuzzle
  ) const {
    result.clear();
    result.reserve(tracers_.size());
    for (std::size_t index = 0; index < tracers_.size(); ++index) {
      if (!active_[index]) {
        continue;
      }
      TransientTracer tracer = tracers_[index];
      if (
        combatEffectsQuality <= 0 &&
        (
          tracer.style == TracerStyle::MachineGun ||
          tracer.style == TracerStyle::MachineGunMuzzleFlash
        )
      ) {
        continue;
      }
      if (followsLiveMuzzle(tracer, attachments_[index])) {
        resolveLiveMuzzle(tracer, attachments_[index]);
      }
      result.push_back(tracer);
    }
  }

private:
  [[nodiscard]] static bool followsLiveMuzzle(
    const TransientTracer& tracer,
    const TransientTracerAttachment& attachment
  ) {
    return attachment.followMuzzle &&
      (
        tracer.style == TracerStyle::RevolverMuzzleFlash ||
        tracer.style == TracerStyle::RocketLauncherMuzzleFlash
      );
  }

  std::array<TransientTracer, kCapacity> tracers_ = {};
  std::array<bool, kCapacity> active_ = {};
  std::array<TransientTracerAttachment, kCapacity> attachments_ = {};
  std::array<std::uint8_t, kCapacity> expiryGraceState_ = {};
};

[[nodiscard]] RenderColor tracerColor(Weapon weapon, std::uint32_t seed);
[[nodiscard]] float localTracerVisualRange(const WeaponFireResult& fire);

[[nodiscard]] WeaponFireResult localPerspectiveTracerFire(
  const Arena& arena,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  const PlayerState& localPlayer,
  const LocalTracerAimHistory& localAimHistory
);

[[nodiscard]] CapturedMachineGunTracerPresentation
captureMachineGunTracerPresentation(
  const WeaponFireResult& fire,
  Vec3 visualStart
);

} // namespace lg
