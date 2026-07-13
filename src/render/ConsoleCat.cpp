#include "render/ConsoleCat.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

constexpr float kConsoleHeightRatio = 0.55F;
constexpr float kFloorPadding = 7.0F;
constexpr float kCatHalfWidth = 25.0F;

[[nodiscard]] float catFloor(float viewportHeight) {
  return viewportHeight * kConsoleHeightRatio - kFloorPadding;
}

} // namespace

void ConsoleCatController::reset(float viewportWidth, float viewportHeight) {
  pose_ = {};
  pose_.position = {
    std::clamp(viewportWidth * 0.18F, kCatHalfWidth, viewportWidth - kCatHalfWidth),
    catFloor(viewportHeight),
  };
  pose_.laser = pose_.position;
  pose_.action = ConsoleCatAction::Idle;
  pose_.facingRight = true;
  actionSeconds_ = 0.0F;
  velocityX_ = 0.0F;
  velocityY_ = 0.0F;
  initialized_ = true;
}

void ConsoleCatController::update(
  float deltaSeconds,
  float cursorX,
  float cursorY,
  float viewportWidth,
  float viewportHeight
) {
  if (!initialized_) {
    reset(viewportWidth, viewportHeight);
  }

  // Presentation time is clamped so a debugger pause cannot teleport the cat.
  const float dt = std::clamp(deltaSeconds, 0.0F, 0.05F);
  const float floor = catFloor(viewportHeight);
  pose_.laser = {
    std::clamp(cursorX, 0.0F, viewportWidth),
    std::clamp(cursorY, 0.0F, viewportHeight * kConsoleHeightRatio),
  };
  const float offsetX = pose_.laser.x - pose_.position.x;
  const float offsetY = pose_.laser.y - pose_.position.y;
  pose_.facingRight = std::abs(offsetX) < 1.0F ? pose_.facingRight : offsetX > 0.0F;
  actionSeconds_ += dt;

  switch (pose_.action) {
  case ConsoleCatAction::Idle:
    velocityX_ = 0.0F;
    if (offsetY < -24.0F && std::abs(offsetX) <= 48.0F) {
      pose_.action = ConsoleCatAction::Crouch;
      actionSeconds_ = 0.0F;
    } else if (std::abs(offsetX) > 48.0F) {
      pose_.action = ConsoleCatAction::Stalk;
      actionSeconds_ = 0.0F;
    }
    break;
  case ConsoleCatAction::Stalk: {
    const float direction = offsetX < 0.0F ? -1.0F : 1.0F;
    velocityX_ = direction * std::clamp(std::abs(offsetX) * 2.0F, 55.0F, 175.0F);
    pose_.position.x += velocityX_ * dt;
    const bool laserIsTempting = offsetY < -24.0F && std::abs(offsetX) < 190.0F;
    if (laserIsTempting || actionSeconds_ > 1.15F) {
      pose_.action = ConsoleCatAction::Crouch;
      actionSeconds_ = 0.0F;
      velocityX_ = 0.0F;
    } else if (std::abs(offsetX) <= 34.0F) {
      pose_.action = ConsoleCatAction::Idle;
      actionSeconds_ = 0.0F;
    }
    break;
  }
  case ConsoleCatAction::Crouch:
    velocityX_ = 0.0F;
    if (actionSeconds_ >= 0.18F) {
      // The leap aims toward the pointer but remains bounded and purely visual.
      velocityX_ = std::clamp(offsetX * 2.4F, -310.0F, 310.0F);
      velocityY_ = std::clamp(offsetY * 3.0F - 250.0F, -500.0F, -310.0F);
      pose_.action = ConsoleCatAction::Leap;
      actionSeconds_ = 0.0F;
    }
    break;
  case ConsoleCatAction::Leap:
    velocityY_ += 1050.0F * dt;
    pose_.position.x += velocityX_ * dt;
    pose_.position.y += velocityY_ * dt;
    if (pose_.position.y >= floor && velocityY_ > 0.0F) {
      pose_.position.y = floor;
      velocityX_ = 0.0F;
      velocityY_ = 0.0F;
      pose_.action = ConsoleCatAction::Land;
      actionSeconds_ = 0.0F;
    }
    break;
  case ConsoleCatAction::Land:
    if (actionSeconds_ >= 0.22F) {
      pose_.action = ConsoleCatAction::Idle;
      actionSeconds_ = 0.0F;
    }
    break;
  }

  pose_.position.x = std::clamp(
    pose_.position.x,
    kCatHalfWidth,
    std::max(kCatHalfWidth, viewportWidth - kCatHalfWidth)
  );
  pose_.position.y = std::min(pose_.position.y, floor);
  pose_.frame = static_cast<std::uint8_t>(
    std::floor(actionSeconds_ * 8.0F)
  ) & 1U;
}

} // namespace lg
