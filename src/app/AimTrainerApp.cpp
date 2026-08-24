#include "app/AimTrainerApp.hpp"
#include "app/AimTrainerInput.hpp"
#include "app/ClientAudio.hpp"
#include "app/ClientCvars.hpp"

#include "client/HitConfirmAudio.hpp"
#include "console/ConsoleSystem.hpp"
#include "dev/DevControlServer.hpp"
#include "render/OptionMenuLayout.hpp"
#include "render/Renderer.hpp"
#include "shared/Constants.hpp"
#include "shared/FixedTick.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/MapRegistry.hpp"
#include "sim/WeaponCatalog.hpp"
#include "trainer/AimTrainerEditor.hpp"
#include "trainer/AimTrainerPresentation.hpp"
#include "trainer/AimTrainerVideoSettings.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace lg {
namespace {

constexpr float kTrainerDegreesToRadians = 0.01745329252F;
constexpr float kTrainerRadiansToDegrees = 57.2957795131F;

#if LG_DUEL_HAS_SDL3
constexpr std::size_t kTrainerVideoSettingCount = 11U;
constexpr std::size_t kTrainerVideoApplyRow = 9U;
constexpr std::size_t kTrainerVideoCloseRow = 10U;

struct TrainerResolution {
  int width = 1280;
  int height = 720;
};

struct TrainerVideoMenu {
  bool open = false;
  std::size_t selectedRow = 0U;
  std::size_t scrollRows = 0U;
  int hoveredRow = -1;
  int pressedRow = -1;
  int fullscreenMode = 0;
  TrainerResolution resolution;
  int textureFilter = 2;
  int textureAnisotropy = 8;
  float displayGamma = 1.0F;
  bool bloom = true;
  int antiAliasing = 0;
  int sunShadows = 0;
  int pointLights = 1;
};

struct TrainerConsoleState {
  bool open = false;
  std::string input;
  std::size_t cursorIndex = 0U;
  std::deque<std::string> output;
  std::vector<std::string> history;
  std::size_t historyIndex = 0U;
};

struct TrainerControlOperation {
  enum class Stage {
    Start,
    WaitFrames,
    SendInput,
    Capture,
  };

  dev::QueuedControlRequest queued;
  Stage stage = Stage::Start;
  std::uint64_t targetRenderedFrame = 0U;
  std::uint32_t inputTicksRemaining = 0U;
  std::filesystem::path capturePath;
};

[[nodiscard]] AimTrainerVideoSettings savedTrainerVideoSettings(
  const TrainerVideoMenu& menu
) {
  return {
    menu.fullscreenMode,
    menu.resolution.width,
    menu.resolution.height,
    menu.textureFilter,
    menu.textureAnisotropy,
    menu.displayGamma,
    menu.bloom,
    menu.antiAliasing,
    menu.sunShadows,
    menu.pointLights,
  };
}

void restoreTrainerVideoSettings(
  TrainerVideoMenu& menu,
  const AimTrainerVideoSettings& saved
) {
  menu.fullscreenMode = saved.displayMode;
  menu.resolution = {saved.resolutionWidth, saved.resolutionHeight};
  menu.textureFilter = saved.textureFilter;
  menu.textureAnisotropy = saved.textureAnisotropy;
  menu.displayGamma = saved.displayGamma;
  menu.bloom = saved.bloom;
  menu.antiAliasing = saved.antiAliasing;
  menu.sunShadows = saved.sunShadows;
  menu.pointLights = saved.pointLights;
}

void appendTrainerConsoleOutput(
  TrainerConsoleState& state,
  std::string_view text
) {
  std::istringstream stream{std::string(text)};
  std::string line;
  while (std::getline(stream, line)) state.output.push_back(std::move(line));
  while (state.output.size() > 128U) state.output.pop_front();
}

[[nodiscard]] ConsoleRenderState trainerConsoleRenderState(
  const TrainerConsoleState& state
) {
  ConsoleRenderState rendered;
  rendered.open = state.open;
  rendered.showCat = false;
  rendered.input = state.input;
  rendered.cursorIndex = state.cursorIndex;
  rendered.lines.assign(state.output.begin(), state.output.end());
  return rendered;
}

void syncTrainerVideoMenu(
  TrainerVideoMenu& menu,
  SDL_Window* window,
  const RenderSettings& settings
) {
  const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0U;
  menu.fullscreenMode = !fullscreen ? 0 :
    SDL_GetWindowFullscreenMode(window) == nullptr ? 1 : 2;
  (void)SDL_GetWindowSize(window, &menu.resolution.width, &menu.resolution.height);
  menu.textureFilter = settings.textureFilter;
  menu.textureAnisotropy = settings.textureAnisotropy;
  menu.displayGamma = settings.displayGamma;
  menu.bloom = settings.bloomEnabled;
  menu.antiAliasing = settings.antiAliasingQuality;
  menu.sunShadows = settings.sunShadowQuality;
  menu.pointLights = settings.pointLightQuality;
}

[[nodiscard]] std::vector<TrainerResolution> trainerResolutionOptions(
  SDL_Window* window,
  TrainerResolution requested
) {
  std::set<std::pair<int, int>> unique;
  const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
  if (display != 0) {
    int modeCount = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &modeCount);
    for (int index = 0; modes != nullptr && index < modeCount; ++index) {
      if (modes[index] != nullptr && modes[index]->w > 0 && modes[index]->h > 0) {
        unique.emplace(modes[index]->w, modes[index]->h);
      }
    }
    if (modes != nullptr) SDL_free(modes);
    if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display)) {
      unique.emplace(desktop->w, desktop->h);
    }
  }
  for (const TrainerResolution preset : {
         TrainerResolution{1280, 720},
         TrainerResolution{1600, 900},
         TrainerResolution{1920, 1080},
         TrainerResolution{2560, 1440},
         TrainerResolution{3840, 2160},
       }) {
    unique.emplace(preset.width, preset.height);
  }
  unique.emplace(requested.width, requested.height);

  std::vector<TrainerResolution> options;
  options.reserve(unique.size());
  for (const auto& [width, height] : unique) options.push_back({width, height});
  std::sort(options.begin(), options.end(), [](TrainerResolution left, TrainerResolution right) {
    const int leftArea = left.width * left.height;
    const int rightArea = right.width * right.height;
    return leftArea != rightArea ? leftArea < rightArea :
      left.width != right.width ? left.width < right.width : left.height < right.height;
  });
  return options;
}

[[nodiscard]] bool isNativeResolution(SDL_Window* window, TrainerResolution resolution) {
  const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
  const SDL_DisplayMode* desktop = display != 0 ? SDL_GetDesktopDisplayMode(display) : nullptr;
  return desktop != nullptr && desktop->w == resolution.width && desktop->h == resolution.height;
}

void adjustTrainerVideoMenu(TrainerVideoMenu& menu, SDL_Window* window, int direction) {
  if (direction == 0) return;
  switch (menu.selectedRow) {
  case 0U: menu.fullscreenMode = (menu.fullscreenMode + direction + 3) % 3; break;
  case 1U: {
    const std::vector<TrainerResolution> options =
      trainerResolutionOptions(window, menu.resolution);
    const auto current = std::find_if(
      options.begin(), options.end(), [&menu](TrainerResolution option) {
        return option.width == menu.resolution.width && option.height == menu.resolution.height;
      }
    );
    const int index = current == options.end()
      ? 0 : static_cast<int>(current - options.begin());
    const int count = static_cast<int>(options.size());
    menu.resolution = options[static_cast<std::size_t>((index + direction + count) % count)];
    break;
  }
  case 2U: menu.textureFilter = (menu.textureFilter + direction + 3) % 3; break;
  case 3U: {
    static constexpr std::array<int, 5> values = {1, 2, 4, 8, 16};
    const auto found = std::find(values.begin(), values.end(), menu.textureAnisotropy);
    const int index = found == values.end()
      ? 0 : static_cast<int>(found - values.begin());
    menu.textureAnisotropy = values[static_cast<std::size_t>((index + direction + 5) % 5)];
    break;
  }
  case 4U:
    menu.displayGamma = std::clamp(
      menu.displayGamma + 0.05F * static_cast<float>(direction), 0.5F, 1.5F
    );
    break;
  case 5U: menu.bloom = !menu.bloom; break;
  case 6U: menu.antiAliasing = (menu.antiAliasing + direction + 3) % 3; break;
  case 7U: menu.sunShadows = (menu.sunShadows + direction + 3) % 3; break;
  case 8U: menu.pointLights = (menu.pointLights + direction + 3) % 3; break;
  default: break;
  }
}

void applyTrainerVideoMenu(
  const TrainerVideoMenu& menu,
  SDL_Window* window,
  RenderSettings& settings
) {
  if (menu.fullscreenMode == 0) {
    (void)SDL_SetWindowFullscreen(window, false);
    (void)SDL_SyncWindow(window);
    (void)SDL_SetWindowFullscreenMode(window, nullptr);
    (void)SDL_SetWindowSize(window, menu.resolution.width, menu.resolution.height);
  } else if (menu.fullscreenMode == 1) {
    (void)SDL_SetWindowFullscreenMode(window, nullptr);
    (void)SDL_SetWindowFullscreen(window, true);
  } else {
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    int modeCount = 0;
    SDL_DisplayMode** modes = display != 0
      ? SDL_GetFullscreenDisplayModes(display, &modeCount) : nullptr;
    const SDL_DisplayMode* chosen = nullptr;
    for (int index = 0; modes != nullptr && index < modeCount; ++index) {
      if (modes[index] != nullptr &&
          modes[index]->w == menu.resolution.width &&
          modes[index]->h == menu.resolution.height &&
          (chosen == nullptr || modes[index]->refresh_rate > chosen->refresh_rate)) {
        chosen = modes[index];
      }
    }
    (void)SDL_SetWindowFullscreenMode(window, chosen);
    (void)SDL_SetWindowFullscreen(window, true);
    if (modes != nullptr) SDL_free(modes);
  }
  (void)SDL_SyncWindow(window);
  settings.textureFilter = menu.textureFilter;
  settings.textureAnisotropy = menu.textureAnisotropy;
  settings.displayGamma = menu.displayGamma;
  settings.bloomEnabled = menu.bloom;
  settings.antiAliasingQuality = menu.antiAliasing;
  settings.sunShadowQuality = menu.sunShadows;
  settings.pointLightQuality = menu.pointLights;
}

void addTrainerVideoHud(
  HudRenderState& hud,
  const TrainerVideoMenu& menu,
  SDL_Window* window,
  std::string_view settingsWarning
) {
  if (!menu.open) return;
  int width = 0;
  int height = 0;
  (void)SDL_GetWindowSizeInPixels(window, &width, &height);
  (void)height;
  const auto item = [&menu](
    std::size_t row,
    std::string label,
    std::string value,
    bool command = false
  ) {
    return HudRenderState::SettingsMenuItem{
      std::move(label), std::move(value), menu.selectedRow == row, false, command
    };
  };
  hud.settingsOpen = true;
  hud.settingsScrollRows = menu.scrollRows;
  hud.settingsHoveredRow = menu.hoveredRow;
  hud.settingsPressedRow = menu.pressedRow;
  hud.settingsItems = {
    item(0U, "Display mode", menu.fullscreenMode == 0 ? "Windowed" :
      menu.fullscreenMode == 1 ? "Borderless fullscreen" : "Exclusive fullscreen"),
    item(1U, "Resolution", std::to_string(menu.resolution.width) + "x" +
      std::to_string(menu.resolution.height) +
      (isNativeResolution(window, menu.resolution) ? " (native)" : "")),
    item(2U, "Texture filter", menu.textureFilter == 0 ? "Nearest" :
      menu.textureFilter == 1 ? "Bilinear" : "Trilinear"),
    item(3U, "Texture anisotropy", std::to_string(menu.textureAnisotropy) + "x"),
    item(4U, "Brightness / gamma", std::to_string(
      static_cast<int>(std::lround(menu.displayGamma * 100.0F))) + "%"),
    item(5U, "Bright-effect bloom", menu.bloom ? "On" : "Off"),
    item(6U, "Anti-aliasing", menu.antiAliasing == 0 ? "Off" :
      menu.antiAliasing == 1 ? "2x MSAA" : "4x MSAA"),
    item(7U, "Sun shadows", menu.sunShadows == 0 ? "Off" :
      menu.sunShadows == 1 ? "Low" : "High"),
    item(8U, "Live point lights", menu.pointLights == 0 ? "Combat only" :
      menu.pointLights == 1 ? "16 lights" : "32 lights"),
    item(kTrainerVideoApplyRow, "Apply changes", "Enter", true),
    item(kTrainerVideoCloseRow, "Close", "Esc", true),
  };
  hud.settingsFooter = aimTrainerUsesCompactHud(width)
    ? "ARROWS change  ENTER apply  ESC/F10 close"
    : "UP/DOWN select. LEFT/RIGHT change. ENTER change or apply. ESC or F10 closes.";
  if (!settingsWarning.empty()) {
    hud.settingsFooter += "  SAVE: " + std::string(settingsWarning);
  }
}
#endif

[[nodiscard]] std::filesystem::path defaultBalanceConfigPath() {
  namespace fs = std::filesystem;
  fs::path directory = fs::current_path();
  for (;;) {
    const fs::path candidate = directory / "config" / "balance.cfg";
    if (fs::exists(candidate)) return candidate;
    const fs::path parent = directory.parent_path();
    if (parent.empty() || parent == directory) break;
    directory = parent;
  }
  return {};
}

[[nodiscard]] BalanceConfig loadTrainerBalance(std::string& warning) {
  const std::filesystem::path path = defaultBalanceConfigPath();
  if (path.empty()) {
    warning = "config/balance.cfg was not found; using built-in balance values";
    return {};
  }
  const BalanceConfigLoadResult loaded = loadBalanceConfigFromFile(path.string());
  if (!loaded.ok) {
    warning = "Could not load " + path.string() + ": " + loaded.error +
      "; using built-in balance values";
    return {};
  }
  return loaded.config;
}

void addTrainerHud(
  HudRenderState& hud,
  const AimTrainerMenu& menu,
  const AimTrainerEditor& editor,
  int hoveredRow,
  int pressedRow,
  std::string_view balanceWarning,
  int viewportWidth
) {
  const AimTrainerFrame& frame = menu.frame();
  const AimScenario& draft = menu.draft();
  hud.selectedWeapon = frame.selectedWeapon;
  hud.topLeftLines.push_back("AIM TRAINER  |  " + draft.name);
  hud.topLeftLines.push_back(
    "TIME " + std::to_string(frame.remainingTicks / kFixedTickRate) + "s  SCORE " +
    std::to_string(frame.stats.score) + "  ACC " +
    std::to_string(static_cast<int>(std::lround(frame.stats.accuracyPercent()))) + "%"
  );
  hud.topLeftLines.push_back(
    "DAMAGE " + std::to_string(frame.stats.damage) + "  CLEARS " +
    std::to_string(frame.stats.clears) + "  HITS " + std::to_string(frame.stats.hits) +
    "/" + std::to_string(frame.stats.attempts) + "  SPM " +
    std::to_string(static_cast<int>(std::lround(
      frame.stats.scorePerMinute(frame.elapsedTicks)
    )))
  );
  if (!draft.groups.empty()) {
    const std::size_t groupIndex = std::min(
      menu.selectedGroupIndex(),
      draft.groups.size() - 1U
    );
    const AimTargetGroup& group = draft.groups[groupIndex];
    hud.topRightLines.push_back(
      "GROUP " + group.name + "  " +
      (group.visual == AimTargetVisual::Orb ? "ORB" : "WORKER") +
      "  R " + std::to_string(group.radius)
    );
  }
  hud.topRightLines.push_back(
    std::string("MOVE ") +
    (draft.playerMovement == AimPlayerMovement::Locked ? "LOCKED" : "NORMAL") +
    "  WEAPONS " + (draft.weaponPolicy == AimWeaponPolicy::All ? "ALL" : "FORCED")
  );
  hud.topRightLines.push_back(
    "AMMO " + std::to_string(frame.ammo[weaponIndex(frame.selectedWeapon)]) +
    "  GROUPS " + std::to_string(draft.groups.size()) +
    "  TARGETS " + std::to_string(frame.targets.size())
  );
  const bool compact = aimTrainerUsesCompactHud(viewportWidth);
  if (compact) {
    hud.topLeftLines.insert(
      hud.topLeftLines.end(),
      hud.topRightLines.begin(),
      hud.topRightLines.end()
    );
    hud.topRightLines.clear();
  }
  if (frame.phase != AimTrainerPhase::Running) {
    hud.centerLines.push_back(
      frame.phase == AimTrainerPhase::Results
        ? frame.message : "F3: START  ESC: SCENARIOS"
    );
  }
  if (compact) {
    hud.bottomCenterLines.push_back("LMB fire  RMB zoom  WASD move  SPACE jump");
    hud.bottomCenterLines.push_back("Q dash  CTRL crouch  SHIFT sneak");
    hud.bottomCenterLines.push_back("1-9 weapon  F3 start  F5 restart");
    hud.bottomCenterLines.push_back("ESC scenarios  F10 video");
  } else {
    hud.bottomCenterLines.push_back(
      "LMB fire  RMB zoom  WASD move  SPACE jump  Q dash  CTRL crouch  SHIFT sneak"
    );
    hud.bottomCenterLines.push_back(
      "1-9 switch weapon  F3 start  F5 restart  ESC scenarios  F10 video"
    );
  }

  hud.trainerMenuOpen = editor.open();
  if (!editor.open()) return;
  const std::vector<AimTrainerEditorRow> rows = editor.rows();
  hud.trainerMenuItems.reserve(rows.size());
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const AimTrainerEditorRow& row = rows[index];
    HudRenderState::SettingsMenuItem item;
    item.label = row.label;
    item.value = editor.editingText() && index == editor.selectedRow()
      ? editor.textInput() + "_" : row.value;
    item.active = index == editor.selectedRow();
    item.changed = editor.editingText() && item.active;
    item.command = row.command || row.editable;
    hud.trainerMenuItems.push_back(std::move(item));
  }
  hud.trainerMenuScrollRows = editor.scrollRows();
  hud.trainerMenuHoveredRow = hoveredRow;
  hud.trainerMenuPressedRow = pressedRow;
  hud.trainerMenuFooter = editor.editingText()
    ? "Type value. ENTER accepts; ESC cancels."
    : compact
      ? "ARROWS navigate/change. ENTER select. ESC close."
      : "UP/DOWN select. LEFT/RIGHT change. ENTER edit or run. ESC closes.";
  if (!editor.message().empty()) hud.trainerMenuFooter += "  " + editor.message();
  if (!menu.warning().empty()) hud.trainerMenuFooter += "  STORAGE: " + menu.warning();
  if (!balanceWarning.empty()) {
    hud.trainerMenuFooter += "  BALANCE: " + std::string(balanceWarning);
  }
}

#if LG_DUEL_HAS_SDL3
[[nodiscard]] OptionMenuLayout editorLayout(
  SDL_Window* window,
  const AimTrainerEditor& editor
) {
  int width = 0;
  int height = 0;
  (void)SDL_GetWindowSizeInPixels(window, &width, &height);
  return buildOptionMenuLayout(width, height, editor.rows().size(), editor.scrollRows());
}

[[nodiscard]] OptionMenuLayout trainerVideoLayout(
  SDL_Window* window,
  const TrainerVideoMenu& menu
) {
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(window, &width, &height);
  return buildOptionMenuLayout(
    width,
    height,
    kTrainerVideoSettingCount,
    menu.scrollRows
  );
}

void keepEditorSelectionVisible(SDL_Window* window, AimTrainerEditor& editor) {
  const OptionMenuLayout layout = editorLayout(window, editor);
  std::size_t scroll = std::min(editor.scrollRows(), layout.maxScrollRows);
  if (editor.selectedRow() < scroll) {
    scroll = editor.selectedRow();
  } else if (editor.selectedRow() >= scroll + layout.visibleRows) {
    scroll = editor.selectedRow() - layout.visibleRows + 1U;
  }
  editor.setScrollRows(std::min(scroll, layout.maxScrollRows));
}

void keepVideoSelectionVisible(SDL_Window* window, TrainerVideoMenu& menu) {
  const OptionMenuLayout layout = trainerVideoLayout(window, menu);
  std::size_t scroll = std::min(menu.scrollRows, layout.maxScrollRows);
  if (menu.selectedRow < scroll) {
    scroll = menu.selectedRow;
  } else if (menu.selectedRow >= scroll + layout.visibleRows) {
    scroll = menu.selectedRow - layout.visibleRows + 1U;
  }
  menu.scrollRows = std::min(scroll, layout.maxScrollRows);
}

void syncMenuInput(
  SDL_Window* window,
  const AimTrainerEditor& editor,
  bool videoMenuOpen,
  bool consoleOpen = false
) {
  (void)SDL_SetWindowRelativeMouseMode(
    window,
    !editor.open() && !videoMenuOpen && !consoleOpen
  );
  if (editor.editingText() || consoleOpen) {
    (void)SDL_StartTextInput(window);
  } else {
    (void)SDL_StopTextInput(window);
  }
}
#endif

} // namespace

AimTrainerApp::AimTrainerApp(DeveloperControlOptions developerControl)
  : developerControl_(developerControl) {}

int AimTrainerApp::run() const {
#if LG_DUEL_HAS_SDL3
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 1;
  }
  const bool audioSubsystemAvailable = SDL_InitSubSystem(SDL_INIT_AUDIO);
  SDL_Window* window = SDL_CreateWindow(
    "LG Duel - Aim Trainer",
    1280,
    720,
    SDL_WINDOW_RESIZABLE
  );
  if (window == nullptr) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }
  Renderer renderer;
  if (!renderer.initialize(window)) {
    std::cerr << "Renderer initialization failed: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  const LocalMapLoadResult map = loadLocalMap("aim_trainer");
  if (!map.ok) {
    std::cerr << "Aim trainer map failed: " << map.error << '\n';
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  const char* executableBasePath = SDL_GetBasePath();
  const std::filesystem::path runtimeDirectory = std::filesystem::weakly_canonical(
    executableBasePath != nullptr
      ? std::filesystem::path(executableBasePath)
      : std::filesystem::current_path()
  );
  ClientAudio audio;
  const bool audioAvailable =
    audioSubsystemAvailable && audio.initialize(runtimeDirectory);
  const std::filesystem::path repositoryRoot =
    runtimeDirectory.parent_path().parent_path();
  const std::filesystem::path captureDirectory =
    runtimeDirectory.parent_path() / "captures";
  dev::DevControlServer developerControl;
  if (developerControl_.enabled) {
    std::error_code directoryError;
    std::filesystem::create_directories(captureDirectory, directoryError);
    if (directoryError) {
      std::cerr << "Could not create developer capture directory: "
                << directoryError.message() << '\n';
      renderer.shutdown();
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
    std::string controlError;
    if (!developerControl.start(developerControl_.port, controlError)) {
      std::cerr << "Developer control startup failed: " << controlError << '\n';
      renderer.shutdown();
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
    std::cout << "Aim trainer developer control enabled on 127.0.0.1:"
              << developerControl.port() << '\n';
    std::cout << "Capture output: " << captureDirectory.string() << '\n';
  }
  std::string balanceWarning;
  const BalanceConfig balance = loadTrainerBalance(balanceWarning);
  if (!balanceWarning.empty()) std::cerr << balanceWarning << '\n';
  const MovementTuning movement;
  AimTrainer trainer(map.arena, balance, movement);
  char* preferencePath = SDL_GetPrefPath("LG Duel", "LG Duel");
  const std::filesystem::path preferences = preferencePath != nullptr
    ? std::filesystem::path(preferencePath) : std::filesystem::path(".");
  if (preferencePath != nullptr) SDL_free(preferencePath);
  AimTrainerStore store(preferences);
  AimTrainerMenu menu(trainer, store);
  menu.setRuntimeIdentity(
    "aim_trainer",
    map.descriptor.contentHash,
    AimTrainer::balanceFingerprint(balance, movement)
  );
  AimTrainerEditor editor(menu);
  TrainerVideoMenu videoMenu;
  const std::filesystem::path videoSettingsPath =
    preferences / "aim_trainer" / "video.cfg";
  const AimTrainerVideoSettingsLoadResult loadedVideoSettings =
    loadAimTrainerVideoSettings(videoSettingsPath);
  if (loadedVideoSettings.loaded) {
    restoreTrainerVideoSettings(videoMenu, loadedVideoSettings.settings);
  }
  std::string videoSettingsWarning = loadedVideoSettings.warning;
  float videoWheelRemainder = 0.0F;
  float scenarioWheelRemainder = 0.0F;
  syncMenuInput(window, editor, videoMenu.open);

  bool running = true;
  bool forward = false;
  bool backward = false;
  bool left = false;
  bool right = false;
  bool attack = false;
  bool jump = false;
  bool dash = false;
  bool crouch = false;
  bool sneak = false;
  bool zoomed = false;
  bool suppressConsoleToggleText = false;
  TrainerConsoleState consoleState;
  ConsoleSystem trainerConsole;
  registerClientCvars(trainerConsole);
  appendTrainerConsoleOutput(
    consoleState,
    "LG Duel aim trainer console. Type cmdlist for commands."
  );
  (void)trainerConsole.registerCommand(
    "quit",
    "Quit the aim trainer.",
    [&running](const std::vector<std::string>&) {
      running = false;
      return std::string{};
    }
  );
  (void)trainerConsole.registerCommand(
    "clear",
    "Clear console output.",
    [&consoleState](const std::vector<std::string>&) {
      consoleState.output.clear();
      return std::string{};
    }
  );
  (void)trainerConsole.registerCommand(
    "trainer_start",
    "Start the selected aim-trainer scenario.",
    [&menu, &editor](const std::vector<std::string>&) {
      const AimTrainerArmResult started = menu.start();
      if (started.ok) editor.setOpen(false);
      return started.ok ? std::string("aim trainer started") : started.error;
    }
  );
  (void)trainerConsole.registerCommand(
    "trainer_restart",
    "Restart the selected aim-trainer scenario.",
    [&menu, &editor](const std::vector<std::string>&) {
      const AimTrainerArmResult restarted = menu.restart();
      if (restarted.ok) editor.setOpen(false);
      return restarted.ok ? std::string("aim trainer restarted") : restarted.error;
    }
  );
  (void)trainerConsole.registerCommand(
    "trainer_abort",
    "Abort the current aim-trainer run without recording a ranked result.",
    [&menu, &editor](const std::vector<std::string>&) {
      menu.abort();
      editor.setOpen(true);
      return std::string("aim trainer aborted");
    }
  );
  RenderSettings settings;
  settings.playerModel = 1;
  settings.drawRemoteWeapons = false;
  settings.showOwnWeapons = true;
  settings.localSelectedWeapon = Weapon::LightningGun;
  if (loadedVideoSettings.loaded) {
    applyTrainerVideoMenu(videoMenu, window, settings);
  }
  const auto clearGameplayInput = [&] {
    forward = false;
    backward = false;
    left = false;
    right = false;
    attack = false;
    jump = false;
    dash = false;
    crouch = false;
    sneak = false;
    zoomed = false;
  };
  const auto openTrainerVideoMenu = [&] {
    videoWheelRemainder = 0.0F;
    clearGameplayInput();
    editor.setOpen(false);
    videoMenu.open = true;
    videoMenu.hoveredRow = -1;
    videoMenu.pressedRow = -1;
    syncTrainerVideoMenu(videoMenu, window, settings);
    syncMenuInput(window, editor, videoMenu.open, consoleState.open);
  };
  const auto closeTrainerMenus = [&] {
    videoMenu.open = false;
    editor.setOpen(false);
    syncMenuInput(window, editor, videoMenu.open, consoleState.open);
  };
  const auto handleTrainerVideoAction = [&](std::string_view action) {
    if (!videoMenu.open) return false;
    if (action == "up") {
      videoMenu.selectedRow =
        (videoMenu.selectedRow + kTrainerVideoSettingCount - 1U) %
        kTrainerVideoSettingCount;
    } else if (action == "down") {
      videoMenu.selectedRow =
        (videoMenu.selectedRow + 1U) % kTrainerVideoSettingCount;
    } else if (action == "left") {
      adjustTrainerVideoMenu(videoMenu, window, -1);
    } else if (action == "right") {
      adjustTrainerVideoMenu(videoMenu, window, 1);
    } else if (action == "activate") {
      if (videoMenu.selectedRow == kTrainerVideoApplyRow) {
        applyTrainerVideoMenu(videoMenu, window, settings);
        videoSettingsWarning.clear();
        (void)saveAimTrainerVideoSettings(
          videoSettingsPath,
          savedTrainerVideoSettings(videoMenu),
          videoSettingsWarning
        );
      } else if (videoMenu.selectedRow == kTrainerVideoCloseRow) {
        videoMenu.open = false;
      } else {
        adjustTrainerVideoMenu(videoMenu, window, 1);
      }
    } else {
      return false;
    }
    keepVideoSelectionVisible(window, videoMenu);
    syncMenuInput(window, editor, videoMenu.open, consoleState.open);
    return true;
  };
  (void)trainerConsole.registerCommand(
    "trainer_menu",
    "Drive trainer menus: video, scenarios, close, up, down, left, right, activate.",
    [&](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2U) {
        return std::string(
          "usage: trainer_menu <video|scenarios|close|up|down|left|right|activate>"
        );
      }
      const std::string_view action = arguments[1];
      if (action == "video") {
        openTrainerVideoMenu();
        return std::string("video menu opened");
      }
      if (action == "scenarios") {
        scenarioWheelRemainder = 0.0F;
        clearGameplayInput();
        videoMenu.open = false;
        editor.setOpen(true);
        keepEditorSelectionVisible(window, editor);
        syncMenuInput(window, editor, videoMenu.open, consoleState.open);
        return std::string("scenario menu opened");
      }
      if (action == "close") {
        closeTrainerMenus();
        return std::string("trainer menus closed");
      }
      if (videoMenu.open) {
        return handleTrainerVideoAction(action)
          ? std::string("video menu action applied")
          : std::string("unknown trainer menu action");
      }
      if (!editor.open()) return std::string("no trainer menu is open");
      if (action == "up") {
        editor.moveSelection(-1);
      } else if (action == "down") {
        editor.moveSelection(1);
      } else if (action == "left" && !editor.editingText()) {
        (void)editor.adjustSelected(-1);
      } else if (action == "right" && !editor.editingText()) {
        (void)editor.adjustSelected(1);
      } else if (action == "activate") {
        if (editor.editingText()) (void)editor.commitText();
        else (void)editor.activateSelected();
      } else {
        return std::string("unknown trainer menu action");
      }
      keepEditorSelectionVisible(window, editor);
      syncMenuInput(window, editor, videoMenu.open, consoleState.open);
      return std::string("scenario menu action applied");
    }
  );
  float yaw = 0.0F;
  float pitch = 0.0F;
  int hoveredRow = -1;
  int pressedRow = -1;
  Weapon requestedWeapon = Weapon::LightningGun;
  std::uint32_t commandSequence = 0;
  std::uint64_t renderedFrameSerial = 0U;
  std::uint64_t hitConfirmSoundsPlayed = 0U;
  float accumulator = 0.0F;
  auto previous = std::chrono::steady_clock::now();
  std::array<bool, Arena::kHealthPickupCount> pickups = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> explosions = {};
  std::array<WeaponFireResult, kDuelPlayerCount> fires = {};
  std::array<RemotePlayerView, kDuelPlayerCount> remotePlayers = {};
  std::optional<TrainerControlOperation> activeControl;
  std::optional<dev::CameraTransform> controlCamera;

  const auto currentControlCamera = [&]() {
    if (controlCamera.has_value()) return *controlCamera;
    dev::CameraTransform camera;
    camera.position = menu.frame().player.position + Vec3{0.0F, 0.0F, 0.65F};
    camera.yawDegrees = yaw * kTrainerRadiansToDegrees;
    camera.pitchDegrees = pitch * kTrainerRadiansToDegrees;
    camera.fieldOfView = settings.fieldOfView;
    return camera;
  };
  const auto captureRelativePath = [&repositoryRoot](
    const std::filesystem::path& path
  ) {
    const std::filesystem::path relative = path.lexically_relative(repositoryRoot);
    return relative.empty() ? path.generic_string() : relative.generic_string();
  };
  const auto timestampMilliseconds = []() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()
    ).count();
  };
  const auto controlStatus = [&]() {
    dev::JsonValue status = dev::JsonValue::objectValue();
    status.object["control_protocol"] = dev::JsonValue::numberValue(1);
    status.object["client_running"] = dev::JsonValue::booleanValue(true);
    status.object["server_running"] = dev::JsonValue::booleanValue(false);
    status.object["connected"] = dev::JsonValue::booleanValue(false);
    status.object["connection_state"] = dev::JsonValue::numberValue(0);
    status.object["connection_message"] =
      dev::JsonValue::stringValue("local aim trainer");
    status.object["map"] = dev::JsonValue::stringValue("aim_trainer");
    status.object["map_revision"] = dev::JsonValue::numberValue(1);
    status.object["game_mode"] = dev::JsonValue::stringValue("AIM_TRAINER");
    status.object["match_state"] = dev::JsonValue::stringValue(
      menu.frame().phase == AimTrainerPhase::Running ? "RUNNING" :
      menu.frame().phase == AimTrainerPhase::Results ? "RESULTS" : "IDLE"
    );
    status.object["spectator"] = dev::JsonValue::booleanValue(false);
    status.object["development_camera"] =
      dev::JsonValue::booleanValue(controlCamera.has_value());
    status.object["benchmark_enabled"] = dev::JsonValue::booleanValue(false);
    status.object["camera"] = dev::cameraJson(currentControlCamera());
    status.object["renderer"] =
      dev::JsonValue::stringValue(std::string(renderer.backendName()));
    status.object["requested_renderer"] =
      dev::JsonValue::stringValue(std::string(renderer.requestedBackendName()));
    status.object["actual_renderer"] =
      dev::JsonValue::stringValue(std::string(renderer.backendName()));
    status.object["gpu_name"] =
      dev::JsonValue::stringValue(std::string(renderer.gpuName()));
    status.object["graphics_driver_name"] =
      dev::JsonValue::stringValue(std::string(renderer.graphicsDriverName()));
    status.object["graphics_driver_version"] =
      dev::JsonValue::stringValue(std::string(renderer.graphicsDriverVersion()));
    status.object["graphics_driver_info"] =
      dev::JsonValue::stringValue(std::string(renderer.graphicsDriverInfo()));
    status.object["software_renderer"] =
      dev::JsonValue::booleanValue(renderer.softwareRenderer());
    status.object["vulkan_api_version"] =
      dev::JsonValue::stringValue(std::string(renderer.vulkanApiVersion()));
    status.object["vulkan_icd_path"] =
      dev::JsonValue::stringValue(std::string(renderer.vulkanIcdPath()));
    status.object["vulkan_icd_sha256"] =
      dev::JsonValue::stringValue(std::string(renderer.vulkanIcdSha256()));
    status.object["gpu_verification_state"] = dev::JsonValue::stringValue(
      renderer.backendName() == "SDL_GPU/vulkan"
        ? "pending-launcher-verification" : "not-verified"
    );
    status.object["gpu_verified"] = dev::JsonValue::booleanValue(false);
    status.object["capture_output_directory"] =
      dev::JsonValue::stringValue(captureDirectory.string());
    status.object["capture_output_relative"] =
      dev::JsonValue::stringValue(captureRelativePath(captureDirectory));
    status.object["rendered_frame"] =
      dev::JsonValue::numberValue(static_cast<double>(renderedFrameSerial));
    status.object["player_position"] = dev::JsonValue::arrayValue({
      dev::JsonValue::numberValue(menu.frame().player.position.x),
      dev::JsonValue::numberValue(menu.frame().player.position.y),
      dev::JsonValue::numberValue(menu.frame().player.position.z),
    });
    status.object["player_yaw"] =
      dev::JsonValue::numberValue(yaw * kTrainerRadiansToDegrees);
    status.object["player_pitch"] =
      dev::JsonValue::numberValue(pitch * kTrainerRadiansToDegrees);
    status.object["player_health"] =
      dev::JsonValue::numberValue(menu.frame().player.health);
    status.object["player_weapon"] = dev::JsonValue::stringValue(
      std::string(weaponShortName(requestedWeapon))
    );
    status.object["audio_available"] =
      dev::JsonValue::booleanValue(audioAvailable);
    status.object["sound_enabled"] =
      dev::JsonValue::booleanValue(trainerConsole.getBool("s_enable"));
    status.object["sound_volume"] =
      dev::JsonValue::numberValue(trainerConsole.getFloat("s_volume"));
    status.object["audio_hit_confirm_count"] =
      dev::JsonValue::numberValue(
        static_cast<double>(hitConfirmSoundsPlayed)
      );
    status.object["menu"] = dev::JsonValue::stringValue(
      consoleState.open ? "console" :
      videoMenu.open ? "video" : editor.open() ? "scenarios" : "closed"
    );
    status.object["menu_selected_row"] = dev::JsonValue::numberValue(
      videoMenu.open ? videoMenu.selectedRow : editor.selectedRow()
    );
    status.object["menu_scroll_rows"] = dev::JsonValue::numberValue(
      videoMenu.open ? videoMenu.scrollRows : editor.scrollRows()
    );
    int windowWidth = 0;
    int windowHeight = 0;
    (void)SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    status.object["window_width"] = dev::JsonValue::numberValue(windowWidth);
    status.object["window_height"] = dev::JsonValue::numberValue(windowHeight);
    dev::JsonValue video = dev::JsonValue::objectValue();
    video.object["display_mode"] =
      dev::JsonValue::numberValue(videoMenu.fullscreenMode);
    video.object["resolution_width"] =
      dev::JsonValue::numberValue(videoMenu.resolution.width);
    video.object["resolution_height"] =
      dev::JsonValue::numberValue(videoMenu.resolution.height);
    video.object["texture_filter"] =
      dev::JsonValue::numberValue(videoMenu.textureFilter);
    video.object["texture_anisotropy"] =
      dev::JsonValue::numberValue(videoMenu.textureAnisotropy);
    video.object["display_gamma"] =
      dev::JsonValue::numberValue(videoMenu.displayGamma);
    video.object["bloom"] = dev::JsonValue::booleanValue(videoMenu.bloom);
    video.object["anti_aliasing"] =
      dev::JsonValue::numberValue(videoMenu.antiAliasing);
    video.object["sun_shadows"] =
      dev::JsonValue::numberValue(videoMenu.sunShadows);
    video.object["point_lights"] =
      dev::JsonValue::numberValue(videoMenu.pointLights);
    status.object["video_menu"] = std::move(video);
    dev::JsonValue targets = dev::JsonValue::arrayValue();
    for (const AimTargetView& target : menu.frame().targets) {
      dev::JsonValue value = dev::JsonValue::objectValue();
      value.object["id"] = dev::JsonValue::numberValue(target.id);
      value.object["visual"] = dev::JsonValue::stringValue(
        target.visual == AimTargetVisual::Worker ? "worker" : "orb"
      );
      value.object["active"] = dev::JsonValue::booleanValue(target.active);
      value.object["position"] = dev::JsonValue::arrayValue({
        dev::JsonValue::numberValue(target.position.x),
        dev::JsonValue::numberValue(target.position.y),
        dev::JsonValue::numberValue(target.position.z),
      });
      value.object["velocity"] = dev::JsonValue::arrayValue({
        dev::JsonValue::numberValue(target.worker.velocity.x),
        dev::JsonValue::numberValue(target.worker.velocity.y),
        dev::JsonValue::numberValue(target.worker.velocity.z),
      });
      value.object["yaw"] =
        dev::JsonValue::numberValue(target.worker.viewYawRadians);
      targets.array.push_back(std::move(value));
    }
    status.object["targets"] = std::move(targets);
    return status;
  };
  const auto completeControlError = [
    &developerControl,
    &activeControl
  ](std::string code, std::string message) {
    if (!activeControl.has_value()) return;
    developerControl.complete(
      activeControl->queued.token,
      dev::errorResponse(
        activeControl->queued.request.id,
        std::move(code),
        std::move(message)
      )
    );
    activeControl.reset();
  };

  while (running) {
    if (developerControl.running() && !activeControl.has_value()) {
      if (std::optional<dev::QueuedControlRequest> queued =
            developerControl.pollRequest(); queued.has_value()) {
        TrainerControlOperation operation;
        operation.queued = std::move(*queued);
        activeControl = std::move(operation);
      }
    }
    if (activeControl.has_value() &&
        activeControl->stage == TrainerControlOperation::Stage::WaitFrames &&
        renderedFrameSerial >= activeControl->targetRenderedFrame) {
      dev::JsonValue result = dev::JsonValue::objectValue();
      result.object["rendered_frame"] =
        dev::JsonValue::numberValue(static_cast<double>(renderedFrameSerial));
      result.object["waited_frames"] = dev::JsonValue::numberValue(
        activeControl->queued.request.waitFrames
      );
      developerControl.complete(
        activeControl->queued.token,
        dev::successResponse(activeControl->queued.request.id, std::move(result))
      );
      activeControl.reset();
    }
    if (activeControl.has_value() &&
        activeControl->stage == TrainerControlOperation::Stage::Start) {
      const dev::ControlRequest& request = activeControl->queued.request;
      switch (request.operation) {
      case dev::ControlOperation::Status:
        developerControl.complete(
          activeControl->queued.token,
          dev::successResponse(request.id, controlStatus())
        );
        activeControl.reset();
        break;
      case dev::ControlOperation::GetCamera:
        developerControl.complete(
          activeControl->queued.token,
          dev::successResponse(request.id, dev::cameraJson(currentControlCamera()))
        );
        activeControl.reset();
        break;
      case dev::ControlOperation::SetCamera: {
        controlCamera = request.camera;
        if (!controlCamera->fieldOfView.has_value()) {
          controlCamera->fieldOfView = settings.fieldOfView;
        }
        dev::JsonValue result = dev::cameraJson(*controlCamera);
        result.object["mode"] =
          dev::JsonValue::stringValue("development_camera");
        developerControl.complete(
          activeControl->queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControl.reset();
        break;
      }
      case dev::ControlOperation::ExecConsole: {
        const std::string output = trainerConsole.execute(request.consoleCommand);
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["command"] =
          dev::JsonValue::stringValue(request.consoleCommand);
        result.object["output"] = dev::JsonValue::stringValue(output);
        developerControl.complete(
          activeControl->queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControl.reset();
        break;
      }
      case dev::ControlOperation::GetCvar:
      case dev::ControlOperation::SetCvar:
        completeControlError(
          "unknown_cvar",
          "the aim trainer has not registered client cvars yet"
        );
        break;
      case dev::ControlOperation::SetPlayerView: {
        controlCamera.reset();
        yaw = request.playerYawDegrees * kTrainerDegreesToRadians;
        pitch = request.playerPitchDegrees * kTrainerDegreesToRadians;
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["yaw"] =
          dev::JsonValue::numberValue(request.playerYawDegrees);
        result.object["pitch"] =
          dev::JsonValue::numberValue(request.playerPitchDegrees);
        developerControl.complete(
          activeControl->queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControl.reset();
        break;
      }
      case dev::ControlOperation::SetPlayerWeapon: {
        const std::optional<Weapon> weapon = parseWeaponToken(request.playerWeapon);
        if (!weapon.has_value()) {
          completeControlError(
            "invalid_weapon",
            "unknown weapon: " + request.playerWeapon
          );
          break;
        }
        requestedWeapon = *weapon;
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["weapon"] = dev::JsonValue::stringValue(
          std::string(weaponShortName(requestedWeapon))
        );
        developerControl.complete(
          activeControl->queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControl.reset();
        break;
      }
      case dev::ControlOperation::WaitFrames:
        activeControl->targetRenderedFrame =
          renderedFrameSerial + request.waitFrames;
        activeControl->stage = TrainerControlOperation::Stage::WaitFrames;
        break;
      case dev::ControlOperation::SendInput:
        controlCamera.reset();
        activeControl->inputTicksRemaining = request.playerInput.ticks;
        activeControl->stage = TrainerControlOperation::Stage::SendInput;
        break;
      case dev::ControlOperation::CaptureScreenshot: {
        const std::string requestedName = request.captureName.empty()
          ? "aim-trainer-" + std::to_string(timestampMilliseconds())
          : request.captureName;
        activeControl->capturePath = captureDirectory /
          (dev::sanitizeGeneratedCaptureName(requestedName) + ".png");
        activeControl->stage = TrainerControlOperation::Stage::Capture;
        break;
      }
      default:
        completeControlError(
          "unsupported_in_aim_trainer",
          "that developer-control operation requires the network game client"
        );
        break;
      }
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
        continue;
      }
      if (event.type == SDL_EVENT_TEXT_INPUT && consoleState.open) {
        const std::string_view text = event.text.text;
        const bool toggleText =
          text == "`" || text == "~" || text == "§" || text == "½";
        if (suppressConsoleToggleText && toggleText) {
          suppressConsoleToggleText = false;
        } else {
          suppressConsoleToggleText = false;
          consoleState.input.insert(consoleState.cursorIndex, text);
          consoleState.cursorIndex += text.size();
        }
        continue;
      }
      if (event.type == SDL_EVENT_TEXT_INPUT && editor.editingText()) {
        editor.insertText(event.text.text);
        continue;
      }
      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (consoleState.open) {
          continue;
        } else if (videoMenu.open) {
          const OptionMenuLayout layout = trainerVideoLayout(window, videoMenu);
          videoMenu.hoveredRow = optionMenuPointInScrollbarTrack(
            layout, event.motion.x, event.motion.y
          ) ? -1 : optionMenuRowAt(
            layout,
            videoMenu.scrollRows,
            kTrainerVideoSettingCount,
            event.motion.y
          );
        } else if (editor.open()) {
          const std::vector<AimTrainerEditorRow> rows = editor.rows();
          const OptionMenuLayout layout = editorLayout(window, editor);
          hoveredRow = optionMenuPointInScrollbarTrack(
            layout,
            event.motion.x,
            event.motion.y
          ) ? -1 : optionMenuRowAt(
            layout,
            editor.scrollRows(),
            rows.size(),
            event.motion.y
          );
        } else {
          const AimTrainerViewAngles angles = applyAimTrainerMouseMotion(
            {yaw, pitch}, event.motion.xrel, event.motion.yrel
          );
          yaw = angles.yaw;
          pitch = angles.pitch;
        }
        continue;
      }
      if (event.type == SDL_EVENT_MOUSE_WHEEL && videoMenu.open) {
        const OptionMenuLayout layout = trainerVideoLayout(window, videoMenu);
        const AimTrainerWheelInput wheel =
          accumulateAimTrainerWheel(videoWheelRemainder, event.wheel.y);
        videoWheelRemainder = wheel.remainder;
        if (wheel.rowDelta < 0) {
          const std::size_t rows = static_cast<std::size_t>(-wheel.rowDelta);
          videoMenu.scrollRows = videoMenu.scrollRows > rows
            ? videoMenu.scrollRows - rows : 0U;
        } else if (wheel.rowDelta > 0) {
          videoMenu.scrollRows = std::min(
            videoMenu.scrollRows + static_cast<std::size_t>(wheel.rowDelta),
            layout.maxScrollRows
          );
        }
        continue;
      }
      if (event.type == SDL_EVENT_MOUSE_WHEEL && editor.open()) {
        const OptionMenuLayout layout = editorLayout(window, editor);
        const AimTrainerWheelInput wheel =
          accumulateAimTrainerWheel(scenarioWheelRemainder, event.wheel.y);
        scenarioWheelRemainder = wheel.remainder;
        std::size_t scrollRows = editor.scrollRows();
        if (wheel.rowDelta < 0) {
          const std::size_t rows = static_cast<std::size_t>(-wheel.rowDelta);
          scrollRows = scrollRows > rows ? scrollRows - rows : 0U;
        } else if (wheel.rowDelta > 0) {
          scrollRows = std::min(
            scrollRows + static_cast<std::size_t>(wheel.rowDelta),
            layout.maxScrollRows
          );
        }
        editor.setScrollRows(scrollRows);
        continue;
      }
      if (
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP
      ) {
        const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (consoleState.open) {
          continue;
        } else if (videoMenu.open) {
          if (event.button.button == SDL_BUTTON_LEFT) {
            const OptionMenuLayout layout = trainerVideoLayout(window, videoMenu);
            const int row = optionMenuRowAt(
              layout,
              videoMenu.scrollRows,
              kTrainerVideoSettingCount,
              event.button.y
            );
            videoMenu.pressedRow = pressed ? row : -1;
            if (!pressed && row >= 0) {
              videoMenu.selectedRow = static_cast<std::size_t>(row);
              if (videoMenu.selectedRow == kTrainerVideoApplyRow) {
                applyTrainerVideoMenu(videoMenu, window, settings);
                videoSettingsWarning.clear();
                (void)saveAimTrainerVideoSettings(
                  videoSettingsPath,
                  savedTrainerVideoSettings(videoMenu),
                  videoSettingsWarning
                );
              } else if (videoMenu.selectedRow == kTrainerVideoCloseRow) {
                videoMenu.open = false;
              } else {
                const int direction = event.button.x <
                  layout.panelX + layout.panelWidth * 0.75F ? -1 : 1;
                adjustTrainerVideoMenu(videoMenu, window, direction);
              }
              syncMenuInput(window, editor, videoMenu.open);
            }
          }
        } else if (editor.open()) {
          if (event.button.button == SDL_BUTTON_LEFT) {
            const std::vector<AimTrainerEditorRow> rows = editor.rows();
            const OptionMenuLayout layout = editorLayout(window, editor);
            const int row = optionMenuRowAt(
              layout,
              editor.scrollRows(),
              rows.size(),
              event.button.y
            );
            pressedRow = pressed ? row : -1;
            if (!pressed && row >= 0) {
              editor.selectRow(static_cast<std::size_t>(row));
              const AimTrainerEditorRow& selected = rows[static_cast<std::size_t>(row)];
              if (selected.command || selected.editable) {
                (void)editor.activateSelected();
              } else {
                const int direction = event.button.x <
                  layout.panelX + layout.panelWidth * 0.75F ? -1 : 1;
                (void)editor.adjustSelected(direction);
              }
              keepEditorSelectionVisible(window, editor);
              syncMenuInput(window, editor, videoMenu.open);
            }
          }
        } else if (event.button.button == SDL_BUTTON_LEFT) {
          attack = pressed;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
          zoomed = pressed;
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
          dash = pressed;
        }
        continue;
      }
      if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) continue;
      const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
      const bool firstPress = pressed && !event.key.repeat;

      if (firstPress && event.key.scancode == SDL_SCANCODE_GRAVE) {
        consoleState.open = !consoleState.open;
        suppressConsoleToggleText = consoleState.open;
        if (consoleState.open) {
          clearGameplayInput();
          editor.setOpen(false);
          videoMenu.open = false;
          consoleState.historyIndex = consoleState.history.size();
        }
        syncMenuInput(window, editor, videoMenu.open, consoleState.open);
        continue;
      }
      if (consoleState.open) {
        if (!pressed) continue;
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
          consoleState.open = false;
        } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
          if (consoleState.cursorIndex > 0U) {
            consoleState.input.erase(consoleState.cursorIndex - 1U, 1U);
            --consoleState.cursorIndex;
          }
        } else if (event.key.scancode == SDL_SCANCODE_DELETE) {
          if (consoleState.cursorIndex < consoleState.input.size()) {
            consoleState.input.erase(consoleState.cursorIndex, 1U);
          }
        } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
          if (consoleState.cursorIndex > 0U) --consoleState.cursorIndex;
        } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
          consoleState.cursorIndex = std::min(
            consoleState.cursorIndex + 1U,
            consoleState.input.size()
          );
        } else if (event.key.scancode == SDL_SCANCODE_HOME) {
          consoleState.cursorIndex = 0U;
        } else if (event.key.scancode == SDL_SCANCODE_END) {
          consoleState.cursorIndex = consoleState.input.size();
        } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
          if (!consoleState.input.empty()) {
            appendTrainerConsoleOutput(consoleState, "] " + consoleState.input);
            const std::string result = trainerConsole.execute(consoleState.input);
            if (!result.empty()) appendTrainerConsoleOutput(consoleState, result);
            consoleState.history.push_back(consoleState.input);
            consoleState.historyIndex = consoleState.history.size();
            consoleState.input.clear();
            consoleState.cursorIndex = 0U;
          }
        } else if (event.key.scancode == SDL_SCANCODE_UP &&
                   !consoleState.history.empty()) {
          if (consoleState.historyIndex > 0U) --consoleState.historyIndex;
          consoleState.input = consoleState.history[consoleState.historyIndex];
          consoleState.cursorIndex = consoleState.input.size();
        } else if (event.key.scancode == SDL_SCANCODE_DOWN &&
                   !consoleState.history.empty()) {
          if (consoleState.historyIndex + 1U < consoleState.history.size()) {
            ++consoleState.historyIndex;
            consoleState.input = consoleState.history[consoleState.historyIndex];
          } else {
            consoleState.historyIndex = consoleState.history.size();
            consoleState.input.clear();
          }
          consoleState.cursorIndex = consoleState.input.size();
        } else if (event.key.scancode == SDL_SCANCODE_TAB) {
          const std::vector<std::string> matches =
            trainerConsole.complete(consoleState.input);
          if (matches.size() == 1U) {
            consoleState.input = matches.front();
            consoleState.cursorIndex = consoleState.input.size();
          } else if (!matches.empty()) {
            std::string line;
            for (const std::string& match : matches) line += match + ' ';
            appendTrainerConsoleOutput(consoleState, line);
          }
        }
        syncMenuInput(window, editor, videoMenu.open, consoleState.open);
        continue;
      }

      if (firstPress && event.key.scancode == SDL_SCANCODE_F10) {
        if (videoMenu.open) closeTrainerMenus();
        else openTrainerVideoMenu();
        continue;
      }
      if (videoMenu.open) {
        const bool repeatable =
          event.key.scancode == SDL_SCANCODE_UP ||
          event.key.scancode == SDL_SCANCODE_DOWN ||
          event.key.scancode == SDL_SCANCODE_LEFT ||
          event.key.scancode == SDL_SCANCODE_RIGHT;
        if (!shouldHandleAimTrainerMenuKeyDown(pressed, event.key.repeat, repeatable)) {
          continue;
        }
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) closeTrainerMenus();
        else if (event.key.scancode == SDL_SCANCODE_UP) {
          (void)handleTrainerVideoAction("up");
        } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
          (void)handleTrainerVideoAction("down");
        } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
          (void)handleTrainerVideoAction("left");
        } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
          (void)handleTrainerVideoAction("right");
        } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
          (void)handleTrainerVideoAction("activate");
        }
        continue;
      }
      if (firstPress && event.key.scancode == SDL_SCANCODE_F3) {
        if (menu.frame().phase != AimTrainerPhase::Running) {
          const AimTrainerArmResult started = menu.start();
          if (started.ok) {
            clearGameplayInput();
            editor.setOpen(false);
          }
        }
        hoveredRow = -1;
        pressedRow = -1;
        syncMenuInput(window, editor, videoMenu.open);
        continue;
      }
      if (firstPress && event.key.scancode == SDL_SCANCODE_F5) {
        const AimTrainerArmResult restarted = menu.restart();
        if (restarted.ok) {
          clearGameplayInput();
          editor.setOpen(false);
        }
        hoveredRow = -1;
        pressedRow = -1;
        syncMenuInput(window, editor, videoMenu.open);
        continue;
      }
      if (firstPress && event.key.scancode == SDL_SCANCODE_ESCAPE) {
        switch (aimTrainerEscapeAction(editor.open(), editor.editingText())) {
        case AimTrainerEscapeAction::CancelText:
          editor.cancelText();
          break;
        case AimTrainerEscapeAction::OpenScenarios:
          scenarioWheelRemainder = 0.0F;
          clearGameplayInput();
          editor.setOpen(true);
          break;
        case AimTrainerEscapeAction::CloseScenarios:
          editor.setOpen(false);
          break;
        }
        keepEditorSelectionVisible(window, editor);
        syncMenuInput(window, editor, videoMenu.open);
        continue;
      }
      if (editor.open()) {
        const bool repeatable =
          event.key.scancode == SDL_SCANCODE_UP ||
          event.key.scancode == SDL_SCANCODE_DOWN ||
          event.key.scancode == SDL_SCANCODE_LEFT ||
          event.key.scancode == SDL_SCANCODE_RIGHT;
        if (!shouldHandleAimTrainerMenuKeyDown(pressed, event.key.repeat, repeatable)) {
          continue;
        }
        if (event.key.scancode == SDL_SCANCODE_UP) {
          editor.moveSelection(-1);
        } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
          editor.moveSelection(1);
        } else if (event.key.scancode == SDL_SCANCODE_LEFT && !editor.editingText()) {
          (void)editor.adjustSelected(-1);
        } else if (event.key.scancode == SDL_SCANCODE_RIGHT && !editor.editingText()) {
          (void)editor.adjustSelected(1);
        } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
          if (editor.editingText()) (void)editor.commitText();
          else (void)editor.activateSelected();
        } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
          editor.backspace();
        }
        keepEditorSelectionVisible(window, editor);
        syncMenuInput(window, editor, videoMenu.open);
        continue;
      }
      switch (event.key.scancode) {
      case SDL_SCANCODE_W: forward = pressed; break;
      case SDL_SCANCODE_S: backward = pressed; break;
      case SDL_SCANCODE_A: left = pressed; break;
      case SDL_SCANCODE_D: right = pressed; break;
      case SDL_SCANCODE_SPACE: jump = pressed; break;
      case SDL_SCANCODE_Q: dash = pressed; break;
      case SDL_SCANCODE_LCTRL: crouch = pressed; break;
      case SDL_SCANCODE_LSHIFT: sneak = pressed; break;
      default: break;
      }
      if (
        firstPress &&
        event.key.scancode >= SDL_SCANCODE_1 &&
        event.key.scancode <= SDL_SCANCODE_9
      ) {
        requestedWeapon = static_cast<Weapon>(
          event.key.scancode - SDL_SCANCODE_1
        );
      }
    }

    const auto now = std::chrono::steady_clock::now();
    const float elapsed = std::chrono::duration<float>(now - previous).count();
    previous = now;
    const FixedTickFrame plan = planFixedTicks(accumulator, elapsed, kFixedTickSeconds, 8);
    const AimTrainerPhase beforeTicks = menu.frame().phase;
    for (int index = 0; index < plan.tickCount; ++index) {
      UserCommand command;
      command.sequence = ++commandSequence;
      const bool controlled = activeControl.has_value() &&
        activeControl->stage == TrainerControlOperation::Stage::SendInput;
      if (controlled) {
        const dev::PlayerInput& input = activeControl->queued.request.playerInput;
        const bool firstTick = activeControl->inputTicksRemaining == input.ticks;
        if (input.yawDegrees.has_value()) {
          yaw = *input.yawDegrees * kTrainerDegreesToRadians;
          pitch = *input.pitchDegrees * kTrainerDegreesToRadians;
        }
        if (!input.weapon.empty()) {
          requestedWeapon = *parseWeaponToken(input.weapon);
        }
        command.viewYawRadians = yaw;
        command.viewPitchRadians = pitch;
        command.forwardMove = input.forward;
        command.rightMove = input.right;
        command.upMove = input.up;
        command.attack = input.attack && (!input.attackOneTick || firstTick);
        command.jump = input.jump && (!input.jumpOneTick || firstTick);
        command.dash = input.dash && (!input.dashOneTick || firstTick);
        command.crouch = input.crouch && (!input.crouchOneTick || firstTick);
        command.sneak = input.sneak && (!input.sneakOneTick || firstTick);
        command.zoomed = input.zoom && (!input.zoomOneTick || firstTick);
        command.weapon = requestedWeapon;
      } else {
        command.viewYawRadians = yaw;
        command.viewPitchRadians = pitch;
        command.forwardMove = (forward ? 1.0F : 0.0F) - (backward ? 1.0F : 0.0F);
        command.rightMove = (right ? 1.0F : 0.0F) - (left ? 1.0F : 0.0F);
        command.attack = attack;
        command.jump = jump;
        command.dash = dash;
        command.crouch = crouch;
        command.sneak = sneak;
        command.zoomed = zoomed;
        command.weapon = requestedWeapon;
      }
      command.planarAim = false;
      menu.tick(command);
      if (controlled && activeControl->inputTicksRemaining > 0U) {
        --activeControl->inputTicksRemaining;
      }
    }
    if (audioAvailable) {
      const bool soundEnabled = trainerConsole.getBool("s_enable");
      const float masterVolume = trainerConsole.getFloat("s_volume");
      const auto soundVolume = [&trainerConsole, masterVolume](
        std::string_view name
      ) {
        return masterVolume * trainerConsole.getFloat(name);
      };
      if (soundEnabled) {
        for (const WeaponFireResult& fire : menu.frame().pendingFires) {
          const WeaponFireAudioEvent event =
            routeWeaponFireAudioEvent(fire, true);
          switch (event.cue) {
          case WeaponFireAudioCue::Railgun:
            audio.playRailFire(soundVolume("s_rg_fire_volume"));
            break;
          case WeaponFireAudioCue::Revolver:
            audio.playRevolverFire(soundVolume("s_rg_fire_volume"));
            break;
          case WeaponFireAudioCue::RocketLauncher:
            audio.playRocketFire(soundVolume("s_rl_fire_volume"));
            break;
          case WeaponFireAudioCue::MachineGun:
            audio.playMachineGunFire(soundVolume("s_mg_fire_volume"));
            break;
          case WeaponFireAudioCue::Shotgun:
            audio.playShotgunFire(soundVolume("s_sg_fire_volume"));
            break;
          case WeaponFireAudioCue::GrenadeLauncher:
            audio.playGrenadeLauncherFire(soundVolume("s_gl_fire_volume"));
            break;
          case WeaponFireAudioCue::PlasmaGun:
            audio.playPlasmaGunFire(soundVolume("s_pg_fire_volume"));
            break;
          case WeaponFireAudioCue::None:
            break;
          }
        }
        if (menu.frame().hitConfirmPending) {
          audio.playHit(
            soundVolume("s_hit_volume"),
            static_cast<int>(std::min(
              menu.frame().pendingHitConfirmDamage,
              static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            )),
            menu.frame().pendingHitConfirmHeadshot
          );
          ++hitConfirmSoundsPlayed;
        }
      }
      const bool beamActive =
        soundEnabled && menu.frame().latestBeam.active;
      audio.setLightningGunFire(
        beamActive,
        beamActive ? soundVolume("s_lg_fire_volume") : 0.0F
      );
      audio.update();
    }
    if (activeControl.has_value() &&
        activeControl->stage == TrainerControlOperation::Stage::SendInput &&
        activeControl->inputTicksRemaining == 0U) {
      dev::JsonValue result = dev::JsonValue::objectValue();
      result.object["ticks"] = dev::JsonValue::numberValue(
        activeControl->queued.request.playerInput.ticks
      );
      result.object["last_command_sequence"] =
        dev::JsonValue::numberValue(commandSequence);
      developerControl.complete(
        activeControl->queued.token,
        dev::successResponse(activeControl->queued.request.id, std::move(result))
      );
      activeControl.reset();
    }
    if (
      beforeTicks == AimTrainerPhase::Running &&
      menu.frame().phase == AimTrainerPhase::Results
    ) {
      clearGameplayInput();
      editor.setOpen(true);
      syncMenuInput(window, editor, videoMenu.open);
    }

    const AimTrainerPresentation presentation =
      buildAimTrainerPresentation(menu.frame());
    fires = {};
    for (
      std::size_t index = 0;
      index < menu.frame().pendingFires.size() && index < fires.size();
      ++index
    ) {
      fires[index] = menu.frame().pendingFires[index];
    }
    settings.localSelectedWeapon = menu.frame().selectedWeapon;
    PlayerState renderPlayer = menu.frame().player;
    RenderSettings renderSettings = settings;
    renderSettings.presentationTimeSeconds =
      presentation.animationTimeSeconds;
    if (controlCamera.has_value()) {
      renderPlayer = {};
      renderPlayer.position =
        controlCamera->position - Vec3{0.0F, 0.0F, 0.65F};
      renderPlayer.viewYawRadians =
        controlCamera->yawDegrees * kTrainerDegreesToRadians;
      renderPlayer.viewPitchRadians =
        controlCamera->pitchDegrees * kTrainerDegreesToRadians;
      renderPlayer.health = 100;
      renderSettings.fieldOfView =
        controlCamera->fieldOfView.value_or(renderSettings.fieldOfView);
      renderSettings.showOwnWeapons = false;
    }
    HudRenderState hud;
    int hudWidth = 0;
    int hudHeight = 0;
    (void)SDL_GetWindowSizeInPixels(window, &hudWidth, &hudHeight);
    (void)hudHeight;
    addTrainerHud(
      hud,
      menu,
      editor,
      hoveredRow,
      pressedRow,
      balanceWarning,
      hudWidth
    );
    addTrainerVideoHud(hud, videoMenu, window, videoSettingsWarning);
    ConsoleRenderState console = trainerConsoleRenderState(consoleState);
    std::optional<FrameCaptureRequest> captureRequest;
    FrameCaptureResult captureResult;
    if (activeControl.has_value() &&
        activeControl->stage == TrainerControlOperation::Stage::Capture) {
      const dev::ControlRequest& request = activeControl->queued.request;
      captureRequest = FrameCaptureRequest{
        activeControl->capturePath.string(),
        request.hideHud,
        request.hideOverlays,
      };
      if (request.hideHud) hud = {};
      if (request.hideOverlays) console = {};
    }
    renderer.render(
      map.arena,
      renderPlayer,
      remotePlayers,
      menu.frame().latestBeam,
      fires,
      explosions,
      presentation.projectiles,
      menu.frame().icePools,
      pickups,
      {},
      presentation.targetEffects,
      0U,
      renderSettings,
      hud,
      console,
      {},
      captureRequest.has_value() ? &*captureRequest : nullptr,
      captureRequest.has_value() ? &captureResult : nullptr
    );
    ++renderedFrameSerial;
    if (captureRequest.has_value() && activeControl.has_value() &&
        activeControl->stage == TrainerControlOperation::Stage::Capture) {
      const dev::ControlRequest& request = activeControl->queued.request;
      dev::JsonValue capture = dev::JsonValue::objectValue();
      capture.object["ok"] = dev::JsonValue::booleanValue(captureResult.ok);
      capture.object["path"] =
        dev::JsonValue::stringValue(activeControl->capturePath.string());
      capture.object["relative_path"] = dev::JsonValue::stringValue(
        captureRelativePath(activeControl->capturePath)
      );
      capture.object["width"] =
        dev::JsonValue::numberValue(captureResult.width);
      capture.object["height"] =
        dev::JsonValue::numberValue(captureResult.height);
      capture.object["map"] = dev::JsonValue::stringValue("aim_trainer");
      capture.object["map_revision"] = dev::JsonValue::numberValue(1);
      capture.object["map_content_hash"] =
        dev::JsonValue::numberValue(map.descriptor.contentHash);
      capture.object["renderer"] =
        dev::JsonValue::stringValue(std::string(renderer.backendName()));
      capture.object["camera"] = dev::cameraJson(currentControlCamera());
      capture.object["timestamp_ms"] = dev::JsonValue::numberValue(
        static_cast<double>(timestampMilliseconds())
      );
      dev::JsonValue frameState = dev::JsonValue::objectValue();
      frameState.object["rendered_frame_serial"] =
        dev::JsonValue::numberValue(static_cast<double>(renderedFrameSerial));
      frameState.object["scenario"] =
        dev::JsonValue::stringValue(menu.draft().name);
      frameState.object["score"] =
        dev::JsonValue::numberValue(menu.frame().stats.score);
      frameState.object["phase"] = dev::JsonValue::stringValue(
        menu.frame().phase == AimTrainerPhase::Running ? "running" :
        menu.frame().phase == AimTrainerPhase::Results ? "results" : "idle"
      );
      capture.object["frame_state"] = std::move(frameState);
      if (captureResult.ok) {
        developerControl.complete(
          activeControl->queued.token,
          dev::successResponse(request.id, std::move(capture))
        );
      } else {
        developerControl.complete(
          activeControl->queued.token,
          dev::errorResponse(request.id, "capture_failed", captureResult.error)
        );
      }
      activeControl.reset();
    }
    menu.consumePresentationEvents();
  }
  developerControl.stop();
  audio.shutdown();
  renderer.shutdown();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
#else
  std::cout << "Aim trainer requires SDL3. Build with SDL3 enabled.\n";
  return 1;
#endif
}

} // namespace lg
