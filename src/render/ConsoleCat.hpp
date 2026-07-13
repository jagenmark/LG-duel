#pragma once

#include "render/DrawList2D.hpp"

#include <cstdint>

namespace lg {

enum class ConsoleCatAction : std::uint8_t {
  Idle,
  Stalk,
  Crouch,
  Leap,
  Land,
  LieDown,
  Sleep,
};

struct ConsoleCatPose {
  ScreenPoint position = {};
  ScreenPoint laser = {};
  ConsoleCatAction action = ConsoleCatAction::Idle;
  std::uint8_t frame = 0;
  bool facingRight = true;
  bool profile = false;
};

class ConsoleCatController {
public:
  void reset(float viewportWidth, float viewportHeight);
  void update(
    float deltaSeconds,
    float cursorX,
    float cursorY,
    float viewportWidth,
    float viewportHeight
  );

  [[nodiscard]] const ConsoleCatPose& pose() const { return pose_; }

private:
  ConsoleCatPose pose_;
  float actionSeconds_ = 0.0F;
  float velocityX_ = 0.0F;
  float velocityY_ = 0.0F;
  ScreenPoint previousCursor_ = {};
  float inactiveSeconds_ = 0.0F;
  bool hasPreviousCursor_ = false;
  bool initialized_ = false;
};

} // namespace lg
