#include "render/Renderer.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

#if LG_DUEL_HAS_SDL3
[[nodiscard]] SDL_FRect rect(float x, float y, float w, float h) {
  return SDL_FRect{x, y, w, h};
}

[[nodiscard]] float remap(float value, float inMin, float inMax, float outMin, float outMax) {
  const float t = (value - inMin) / (inMax - inMin);
  return outMin + (std::clamp(t, 0.0F, 1.0F) * (outMax - outMin));
}

[[nodiscard]] float project(float value, float inMin, float inMax, float outMin, float outMax) {
  const float t = (value - inMin) / (inMax - inMin);
  return outMin + (t * (outMax - outMin));
}

void drawThickLine(
  SDL_Renderer* renderer,
  float startX,
  float startY,
  float endX,
  float endY,
  float width
) {
  const float deltaX = endX - startX;
  const float deltaY = endY - startY;
  const float lineLength = std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
  const float normalX = lineLength > 0.0F ? -deltaY / lineLength : 0.0F;
  const float normalY = lineLength > 0.0F ? deltaX / lineLength : 1.0F;
  const int halfWidth = std::max(0, static_cast<int>(width * 0.5F));
  for (int offset = -halfWidth; offset <= halfWidth; ++offset) {
    const float offsetX = normalX * static_cast<float>(offset);
    const float offsetY = normalY * static_cast<float>(offset);
    SDL_RenderLine(
      renderer,
      startX + offsetX,
      startY + offsetY,
      endX + offsetX,
      endY + offsetY
    );
  }
}

void drawCrosshair(
  SDL_Renderer* renderer,
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
  SDL_SetRenderDrawColor(
    renderer,
    settings.crosshairRed,
    settings.crosshairGreen,
    settings.crosshairBlue,
    static_cast<Uint8>(settings.crosshairAlpha * 255.0F)
  );
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  if (settings.crosshairStyle == 2) {
    const SDL_FRect dot = rect(
      centerX - (thickness * 0.5F),
      centerY - (thickness * 0.5F),
      thickness,
      thickness
    );
    SDL_RenderFillRect(renderer, &dot);
    return;
  }

  const SDL_FRect left = rect(centerX - gap - size, centerY - thickness * 0.5F, size, thickness);
  const SDL_FRect right = rect(centerX + gap, centerY - thickness * 0.5F, size, thickness);
  const SDL_FRect top = rect(centerX - thickness * 0.5F, centerY - gap - size, thickness, size);
  const SDL_FRect bottom = rect(centerX - thickness * 0.5F, centerY + gap, thickness, size);
  SDL_RenderFillRect(renderer, &left);
  SDL_RenderFillRect(renderer, &right);
  SDL_RenderFillRect(renderer, &top);
  SDL_RenderFillRect(renderer, &bottom);

  if (settings.crosshairStyle == 1) {
    const SDL_FRect dot = rect(centerX - 1.0F, centerY - 1.0F, 2.0F, 2.0F);
    SDL_RenderFillRect(renderer, &dot);
  }
}

void drawConsole(
  SDL_Renderer* renderer,
  int width,
  int height,
  const ConsoleRenderState& console
) {
  if (!console.open) {
    return;
  }

  const float consoleHeight = static_cast<float>(height) * 0.55F;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 5, 8, 12, 235);
  const SDL_FRect background = rect(0.0F, 0.0F, static_cast<float>(width), consoleHeight);
  SDL_RenderFillRect(renderer, &background);

  SDL_SetRenderDrawColor(renderer, 92, 170, 230, 255);
  const SDL_FRect border = rect(0.0F, consoleHeight - 2.0F, static_cast<float>(width), 2.0F);
  SDL_RenderFillRect(renderer, &border);

  constexpr float kLineHeight = 10.0F;
  const int visibleLines = std::max(1, static_cast<int>((consoleHeight - 34.0F) / kLineHeight));
  const std::size_t firstLine = console.lines.size() > static_cast<std::size_t>(visibleLines)
    ? console.lines.size() - static_cast<std::size_t>(visibleLines)
    : 0U;
  SDL_SetRenderDrawColor(renderer, 215, 225, 235, 255);
  float y = 10.0F;
  for (std::size_t index = firstLine; index < console.lines.size(); ++index) {
    SDL_RenderDebugText(renderer, 10.0F, y, console.lines[index].c_str());
    y += kLineHeight;
  }

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  const std::string prompt = "] " + console.input + '_';
  SDL_RenderDebugText(renderer, 10.0F, consoleHeight - 20.0F, prompt.c_str());
}

void drawHud(
  SDL_Renderer* renderer,
  int width,
  int height,
  const HudRenderState& hud
) {
  SDL_SetRenderDrawColor(renderer, 235, 242, 250, 255);
  float y = 12.0F;
  for (const std::string& line : hud.topLeftLines) {
    SDL_RenderDebugText(renderer, 12.0F, y, line.c_str());
    y += 12.0F;
  }

  y = 12.0F;
  for (const std::string& line : hud.topRightLines) {
    const float x = std::max(
      12.0F,
      static_cast<float>(width) - 12.0F -
        static_cast<float>(line.size() * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE)
    );
    SDL_RenderDebugText(renderer, x, y, line.c_str());
    y += 12.0F;
  }

  const float firstY =
    (static_cast<float>(height) * 0.5F) -
    (static_cast<float>(hud.centerLines.size()) * 7.0F);
  y = firstY;
  for (const std::string& line : hud.centerLines) {
    const float x = std::max(
      12.0F,
      (static_cast<float>(width) -
       static_cast<float>(line.size() * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE)) * 0.5F
    );
    SDL_RenderDebugText(renderer, x, y, line.c_str());
    y += 14.0F;
  }
}
#endif

} // namespace

Renderer::~Renderer() {
  shutdown();
}

bool Renderer::initialize(void* window) {
#if LG_DUEL_HAS_SDL3
  renderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window), nullptr);
  return renderer_ != nullptr;
#else
  (void)window;
  return false;
#endif
}

void Renderer::render(
  const Arena& arena,
  const PlayerState& player,
  const PlayerState& opponent,
  const LightningGunResult& lightningGun,
  const RenderSettings& settings,
  const HudRenderState& hud,
  const ConsoleRenderState& console
) {
#if LG_DUEL_HAS_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(renderer_);
  if (renderer == nullptr) {
    return;
  }

  int width = 0;
  int height = 0;
  SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

  SDL_SetRenderDrawColor(renderer, 12, 14, 18, 255);
  SDL_RenderClear(renderer);

  const float margin = 40.0F;
  const float arenaSize = static_cast<float>(std::min(width, height)) - (margin * 2.0F);
  const float arenaLeft = (static_cast<float>(width) - arenaSize) * 0.5F;
  const float arenaTop = (static_cast<float>(height) - arenaSize) * 0.5F;
  const float worldHalfExtent = 10.0F * (settings.fieldOfView / 90.0F);
  const float worldMinX = player.position.x - worldHalfExtent;
  const float worldMaxX = player.position.x + worldHalfExtent;
  const float worldMinY = player.position.y - worldHalfExtent;
  const float worldMaxY = player.position.y + worldHalfExtent;
  const SDL_Rect worldClip = {
    static_cast<int>(arenaLeft),
    static_cast<int>(arenaTop),
    static_cast<int>(arenaSize),
    static_cast<int>(arenaSize),
  };
  SDL_SetRenderClipRect(renderer, &worldClip);

  SDL_SetRenderDrawColor(renderer, 54, 61, 72, 255);
  const SDL_FRect arenaRect = rect(
    project(arena.min.x, worldMinX, worldMaxX, arenaLeft, arenaLeft + arenaSize),
    project(arena.max.y, worldMinY, worldMaxY, arenaTop + arenaSize, arenaTop),
    arenaSize * ((arena.max.x - arena.min.x) / (worldMaxX - worldMinX)),
    arenaSize * ((arena.max.y - arena.min.y) / (worldMaxY - worldMinY))
  );
  SDL_RenderRect(renderer, &arenaRect);

  const float playerX = project(player.position.x, worldMinX, worldMaxX, arenaLeft, arenaLeft + arenaSize);
  const float playerY = project(player.position.y, worldMinY, worldMaxY, arenaTop + arenaSize, arenaTop);
  const float opponentX = project(
    opponent.position.x,
    worldMinX,
    worldMaxX,
    arenaLeft,
    arenaLeft + arenaSize
  );
  const float opponentY = project(
    opponent.position.y,
    worldMinY,
    worldMaxY,
    arenaTop + arenaSize,
    arenaTop
  );

  if (lightningGun.active) {
    const float beamStartX = project(
      lightningGun.start.x,
      worldMinX,
      worldMaxX,
      arenaLeft,
      arenaLeft + arenaSize
    );
    const float beamStartY = project(
      lightningGun.start.y,
      worldMinY,
      worldMaxY,
      arenaTop + arenaSize,
      arenaTop
    );
    const float beamEndX = project(
      lightningGun.end.x,
      worldMinX,
      worldMaxX,
      arenaLeft,
      arenaLeft + arenaSize
    );
    const float beamEndY = project(
      lightningGun.end.y,
      worldMinY,
      worldMaxY,
      arenaTop + arenaSize,
      arenaTop
    );
    const auto hitBoost = [hit = lightningGun.hit](std::uint8_t channel) {
      return static_cast<Uint8>(
        hit ? std::min(255, static_cast<int>(channel) + 60) : channel
      );
    };
    SDL_SetRenderDrawColor(
      renderer,
      hitBoost(settings.beamRed),
      hitBoost(settings.beamGreen),
      hitBoost(settings.beamBlue),
      static_cast<Uint8>(settings.beamAlpha * 255.0F)
    );
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    drawThickLine(
      renderer,
      beamStartX,
      beamStartY,
      beamEndX,
      beamEndY,
      settings.beamWidth
    );
  }

  const float radius = 7.0F;
  SDL_SetRenderDrawColor(renderer, 224, 82, 92, 255);
  const SDL_FRect opponentRect = rect(
    opponentX - radius,
    opponentY - radius,
    radius * 2.0F,
    radius * 2.0F
  );
  SDL_RenderFillRect(renderer, &opponentRect);

  if (hud.showOpponentHealthBar) {
    const float opponentHealthRatio =
      std::clamp(static_cast<float>(opponent.health) / 100.0F, 0.0F, 1.0F);
    SDL_SetRenderDrawColor(renderer, 224, 82, 92, 255);
    const SDL_FRect opponentHealthRect = rect(
      opponentX - 18.0F,
      opponentY - 16.0F,
      36.0F * opponentHealthRatio,
      4.0F
    );
    SDL_RenderFillRect(renderer, &opponentHealthRect);
  }

  SDL_SetRenderDrawColor(renderer, 66, 211, 146, 255);
  const SDL_FRect playerRect = rect(playerX - radius, playerY - radius, radius * 2.0F, radius * 2.0F);
  SDL_RenderFillRect(renderer, &playerRect);

  const Vec3 forward = yawForward(player.viewYawRadians);
  SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255);
  SDL_RenderLine(renderer, playerX, playerY, playerX + (forward.x * 24.0F), playerY - (forward.y * 24.0F));

  SDL_SetRenderClipRect(renderer, nullptr);
  const float speed = length(player.velocity);
  const float speedBarWidth = std::min(speed / 12.0F, 1.0F) * 220.0F;
  SDL_SetRenderDrawColor(renderer, 86, 156, 214, 255);
  const SDL_FRect speedRect = rect(24.0F, static_cast<float>(height) - 34.0F, speedBarWidth, 10.0F);
  SDL_RenderFillRect(renderer, &speedRect);

  const float zValue = remap(player.position.z, arena.min.z, arena.max.z, 0.0F, 180.0F);
  SDL_SetRenderDrawColor(renderer, 234, 196, 106, 255);
  const SDL_FRect heightRect = rect(static_cast<float>(width) - 34.0F, static_cast<float>(height) - 24.0F - zValue, 10.0F, zValue);
  SDL_RenderFillRect(renderer, &heightRect);

  drawCrosshair(renderer, width, height, settings);
  drawHud(renderer, width, height, hud);
  drawConsole(renderer, width, height, console);
  SDL_RenderPresent(renderer);
#else
  (void)arena;
  (void)player;
  (void)opponent;
  (void)lightningGun;
  (void)settings;
  (void)hud;
  (void)console;
#endif
}

bool Renderer::setVSync(bool enabled) {
#if LG_DUEL_HAS_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(renderer_);
  return renderer != nullptr &&
    SDL_SetRenderVSync(renderer, enabled ? 1 : SDL_RENDERER_VSYNC_DISABLED);
#else
  (void)enabled;
  return false;
#endif
}

void Renderer::shutdown() {
#if LG_DUEL_HAS_SDL3
  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
    renderer_ = nullptr;
  }
#endif
}

} // namespace lg
