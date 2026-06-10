#include "render/Renderer.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
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

struct ViewProjection {
  float arenaLeft = 0.0F;
  float arenaTop = 0.0F;
  float arenaSize = 0.0F;
  float worldHalfExtent = 10.0F;
  Vec3 origin = {};
  Vec3 forward = {};
  Vec3 right = {};
  bool rotated = false;
};

[[nodiscard]] Vec3 worldToView(const ViewProjection& view, Vec3 worldPosition) {
  if (!view.rotated) {
    return worldPosition;
  }

  const Vec3 offset = worldPosition - view.origin;
  return {
    dot(offset, view.right),
    dot(offset, view.forward),
    worldPosition.z,
  };
}

[[nodiscard]] SDL_FPoint worldToScreen(
  const ViewProjection& view,
  Vec3 worldPosition
) {
  const Vec3 viewPosition = worldToView(view, worldPosition);
  const float minX = view.rotated
    ? -view.worldHalfExtent
    : view.origin.x - view.worldHalfExtent;
  const float maxX = view.rotated
    ? view.worldHalfExtent
    : view.origin.x + view.worldHalfExtent;
  const float minY = view.rotated
    ? -view.worldHalfExtent
    : view.origin.y - view.worldHalfExtent;
  const float maxY = view.rotated
    ? view.worldHalfExtent
    : view.origin.y + view.worldHalfExtent;
  return {
    project(
      viewPosition.x,
      minX,
      maxX,
      view.arenaLeft,
      view.arenaLeft + view.arenaSize
    ),
    project(
      viewPosition.y,
      minY,
      maxY,
      view.arenaTop + view.arenaSize,
      view.arenaTop
    ),
  };
}

void drawFilledQuad(
  SDL_Renderer* renderer,
  const std::array<SDL_FPoint, 4>& points,
  SDL_FColor color
) {
  const std::array<SDL_Vertex, 4> vertices = {{
    {points[0], color, {}},
    {points[1], color, {}},
    {points[2], color, {}},
    {points[3], color, {}},
  }};
  constexpr std::array<int, 6> indices = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(
    renderer,
    nullptr,
    vertices.data(),
    static_cast<int>(vertices.size()),
    indices.data(),
    static_cast<int>(indices.size())
  );
}

void drawQuadOutline(
  SDL_Renderer* renderer,
  const std::array<SDL_FPoint, 4>& points
) {
  for (std::size_t index = 0; index < points.size(); ++index) {
    const SDL_FPoint& start = points[index];
    const SDL_FPoint& end = points[(index + 1U) % points.size()];
    SDL_RenderLine(renderer, start.x, start.y, end.x, end.y);
  }
}

void drawDebugText(
  SDL_Renderer* renderer,
  float x,
  float y,
  const char* text,
  float scale
) {
  SDL_SetRenderScale(renderer, scale, scale);
  SDL_RenderDebugText(renderer, x / scale, y / scale, text);
  SDL_SetRenderScale(renderer, 1.0F, 1.0F);
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

void drawHitMarker(
  SDL_Renderer* renderer,
  float centerX,
  float centerY
) {
  constexpr float inner = 5.0F;
  constexpr float outer = 10.0F;
  SDL_SetRenderDrawColor(renderer, 255, 244, 196, 255);
  SDL_RenderLine(renderer, centerX - outer, centerY - outer, centerX - inner, centerY - inner);
  SDL_RenderLine(renderer, centerX + inner, centerY + inner, centerX + outer, centerY + outer);
  SDL_RenderLine(renderer, centerX + inner, centerY - inner, centerX + outer, centerY - outer);
  SDL_RenderLine(renderer, centerX - outer, centerY + outer, centerX - inner, centerY + inner);
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

  const float centerX = settings.crosshairUseScreenPosition
    ? std::clamp(settings.crosshairScreenX, 0.0F, static_cast<float>(width))
    : static_cast<float>(width) * 0.5F;

  const float centerY = settings.crosshairUseScreenPosition
    ? std::clamp(settings.crosshairScreenY, 0.0F, static_cast<float>(height))
    : static_cast<float>(height) * 0.5F;

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

  constexpr float kTextScale = 2.0F;
  constexpr float kLineHeight = 20.0F;
  const int visibleLines = std::max(1, static_cast<int>((consoleHeight - 34.0F) / kLineHeight));
  const std::size_t firstLine = console.lines.size() > static_cast<std::size_t>(visibleLines)
    ? console.lines.size() - static_cast<std::size_t>(visibleLines)
    : 0U;
  SDL_SetRenderDrawColor(renderer, 215, 225, 235, 255);
  float y = 10.0F;
  for (std::size_t index = firstLine; index < console.lines.size(); ++index) {
    drawDebugText(renderer, 10.0F, y, console.lines[index].c_str(), kTextScale);
    y += kLineHeight;
  }

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  const std::string prompt = "] " + console.input + '_';
  drawDebugText(
    renderer,
    10.0F,
    consoleHeight - 24.0F,
    prompt.c_str(),
    kTextScale
  );
}

void drawHud(
  SDL_Renderer* renderer,
  int width,
  int height,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  constexpr float kTextScale = 2.0F;
  constexpr float kCharacterWidth =
    static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * kTextScale;
  SDL_SetRenderDrawColor(renderer, 235, 242, 250, 255);
  float y = 12.0F;
  for (const std::string& line : hud.topLeftLines) {
    drawDebugText(renderer, 12.0F, y, line.c_str(), kTextScale);
    y += 20.0F;
  }

  y = 12.0F;
  for (const std::string& line : hud.topRightLines) {
    const float x = std::max(
      12.0F,
      static_cast<float>(width) - 12.0F -
        (static_cast<float>(line.size()) * kCharacterWidth)
    );
    drawDebugText(renderer, x, y, line.c_str(), kTextScale);
    y += 20.0F;
  }

  const float firstY =
    (static_cast<float>(height) * 0.5F) -
    (static_cast<float>(hud.centerLines.size()) * 11.0F) +
    hud.centerOffsetY;
  y = firstY;
  for (const std::string& line : hud.centerLines) {
    const float x = std::max(
      12.0F,
      (static_cast<float>(width) -
       (static_cast<float>(line.size()) * kCharacterWidth)) * 0.5F
    );
    drawDebugText(renderer, x, y, line.c_str(), kTextScale);
    y += 22.0F;
  }

  const float healthCharacterWidth =
    static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) *
    settings.healthTextScale;
  const float healthLineHeight = 11.0F * settings.healthTextScale;
  y = static_cast<float>(height) -
    24.0F -
    (static_cast<float>(hud.bottomCenterLines.size()) * healthLineHeight);
  for (const std::string& line : hud.bottomCenterLines) {
    const float x = std::max(
      12.0F,
      (static_cast<float>(width) -
       (static_cast<float>(line.size()) * healthCharacterWidth)) * 0.5F
    );
    drawDebugText(renderer, x, y, line.c_str(), settings.healthTextScale);
    y += healthLineHeight;
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
  const float worldHalfExtent =
    10.0F * (settings.fieldOfView / 90.0F) / settings.cameraZoom;
  const ViewProjection view{
    arenaLeft,
    arenaTop,
    arenaSize,
    worldHalfExtent,
    player.position,
    yawForward(player.viewYawRadians),
    yawRight(player.viewYawRadians),
    settings.rotateView,
  };
  const SDL_Rect worldClip = {
    static_cast<int>(arenaLeft),
    static_cast<int>(arenaTop),
    static_cast<int>(arenaSize),
    static_cast<int>(arenaSize),
  };
  SDL_SetRenderClipRect(renderer, &worldClip);

  SDL_SetRenderDrawColor(renderer, 54, 61, 72, 255);
  const std::array<SDL_FPoint, 4> arenaCorners = {
    worldToScreen(view, {arena.min.x, arena.min.y, 0.0F}),
    worldToScreen(view, {arena.max.x, arena.min.y, 0.0F}),
    worldToScreen(view, {arena.max.x, arena.max.y, 0.0F}),
    worldToScreen(view, {arena.min.x, arena.max.y, 0.0F}),
  };
  drawQuadOutline(renderer, arenaCorners);

  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    const std::array<SDL_FPoint, 4> wallCorners = {
      worldToScreen(view, {wall.min.x, wall.min.y, 0.0F}),
      worldToScreen(view, {wall.max.x, wall.min.y, 0.0F}),
      worldToScreen(view, {wall.max.x, wall.max.y, 0.0F}),
      worldToScreen(view, {wall.min.x, wall.max.y, 0.0F}),
    };
    drawFilledQuad(renderer, wallCorners, SDL_FColor{0.12F, 0.15F, 0.19F, 1.0F});
    SDL_SetRenderDrawColor(renderer, 82, 95, 112, 255);
    drawQuadOutline(renderer, wallCorners);
  }

  const SDL_FPoint playerScreen = worldToScreen(view, player.position);
  const SDL_FPoint opponentScreen = worldToScreen(view, opponent.position);
  const float playerX = playerScreen.x;
  const float playerY = playerScreen.y;
  const float opponentX = opponentScreen.x;
  const float opponentY = opponentScreen.y;

  if (lightningGun.active) {
    const SDL_FPoint beamStart = worldToScreen(view, lightningGun.start);
    const SDL_FPoint beamEnd = worldToScreen(view, lightningGun.end);
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
      beamStart.x,
      beamStart.y,
      beamEnd.x,
      beamEnd.y,
      settings.beamWidth
    );
    if (lightningGun.hit) {
      drawHitMarker(renderer, beamEnd.x, beamEnd.y);
    }
  }

  const float playerSize = settings.playerSizePixels;
  const float radius = playerSize * 0.5F;
  if (lightningGun.hit) {
    SDL_SetRenderDrawColor(renderer, 255, 190, 198, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 224, 82, 92, 255);
  }
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
    const float healthBarHalfWidth = playerSize * (18.0F / 14.0F);
    const float healthBarOffset = playerSize + 2.0F;
    const float healthBarHeight = std::max(2.0F, playerSize * (4.0F / 14.0F));
    SDL_SetRenderDrawColor(renderer, 224, 82, 92, 255);
    const SDL_FRect opponentHealthRect = rect(
      opponentX - healthBarHalfWidth,
      opponentY - healthBarOffset,
      healthBarHalfWidth * 2.0F * opponentHealthRatio,
      healthBarHeight
    );
    SDL_RenderFillRect(renderer, &opponentHealthRect);
  }

  SDL_SetRenderDrawColor(renderer, 66, 211, 146, 255);
  const SDL_FRect playerRect = rect(playerX - radius, playerY - radius, radius * 2.0F, radius * 2.0F);
  SDL_RenderFillRect(renderer, &playerRect);

  const SDL_FPoint facingEnd = worldToScreen(
    view,
    player.position + (yawForward(player.viewYawRadians) * 1.5F)
  );
  SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255);
  SDL_RenderLine(renderer, playerX, playerY, facingEnd.x, facingEnd.y);

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
  drawHud(renderer, width, height, hud, settings);
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
