#include "app/GameApp.hpp"

#include "client/ClientGame.hpp"
#include "net/LoopbackTransport.hpp"
#include "render/Renderer.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "shared/FixedTick.hpp"
#include "shared/Math.hpp"
#include "sim/Arena.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <iostream>

namespace lg {
namespace {

constexpr float kMouseSensitivityRadians = 0.0025F;
constexpr float kHalfPi = 1.57079632679F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;
constexpr int kMaxSimulationTicksPerFrame = 8;

struct LocalInputState {
  bool forward = false;
  bool back = false;
  bool left = false;
  bool right = false;
  bool up = false;
  bool down = false;
  bool attack = false;
  float mouseDeltaX = 0.0F;
  float mouseDeltaY = 0.0F;
};

#if LG_DUEL_HAS_SDL3
[[nodiscard]] const char* movementModeName(MovementMode mode) {
  switch (mode) {
  case MovementMode::Grounded:
    return "Grounded";
  case MovementMode::Airborne:
    return "Airborne";
  case MovementMode::Flying:
    return "Flying";
  }

  return "Unknown";
}

void setKey(LocalInputState& input, SDL_Scancode scancode, bool pressed) {
  switch (scancode) {
  case SDL_SCANCODE_W:
    input.forward = pressed;
    break;
  case SDL_SCANCODE_S:
    input.back = pressed;
    break;
  case SDL_SCANCODE_A:
    input.left = pressed;
    break;
  case SDL_SCANCODE_D:
    input.right = pressed;
    break;
  case SDL_SCANCODE_SPACE:
    input.up = pressed;
    break;
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL:
  case SDL_SCANCODE_LSHIFT:
  case SDL_SCANCODE_RSHIFT:
    input.down = pressed;
    break;
  default:
    break;
  }
}

[[nodiscard]] UserCommand buildCommand(
  const LocalInputState& input,
  const PlayerState& player,
  std::uint32_t sequence,
  std::uint32_t clientTick
) {
  UserCommand command;
  command.sequence = sequence;
  command.clientTick = clientTick;
  command.viewYawRadians = player.viewYawRadians + (input.mouseDeltaX * kMouseSensitivityRadians);
  command.viewPitchRadians = clamp(
    player.viewPitchRadians - (input.mouseDeltaY * kMouseSensitivityRadians),
    -kMaxPitchRadians,
    kMaxPitchRadians
  );
  command.forwardMove = (input.forward ? 1.0F : 0.0F) - (input.back ? 1.0F : 0.0F);
  command.rightMove = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
  command.upMove = (input.up ? 1.0F : 0.0F) - (input.down ? 1.0F : 0.0F);
  command.jump = input.up;
  command.attack = input.attack;
  return command;
}
#endif

} // namespace

int GameApp::run() const {
#if LG_DUEL_HAS_SDL3
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow(name().data(), 1280, 720, SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }

  if (!SDL_SetWindowRelativeMouseMode(window, true)) {
    std::cerr << "Relative mouse mode failed: " << SDL_GetError() << '\n';
  }

  Renderer renderer;
  if (!renderer.initialize(window)) {
    std::cerr << "Renderer initialization failed: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  constexpr std::size_t kLocalPlayerIndex = 0;
  constexpr std::size_t kOpponentPlayerIndex = 1;
  const Arena arena;
  LoopbackTransport transport;
  ServerGame server(transport);
  ClientGame client(transport, kLocalPlayerIndex);
  client.receiveSnapshots();

  LocalInputState input;
  bool running = true;
  bool resetRequested = false;
  std::uint32_t commandSequence = 0;
  std::uint32_t clientTick = 0;

  using Clock = std::chrono::steady_clock;
  auto previousTime = Clock::now();
  float accumulatorSeconds = 0.0F;
  float titleAccumulatorSeconds = 0.0F;
  float droppedSimulationSeconds = 0.0F;
  std::uint32_t overloadFrameCount = 0;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
        running = false;
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
        if (pressed && event.key.scancode == SDL_SCANCODE_ESCAPE) {
          running = false;
        }
        if (pressed && event.key.scancode == SDL_SCANCODE_R) {
          resetRequested = true;
        }
        setKey(input, event.key.scancode, pressed);
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT) {
          input.attack = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        }
        break;
      case SDL_EVENT_MOUSE_MOTION:
        input.mouseDeltaX += event.motion.xrel;
        input.mouseDeltaY += event.motion.yrel;
        break;
      default:
        break;
      }
    }

    const auto now = Clock::now();
    const auto elapsed = std::chrono::duration<float>(now - previousTime);
    previousTime = now;
    titleAccumulatorSeconds += elapsed.count();

    const FixedTickFrame fixedTickFrame = planFixedTicks(
      accumulatorSeconds,
      elapsed.count(),
      kFixedTickSeconds,
      kMaxSimulationTicksPerFrame
    );
    if (fixedTickFrame.droppedSeconds > 0.0F) {
      droppedSimulationSeconds += fixedTickFrame.droppedSeconds;
      ++overloadFrameCount;
    }

    bool consumedMouseForTick = false;
    for (int tick = 0; tick < fixedTickFrame.tickCount; ++tick) {
      LocalInputState tickInput = input;
      if (consumedMouseForTick) {
        tickInput.mouseDeltaX = 0.0F;
        tickInput.mouseDeltaY = 0.0F;
      }

      const PlayerState& predictedPlayer = client.predictedPlayer();
      const UserCommand command =
        buildCommand(tickInput, predictedPlayer, commandSequence++, clientTick++);
      client.sendCommand(command, resetRequested);
      resetRequested = false;
      server.tick(kFixedTickSeconds);
      client.receiveSnapshots();
      consumedMouseForTick = true;
    }
    if (consumedMouseForTick) {
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }

    if (titleAccumulatorSeconds >= 0.1F) {
      const ServerSnapshot& snapshot = client.snapshot();
      const PlayerState& player = client.predictedPlayer();
      const PlayerState& opponent = snapshot.players[kOpponentPlayerIndex];
      const LightningGunResult& lightningGun =
        snapshot.lightningGuns[kLocalPlayerIndex];
      const PredictionDiagnostics& prediction = client.predictionDiagnostics();
      char title[256];
      std::snprintf(
        title,
        sizeof(title),
        "%s | tick %u | cmd %u/%u | pending %zu | corrections %u %.4f | overload %u %.3fs | collision %s | %s | target %d HP respawn %u | LG %s | pos %.2f %.2f %.2f | vel %.2f %.2f %.2f",
        name().data(),
        snapshot.serverTick,
        commandSequence == 0 ? 0 : commandSequence - 1,
        client.lastAcknowledgedCommand(),
        prediction.pendingCommandCount,
        prediction.correctionCount,
        prediction.lastCorrectionDistance,
        overloadFrameCount,
        droppedSimulationSeconds,
        snapshot.playersColliding ? "YES" : "NO",
        movementModeName(player.movementMode),
        opponent.health,
        snapshot.respawnTicksRemaining[kOpponentPlayerIndex],
        lightningGun.hit ? "HIT" : (lightningGun.active ? "MISS" : "OFF"),
        player.position.x,
        player.position.y,
        player.position.z,
        player.velocity.x,
        player.velocity.y,
        player.velocity.z
      );
      SDL_SetWindowTitle(window, title);
      titleAccumulatorSeconds = 0.0F;
    }

    const ServerSnapshot& snapshot = client.snapshot();
    const float interpolationAlpha = clamp(
      accumulatorSeconds / kFixedTickSeconds,
      0.0F,
      1.0F
    );
    renderer.render(
      arena,
      client.predictedPlayer(),
      client.interpolatedPlayer(kOpponentPlayerIndex, interpolationAlpha),
      snapshot.lightningGuns[kLocalPlayerIndex]
    );
    SDL_Delay(1);
  }

  renderer.shutdown();
  SDL_DestroyWindow(window);
  SDL_Quit();
#else
  std::cout << name() << " local playable input/rendering requires SDL3.\n";
  std::cout << "Install SDL3 and configure with -DLG_DUEL_REQUIRE_SDL3=ON to enable the playable app.\n";
#endif

  return 0;
}

std::string_view GameApp::name() const {
  return "LG Duel";
}

} // namespace lg
