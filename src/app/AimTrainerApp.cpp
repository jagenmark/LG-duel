#include "app/AimTrainerApp.hpp"

#include "render/OptionMenuLayout.hpp"
#include "render/Renderer.hpp"
#include "shared/Constants.hpp"
#include "shared/FixedTick.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/MapRegistry.hpp"
#include "trainer/AimTrainerEditor.hpp"
#include "trainer/AimTrainerPresentation.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace lg {
namespace {

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
  std::string_view balanceWarning
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
  hud.centerLines.push_back(
    frame.phase == AimTrainerPhase::Running ? "TRAINING" :
      frame.phase == AimTrainerPhase::Results ? frame.message : "F3: START  ESC: SCENARIOS"
  );
  hud.bottomCenterLines.push_back(
    "LMB fire  RMB zoom  WASD move  SPACE jump  Q dash  CTRL crouch  SHIFT sneak"
  );
  hud.bottomCenterLines.push_back("1-9 switch weapon  F3 start  F5 restart  ESC scenarios");

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
    ? "Type a value. ENTER accepts; ESC cancels."
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

void syncMenuInput(SDL_Window* window, const AimTrainerEditor& editor) {
  (void)SDL_SetWindowRelativeMouseMode(window, !editor.open());
  if (editor.editingText()) {
    (void)SDL_StartTextInput(window);
  } else {
    (void)SDL_StopTextInput(window);
  }
}
#endif

} // namespace

int AimTrainerApp::run() const {
#if LG_DUEL_HAS_SDL3
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 1;
  }
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
  syncMenuInput(window, editor);

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
  float yaw = 0.0F;
  float pitch = 0.0F;
  int hoveredRow = -1;
  int pressedRow = -1;
  Weapon requestedWeapon = Weapon::LightningGun;
  std::uint32_t commandSequence = 0;
  float accumulator = 0.0F;
  auto previous = std::chrono::steady_clock::now();
  RenderSettings settings;
  settings.playerModel = 1;
  settings.drawRemoteWeapons = false;
  settings.showOwnWeapons = true;
  settings.localSelectedWeapon = Weapon::LightningGun;
  std::array<bool, Arena::kHealthPickupCount> pickups = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> explosions = {};
  std::array<WeaponFireResult, kDuelPlayerCount> fires = {};
  std::array<RemotePlayerView, kDuelPlayerCount> remotePlayers = {};

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
        continue;
      }
      if (event.type == SDL_EVENT_TEXT_INPUT && editor.editingText()) {
        editor.insertText(event.text.text);
        continue;
      }
      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (editor.open()) {
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
          yaw += event.motion.xrel * 0.0025F;
          pitch = std::clamp(pitch - event.motion.yrel * 0.0025F, -1.5F, 1.5F);
        }
        continue;
      }
      if (event.type == SDL_EVENT_MOUSE_WHEEL && editor.open()) {
        const OptionMenuLayout layout = editorLayout(window, editor);
        editor.setScrollRows(optionMenuScrollForWheel(
          layout,
          editor.scrollRows(),
          event.wheel.y
        ));
        continue;
      }
      if (
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP
      ) {
        const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (editor.open()) {
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
              syncMenuInput(window, editor);
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
        syncMenuInput(window, editor);
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
        syncMenuInput(window, editor);
        continue;
      }
      if (editor.open()) {
        if (!firstPress) continue;
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
          if (editor.editingText()) editor.cancelText();
          else if (menu.frame().phase == AimTrainerPhase::Running) editor.setOpen(false);
          else running = false;
        } else if (event.key.scancode == SDL_SCANCODE_UP) {
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
        syncMenuInput(window, editor);
        continue;
      }

      if (firstPress && event.key.scancode == SDL_SCANCODE_ESCAPE) {
        clearGameplayInput();
        editor.setOpen(true);
        syncMenuInput(window, editor);
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
      command.planarAim = false;
      command.weapon = requestedWeapon;
      menu.tick(command);
    }
    if (
      beforeTicks == AimTrainerPhase::Running &&
      menu.frame().phase == AimTrainerPhase::Results
    ) {
      clearGameplayInput();
      editor.setOpen(true);
      syncMenuInput(window, editor);
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
    HudRenderState hud;
    addTrainerHud(
      hud,
      menu,
      editor,
      hoveredRow,
      pressedRow,
      balanceWarning
    );
    ConsoleRenderState console;
    renderer.render(
      map.arena,
      menu.frame().player,
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
      settings,
      hud,
      console
    );
    menu.consumePresentationEvents();
  }
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
