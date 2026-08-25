#include "render/ScreenUi.hpp"
#include "app/TextInput.hpp"
#include "render/ChatLayout.hpp"
#include "render/ConsoleLayout.hpp"
#include "render/OptionMenuLayout.hpp"
#include "sim/Combat.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace lg {
namespace {

constexpr float kGlyphSize = 8.0F;
constexpr float kTwoPi = 6.28318530718F;
constexpr float kHalfPi = 1.57079632679F;

using CatSprite = std::array<std::string_view, 30>;
constexpr std::size_t kCatSpriteWidth = 35U;

// Visually authored and previewed as a palette-indexed 35x30 calico set.
// Shared markings and compact facial features identify one cat across every
// pose.
constexpr CatSprite kCatIdle = {{
  "",
  "       c                    c",
  "       cc                  cc",
  "       ccpc              ccpc",
  "       ccppc            ccppc",
  "      cccdppccccccccccccdpppc",
  "      cccdddpcqqqqqqqqcdddddcc",
  "      ccddddddqqqqqqqqddddddcc",
  "      ccdddddqqqqqqqqqqdddddcc",
  "     ccggggddqqqqqqqqqddddddcc",
  "     ccggggggdddqqqqqggdddddgcc",
  "    ccggggggggdggqggggggdgggggcc",
  "    ccgggggcccgggqggggcccgggggcc",
  "    ccgggggcccggggggggcccgggggcc",
  "     ccggggcccggggggggcccggggcc",
  "     ccgggggggggccccgggggggggcc",
  "     ccgbbbgggggccccgggggbbbgcc",
  "     cccbbbggggggccggggggbbbccc",
  "      cccgggggccccccccgggggccccc",
  "       ccccgggggggggggggggcccccc",
  "        cccccggggggggggcccccdddcc",
  "         cccccccccccccccccccdddcc",
  "        ccddccccccccccccggcc ddcc",
  "        ccddddddgccgggggggccdddc",
  "        ccddddddgccgggggggccddcc",
  "        ccdcgggcgccgcgggcgccdccc",
  "        ccdcgggcgccgcgggcgcccc",
  "        ccdcgggcgccgcgggcgccc",
  "        cccccccccccccccccccc",
  "           ccccc    ccccc",
}};

constexpr CatSprite kCatIdleBlink = {{
  "",
  "       c                    c",
  "       cc                  cc",
  "       ccpc              ccpc",
  "       ccppc            ccppc",
  "      cccdppccccccccccccdpppc",
  "      cccdddpcqqqqqqqqcdddddcc",
  "      ccddddddqqqqqqqqddddddcc",
  "      ccdddddqqqqqqqqqqdddddcc",
  "     ccggggddqqqqqqqqqddddddcc",
  "     ccggggggdddqqqqqggdddddgcc",
  "    ccggggggggdggqggggggdgggggcc",
  "    ccgggggggggggqggggggggggggcc",
  "    ccggggccccggggggggccccggggcc",
  "     ccggggggggggggggggggggggcc",
  "     ccgggggggggccccgggggggggcc",
  "     ccgbbbgggggccccgggggbbbgcc",
  "     cccbbbggggggccggggggbbbccc",
  "      cccgggggccccccccgggggccccc",
  "       ccccgggggggggggggggcccccc",
  "        cccccggggggggggcccccdddcc",
  "         cccccccccccccccccccdddcc",
  "        ccddccccccccccccggcc ddcc",
  "        ccddddddgccgggggggccdddc",
  "        ccddddddgccgggggggccddcc",
  "        ccdcgggcgccgcgggcgccdccc",
  "        ccdcgggcgccgcgggcgcccc",
  "        ccdcgggcgccgcgggcgccc",
  "        cccccccccccccccccccc",
  "           ccccc    ccccc",
}};

constexpr CatSprite kCatCrouch = {{
  "",
  "",
  "",
  "",
  "",
  "",
  "       c                    c",
  "       cc                  cc",
  "       ccpc              ccpc",
  "       ccppc            ccppc",
  "      cccdppccccccccccccdpppc",
  "      cccdddpcqqqqqqqqcdddddcc",
  "      ccddddddqqqqqqqqddddddcc",
  "      ccdddddqqqqqqqqqqdddddcc",
  "     ccggggddqqqqqqqqqddddddcc",
  "     ccggggggdddqqqqqggdddddgcc",
  "    ccggggggggdggqggggggdgggggcc",
  "    ccgggggcccgggqggggcccgggggcc",
  "    ccgggggcccggggggggcccgggggcc",
  "     ccggggcccggggggggcccggggcc",
  "     ccgggggggggccccgggggggggcc",
  "     ccgbbbgggggccccgggggbbbgcc",
  "     cccbbbggggggccggggggbbbccc",
  "      cccgggggccccccccgggggccccc",
  "       ccccgggggggggggggggcccccc",
  "        cccccggggggggggcccccdddcc",
  "         cccccccccccccccccccdddcc",
  "        ccddccccccccccccggcc ddcc",
  "        ccddddddgccgggggggccdddc",
  "        ccddddddgccgggggggccddcc",
}};

constexpr CatSprite kCatLeap = {{
  "",
  "       c                    c",
  "       cc                  cc",
  "       ccpc              ccpc",
  "       ccppc            ccppc",
  "      cccdppccccccccccccdpppc",
  "      cccdddpcqqqqqqqqcdddddcc",
  "      ccddddddqqqqqqqqddddddcc",
  "      ccdddddqqqqqqqqqqdddddcc",
  "     ccggggddqqqqqqqqqddddddcc",
  "     ccggggggdddqqqqqggdddddgcc",
  "    ccggggggggdggqggggggdgggggcc",
  "    ccgggggcccgggqggggcccgggggcc",
  "    ccgggggcccggggggggcccgggggcc",
  "     ccggggcccggggggggcccggggcc",
  "     ccgggggggggccccgggggggggcc",
  "     ccgbbbgggggccccgggggbbbgcc",
  "     cccbbbggggggccggggggbbbccc",
  "      cccgggggccccccccgggggccccc",
  "       ccccgggggggggggggggcccccc",
  "        cccccggggggggggcccccdddcc",
  "         cccgccccccccccgccccdddcc",
  "          ccggcccccc    ggcc ddcc",
  "          cggc dgccg    cggcdddc",
  "          cggc dgccg     ggccdcc",
  "         cggcc cgccg     cggcccc",
  "         cccc  cgccg      cccc",
  "            c  cgccg      c c",
  "               ccccc",
  "               c",
}};

constexpr CatSprite kCatLieHalf = {{
  "",
  "",
  "",
  "",
  "",
  "",
  "                         c",
  "                         cc",
  "                         cpc   c",
  "                        ccppccccc",
  "                        cpppccqcc",
  "                       ccdpqqqqdcc",
  "                       cdddqqqqddc",
  "                      cgddddqqqddc",
  "                      cdddddqqqddcc",
  "          cccccccccccccgdddggdgggcc",
  "     c   ccddddhhhhhhccggdgggcgggcc",
  "    ccccccdddddhhhhhhhcggggggcgggc",
  "  cccdccdddddddhhhhhhhcggggggggggc",
  "  cdddccdddddddhhhhhhhccgggggggbbc",
  "  cdddccdddddddhhhhhhhcccgggggccc",
  " ccdddccddddddddhhhhhhcgcccccccc",
  " ccddccdddddddddhhhhhcgggcccccc",
  " cccdccdddddddddhhhhhcgggggcc",
  "  cccccddccccccccccccccccggc",
  "  ccccccccggggggggggggggcccc",
  "   c  cccggggggggggggggggcc",
  "       cggggggggggggggggggc",
  "      cccggggggggggggggggccc",
  "       cccccccccccccccccccc",
}};

constexpr CatSprite kCatSleep = {{
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "                      c",
  "                      cc",
  "                     ccgc    c",
  "          ccccccccc  ccpgcccccc",
  "       cccccccccccccccgdpcccgqccc",
  "      ccccdhhhhhhhhhccpddpqqqqdgc",
  "    cccddddhhhhhhhhccgddddqqqqddcc",
  "   cccddddddhhhhhhhcggdddddqqqddgc",
  "   ccdddddddhhhhhhccgggdddgqqqddgcc",
  "  ccddddddddhhhhhhccggggdggggggggcc",
  "  ccdccddddddhhhhhccgccccgggccccccc",
  "  cdccdddddddhhhhhhcggggggggggggcc",
  " ccdcddddddddhhhhhhcgggggcccggggcc",
  " cddcdddddddddhhhhhcccggggcggggccc",
  " cdddcddddddddhhhhhhcccgccccccccc",
  "  cddcccddddddddhhhhggggccggggcc",
  "  cddddcccddddddddhcggggccggggc",
  "  ccccddddddddddddccggggccggggc",
  "     cccdddddddddddccccc  cccc",
  "        ccccccccccc  cc    cc",
}};

constexpr CatSprite kCatProfileWalkA = {{
  "",
  "",
  "",
  "",
  "                         c",
  "                         cc",
  "                         cpc   c",
  "                        ccppccccc",
  "                        cpppccqcc",
  "                       ccdpqqqqdcc",
  "                       cdddqqqqddc",
  "                      cgddddqqqddc",
  "          cccccccccccccdddddqqqddcc",
  "     c   ccddddhhhhhhccgdddggdgggcc",
  "    ccccccdddddhhhhhhccggdgggcgggcc",
  "  cccdccdddddddhhhhhhhcggggggcgggc",
  "  cdddccdddddddhhhhhhhcggggggggggc",
  "  cdddccdddddddhhhhhhhccgggggggbbc",
  " ccdddccddddddddhhhhhhcccgggggccc",
  " ccddccdddddddddhhhhhcggcccccccc",
  " cccdccdddddddddhhhhhcgggcccccc",
  "  cccccdddddddddhhhhhcgggggc",
  "  cccccccdcccccdhhhhhccccccc",
  "   c  cccccgggcdddddhcgggcc",
  "        cccgggccccccccgggc",
  "         ccgggccccccccgggc",
  "          cgggc      cgggc",
  "          ccccc      ccccc",
  "          ccccc      ccccc",
  "",
}};

constexpr CatSprite kCatProfileWalkB = {{
  "",
  "",
  "",
  "",
  "",
  "                         c",
  "                         cc",
  "                         cpc   c",
  "                        ccppccccc",
  "                        cpppccqcc",
  "                       ccdpqqqqdcc",
  "                       cdddqqqqddc",
  "                      cgddddqqqddc",
  "          cccccccccccccdddddqqqddcc",
  "     c   ccddddhhhhhhccgdddggdgggcc",
  "    ccccccdddddhhhhhhccggdgggcgggcc",
  "  cccdccdddddddhhhhhhhcggggggcgggc",
  "  cdddccdddddddhhhhhhhcggggggggggc",
  "  cdddccdddddddhhhhhhhccgggggggbbc",
  " ccdddccddddddddhhhhhhcccgggggccc",
  " ccddccdddddddddhhhhhcggcccccccc",
  " cccdccdddddddddhhhhhcgggcccccc",
  "  cccccdddddddddhhhhhcgggggc",
  "  cccccccdcccccdhhhhhccccccc",
  "   c  cccccgggcdddddhcgggcc",
  "        ccccgggcccccccgggc",
  "         cccgggcccccccgggc",
  "           ccccc     cgggc",
  "           ccccc     ccccc",
  "                     ccccc",
}};

constexpr CatSprite kCatProfileLeap = {{
  "",
  "",
  "                         c",
  "                         cc",
  "                         cpc   c",
  "                        ccppccccc",
  "                        cpppccqcc",
  "                       ccdpqqqqdcc",
  "                       cdddqqqqddc",
  "                      cgddddqqqddc",
  "                      cdddddqqqddcc",
  "                     ccgdddggdgggcc",
  "          cccccccccccccggdgggcgggcc",
  "     c   ccddddhhhhhhhcggggggcgggc",
  "    ccccccdddddhhhhhhhcggggggggggc",
  "  cccdccdddddddhhhhhhhccgggggggbbc",
  "  cdddccdddddddhhhhhhhcccgggggccc",
  "  cdddccdddddddhhhhhhhcgcccccccc",
  " ccdddccddddddddhhhhhhcggcccccc",
  " ccddccdddddddddhhhhhcggggggcc",
  " cccdccdddddddddhhhhhcgggggcc",
  "  cccccdddddddddhhhhhcgggggc",
  "  cccccccdcccccdhhhhhccccgcc",
  "   c  cccccgggcdddddhdgggcc",
  "        ccgggcccccccccccggc",
  "         cgggccccccccccccggcc",
  "         gggcc           ccggc",
  "        ccccc             ccg",
  "        ccccc               c",
  "",
}};

constexpr CatSprite kCatProfileCrouch = {{
  "",
  "",
  "",
  "",
  "",
  "",
  "                         c",
  "                         cc",
  "                         cpc   c",
  "                        ccppccccc",
  "                        cpppccqcc",
  "                       ccdpqqqqdcc",
  "                       cdddqqqqddc",
  "                      cgddddqqqddc",
  "                      cdddddqqqddcc",
  "          cccccccccccccgdddggdgggcc",
  "     c   ccddddhhhhhhccggdgggcgggcc",
  "    ccccccdddddhhhhhhhcggggggcgggc",
  "  cccdccdddddddhhhhhhhcggggggggggc",
  "  cdddccdddddddhhhhhhhccgggggggbbc",
  "  cdddccdddddddhhhhhhhcccgggggccc",
  " ccdddccddddddddhhhhhhcgcccccccc",
  " ccddccdddddddddhhhhhcgggcccccc",
  " cccdccdddddddddhhhhhcgggggcc",
  "  cccccddccccccccccccccccggc",
  "  ccccccccggggggggggggggcccc",
  "   c  cccggggggggggggggggcc",
  "       cggggggggggggggggggc",
  "      cccggggggggggggggggccc",
  "       cccccccccccccccccccc",
}};

[[nodiscard]] const CatSprite& catSprite(
  ConsoleCatAction action,
  std::uint8_t frame,
  bool profile
) {
  if (profile) {
    if (action == ConsoleCatAction::Leap) {
      return kCatProfileLeap;
    }
    if (
      action == ConsoleCatAction::Crouch ||
      action == ConsoleCatAction::Land
    ) {
      return kCatProfileCrouch;
    }
    return frame == 0U ? kCatProfileWalkA : kCatProfileWalkB;
  }
  switch (action) {
  case ConsoleCatAction::Stalk:
    return kCatIdle;
  case ConsoleCatAction::Crouch:
  case ConsoleCatAction::Land:
    return kCatCrouch;
  case ConsoleCatAction::LieDown:
    if (frame == 0U) {
      return kCatCrouch;
    }
    return frame == 1U ? kCatLieHalf : kCatSleep;
  case ConsoleCatAction::Sleep:
    return kCatSleep;
  case ConsoleCatAction::Leap:
    return kCatLeap;
  case ConsoleCatAction::Idle:
    return frame == 0U ? kCatIdle : kCatIdleBlink;
  }
  return kCatIdle;
}

[[nodiscard]] float snappedTextScale(float scale) {
  constexpr std::array<float, 10> pixelHeights = {
    8.0F,
    12.0F,
    16.0F,
    24.0F,
    32.0F,
    48.0F,
    64.0F,
    96.0F,
    128.0F,
    160.0F,
  };
  const float targetHeight = kGlyphSize * std::max(0.1F, scale);
  float nearest = pixelHeights.front();
  float nearestDistance = std::abs(targetHeight - nearest);
  for (const float pixelHeight : pixelHeights) {
    const float distance = std::abs(targetHeight - pixelHeight);
    if (distance < nearestDistance) {
      nearest = pixelHeight;
      nearestDistance = distance;
    }
  }
  return nearest / kGlyphSize;
}

[[nodiscard]] float textWidth(std::string_view text, float scale) {
  return static_cast<float>(utf8GlyphCount(text)) *
    kGlyphSize *
    snappedTextScale(scale);
}

[[nodiscard]] std::string trimCell(std::string_view value) {
  while (!value.empty() && value.front() == ' ') {
    value.remove_prefix(1U);
  }
  while (!value.empty() && value.back() == ' ') {
    value.remove_suffix(1U);
  }
  return std::string(value);
}

[[nodiscard]] std::string signedScore(PlayerScore score) {
  return score >= 0
    ? "+" + std::to_string(score)
    : std::to_string(score);
}

[[nodiscard]] std::vector<std::string>
wrapOptionMenuFooter(std::string_view text, std::size_t maxGlyphs) {
  constexpr std::size_t maxLines = 2U;
  std::vector<std::string> lines;
  maxGlyphs = std::max<std::size_t>(1U, maxGlyphs);
  while (!text.empty() && lines.size() < maxLines) {
    if (utf8GlyphCount(text) <= maxGlyphs) {
      lines.push_back(trimCell(text));
      text = {};
      break;
    }
    const std::size_t hardEnd = utf8ByteOffsetForGlyph(text, maxGlyphs);
    std::size_t breakAt = text.rfind(' ', hardEnd);
    if (breakAt == std::string_view::npos || breakAt == 0U) {
      breakAt = hardEnd;
    }
    lines.push_back(trimCell(text.substr(0U, breakAt)));
    text.remove_prefix(breakAt);
    while (!text.empty() && text.front() == ' ') {
      text.remove_prefix(1U);
    }
  }
  if (!text.empty() && !lines.empty()) {
    constexpr std::string_view ellipsis = "...";
    std::string &last = lines.back();
    const std::size_t available =
        maxGlyphs > ellipsis.size() ? maxGlyphs - ellipsis.size() : 0U;
    if (utf8GlyphCount(last) > available) {
      last.resize(utf8ByteOffsetForGlyph(last, available));
    }
    last += ellipsis;
  }
  return lines;
}

[[nodiscard]] float countdownGlyphOffsetX(
  const std::string& text,
  float scale
) {
  const char finalDigit = text.back();
  return (finalDigit == '0' || finalDigit == '4' ? 0.5F : 1.0F) * scale;
}

void addRect(
  DrawList2D& drawList,
  float x,
  float y,
  float width,
  float height,
  RenderColor color
) {
  const std::array<ScreenPoint, 4> points = {{
    {x, y},
    {x + width, y},
    {x + width, y + height},
    {x, y + height},
  }};
  drawList.overlayCommands.emplace_back(FilledQuad2D{points, color});
}

void addImage(
  DrawList2D& drawList,
  HudImage image,
  ScreenRect destination,
  RenderColor color = {},
  ScreenRect source = {0.0F, 0.0F, 1.0F, 1.0F}
) {
  if (destination.width <= 0.0F || destination.height <= 0.0F ||
      source.width <= 0.0F || source.height <= 0.0F) {
    return;
  }
  drawList.overlayCommands.emplace_back(Image2D{
    image,
    destination,
    source,
    color,
  });
}

void addLine(
  DrawList2D& drawList,
  ScreenPoint start,
  ScreenPoint end,
  RenderColor color,
  float width
) {
  drawList.overlayCommands.emplace_back(Line2D{
    start,
    end,
    color,
    width,
  });
}

void addDiamond(
  DrawList2D& drawList,
  ScreenPoint center,
  float radius,
  RenderColor color
) {
  const std::array<ScreenPoint, 4> points = {{
    {center.x, center.y - radius},
    {center.x + radius, center.y},
    {center.x, center.y + radius},
    {center.x - radius, center.y},
  }};
  drawList.overlayCommands.emplace_back(FilledQuad2D{points, color});
}

void addFreezeBeamPuffs(
  DrawList2D& drawList,
  ScreenPoint start,
  ScreenPoint end,
  float scale,
  float phaseRadians
) {
  constexpr int kPuffCount = 14;
  const float dx = end.x - start.x;
  const float dy = end.y - start.y;
  const float beamLength = std::max(1.0F, std::sqrt(dx * dx + dy * dy));
  const float normalX = -dy / beamLength;
  const float normalY = dx / beamLength;
  for (int index = 0; index < kPuffCount; ++index) {
    const float t = (static_cast<float>(index) + 0.35F) /
      static_cast<float>(kPuffCount);
    const float phase = static_cast<float>(index) * 1.713F + phaseRadians * 0.22F;
    const float side =
      std::sin(phase) * (18.0F + 16.0F * std::sin(phase * 0.47F));
    const float drift = std::cos(phase * 0.71F) * 8.0F;
    const float alongX = start.x + dx * t;
    const float alongY = start.y + dy * t;
    const float radius =
      (2.4F + std::fmod(static_cast<float>(index * 7), 4.0F)) *
      scale;
    const std::uint8_t alpha = static_cast<std::uint8_t>(
      std::clamp(34.0F + std::sin(phase) * 16.0F, 16.0F, 58.0F)
    );
    addDiamond(
      drawList,
      {
        alongX + normalX * side * scale + normalX * drift * scale,
        alongY + normalY * side * scale + normalY * drift * scale,
      },
      radius,
      {220, 248, 255, alpha}
    );
  }
}

void addLayeredFreezeBeam2D(
  DrawList2D& drawList,
  ScreenPoint start,
  ScreenPoint end,
  float scale,
  const RenderSettings& settings
) {
  const float active = std::clamp(settings.freezeGunFiringAmount, 0.0F, 1.0F);
  const float flash = std::clamp(
    settings.freezeGunActivationFlashAmount,
    0.0F,
    1.0F
  );

  addLine(drawList, start, end, {82, 203, 255, 42}, settings.beamWidth * 5.2F);
  // Keep the local beam aim-readable: every line follows the same current
  // muzzle-to-crosshair axis. The puffs below provide the moving detail.
  addLine(drawList, start, end, {238, 253, 255, 245}, std::max(1.4F, settings.beamWidth * 0.72F));
  addFreezeBeamPuffs(drawList, start, end, scale, settings.beamPhaseRadians);

  addDiamond(drawList, start, (7.0F + active * 3.0F + flash * 9.0F) * scale, {
    220,
    251,
    255,
    static_cast<std::uint8_t>(105.0F + flash * 105.0F),
  });
}

void addText(
  DrawList2D& drawList,
  float x,
  float y,
  std::string text,
  RenderColor color,
  float scale,
  TextHorizontalAlignment horizontalAlignment = TextHorizontalAlignment::Left
) {
  drawList.overlayCommands.emplace_back(Text2D{
    {x, y},
    std::move(text),
    color,
    scale,
    horizontalAlignment,
  });
}

[[nodiscard]] RenderColor networkQualityColor(
  float value,
  float warning,
  float critical
) {
  if (value >= critical) return {255, 92, 92, 255};
  if (value >= warning) return {255, 210, 78, 255};
  return {112, 232, 142, 255};
}

void addNetGraph(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState::NetGraphState& state
) {
  if (state.mode <= 0 || !state.telemetry.valid) return;

  const bool expanded = state.mode >= 2;
  const float basePanelWidth = expanded ? 460.0F : 252.0F;
  const float basePanelHeight = expanded ? 395.0F : 201.0F;
  const float availableWidth = std::max(1.0F, static_cast<float>(width) - 24.0F);
  const float availableHeight = std::max(1.0F, static_cast<float>(height) - 24.0F);
  const float fitScale = std::min(
    availableWidth / basePanelWidth,
    availableHeight / basePanelHeight
  );
  // The cvar controls the preferred size; tiny windows constrain it so the
  // diagnostics remain fully visible instead of spilling off-screen.
  const float scale = std::max(
    0.5F,
    std::min(std::clamp(state.scale, 0.75F, 3.0F), fitScale)
  );
  const float panelWidth = basePanelWidth * scale;
  const float panelHeight = basePanelHeight * scale;
  const float margin = 12.0F * scale;
  const float x = std::max(margin, static_cast<float>(width) - panelWidth - margin);
  const float maximumY = std::max(margin, static_cast<float>(height) - panelHeight - margin);
  const float y = std::clamp(
    static_cast<float>(height) * 0.18F,
    margin,
    maximumY
  );
  const float textScale = scale;
  const float rowHeight = 17.0F * scale;
  const RenderColor label = {190, 203, 214, 255};
  const RenderColor value = {238, 244, 248, 255};

  addRect(drawList, x, y, panelWidth, panelHeight, {7, 12, 17, 206});
  addRect(drawList, x, y, 3.0F * scale, panelHeight, {64, 180, 224, 230});
  addText(drawList, x + margin, y + 9.0F * scale, "NETWORK", {128, 220, 255, 255}, textScale);
  const bool interrupted = state.telemetry.snapshotAgeMilliseconds >= 1000.0F;
  const bool unstable =
    state.telemetry.incomingLossPercent >= 2.0F ||
    state.telemetry.outgoingLossPercent >= 2.0F ||
    state.telemetry.snapshotJitterMilliseconds >= 10.0F ||
    state.telemetry.snapshotAgeMilliseconds >=
      std::max(40.0F, state.interpolationEffectiveDelayMilliseconds * 2.0F);
  addText(
    drawList,
    x + panelWidth - margin,
    y + 9.0F * scale,
    interrupted ? "INTERRUPTED" : unstable ? "UNSTABLE" : "HEALTHY",
    interrupted || unstable
      ? RenderColor{255, 92, 92, 255}
      : RenderColor{112, 232, 142, 255},
    textScale,
    TextHorizontalAlignment::Right
  );

  float rowY = y + 31.0F * scale;
  const auto addMetric = [&](const char* name, const char* format,
                             float number, RenderColor numberColor) {
    char text[64];
    std::snprintf(text, sizeof(text), format, number);
    addText(drawList, x + margin, rowY, name, label, textScale);
    addText(
      drawList,
      x + panelWidth - margin,
      rowY,
      text,
      numberColor,
      textScale,
      TextHorizontalAlignment::Right
    );
    rowY += rowHeight;
  };
  const auto addTextMetric = [&](const char* name, const char* text,
                                 RenderColor textColor) {
    addText(drawList, x + margin, rowY, name, label, textScale);
    addText(
      drawList,
      x + panelWidth - margin,
      rowY,
      text,
      textColor,
      textScale,
      TextHorizontalAlignment::Right
    );
    rowY += rowHeight;
  };

  addMetric("PING", "%.1f ms", state.telemetry.pingMilliseconds,
            networkQualityColor(state.telemetry.pingMilliseconds, 80.0F, 140.0F));
  addMetric("JITTER", "%.1f ms", state.telemetry.snapshotJitterMilliseconds,
            networkQualityColor(state.telemetry.snapshotJitterMilliseconds, 4.0F, 10.0F));
  addMetric("LOSS IN", "%.1f%%", state.telemetry.incomingLossPercent,
            networkQualityColor(state.telemetry.incomingLossPercent, 0.5F, 2.0F));
  addMetric("LOSS OUT", "%.1f%%", state.telemetry.outgoingLossPercent,
            networkQualityColor(state.telemetry.outgoingLossPercent, 0.5F, 2.0F));
  addMetric("SNAP RATE", "%.0f /s", state.telemetry.snapshotRate,
            networkQualityColor(125.0F - state.telemetry.snapshotRate, 5.0F, 20.0F));
  addMetric(
    "INTERP",
    "%.0f ms",
    state.interpolationEffectiveDelayMilliseconds,
    value
  );
  char lead[48];
  std::snprintf(
    lead,
    sizeof(lead),
    "%.2f / %.2f tk",
    state.interpolationBufferLeadTicks,
    state.interpolationDesiredBufferLeadTicks
  );
  addTextMetric("LEAD/TARGET", lead, value);
  addMetric("PKT AGE", "%.1f ms", state.telemetry.snapshotAgeMilliseconds,
            networkQualityColor(
              state.telemetry.snapshotAgeMilliseconds,
              std::max(16.0F, state.interpolationEffectiveDelayMilliseconds),
              std::max(
                40.0F,
                state.interpolationEffectiveDelayMilliseconds * 2.0F
              )
            ));

  if (!expanded) return;

  rowY += 3.0F * scale;
  const float detailStartY = rowY;
  char detail[96];
  std::snprintf(
    detail,
    sizeof(detail),
    "BW IN/OUT  %.0f / %.0f kbit",
    state.telemetry.incomingKilobitsPerSecond,
    state.telemetry.outgoingKilobitsPerSecond
  );
  addText(drawList, x + margin, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "ERROR %+.2f tk  RATE %.3fx",
    state.interpolationTimelineErrorTicks,
    state.interpolationPlaybackRate
  );
  addText(drawList, x + margin, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "DELAY %.1f ms  SNAPS %zu",
    state.interpolationEffectiveDelayMilliseconds,
    state.interpolationBufferedSnapshotCount
  );
  addText(drawList, x + margin, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "PLAY %s  UNDERRUN %s",
    state.interpolationPlaybackStarted ? "ON" : "WAIT",
    state.interpolationUnderrun ? "ACTIVE" : "CLEAR"
  );
  addText(
    drawList,
    x + margin,
    rowY,
    detail,
    state.interpolationUnderrun
      ? RenderColor{255, 112, 202, 255}
      : value,
    textScale
  );
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "EVENTS UNDER %u  HARD %u",
    state.interpolationUnderrunCount,
    state.interpolationHardCorrectionCount
  );
  addText(drawList, x + margin, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "TICK P/N %.2f / %.0f",
    state.interpolationPresentationTick,
    state.interpolationNewestSnapshotTick
  );
  addText(drawList, x + margin, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "COLLISION TICK %u  %s",
    state.interpolationSampleTick,
    state.interpolationSampleEligible ? "VALID" : "NONE"
  );
  addText(drawList, x + margin, rowY, detail, value, textScale);
  const float secondDetailX = x + 230.0F * scale;
  rowY = detailStartY;
  std::snprintf(
    detail,
    sizeof(detail),
    "BYTES SNAP/CMD  %zu / %zu",
    state.telemetry.lastSnapshotBytes,
    state.telemetry.lastCommandBytes
  );
  addText(drawList, secondDetailX, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "PENDING %zu  QUEUE %zu",
    state.pendingCommands,
    state.snapshotQueueDepth
  );
  addText(drawList, secondDetailX, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "CORR %.3f  AVG %.3f  MAX %.3f",
    state.lastCorrectionDistance,
    [&state]() {
      constexpr float kVisibleCorrectionMinimum = 0.001F;
      float total = 0.0F;
      std::size_t correctionSamples = 0;
      for (std::size_t index = 0; index < state.telemetry.historyCount; ++index) {
        const float distance =
          state.telemetry.history[index].predictionCorrectionDistance;
        if (distance >= kVisibleCorrectionMinimum) {
          total += distance;
          ++correctionSamples;
        }
      }
      return correctionSamples > 0
        ? total / static_cast<float>(correctionSamples)
        : 0.0F;
    }(),
    [&state]() {
      float maximum = 0.0F;
      for (std::size_t index = 0; index < state.telemetry.historyCount; ++index) {
        maximum = std::max(
          maximum,
          state.telemetry.history[index].predictionCorrectionDistance
        );
      }
      return maximum;
    }()
  );
  addText(
    drawList,
    secondDetailX,
    rowY,
    detail,
    {112, 174, 255, 255},
    textScale
  );
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "REWIND %u / %u ticks",
    state.requestedRewindTicks,
    state.appliedRewindTicks
  );
  addText(drawList, secondDetailX, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "RTT VAR %.1f ms",
    state.telemetry.pingVariationMilliseconds
  );
  addText(drawList, secondDetailX, rowY, detail, value, textScale);
  rowY += rowHeight;
  std::snprintf(
    detail,
    sizeof(detail),
    "LATE %llu  REORDER %llu",
    static_cast<unsigned long long>(state.telemetry.lateSnapshots),
    static_cast<unsigned long long>(state.telemetry.reorderedSnapshots)
  );
  addText(drawList, secondDetailX, rowY, detail, value, textScale);

  const float graphHeight = 68.0F * scale;
  const float graphX = x + margin;
  const float graphY = y + panelHeight - graphHeight - 25.0F * scale;
  const float graphWidth = panelWidth - 2.0F * margin;
  addRect(drawList, graphX, graphY, graphWidth, graphHeight, {2, 5, 8, 220});
  addLine(
    drawList,
    {graphX, graphY + graphHeight - 1.0F},
    {graphX + graphWidth, graphY + graphHeight - 1.0F},
    {75, 91, 102, 210},
    scale
  );

  const std::size_t count = state.telemetry.historyCount;
  const float columnWidth = graphWidth /
    static_cast<float>(kNetworkTelemetryHistorySamples);
  const std::size_t firstColumn = kNetworkTelemetryHistorySamples - count;
  for (std::size_t index = 0; index < count; ++index) {
    const NetworkTelemetrySample& sample = state.telemetry.history[index];
    const float columnX = graphX +
      static_cast<float>(firstColumn + index) * columnWidth;
    RenderColor color = {82, 213, 122, 230};
    float barHeight = std::clamp(
      (3.0F + sample.snapshotJitterMilliseconds * 2.0F) * scale,
      3.0F * scale,
      graphHeight - 2.0F * scale
    );
    if (sample.snapshotGaps > 0 || sample.incomingLossPercent >= 2.0F) {
      color = {244, 72, 72, 240};
      barHeight = graphHeight - 2.0F * scale;
    } else if (sample.interpolationUnderrun) {
      color = {255, 80, 190, 245};
      barHeight = std::max(barHeight, graphHeight * 0.72F);
    } else if (sample.lateSnapshots > 0 ||
               sample.snapshotJitterMilliseconds >= 4.0F) {
      color = {246, 195, 68, 235};
    }
    addRect(
      drawList,
      columnX,
      graphY + graphHeight - barHeight,
      std::max(scale, columnWidth),
      barHeight,
      color
    );
    if (sample.outgoingLossPercent >= 0.5F) {
      addRect(
        drawList,
        columnX,
        graphY,
        std::max(scale, columnWidth),
        3.0F * scale,
        {255, 143, 58, 255}
      );
    }
    if (sample.interpolationHardCorrection) {
      // Controller events are attached only to the sample where their counters
      // advance, keeping a held timeline from painting every later column.
      addRect(
        drawList,
        columnX,
        graphY,
        std::max(scale, columnWidth),
        5.0F * scale,
        {64, 220, 255, 255}
      );
    }
    constexpr float kVisibleCorrectionMinimum = 0.001F;
    if (sample.predictionCorrectionDistance >= kVisibleCorrectionMinimum) {
      // A logarithmic scale preserves distinction among small corrections
      // without letting one large reconciliation cover the complete graph.
      const float magnitude = std::clamp(
        std::log1p(
          sample.predictionCorrectionDistance / kVisibleCorrectionMinimum
        ) / std::log1p(0.25F / kVisibleCorrectionMinimum),
        0.0F,
        1.0F
      );
      // Corrections are frequent during normal movement. Keep them legible as
      // a magnitude trace without letting them dominate loss/starvation events.
      const float correctionHeight =
        (1.5F + magnitude * 18.0F) * scale;
      const RenderColor correctionColor = {
        static_cast<std::uint8_t>(70.0F + 20.0F * magnitude),
        static_cast<std::uint8_t>(150.0F - 30.0F * magnitude),
        255,
        static_cast<std::uint8_t>(150.0F + 105.0F * magnitude),
      };
      addRect(
        drawList,
        columnX,
        graphY + graphHeight - correctionHeight,
        std::max(scale, columnWidth),
        correctionHeight,
        correctionColor
      );
    }
    if (sample.interpolationUnderrun) {
      // Keep the edge visible even when loss owns the column's main severity
      // color or a prediction-correction trace overlaps its lower portion.
      addRect(
        drawList,
        columnX,
        graphY + graphHeight * 0.46F,
        std::max(scale, columnWidth),
        3.0F * scale,
        {255, 80, 190, 255}
      );
    }
  }
  const float legendY = graphY + graphHeight + 3.0F * scale;
  float legendX = graphX;
  const auto addLegendLabel = [&drawList, &legendX, scale](
    float rowY,
    std::string_view text,
    RenderColor color
  ) {
    addText(drawList, legendX, rowY, std::string{text}, color, scale);
    legendX += (static_cast<float>(text.size()) + 1.0F) * kGlyphSize * scale;
  };
  addLegendLabel(legendY, "10S", {132, 148, 160, 255});
  addLegendLabel(legendY, "LOSS", {244, 72, 72, 255});
  addLegendLabel(legendY, "LATE", {246, 195, 68, 255});
  addLegendLabel(legendY, "UNDER", {255, 80, 190, 255});
  addLegendLabel(legendY, "HARD", {64, 220, 255, 255});
  addLegendLabel(legendY, "PRED", {112, 120, 255, 255});
}

[[nodiscard]] std::uint8_t blendChannel(
  std::uint8_t base,
  std::uint8_t highlight,
  float amount
) {
  return static_cast<std::uint8_t>(
    std::clamp(
      static_cast<float>(base) +
        (static_cast<float>(highlight) - static_cast<float>(base)) * amount,
      0.0F,
      255.0F
    )
  );
}

[[nodiscard]] RenderColor lerpColor(
  RenderColor a,
  RenderColor b,
  float amount
) {
  const float t = std::clamp(amount, 0.0F, 1.0F);
  return {
    blendChannel(a.red, b.red, t),
    blendChannel(a.green, b.green, t),
    blendChannel(a.blue, b.blue, t),
    blendChannel(a.alpha, b.alpha, t),
  };
}

[[nodiscard]] RenderColor localHealthFillColor(float healthRatio) {
  constexpr RenderColor red = {220, 38, 38, 255};
  constexpr RenderColor yellow = {228, 206, 42, 255};
  constexpr RenderColor green = {64, 214, 34, 255};
  const float ratio = std::clamp(healthRatio, 0.0F, 1.0F);
  if (ratio >= 0.75F) {
    return green;
  }
  if (ratio >= 0.5F) {
    return lerpColor(yellow, green, (ratio - 0.5F) / 0.25F);
  }
  return lerpColor(red, yellow, ratio / 0.5F);
}

void addOutline(
  DrawList2D& drawList,
  float x,
  float y,
  float width,
  float height,
  RenderColor color
) {
  addLine(drawList, {x, y}, {x + width, y}, color, 1.0F);
  addLine(
    drawList,
    {x + width, y},
    {x + width, y + height},
    color,
    1.0F
  );
  addLine(
    drawList,
    {x + width, y + height},
    {x, y + height},
    color,
    1.0F
  );
  addLine(drawList, {x, y + height}, {x, y}, color, 1.0F);
}

struct NavigationSafeBounds {
  float left = 0.0F;
  float right = 0.0F;
  float top = 0.0F;
  float bottom = 0.0F;
};

[[nodiscard]] NavigationSafeBounds navigationSafeBounds(
  int outputWidth,
  int outputHeight
) {
  const float width = static_cast<float>(std::max(1, outputWidth));
  const float height = static_cast<float>(std::max(1, outputHeight));
  const float horizontalMargin = std::min(
    width * 0.25F,
    std::max(24.0F, width * 0.06F)
  );
  const float verticalMargin = std::min(
    height * 0.30F,
    std::max(56.0F, height * 0.14F)
  );
  return {
    horizontalMargin,
    std::max(horizontalMargin, width - horizontalMargin),
    verticalMargin,
    std::max(verticalMargin, height - verticalMargin),
  };
}

[[nodiscard]] ScreenPoint navigationScreenDirection(
  ScreenPoint center,
  ScreenPoint edge
) {
  const float x = edge.x - center.x;
  const float y = edge.y - center.y;
  const float magnitude = std::hypot(x, y);
  if (magnitude <= 0.0001F) {
    return {0.0F, -1.0F};
  }
  return {x / magnitude, y / magnitude};
}

void addNavigationArrow(
  DrawList2D& drawList,
  ScreenPoint center,
  ScreenPoint edge,
  RenderColor color
) {
  const ScreenPoint direction = navigationScreenDirection(center, edge);
  const ScreenPoint perpendicular = {-direction.y, direction.x};
  const ScreenPoint tip = {
    edge.x + direction.x * 11.0F,
    edge.y + direction.y * 11.0F,
  };
  const ScreenPoint left = {
    edge.x - direction.x * 7.0F + perpendicular.x * 7.0F,
    edge.y - direction.y * 7.0F + perpendicular.y * 7.0F,
  };
  const ScreenPoint back = {
    edge.x - direction.x * 5.0F,
    edge.y - direction.y * 5.0F,
  };
  const ScreenPoint right = {
    edge.x - direction.x * 7.0F - perpendicular.x * 7.0F,
    edge.y - direction.y * 7.0F - perpendicular.y * 7.0F,
  };
  drawList.overlayCommands.emplace_back(FilledQuad2D{
    {tip, left, back, right},
    color,
  });
}

[[nodiscard]] ScreenPoint clampedNavigationEdgePosition(
  const McGuffinNavigationProjection& projection,
  const NavigationSafeBounds& bounds
) {
  ScreenPoint edgePosition = projection.edgePosition;
  constexpr float arrowInset = 12.0F;
  const float edgeInsetX = std::min(
    arrowInset,
    (bounds.right - bounds.left) * 0.5F
  );
  const float edgeInsetY = std::min(
    arrowInset,
    (bounds.bottom - bounds.top) * 0.5F
  );
  edgePosition.x = std::clamp(
    edgePosition.x,
    bounds.left + edgeInsetX,
    bounds.right - edgeInsetX
  );
  edgePosition.y = std::clamp(
    edgePosition.y,
    bounds.top + edgeInsetY,
    bounds.bottom - edgeInsetY
  );
  return edgePosition;
}

[[nodiscard]] RenderColor mcguffinNavigationColor(
  McGuffinNavigationKind kind
) {
  switch (kind) {
  case McGuffinNavigationKind::InstallBase:
  case McGuffinNavigationKind::DefendBase:
    return {112, 232, 142, 245};
  case McGuffinNavigationKind::AttackBase:
    return {255, 126, 102, 245};
  case McGuffinNavigationKind::RecoverObjective:
    return {255, 198, 88, 245};
  case McGuffinNavigationKind::FollowCarrier:
    return {116, 202, 255, 245};
  case McGuffinNavigationKind::Objective:
    return {255, 224, 96, 245};
  case McGuffinNavigationKind::None:
    break;
  }
  return {235, 242, 250, 245};
}

[[nodiscard]] std::string_view mcguffinNavigationCompactLabel(
  McGuffinNavigationKind kind
) {
  switch (kind) {
  case McGuffinNavigationKind::Objective:
    return "OBJ";
  case McGuffinNavigationKind::RecoverObjective:
    return "GET";
  case McGuffinNavigationKind::FollowCarrier:
    return "C";
  case McGuffinNavigationKind::InstallBase:
    return "IN";
  case McGuffinNavigationKind::DefendBase:
    return "DEF";
  case McGuffinNavigationKind::AttackBase:
    return "ATK";
  case McGuffinNavigationKind::None:
    break;
  }
  return {};
}

[[nodiscard]] std::string_view mcguffinNavigationMicroLabel(
  McGuffinNavigationKind kind
) {
  switch (kind) {
  case McGuffinNavigationKind::Objective:
    return "O";
  case McGuffinNavigationKind::RecoverObjective:
    return "R";
  case McGuffinNavigationKind::FollowCarrier:
    return "C";
  case McGuffinNavigationKind::InstallBase:
    return "I";
  case McGuffinNavigationKind::DefendBase:
    return "D";
  case McGuffinNavigationKind::AttackBase:
    return "A";
  case McGuffinNavigationKind::None:
    break;
  }
  return {};
}

[[nodiscard]] std::string mcguffinNavigationText(
  float distance,
  std::string_view label
) {
  const long roundedDistance = std::max(0L, std::lround(std::max(0.0F, distance)));
  return std::string(label) +
    " " + std::to_string(roundedDistance) + "u";
}

void addMcGuffinNavigation(
  DrawList2D& drawList,
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
  const HudRenderState& hud
) {
  if (
    !hud.mcguffinNavigation.active ||
    hud.mcguffinNavigation.kind == McGuffinNavigationKind::None ||
    hud.settingsOpen ||
    hud.miscMenuOpen ||
    hud.trainerMenuOpen ||
    hud.scoreboardOpen
  ) {
    return;
  }
  const McGuffinNavigationProjection projection =
    projectMcGuffinNavigationTarget(
      hud.mcguffinNavigation,
      camera,
      outputWidth,
      outputHeight
    );
  if (!projection.valid) {
    return;
  }

  const ScreenPoint screenCenter = {
    static_cast<float>(outputWidth) * 0.5F,
    static_cast<float>(outputHeight) * 0.5F,
  };
  const RenderColor color = mcguffinNavigationColor(
    hud.mcguffinNavigation.kind
  );
  constexpr float textScale = 1.25F;
  NavigationSafeBounds bounds = navigationSafeBounds(
    outputWidth,
    outputHeight
  );
  const std::size_t topLineCount = std::max({
    hud.topLeftLines.size(),
    hud.topCenterLines.size(),
    hud.topRightLines.size(),
  });
  const float topHudBottom = topLineCount == 0U
    ? 0.0F
    : 12.0F + static_cast<float>(topLineCount) * 20.0F - 4.0F;
  // Reserve the same top band for the card that the existing HUD uses.
  bounds.top = std::max(bounds.top, topHudBottom + 12.0F);
  bounds.bottom = std::max(bounds.top, bounds.bottom);

  const float safeWidth = std::max(0.0F, bounds.right - bounds.left);
  const auto cardWidthFor = [](
    std::string_view value,
    float cardPadding
  ) {
    return textWidth(value, textScale) + cardPadding * 2.0F;
  };
  std::string text = mcguffinNavigationText(
    projection.distance,
    mcguffinNavigationLabel(hud.mcguffinNavigation.kind)
  );
  float padding = 6.0F;
  float panelWidth = cardWidthFor(text, padding);
  if (panelWidth > safeWidth) {
    text = mcguffinNavigationText(
      projection.distance,
      mcguffinNavigationCompactLabel(hud.mcguffinNavigation.kind)
    );
    padding = 4.0F;
    panelWidth = cardWidthFor(text, padding);
  }
  if (panelWidth > safeWidth) {
    text = mcguffinNavigationText(
      projection.distance,
      mcguffinNavigationMicroLabel(hud.mcguffinNavigation.kind)
    );
    padding = 2.0F;
    panelWidth = cardWidthFor(text, padding);
  }
  const float panelHeight =
    kGlyphSize * snappedTextScale(textScale) + padding * 2.0F;
  const float maximumPanelTop = std::max(
    0.0F,
    static_cast<float>(outputHeight) - panelHeight - 8.0F
  );
  // Keep both the card and the arrow below the existing top HUD. The extra
  // margin also keeps the arrow tip from touching the last status line.
  bounds.top = std::min(bounds.top, maximumPanelTop);
  bounds.bottom = std::max(bounds.top, bounds.bottom);
  const float availableHeight = std::max(0.0F, bounds.bottom - bounds.top);
  const bool cardFits =
    panelWidth <= safeWidth && panelHeight <= availableHeight;
  ScreenPoint edgePosition = {};
  if (!projection.onScreen) {
    edgePosition = clampedNavigationEdgePosition(projection, bounds);
    addNavigationArrow(drawList, screenCenter, edgePosition, color);
  }
  if (!cardFits) {
    // An extremely small viewport cannot hold the label and distance while
    // keeping the card inside the safe area. Keep the directional arrow for
    // off-screen targets and omit only the impossible card.
    return;
  }

  const auto safeClamp = [](float value, float lower, float upper) {
    if (lower > upper) {
      return (lower + upper) * 0.5F;
    }
    return std::clamp(value, lower, upper);
  };
  const float minimumCardCenterX = bounds.left + panelWidth * 0.5F;
  const float maximumCardCenterX = bounds.right - panelWidth * 0.5F;
  const float minimumCardCenterY = bounds.top + panelHeight * 0.5F;
  const float maximumCardCenterY = bounds.bottom - panelHeight * 0.5F;
  ScreenPoint textPosition = {};
  TextHorizontalAlignment textAlignment = TextHorizontalAlignment::Center;
  if (projection.onScreen) {
    textPosition = projection.screenPosition;
    textPosition.y -= panelHeight + 7.0F;
    const bool nearCrosshair =
      std::fabs(projection.screenPosition.x - screenCenter.x) < 84.0F &&
      std::fabs(projection.screenPosition.y - screenCenter.y) < 64.0F;
    if (nearCrosshair) {
      textPosition = {
        screenCenter.x,
        minimumCardCenterY,
      };
    }
    textPosition.x = safeClamp(
      textPosition.x,
      minimumCardCenterX,
      maximumCardCenterX
    );
    textPosition.y = safeClamp(
      textPosition.y,
      minimumCardCenterY,
      maximumCardCenterY
    );
  } else {
    const bool left = projection.edgePosition.x <= screenCenter.x - 1.0F &&
      std::fabs(projection.edgePosition.x - bounds.left) < 1.0F;
    const bool right = projection.edgePosition.x >= screenCenter.x + 1.0F &&
      std::fabs(projection.edgePosition.x - bounds.right) < 1.0F;
    if (left) {
      textAlignment = TextHorizontalAlignment::Left;
      textPosition = {
        edgePosition.x + 16.0F,
        edgePosition.y,
      };
    } else if (right) {
      textAlignment = TextHorizontalAlignment::Right;
      textPosition = {
        edgePosition.x - 16.0F,
        edgePosition.y,
      };
    } else if (edgePosition.y <= screenCenter.y) {
      textPosition = {
        edgePosition.x,
        edgePosition.y + 16.0F + panelHeight * 0.5F,
      };
    } else {
      textPosition = {
        edgePosition.x,
        edgePosition.y - 16.0F - panelHeight * 0.5F,
      };
    }
    textPosition.x = safeClamp(
      textPosition.x,
      textAlignment == TextHorizontalAlignment::Left
        ? bounds.left + padding
        : textAlignment == TextHorizontalAlignment::Right
          ? bounds.left + panelWidth - padding
          : minimumCardCenterX,
      textAlignment == TextHorizontalAlignment::Left
        ? bounds.right - panelWidth + padding
        : textAlignment == TextHorizontalAlignment::Right
          ? bounds.right - padding
          : maximumCardCenterX
    );
    textPosition.y = safeClamp(
      textPosition.y,
      minimumCardCenterY,
      maximumCardCenterY
    );
  }

  const float panelX = textPosition.x -
    (textAlignment == TextHorizontalAlignment::Left
      ? padding
      : textAlignment == TextHorizontalAlignment::Right
        ? panelWidth - padding
        : panelWidth * 0.5F);
  const float panelY = textPosition.y - panelHeight * 0.5F;
  addRect(
    drawList,
    panelX,
    panelY,
    panelWidth,
    panelHeight,
    {7, 12, 17, 212}
  );
  addOutline(drawList, panelX, panelY, panelWidth, panelHeight, color);
  addText(
    drawList,
    textPosition.x,
    textPosition.y - kGlyphSize * snappedTextScale(textScale) * 0.5F,
    text,
    color,
    textScale,
    textAlignment
  );
}


void addLocalHealthBar(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings,
  float bottomY
) {
  (void)height;
  const float scale = std::clamp(settings.healthTextScale, 0.5F, 20.0F);
  const float labelScale = std::max(0.75F, scale * 0.72F);
  const std::string value =
    std::to_string(std::max(0, localPlayer.health)) + " / " +
    std::to_string(std::max(1, hud.healthAmount));
  const float valueWidth = textWidth(value, labelScale);
  const float barWidth = std::min(
    static_cast<float>(width) - 24.0F,
    std::max(60.0F * scale, valueWidth + 8.0F * scale)
  );
  const float barHeight = 13.0F * scale;
  const float border = std::max(1.0F, std::round(1.0F * scale));
  const float padding = std::max(2.0F, std::round(2.0F * scale));
  const float labelHeight = kGlyphSize * labelScale;
  const float labelGap = std::max(4.0F, 3.0F * scale);
  const float totalHeight = labelHeight + labelGap + barHeight + border * 2.0F;
  const float x = std::min(
    24.0F * scale,
    std::max(12.0F, static_cast<float>(width) - barWidth - 12.0F)
  );
  const float y = bottomY - totalHeight;
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio =
    std::clamp(static_cast<float>(localPlayer.health) / maxHealth, 0.0F, 1.0F);
  addText(drawList, x, y, "HP", {240, 246, 252, 255}, labelScale);
  addText(
    drawList,
    x + barWidth - valueWidth,
    y,
    value,
    {240, 246, 252, 255},
    labelScale
  );
  const float barY = y + labelHeight + labelGap + border;
  addRect(
    drawList,
    x - border,
    barY - border,
    barWidth + border * 2.0F,
    barHeight + border * 2.0F,
    {5, 16, 9, 210}
  );
  addOutline(
    drawList,
    x - border,
    barY - border,
    barWidth + border * 2.0F,
    barHeight + border * 2.0F,
    {69, 226, 42, 255}
  );
  addRect(drawList, x, barY, barWidth, barHeight, {7, 10, 8, 210});
  addRect(
    drawList,
    x + padding,
    barY + padding,
    std::max(0.0F, (barWidth - padding * 2.0F) * healthRatio),
    std::max(0.0F, barHeight - padding * 2.0F),
    localHealthFillColor(healthRatio)
  );
}

void addArtHealthBar(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  const float viewportScale = std::clamp(
    static_cast<float>(height) / 720.0F,
    0.75F,
    1.5F
  );
  const float requestedScale =
    viewportScale * std::clamp(settings.healthTextScale, 0.5F, 20.0F) * 0.5F;
  // Keep the complete authored layout on-screen at large health sizes. The
  // default scale remains one, so the normal layout is unchanged.
  constexpr float kArtLayoutWidth = 530.0F;
  constexpr float kArtLayoutHeight = 52.0F;
  constexpr float kArtLayoutMargin = 8.0F;
  const float maximumHorizontalScale = std::max(
    0.0F,
    (static_cast<float>(width) - kArtLayoutMargin) / kArtLayoutWidth
  );
  const float maximumVerticalScale = std::max(
    0.0F,
    (static_cast<float>(height) - kArtLayoutMargin) / kArtLayoutHeight
  );
  const float scale = std::min({
    requestedScale,
    maximumHorizontalScale,
    maximumVerticalScale,
  });
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio = std::clamp(
    static_cast<float>(localPlayer.health) / maxHealth,
    0.0F,
    1.0F
  );
  const float barWidth = 374.0F * scale;
  const float barHeight = 23.0F * scale;
  const float left = 18.0F * scale;
  const float bottom = static_cast<float>(height) - 25.0F * scale;
  const float top = bottom - barHeight;
  const float numberX = left + 76.0F * scale;
  const float plusX = numberX - 36.0F * scale;
  const float dividerX = left + 122.0F * scale;
  const float barX = dividerX + 16.0F * scale;
  const float textScale = std::max(1.0F, 2.0F * scale);
  const float textY = top + 4.0F * scale;
  const std::string value = std::to_string(std::max(0, localPlayer.health));

  addText(
    drawList,
    plusX,
    textY,
    "+",
    {245, 247, 248, 255},
    textScale * 1.25F
  );
  addText(
    drawList,
    numberX,
    textY,
    value,
    {245, 247, 248, 255},
    textScale,
    TextHorizontalAlignment::Center
  );
  addLine(
    drawList,
    {dividerX, top - 3.0F * scale},
    {dividerX, bottom + 3.0F * scale},
    {210, 216, 221, 210},
    std::max(1.0F, scale)
  );

  if (settings.healthStyle == 3) {
    addImage(
      drawList,
      HudImage::HealthSegmented,
      {barX, top, barWidth * healthRatio, barHeight},
      {255, 255, 255, 255},
      {0.0F, 0.0F, healthRatio, 1.0F}
    );
    return;
  }

  const float bevel = std::min(8.0F * scale, barWidth * 0.08F);
  const std::array<ScreenPoint, 4> frame = {{
    {barX + bevel, top},
    {barX + barWidth, top},
    {barX + barWidth - bevel, bottom},
    {barX, bottom},
  }};
  if (settings.healthStyle == 4) {
    drawList.overlayCommands.emplace_back(FilledQuad2D{
      frame,
      {14, 16, 18, 205},
    });
  }
  const RenderColor outline = settings.healthStyle == 5
    ? RenderColor{244, 246, 247, 255}
    : RenderColor{92, 98, 104, 215};
  for (std::size_t edge = 0; edge < frame.size(); ++edge) {
    addLine(
      drawList,
      frame[edge],
      frame[(edge + 1U) % frame.size()],
      outline,
      std::max(1.0F, 1.25F * scale)
    );
  }

  if (healthRatio <= 0.0F) {
    return;
  }
  const float insetX = 6.0F * scale;
  const float insetY = settings.healthStyle == 5
    ? 6.0F * scale
    : 4.0F * scale;
  const float fillWidth = std::max(
    0.0F,
    (barWidth - insetX * 2.0F) * healthRatio
  );
  const HudImage image = settings.healthStyle == 5
    ? HudImage::HealthOutlined
    : HudImage::HealthFilled;
  // Each source image also contains its own frame. Sample only the textured
  // fill so that it does not draw a second rectangular border inside the
  // shaped frame above.
  const ScreenRect fillSource = settings.healthStyle == 5
    ? ScreenRect{
        12.0F / 747.0F,
        12.0F / 45.0F,
        (508.0F / 747.0F) * healthRatio,
        20.0F / 45.0F,
      }
    : ScreenRect{
        7.0F / 746.0F,
        6.0F / 35.0F,
        (540.0F / 746.0F) * healthRatio,
        23.0F / 35.0F,
      };
  addImage(
    drawList,
    image,
    {
      barX + insetX,
      top + insetY,
      fillWidth,
      std::max(0.0F, barHeight - insetY * 2.0F),
    },
    {255, 255, 255, 255},
    fillSource
  );
}

void addLocalHealthNumber(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  const float scale = std::clamp(settings.healthTextScale, 0.5F, 20.0F);
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio =
    std::clamp(static_cast<float>(localPlayer.health) / maxHealth, 0.0F, 1.0F);
  const std::string text = std::to_string(std::max(0, localPlayer.health));
  const float x = static_cast<float>(width) * 0.5F;
  const float y =
    static_cast<float>(height) - 24.0F - kGlyphSize * snappedTextScale(scale);
  addText(
    drawList,
    x,
    y,
    text,
    localHealthFillColor(healthRatio),
    scale,
    TextHorizontalAlignment::Center
  );
}

[[nodiscard]] RenderColor quakeLiveWeaponColor(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun:
    return {255, 232, 92, 255};
  case Weapon::Shotgun:
    return {255, 150, 64, 255};
  case Weapon::GrenadeLauncher:
    return {80, 224, 96, 255};
  case Weapon::RocketLauncher:
    return {255, 72, 54, 255};
  case Weapon::LightningGun:
    return {245, 244, 168, 255};
  case Weapon::Railgun:
    return {72, 232, 112, 255};
  case Weapon::PlasmaGun:
    return {190, 82, 255, 255};
  case Weapon::FreezeGun:
    return {154, 232, 255, 255};
  case Weapon::Revolver:
    return {225, 190, 118, 255};
  }
  return {230, 238, 246, 255};
}

[[nodiscard]] std::size_t weaponHudSlotIndex(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun:
    return 0;
  case Weapon::Shotgun:
    return 1;
  case Weapon::GrenadeLauncher:
    return 2;
  case Weapon::RocketLauncher:
    return 3;
  case Weapon::LightningGun:
    return 4;
  case Weapon::Railgun:
    return 5;
  case Weapon::PlasmaGun:
    return 6;
  case Weapon::FreezeGun:
    return 7;
  case Weapon::Revolver:
    return 8;
  }
  return 4;
}

void addCrosshairHealthAndAmmo(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  const float scale = std::clamp(settings.healthTextScale, 0.5F, 20.0F);
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio =
    std::clamp(static_cast<float>(localPlayer.health) / maxHealth, 0.0F, 1.0F);
  const std::string healthText = std::to_string(std::max(0, localPlayer.health));
  const std::string ammoText = hud.weaponValues[weaponHudSlotIndex(hud.selectedWeapon)];
  const float centerX = static_cast<float>(width) * 0.5F;
  const float centerY = static_cast<float>(height) * 0.5F;
  const float y = centerY + std::max(22.0F, settings.crosshairGap + settings.crosshairSize + 10.0F);
  constexpr float sideOffset = 56.0F;
  const bool infiniteAmmo = ammoText == "\xE2\x88\x9E";
  const float ammoScale = infiniteAmmo ? scale * 1.4F : scale;
  const float ammoY = infiniteAmmo ? y - scale * 2.25F : y;

  addText(
    drawList,
    centerX - sideOffset,
    y,
    healthText,
    localHealthFillColor(healthRatio),
    scale,
    TextHorizontalAlignment::Right
  );
  addText(
    drawList,
    centerX + sideOffset,
    ammoY,
    ammoText,
    quakeLiveWeaponColor(hud.selectedWeapon),
    ammoScale
  );
}

void addSpeedText(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  if (hud.speedText.empty()) {
    return;
  }

  const float scale = std::clamp(settings.speedTextScale, 0.5F, 6.0F);
  const float snappedScale = snappedTextScale(scale);
  const float crosshairReach = settings.crosshairEnabled
    ? settings.crosshairGap + settings.crosshairSize
    : 0.0F;
  const float y =
    static_cast<float>(height) * 0.5F +
    std::max(24.0F, crosshairReach + 14.0F) +
    snappedScale * 2.0F;
  addText(
    drawList,
    static_cast<float>(width) * 0.5F,
    y,
    hud.speedText,
    {230, 238, 246, 225},
    scale,
    TextHorizontalAlignment::Center
  );
}

void addDashIndicator(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const RenderSettings& settings
) {
  constexpr float indicatorWidth = 48.0F;
  constexpr float indicatorHeight = 14.0F;
  constexpr float textScale = 1.0F;
  const bool ready = localPlayer.dashCooldownTicksRemaining == 0;
  const float crosshairReach = settings.crosshairEnabled
    ? settings.crosshairGap + settings.crosshairSize
    : 0.0F;
  const float centerX = static_cast<float>(width) * 0.5F;
  const float y =
    static_cast<float>(height) * 0.5F +
    std::max(48.0F, crosshairReach + 38.0F);
  const RenderColor fill = ready
    ? RenderColor{42, 112, 78, 220}
    : RenderColor{38, 43, 50, 190};
  const RenderColor outline = ready
    ? RenderColor{104, 238, 166, 245}
    : RenderColor{100, 108, 118, 205};
  const RenderColor text = ready
    ? RenderColor{218, 255, 232, 255}
    : RenderColor{150, 158, 168, 225};

  addRect(
    drawList,
    centerX - indicatorWidth * 0.5F,
    y,
    indicatorWidth,
    indicatorHeight,
    fill
  );
  addOutline(
    drawList,
    centerX - indicatorWidth * 0.5F,
    y,
    indicatorWidth,
    indicatorHeight,
    outline
  );
  addText(
    drawList,
    centerX,
    y + 3.0F,
    "DASH",
    text,
    textScale,
    TextHorizontalAlignment::Center
  );
}

[[maybe_unused]] void addProceduralWeaponIcon(
  DrawList2D& drawList,
  float centerX,
  float centerY,
  Weapon weapon,
  RenderColor color,
  float scale
) {
  const auto rect = [&](float x, float y, float width, float height) {
    addRect(
      drawList,
      centerX + x * scale,
      centerY + y * scale,
      width * scale,
      height * scale,
      color
    );
  };
  const auto quad = [&](std::array<ScreenPoint, 4> points) {
    for (ScreenPoint& point : points) {
      point.x = centerX + point.x * scale;
      point.y = centerY + point.y * scale;
    }
    drawList.overlayCommands.emplace_back(FilledQuad2D{points, color});
  };
  const auto line =
    [&](float x1, float y1, float x2, float y2, float width) {
      addLine(
        drawList,
        {centerX + x1 * scale, centerY + y1 * scale},
        {centerX + x2 * scale, centerY + y2 * scale},
        color,
        width * scale
      );
    };
  const auto lineColor = [&](
    float x1,
    float y1,
    float x2,
    float y2,
    float width,
    RenderColor lineRenderColor
  ) {
    addLine(
      drawList,
      {centerX + x1 * scale, centerY + y1 * scale},
      {centerX + x2 * scale, centerY + y2 * scale},
      lineRenderColor,
      width * scale
    );
  };
  const auto ringAt = [&](
    float x,
    float y,
    float radius,
    float width,
    int segments,
    RenderColor ringColor
  ) {
    for (int segment = 0; segment < segments; ++segment) {
      const float a0 =
        (static_cast<float>(segment) / static_cast<float>(segments)) *
        kTwoPi;
      const float a1 =
        (static_cast<float>(segment + 1) / static_cast<float>(segments)) *
        kTwoPi;
      lineColor(
        x + std::cos(a0) * radius,
        y + std::sin(a0) * radius,
        x + std::cos(a1) * radius,
        y + std::sin(a1) * radius,
        width,
        ringColor
      );
    }
  };
  const auto ring = [&](float radius, float width, int segments) {
    ringAt(0.0F, 0.0F, radius, width, segments, color);
  };

  if (weapon == Weapon::MachineGun) {
    rect(-17.0F, -15.0F, 8.0F, 30.0F);
    rect(-9.0F, -12.0F, 8.0F, 24.0F);
    rect(-2.0F, -11.0F, 19.0F, 5.0F);
    rect(-2.0F, -2.5F, 22.0F, 5.0F);
    rect(-2.0F, 6.0F, 19.0F, 5.0F);
    rect(17.0F, -10.0F, 4.0F, 3.0F);
    rect(20.0F, -1.5F, 4.0F, 3.0F);
    rect(17.0F, 7.0F, 4.0F, 3.0F);
    return;
  }

  if (weapon == Weapon::Shotgun) {
    for (float x : {-13.0F, -1.0F, 11.0F}) {
      line(x, -18.0F, x, 12.0F, 4.0F);
      line(x + 6.0F, -18.0F, x + 6.0F, 12.0F, 4.0F);
      line(x, -18.0F, x + 6.0F, -18.0F, 4.0F);
      line(x, 12.0F, x + 6.0F, 12.0F, 4.0F);
      rect(x - 1.0F, 14.0F, 8.0F, 4.0F);
    }
    return;
  }

  if (weapon == Weapon::GrenadeLauncher) {
    rect(-8.0F, -14.0F, 16.0F, 4.0F);
    rect(-13.0F, -10.0F, 26.0F, 8.0F);
    rect(-15.0F, -2.0F, 30.0F, 12.0F);
    rect(-11.0F, 10.0F, 22.0F, 6.0F);
    rect(-5.0F, 16.0F, 10.0F, 3.0F);
    rect(1.0F, -20.0F, 8.0F, 5.0F);
    line(8.0F, -19.0F, 16.0F, -17.0F, 4.0F);
    line(16.0F, -17.0F, 20.0F, -10.0F, 4.0F);
    return;
  }

  if (weapon == Weapon::RocketLauncher) {
    quad({{
      {-18.0F, -13.0F},
      {-10.0F, -21.0F},
      {17.0F, 6.0F},
      {9.0F, 14.0F},
    }});
    quad({{
      {17.0F, 6.0F},
      {9.0F, 14.0F},
      {24.0F, 20.0F},
      {26.0F, 18.0F},
    }});
    quad({{
      {-23.0F, -18.0F},
      {-18.0F, -24.0F},
      {-10.0F, -21.0F},
      {-18.0F, -13.0F},
    }});
    quad({{
      {-16.0F, -12.0F},
      {-29.0F, -8.0F},
      {-23.0F, -1.0F},
      {-9.0F, -5.0F},
    }});
    quad({{
      {-11.0F, -18.0F},
      {-13.0F, -31.0F},
      {-5.0F, -27.0F},
      {-3.0F, -14.0F},
    }});
    line(-19.0F, -20.0F, -28.0F, -27.0F, 4.0F);
    line(-22.0F, -15.0F, -33.0F, -16.0F, 4.0F);
    return;
  }

  if (weapon == Weapon::LightningGun || weapon == Weapon::FreezeGun) {
    for (int branch = 0; branch < 6; ++branch) {
      const float angle =
        (static_cast<float>(branch) / 6.0F) * kTwoPi - kHalfPi;
      const float dx = std::cos(angle);
      const float dy = std::sin(angle);
      const float px = -dy;
      const float py = dx;
      line(0.0F, 0.0F, dx * 21.0F, dy * 21.0F, 5.0F);
      line(
        dx * 12.0F,
        dy * 12.0F,
        dx * 18.0F + px * 7.0F,
        dy * 18.0F + py * 7.0F,
        4.0F
      );
      line(
        dx * 12.0F,
        dy * 12.0F,
        dx * 18.0F - px * 7.0F,
        dy * 18.0F - py * 7.0F,
        4.0F
      );
    }
    return;
  }

  if (weapon == Weapon::Railgun || weapon == Weapon::Revolver) {
    const RenderColor holeColor = {6, 8, 10, 230};
    rect(-8.0F, -17.0F, 16.0F, 34.0F);
    rect(-14.0F, -12.0F, 28.0F, 24.0F);
    rect(-17.0F, -7.0F, 34.0F, 14.0F);
    ring(16.0F, 3.0F, 24);
    ringAt(0.0F, 0.0F, 1.4F, 5.8F, 10, holeColor);
    for (int hole = 0; hole < 6; ++hole) {
      const float angle = static_cast<float>(hole) / 6.0F * kTwoPi;
      ringAt(
        std::cos(angle) * 8.5F,
        std::sin(angle) * 8.5F,
        1.4F,
        5.8F,
        10,
        holeColor
      );
    }
    return;
  }

  if (weapon == Weapon::PlasmaGun) {
    rect(-4.0F, -20.0F, 8.0F, 40.0F);
    rect(-20.0F, -4.0F, 40.0F, 8.0F);
    rect(-12.0F, -12.0F, 24.0F, 24.0F);
    line(-16.0F, -16.0F, 16.0F, 16.0F, 3.0F);
    line(-16.0F, 16.0F, 16.0F, -16.0F, 3.0F);
  }
}

[[nodiscard]] HudImage weaponHudImage(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun: return HudImage::WeaponMachineGun;
  case Weapon::Shotgun: return HudImage::WeaponShotgun;
  case Weapon::GrenadeLauncher: return HudImage::WeaponGrenadeLauncher;
  case Weapon::RocketLauncher: return HudImage::WeaponRocketLauncher;
  case Weapon::LightningGun: return HudImage::WeaponLightningGun;
  case Weapon::Railgun: return HudImage::WeaponSniperRifle;
  case Weapon::PlasmaGun: return HudImage::WeaponPlasmaGun;
  case Weapon::FreezeGun: return HudImage::WeaponFreezeGun;
  case Weapon::Revolver: return HudImage::WeaponRevolver;
  }
  return HudImage::WeaponMachineGun;
}

[[nodiscard]] float weaponHudImageAspect(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun: return 347.0F / 133.0F;
  case Weapon::Shotgun: return 323.0F / 124.0F;
  case Weapon::GrenadeLauncher: return 297.0F / 122.0F;
  case Weapon::RocketLauncher: return 345.0F / 134.0F;
  case Weapon::LightningGun: return 324.0F / 124.0F;
  case Weapon::Railgun: return 357.0F / 127.0F;
  case Weapon::PlasmaGun: return 297.0F / 133.0F;
  case Weapon::FreezeGun: return 332.0F / 112.0F;
  case Weapon::Revolver: return 261.0F / 143.0F;
  }
  return 2.5F;
}

void addWeaponIcon(
  DrawList2D& drawList,
  float centerX,
  float centerY,
  Weapon weapon,
  RenderColor color,
  float scale
) {
  const float aspect = weaponHudImageAspect(weapon);
  const float width = std::min(44.0F * scale, 34.0F * scale * aspect);
  const float height = width / aspect;
  addImage(
    drawList,
    weaponHudImage(weapon),
    {centerX - width * 0.5F, centerY - height * 0.5F, width, height},
    color
  );
}

void addSelectedWeaponIndicator(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  (void)width;
  constexpr std::array<Weapon, 9> weapons = {{
    Weapon::MachineGun,
    Weapon::Shotgun,
    Weapon::GrenadeLauncher,
    Weapon::RocketLauncher,
    Weapon::LightningGun,
    Weapon::Railgun,
    Weapon::PlasmaGun,
    Weapon::FreezeGun,
    Weapon::Revolver,
  }};
  const float viewportScale = std::clamp(
    static_cast<float>(height) / 720.0F,
    0.75F,
    1.25F
  );
  const float scale =
    viewportScale * std::clamp(settings.weaponBarScale, 0.5F, 4.0F);
  const float rowHeight = 24.0F * scale;
  const float gap = 4.0F * scale;
  const float iconScale = 0.38F * scale;
  const float textScale = 1.45F * scale;
  const float panelHeight =
    rowHeight * static_cast<float>(weapons.size()) +
    gap * static_cast<float>(weapons.size() - 1U);
  const float x = 4.0F * viewportScale;
  const float y = static_cast<float>(height) * 0.18F;

  float rowY = std::min(
    y,
    static_cast<float>(height) - panelHeight - 96.0F * viewportScale
  );
  rowY = std::max(48.0F * viewportScale, rowY);
  for (std::size_t index = 0; index < weapons.size(); ++index) {
    const Weapon weapon = weapons[index];
    const bool selected = weapon == hud.selectedWeapon;
    const RenderColor weaponColor = quakeLiveWeaponColor(weapon);
    const RenderColor textColor = selected
      ? RenderColor{245, 250, 255, 255}
      : RenderColor{220, 226, 232, 235};
    if (selected) {
      addRect(
        drawList,
        x - 3.0F * scale,
        rowY - 1.0F * scale,
        45.0F * scale,
        rowHeight + 2.0F * scale,
        {10, 13, 18, 155}
      );
    }

    addWeaponIcon(
      drawList,
      x + 9.0F * scale,
      rowY + rowHeight * 0.5F,
      weapon,
      weaponColor,
      iconScale
    );
    addText(
      drawList,
      x + 20.0F * scale,
      rowY + 4.0F * scale,
      hud.weaponValues[index],
      textColor,
      textScale
    );

    rowY += rowHeight + gap;
  }
}

[[nodiscard]] RenderColor withAlpha(RenderColor color, float alphaScale) {
  color.alpha = static_cast<std::uint8_t>(
    std::clamp(
      static_cast<float>(color.alpha) * std::clamp(alphaScale, 0.0F, 1.0F),
      0.0F,
      255.0F
    )
  );
  return color;
}

void addKillFeed(
  DrawList2D& drawList,
  int width,
  float startY,
  const HudRenderState& hud
) {
  if (hud.killFeedLines.empty()) {
    return;
  }

  constexpr float textScale = 2.25F;
  constexpr float rowHeight = 31.5F;
  constexpr float rightPadding = 10.0F;
  constexpr float textIconGap = 12.0F;
  constexpr float iconWidth = 30.0F;
  constexpr float iconScale = 0.51F;
  constexpr RenderColor baseText = {235, 242, 250, 245};

  float y = startY;
  for (const HudRenderState::KillFeedLine& line : hud.killFeedLines) {
    const float alpha = std::clamp(line.alpha, 0.0F, 1.0F);
    if (alpha <= 0.0F) {
      y += rowHeight;
      continue;
    }

    const float killerWidth = textWidth(line.killerName, textScale);
    const float killedWidth = textWidth(line.killedName, textScale);
    const bool selfKill = line.killedName.empty();
    const float rowWidth =
      killerWidth + iconWidth + textIconGap +
      (selfKill ? 0.0F : killedWidth + textIconGap);
    float x = static_cast<float>(width) - rightPadding - rowWidth;
    x = std::max(12.0F, x);
    const float textY = y + 3.0F;
    const RenderColor textColor = withAlpha(baseText, alpha);
    const RenderColor weaponColor =
      withAlpha(quakeLiveWeaponColor(line.weapon), alpha);

    addText(drawList, x, textY, line.killerName, textColor, textScale);
    x += killerWidth + textIconGap;
    addWeaponIcon(
      drawList,
      x + iconWidth * 0.5F,
      y + rowHeight * 0.5F,
      line.weapon,
      weaponColor,
      iconScale
    );
    if (!selfKill) {
      x += iconWidth + textIconGap;
      addText(drawList, x, textY, line.killedName, textColor, textScale);
    }

    y += rowHeight;
  }
}

[[nodiscard]] ScreenPoint screenPointFromProjection(
  ProjectedPoint projected,
  int width,
  int height
) {
  return {
    (projected.x + 1.0F) * 0.5F * static_cast<float>(width),
    (1.0F - projected.y) * 0.5F * static_cast<float>(height),
  };
}

[[nodiscard]] bool hasClearLineToPoint(
  const PerspectiveCamera& camera,
  const Arena& arena,
  Vec3 point
) {
  const Vec3 direction = point - camera.position;
  if (length(direction) <= 0.00001F) {
    return true;
  }
  return traceWorld(arena, camera.position, direction, 1.0F).distance >= 0.999F;
}

[[nodiscard]] bool enemyBodyVisibleFromCamera(
  const PerspectiveCamera& camera,
  const Arena& arena,
  const PlayerState& player
) {
  const float radius = player.bounds.radius;
  const float bottom = player.position.z - player.bounds.halfHeight;
  const float middle = player.position.z;
  const float top = player.position.z + player.bounds.halfHeight;
  constexpr std::array<std::array<float, 2>, 9> planarOffsets = {{
    {{0.0F, 0.0F}},
    {{-1.0F, 0.0F}},
    {{1.0F, 0.0F}},
    {{0.0F, -1.0F}},
    {{0.0F, 1.0F}},
    {{-1.0F, -1.0F}},
    {{-1.0F, 1.0F}},
    {{1.0F, -1.0F}},
    {{1.0F, 1.0F}},
  }};
  const std::array<float, 3> zLevels = {{bottom, middle, top}};
  const auto sampleVisible = [&](Vec3 sample) {
    ProjectedPoint projected;
    return
      projectPerspectivePoint(camera, sample, projected) &&
      projected.x >= -1.0F &&
      projected.x <= 1.0F &&
      projected.y >= -1.0F &&
      projected.y <= 1.0F &&
      hasClearLineToPoint(camera, arena, sample);
  };

  for (float z : zLevels) {
    for (const auto& offset : planarOffsets) {
      if (
        sampleVisible({
          player.position.x + offset[0] * radius,
          player.position.y + offset[1] * radius,
          z,
        })
      ) {
        return true;
      }
    }
  }
  return false;
}

void addFloatingHealthBar(
  DrawList2D& drawList,
  int width,
  int height,
  const PerspectiveCamera& camera,
  const PlayerState& player,
  float alpha,
  bool teammate,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  const bool enabled = teammate
    ? settings.teammateHealthBarEnabled
    : settings.enemyHealthBarEnabled;
  if (
    (!teammate && !hud.showOpponentHealthBar) ||
    !enabled ||
    player.health <= 0 ||
    alpha <= 0.0F
  ) {
    return;
  }

  const float maxDistance = teammate
    ? settings.teammateHealthBarMaxDistance
    : settings.enemyHealthBarMaxDistance;
  if (
    maxDistance > 0.0F &&
    length(player.position - camera.position) > maxDistance
  ) {
    return;
  }

  const float worldOffsetZ = teammate
    ? settings.teammateHealthBarWorldOffsetZ
    : settings.enemyHealthBarWorldOffsetZ;
  const Vec3 anchor =
    player.position +
    Vec3{0.0F, 0.0F, player.bounds.halfHeight + worldOffsetZ};
  ProjectedPoint projected;
  if (!projectPerspectivePoint(camera, anchor, projected)) {
    return;
  }
  const ScreenPoint anchorScreen =
    screenPointFromProjection(projected, width, height);
  const float barWidth = std::max(
    1.0F,
    teammate ? settings.teammateHealthBarWidth : settings.enemyHealthBarWidth
  );
  const float barHeight = std::max(
    1.0F,
    teammate ? settings.teammateHealthBarHeight : settings.enemyHealthBarHeight
  );
  const float border = std::max(1.0F, std::round(barHeight * 0.25F));
  const float offsetX = teammate
    ? settings.teammateHealthBarScreenOffsetX
    : settings.enemyHealthBarScreenOffsetX;
  const float offsetY = teammate
    ? settings.teammateHealthBarScreenOffsetY
    : settings.enemyHealthBarScreenOffsetY;
  const float x = anchorScreen.x + offsetX - barWidth * 0.5F;
  const float y = anchorScreen.y + offsetY - barHeight;
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio =
    std::clamp(static_cast<float>(player.health) / maxHealth, 0.0F, 1.0F);
  const float barAlpha = teammate
    ? settings.teammateHealthBarAlpha
    : settings.enemyHealthBarAlpha;
  const RenderColor outline =
    withAlpha({220, 226, 236, 255}, alpha * barAlpha);
  const RenderColor back =
    withAlpha({10, 13, 18, 215}, alpha * barAlpha);
  const RenderColor fill = withAlpha(
    {
      teammate ? settings.teammateHealthBarRed : settings.enemyHealthBarRed,
      teammate
        ? settings.teammateHealthBarGreen
        : settings.enemyHealthBarGreen,
      teammate ? settings.teammateHealthBarBlue : settings.enemyHealthBarBlue,
      255,
    },
    alpha * barAlpha
  );

  addRect(
    drawList,
    x - border,
    y - border,
    barWidth + border * 2.0F,
    barHeight + border * 2.0F,
    outline
  );
  addRect(drawList, x, y, barWidth, barHeight, back);
  addRect(drawList, x, y, barWidth * healthRatio, barHeight, fill);
}

void addFloatingNameTag(
  DrawList2D& drawList,
  int width,
  int height,
  const PerspectiveCamera& camera,
  const RemotePlayerView& remote,
  const RenderSettings& settings
) {
  if (remote.name.empty() || remote.player.health <= 0) {
    return;
  }

  const bool enabled = remote.teammate
    ? settings.teammateNameTagEnabled
    : settings.enemyNameTagEnabled;
  if (!enabled) {
    return;
  }

  const float maxDistance = remote.teammate
    ? settings.teammateNameTagMaxDistance
    : settings.enemyNameTagMaxDistance;
  if (
    maxDistance > 0.0F &&
    length(remote.player.position - camera.position) > maxDistance
  ) {
    return;
  }

  const float worldOffsetZ = remote.teammate
    ? settings.teammateNameTagWorldOffsetZ
    : settings.enemyNameTagWorldOffsetZ;
  const Vec3 anchor =
    remote.player.position +
    Vec3{0.0F, 0.0F, remote.player.bounds.halfHeight + worldOffsetZ};
  ProjectedPoint projected;
  if (!projectPerspectivePoint(camera, anchor, projected)) {
    return;
  }

  const ScreenPoint anchorScreen =
    screenPointFromProjection(projected, width, height);
  const float scale = std::max(
    0.1F,
    remote.teammate ? settings.teammateNameTagScale : settings.enemyNameTagScale
  );
  const float offsetX = remote.teammate
    ? settings.teammateNameTagScreenOffsetX
    : settings.enemyNameTagScreenOffsetX;
  const float offsetY = remote.teammate
    ? settings.teammateNameTagScreenOffsetY
    : settings.enemyNameTagScreenOffsetY;
  const float alpha = std::clamp(
    remote.teammate ? settings.teammateNameTagAlpha : settings.enemyNameTagAlpha,
    0.0F,
    1.0F
  );
  const RenderColor color = {
    remote.teammate ? settings.teammateNameTagRed : settings.enemyNameTagRed,
    remote.teammate ? settings.teammateNameTagGreen : settings.enemyNameTagGreen,
    remote.teammate ? settings.teammateNameTagBlue : settings.enemyNameTagBlue,
    static_cast<std::uint8_t>(alpha * 255.0F),
  };

  addText(
    drawList,
    anchorScreen.x + offsetX,
    anchorScreen.y + offsetY,
    remote.name,
    color,
    scale,
    TextHorizontalAlignment::Center
  );
}

void addCrosshair(
  DrawList2D& drawList,
  int width,
  int height,
  const RenderSettings& settings
) {
  if (!settings.crosshairEnabled) {
    return;
  }

  const float centerX = static_cast<float>(width) * 0.5F;
  const float centerY = static_cast<float>(height) * 0.5F;
  const float size = settings.crosshairSize;
  const float gap = settings.crosshairGap;
  const float thickness = settings.crosshairThickness;
  const float dotThickness = settings.crosshairDotThickness;
  const float outlineWidth = settings.crosshairOutlineEnabled
    ? std::max(0.0F, settings.crosshairOutlineWidth)
    : 0.0F;
  const float hitAmount = std::clamp(settings.crosshairHitAmount, 0.0F, 1.0F);
  const RenderColor color = {
    blendChannel(settings.crosshairRed, settings.crosshairHitRed, hitAmount),
    blendChannel(settings.crosshairGreen, settings.crosshairHitGreen, hitAmount),
    blendChannel(settings.crosshairBlue, settings.crosshairHitBlue, hitAmount),
    static_cast<std::uint8_t>(
      std::clamp(settings.crosshairAlpha, 0.0F, 1.0F) * 255.0F
    ),
  };
  const RenderColor outlineColor = {0, 0, 0, color.alpha};
  const auto rect = [&](float x, float y, float w, float h, RenderColor rectColor) {
    addRect(drawList, x, y, w, h, rectColor);
  };
  const auto cross = [&](float crossGap, float extra, RenderColor crossColor) {
    const float crossThickness = thickness + extra * 2.0F;
    const float halfThickness = crossThickness * 0.5F;
    const float crossSize = size + extra;
    if (crossGap <= 0.0F) {
      rect(
        centerX - crossSize,
        centerY - halfThickness,
        crossSize * 2.0F,
        crossThickness,
        crossColor
      );
      rect(
        centerX - halfThickness,
        centerY - crossSize,
        crossThickness,
        crossSize * 2.0F,
        crossColor
      );
      return;
    }
    const float armGap = std::max(0.0F, crossGap - extra);
    rect(
      centerX - armGap - crossSize,
      centerY - halfThickness,
      crossSize,
      crossThickness,
      crossColor
    );
    rect(
      centerX + armGap,
      centerY - halfThickness,
      crossSize,
      crossThickness,
      crossColor
    );
    rect(
      centerX - halfThickness,
      centerY - armGap - crossSize,
      crossThickness,
      crossSize,
      crossColor
    );
    rect(
      centerX - halfThickness,
      centerY + armGap,
      crossThickness,
      crossSize,
      crossColor
    );
  };
  const auto ring = [&](float extra, RenderColor ringColor) {
    const float ringRadius = std::max(1.0F, size + extra);
    const float ringThickness = thickness + extra * 2.0F;
    constexpr int segmentCount = 36;
    for (int segment = 0; segment < segmentCount; ++segment) {
      const float a0 =
        (static_cast<float>(segment) / static_cast<float>(segmentCount)) *
        kTwoPi;
      const float a1 =
        (static_cast<float>(segment + 1) / static_cast<float>(segmentCount)) *
        kTwoPi;
      addLine(
        drawList,
        {
          centerX + std::cos(a0) * ringRadius,
          centerY + std::sin(a0) * ringRadius,
        },
        {
          centerX + std::cos(a1) * ringRadius,
          centerY + std::sin(a1) * ringRadius,
        },
        ringColor,
        ringThickness
      );
    }
  };
  const auto dot = [&](float extra, RenderColor dotColor) {
    const float dotSize = dotThickness + extra * 2.0F;
    rect(
      centerX - dotSize * 0.5F,
      centerY - dotSize * 0.5F,
      dotSize,
      dotSize,
      dotColor
    );
  };

  const auto drawMainShape = [&](float extra, RenderColor shapeColor) {
    switch (settings.crosshairStyle) {
    case 1:
      cross(0.0F, extra, shapeColor);
      break;
    case 3:
      ring(extra, shapeColor);
      break;
    case 2:
      break;
    case 0:
    default:
      cross(gap, extra, shapeColor);
      break;
    }
  };

  if (outlineWidth > 0.0F) {
    drawMainShape(outlineWidth, outlineColor);
    if (settings.crosshairDotEnabled) {
      dot(outlineWidth, outlineColor);
    }
  }

  drawMainShape(0.0F, color);
  if (settings.crosshairDotEnabled) {
    dot(0.0F, color);
  }
}

[[nodiscard]] ScreenPoint directionalDamagePerimeterPoint(
  float centerX,
  float centerY,
  float halfWidth,
  float halfHeight,
  float inset,
  float relativeYaw
) {
  const ScreenPoint direction = {
    -std::sin(relativeYaw),
    -std::cos(relativeYaw),
  };
  const float availableHalfWidth = std::max(0.0F, halfWidth - inset);
  const float availableHalfHeight = std::max(0.0F, halfHeight - inset);
  const float horizontalDistance = std::fabs(direction.x) > 0.0001F
    ? availableHalfWidth / std::fabs(direction.x)
    : std::numeric_limits<float>::infinity();
  const float verticalDistance = std::fabs(direction.y) > 0.0001F
    ? availableHalfHeight / std::fabs(direction.y)
    : std::numeric_limits<float>::infinity();
  const float distance = std::min(horizontalDistance, verticalDistance);
  return {
    centerX + direction.x * distance,
    centerY + direction.y * distance,
  };
}

[[nodiscard]] ScreenPoint directionalDamageArcPoint(
  float centerX,
  float centerY,
  float halfWidth,
  float halfHeight,
  float inset,
  float relativeYaw,
  float sampleYaw,
  float halfAngle,
  float curveDepth
) {
  const ScreenPoint perimeterPoint = directionalDamagePerimeterPoint(
    centerX,
    centerY,
    halfWidth,
    halfHeight,
    inset,
    sampleYaw
  );
  const float normalizedOffset = halfAngle > 0.0F
    ? std::clamp((sampleYaw - relativeYaw) / halfAngle, -1.0F, 1.0F)
    : 0.0F;
  const float bow = curveDepth * (1.0F - normalizedOffset * normalizedOffset);
  const float towardCenterX = centerX - perimeterPoint.x;
  const float towardCenterY = centerY - perimeterPoint.y;
  const float towardCenterLength = std::hypot(towardCenterX, towardCenterY);
  if (towardCenterLength <= 0.0001F) {
    return perimeterPoint;
  }
  return {
    perimeterPoint.x + towardCenterX / towardCenterLength * bow,
    perimeterPoint.y + towardCenterY / towardCenterLength * bow,
  };
}

[[nodiscard]] float directionalDamageArcHalfAngle(
  float relativeYaw,
  float centerX,
  float centerY,
  float halfWidth,
  float halfHeight,
  float inset,
  float desiredHalfLength
) {
  constexpr float kBearingSample = 0.001F;
  const ScreenPoint before = directionalDamagePerimeterPoint(
    centerX,
    centerY,
    halfWidth,
    halfHeight,
    inset,
    relativeYaw - kBearingSample
  );
  const ScreenPoint after = directionalDamagePerimeterPoint(
    centerX,
    centerY,
    halfWidth,
    halfHeight,
    inset,
    relativeYaw + kBearingSample
  );
  const float tangentPixels = std::hypot(
    (after.x - before.x) / (2.0F * kBearingSample),
    (after.y - before.y) / (2.0F * kBearingSample)
  );
  return std::clamp(
    desiredHalfLength / std::max(1.0F, tangentPixels),
    0.06F,
    0.60F
  );
}

void addDirectionalDamageArc(
  DrawList2D& drawList,
  float centerX,
  float centerY,
  float halfWidth,
  float halfHeight,
  float inset,
  float relativeYaw,
  float halfAngle,
  float curveDepth,
  RenderColor color,
  float lineWidth,
  int segmentCount
) {
  segmentCount = std::max(1, segmentCount);
  const float startAngle = relativeYaw - halfAngle;
  ScreenPoint previous = directionalDamageArcPoint(
    centerX,
    centerY,
    halfWidth,
    halfHeight,
    inset,
    relativeYaw,
    startAngle,
    halfAngle,
    curveDepth
  );
  for (int segment = 1; segment <= segmentCount; ++segment) {
    const float amount = static_cast<float>(segment) /
      static_cast<float>(segmentCount);
    const float angle = startAngle + halfAngle * 2.0F * amount;
    const ScreenPoint current = directionalDamageArcPoint(
      centerX,
      centerY,
      halfWidth,
      halfHeight,
      inset,
      relativeYaw,
      angle,
      halfAngle,
      curveDepth
    );
    addLine(drawList, previous, current, color, lineWidth);
    previous = current;
  }
}

void addDirectionalDamageIndicators(
  DrawList2D& drawList,
  int width,
  int height,
  const DirectionalDamagePresentation& presentation
) {
  if (!presentation.enabled || width <= 0 || height <= 0) {
    return;
  }

  const float scale = std::isfinite(presentation.scale)
    ? std::clamp(presentation.scale, 0.25F, 4.0F)
    : 1.0F;
  const float centerX = static_cast<float>(width) * 0.5F;
  const float centerY = static_cast<float>(height) * 0.5F;
  const float halfWidth = centerX;
  const float halfHeight = centerY;
  const float minimumHalfExtent = std::min(halfWidth, halfHeight);
  constexpr float kCrescentThicknessMultiplier = 3.5F;
  const float softWidth = std::max(
    2.0F,
    8.0F * scale * kCrescentThicknessMultiplier
  );
  const float highlightWidth = std::max(
    1.0F,
    2.0F * scale * kCrescentThicknessMultiplier
  );
  const float edgePadding = std::max(
    2.0F * scale,
    softWidth * 0.5F + scale
  );
  const float requestedInset = std::isfinite(presentation.distancePixels)
    ? presentation.distancePixels * scale
    : 24.0F * scale;
  const float maximumInset = std::max(
    0.0F,
    minimumHalfExtent - edgePadding
  );
  const float minimumInset = std::min(2.0F * scale, maximumInset);
  const float inset = std::clamp(
    requestedInset,
    minimumInset,
    maximumInset
  );
  const float softInset = std::min(
    inset + 3.0F * scale,
    maximumInset
  );
  const float perimeterInset = std::min(
    std::max(inset, edgePadding),
    minimumHalfExtent
  );
  const float softPerimeterInset = std::min(
    std::max(softInset, edgePadding),
    minimumHalfExtent
  );

  const auto alpha = [](float amount) {
    return static_cast<std::uint8_t>(
      std::clamp(amount, 0.0F, 1.0F) * 255.0F
    );
  };

  for (const DirectionalDamageIndicator& indicator : presentation.indicators) {
    const float opacity = std::isfinite(indicator.opacity)
      ? std::clamp(indicator.opacity, 0.0F, 1.0F)
      : 0.0F;
    if (!indicator.active || opacity <= 0.0F) {
      continue;
    }

    const RenderColor color = indicator.selfDamage
      ? RenderColor{255, 186, 66, alpha(opacity)}
      : RenderColor{255, 76, 70, alpha(opacity)};
    const RenderColor softColor = {
      color.red,
      color.green,
      color.blue,
      alpha(opacity * 0.28F),
    };
    const float relativeYaw = indicator.directionValid &&
        std::isfinite(indicator.relativeYawRadians)
      ? std::atan2(
          std::sin(indicator.relativeYawRadians),
          std::cos(indicator.relativeYawRadians)
        )
      : 0.0F;
    const float desiredHalfLength = (
      indicator.directionValid ? 108.0F : 132.0F
    ) * scale * (indicator.selfDamage ? 0.82F : 1.0F);
    const float softHalfAngle = directionalDamageArcHalfAngle(
      relativeYaw,
      centerX,
      centerY,
      halfWidth,
      halfHeight,
      softPerimeterInset,
      desiredHalfLength + 10.0F * scale
    );
    const float highlightHalfAngle = directionalDamageArcHalfAngle(
      relativeYaw,
      centerX,
      centerY,
      halfWidth,
      halfHeight,
      perimeterInset,
      desiredHalfLength
    );

    // The path endpoints stay on the inset perimeter for every bearing. A
    // shallow inward bow keeps each path curved while the two paths make one
    // restrained crescent, not a cursor marker or a straight border bar.
    addDirectionalDamageArc(
      drawList,
      centerX,
      centerY,
      halfWidth,
      halfHeight,
      softPerimeterInset,
      relativeYaw,
      softHalfAngle,
      24.0F * scale,
      softColor,
      softWidth,
      12
    );
    addDirectionalDamageArc(
      drawList,
      centerX,
      centerY,
      halfWidth,
      halfHeight,
      perimeterInset,
      relativeYaw,
      highlightHalfAngle,
      18.0F * scale,
      color,
      highlightWidth,
      10
    );
  }
}

void addHitMarker(
  DrawList2D& drawList,
  int width,
  int height,
  const RenderSettings& settings
) {
  if (!settings.hitMarkerEnabled || settings.hitMarkerAmount <= 0.0F) {
    return;
  }

  const float centerX = static_cast<float>(width) * 0.5F;
  const float centerY = static_cast<float>(height) * 0.5F;
  const float size = settings.hitMarkerSize;
  const float inner = size * 0.35F;
  const RenderColor color = {
    settings.hitMarkerRed,
    settings.hitMarkerGreen,
    settings.hitMarkerBlue,
    static_cast<std::uint8_t>(
      std::clamp(settings.hitMarkerAmount, 0.0F, 1.0F) * 255.0F
    ),
  };
  addLine(
    drawList,
    {centerX - size, centerY - size},
    {centerX - inner, centerY - inner},
    color,
    settings.hitMarkerThickness
  );
  addLine(
    drawList,
    {centerX + inner, centerY + inner},
    {centerX + size, centerY + size},
    color,
    settings.hitMarkerThickness
  );
  addLine(
    drawList,
    {centerX + inner, centerY - inner},
    {centerX + size, centerY - size},
    color,
    settings.hitMarkerThickness
  );
  addLine(
    drawList,
    {centerX - size, centerY + size},
    {centerX - inner, centerY + inner},
    color,
    settings.hitMarkerThickness
  );
}

[[nodiscard]] float damageNumberImpact(int damage) {
  constexpr float kFullScaleDamage = 100.0F;
  return std::clamp(
    static_cast<float>(std::max(0, damage)) / kFullScaleDamage,
    0.0F,
    1.0F
  );
}

[[nodiscard]] float damageNumberScale(
  const RenderSettings& settings,
  int damage,
  float modeScale
) {
  constexpr float kMaxDamageScaleBoost = 0.25F;
  const float baseScale = std::max(0.1F, settings.damageNumbersSize);
  return baseScale *
    (1.0F + damageNumberImpact(damage) * kMaxDamageScaleBoost) *
    modeScale;
}

[[nodiscard]] RenderColor damageNumberColor(
  const RenderSettings& settings,
  int damage,
  bool headshot
) {
  RenderColor color = {
    settings.damageNumbersRed,
    settings.damageNumbersGreen,
    settings.damageNumbersBlue,
    255,
  };
  constexpr RenderColor highDamageColor = {255, 36, 36, 255};
  if (settings.damageNumbersDamageColor) {
    const float amount = damageNumberImpact(damage);
    color.red = blendChannel(color.red, highDamageColor.red, amount);
    color.green = blendChannel(color.green, highDamageColor.green, amount);
    color.blue = blendChannel(color.blue, highDamageColor.blue, amount);
  }
  if (headshot) {
    constexpr float kHeadshotAccent = 0.7F;
    color.red = blendChannel(color.red, highDamageColor.red, kHeadshotAccent);
    color.green =
      blendChannel(color.green, highDamageColor.green, kHeadshotAccent);
    color.blue =
      blendChannel(color.blue, highDamageColor.blue, kHeadshotAccent);
  }
  return color;
}

void addDamageText(
  DrawList2D& drawList,
  float x,
  float y,
  const std::string& text,
  RenderColor color,
  float scale,
  bool headshot
) {
  if (headshot) {
    RenderColor backing = {18, 4, 6, color.alpha};
    backing.alpha = static_cast<std::uint8_t>(
      std::clamp(static_cast<float>(backing.alpha) * 0.85F, 0.0F, 255.0F)
    );
    // Headshots get a dark backing pass instead of a word label or fake bold,
    // keeping the read fast without widening the number shape.
    addText(
      drawList,
      x + std::max(1.0F, scale * 0.8F),
      y + std::max(1.0F, scale * 0.8F),
      text,
      backing,
      scale
    );
  }
  addText(drawList, x, y, text, color, scale);
}

[[nodiscard]] std::size_t newerDamageEntriesForTarget(
  const DamageNumberPresentation& damageNumbers,
  const DamageNumberEntry& entry
) {
  return static_cast<std::size_t>(std::count_if(
    damageNumbers.entries.begin(),
    damageNumbers.entries.end(),
    [&entry](const DamageNumberEntry& other) {
      return other.targetPlayerIndex == entry.targetPlayerIndex &&
        other.sequence > entry.sequence;
    }
  ));
}

void addFloatingDamageNumbers(
  DrawList2D& drawList,
  int width,
  int height,
  const PerspectiveCamera& camera,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  const float alphaScale = std::clamp(settings.damageNumbersAlpha, 0.0F, 1.0F);
  const float duration = std::max(0.001F, settings.damageNumbersDuration);
  const float baseScale = std::max(0.1F, settings.damageNumbersSize);
  constexpr std::size_t kMaxVisibleEntriesPerTarget = 5;
  for (const DamageNumberEntry& entry : hud.damageNumbers.entries) {
    if (!entry.hasWorldPosition) {
      continue;
    }
    const std::size_t newerEntries =
      newerDamageEntriesForTarget(hud.damageNumbers, entry);
    if (newerEntries >= kMaxVisibleEntriesPerTarget) {
      continue;
    }

    ProjectedPoint projected;
    if (!projectPerspectivePoint(camera, entry.worldPosition, projected)) {
      continue;
    }

    const ScreenPoint anchorScreen =
      screenPointFromProjection(projected, width, height);
    const float life = std::clamp(entry.ageSeconds / duration, 0.0F, 1.0F);
    const float fade = 1.0F - life;
    const float textScale = damageNumberScale(settings, entry.damage, 1.0F);
    const float drift = life * 28.0F * textScale;
    const float stackSlot = static_cast<float>(newerEntries);
    const float wobble = (entry.sequence % 2U == 0U ? -1.0F : 1.0F) *
      2.0F * textScale;
    const std::string text = std::to_string(entry.damage);
    const float widthPixels = textWidth(text, textScale);
    addDamageText(
      drawList,
      anchorScreen.x + settings.damageNumbersOffsetX + wobble -
        widthPixels * 0.5F,
      anchorScreen.y + settings.damageNumbersOffsetY - 46.0F * baseScale -
        stackSlot * 11.0F * baseScale - drift,
      text,
      withAlpha(
        damageNumberColor(settings, entry.damage, entry.headshot),
        alphaScale * fade
      ),
      textScale,
      entry.headshot
    );
  }

  for (const DamageNumberTally& tally : hud.damageNumbers.tallies) {
    if (!tally.active || !tally.hasWorldPosition) {
      continue;
    }

    ProjectedPoint projected;
    if (!projectPerspectivePoint(camera, tally.worldPosition, projected)) {
      continue;
    }

    const ScreenPoint anchorScreen =
      screenPointFromProjection(projected, width, height);
    const float life =
      std::clamp(tally.secondsSinceLastHit / duration, 0.0F, 1.0F);
    const float fade = 1.0F - life * 0.55F;
    const float textScale = damageNumberScale(settings, tally.damage, 1.35F);
    const std::string text = std::to_string(tally.damage);
    const float widthPixels = textWidth(text, textScale);
    addDamageText(
      drawList,
      anchorScreen.x + settings.damageNumbersOffsetX - widthPixels * 0.5F,
      anchorScreen.y + settings.damageNumbersOffsetY - 34.0F * baseScale,
      text,
      withAlpha(
        damageNumberColor(settings, tally.damage, tally.headshot),
        alphaScale * fade
      ),
      textScale,
      tally.headshot
    );
  }
}

void addHud(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  constexpr float textScale = 2.0F;
  constexpr float characterWidth = kGlyphSize * textScale;
  constexpr RenderColor defaultText = {235, 242, 250, 255};
  constexpr float fpsRightOffset = 8.0F;
  constexpr float fpsTopOffset = 4.0F;

  if (!hud.fpsText.empty()) {
    const float fpsScale = std::clamp(settings.fpsTextScale, 0.5F, 6.0F);
    addText(
      drawList,
      static_cast<float>(width) - fpsRightOffset,
      fpsTopOffset,
      hud.fpsText,
      {245, 248, 252, 245},
      fpsScale,
      TextHorizontalAlignment::Right
    );
  }

  if (hud.scoreboardOpen) {
    const float panelWidth =
      std::min(720.0F, std::max(160.0F, static_cast<float>(width) - 32.0F));
    const float standingHeight = hud.freeForAllStandingRows.empty()
      ? 0.0F
      : 8.0F + static_cast<float>(hud.freeForAllStandingRows.size()) * 20.0F;
    const float minimumPanelY = standingHeight == 0.0F
      ? 12.0F
      : standingHeight + 20.0F;
    const float lineCount = static_cast<float>(
      std::max<std::size_t>(
        1U,
        hud.freeForAllScoreboard
          ? hud.freeForAllScoreboardRows.size() + 2U
          : hud.scoreboardLines.size()
      )
    );
    const float panelHeight = std::min(
      40.0F + lineCount * 28.0F,
      std::max(
        120.0F,
        static_cast<float>(height) - minimumPanelY - 12.0F
      )
    );
    const float rowHeight = std::min(28.0F, (panelHeight - 32.0F) / lineCount);
    // A full 16-player roster must remain readable on the minimum supported
    // viewport instead of extending beyond the screen. Scale both columns and
    // rows together so the scoreboard preserves its visual hierarchy.
    const float scoreboardScale = std::min({
      1.0F,
      rowHeight / 28.0F,
      std::max(0.5F, (panelWidth - 32.0F) / 660.0F),
    });
    const float scoreboardTextScale = textScale * scoreboardScale;
    const float panelX =
      (static_cast<float>(width) - panelWidth) * 0.5F;
    const float panelY =
      std::max(
        minimumPanelY,
        (static_cast<float>(height) - panelHeight) * 0.35F
      );
    addRect(
      drawList,
      panelX,
      panelY,
      panelWidth,
      panelHeight,
      {7, 11, 17, 225}
    );
    addOutline(
      drawList,
      panelX,
      panelY,
      panelWidth,
      panelHeight,
      {78, 168, 235, 255}
    );

    const float scoreboardX = panelX + std::max(
      16.0F,
      (panelWidth - 660.0F * scoreboardScale) * 0.5F
    );
    const float nameX = scoreboardX;
    const float scoreX = scoreboardX + 280.0F * scoreboardScale;
    const float accuracyX = scoreboardX + 360.0F * scoreboardScale;
    const float percentX = accuracyX + textWidth("LG ", scoreboardTextScale);
    const float damageX = scoreboardX + 470.0F * scoreboardScale;

    constexpr std::size_t kScoreboardNameColumnChars = 16U;
    constexpr std::size_t kScoreboardScoreColumnChars =
      kScoreboardNameColumnChars + 4U;
    constexpr std::size_t kScoreboardAccuracyColumnChars =
      kScoreboardScoreColumnChars + 6U;
    constexpr std::size_t kScoreboardDamageColumnChars =
      kScoreboardAccuracyColumnChars + 8U;

    float scoreboardY = panelY + std::max(8.0F, 20.0F * scoreboardScale);
    if (hud.freeForAllScoreboard) {
      const float rankX = scoreboardX;
      const float ffaNameX = scoreboardX + 72.0F * scoreboardScale;
      const float ffaScoreX = scoreboardX + 320.0F * scoreboardScale;
      const float ffaAccuracyX = scoreboardX + 400.0F * scoreboardScale;
      const float ffaDamageX = scoreboardX + 540.0F * scoreboardScale;
      addText(
        drawList,
        scoreboardX,
        scoreboardY,
        "FREE FOR ALL",
        {255, 220, 120, 255},
        scoreboardTextScale
      );
      scoreboardY += rowHeight;
      const RenderColor headerColor = {180, 200, 220, 255};
      addText(drawList, rankX, scoreboardY, "RANK", headerColor, scoreboardTextScale);
      addText(drawList, ffaNameX, scoreboardY, "NAME", headerColor, scoreboardTextScale);
      addText(drawList, ffaScoreX, scoreboardY, "SCORE", headerColor, scoreboardTextScale);
      addText(drawList, ffaAccuracyX, scoreboardY, "ACC", headerColor, scoreboardTextScale);
      addText(drawList, ffaDamageX, scoreboardY, "DAMAGE", headerColor, scoreboardTextScale);
      scoreboardY += rowHeight;
      for (const HudRenderState::FreeForAllScoreboardRow& row :
           hud.freeForAllScoreboardRows) {
        if (row.localPlayer) {
          addRect(
            drawList,
            panelX + 8.0F,
            scoreboardY - 4.0F * scoreboardScale,
            panelWidth - 16.0F,
            rowHeight,
            {34, 91, 126, 150}
          );
        }
        const RenderColor rowColor = row.localPlayer
          ? RenderColor{255, 232, 145, 255}
          : RenderColor{225, 235, 245, 255};
        addText(
          drawList,
          rankX,
          scoreboardY,
          std::to_string(row.rank),
          rowColor,
          scoreboardTextScale
        );
        addText(
          drawList,
          ffaNameX,
          scoreboardY,
          (row.localPlayer ? "> " : "  ") + row.name,
          rowColor,
          scoreboardTextScale
        );
        addText(
          drawList,
          ffaScoreX,
          scoreboardY,
          signedScore(row.score),
          rowColor,
          scoreboardTextScale
        );
        std::string weapon(weaponShortName(row.accuracyWeapon));
        std::transform(
          weapon.begin(),
          weapon.end(),
          weapon.begin(),
          [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
          }
        );
        addText(
          drawList,
          ffaAccuracyX,
          scoreboardY,
          weapon,
          quakeLiveWeaponColor(row.accuracyWeapon),
          scoreboardTextScale
        );
        addText(
          drawList,
          ffaAccuracyX + textWidth(weapon + " ", scoreboardTextScale),
          scoreboardY,
          std::to_string(row.accuracyPercent) + "%",
          rowColor,
          scoreboardTextScale
        );
        addText(
          drawList,
          ffaDamageX,
          scoreboardY,
          std::to_string(row.totalDamage),
          rowColor,
          scoreboardTextScale
        );
        scoreboardY += rowHeight;
      }
    } else {
    for (std::size_t index = 0; index < hud.scoreboardLines.size(); ++index) {
      const std::string& line = hud.scoreboardLines[index];
      const Team team = index < hud.scoreboardLineTeams.size()
        ? hud.scoreboardLineTeams[index]
        : Team::None;
      const std::size_t weaponColumn =
        index < hud.scoreboardLineAccuracyWeaponColumns.size()
          ? hud.scoreboardLineAccuracyWeaponColumns[index]
          : std::string::npos;
      const bool hasWeaponColumn =
        weaponColumn != std::string::npos &&
        weaponColumn + 2U <= line.size() &&
        index < hud.scoreboardLineAccuracyWeapons.size();
      if (index == 0) {
        addText(
          drawList,
          scoreboardX,
          scoreboardY,
          line,
          {255, 220, 120, 255},
          scoreboardTextScale
        );
      } else {
        const RenderColor baseColor = {225, 235, 245, 255};
        const RenderColor teamColor = team == Team::Red
          ? RenderColor{224, 82, 92, 255}
          : RenderColor{82, 190, 224, 255};
        const RenderColor nameColor =
          team == Team::None ? baseColor : teamColor;
        const auto cell = [&line](std::size_t start, std::size_t end) {
          if (start >= line.size()) {
            return std::string();
          }
          return trimCell(line.substr(start, std::min(end, line.size()) - start));
        };
        const std::string name = cell(0U, kScoreboardNameColumnChars);
        const std::string score = cell(
          kScoreboardScoreColumnChars,
          kScoreboardAccuracyColumnChars - 1U
        );
        const std::string accuracy = cell(
          kScoreboardAccuracyColumnChars,
          kScoreboardDamageColumnChars - 1U
        );
        const std::string damage =
          cell(kScoreboardDamageColumnChars, line.size());

        if (!name.empty()) {
          addText(
            drawList,
            nameX,
            scoreboardY,
            name,
            nameColor,
            scoreboardTextScale
          );
        }
        if (!score.empty()) {
          addText(drawList, scoreX, scoreboardY, score, baseColor, scoreboardTextScale);
        }
        if (hasWeaponColumn && accuracy.size() >= 2U) {
          addText(
            drawList,
            accuracyX,
            scoreboardY,
            accuracy.substr(0U, 2U),
            quakeLiveWeaponColor(hud.scoreboardLineAccuracyWeapons[index]),
            scoreboardTextScale
          );
          const std::string percent = trimCell(accuracy.substr(2U));
          if (!percent.empty()) {
            addText(
              drawList,
              percentX,
              scoreboardY,
              percent,
              baseColor,
              scoreboardTextScale
            );
          }
        } else if (!accuracy.empty()) {
          addText(
            drawList,
            accuracyX,
            scoreboardY,
            accuracy,
            baseColor,
            scoreboardTextScale
          );
        }
        if (!damage.empty()) {
          addText(drawList, damageX, scoreboardY, damage, baseColor, scoreboardTextScale);
        }
      }
      scoreboardY += rowHeight;
    }
    }
  }

  if (!hud.freeForAllStandingRows.empty()) {
    const float cardWidth = std::min(
      360.0F,
      std::max(180.0F, static_cast<float>(width) - 24.0F)
    );
    const float cardHeight =
      8.0F + static_cast<float>(hud.freeForAllStandingRows.size()) * 20.0F;
    const float cardX = (static_cast<float>(width) - cardWidth) * 0.5F;
    constexpr float cardY = 6.0F;
    constexpr float standingScale = 1.5F;
    addRect(drawList, cardX, cardY, cardWidth, cardHeight, {7, 11, 17, 220});
    addOutline(
      drawList,
      cardX,
      cardY,
      cardWidth,
      cardHeight,
      {78, 168, 235, 245}
    );
    float standingY = cardY + 5.0F;
    for (const HudRenderState::FreeForAllStandingRow& row :
         hud.freeForAllStandingRows) {
      const RenderColor color = row.localPlayer
        ? RenderColor{255, 232, 145, 255}
        : RenderColor{235, 242, 250, 255};
      addText(
        drawList,
        cardX + 8.0F,
        standingY,
        "#" + std::to_string(row.rank),
        color,
        standingScale
      );
      addText(
        drawList,
        cardX + 52.0F,
        standingY,
        (row.localPlayer ? "> " : "  ") + row.name,
        color,
        standingScale
      );
      addText(
        drawList,
        cardX + cardWidth - 8.0F,
        standingY,
        signedScore(row.score),
        color,
        standingScale,
        TextHorizontalAlignment::Right
      );
      standingY += 20.0F;
    }
  }

  if (hud.killcam.active) {
    const float panelWidth = std::min(
      620.0F,
      std::max(260.0F, static_cast<float>(width) - 24.0F)
    );
    const float panelHeight = 126.0F;
    const float panelX = (static_cast<float>(width) - panelWidth) * 0.5F;
    constexpr float panelY = 34.0F;
    addRect(drawList, panelX, panelY, panelWidth, panelHeight, {7, 11, 17, 232});
    addOutline(drawList, panelX, panelY, panelWidth, panelHeight,
               {235, 90, 70, 245});
    addText(drawList, static_cast<float>(width) * 0.5F, panelY + 8.0F,
            "KILLCAM", {255, 225, 190, 255}, 2.6F,
            TextHorizontalAlignment::Center);
    addText(drawList, static_cast<float>(width) * 0.5F, panelY + 38.0F,
            "KILLED BY " + hud.killcam.killer + " - " + hud.killcam.weapon,
            {235, 242, 250, 255}, 1.75F, TextHorizontalAlignment::Center);
    addText(drawList, static_cast<float>(width) * 0.5F, panelY + 59.0F,
            hud.killcam.cause, {225, 195, 170, 245}, 1.5F,
            TextHorizontalAlignment::Center);
    constexpr float barXPadding = 28.0F;
    constexpr float barY = panelY + 83.0F;
    const float barWidth = panelWidth - barXPadding * 2.0F;
    addRect(drawList, panelX + barXPadding, barY, barWidth, 8.0F,
            {35, 39, 48, 255});
    addRect(drawList, panelX + barXPadding, barY,
            barWidth * std::clamp(hud.killcam.progress, 0.0F, 1.0F), 8.0F,
            {235, 90, 70, 255});
    addText(drawList, static_cast<float>(width) * 0.5F, panelY + 98.0F,
            hud.killcam.prompt, {255, 240, 220, 255}, 1.35F,
            TextHorizontalAlignment::Center);
  }

  float y = 12.0F;
  for (const std::string& line : hud.topLeftLines) {
    addText(drawList, 12.0F, y, line, defaultText, textScale);
    y += 20.0F;
  }

  y = 12.0F;
  for (const std::string& line : hud.topCenterLines) {
    addText(
      drawList,
      static_cast<float>(width) * 0.5F,
      y,
      line,
      defaultText,
      textScale,
      TextHorizontalAlignment::Center
    );
    y += 20.0F;
  }

  y = hud.fpsText.empty()
    ? 12.0F
    : std::max(24.0F, fpsTopOffset + kGlyphSize *
        std::clamp(settings.fpsTextScale, 0.5F, 6.0F) + 6.0F);
  addKillFeed(drawList, width, y, hud);
  y += static_cast<float>(hud.killFeedLines.size()) * 31.5F;
  for (const std::string& line : hud.topRightLines) {
    const float x = std::max(
      12.0F,
      static_cast<float>(width) - 12.0F -
        static_cast<float>(line.size()) * characterWidth
    );
    addText(drawList, x, y, line, defaultText, textScale);
    y += 20.0F;
  }

  y = (static_cast<float>(height) * 0.5F) -
    static_cast<float>(hud.centerLines.size()) * 11.0F +
    hud.centerOffsetY;
  for (const std::string& line : hud.centerLines) {
    addText(
      drawList,
      static_cast<float>(width) * 0.5F,
      y,
      line,
      defaultText,
      textScale,
      TextHorizontalAlignment::Center
    );
    y += 22.0F;
  }

  if (!hud.countdownText.empty()) {
    const float pulse = std::clamp(hud.countdownPulse, 0.0F, 1.0F);
    const float scale = 10.0F + pulse * 3.0F;
    const float textWidth =
      static_cast<float>(hud.countdownText.size()) * kGlyphSize * scale;
    const float textHeight = kGlyphSize * scale;
    const float cellX = (static_cast<float>(width) - textWidth) * 0.5F;
    const float cellY =
      (static_cast<float>(height) - textHeight) * 0.5F;
    const float textX = cellX + countdownGlyphOffsetX(hud.countdownText, scale);
    const float textY = cellY + 0.5F * scale;
    const float padding = 24.0F + pulse * 12.0F;
    addRect(
      drawList,
      cellX - padding,
      cellY - padding,
      textWidth + padding * 2.0F,
      textHeight + padding * 2.0F,
      {
        8,
        12,
        18,
        static_cast<std::uint8_t>(150.0F + pulse * 55.0F),
      }
    );
    addText(
      drawList,
      textX,
      textY,
      hud.countdownText,
      {
        255,
        static_cast<std::uint8_t>(175.0F + pulse * 70.0F),
        static_cast<std::uint8_t>(75.0F + pulse * 80.0F),
        255,
      },
      scale
    );
  }

  const float bottomY = static_cast<float>(height) - 24.0F;
  if (settings.healthStyle >= 3) {
    addArtHealthBar(drawList, width, height, localPlayer, hud, settings);
  } else if (settings.healthStyle == 2) {
    addCrosshairHealthAndAmmo(
      drawList,
      width,
      height,
      localPlayer,
      hud,
      settings
    );
  } else if (settings.healthStyle == 1) {
    addLocalHealthNumber(drawList, width, height, localPlayer, hud, settings);
  } else {
    addLocalHealthBar(
      drawList,
      width,
      height,
      localPlayer,
      hud,
      settings,
      bottomY
    );
  }

  std::vector<std::string> bottomLines;
  for (const std::string& line : hud.bottomCenterLines) {
    if (line.rfind("HEALTH ", 0) == 0) {
      continue;
    }
    bottomLines.push_back(line);
  }
  const float healthCharacterWidth = kGlyphSize * settings.healthTextScale;
  const float healthLineHeight = 11.0F * settings.healthTextScale;
  const float healthScale = std::clamp(settings.healthTextScale, 0.5F, 20.0F);
  y = bottomY -
    kGlyphSize * std::max(0.75F, healthScale * 0.72F) -
    std::max(4.0F, 3.0F * healthScale) -
    13.0F * healthScale -
    std::max(1.0F, std::round(1.0F * healthScale)) * 2.0F -
    static_cast<float>(bottomLines.size()) * healthLineHeight -
    8.0F * healthScale;
  for (const std::string& line : bottomLines) {
    const float x = std::max(
      12.0F,
      (static_cast<float>(width) -
       static_cast<float>(line.size()) * healthCharacterWidth) * 0.5F
    );
    addText(
      drawList,
      x,
      y,
      line,
      defaultText,
      settings.healthTextScale
    );
    y += healthLineHeight;
  }

  const ChatTextLayout chatLayout = buildChatTextLayout(width, height, hud);
  if (hud.chatHistoryHasSelection &&
      hud.chatHistorySelectionAnchor != hud.chatHistorySelectionFocus) {
    const std::size_t selectionBegin =
        std::min(hud.chatHistorySelectionAnchor, hud.chatHistorySelectionFocus);
    const std::size_t selectionEnd =
        std::max(hud.chatHistorySelectionAnchor, hud.chatHistorySelectionFocus);
    for (const ChatLayoutRow &row : chatLayout.rows) {
      const std::size_t rowBegin = row.textOffset;
      const std::size_t rowEnd = row.textOffset + row.text.size();
      const std::size_t begin = std::max(selectionBegin, rowBegin);
      const std::size_t end = std::min(selectionEnd, rowEnd);
      if (begin >= end) {
        continue;
      }
      const float selectionX =
          row.x + static_cast<float>(
                      utf8GlyphCount(row.text.substr(0U, begin - rowBegin))) *
                      chatLayout.characterWidth;
      const float selectionWidth =
          static_cast<float>(
              utf8GlyphCount(row.text.substr(begin - rowBegin, end - begin))) *
          chatLayout.characterWidth;
      addRect(drawList, selectionX, row.y, selectionWidth,
              chatLayout.lineHeight, {58, 118, 188, 170});
    }
  }
  for (const ChatLayoutRow& row : chatLayout.rows) {
    addText(drawList, row.x, row.y, row.text, {225, 235, 245, 255}, 2.0F);
  }
  if (
    hud.chatHistoryExpanded &&
    chatLayout.totalHistoryRows > 0U &&
    chatLayout.visibleHistoryRows > 0U
  ) {
    constexpr float trackWidth = 8.0F;
    constexpr float trackGap = 12.0F;
    constexpr float screenMargin = 12.0F;
    constexpr float minimumThumbHeight = 18.0F;
    // Keep the indicator attached to the chat block; at the screen edge it
    // looks like an unrelated HUD element and is easy to miss.
    const float trackX = std::min(
      chatLayout.historyRight + trackGap,
      static_cast<float>(width) - screenMargin - trackWidth
    );
    const float trackY = chatLayout.historyTop;
    const float trackHeight = chatLayout.historyBottom - chatLayout.historyTop;
    const float visibleRatio =
      static_cast<float>(chatLayout.visibleHistoryRows) /
      static_cast<float>(chatLayout.totalHistoryRows);
    const float thumbHeight = std::clamp(
      trackHeight * visibleRatio,
      minimumThumbHeight,
      trackHeight
    );
    const std::size_t maximumFirstRow =
      chatLayout.totalHistoryRows - chatLayout.visibleHistoryRows;
    const float historyPosition = maximumFirstRow > 0U
      ? static_cast<float>(chatLayout.firstVisibleHistoryRow) /
        static_cast<float>(maximumFirstRow)
      : 1.0F;
    const float thumbY =
      trackY + (trackHeight - thumbHeight) * historyPosition;
    addRect(
      drawList,
      trackX,
      trackY,
      trackWidth,
      trackHeight,
      {12, 18, 24, 185}
    );
    addRect(
      drawList,
      trackX,
      thumbY,
      trackWidth,
      thumbHeight,
      {112, 190, 224, 245}
    );
    const std::string positionText =
      "ROWS " + std::to_string(chatLayout.firstVisibleHistoryRow + 1U) +
      '-' + std::to_string(
        chatLayout.firstVisibleHistoryRow + chatLayout.visibleHistoryRows
      ) + " / " + std::to_string(chatLayout.totalHistoryRows);
    addText(
      drawList,
      trackX,
      trackY - 16.0F,
      positionText,
      {168, 205, 222, 255},
      1.25F
    );
  }
  if (hud.chatInputOpen) {
    if (!chatLayout.inputRows.empty()) {
      addRect(
        drawList,
        chatLayout.input.x - 6.0F,
        chatLayout.input.y - 3.0F,
        static_cast<float>(width) - chatLayout.input.x * 2.0F + 12.0F,
        static_cast<float>(chatLayout.inputRows.size()) * chatLayout.input.lineHeight + 6.0F,
        {6, 9, 13, 170}
      );
    }
    if (hud.chatHasSelection && hud.chatSelectionAnchor != hud.chatSelectionFocus) {
      const std::size_t begin =
        std::min(hud.chatSelectionAnchor, hud.chatSelectionFocus);
      const std::size_t end =
        std::min(
          std::max(hud.chatSelectionAnchor, hud.chatSelectionFocus),
          hud.chatInput.size()
        );
      for (const ChatInputRow& row : chatLayout.inputRows) {
        const std::size_t rowBegin = row.inputBegin;
        const std::size_t rowEnd = row.inputEnd;
        const std::size_t highlightBegin = std::max(begin, rowBegin);
        const std::size_t highlightEnd = std::min(end, rowEnd);
        if (highlightBegin >= highlightEnd) {
          continue;
        }
        const float selectionX =
          row.x +
          static_cast<float>(
            row.contentColumn +
            utf8GlyphCount(
              hud.chatInput.substr(rowBegin, highlightBegin - rowBegin)
            )
          ) * chatLayout.input.characterWidth;
        const float selectionWidth =
          static_cast<float>(
            utf8GlyphCount(
              hud.chatInput.substr(highlightBegin, highlightEnd - highlightBegin)
            )
          ) * chatLayout.input.characterWidth;
        addRect(
          drawList,
          selectionX,
          row.y,
          selectionWidth,
          chatLayout.input.lineHeight,
          {58, 118, 188, 170}
        );
      }
    }
    const ScreenPoint cursor =
      chatInputCursorPosition(chatLayout, hud.chatInput, hud.chatCursorIndex);
    for (const ChatInputRow& row : chatLayout.inputRows) {
      std::string text = row.text;
      if (std::abs(row.y - cursor.y) < 0.01F) {
        const auto cursorColumn = static_cast<std::size_t>(
          std::round((cursor.x - row.x) / chatLayout.input.characterWidth)
        );
        text.insert(utf8ByteOffsetForGlyph(text, cursorColumn), 1U, '_');
      }
      addText(
        drawList,
        row.x,
        row.y,
        std::move(text),
        {255, 232, 150, 255},
        2.0F
      );
    }
  }
}

void addOptionMenu(DrawList2D &drawList, int width, int height,
                   std::string_view title,
                   const std::vector<HudRenderState::SettingsMenuItem> &items,
                   std::size_t scrollRows, int hoveredRow, int pressedRow,
                   std::string_view footer) {

  addRect(
    drawList,
    0.0F,
    0.0F,
    static_cast<float>(width),
    static_cast<float>(height),
    {0, 0, 0, 120}
  );

  const OptionMenuLayout layout =
      buildOptionMenuLayout(width, height, items.size(), scrollRows);
  const float panelWidth = layout.panelWidth;
  const float rowHeight = layout.rowHeight;
  const float panelHeight = layout.panelHeight;
  const float panelX = layout.panelX;
  const float panelY = layout.panelY;
  addRect(drawList, panelX, panelY, panelWidth, panelHeight, {6, 10, 15, 238});
  addOutline(drawList, panelX, panelY, panelWidth, panelHeight,
             {88, 176, 232, 255});
  addRect(drawList, panelX, panelY, panelWidth, 3.0F, {255, 212, 92, 255});

  addText(drawList, panelX + 22.0F, panelY + 26.0F, std::string(title),
          {255, 226, 132, 255}, 2.5F);

  float y = layout.firstRowY;
  constexpr float textScale = 2.25F;
  constexpr float characterWidth = kGlyphSize * textScale;
  const float labelX = panelX + 28.0F;
  // Keep values on one visual column. The fixed navigation control sits to
  // the right of it, so changing text length never moves either control.
  const float arrowX = panelX + panelWidth - 28.0F - 9.0F * characterWidth;
  const float valueRight = arrowX - 14.0F;
  const float footerY = layout.footerY;
  const std::size_t visibleRows = layout.visibleRows;
  const std::size_t firstRow = std::min(scrollRows, layout.maxScrollRows);
  const std::size_t lastRow = std::min(items.size(), firstRow + visibleRows);
  for (std::size_t index = firstRow; index < lastRow; ++index) {
    const HudRenderState::SettingsMenuItem &item = items[index];
    const RenderColor labelColor = item.active
      ? RenderColor{255, 244, 184, 255}
      : RenderColor{214, 226, 238, 255};
    const RenderColor valueColor = item.changed
      ? RenderColor{255, 210, 95, 255}
      : RenderColor{156, 214, 242, 255};
    const bool hovered = static_cast<int>(index) == hoveredRow;
    const bool pressed = static_cast<int>(index) == pressedRow;
    if (item.active || hovered) {
      addRect(
        drawList,
        panelX + 14.0F,
        y - 5.0F,
        panelWidth - 28.0F,
        rowHeight,
        pressed ? RenderColor{76, 112, 134, 235} :
          (hovered ? RenderColor{42, 72, 92, 230} : RenderColor{32, 54, 70, 220})
      );
    }
    if (item.active) {
      addRect(drawList, panelX + 18.0F, y + 1.0F, 4.0F, 18.0F, {255, 212, 92, 255});
    }
    addText(drawList, labelX, y, item.label, labelColor, textScale);
    addText(
      drawList,
      valueRight,
      y,
      item.value,
      valueColor,
      textScale,
      TextHorizontalAlignment::Right
    );
    if (!item.command) {
      addText(drawList, arrowX, y, "<  >", hovered ? RenderColor{190, 226, 245, 255} : RenderColor{110, 128, 144, 255}, textScale);
    }
    y += rowHeight;
  }

  if (items.size() > visibleRows) {
    addRect(drawList, layout.scrollbarTrackX, layout.scrollbarTrackY,
            layout.scrollbarTrackWidth, layout.scrollbarTrackHeight,
            {56, 80, 96, 220});
    addRect(drawList, layout.scrollbarThumbX, layout.scrollbarThumbY,
            layout.scrollbarThumbWidth, layout.scrollbarThumbHeight,
            {120, 202, 238, 255});
  }

  if (!footer.empty()) {
    constexpr float footerScale = 2.0F;
    constexpr float footerLineHeight = 18.0F;
    const float footerWidth = std::max(1.0F, panelWidth - 44.0F);
    const std::size_t footerColumns = static_cast<std::size_t>(
        std::max(1.0F, std::floor(footerWidth /
                                  (kGlyphSize *
                                   snappedTextScale(footerScale)))));
    const std::vector<std::string> footerLines =
        wrapOptionMenuFooter(footer, footerColumns);
    float lineY =
        footerY - (footerLines.size() > 1U ? 8.0F : 0.0F);
    for (const std::string &line : footerLines) {
      addText(drawList, panelX + 22.0F, lineY, line,
              {174, 190, 204, 255}, footerScale);
      lineY += footerLineHeight;
    }
  }
}

void addSettingsMenu(DrawList2D &drawList, int width, int height,
                     const HudRenderState &hud) {
  addOptionMenu(drawList, width, height, "SETTINGS / VIDEO", hud.settingsItems,
                hud.settingsScrollRows, hud.settingsHoveredRow,
                hud.settingsPressedRow, hud.settingsFooter);
}

void addMiscMenu(DrawList2D &drawList, int width, int height,
                 const HudRenderState &hud) {
  addOptionMenu(drawList, width, height, "TOOLS / DEBUG", hud.miscMenuItems,
                hud.miscMenuScrollRows, hud.miscMenuHoveredRow,
                hud.miscMenuPressedRow, hud.miscMenuFooter);
}

void addTrainerMenu(DrawList2D &drawList, int width, int height,
                    const HudRenderState &hud) {
  addOptionMenu(drawList, width, height, "AIM TRAINER / SCENARIO",
                hud.trainerMenuItems, hud.trainerMenuScrollRows,
                hud.trainerMenuHoveredRow, hud.trainerMenuPressedRow,
                hud.trainerMenuFooter);
}

void addConsole(
  DrawList2D& drawList,
  int width,
  int height,
  const ConsoleRenderState& console
) {
  if (!console.open) {
    return;
  }

  const float consoleHeight = static_cast<float>(height) * 0.55F;
  addRect(
    drawList,
    0.0F,
    0.0F,
    static_cast<float>(width),
    consoleHeight,
    {5, 8, 12, 235}
  );
  addRect(
    drawList,
    0.0F,
    consoleHeight - 2.0F,
    static_cast<float>(width),
    2.0F,
    {92, 170, 230, 255}
  );

  if (console.showCat) {
    constexpr float catPixel = 3.0F;
  const CatSprite& sprite = catSprite(
    console.cat.action,
    console.cat.frame,
    console.cat.profile
  );
  const float spriteWidth = static_cast<float>(kCatSpriteWidth) * catPixel;
  const float spriteHeight = static_cast<float>(sprite.size()) * catPixel;
  const float spriteLeft = console.cat.position.x - spriteWidth * 0.5F;
  const float spriteTop = console.cat.position.y - spriteHeight;
  if (console.cat.action != ConsoleCatAction::Leap) {
    addRect(
      drawList,
      console.cat.position.x - 36.0F,
      console.cat.position.y - 2.0F,
      72.0F,
      4.0F,
      {0, 0, 0, 105}
    );
  }
  for (std::size_t row = 0; row < sprite.size(); ++row) {
    for (std::size_t column = 0; column < kCatSpriteWidth; ++column) {
      const std::size_t sourceColumn = console.cat.facingRight
        ? column
        : kCatSpriteWidth - column - 1U;
      const char pixel = sourceColumn < sprite[row].size()
        ? sprite[row][sourceColumn]
        : ' ';
      RenderColor color;
      switch (pixel) {
      case 'X': color = {24, 22, 20, 255}; break;
      case 'q': color = {78, 45, 27, 255}; break;
      case 'd': color = {190, 132, 73, 255}; break;
      case 'g': color = {247, 244, 235, 255}; break;
      case 'h': color = {218, 207, 185, 255}; break;
      case 'w': color = {255, 252, 247, 255}; break;
      case 'c': color = {24, 22, 20, 255}; break;
      case 'p': color = {247, 203, 218, 255}; break;
      case 'b': color = {247, 154, 181, 255}; break;
      default: continue;
      }
      addRect(
        drawList,
        spriteLeft + static_cast<float>(column) * catPixel,
        spriteTop + static_cast<float>(row) * catPixel,
        catPixel,
        catPixel,
        color
      );
    }
  }

  if (console.cat.action == ConsoleCatAction::Sleep) {
    const float direction = console.cat.facingRight ? 1.0F : -1.0F;
    const float phase = static_cast<float>(console.cat.frame);
    const float faceX = console.cat.position.x + direction * 20.0F;
    addText(
      drawList,
      faceX,
      console.cat.position.y - 52.0F - phase * 1.5F,
      "Z",
      {220, 232, 245, 225},
      1.0F
    );
    if (console.cat.frame >= 2U) {
      addText(
        drawList,
        faceX + direction * 10.0F,
        console.cat.position.y - 68.0F - (phase - 2.0F) * 1.5F,
        "Z",
        {190, 208, 228, 180},
        1.25F
      );
    }
    if (console.cat.frame >= 4U) {
      addText(
        drawList,
        faceX + direction * 23.0F,
        console.cat.position.y - 88.0F - (phase - 4.0F) * 1.5F,
        "Z",
        {160, 184, 212, 135},
        1.5F
      );
    }
  }
  }

  if (console.showCat) {
    // The laser is part of the cat feature, so the visibility cvar must hide
    // it with the sprite, shadow, and sleep markers.
    const float laserX = console.cat.laser.x;
    const float laserY = console.cat.laser.y;
    addRect(drawList, laserX - 14.0F, laserY - 5.0F, 28.0F, 10.0F, {255, 24, 40, 38});
    addRect(drawList, laserX - 5.0F, laserY - 14.0F, 10.0F, 28.0F, {255, 24, 40, 38});
    addRect(drawList, laserX - 9.0F, laserY - 7.0F, 18.0F, 14.0F, {255, 34, 48, 76});
    addRect(drawList, laserX - 7.0F, laserY - 9.0F, 14.0F, 18.0F, {255, 34, 48, 76});
    addRect(drawList, laserX - 5.0F, laserY - 5.0F, 10.0F, 10.0F, {255, 58, 72, 190});
    addRect(drawList, laserX - 3.0F, laserY - 3.0F, 6.0F, 6.0F, {255, 112, 118, 245});
    addRect(drawList, laserX + 11.0F, laserY - 9.0F, 4.0F, 4.0F, {255, 42, 58, 78});
    addRect(drawList, laserX - 13.0F, laserY + 9.0F, 3.0F, 3.0F, {255, 42, 58, 62});
  }

  constexpr float textScale = 2.0F;
  const ConsoleTextLayout layout = buildConsoleTextLayout(width, height, console);
  if (console.hasSelection && console.selectionAnchor != console.selectionFocus) {
    const std::size_t selectionBegin =
      std::min(console.selectionAnchor, console.selectionFocus);
    const std::size_t selectionEnd =
      std::max(console.selectionAnchor, console.selectionFocus);
    for (const ConsoleLayoutLine& line : layout.lines) {
      const std::size_t lineBegin = line.textOffset;
      const std::size_t lineEnd = line.textOffset + line.text.size();
      const std::size_t begin = std::max(selectionBegin, lineBegin);
      const std::size_t end = std::min(selectionEnd, lineEnd);
      if (begin >= end) {
        continue;
      }
      const float x = line.x + static_cast<float>(utf8GlyphCount(
                                   line.text.substr(0U, begin - lineBegin))) *
                                   layout.characterWidth;
      addRect(drawList, x, line.y,
              static_cast<float>(utf8GlyphCount(
                  line.text.substr(begin - lineBegin, end - begin))) *
                  layout.characterWidth,
              layout.lineHeight, {58, 118, 188, 170});
    }
  }
  if (console.inputHasSelection &&
      console.inputSelectionAnchor != console.inputSelectionFocus) {

    const std::size_t selectionBegin =
        std::min(console.inputSelectionAnchor, console.inputSelectionFocus);
    const std::size_t selectionEnd = std::min(
        std::max(console.inputSelectionAnchor, console.inputSelectionFocus),
        console.input.size());
    for (const ConsoleLayoutLine &line : layout.lines) {
      if (!line.prompt) {
        continue;
      }
      const std::size_t begin = std::max(selectionBegin, line.inputBegin);
      const std::size_t end = std::min(selectionEnd, line.inputEnd);
      if (begin >= end) {
        continue;
      }
      const float selectionX =
          line.x +
          static_cast<float>(line.contentColumn +
                             utf8GlyphCount(console.input.substr(
                                 line.inputBegin, begin - line.inputBegin))) *
              layout.characterWidth;
      const float selectionWidth =
          static_cast<float>(
              utf8GlyphCount(console.input.substr(begin, end - begin))) *
          layout.characterWidth;
      addRect(drawList, selectionX, line.y, selectionWidth, layout.lineHeight,
              {58, 118, 188, 170});
    }
  }

  const ScreenPoint cursor =
      consoleInputCursorPosition(layout, console.input, console.cursorIndex);
  for (const ConsoleLayoutLine &line : layout.lines) {
    std::string text = line.text;
    if (line.prompt && std::abs(line.y - cursor.y) < 0.01F) {
      const auto cursorColumn = static_cast<std::size_t>(
          std::round((cursor.x - line.x) / layout.characterWidth));
      text.insert(utf8ByteOffsetForGlyph(text, cursorColumn), 1U, '_');
    }
    addText(
      drawList,
      line.x,
      line.y,
      std::move(text),
      line.prompt ? RenderColor{255, 255, 255, 255} : RenderColor{215, 225, 235, 255},
      textScale
    );
  }
}

} // namespace

McGuffinNavigationProjection projectMcGuffinNavigationTarget(
  const McGuffinNavigationTarget& target,
  const PerspectiveCamera& camera,
  int outputWidth,
  int outputHeight
) {
  McGuffinNavigationProjection result;
  if (
    !target.active ||
    target.kind == McGuffinNavigationKind::None ||
    outputWidth <= 0 ||
    outputHeight <= 0 ||
    !std::isfinite(camera.aspectRatio) ||
    camera.aspectRatio <= 0.0F ||
    !std::isfinite(camera.focalLength) ||
    camera.focalLength <= 0.0F ||
    !std::isfinite(camera.nearPlane) ||
    camera.nearPlane <= 0.0F
  ) {
    return result;
  }

  const Vec3 offset = target.worldPosition - camera.position;
  result.distance = length(offset);
  if (!std::isfinite(result.distance)) {
    return {};
  }
  result.valid = true;
  const ScreenPoint screenCenter = {
    static_cast<float>(outputWidth) * 0.5F,
    static_cast<float>(outputHeight) * 0.5F,
  };
  // A target at the camera is already reached. Keep the result finite and
  // quiet instead of producing a false edge arrow from a zero vector.
  if (result.distance <= 0.05F) {
    result.onScreen = true;
    result.screenPosition = screenCenter;
    result.edgePosition = screenCenter;
    return result;
  }

  const Vec3 view = perspectiveCameraSpace(camera, target.worldPosition);
  if (
    !std::isfinite(view.x) ||
    !std::isfinite(view.y) ||
    !std::isfinite(view.z)
  ) {
    return {};
  }
  result.behind = view.z < 0.0F;

  ProjectedPoint projected;
  ScreenPoint direction = {};
  if (view.z >= camera.nearPlane && projectPerspectivePoint(
      camera,
      target.worldPosition,
      projected
    )) {
    result.screenPosition = {
      (projected.x + 1.0F) * 0.5F * static_cast<float>(outputWidth),
      (1.0F - projected.y) * 0.5F * static_cast<float>(outputHeight),
    };
    if (
      std::isfinite(projected.x) &&
      std::isfinite(projected.y) &&
      projected.x >= -1.0F && projected.x <= 1.0F &&
      projected.y >= -1.0F && projected.y <= 1.0F
    ) {
      result.onScreen = true;
      result.edgePosition = result.screenPosition;
      return result;
    }
    direction = {projected.x, -projected.y};
  } else {
    // A point behind the camera cannot use perspective division. Its camera
    // plane direction still gives a stable turn cue, including across yaw
    // wrap and aspect changes.
    direction = {
      view.x / std::max(0.001F, camera.aspectRatio),
      -view.y,
    };
  }

  float directionLength = std::hypot(direction.x, direction.y);
  if (!std::isfinite(directionLength) || directionLength <= 0.0001F) {
    direction = result.behind ? ScreenPoint{0.0F, 1.0F}
                              : ScreenPoint{0.0F, -1.0F};
    directionLength = 1.0F;
  }
  direction.x /= directionLength;
  direction.y /= directionLength;

  const NavigationSafeBounds bounds = navigationSafeBounds(
    outputWidth,
    outputHeight
  );
  const float toLeft = direction.x < 0.0F
    ? (bounds.left - screenCenter.x) / direction.x
    : std::numeric_limits<float>::infinity();
  const float toRight = direction.x > 0.0F
    ? (bounds.right - screenCenter.x) / direction.x
    : std::numeric_limits<float>::infinity();
  const float toTop = direction.y < 0.0F
    ? (bounds.top - screenCenter.y) / direction.y
    : std::numeric_limits<float>::infinity();
  const float toBottom = direction.y > 0.0F
    ? (bounds.bottom - screenCenter.y) / direction.y
    : std::numeric_limits<float>::infinity();
  float distanceToEdge = std::min({toLeft, toRight, toTop, toBottom});
  if (!std::isfinite(distanceToEdge) || distanceToEdge < 0.0F) {
    distanceToEdge = 0.0F;
  }
  result.edgePosition = {
    std::clamp(
      screenCenter.x + direction.x * distanceToEdge,
      bounds.left,
      bounds.right
    ),
    std::clamp(
      screenCenter.y + direction.y * distanceToEdge,
      bounds.top,
      bounds.bottom
    ),
  };
  return result;
}

DrawList2D buildPerspectiveWeaponOverlay(
  int outputWidth,
  int outputHeight,
  const LightningGunResult& localLightningGun,
  Weapon selectedWeapon,
  Weapon previousWeapon,
  float weaponSwitchProgress,
  const RenderSettings& settings,
  ScreenPoint beamMuzzle
) {
  DrawList2D drawList;
  const float hitAmount = std::clamp(settings.beamHitAmount, 0.0F, 1.0F);
  const bool freezeGunSelected = selectedWeapon == Weapon::FreezeGun;
  const RenderColor color = {
    blendChannel(freezeGunSelected ? 154U : settings.beamRed, freezeGunSelected ? 230U : settings.beamHitRed, hitAmount),
    blendChannel(freezeGunSelected ? 232U : settings.beamGreen, freezeGunSelected ? 255U : settings.beamHitGreen, hitAmount),
    blendChannel(freezeGunSelected ? 255U : settings.beamBlue, freezeGunSelected ? 255U : settings.beamHitBlue, hitAmount),
    static_cast<std::uint8_t>(
      std::clamp(settings.beamAlpha, 0.0F, 1.0F) * 255.0F
    ),
  };
  const float pulse = localLightningGun.active
    ? std::clamp(settings.beamPulse, -1.0F, 1.0F)
    : 0.0F;
  const float brightness = 1.0F + pulse * 0.05F;
  const RenderColor animatedColor = {
    static_cast<std::uint8_t>(
      std::clamp(
        static_cast<float>(color.red) * brightness,
        0.0F,
        255.0F
      )
    ),
    static_cast<std::uint8_t>(
      std::clamp(
        static_cast<float>(color.green) * brightness,
        0.0F,
        255.0F
      )
    ),
    static_cast<std::uint8_t>(
      std::clamp(
        static_cast<float>(color.blue) * brightness,
        0.0F,
        255.0F
      )
    ),
    color.alpha,
  };
  const RenderColor emitterColor = {
    static_cast<std::uint8_t>(
      static_cast<float>(animatedColor.red) * 0.8F
    ),
    static_cast<std::uint8_t>(
      static_cast<float>(animatedColor.green) * 0.8F
    ),
    static_cast<std::uint8_t>(
      static_cast<float>(animatedColor.blue) * 0.8F
    ),
    animatedColor.alpha,
  };
  const float centerX = static_cast<float>(outputWidth) * 0.5F;
  const float height = static_cast<float>(outputHeight);
  const float scale = std::max(0.7F, height / 720.0F);
  const float weaponSide = settings.weaponPosition == 1
    ? 1.0F
    : (settings.weaponPosition == 2 ? -1.0F : 0.0F);
  const float weaponCenterX = centerX + weaponSide * 168.0F * scale;
  const float muzzleY = height - 154.0F * scale;
  if (localLightningGun.active) {
    if (freezeGunSelected) {
      const ScreenPoint start = beamMuzzle.x >= 0.0F
        ? beamMuzzle
        : ScreenPoint{weaponCenterX, height * 1.15F};
      addLayeredFreezeBeam2D(
        drawList,
        start,
        {centerX, height * 0.5F},
        scale,
        settings
      );
    } else {
      const ScreenPoint start = beamMuzzle.x >= 0.0F
        ? beamMuzzle
        : ScreenPoint{weaponCenterX, height * 1.15F};
      addLine(
        drawList,
        start,
        {centerX, height * 0.5F},
        animatedColor,
        settings.beamWidth * (1.0F + pulse * 0.04F)
      );
    }
  }
  if (!settings.showOwnWeapons) {
    return drawList;
  }

  const auto quad =
    [&](std::array<ScreenPoint, 4> points, RenderColor quadColor) {
      drawList.overlayCommands.emplace_back(
        FilledQuad2D{points, quadColor}
      );
  };
  const auto drawWeapon = [&](Weapon weapon, float yOffset) {
    // Authored 3D viewmodels render in the perspective pass. Keep weapon
    // effects in this overlay, but never cover those meshes with legacy shapes.
    if (
      weapon == Weapon::LightningGun ||
      weapon == Weapon::FreezeGun ||
      weapon == Weapon::Railgun ||
      weapon == Weapon::MachineGun ||
      weapon == Weapon::Shotgun ||
      weapon == Weapon::GrenadeLauncher ||
      weapon == Weapon::RocketLauncher ||
      weapon == Weapon::PlasmaGun ||
      weapon == Weapon::Revolver
    ) {
      return;
    }
    const float centerX = weaponCenterX;
    const float muzzle = muzzleY + yOffset;
    const float bodyTop = muzzle + 20.0F * scale;
    const float bodyBottom = height + 18.0F * scale + yOffset;

    if (weapon == Weapon::LightningGun) {
      const float bodyHalfTop = 38.0F * scale;
      const float bodyHalfBottom = 104.0F * scale;
      quad(
        {{
          {centerX - bodyHalfTop, bodyTop},
          {centerX + bodyHalfTop, bodyTop},
          {centerX + bodyHalfBottom, bodyBottom},
          {centerX - bodyHalfBottom, bodyBottom},
        }},
        {34, 42, 52, 255}
      );
      quad(
        {{
          {centerX - 21.0F * scale, muzzle},
          {centerX + 21.0F * scale, muzzle},
          {centerX + 34.0F * scale, bodyTop + 32.0F * scale},
          {centerX - 34.0F * scale, bodyTop + 32.0F * scale},
        }},
        {67, 82, 98, 255}
      );
      quad(
        {{
          {centerX - 10.0F * scale, muzzle - 5.0F * scale},
          {centerX + 10.0F * scale, muzzle - 5.0F * scale},
          {centerX + 14.0F * scale, muzzle + 14.0F * scale},
          {centerX - 14.0F * scale, muzzle + 14.0F * scale},
        }},
        animatedColor
      );
      addRect(
        drawList,
        centerX - 72.0F * scale,
        bodyTop + 44.0F * scale,
        24.0F * scale,
        58.0F * scale,
        {52, 65, 80, 255}
      );
      addRect(
        drawList,
        centerX + 48.0F * scale,
        bodyTop + 44.0F * scale,
        24.0F * scale,
        58.0F * scale,
        {52, 65, 80, 255}
      );
      addRect(
        drawList,
        centerX - 63.0F * scale,
        bodyTop + 53.0F * scale,
        6.0F * scale,
        40.0F * scale,
        emitterColor
      );
      addRect(
        drawList,
        centerX + 57.0F * scale,
        bodyTop + 53.0F * scale,
        6.0F * scale,
        40.0F * scale,
        emitterColor
      );
      return;
    }

    if (weapon == Weapon::Railgun) {
      quad(
        {{
          {centerX - 18.0F * scale, muzzle - 8.0F * scale},
          {centerX + 18.0F * scale, muzzle - 8.0F * scale},
          {centerX + 30.0F * scale, bodyBottom},
          {centerX - 30.0F * scale, bodyBottom},
        }},
        {28, 32, 42, 255}
      );
      addRect(
        drawList,
        centerX - 15.0F * scale,
        muzzle - 18.0F * scale,
        30.0F * scale,
        132.0F * scale,
        {70, 86, 106, 255}
      );
      addRect(
        drawList,
        centerX - 8.0F * scale,
        muzzle - 26.0F * scale,
        16.0F * scale,
        142.0F * scale,
        {24, 28, 36, 255}
      );
      addRect(
        drawList,
        centerX - 4.0F * scale,
        muzzle - 24.0F * scale,
        8.0F * scale,
        132.0F * scale,
        {90, 220, 255, 255}
      );
      return;
    }

    if (weapon == Weapon::MachineGun) {
      quad(
        {{
          {centerX - 30.0F * scale, muzzle + 8.0F * scale},
          {centerX + 30.0F * scale, muzzle + 8.0F * scale},
          {centerX + 72.0F * scale, bodyBottom},
          {centerX - 72.0F * scale, bodyBottom},
        }},
        {36, 40, 43, 255}
      );
      addRect(
        drawList,
        centerX - 18.0F * scale,
        muzzle - 42.0F * scale,
        36.0F * scale,
        82.0F * scale,
        {74, 82, 88, 255}
      );
      addRect(
        drawList,
        centerX - 7.0F * scale,
        muzzle - 58.0F * scale,
        14.0F * scale,
        66.0F * scale,
        {24, 27, 30, 255}
      );
      addRect(
        drawList,
        centerX - 42.0F * scale,
        muzzle + 28.0F * scale,
        84.0F * scale,
        11.0F * scale,
        {218, 196, 116, 255}
      );
      return;
    }

    if (weapon == Weapon::Shotgun) {
      const RenderColor darkIron = {18, 20, 23, 255};
      const RenderColor barrelSteel = {84, 88, 94, 255};
      const RenderColor barrelHighlight = {162, 168, 176, 255};
      const RenderColor woodDark = {58, 35, 24, 255};
      const RenderColor woodWarm = {112, 68, 39, 255};
      const float loweredMuzzle = muzzle + 54.0F * scale;

      // Long, clean twin iron barrels, kept low and centered like a classic
      // first-person sawed-off.
      quad(
        {{
          {centerX - 48.0F * scale, loweredMuzzle - 84.0F * scale},
          {centerX - 12.0F * scale, loweredMuzzle - 84.0F * scale},
          {centerX - 4.0F * scale, bodyBottom},
          {centerX - 68.0F * scale, bodyBottom},
        }},
        barrelSteel
      );
      quad(
        {{
          {centerX + 12.0F * scale, loweredMuzzle - 84.0F * scale},
          {centerX + 48.0F * scale, loweredMuzzle - 84.0F * scale},
          {centerX + 68.0F * scale, bodyBottom},
          {centerX + 4.0F * scale, bodyBottom},
        }},
        barrelSteel
      );
      quad(
        {{
          {centerX - 22.0F * scale, loweredMuzzle - 78.0F * scale},
          {centerX - 12.0F * scale, loweredMuzzle - 78.0F * scale},
          {centerX - 6.0F * scale, bodyBottom},
          {centerX - 22.0F * scale, bodyBottom},
        }},
        barrelHighlight
      );
      quad(
        {{
          {centerX + 12.0F * scale, loweredMuzzle - 78.0F * scale},
          {centerX + 22.0F * scale, loweredMuzzle - 78.0F * scale},
          {centerX + 22.0F * scale, bodyBottom},
          {centerX + 6.0F * scale, bodyBottom},
        }},
        barrelHighlight
      );
      addRect(
        drawList,
        centerX - 5.0F * scale,
        loweredMuzzle - 80.0F * scale,
        10.0F * scale,
        100.0F * scale,
        darkIron
      );

      // Squared-off muzzle lips and dark bore openings.
      addRect(
        drawList,
        centerX - 52.0F * scale,
        loweredMuzzle - 96.0F * scale,
        44.0F * scale,
        17.0F * scale,
        barrelHighlight
      );
      addRect(
        drawList,
        centerX + 8.0F * scale,
        loweredMuzzle - 96.0F * scale,
        44.0F * scale,
        17.0F * scale,
        barrelHighlight
      );
      addRect(
        drawList,
        centerX - 36.0F * scale,
        loweredMuzzle - 93.0F * scale,
        14.0F * scale,
        12.0F * scale,
        darkIron
      );
      addRect(
        drawList,
        centerX + 22.0F * scale,
        loweredMuzzle - 93.0F * scale,
        14.0F * scale,
        12.0F * scale,
        darkIron
      );

      // Wooden holder/foregrip: the only broad mass under the clean barrels.
      quad(
        {{
          {centerX - 56.0F * scale, muzzle + 62.0F * scale},
          {centerX + 56.0F * scale, muzzle + 62.0F * scale},
          {centerX + 86.0F * scale, bodyBottom},
          {centerX - 86.0F * scale, bodyBottom},
        }},
        woodDark
      );
      addRect(
        drawList,
        centerX - 46.0F * scale,
        muzzle + 76.0F * scale,
        92.0F * scale,
        22.0F * scale,
        woodWarm
      );
      addRect(
        drawList,
        centerX - 38.0F * scale,
        muzzle + 106.0F * scale,
        76.0F * scale,
        12.0F * scale,
        {82, 48, 29, 255}
      );
      return;
    }

    if (weapon == Weapon::GrenadeLauncher) {
      quad(
        {{
          {centerX - 50.0F * scale, muzzle + 4.0F * scale},
          {centerX + 50.0F * scale, muzzle + 4.0F * scale},
          {centerX + 86.0F * scale, bodyBottom},
          {centerX - 86.0F * scale, bodyBottom},
        }},
        {33, 45, 39, 255}
      );
      addRect(
        drawList,
        centerX - 35.0F * scale,
        muzzle - 30.0F * scale,
        70.0F * scale,
        60.0F * scale,
        {72, 86, 74, 255}
      );
      addRect(
        drawList,
        centerX - 26.0F * scale,
        muzzle - 21.0F * scale,
        52.0F * scale,
        42.0F * scale,
        {24, 31, 27, 255}
      );
      addRect(
        drawList,
        centerX - 18.0F * scale,
        muzzle - 13.0F * scale,
        36.0F * scale,
        26.0F * scale,
        {112, 188, 90, 255}
      );
      return;
    }

    if (weapon == Weapon::PlasmaGun) {
      quad(
        {{
          {centerX - 42.0F * scale, muzzle + 6.0F * scale},
          {centerX + 42.0F * scale, muzzle + 6.0F * scale},
          {centerX + 91.0F * scale, bodyBottom},
          {centerX - 91.0F * scale, bodyBottom},
        }},
        {30, 39, 48, 255}
      );
      addRect(
        drawList,
        centerX - 24.0F * scale,
        muzzle - 24.0F * scale,
        48.0F * scale,
        62.0F * scale,
        {68, 82, 102, 255}
      );
      addRect(
        drawList,
        centerX - 65.0F * scale,
        muzzle + 22.0F * scale,
        28.0F * scale,
        72.0F * scale,
        {45, 58, 70, 255}
      );
      addRect(
        drawList,
        centerX + 37.0F * scale,
        muzzle + 22.0F * scale,
        28.0F * scale,
        72.0F * scale,
        {45, 58, 70, 255}
      );
      addRect(
        drawList,
        centerX - 15.0F * scale,
        muzzle - 18.0F * scale,
        30.0F * scale,
        50.0F * scale,
        {95, 235, 210, 255}
      );
      return;
    }

    quad(
      {{
        {centerX - 44.0F * scale, muzzle + 6.0F * scale},
        {centerX + 44.0F * scale, muzzle + 6.0F * scale},
        {centerX + 98.0F * scale, bodyBottom},
        {centerX - 98.0F * scale, bodyBottom},
      }},
      {45, 48, 45, 255}
    );
    addRect(
      drawList,
      centerX - 50.0F * scale,
      muzzle - 12.0F * scale,
      100.0F * scale,
      34.0F * scale,
      {80, 84, 76, 255}
    );
    addRect(
      drawList,
      centerX - 32.0F * scale,
      muzzle - 25.0F * scale,
      64.0F * scale,
      18.0F * scale,
      {34, 38, 34, 255}
    );
    addRect(
      drawList,
      centerX - 20.0F * scale,
      muzzle - 20.0F * scale,
      40.0F * scale,
      8.0F * scale,
      {185, 120, 58, 255}
    );
  };

  const float progress = std::clamp(weaponSwitchProgress, 0.0F, 1.0F);
  const auto smooth = [](float value) {
    const float t = std::clamp(value, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
  };
  if (progress < 1.0F && previousWeapon != selectedWeapon) {
    constexpr float kDropPhase = 0.45F;
    if (progress < kDropPhase) {
      drawWeapon(previousWeapon, smooth(progress / kDropPhase) * 260.0F * scale);
    } else {
      const float raiseProgress = (progress - kDropPhase) / (1.0F - kDropPhase);
      drawWeapon(
        selectedWeapon,
        (1.0F - smooth(raiseProgress)) * 260.0F * scale
      );
    }
  } else {
    drawWeapon(selectedWeapon, 0.0F);
  }
  return drawList;
}

void addSniperScope(
  DrawList2D& drawList,
  int outputWidth,
  int outputHeight,
  std::uint8_t chargePercent,
  float scopeAmount
) {
  const float width = static_cast<float>(outputWidth);
  const float height = static_cast<float>(outputHeight);
  const ScreenPoint center = {width * 0.5F, height * 0.5F};
  const float amount = std::clamp(scopeAmount, 0.0F, 1.0F);
  const float smoothAmount = amount * amount * (3.0F - 2.0F * amount);
  const float openingScale = 1.08F - 0.08F * smoothAmount;
  // Screen pixels are square, so one radius keeps the lens circular on every
  // aspect ratio. The shorter side leaves a small rim around the full circle.
  const float radius = std::min(width, height) * 0.46F;
  drawList.overlayCommands.emplace_back(SniperScopeOverlay2D{
    outputWidth,
    outputHeight,
    center,
    radius,
    openingScale,
    smoothAmount,
  });

  // The cached scope mesh owns the circular mask, edge fade, and warm rim.
  // Plain reticle lines and the charge readout remain normal UI primitives.
  const float animatedRadius = radius * openingScale;
  addLine(
    drawList,
    {center.x - animatedRadius, center.y},
    {center.x + animatedRadius, center.y},
    {25, 22, 18, static_cast<std::uint8_t>(210.0F * smoothAmount)},
    1.5F
  );
  addLine(
    drawList,
    {center.x, center.y - animatedRadius},
    {center.x, center.y + animatedRadius},
    {25, 22, 18, static_cast<std::uint8_t>(210.0F * smoothAmount)},
    1.5F
  );
  const std::uint8_t scopeAlpha =
    static_cast<std::uint8_t>(255.0F * smoothAmount);
  addDiamond(drawList, center, 4.0F, {96, 220, 255, scopeAlpha});

  const float charge = std::clamp(
    static_cast<float>(chargePercent) / 100.0F,
    0.0F,
    1.0F
  );
  const float meterWidth = std::clamp(width * 0.12F, 120.0F, 220.0F);
  const float meterHeight = 16.0F;
  const float meterX = center.x + animatedRadius * 0.32F;
  const float meterY = center.y + 26.0F;
  addRect(
    drawList,
    meterX - 2.0F,
    meterY - 2.0F,
    meterWidth + 4.0F,
    meterHeight + 4.0F,
    {28, 24, 18, static_cast<std::uint8_t>(230.0F * smoothAmount)}
  );
  addRect(
    drawList,
    meterX,
    meterY,
    meterWidth * charge,
    meterHeight,
    charge >= 1.0F ? RenderColor{255, 132, 38, scopeAlpha}
                   : RenderColor{232, 194, 92, scopeAlpha}
  );
  addText(
    drawList,
    meterX + meterWidth + 10.0F,
    meterY - 1.0F,
    std::to_string(chargePercent) + '%',
    charge >= 1.0F ? RenderColor{255, 150, 52, scopeAlpha}
                   : RenderColor{246, 220, 156, scopeAlpha},
    1.5F
  );
}

DrawList2D buildScreenUi(
  int outputWidth,
  int outputHeight,
  const PlayerState& localPlayer,
  const RenderSettings& settings,
  const HudRenderState& hud,
  const ConsoleRenderState& console,
  const PerspectiveCamera* navigationCamera
) {
  DrawList2D drawList;
  drawList.clip = {
    0.0F,
    0.0F,
    static_cast<float>(outputWidth),
    static_cast<float>(outputHeight),
  };
  // Menus are modal layers and own both the visual layer and input.
  if (hud.trainerMenuOpen) {
    addTrainerMenu(drawList, outputWidth, outputHeight, hud);
    return drawList;
  }
  if (hud.settingsOpen) {
    addSettingsMenu(drawList, outputWidth, outputHeight, hud);
    return drawList;
  }
  if (hud.miscMenuOpen) {
    addMiscMenu(drawList, outputWidth, outputHeight, hud);
    return drawList;
  }
  if (hud.deathDesaturation > 0.0F) {
    const float strength = std::clamp(hud.deathDesaturation, 0.0F, 1.0F);
    const std::array<ScreenPoint, 4> points = {{
      {0.0F, 0.0F},
      {static_cast<float>(outputWidth), 0.0F},
      {static_cast<float>(outputWidth), static_cast<float>(outputHeight)},
      {0.0F, static_cast<float>(outputHeight)},
    }};
    // The neutral wash is applied below the HUD so critical countdown text
    // stays crisp while the world loses color and brightness during death.
    drawList.commands.emplace_back(FilledQuad2D{
      points,
      {96, 96, 96, static_cast<std::uint8_t>(190.0F * strength)},
    });
  }
  if (hud.sniperScopeActive) {
    addSniperScope(
      drawList,
      outputWidth,
      outputHeight,
      hud.sniperChargePercent,
      hud.sniperScopeAmount
    );
  } else {
    addCrosshair(drawList, outputWidth, outputHeight, settings);
  }
  addDirectionalDamageIndicators(
    drawList,
    outputWidth,
    outputHeight,
    hud.directionalDamage
  );
  addHitMarker(drawList, outputWidth, outputHeight, settings);
  addSpeedText(drawList, outputWidth, outputHeight, hud, settings);
  addDashIndicator(drawList, outputWidth, outputHeight, localPlayer, settings);
  addHud(drawList, outputWidth, outputHeight, localPlayer, hud, settings);
  if (navigationCamera != nullptr) {
    addMcGuffinNavigation(
      drawList,
      outputWidth,
      outputHeight,
      *navigationCamera,
      hud
    );
  }
  addNetGraph(drawList, outputWidth, outputHeight, hud.netGraph);
  addSelectedWeaponIndicator(drawList, outputWidth, outputHeight, hud, settings);
  addConsole(drawList, outputWidth, outputHeight, console);
  return drawList;
}

DrawList2D buildFloatingHealthBars(
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
  const Arena& arena,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const std::array<bool, kDuelPlayerCount>& remoteRenderVisible,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  DrawList2D drawList;
  drawList.clip = {
    0.0F,
    0.0F,
    static_cast<float>(outputWidth),
    static_cast<float>(outputHeight),
  };
  for (std::size_t index = 0; index < remotePlayers.size(); ++index) {
    const RemotePlayerView& remote = remotePlayers[index];
    if (!remote.visible) {
      continue;
    }
    if (!remoteRenderVisible[index]) {
      continue;
    }
    if (
      !remote.teammate &&
      !enemyBodyVisibleFromCamera(camera, arena, remote.player)
    ) {
      continue;
    }
    addFloatingNameTag(
      drawList,
      outputWidth,
      outputHeight,
      camera,
      remote,
      settings
    );
    addFloatingHealthBar(
      drawList,
      outputWidth,
      outputHeight,
      camera,
      remote.player,
      remote.enemyHealthAlpha,
      remote.teammate,
      settings,
      hud
    );
  }
  return drawList;
}

DrawList2D buildFloatingDamageNumbers(
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  DrawList2D drawList;
  drawList.clip = {
    0.0F,
    0.0F,
    static_cast<float>(outputWidth),
    static_cast<float>(outputHeight),
  };
  addFloatingDamageNumbers(
    drawList,
    outputWidth,
    outputHeight,
    camera,
    settings,
    hud
  );
  return drawList;
}

} // namespace lg
