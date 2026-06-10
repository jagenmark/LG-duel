#include "app/GameApp.hpp"

#include "client/ClientSession.hpp"
#include "console/ConsoleSystem.hpp"
#include "input/InputBindings.hpp"
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
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

namespace lg {
namespace {

constexpr float kBaseMouseSensitivityRadians = 0.0025F;
constexpr float kHalfPi = 1.57079632679F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;
constexpr int kMaxSimulationTicksPerFrame = 8;

struct LocalInputState {
  int forward = 0;
  int back = 0;
  int left = 0;
  int right = 0;
  int up = 0;
  int down = 0;
  int attack = 0;
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

bool saveClientConfig(
  const ConsoleSystem& console,
  const InputBindings& bindings,
  const std::string& path
) {
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return false;
  }
  for (const std::string& line : console.archivedConfigLines()) {
    file << line << '\n';
  }
  file << "unbindall\n";
  for (const std::string& line : bindings.configLines()) {
    file << line << '\n';
  }
  return file.good();
}

void registerClientCvars(ConsoleSystem& console) {
  const CvarFlag archivedClient = CvarFlag::Archive | CvarFlag::Client;
  console.registerCvar({"cl_config_version", "Client config migration version.", 0, archivedClient, 0.0F, 100.0F});
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

std::string keyName(SDL_Scancode scancode) {
  switch (scancode) {
  case SDL_SCANCODE_GRAVE:
    return "section";
  case SDL_SCANCODE_LEFT:
    return "left";
  case SDL_SCANCODE_RIGHT:
    return "right";
  case SDL_SCANCODE_UP:
    return "up";
  case SDL_SCANCODE_DOWN:
    return "down";
  case SDL_SCANCODE_LCTRL:
    return "leftctrl";
  case SDL_SCANCODE_RCTRL:
    return "rightctrl";
  case SDL_SCANCODE_LSHIFT:
    return "leftshift";
  case SDL_SCANCODE_RSHIFT:
    return "rightshift";
  default:
    return InputBindings::normalizeKey(SDL_GetScancodeName(scancode));
  }
}

void installDefaultBindings(InputBindings& bindings) {
  (void)bindings.bind("section", "toggleconsole");
  (void)bindings.bind("w", "+forward");
  (void)bindings.bind("s", "+back");
  (void)bindings.bind("a", "+moveleft");
  (void)bindings.bind("d", "+moveright");
  (void)bindings.bind("space", "+moveup");
  (void)bindings.bind("leftctrl", "+movedown");
  (void)bindings.bind("rightctrl", "+movedown");
  (void)bindings.bind("leftshift", "+movedown");
  (void)bindings.bind("rightshift", "+movedown");
  (void)bindings.bind("mouse1", "+attack");
  (void)bindings.bind("r", "resetmatch");
  (void)bindings.bind("f3", "ready");
  (void)bindings.bind("escape", "quit");
}

std::string matchPhaseName(MatchPhase phase) {
  switch (phase) {
  case MatchPhase::WaitingForPlayers:
    return "WAITING FOR PLAYERS";
  case MatchPhase::WaitingForReady:
    return "WAITING FOR READY";
  case MatchPhase::Countdown:
    return "ROUND START";
  case MatchPhase::Live:
    return "LIVE";
  case MatchPhase::RoundEnd:
    return "ROUND OVER";
  case MatchPhase::MatchEnd:
    return "MATCH OVER";
  }
  return "UNKNOWN";
}

HudRenderState buildHud(const ClientSession& session) {
  HudRenderState hud;
  hud.centerLines.push_back(session.statusMessage());
  if (!session.readyForPlay()) {
    return hud;
  }

  const ClientGame& client = *session.game();
  const ServerSnapshot& snapshot = client.snapshot();
  const std::size_t localPlayerIndex = session.playerIndex();
  const std::size_t opponentPlayerIndex = 1U - localPlayerIndex;
  const std::size_t connectedCount = static_cast<std::size_t>(std::count(
    snapshot.connectedPlayers.begin(),
    snapshot.connectedPlayers.end(),
    true
  ));

  hud.centerLines.clear();
  hud.topLeftLines.push_back(
    "HEALTH " + std::to_string(snapshot.players[localPlayerIndex].health)
  );
  hud.topLeftLines.push_back(
    "PLAYERS " + std::to_string(connectedCount) + '/' +
    std::to_string(kDuelPlayerCount)
  );
  hud.topRightLines.push_back(
    "SCORE " + std::to_string(snapshot.scores[localPlayerIndex]) + " - " +
    std::to_string(snapshot.scores[opponentPlayerIndex]) + " / " +
    std::to_string(snapshot.matchRules.roundLimit)
  );
  if (snapshot.matchRules.timeLimitMinutes > 0) {
    const std::uint32_t limitTicks =
      static_cast<std::uint32_t>(snapshot.matchRules.timeLimitMinutes) * 60U * 125U;
    const std::uint32_t remainingTicks =
      snapshot.liveTicksElapsed < limitTicks
      ? limitTicks - snapshot.liveTicksElapsed
      : 0U;
    const std::uint32_t remainingSeconds = remainingTicks / 125U;
    hud.topRightLines.push_back(
      "TIME " + std::to_string(remainingSeconds / 60U) + ':' +
      (remainingSeconds % 60U < 10U ? "0" : "") +
      std::to_string(remainingSeconds % 60U)
    );
  }
  if (snapshot.matchRules.showOpponentHealth) {
    hud.topRightLines.push_back(
      "OPPONENT " + std::to_string(snapshot.players[opponentPlayerIndex].health)
    );
    hud.showOpponentHealthBar = true;
  }

  hud.centerLines.push_back(matchPhaseName(snapshot.matchPhase));
  switch (snapshot.matchPhase) {
  case MatchPhase::WaitingForPlayers:
    hud.centerLines.push_back(
      std::to_string(connectedCount) + '/' +
      std::to_string(kDuelPlayerCount) + " PLAYERS CONNECTED"
    );
    break;
  case MatchPhase::WaitingForReady:
    hud.centerLines.push_back(
      snapshot.readyPlayers[localPlayerIndex]
        ? "WAITING FOR OTHER PLAYERS TO READY UP"
        : "PRESS F3 TO READY UP"
    );
    break;
  case MatchPhase::Countdown: {
    const std::uint32_t seconds =
      (snapshot.phaseTicksRemaining + 124U) / 125U;
    hud.centerLines.push_back(std::to_string(seconds));
    hud.centerLines.push_back("MOVE ENABLED - WEAPONS LOCKED");
    break;
  }
  case MatchPhase::RoundEnd:
    hud.centerLines.push_back(
      snapshot.roundWinner == localPlayerIndex ? "ROUND WON" : "ROUND LOST"
    );
    break;
  case MatchPhase::MatchEnd:
    hud.centerLines.push_back(
      snapshot.matchWinner == localPlayerIndex ? "MATCH WON" : "MATCH LOST"
    );
    break;
  case MatchPhase::Live:
    hud.centerLines.clear();
    break;
  }
  return hud;
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
  command.forwardMove = (input.forward > 0 ? 1.0F : 0.0F) - (input.back > 0 ? 1.0F : 0.0F);
  command.rightMove = (input.right > 0 ? 1.0F : 0.0F) - (input.left > 0 ? 1.0F : 0.0F);
  command.upMove = (input.up > 0 ? 1.0F : 0.0F) - (input.down > 0 ? 1.0F : 0.0F);
  command.jump = input.up > 0;
  command.attack = input.attack > 0;
  return command;
}
#endif

} // namespace

GameApp::GameApp(std::string serverHost, std::uint16_t serverPort)
  : serverHost_(std::move(serverHost)), serverPort_(serverPort) {}

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

  ConsoleSystem console;
  registerClientCvars(console);
  InputBindings bindings;
  installDefaultBindings(bindings);
  const std::string configPath = clientConfigPath();
  LocalInputState input;
  bool running = true;
  bool resetRequested = false;
  bool readyRequested = false;
  bool quitRequested = false;
  bool clearRequested = false;
  bool writeConfigRequested = false;
  bool toggleConsoleRequested = false;
  ClientSession session;

  const auto registerButtonCommand =
    [&console](std::string name, int& pressCount) {
      console.registerCommand(
        '+' + name,
        "Begin " + name + '.',
        [&pressCount](const std::vector<std::string>&) {
          ++pressCount;
          return std::string{};
        }
      );
      console.registerCommand(
        '-' + name,
        "End " + name + '.',
        [&pressCount](const std::vector<std::string>&) {
          pressCount = std::max(0, pressCount - 1);
          return std::string{};
        }
      );
    };
  registerButtonCommand("forward", input.forward);
  registerButtonCommand("back", input.back);
  registerButtonCommand("moveleft", input.left);
  registerButtonCommand("moveright", input.right);
  registerButtonCommand("moveup", input.up);
  registerButtonCommand("movedown", input.down);
  registerButtonCommand("attack", input.attack);

  console.registerCommand(
    "quit",
    "Quit the client.",
    [&quitRequested](const std::vector<std::string>&) {
      quitRequested = true;
      return "quitting";
    }
  );
  console.registerCommand(
    "ready",
    "Toggle ready state while waiting for a match.",
    [&readyRequested](const std::vector<std::string>&) {
      readyRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "connect",
    "Connect to a server: connect <host> [port], or connect <port> for localhost.",
    [&session](const std::vector<std::string>& arguments) {
      if (arguments.size() < 2 || arguments.size() > 3) {
        return std::string("usage: connect <host> [port]");
      }
      std::string host = arguments[1];
      std::uint16_t port = 27960;
      const auto parsePort = [](std::string_view text, std::uint16_t& parsed) {
        unsigned int value = 0;
        const auto result = std::from_chars(
          text.data(),
          text.data() + text.size(),
          value
        );
        if (
          result.ec != std::errc{} ||
          result.ptr != text.data() + text.size() ||
          value == 0 ||
          value > 65535U
        ) {
          return false;
        }
        parsed = static_cast<std::uint16_t>(value);
        return true;
      };
      if (arguments.size() == 2) {
        std::uint16_t shorthandPort = 0;
        if (parsePort(arguments[1], shorthandPort)) {
          host = "127.0.0.1";
          port = shorthandPort;
        }
      } else if (!parsePort(arguments[2], port)) {
        return std::string("invalid UDP port");
      }
      return session.connect(std::move(host), port)
        ? session.statusMessage()
        : "connect failed: " + session.statusMessage();
    }
  );
  console.registerCommand(
    "disconnect",
    "Disconnect from the current server.",
    [&session](const std::vector<std::string>&) {
      session.disconnect();
      return std::string("Disconnected");
    }
  );
  console.registerCommand(
    "reconnect",
    "Reconnect to the most recently used server.",
    [&session](const std::vector<std::string>&) {
      return session.reconnect()
        ? session.statusMessage()
        : "reconnect failed: " + session.statusMessage();
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
    "toggleconsole",
    "Toggle the client console.",
    [&toggleConsoleRequested](const std::vector<std::string>&) {
      toggleConsoleRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "resetmatch",
    "Request an authoritative match reset.",
    [&resetRequested](const std::vector<std::string>&) {
      resetRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "bind",
    "Bind a key to a command.",
    [&bindings](const std::vector<std::string>& arguments) {
      if (arguments.size() == 2) {
        const std::string command = bindings.binding(arguments[1]);
        return command.empty()
          ? InputBindings::normalizeKey(arguments[1]) + " is unbound"
          : InputBindings::normalizeKey(arguments[1]) + " = " + command;
      }
      if (arguments.size() < 3) {
        return std::string("usage: bind <key> <command>");
      }
      std::string command = arguments[2];
      for (std::size_t index = 3; index < arguments.size(); ++index) {
        command += ' ' + arguments[index];
      }
      if (!bindings.bind(arguments[1], command)) {
        return std::string("invalid binding");
      }
      return InputBindings::normalizeKey(arguments[1]) + " = " + command;
    }
  );
  console.registerCommand(
    "unbind",
    "Remove a key binding.",
    [&bindings, &console](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: unbind <key>");
      }
      for (const std::string& command : bindings.unbind(arguments[1])) {
        (void)console.execute(command);
      }
      return InputBindings::normalizeKey(arguments[1]) + " unbound";
    }
  );
  console.registerCommand(
    "unbindall",
    "Remove every key binding.",
    [&bindings, &console](const std::vector<std::string>&) {
      for (const std::string& command : bindings.unbindAll()) {
        (void)console.execute(command);
      }
      return std::string{};
    }
  );
  console.registerCommand(
    "bindlist",
    "List key bindings.",
    [&bindings](const std::vector<std::string>&) {
      std::string result;
      for (const std::string& line : bindings.list()) {
        result += line + '\n';
      }
      return result;
    }
  );
  console.registerCommand(
    "actionlist",
    "List bindable gameplay actions using Quake 3 command names.",
    [](const std::vector<std::string>&) {
      return std::string(
        "+forward\n"
        "+back\n"
        "+moveleft\n"
        "+moveright\n"
        "+moveup\n"
        "+movedown\n"
        "+attack\n"
        "resetmatch\n"
        "ready\n"
        "toggleconsole\n"
        "quit"
      );
    }
  );
  console.registerCommand(
    "net_stats",
    "Print current connection diagnostics.",
    [&session](const std::vector<std::string>&) {
      char text[160];
      std::snprintf(
        text,
        sizeof(text),
        "state=%d host=%s port=%u player=%zu ping=%.1fms",
        static_cast<int>(session.state()),
        std::string(session.host()).c_str(),
        static_cast<unsigned int>(session.port()),
        session.playerIndex() + 1U,
        session.pingMilliseconds()
      );
      return std::string(text);
    }
  );
  loadClientConfig(console, configPath);
  if (console.getInt("cl_config_version") < 1) {
    (void)bindings.bind("f3", "ready");
    (void)console.execute("set cl_config_version 1");
  }
  (void)session.connect(serverHost_, serverPort_);
  (void)renderer.setVSync(console.getBool("r_vsync"));
  bool appliedVSync = console.getBool("r_vsync");
  ClientConsoleState consoleState;
  appendConsoleOutput(
    consoleState,
    "LG Duel console. Type actionlist, bindlist, cmdlist, or cvarlist."
  );
  bool suppressNextTextInput = false;
  const auto executeBindingCommands =
    [&console, &consoleState](const std::vector<std::string>& commands) {
      for (const std::string& command : commands) {
        const std::string result = console.execute(command);
        if (!result.empty()) {
          appendConsoleOutput(consoleState, result);
        }
      }
    };
  const auto setConsoleOpen =
    [&bindings, &console, &consoleState, &input, window](bool open) {
      if (consoleState.open == open) {
        return;
      }
      for (const std::string& command : bindings.releaseAll()) {
        (void)console.execute(command);
      }
      consoleState.open = open;
      consoleState.historyIndex = consoleState.history.size();
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
      if (open) {
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_StartTextInput(window);
      } else {
        SDL_StopTextInput(window);
        SDL_SetWindowRelativeMouseMode(window, true);
      }
    };
  const auto applyConsoleToggle =
    [&toggleConsoleRequested, &setConsoleOpen, &consoleState]() {
      if (toggleConsoleRequested) {
        toggleConsoleRequested = false;
        setConsoleOpen(!consoleState.open);
      }
    };

  const Arena arena;
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
        const std::string key = keyName(event.key.scancode);
        if (consoleState.open) {
          if (!pressed) {
            executeBindingCommands(bindings.handleKey(key, false));
            break;
          }
          if (bindings.binding(key) == "toggleconsole") {
            suppressNextTextInput = true;
            executeBindingCommands(bindings.handleKey(key, true));
            applyConsoleToggle();
            break;
          }
          if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            setConsoleOpen(false);
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
              applyConsoleToggle();
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
        if (bindings.binding(key) == "toggleconsole") {
          suppressNextTextInput = pressed;
        }
        executeBindingCommands(bindings.handleKey(key, pressed));
        applyConsoleToggle();
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
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const std::string key = "mouse" + std::to_string(event.button.button);
        if (!consoleState.open) {
          executeBindingCommands(bindings.handleKey(key, pressed));
          applyConsoleToggle();
        } else if (!pressed) {
          executeBindingCommands(bindings.handleKey(key, false));
        }
        break;
      }
      case SDL_EVENT_MOUSE_MOTION:
        if (!consoleState.open) {
          input.mouseDeltaX += event.motion.xrel;
          input.mouseDeltaY += event.motion.yrel;
        }
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        executeBindingCommands(bindings.releaseAll());
        input.mouseDeltaX = 0.0F;
        input.mouseDeltaY = 0.0F;
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
        saveClientConfig(console, bindings, configPath)
          ? "wrote " + configPath
          : "failed to write " + configPath
      );
      writeConfigRequested = false;
    }
    if (quitRequested) {
      running = false;
    }
    session.update();
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
      ClientGame* client = session.game();
      if (client == nullptr || !client->hasSnapshot()) {
        break;
      }
      LocalInputState tickInput = input;
      if (consumedMouseForTick) {
        tickInput.mouseDeltaX = 0.0F;
        tickInput.mouseDeltaY = 0.0F;
      }

      const PlayerState& predictedPlayer = client->predictedPlayer();
      const UserCommand command =
        buildCommand(
          tickInput,
          predictedPlayer,
          commandSequence++,
          clientTick++,
          console.getFloat("sensitivity")
        );
      session.sendCommand(command, resetRequested, readyRequested);
      resetRequested = false;
      readyRequested = false;
      session.update();
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
      char title[256];
      char fpsText[32] = {};
      if (console.getBool("cl_showfps")) {
        std::snprintf(fpsText, sizeof(fpsText), " | %.0f FPS", displayedFramesPerSecond);
      }
      const ClientGame* titleClient = session.game();
      if (
        console.getBool("cl_show_net") &&
        titleClient != nullptr &&
        titleClient->hasSnapshot()
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
        const ServerSnapshot& snapshot = titleClient->snapshot();
        const LightningGunResult& lightningGun =
          snapshot.lightningGuns[localPlayerIndex];
        const PredictionDiagnostics& prediction =
          titleClient->predictionDiagnostics();
        std::snprintf(
          title,
          sizeof(title),
          "%s%s | P%zu | ping %.1f ms | tick %u | cmd %u/%u | rewind %u/%u%s | pending %zu | corrections %u %.4f | overload %u %.3fs | phase %s",
          name().data(),
          fpsText,
          localPlayerIndex + 1,
          session.pingMilliseconds(),
          snapshot.serverTick,
          commandSequence == 0 ? 0 : commandSequence - 1,
          titleClient->lastAcknowledgedCommand(),
          lightningGun.requestedRewindTicks,
          lightningGun.appliedRewindTicks,
          lightningGun.rewindClamped ? " CLAMP" : "",
          prediction.pendingCommandCount,
          prediction.correctionCount,
          prediction.lastCorrectionDistance,
          overloadFrameCount,
          droppedSimulationSeconds,
          matchPhaseName(snapshot.matchPhase).c_str()
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

    const float interpolationAlpha = clamp(
      accumulatorSeconds / kFixedTickSeconds,
      0.0F,
      1.0F
    );
    PlayerState renderPlayer;
    PlayerState renderOpponent;
    LightningGunResult renderLightningGun;
    if (const ClientGame* renderClient = session.game();
        renderClient != nullptr && renderClient->hasSnapshot()) {
      const std::size_t localPlayerIndex = session.playerIndex();
      const std::size_t opponentPlayerIndex = 1U - localPlayerIndex;
      renderPlayer = renderClient->predictedPlayer();
      renderOpponent = renderClient->interpolatedPlayer(
        opponentPlayerIndex,
        interpolationAlpha
      );
      renderLightningGun =
        renderClient->snapshot().lightningGuns[localPlayerIndex];
    }
    renderer.render(
      arena,
      renderPlayer,
      renderOpponent,
      renderLightningGun,
      renderSettings(console),
      buildHud(session),
      consoleRenderState(consoleState)
    );
    session.update();
    SDL_Delay(1);
  }

  saveClientConfig(console, bindings, configPath);
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
