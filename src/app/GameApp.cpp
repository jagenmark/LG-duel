#include "app/GameApp.hpp"

#include "client/ClientGame.hpp"
#include "console/ConsoleSystem.hpp"
#include "net/UdpTransport.hpp"
#include "render/Renderer.hpp"
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
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

namespace lg {
namespace {

constexpr float kBaseMouseSensitivityRadians = 0.0025F;
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
struct ClientConsoleState {
  bool open = false;
  std::string input;
  std::deque<std::string> output;
  std::vector<std::string> history;
  std::size_t historyIndex = 0;
};

void appendConsoleOutput(ClientConsoleState& state, std::string_view text) {
  std::istringstream stream{std::string(text)};
  std::string line;
  while (std::getline(stream, line)) {
    state.output.push_back(std::move(line));
  }
  while (state.output.size() > 128) {
    state.output.pop_front();
  }
}

std::string clientConfigPath() {
  char* preferencePath = SDL_GetPrefPath("LG Duel", "LG Duel");
  if (preferencePath == nullptr) {
    return "client.cfg";
  }
  std::string path = preferencePath;
  SDL_free(preferencePath);
  return path + "client.cfg";
}

void loadClientConfig(ConsoleSystem& console, const std::string& path) {
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    (void)console.execute(line);
  }
}

bool saveClientConfig(const ConsoleSystem& console, const std::string& path) {
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return false;
  }
  for (const std::string& line : console.archivedConfigLines()) {
    file << line << '\n';
  }
  return file.good();
}

void registerClientCvars(ConsoleSystem& console) {
  const CvarFlag archivedClient = CvarFlag::Archive | CvarFlag::Client;
  console.registerCvar({"sensitivity", "Mouse sensitivity multiplier.", 1.0F, archivedClient, 0.1F, 10.0F});
  console.registerCvar({"cl_fov", "Top-down camera view extent.", 90.0F, archivedClient, 45.0F, 140.0F});
  console.registerCvar({"cl_showfps", "Show render FPS in the window title.", false, archivedClient, {}, {}});
  console.registerCvar({"cl_show_net", "Show network diagnostics in the window title.", true, archivedClient, {}, {}});
  console.registerCvar({"crosshair_enable", "Draw the crosshair.", true, archivedClient, {}, {}});
  console.registerCvar({"crosshair_style", "Crosshair style: 0 cross, 1 cross and dot, 2 dot.", 0, archivedClient, 0.0F, 2.0F});
  console.registerCvar({"crosshair_size", "Crosshair arm length in pixels.", 8.0F, archivedClient, 1.0F, 40.0F});
  console.registerCvar({"crosshair_thickness", "Crosshair thickness in pixels.", 2.0F, archivedClient, 1.0F, 10.0F});
  console.registerCvar({"crosshair_gap", "Crosshair center gap in pixels.", 3.0F, archivedClient, 0.0F, 30.0F});
  console.registerCvar({"crosshair_alpha", "Crosshair opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"crosshair_r", "Crosshair red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"crosshair_g", "Crosshair green channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"crosshair_b", "Crosshair blue channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_vsync", "Enable renderer vertical sync.", true, archivedClient, {}, {}});
  console.registerCvar({"r_beam_width", "Lightning beam width in pixels.", 2.0F, archivedClient, 1.0F, 12.0F});
  console.registerCvar({"r_beam_alpha", "Lightning beam opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_beam_r", "Lightning beam red channel.", 74, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_beam_g", "Lightning beam green channel.", 166, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_beam_b", "Lightning beam blue channel.", 255, archivedClient, 0.0F, 255.0F});
}

RenderSettings renderSettings(const ConsoleSystem& console) {
  RenderSettings settings;
  settings.fieldOfView = console.getFloat("cl_fov");
  settings.crosshairEnabled = console.getBool("crosshair_enable");
  settings.crosshairStyle = console.getInt("crosshair_style");
  settings.crosshairSize = console.getFloat("crosshair_size");
  settings.crosshairThickness = console.getFloat("crosshair_thickness");
  settings.crosshairGap = console.getFloat("crosshair_gap");
  settings.crosshairAlpha = console.getFloat("crosshair_alpha");
  settings.crosshairRed = static_cast<std::uint8_t>(console.getInt("crosshair_r"));
  settings.crosshairGreen = static_cast<std::uint8_t>(console.getInt("crosshair_g"));
  settings.crosshairBlue = static_cast<std::uint8_t>(console.getInt("crosshair_b"));
  settings.beamWidth = console.getFloat("r_beam_width");
  settings.beamAlpha = console.getFloat("r_beam_alpha");
  settings.beamRed = static_cast<std::uint8_t>(console.getInt("r_beam_r"));
  settings.beamGreen = static_cast<std::uint8_t>(console.getInt("r_beam_g"));
  settings.beamBlue = static_cast<std::uint8_t>(console.getInt("r_beam_b"));
  return settings;
}

ConsoleRenderState consoleRenderState(const ClientConsoleState& state) {
  ConsoleRenderState renderState;
  renderState.open = state.open;
  renderState.input = state.input;
  renderState.lines.assign(state.output.begin(), state.output.end());
  return renderState;
}

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
  std::uint32_t clientTick,
  float sensitivity
) {
  UserCommand command;
  command.sequence = sequence;
  command.clientTick = clientTick;
  command.viewYawRadians =
    player.viewYawRadians + (input.mouseDeltaX * kBaseMouseSensitivityRadians * sensitivity);
  command.viewPitchRadians = clamp(
    player.viewPitchRadians - (input.mouseDeltaY * kBaseMouseSensitivityRadians * sensitivity),
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

GameApp::GameApp(std::string serverHost, std::uint16_t serverPort)
  : serverHost_(std::move(serverHost)), serverPort_(serverPort) {}

int GameApp::run() const {
#if LG_DUEL_HAS_SDL3
  UdpClientTransport transport(serverHost_, serverPort_);
  if (!transport.initialize()) {
    std::cerr << "UDP client initialization failed: " << transport.lastError() << '\n';
    return 1;
  }

  const auto connectionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!transport.connected() && std::chrono::steady_clock::now() < connectionDeadline) {
    transport.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!transport.connected()) {
    std::cerr << "Timed out connecting to " << serverHost_ << ':' << serverPort_ << '\n';
    return 1;
  }

  const std::size_t localPlayerIndex = transport.playerIndex();
  const std::size_t opponentPlayerIndex = 1U - localPlayerIndex;
  ClientGame client(transport, localPlayerIndex);
  const auto snapshotDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!client.hasSnapshot() && std::chrono::steady_clock::now() < snapshotDeadline) {
    transport.update();
    client.receiveSnapshots();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!client.hasSnapshot()) {
    std::cerr << "Connected, but no server snapshot was received.\n";
    return 1;
  }

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

  ConsoleSystem console;
  registerClientCvars(console);
  const std::string configPath = clientConfigPath();
  loadClientConfig(console, configPath);
  bool quitRequested = false;
  bool clearRequested = false;
  bool writeConfigRequested = false;
  console.registerCommand(
    "quit",
    "Quit the client.",
    [&quitRequested](const std::vector<std::string>&) {
      quitRequested = true;
      return "quitting";
    }
  );
  console.registerCommand(
    "clear",
    "Clear console scrollback.",
    [&clearRequested](const std::vector<std::string>&) {
      clearRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "writeconfig",
    "Write archived client cvars.",
    [&writeConfigRequested](const std::vector<std::string>&) {
      writeConfigRequested = true;
      return "writing client config";
    }
  );
  console.registerCommand(
    "net_stats",
    "Print current connection diagnostics.",
    [&transport](const std::vector<std::string>&) {
      char text[96];
      std::snprintf(
        text,
        sizeof(text),
        "connected=%d player=%u ping=%.1fms",
        transport.connected() ? 1 : 0,
        static_cast<unsigned int>(transport.playerIndex() + 1U),
        transport.pingMilliseconds()
      );
      return std::string(text);
    }
  );
  (void)renderer.setVSync(console.getBool("r_vsync"));
  bool appliedVSync = console.getBool("r_vsync");
  ClientConsoleState consoleState;
  appendConsoleOutput(consoleState, "LG Duel console. Type cmdlist or cvarlist.");
  bool suppressNextTextInput = false;

  const Arena arena;

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
  std::uint32_t renderedFrameCount = 0;
  float displayedFramesPerSecond = 0.0F;

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
        if (pressed && event.key.scancode == SDL_SCANCODE_GRAVE) {
          suppressNextTextInput = true;
          consoleState.open = !consoleState.open;
          consoleState.historyIndex = consoleState.history.size();
          input = {};
          if (consoleState.open) {
            SDL_SetWindowRelativeMouseMode(window, false);
            SDL_StartTextInput(window);
          } else {
            SDL_StopTextInput(window);
            SDL_SetWindowRelativeMouseMode(window, true);
          }
          break;
        }
        if (consoleState.open) {
          if (!pressed) {
            break;
          }
          if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            consoleState.open = false;
            SDL_StopTextInput(window);
            SDL_SetWindowRelativeMouseMode(window, true);
          } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
            if (!consoleState.input.empty()) {
              consoleState.input.pop_back();
            }
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (!consoleState.input.empty()) {
              appendConsoleOutput(consoleState, "] " + consoleState.input);
              const std::string result = console.execute(consoleState.input);
              if (!result.empty()) {
                appendConsoleOutput(consoleState, result);
              }
              consoleState.history.push_back(consoleState.input);
              consoleState.historyIndex = consoleState.history.size();
              consoleState.input.clear();
            }
          } else if (event.key.scancode == SDL_SCANCODE_UP && !consoleState.history.empty()) {
            if (consoleState.historyIndex > 0) {
              --consoleState.historyIndex;
            }
            consoleState.input = consoleState.history[consoleState.historyIndex];
          } else if (event.key.scancode == SDL_SCANCODE_DOWN && !consoleState.history.empty()) {
            if (consoleState.historyIndex + 1 < consoleState.history.size()) {
              ++consoleState.historyIndex;
              consoleState.input = consoleState.history[consoleState.historyIndex];
            } else {
              consoleState.historyIndex = consoleState.history.size();
              consoleState.input.clear();
            }
          } else if (event.key.scancode == SDL_SCANCODE_TAB) {
            const std::size_t wordStart = consoleState.input.find_last_of(" \t");
            const std::size_t prefixStart =
              wordStart == std::string::npos ? 0U : wordStart + 1U;
            const std::string prefix = consoleState.input.substr(prefixStart);
            const std::vector<std::string> matches = console.complete(prefix);
            if (matches.size() == 1) {
              consoleState.input.replace(prefixStart, std::string::npos, matches[0]);
            } else if (!matches.empty()) {
              std::string line;
              for (const std::string& match : matches) {
                line += match + ' ';
              }
              appendConsoleOutput(consoleState, line);
            }
          }
          break;
        }
        if (pressed && event.key.scancode == SDL_SCANCODE_ESCAPE) {
          running = false;
        }
        if (pressed && event.key.scancode == SDL_SCANCODE_R) {
          resetRequested = true;
        }
        setKey(input, event.key.scancode, pressed);
        break;
      }
      case SDL_EVENT_TEXT_INPUT:
        if (suppressNextTextInput) {
          suppressNextTextInput = false;
        } else if (consoleState.open) {
          consoleState.input += event.text.text;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!consoleState.open && event.button.button == SDL_BUTTON_LEFT) {
          input.attack = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        }
        break;
      case SDL_EVENT_MOUSE_MOTION:
        if (!consoleState.open) {
          input.mouseDeltaX += event.motion.xrel;
          input.mouseDeltaY += event.motion.yrel;
        }
        break;
      default:
        break;
      }
    }

    if (clearRequested) {
      consoleState.output.clear();
      clearRequested = false;
    }
    if (writeConfigRequested) {
      appendConsoleOutput(
        consoleState,
        saveClientConfig(console, configPath)
          ? "wrote " + configPath
          : "failed to write " + configPath
      );
      writeConfigRequested = false;
    }
    if (quitRequested) {
      running = false;
    }
    const bool requestedVSync = console.getBool("r_vsync");
    if (requestedVSync != appliedVSync) {
      if (!renderer.setVSync(requestedVSync)) {
        appendConsoleOutput(consoleState, "failed to change r_vsync");
      }
      appliedVSync = requestedVSync;
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
        buildCommand(
          tickInput,
          predictedPlayer,
          commandSequence++,
          clientTick++,
          console.getFloat("sensitivity")
        );
      client.sendCommand(command, resetRequested);
      resetRequested = false;
      transport.update();
      client.receiveSnapshots();
      consumedMouseForTick = true;
    }
    if (consumedMouseForTick) {
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }

    ++renderedFrameCount;
    if (titleAccumulatorSeconds >= 0.1F) {
      displayedFramesPerSecond =
        static_cast<float>(renderedFrameCount) / titleAccumulatorSeconds;
      renderedFrameCount = 0;
      const ServerSnapshot& snapshot = client.snapshot();
      const PlayerState& player = client.predictedPlayer();
      const PlayerState& opponent = snapshot.players[opponentPlayerIndex];
      const LightningGunResult& lightningGun =
        snapshot.lightningGuns[localPlayerIndex];
      const PredictionDiagnostics& prediction = client.predictionDiagnostics();
      char title[256];
      char fpsText[32] = {};
      if (console.getBool("cl_showfps")) {
        std::snprintf(fpsText, sizeof(fpsText), " | %.0f FPS", displayedFramesPerSecond);
      }
      if (console.getBool("cl_show_net")) {
        std::snprintf(
          title,
          sizeof(title),
          "%s%s | P%zu | ping %.1f ms | tick %u | cmd %u/%u | rewind %u/%u%s | pending %zu | corrections %u %.4f | overload %u %.3fs | collision %s | %s | target %d HP respawn %u | LG %s",
          name().data(),
          fpsText,
          localPlayerIndex + 1,
          transport.pingMilliseconds(),
          snapshot.serverTick,
          commandSequence == 0 ? 0 : commandSequence - 1,
          client.lastAcknowledgedCommand(),
          lightningGun.requestedRewindTicks,
          lightningGun.appliedRewindTicks,
          lightningGun.rewindClamped ? " CLAMP" : "",
          prediction.pendingCommandCount,
          prediction.correctionCount,
          prediction.lastCorrectionDistance,
          overloadFrameCount,
          droppedSimulationSeconds,
          snapshot.playersColliding ? "YES" : "NO",
          movementModeName(player.movementMode),
          opponent.health,
          snapshot.respawnTicksRemaining[opponentPlayerIndex],
          lightningGun.hit ? "HIT" : (lightningGun.active ? "MISS" : "OFF")
        );
      } else {
        std::snprintf(
          title,
          sizeof(title),
          "%s%s",
          name().data(),
          fpsText
        );
      }
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
      client.interpolatedPlayer(opponentPlayerIndex, interpolationAlpha),
      snapshot.lightningGuns[localPlayerIndex],
      renderSettings(console),
      consoleRenderState(consoleState)
    );
    transport.update();
    client.receiveSnapshots();
    if (transport.timedOut()) {
      std::cerr << "Server connection timed out.\n";
      running = false;
    }
    SDL_Delay(1);
  }

  saveClientConfig(console, configPath);
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
  return "LG Duel Client";
}

} // namespace lg
