#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace lg {

struct RenderColor {
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
};

struct ScreenPoint {
  float x = 0.0F;
  float y = 0.0F;
};

struct ScreenRect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

struct FilledQuad2D {
  std::array<ScreenPoint, 4> points = {};
  RenderColor color = {};
};

enum class HudImage : std::uint8_t {
  WeaponMachineGun,
  WeaponShotgun,
  WeaponGrenadeLauncher,
  WeaponRocketLauncher,
  WeaponLightningGun,
  WeaponSniperRifle,
  WeaponPlasmaGun,
  WeaponFreezeGun,
  WeaponRevolver,
  HealthSegmented,
  HealthFilled,
  HealthOutlined,
  Count,
};

struct Image2D {
  HudImage image = HudImage::WeaponMachineGun;
  ScreenRect destination = {};
  // Normalized coordinates within the source image.
  ScreenRect source = {0.0F, 0.0F, 1.0F, 1.0F};
  RenderColor color = {};
};

struct Line2D {
  ScreenPoint start = {};
  ScreenPoint end = {};
  RenderColor color = {};
  float width = 1.0F;
};

struct SniperScopeOverlay2D {
  int outputWidth = 0;
  int outputHeight = 0;
  ScreenPoint center = {};
  float radius = 0.0F;
  float openingScale = 1.0F;
  float opacity = 1.0F;
  RenderColor color = {255, 255, 255, 255};
};

enum class TextHorizontalAlignment : std::uint8_t {
  Left,
  Center,
  Right,
};

struct Text2D {
  ScreenPoint position = {};
  std::string text;
  RenderColor color = {};
  float scale = 1.0F;
  TextHorizontalAlignment horizontalAlignment = TextHorizontalAlignment::Left;
};

using DrawCommand2D =
  std::variant<FilledQuad2D, Image2D, Line2D, SniperScopeOverlay2D, Text2D>;

struct DrawList2D {
  ScreenRect clip = {};
  std::vector<DrawCommand2D> commands;
  std::vector<DrawCommand2D> overlayCommands;
};

} // namespace lg
