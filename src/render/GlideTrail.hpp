#pragma once

#include "render/Renderer.hpp"
#include "sim/Movement.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lg {

// Short world-space boot streaks. Sample by distance so density does not depend
// on render rate; discard history across teleports and visibility changes.
class GlideTrail {
public:
  void update(float dt, std::uint32_t revision, bool replay) {
    if (revision != revision_ || replay != replay_) {
      *this = {};
      revision_ = revision;
      replay_ = replay;
    }
    for (auto& effect : effects_) effect.ageSeconds += std::max(0.0F, dt);
    for (auto& sample : samples_) {
      if (sample.active) sample.slideSeconds += std::max(0.0F, dt);
    }
  }

  void observe(std::size_t owner, const PlayerState* player, const MovementTuning& tuning) {
    if (owner >= samples_.size()) return;
    auto& previous = samples_[owner];
    if (player == nullptr || player->health <= 0) {
      previous.active = false;
      for (auto& effect : effects_) {
        if (effect.ownerIndex == owner) effect.lifetimeSeconds = 0.0F;
      }
      return;
    }
    if (!isGlideSlideActive(*player, tuning)) {
      previous.active = false;
      return;
    }
    const Vec3 center = player->position - Vec3{0, 0, player->bounds.halfHeight - .045F};
    const Vec3 forward{std::cos(player->viewYawRadians), std::sin(player->viewYawRadians), 0};
    const Vec3 right{-forward.y, forward.x, 0};
    const std::array<Vec3, 2> feet{center - right * .18F + forward * .23F,
                                   center + right * .18F - forward * .11F};
    const Vec3 delta = center - previous.center;
    const float distance = std::sqrt(dot(delta, delta));
    if (previous.active && distance < .10F) return;
    if (previous.active && distance < 1.5F && std::abs(delta.z) < .3F) {
      const float strength = std::clamp(std::hypot(player->velocity.x, player->velocity.y) / 10.5F, .3F, 1.0F);
      for (std::size_t foot = 0; foot < feet.size(); ++foot) {
        auto& effect = effects_[next_++ % effects_.size()];
        effect = {};
        effect.type = TransientEffectType::GlideFootTrail;
        effect.position = (feet[foot] + previous.feet[foot]) * .5F;
        effect.direction = (feet[foot] - previous.feet[foot]) * .5F;
        effect.lifetimeSeconds = .24F;
        effect.initialScale = .025F + .025F * strength;
        effect.finalScale = .01F;
        const float onset = std::clamp(previous.slideSeconds / .12F, 0.0F, 1.0F);
        const float easedOnset = onset * onset * (3.0F - 2.0F * onset);
        effect.color = {100, 223, 245, static_cast<std::uint8_t>((65 + 65 * strength) * easedOnset)};
        effect.ownerIndex = static_cast<std::uint8_t>(owner);
      }
    }
    previous = {true, center, feet, previous.active ? previous.slideSeconds : 0.0F};
  }

  void appendActive(std::vector<TransientEffect>& out) const {
    for (const auto& effect : effects_) {
      if (effect.ageSeconds < effect.lifetimeSeconds) out.push_back(effect);
    }
  }

private:
  struct Sample { bool active = false; Vec3 center{}; std::array<Vec3, 2> feet{}; float slideSeconds = 0.0F; };
  std::array<Sample, kDuelPlayerCount> samples_{};
  std::array<TransientEffect, 512> effects_{};
  std::size_t next_ = 0;
  std::uint32_t revision_ = 0;
  bool replay_ = false;
};
}
