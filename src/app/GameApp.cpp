#include "app/GameApp.hpp"

#include "app/ClientAudio.hpp"
#include "app/ClientChat.hpp"
#include "app/ClientCvars.hpp"
#include "app/ConsoleInput.hpp"
#include "app/HudPresentation.hpp"
#include "app/Scoreboard.hpp"
#include "app/TextInput.hpp"
#include "client/ClientSession.hpp"
#include "client/HitConfirmAudio.hpp"
#include "client/LocalHitFeedback.hpp"
#include "console/ConsoleSystem.hpp"
#include "input/InputBindings.hpp"
#include "input/MouseAim.hpp"
#include "render/ConsoleLayout.hpp"
#include "render/ChatLayout.hpp"
#include "render/Renderer.hpp"
#include "shared/Constants.hpp"
#include "shared/FixedTick.hpp"
#include "shared/Math.hpp"
#include "shared/Sequence.hpp"
#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/Movement.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"
#include "sim/WeaponCatalog.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lg {
namespace {

constexpr float kHalfPi = 1.57079632679F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;
constexpr int kMaxSimulationTicksPerFrame = 8;
constexpr float kDegreesToRadians = 0.01745329252F;
constexpr std::uint32_t kClientRailgunCooldownTicks = 188;
constexpr float kRailgunBeamLingerSeconds = 0.5F;

enum class AimMode {
  Relative3D,
  Absolute2D,
};

[[nodiscard]] AimMode aimModeFromInt(int value) {
  return value == 1 ? AimMode::Absolute2D : AimMode::Relative3D;
}

[[nodiscard]] std::uint8_t selfDamagePercent(const ConsoleSystem& console) {
  return static_cast<std::uint8_t>(
    std::clamp(static_cast<int>(std::lround(console.getFloat("g_selfdamage"))), 0, 100)
  );
}

#if LG_DUEL_HAS_SDL3
[[nodiscard]] bool isClipboardPasteKey(const SDL_KeyboardEvent& event) {
  return event.key == SDLK_V && (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

[[nodiscard]] bool isClipboardCopyKey(const SDL_KeyboardEvent& event) {
  return event.key == SDLK_C && (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

void pasteClipboardTextIntoConsole(std::string& input, std::size_t& cursorIndex) {
  char* clipboardText = SDL_GetClipboardText();
  if (clipboardText == nullptr) {
    return;
  }
  appendConsolePasteText(input, cursorIndex, clipboardText);
  SDL_free(clipboardText);
}

void copyTextToClipboard(std::string_view text) {
  const std::string clipboardText{text};
  (void)SDL_SetClipboardText(clipboardText.c_str());
}
#endif

[[nodiscard]] std::int32_t healthAmount(const ConsoleSystem& console) {
  return std::clamp(console.getInt("g_healthamount"), 1, 100000);
}

[[nodiscard]] WeaponDamageTuning weaponDamageTuning(const ConsoleSystem& console) {
  return {
    console.getInt("g_sg_damage"),
    console.getInt("g_mg_damage"),
    console.getInt("g_lg_damage"),
    console.getInt("g_rg_damage"),
    console.getInt("g_rl_damage"),
  };
}

[[nodiscard]] DamageNumbersConfig damageNumbersConfig(
  const ConsoleSystem& console
) {
  return {
    static_cast<DamageNumbersMode>(console.getInt("r_damage_numbers_mode")),
    console.getFloat("r_damage_numbers_window"),
    console.getFloat("r_damage_numbers_duration"),
  };
}

[[nodiscard]] LocalDamageSource localDamageSourceForWeapon(Weapon weapon) {
  if (weapon == Weapon::LightningGun) {
    return LocalDamageSource::LightningGun;
  }
  if (weapon == Weapon::RocketLauncher || weapon == Weapon::GrenadeLauncher) {
    return LocalDamageSource::RocketExplosion;
  }
  return LocalDamageSource::WeaponFire;
}

[[nodiscard]] Vec3 cameraUp(float yawRadians, float pitchRadians) {
  return {
    -std::cos(yawRadians) * std::sin(pitchRadians),
    -std::sin(yawRadians) * std::sin(pitchRadians),
    std::cos(pitchRadians),
  };
}

[[nodiscard]] Vec3 viewmodelMuzzlePosition(const PlayerState& player) {
  constexpr CollisionBounds defaultBounds = {};
  const float eyeHeight =
    0.65F * (player.bounds.halfHeight / defaultBounds.halfHeight);
  const Vec3 eyePosition =
    player.position + Vec3{0.0F, 0.0F, eyeHeight};
  return eyePosition +
    cameraForward(player.viewYawRadians, player.viewPitchRadians) * 0.55F -
    cameraUp(player.viewYawRadians, player.viewPitchRadians) * 0.32F;
}

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
  float mouseX = 0.0F;
  float mouseY = 0.0F;
  bool hasMousePosition = false;
};

struct PresentationViewState {
  float yawRadians = 0.0F;
  float pitchRadians = 0.0F;
  bool initialized = false;
};

struct FrameTimeSummary {
  float averageMilliseconds = 0.0F;
  float p50Milliseconds = 0.0F;
  float p95Milliseconds = 0.0F;
  float p99Milliseconds = 0.0F;
  float maxMilliseconds = 0.0F;
};

struct FrameTimeHistory {
  static constexpr std::size_t kSampleCount = 512;

  std::array<float, kSampleCount> samples = {};
  std::size_t nextSample = 0;
  std::size_t sampleCount = 0;
  std::array<float, kSampleCount> sortedSamples = {};

  void push(float milliseconds) {
    samples[nextSample] = milliseconds;
    nextSample = (nextSample + 1U) % samples.size();
    sampleCount = std::min(sampleCount + 1U, samples.size());
  }

  [[nodiscard]] FrameTimeSummary summarize() {
    if (sampleCount == 0) {
      return {};
    }

    float sum = 0.0F;
    for (std::size_t index = 0; index < sampleCount; ++index) {
      const float sample = samples[index];
      sortedSamples[index] = sample;
      sum += sample;
    }
    std::sort(sortedSamples.begin(), sortedSamples.begin() + sampleCount);
    const auto percentile =
      [&](float fraction) {
        const std::size_t index = std::min(
          sampleCount - 1U,
          static_cast<std::size_t>(
            std::round(fraction * static_cast<float>(sampleCount - 1U))
          )
        );
        return sortedSamples[index];
      };

    return {
      sum / static_cast<float>(sampleCount),
      percentile(0.50F),
      percentile(0.95F),
      percentile(0.99F),
      sortedSamples[sampleCount - 1U],
    };
  }
};

#if LG_DUEL_HAS_SDL3
struct ClientConsoleState {
  bool open = false;
  std::string input;
  std::size_t cursorIndex = 0;
  std::deque<std::string> output;
  std::vector<std::string> history;
  std::size_t historyIndex = 0;
  bool hasSelection = false;
  bool selecting = false;
  std::size_t selectionAnchor = 0;
  std::size_t selectionFocus = 0;
};

struct ClientChatState {
  struct Message {
    std::uint8_t playerIndex = 0;
    std::string text;
    std::string speakerName;
  };

  bool inputOpen = false;
  std::string input;
  std::size_t cursorIndex = 0;
  TextSelection selection;
  bool selecting = false;
  std::string pendingMessage;
  std::deque<Message> history;
  std::uint32_t lastSequence = 0;
  std::chrono::steady_clock::time_point visibleUntil = {};
};

struct LingeringRailBeam {
  WeaponFireResult fire;
  WeaponFireResult sourceFire;
  bool active = false;
  std::chrono::steady_clock::time_point expiresAt = {};
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
    const std::size_t firstText = line.find_first_not_of(" \t");
    if (firstText == std::string::npos || line[firstText] == '#') {
      continue;
    }
    (void)console.execute(line);
  }
}

std::filesystem::path soundMixerConfigPath(
  const std::filesystem::path& assetBasePath
) {
  return assetBasePath / "config" / "sound_mixer.cfg";
}

void loadSoundMixerConfigs(
  ConsoleSystem& console,
  const std::filesystem::path& assetBasePath
) {
  loadClientConfig(console, soundMixerConfigPath(assetBasePath).string());

  std::filesystem::path directory = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    loadClientConfig(console, (directory / "config" / "sound_mixer.cfg").string());
    if (!directory.has_parent_path() || directory == directory.parent_path()) {
      break;
    }
    directory = directory.parent_path();
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

RenderSettings renderSettings(const ConsoleSystem& console) {
  RenderSettings settings;
  settings.renderMode = console.getInt("cl_render_mode");
  settings.fieldOfView = console.getFloat("cl_fov");
  settings.cameraZoom = console.getFloat("cl_camera_zoom");
  settings.rotateView = console.getBool("cl_rotate_view");
  settings.healthTextScale = console.getFloat("cl_health_size");
  settings.crosshairEnabled = console.getBool("crosshair_enable");
  settings.crosshairStyle = console.getInt("crosshair_style");
  settings.crosshairSize = console.getFloat("crosshair_size");
  settings.crosshairThickness = console.getFloat("crosshair_thickness");
  settings.crosshairGap = console.getFloat("crosshair_gap");
  settings.crosshairAlpha = console.getFloat("crosshair_alpha");
  settings.crosshairRed = static_cast<std::uint8_t>(console.getInt("crosshair_r"));
  settings.crosshairGreen = static_cast<std::uint8_t>(console.getInt("crosshair_g"));
  settings.crosshairBlue = static_cast<std::uint8_t>(console.getInt("crosshair_b"));
  settings.crosshairHitRed = static_cast<std::uint8_t>(console.getInt("crosshair_hit_r"));
  settings.crosshairHitGreen = static_cast<std::uint8_t>(console.getInt("crosshair_hit_g"));
  settings.crosshairHitBlue = static_cast<std::uint8_t>(console.getInt("crosshair_hit_b"));
  settings.beamWidth = console.getFloat("r_beam_width");
  settings.beamAlpha = console.getFloat("r_beam_alpha");
  settings.beamRed = static_cast<std::uint8_t>(console.getInt("r_beam_r"));
  settings.beamGreen = static_cast<std::uint8_t>(console.getInt("r_beam_g"));
  settings.beamBlue = static_cast<std::uint8_t>(console.getInt("r_beam_b"));
  settings.beamHitRed =
    static_cast<std::uint8_t>(console.getInt("r_beam_hit_r"));
  settings.beamHitGreen =
    static_cast<std::uint8_t>(console.getInt("r_beam_hit_g"));
  settings.beamHitBlue =
    static_cast<std::uint8_t>(console.getInt("r_beam_hit_b"));
  settings.enemyBeamWidth = console.getFloat("r_enemy_beam_width");
  settings.enemyBeamAlpha = console.getFloat("r_enemy_beam_alpha");
  settings.enemyBeamRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_beam_r"));
  settings.enemyBeamGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_beam_g"));
  settings.enemyBeamBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_beam_b"));
  settings.hitMarkerEnabled = console.getBool("r_hitmarker_enable");
  settings.hitMarkerSize = console.getFloat("r_hitmarker_size");
  settings.hitMarkerThickness = console.getFloat("r_hitmarker_thickness");
  settings.hitMarkerRed =
    static_cast<std::uint8_t>(console.getInt("r_hitmarker_r"));
  settings.hitMarkerGreen =
    static_cast<std::uint8_t>(console.getInt("r_hitmarker_g"));
  settings.hitMarkerBlue =
    static_cast<std::uint8_t>(console.getInt("r_hitmarker_b"));
  settings.damageNumbersDuration = console.getFloat("r_damage_numbers_duration");
  settings.damageNumbersSize = console.getFloat("r_damage_numbers_size");
  settings.damageNumbersAlpha = console.getFloat("r_damage_numbers_alpha");
  settings.damageNumbersRed =
    static_cast<std::uint8_t>(console.getInt("r_damage_numbers_r"));
  settings.damageNumbersGreen =
    static_cast<std::uint8_t>(console.getInt("r_damage_numbers_g"));
  settings.damageNumbersBlue =
    static_cast<std::uint8_t>(console.getInt("r_damage_numbers_b"));
  settings.damageNumbersOffsetX =
    console.getFloat("r_damage_numbers_offset_x");
  settings.damageNumbersOffsetY =
    console.getFloat("r_damage_numbers_offset_y");
  settings.enemyRed = static_cast<std::uint8_t>(console.getInt("r_enemy_r"));
  settings.enemyGreen = static_cast<std::uint8_t>(console.getInt("r_enemy_g"));
  settings.enemyBlue = static_cast<std::uint8_t>(console.getInt("r_enemy_b"));
  settings.enemyAlpha = console.getFloat("r_enemy_alpha");
  settings.enemyOutlineEnabled = console.getBool("r_enemy_outline_enable");
  settings.playerOutlineStyle = static_cast<PlayerOutlineStyle>(
    console.getInt("r_player_outline_style")
  );
  settings.enemyOutlineWidth = console.getFloat("r_enemy_outline_width");
  settings.enemyOutlineAlpha = console.getFloat("r_enemy_outline_alpha");
  settings.enemyOutlineRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_outline_r"));
  settings.enemyOutlineGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_outline_g"));
  settings.enemyOutlineBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_outline_b"));
  settings.enemyLeanEnabled = console.getBool("r_enemy_lean");
  settings.enemyLeanScale = console.getFloat("r_enemy_lean_scale");
  settings.enemyHitRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_hit_r"));
  settings.enemyHitGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_hit_g"));
  settings.enemyHitBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_hit_b"));
  settings.enemyHealthBarEnabled = console.getBool("r_enemy_health_enable");
  settings.enemyHealthBarDamageOnly =
    console.getBool("r_enemy_health_damage_only");
  settings.enemyHealthBarFade = console.getBool("r_enemy_health_fade");
  settings.enemyHealthBarVisibleDuration =
    console.getFloat("r_enemy_health_duration");
  settings.enemyHealthBarMaxDistance =
    console.getFloat("r_enemy_health_max_distance");
  settings.enemyHealthBarWidth = console.getFloat("r_enemy_health_width");
  settings.enemyHealthBarHeight = console.getFloat("r_enemy_health_height");
  settings.enemyHealthBarWorldOffsetZ =
    console.getFloat("r_enemy_health_offset_z");
  settings.enemyHealthBarScreenOffsetX =
    console.getFloat("r_enemy_health_offset_x");
  settings.enemyHealthBarScreenOffsetY =
    console.getFloat("r_enemy_health_offset_y");
  settings.enemyHealthBarAlpha = console.getFloat("r_enemy_health_alpha");
  settings.enemyHealthBarRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_health_r"));
  settings.enemyHealthBarGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_health_g"));
  settings.enemyHealthBarBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_health_b"));
  settings.teammateBeamWidth = console.getFloat("r_teammate_beam_width");
  settings.teammateBeamAlpha = console.getFloat("r_teammate_beam_alpha");
  settings.teammateBeamRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_beam_r"));
  settings.teammateBeamGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_beam_g"));
  settings.teammateBeamBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_beam_b"));
  settings.teammateRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_r"));
  settings.teammateGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_g"));
  settings.teammateBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_b"));
  settings.teammateAlpha = console.getFloat("r_teammate_alpha");
  settings.teammateOutlineEnabled =
    console.getBool("r_teammate_outline_enable");
  settings.teammateOutlineWidth =
    console.getFloat("r_teammate_outline_width");
  settings.teammateOutlineAlpha =
    console.getFloat("r_teammate_outline_alpha");
  settings.teammateOutlineRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_outline_r"));
  settings.teammateOutlineGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_outline_g"));
  settings.teammateOutlineBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_outline_b"));
  settings.teammateLeanEnabled = console.getBool("r_teammate_lean");
  settings.teammateLeanScale = console.getFloat("r_teammate_lean_scale");

  settings.teammateHealthBarEnabled =
    console.getBool("r_teammate_health_enable");
  settings.teammateHealthBarDamageOnly =
    console.getBool("r_teammate_health_damage_only");
  settings.teammateHealthBarFade =
    console.getBool("r_teammate_health_fade");
  settings.teammateHealthBarVisibleDuration =
    console.getFloat("r_teammate_health_duration");
  settings.teammateHealthBarMaxDistance =
    console.getFloat("r_teammate_health_max_distance");
  settings.teammateHealthBarWidth =
    console.getFloat("r_teammate_health_width");
  settings.teammateHealthBarHeight =
    console.getFloat("r_teammate_health_height");
  settings.teammateHealthBarWorldOffsetZ =
    console.getFloat("r_teammate_health_offset_z");
  settings.teammateHealthBarScreenOffsetX =
    console.getFloat("r_teammate_health_offset_x");
  settings.teammateHealthBarScreenOffsetY =
    console.getFloat("r_teammate_health_offset_y");
  settings.teammateHealthBarAlpha =
    console.getFloat("r_teammate_health_alpha");
  settings.teammateHealthBarRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_health_r"));
  settings.teammateHealthBarGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_health_g"));
  settings.teammateHealthBarBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_health_b"));
  settings.enemyNameTagEnabled = console.getBool("r_enemy_name_enable");
  settings.enemyNameTagAlpha = console.getFloat("r_enemy_name_alpha");
  settings.enemyNameTagScale = console.getFloat("r_enemy_name_font_size");
  settings.enemyNameTagWorldOffsetZ = console.getFloat("r_enemy_name_offset_z");
  settings.enemyNameTagScreenOffsetX = console.getFloat("r_enemy_name_offset_x");
  settings.enemyNameTagScreenOffsetY = console.getFloat("r_enemy_name_offset_y");
  settings.enemyNameTagMaxDistance = console.getFloat("r_enemy_name_max_distance");
  settings.enemyNameTagRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_name_r"));
  settings.enemyNameTagGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_name_g"));
  settings.enemyNameTagBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_name_b"));
  settings.teammateNameTagEnabled = console.getBool("r_teammate_name_enable");
  settings.teammateNameTagAlpha = console.getFloat("r_teammate_name_alpha");
  settings.teammateNameTagScale = console.getFloat("r_teammate_name_font_size");
  settings.teammateNameTagWorldOffsetZ =
    console.getFloat("r_teammate_name_offset_z");
  settings.teammateNameTagScreenOffsetX =
    console.getFloat("r_teammate_name_offset_x");
  settings.teammateNameTagScreenOffsetY =
    console.getFloat("r_teammate_name_offset_y");
  settings.teammateNameTagMaxDistance =
    console.getFloat("r_teammate_name_max_distance");
  settings.teammateNameTagRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_name_r"));
  settings.teammateNameTagGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_name_g"));
  settings.teammateNameTagBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_name_b"));
  settings.showLagCompensation = console.getBool("cl_show_lagcomp");
  return settings;
}

float zoomSensitivityMultiplier(
  float baseFieldOfView,
  float zoomFieldOfView,
  float manualMultiplier
) {
  if (manualMultiplier > 0.0F) {
    return manualMultiplier;
  }

  constexpr float sensRatio = 1.0F;
  const float baseHalfAngle = baseFieldOfView * 0.5F * kDegreesToRadians;
  const float zoomHalfAngle = zoomFieldOfView * 0.5F * kDegreesToRadians;
  const float baseTangent = std::tan(baseHalfAngle);
  if (std::fabs(baseTangent) <= 0.0001F) {
    return 1.0F;
  }
  return (1.0F / sensRatio) * (std::tan(zoomHalfAngle) / baseTangent);
}

MovementTuning movementTuning(const ConsoleSystem& console) {
  MovementTuning tuning;
  tuning.flightEnabled = console.getBool("g_flight");
  tuning.groundAcceleration = console.getFloat("g_accel");
  tuning.airAcceleration = console.getFloat("g_airaccel");
  tuning.airControlEnabled = console.getBool("g_aircontrol");
  tuning.groundFriction = console.getFloat("g_friction");
  tuning.stopSpeed = console.getFloat("g_stopspeed");
  tuning.maxGroundSpeed = console.getFloat("g_maxspeed");
  tuning.maxAirSpeed = tuning.maxGroundSpeed;
  tuning.flightAcceleration = console.getFloat("g_flightaccel");
  tuning.maxFlightSpeed = console.getFloat("g_flightmaxspeed");
  tuning.flightDamping = console.getFloat("g_flightdamping");
  tuning.flightGravityCancel = 1.0F;
  return tuning;
}

bool sameRuntimeMovementTuning(
  const MovementTuning& lhs,
  const MovementTuning& rhs
) {
  return lhs.flightEnabled == rhs.flightEnabled &&
    lhs.groundAcceleration == rhs.groundAcceleration &&
    lhs.airAcceleration == rhs.airAcceleration &&
    lhs.airControlEnabled == rhs.airControlEnabled &&
    lhs.groundFriction == rhs.groundFriction &&
    lhs.stopSpeed == rhs.stopSpeed &&
    lhs.maxGroundSpeed == rhs.maxGroundSpeed &&
    lhs.flightAcceleration == rhs.flightAcceleration &&
    lhs.maxFlightSpeed == rhs.maxFlightSpeed &&
    lhs.flightDamping == rhs.flightDamping &&
    lhs.flightGravityCancel == rhs.flightGravityCancel;
}

ConsoleRenderState consoleRenderState(const ClientConsoleState& state) {
  ConsoleRenderState renderState;
  renderState.open = state.open;
  renderState.input = state.input;
  renderState.cursorIndex = state.cursorIndex;
  renderState.lines.assign(state.output.begin(), state.output.end());
  renderState.hasSelection = state.hasSelection;
  renderState.selectionAnchor = state.selectionAnchor;
  renderState.selectionFocus = state.selectionFocus;
  return renderState;
}

void clearConsoleSelection(ClientConsoleState& state) {
  state.hasSelection = false;
  state.selecting = false;
  state.selectionAnchor = 0;
  state.selectionFocus = 0;
}

ConsoleTextLayout consoleLayoutForWindow(
  SDL_Window* window,
  const ClientConsoleState& state
) {
  int viewportWidth = 0;
  int viewportHeight = 0;
  SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);
  return buildConsoleTextLayout(
    viewportWidth,
    viewportHeight,
    consoleRenderState(state)
  );
}

std::string consoleClipboardTextForWindow(
  SDL_Window* window,
  const ClientConsoleState& state
) {
  if (state.hasSelection && state.selectionAnchor != state.selectionFocus) {
    return consoleSelectedText(
      consoleLayoutForWindow(window, state),
      state.selectionAnchor,
      state.selectionFocus
    );
  }
  return consoleInputClipboardText(state.input);
}

HudRenderState chatHudRenderState(const ClientChatState& state) {
  HudRenderState hud;
  hud.chatInputOpen = state.inputOpen;
  hud.chatInput = state.input;
  hud.chatCursorIndex = state.cursorIndex;
  hud.chatHasSelection = hasSelection(state.selection);
  hud.chatSelectionAnchor = state.selection.anchor;
  hud.chatSelectionFocus = state.selection.focus;
  return hud;
}

ChatTextLayout chatLayoutForWindow(SDL_Window* window, const ClientChatState& state) {
  int viewportWidth = 0;
  int viewportHeight = 0;
  SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);
  return buildChatTextLayout(viewportWidth, viewportHeight, chatHudRenderState(state));
}

void clearChatSelection(ClientChatState& state) {
  clearSelection(state.selection);
  state.selecting = false;
}

std::string chatClipboardText(const ClientChatState& state) {
  if (hasSelection(state.selection)) {
    return selectedText(state.input, state.selection);
  }
  return state.input;
}

void pasteClipboardTextIntoChat(ClientChatState& state) {
  char* clipboardText = SDL_GetClipboardText();
  if (clipboardText == nullptr) {
    return;
  }
  replaceSelectionOrInsert(
    state.input,
    state.cursorIndex,
    state.selection,
    clipboardText,
    TextInputFilter::Chat,
    kMaxChatMessageBytes
  );
  SDL_free(clipboardText);
}

void beginChatSelection(
  SDL_Window* window,
  ClientChatState& state,
  float x,
  float y
) {
  const ChatTextLayout layout = chatLayoutForWindow(window, state);
  const std::size_t offset = chatInputOffsetAt(layout, state.input, x, y);
  state.cursorIndex = offset;
  state.selection.active = true;
  state.selection.anchor = offset;
  state.selection.focus = offset;
  state.selecting = true;
}

void updateChatSelection(
  SDL_Window* window,
  ClientChatState& state,
  float x,
  float y
) {
  if (!state.selecting) {
    return;
  }
  const ChatTextLayout layout = chatLayoutForWindow(window, state);
  state.selection.focus = chatInputOffsetAt(layout, state.input, x, y);
  state.cursorIndex = state.selection.focus;
}

void beginConsoleSelection(
  SDL_Window* window,
  ClientConsoleState& state,
  float x,
  float y
) {
  const ConsoleTextLayout layout = consoleLayoutForWindow(window, state);
  const std::size_t offset = consoleTextOffsetAt(layout, x, y);
  state.hasSelection = true;
  state.selecting = true;
  state.selectionAnchor = offset;
  state.selectionFocus = offset;
}

void updateConsoleSelection(
  SDL_Window* window,
  ClientConsoleState& state,
  float x,
  float y
) {
  if (!state.selecting) {
    return;
  }
  state.selectionFocus =
    consoleTextOffsetAt(consoleLayoutForWindow(window, state), x, y);
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

std::string mouseButtonName(Uint8 button) {
  switch (button) {
  case SDL_BUTTON_LEFT:
    return "mouse1";
  case SDL_BUTTON_RIGHT:
    return "mouse2";
  case SDL_BUTTON_MIDDLE:
    return "mouse3";
  default:
    return "mouse" + std::to_string(static_cast<unsigned int>(button));
  }
}

bool isConsoleToggleText(std::string_view text) {
  return text == "\xC2\xA7" || text == "`" || text == "~";
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
  (void)bindings.bind("mouse2", "+zoom");
  (void)bindings.bind("1", "weapon mg");
  (void)bindings.bind("2", "weapon sg");
  (void)bindings.bind("3", "weapon gl");
  (void)bindings.bind("4", "weapon rl");
  (void)bindings.bind("5", "weapon lg");
  (void)bindings.bind("6", "weapon rg");
  (void)bindings.bind("7", "weapon pg");
  (void)bindings.bind("q", "weapon rl");
  (void)bindings.bind("e", "weapon lg");
  (void)bindings.bind("r", "weapon rg");
  (void)bindings.bind("f5", "resetmatch");
  (void)bindings.bind("f3", "ready");
  (void)bindings.bind("t", "messagemode");
  (void)bindings.bind("z", "showchat");
  (void)bindings.bind("tab", "+scores");
  (void)bindings.bind("escape", "quit");
}

std::string gameModeName(GameMode gameMode) {
  switch (gameMode) {
  case GameMode::Duel:
    return "DUEL";
  case GameMode::ClanArena:
    return "CLAN ARENA";
  }
  return "UNKNOWN";
}

std::string teamName(Team team) {
  switch (team) {
  case Team::None:
    return "NONE";
  case Team::Red:
    return "RED";
  case Team::Blue:
    return "BLUE";
  }
  return "UNKNOWN";
}

std::string aliveCountLine(const ServerSnapshot& snapshot) {
  std::uint32_t redAlive = 0;
  std::uint32_t blueAlive = 0;
  for (std::size_t index = 0; index < snapshot.players.size(); ++index) {
    if (!snapshot.connectedPlayers[index] || snapshot.players[index].health <= 0) {
      continue;
    }
    if (snapshot.teams[index] == Team::Red) {
      ++redAlive;
    } else if (snapshot.teams[index] == Team::Blue) {
      ++blueAlive;
    }
  }
  return "ALIVE " + std::to_string(redAlive) + 'v' + std::to_string(blueAlive);
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

HudRenderState buildHud(const ClientSession& session, bool showAliveCounts) {
  HudRenderState hud;
  hud.centerLines.push_back(session.statusMessage());
  if (!session.readyForPlay()) {
    return hud;
  }

  const ClientGame& client = *session.game();
  const ServerSnapshot& snapshot = client.snapshot();
  const std::size_t localPlayerIndex = session.playerIndex();
  const std::size_t remotePlayerIndex =
    opponentPlayerIndex(snapshot, localPlayerIndex);
  const std::size_t connectedCount = static_cast<std::size_t>(std::count(
    snapshot.connectedPlayers.begin(),
    snapshot.connectedPlayers.end(),
    true
  ));

  hud.healthAmount = snapshot.healthAmount;
  hud.centerLines.clear();
  hud.bottomCenterLines.push_back(
    "HEALTH " + std::to_string(snapshot.players[localPlayerIndex].health)
  );
  hud.topLeftLines.push_back(
    "PLAYERS " + std::to_string(connectedCount) + '/' +
    std::to_string(kDuelPlayerCount)
  );
  if (snapshot.matchPhase != MatchPhase::Live) {
    hud.topLeftLines.push_back("MODE " + gameModeName(snapshot.gameMode));
    if (snapshot.gameMode == GameMode::ClanArena) {
      hud.topLeftLines.push_back(
        "TEAM " + teamName(snapshot.teams[localPlayerIndex])
      );
    }
  }
  if (showAliveCounts && snapshot.gameMode == GameMode::ClanArena) {
    hud.topRightLines.push_back(aliveCountLine(snapshot));
  }
  hud.topRightLines.push_back(hudScoreLine(snapshot, localPlayerIndex));
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
  if (snapshot.matchRules.showOpponentHealth && remotePlayerIndex != localPlayerIndex) {
    hud.showOpponentHealthBar = true;
  }

  hud.centerLines.push_back(matchPhaseName(snapshot.matchPhase));
  hud.centerOffsetY = matchPhaseMessageOffsetY(snapshot.matchPhase);
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
    hud.countdownText = std::to_string(seconds);
    hud.countdownPulse =
      1.0F - (
        static_cast<float>((snapshot.phaseTicksRemaining - 1U) % 125U) /
        124.0F
      );
    hud.centerLines.push_back("MOVE ENABLED - WEAPONS LOCKED");
    break;
  }
  case MatchPhase::RoundEnd:
    hud.centerLines.push_back(
      localPlayerWonResult(snapshot, localPlayerIndex, false)
        ? "ROUND WON"
        : "ROUND LOST"
    );
    hud.centerLines.push_back(
      roundStatsLine("YOU", snapshot.roundCombatStats[localPlayerIndex])
    );
    if (remotePlayerIndex != localPlayerIndex) {
      hud.centerLines.push_back(
        playerRoundStatsLine(snapshot, remotePlayerIndex)
      );
    }
    break;
  case MatchPhase::MatchEnd:
    hud.centerLines.push_back(
      localPlayerWonResult(snapshot, localPlayerIndex, true)
        ? "MATCH WON"
        : "MATCH LOST"
    );
    hud.centerLines.push_back(
      roundStatsLine("YOU", snapshot.roundCombatStats[localPlayerIndex])
    );
    if (remotePlayerIndex != localPlayerIndex) {
      hud.centerLines.push_back(
        playerRoundStatsLine(snapshot, remotePlayerIndex)
      );
    }
    break;
  case MatchPhase::Live:
    hud.centerLines.clear();
    break;
  }
  return hud;
}

[[nodiscard]] float absolute2DYaw(
  const LocalInputState& input,
  const PlayerState& player,
  int viewportWidth,
  int viewportHeight,
  float fieldOfView,
  float cameraZoom
) {
  if (!input.hasMousePosition || viewportWidth <= 0 || viewportHeight <= 0) {
    return player.viewYawRadians;
  }

  constexpr float margin = 40.0F;
  const float arenaSize =
    static_cast<float>(std::min(viewportWidth, viewportHeight)) - (margin * 2.0F);
  if (arenaSize <= 1.0F) {
    return player.viewYawRadians;
  }

  const float arenaLeft = (static_cast<float>(viewportWidth) - arenaSize) * 0.5F;
  const float arenaTop = (static_cast<float>(viewportHeight) - arenaSize) * 0.5F;
  const float worldHalfExtent =
    10.0F * (fieldOfView / 90.0F) / cameraZoom;
  const float viewX =
    (((input.mouseX - arenaLeft) / arenaSize) * 2.0F - 1.0F) * worldHalfExtent;
  const float viewY =
    (1.0F - ((input.mouseY - arenaTop) / arenaSize) * 2.0F) * worldHalfExtent;

  const Vec3 aimOffset{viewX, viewY, 0.0F};
  if ((aimOffset.x * aimOffset.x + aimOffset.y * aimOffset.y) <= 0.0001F) {
    return player.viewYawRadians;
  }
  return std::atan2(aimOffset.y, aimOffset.x);
}

[[nodiscard]] UserCommand buildCommand(
  const LocalInputState& input,
  const PlayerState& player,
  std::uint32_t sequence,
  std::uint32_t clientTick,
  float sensitivity,
  AimMode aimMode,
  int viewportWidth,
  int viewportHeight,
  float fieldOfView,
  float cameraZoom,
  int renderMode,
  Weapon weapon
) {
  UserCommand command;
  command.sequence = sequence;
  command.clientTick = clientTick;
  const bool perspective = renderMode == 1;
  const AimMode effectiveAimMode =
    perspective ? AimMode::Relative3D : aimMode;
  if (effectiveAimMode == AimMode::Relative3D) {
    command.viewYawRadians = relativeMouseYaw(
      player.viewYawRadians,
      input.mouseDeltaX,
      sensitivity
    );
    command.viewPitchRadians = perspective
      ? clamp(
          player.viewPitchRadians -
            (input.mouseDeltaY * kBaseMouseSensitivityRadians * sensitivity),
          -kMaxPitchRadians,
          kMaxPitchRadians
        )
      : 0.0F;
  } else {
    command.viewYawRadians = absolute2DYaw(
      input,
      player,
      viewportWidth,
      viewportHeight,
      fieldOfView,
      cameraZoom
    );
    command.viewPitchRadians = 0.0F;
  }
  command.planarAim = !perspective;

  command.forwardMove = (input.forward > 0 ? 1.0F : 0.0F) - (input.back > 0 ? 1.0F : 0.0F);
  command.rightMove = (input.right > 0 ? 1.0F : 0.0F) - (input.left > 0 ? 1.0F : 0.0F);
  command.upMove = (input.up > 0 ? 1.0F : 0.0F) - (input.down > 0 ? 1.0F : 0.0F);
  command.jump = input.up > 0;
  command.attack = input.attack > 0;
  command.weapon = weapon;
  return command;
}

[[nodiscard]] UserCommand buildCommandWithViewAngles(
  const LocalInputState& input,
  std::uint32_t sequence,
  std::uint32_t clientTick,
  float yawRadians,
  float pitchRadians,
  bool planarAim,
  Weapon weapon
) {
  UserCommand command;
  command.sequence = sequence;
  command.clientTick = clientTick;
  command.viewYawRadians = yawRadians;
  command.viewPitchRadians = pitchRadians;
  command.planarAim = planarAim;
  command.forwardMove = (input.forward > 0 ? 1.0F : 0.0F) - (input.back > 0 ? 1.0F : 0.0F);
  command.rightMove = (input.right > 0 ? 1.0F : 0.0F) - (input.left > 0 ? 1.0F : 0.0F);
  command.upMove = (input.up > 0 ? 1.0F : 0.0F) - (input.down > 0 ? 1.0F : 0.0F);
  command.jump = input.up > 0;
  command.attack = input.attack > 0;
  command.weapon = weapon;
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
  const bool audioSubsystemAvailable = SDL_InitSubSystem(SDL_INIT_AUDIO);

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
  std::cout << "Renderer backend: " << renderer.backendName() << '\n';
  const char* executableBasePath = SDL_GetBasePath();
  const std::filesystem::path assetBasePath =
    executableBasePath != nullptr ? executableBasePath : std::filesystem::current_path();
  ClientAudio audio;
  const bool audioAvailable =
    audioSubsystemAvailable && audio.initialize(assetBasePath);

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
  bool openChatRequested = false;
  bool showChatRequested = false;
  bool requestGameModePending = false;
  bool requestTeamPending = false;
  GameMode requestedGameMode = GameMode::Duel;
  Team requestedTeam = Team::None;
  int scoreboardPressCount = 0;
  int zoomPressCount = 0;
  Weapon selectedWeapon = Weapon::LightningGun;
  Weapon viewWeapon = Weapon::LightningGun;
  Weapon previousViewWeapon = Weapon::LightningGun;
  float weaponSwitchSeconds = 1.0F;
  bool botDodgeEnabled = false;
  std::int32_t botDodgeMinIntervalMs = 250;
  std::int32_t botDodgeMaxIntervalMs = 750;
  std::string pendingPlayerName;
  std::string lastSentPlayerName;
  std::string pendingMapName;
  ClientChatState chatState;
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
  registerButtonCommand("scores", scoreboardPressCount);
  registerButtonCommand("zoom", zoomPressCount);

  console.registerCommand(
    "weapon",
    "Select weapon: weapon <mg|sg|gl|rl|lg|rg|pg|1..7>.",
    [&selectedWeapon](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: weapon <mg|sg|gl|rl|lg|rg|pg|1..7>");
      }
      const std::optional<Weapon> parsed = parseWeaponToken(arguments[1]);
      if (parsed.has_value()) {
        selectedWeapon = *parsed;
        return std::string("weapon = ") + std::string(weaponShortName(*parsed));
      }
      return std::string("usage: weapon <mg|sg|gl|rl|lg|rg|pg|1..7>");
    }
  );
  console.registerCommand(
    "bot_dodge",
    "Toggle BOT random left/right movement: bot_dodge [0|1] [min_ms max_ms].",
    [&botDodgeEnabled, &botDodgeMinIntervalMs, &botDodgeMaxIntervalMs](
      const std::vector<std::string>& arguments
    ) {
      auto parseInt = [](const std::string& text, int& value) {
        const auto result =
          std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} &&
          result.ptr == text.data() + text.size();
      };

      bool enabled = !botDodgeEnabled;
      std::size_t intervalArgument = 1;
      if (arguments.size() >= 2) {
        if (
          arguments[1] == "1" ||
          arguments[1] == "on" ||
          arguments[1] == "true"
        ) {
          enabled = true;
          intervalArgument = 2;
        } else if (
          arguments[1] == "0" ||
          arguments[1] == "off" ||
          arguments[1] == "false"
        ) {
          enabled = false;
          intervalArgument = 2;
        }
      }

      int minMs = botDodgeMinIntervalMs;
      int maxMs = botDodgeMaxIntervalMs;
      if (arguments.size() > intervalArgument) {
        if (arguments.size() != intervalArgument + 2) {
          return std::string("usage: bot_dodge [0|1] [min_ms max_ms]");
        }
        if (
          !parseInt(arguments[intervalArgument], minMs) ||
          !parseInt(arguments[intervalArgument + 1], maxMs)
        ) {
          return std::string("usage: bot_dodge [0|1] [min_ms max_ms]");
        }
      }
      minMs = std::clamp(minMs, 1, 10000);
      maxMs = std::clamp(maxMs, 1, 10000);
      if (minMs > maxMs) {
        std::swap(minMs, maxMs);
      }
      botDodgeEnabled = enabled;
      botDodgeMinIntervalMs = minMs;
      botDodgeMaxIntervalMs = maxMs;
      return std::string("bot_dodge = ") + (botDodgeEnabled ? "1" : "0") +
        " (" + std::to_string(botDodgeMinIntervalMs) + "-" +
        std::to_string(botDodgeMaxIntervalMs) + " ms)";
    }
  );
  console.registerCommand(
    "player",
    "Set your player name: player <name>.",
    [&console, &pendingPlayerName](const std::vector<std::string>& arguments) {
      if (arguments.size() < 2) {
        return std::string("usage: player <name>");
      }
      std::string name = arguments[1];
      for (std::size_t index = 2; index < arguments.size(); ++index) {
        name += ' ' + arguments[index];
      }
      if (name.size() > kMaxPlayerNameBytes) {
        return "player name is limited to " +
          std::to_string(kMaxPlayerNameBytes) + " characters";
      }
      const std::string result = console.execute("set cl_player_name \"" + name + '"');
      if (!result.starts_with("cl_player_name = ")) {
        return result;
      }
      pendingPlayerName = name;
      return "name = " + pendingPlayerName;
    }
  );
  console.registerCommand(
    "map",
    "Request a server map change: map <name> loads maps/<name>.lgmap.",
    [&pendingMapName](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: map <name>");
      }
      const std::string& name = arguments[1];
      if (name.empty() || name.size() > kMaxMapNameBytes) {
        return "map name is limited to " +
          std::to_string(kMaxMapNameBytes) + " characters";
      }
      for (const unsigned char character : name) {
        if (
          !std::isalnum(character) &&
          character != '_' &&
          character != '-'
        ) {
          return std::string("map name may only use letters, numbers, _ and -");
        }
      }
      pendingMapName = name;
      return "map change requested: " + pendingMapName;
    }
  );
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
    "gamemode",
    "Select the active gamemode: gamemode <duel|ca|clanarena>.",
    [&requestGameModePending, &requestedGameMode](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: gamemode <duel|ca|clanarena>");
      }
      std::string value = arguments[1];
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (value == "duel") {
        requestedGameMode = GameMode::Duel;
      } else if (value == "ca" || value == "clanarena" || value == "clan_arena") {
        requestedGameMode = GameMode::ClanArena;
      } else {
        return std::string("usage: gamemode <duel|ca|clanarena>");
      }
      requestGameModePending = true;
      return std::string("gamemode = ") + gameModeName(requestedGameMode);
    }
  );
  console.registerCommand(
    "team",
    "Select your Clan Arena team: team <red|blue|none>.",
    [&requestTeamPending, &requestedTeam](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: team <red|blue|none>");
      }
      std::string value = arguments[1];
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (value == "red") {
        requestedTeam = Team::Red;
      } else if (value == "blue") {
        requestedTeam = Team::Blue;
      } else if (value == "none" || value == "unassigned") {
        requestedTeam = Team::None;
      } else {
        return std::string("usage: team <red|blue|none>");
      }
      requestTeamPending = true;
      return std::string("team = ") + teamName(requestedTeam);
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
    "messagemode",
    "Open team-wide chat input.",
    [&openChatRequested](const std::vector<std::string>&) {
      openChatRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "showchat",
    "Show chat history for five seconds.",
    [&showChatRequested](const std::vector<std::string>&) {
      showChatRequested = true;
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
        "+scores\n"
        "+zoom\n"
        "weapon\n"
        "map\n"
        "player\n"
        "resetmatch\n"
        "ready\n"
        "gamemode\n"
        "team\n"

        "messagemode\n"
        "showchat\n"
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
  loadSoundMixerConfigs(console, assetBasePath);
  if (console.getInt("cl_config_version") < 7) {
    (void)bindings.bind("f3", "ready");
    (void)bindings.bind("t", "messagemode");
    (void)bindings.bind("z", "showchat");
    (void)bindings.bind("tab", "+scores");
    if (bindings.binding("mouse2").empty()) {
      (void)bindings.bind("mouse2", "+zoom");
    }
    (void)bindings.bind("1", "weapon mg");
    (void)bindings.bind("2", "weapon sg");
    (void)bindings.bind("3", "weapon gl");
    (void)bindings.bind("4", "weapon rl");
    (void)bindings.bind("5", "weapon lg");
    (void)bindings.bind("6", "weapon rg");
    (void)bindings.bind("7", "weapon pg");
    (void)bindings.bind("q", "weapon rl");
    (void)bindings.bind("e", "weapon lg");
    (void)bindings.bind("r", "weapon rg");
    if (bindings.binding("f5").empty()) {
      (void)bindings.bind("f5", "resetmatch");
    }
    (void)console.execute("set cl_config_version 7");
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
      clearConsoleSelection(consoleState);
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
  const auto setChatOpen =
    [&bindings, &console, &chatState, &input, window](bool open) {
      if (chatState.inputOpen == open) {
        return;
      }
      for (const std::string& command : bindings.releaseAll()) {
        (void)console.execute(command);
      }
      chatState.inputOpen = open;
      if (open) {
        chatState.cursorIndex = chatState.input.size();
        clearChatSelection(chatState);
      }
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

  const Arena fallbackArena = thunderstruckArena();
  std::uint32_t commandSequence = 0;
  std::uint32_t clientTick = 0;

  using Clock = std::chrono::steady_clock;
  auto previousTime = Clock::now();
  auto previousOuterFrameStart = previousTime;
  float accumulatorSeconds = 0.0F;
  float titleAccumulatorSeconds = 0.0F;
  float frameStatsAccumulatorSeconds = 0.0F;
  constexpr float kWeaponSwitchDurationSeconds = 0.16F;
  float droppedSimulationSeconds = 0.0F;
  std::uint32_t overloadFrameCount = 0;
  std::uint32_t renderedFrameCount = 0;
  float displayedFramesPerSecond = 0.0F;
  FrameTimeHistory outerFrameTimes;
  FrameTimeSummary displayedFrameTimes;
  PresentationViewState presentationView;
  ClientGame* presentationViewGame = nullptr;
  bool previousFrameUsedPresentationView = false;
  MovementTuning lastRequestedMovementTuning = movementTuning(console);
  float lastRequestedPlayerSizeScaleXY =
    console.getFloat("g_playersize_xy");
  float lastRequestedPlayerSizeScaleZ =
    console.getFloat("g_playersize_z");
  float lastRequestedLightningKnockback =
    console.getFloat("g_lg_knockback");
  float lastRequestedLightningFireHz =
    console.getFloat("g_lg_fire_hz");
  float lastRequestedRocketKnockback =
    console.getFloat("g_rl_knockback");
  WeaponDamageTuning lastRequestedWeaponDamage =
    weaponDamageTuning(console);
  float lastRequestedVampirism =
    console.getFloat("g_vampirism");
  std::uint8_t lastRequestedSelfDamagePercent =
    selfDamagePercent(console);
  std::int32_t lastRequestedHealthAmount =
    healthAmount(console);
  bool lastRequestedBotDodgeEnabled = botDodgeEnabled;
  std::int32_t lastRequestedBotDodgeMinIntervalMs = botDodgeMinIntervalMs;
  std::int32_t lastRequestedBotDodgeMaxIntervalMs = botDodgeMaxIntervalMs;
  bool movementTuningRequestPending = true;
  bool relativeMouseModeEnabled = true;
  const ClientGame* audioGame = nullptr;
  std::uint32_t lastAudioServerTick = 0;
  std::uint32_t lastHitSoundServerTick = 0;
  constexpr std::uint32_t kTransientAudioEventTicks = 8;
  std::array<WeaponFireResult, kDuelPlayerCount> lastPlayedWeaponFires = {};
  std::array<std::uint32_t, kDuelPlayerCount> lastPlayedWeaponFireAudioTicks = {};
  std::array<bool, kDuelPlayerCount> hasLastPlayedWeaponFire = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> lastPlayedRocketExplosions = {};
  std::array<std::uint32_t, kDuelPlayerCount> lastPlayedRocketExplosionAudioTicks = {};
  std::array<bool, kDuelPlayerCount> hasLastPlayedRocketExplosion = {};
  std::array<FragEvent, kDuelPlayerCount> lastPlayedFragEvents = {};
  std::array<std::uint32_t, kDuelPlayerCount> lastPlayedFragAudioTicks = {};
  std::array<bool, kDuelPlayerCount> hasLastPlayedFragEvent = {};
  DamageNumberState damageNumberState;
  std::uint32_t lastDamageNumberServerTick = 0;
  std::array<std::uint32_t, kDuelPlayerCount> lastDamageNumberFeedbackSequences = {};
  std::array<bool, kDuelPlayerCount> hasLastDamageNumberFeedbackSequence = {};
  bool damageNumberStateInitialized = false;
  std::array<std::uint32_t, kDuelPlayerCount> lastPlayedFootstepAudioSequences = {};
  std::array<std::uint32_t, kMaxRocketProjectiles> lastPlayedGrenadeBounceAudioSequences = {};
  std::uint32_t lastLocalRailFireTick = 0;
  bool hasLocalRailFireTick = false;
  bool localRailReadySoundPlayed = true;
  MatchPhase lastAudioMatchPhase = MatchPhase::WaitingForPlayers;
  std::uint32_t lastAudioCountdownSecond = 0;
  std::array<int, kDuelPlayerCount> lastAudioPlayerHealth = {};
  bool previousLocalHit = false;
  bool audioStateInitialized = false;
  bool hasLocalPlayerAliveState = false;
  bool wasLocalPlayerAlive = false;
  bool hasEnemyHitTime = false;
  Clock::time_point lastEnemyHitTime = {};
  std::array<bool, kDuelPlayerCount> hasEnemyHitTimeByTarget = {};
  std::array<Clock::time_point, kDuelPlayerCount> lastEnemyHitTimeByTarget = {};
  bool hasBeamHitTime = false;
  Clock::time_point lastBeamHitTime = {};
  LocalHitFeedbackDedupeState localHitFeedbackDedupe;
  std::array<int, kDuelPlayerCount> lastRemoteHealth = {};
  std::array<bool, kDuelPlayerCount> hasLastRemoteHealth = {};
  std::array<Clock::time_point, kDuelPlayerCount> lastRemoteDamageTime = {};
  std::array<bool, kDuelPlayerCount> hasLastRemoteDamageTime = {};
  std::array<LingeringRailBeam, kDuelPlayerCount> lingeringRailBeams = {};
  std::array<FootstepAudioState, kDuelPlayerCount> footstepAudioStates = {};

  while (running) {
    const auto outerFrameStart = Clock::now();
    const auto outerFrameElapsed =
      std::chrono::duration<float>(outerFrameStart - previousOuterFrameStart);
    previousOuterFrameStart = outerFrameStart;
    outerFrameTimes.push(outerFrameElapsed.count() * 1000.0F);
    frameStatsAccumulatorSeconds += outerFrameElapsed.count();
    if (frameStatsAccumulatorSeconds >= 0.25F) {
      displayedFrameTimes = outerFrameTimes.summarize();
      frameStatsAccumulatorSeconds = 0.0F;
    }

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
        if (chatState.inputOpen) {
          if (!pressed) {
            (void)bindings.handleKey(key, false);
            break;
          }
          if ((event.key.key == SDLK_A) && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0) {
            selectAll(chatState.input, chatState.selection);
            chatState.cursorIndex = chatState.input.size();
          } else if (isClipboardPasteKey(event.key)) {
            pasteClipboardTextIntoChat(chatState);
          } else if (isClipboardCopyKey(event.key)) {
            copyTextToClipboard(chatClipboardText(chatState));
          } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            chatState.input.clear();
            chatState.cursorIndex = 0U;
            clearChatSelection(chatState);
            setChatOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
            backspaceSelectionOrText(
              chatState.input,
              chatState.cursorIndex,
              chatState.selection
            );
          } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
            clearChatSelection(chatState);
            moveCursorLeft(chatState.input, chatState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
            clearChatSelection(chatState);
            moveCursorRight(chatState.input, chatState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (!chatState.input.empty()) {
              chatState.pendingMessage = chatState.input;
            }
            chatState.input.clear();
            chatState.cursorIndex = 0U;
            clearChatSelection(chatState);
            setChatOpen(false);
          }
          break;
        }
        if (consoleState.open) {
          if (!pressed) {
            if (bindings.binding(key) == "toggleconsole") {
              suppressNextTextInput = false;
            }
            executeBindingCommands(bindings.handleKey(key, false));
            break;
          }
          if (bindings.binding(key) == "toggleconsole") {
            suppressNextTextInput = true;
            executeBindingCommands(bindings.handleKey(key, true));
            applyConsoleToggle();
            break;
          }
          if (isClipboardPasteKey(event.key)) {
            clearConsoleSelection(consoleState);
            pasteClipboardTextIntoConsole(consoleState.input, consoleState.cursorIndex);
          } else if (isClipboardCopyKey(event.key)) {
            copyTextToClipboard(consoleClipboardTextForWindow(window, consoleState));
          } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            setConsoleOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
            if (!consoleState.input.empty()) {
              clearConsoleSelection(consoleState);
              backspaceConsoleInput(consoleState.input, consoleState.cursorIndex);
            }
          } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
            clearConsoleSelection(consoleState);
            moveConsoleCursorLeft(consoleState.input, consoleState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
            clearConsoleSelection(consoleState);
            moveConsoleCursorRight(consoleState.input, consoleState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (!consoleState.input.empty()) {
              clearConsoleSelection(consoleState);
              appendConsoleOutput(consoleState, "] " + consoleState.input);
              const std::string result = console.execute(consoleState.input);
              if (!result.empty()) {
                appendConsoleOutput(consoleState, result);
              }
              applyConsoleToggle();
              consoleState.history.push_back(consoleState.input);
              consoleState.historyIndex = consoleState.history.size();
              consoleState.input.clear();
              consoleState.cursorIndex = 0U;
            }
          } else if (event.key.scancode == SDL_SCANCODE_UP && !consoleState.history.empty()) {
            if (consoleState.historyIndex > 0) {
              --consoleState.historyIndex;
            }
            clearConsoleSelection(consoleState);
            consoleState.input = consoleState.history[consoleState.historyIndex];
            consoleState.cursorIndex = consoleState.input.size();
          } else if (event.key.scancode == SDL_SCANCODE_DOWN && !consoleState.history.empty()) {
            if (consoleState.historyIndex + 1 < consoleState.history.size()) {
              ++consoleState.historyIndex;
              clearConsoleSelection(consoleState);
              consoleState.input = consoleState.history[consoleState.historyIndex];
              consoleState.cursorIndex = consoleState.input.size();
            } else {
              consoleState.historyIndex = consoleState.history.size();
              clearConsoleSelection(consoleState);
              consoleState.input.clear();
              consoleState.cursorIndex = 0U;
            }
          } else if (event.key.scancode == SDL_SCANCODE_TAB) {
            const std::string prefix = consoleCompletionPrefix(
              consoleState.input,
              consoleState.cursorIndex
            );
            const std::vector<std::string> matches = console.complete(prefix);
            if (matches.size() == 1) {
              clearConsoleSelection(consoleState);
              replaceConsoleCompletion(
                consoleState.input,
                consoleState.cursorIndex,
                matches[0]
              );
            } else if (!matches.empty()) {
              clearConsoleSelection(consoleState);
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
        if (pressed && bindings.binding(key) == "messagemode") {
          suppressNextTextInput = true;
        }
        executeBindingCommands(bindings.handleKey(key, pressed));
        applyConsoleToggle();
        if (openChatRequested && !consoleState.open) {
          openChatRequested = false;
          setChatOpen(true);
        }
        break;
      }
      case SDL_EVENT_TEXT_INPUT:
        if (
          suppressNextTextInput &&
          (
            isConsoleToggleText(event.text.text) ||
            std::string_view(event.text.text) == "t" ||
            std::string_view(event.text.text) == "T"
          )
        ) {
          suppressNextTextInput = false;
        } else if (consoleState.open) {
          suppressNextTextInput = false;
          clearConsoleSelection(consoleState);
          insertConsoleText(
            consoleState.input,
            consoleState.cursorIndex,
            event.text.text
          );
        } else if (chatState.inputOpen) {
          suppressNextTextInput = false;
          replaceSelectionOrInsert(
            chatState.input,
            chatState.cursorIndex,
            chatState.selection,
            event.text.text,
            TextInputFilter::Chat,
            kMaxChatMessageBytes
          );
        } else {
          suppressNextTextInput = false;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const std::string key = mouseButtonName(event.button.button);
        if (consoleState.open && event.button.button == SDL_BUTTON_LEFT) {
          if (pressed) {
            beginConsoleSelection(
              window,
              consoleState,
              event.button.x,
              event.button.y
            );
          } else {
            updateConsoleSelection(
              window,
              consoleState,
              event.button.x,
              event.button.y
            );
            consoleState.selecting = false;
            if (consoleState.selectionAnchor == consoleState.selectionFocus) {
              clearConsoleSelection(consoleState);
            }
          }
        } else if (chatState.inputOpen && event.button.button == SDL_BUTTON_LEFT) {
          if (pressed) {
            beginChatSelection(
              window,
              chatState,
              event.button.x,
              event.button.y
            );
          } else {
            updateChatSelection(
              window,
              chatState,
              event.button.x,
              event.button.y
            );
            chatState.selecting = false;
            if (chatState.selection.anchor == chatState.selection.focus) {
              clearChatSelection(chatState);
            }
          }
        } else if (!consoleState.open && !chatState.inputOpen) {
          executeBindingCommands(bindings.handleKey(key, pressed));
          applyConsoleToggle();
        } else if (!pressed) {
          executeBindingCommands(bindings.handleKey(key, false));
        }
        break;
      }
      case SDL_EVENT_MOUSE_MOTION:
        if (consoleState.open) {
          updateConsoleSelection(
            window,
            consoleState,
            event.motion.x,
            event.motion.y
          );
        } else if (chatState.inputOpen) {
          updateChatSelection(
            window,
            chatState,
            event.motion.x,
            event.motion.y
          );
        } else {
          input.mouseDeltaX += event.motion.xrel;
          input.mouseDeltaY += event.motion.yrel;
          input.mouseX = event.motion.x;
          input.mouseY = event.motion.y;
          input.hasMousePosition = true;
        }
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        executeBindingCommands(bindings.releaseAll());
        consoleState.selecting = false;
        chatState.selecting = false;
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
    if (showChatRequested) {
      chatState.visibleUntil = Clock::now() + std::chrono::seconds(5);
      showChatRequested = false;
    }
    if (const ClientGame* chatGame = session.game();
        chatGame != nullptr && chatGame->hasSnapshot()) {
      const ServerSnapshot& snapshot = chatGame->snapshot();
      if (
        snapshot.chatSequence != 0U &&
        snapshot.chatSequence != chatState.lastSequence
      ) {
        chatState.lastSequence = snapshot.chatSequence;
        chatState.history.push_back(ClientChatState::Message{
          snapshot.chatPlayerIndex,
          snapshot.chatMessage,
          chatPlayerDisplayName(snapshot, snapshot.chatPlayerIndex),
        });
        while (chatState.history.size() > 8U) {
          chatState.history.pop_front();
        }
        chatState.visibleUntil = Clock::now() + std::chrono::seconds(5);
      }
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
    const bool perspectiveRenderMode = console.getInt("cl_render_mode") == 1;
    const AimMode frameAimMode = perspectiveRenderMode
      ? AimMode::Relative3D
      : aimModeFromInt(console.getInt("cl_aim_mode"));
    const bool usePresentationView =
      perspectiveRenderMode && frameAimMode == AimMode::Relative3D;
    const bool gameInputControlsView =
      usePresentationView && !consoleState.open && !chatState.inputOpen;
    const bool wantsRelativeMouse =
      !consoleState.open && frameAimMode == AimMode::Relative3D;

    if (wantsRelativeMouse != relativeMouseModeEnabled) {
      SDL_SetWindowRelativeMouseMode(window, wantsRelativeMouse);
      relativeMouseModeEnabled = wantsRelativeMouse;
    }
    if (!consoleState.open && frameAimMode == AimMode::Absolute2D) {
      float mouseX = 0.0F;
      float mouseY = 0.0F;
      SDL_GetMouseState(&mouseX, &mouseY);
      input.mouseX = mouseX;
      input.mouseY = mouseY;
      input.hasMousePosition = true;
    }
    ClientGame* currentPresentationGame = session.game();
    if (currentPresentationGame == nullptr) {
      presentationView = {};
      presentationViewGame = nullptr;
      previousFrameUsedPresentationView = usePresentationView;
    } else if (currentPresentationGame != presentationViewGame) {
      presentationView = {};
      presentationViewGame = currentPresentationGame;
    }
    const bool enteredPresentationView =
      usePresentationView && !previousFrameUsedPresentationView;
    if (enteredPresentationView) {
      presentationView.initialized = false;
    }
    if (
      usePresentationView &&
      !presentationView.initialized &&
      currentPresentationGame != nullptr &&
      currentPresentationGame->hasSnapshot()
    ) {
      const PlayerState& predictedPlayer =
        currentPresentationGame->predictedPlayer();
      presentationView.yawRadians = predictedPlayer.viewYawRadians;
      presentationView.pitchRadians = predictedPlayer.viewPitchRadians;
      presentationView.initialized = true;
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }
    if (gameInputControlsView && presentationView.initialized) {
      presentationView.yawRadians = relativeMouseYaw(
        presentationView.yawRadians,
        input.mouseDeltaX,
        console.getFloat("sensitivity") *
          (
            zoomPressCount > 0
              ? zoomSensitivityMultiplier(
                  console.getFloat("cl_fov"),
                  console.getFloat("cl_zoom_fov"),
                  console.getFloat("cl_zoom_sensitivity")
                )
              : 1.0F
          )
      );
      presentationView.pitchRadians = clamp(
        presentationView.pitchRadians -
          (
            input.mouseDeltaY *
            kBaseMouseSensitivityRadians *
            console.getFloat("sensitivity") *
            (
              zoomPressCount > 0
                ? zoomSensitivityMultiplier(
                    console.getFloat("cl_fov"),
                    console.getFloat("cl_zoom_fov"),
                    console.getFloat("cl_zoom_sensitivity")
                  )
                : 1.0F
            )
          ),
        -kMaxPitchRadians,
        kMaxPitchRadians
      );
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }
    previousFrameUsedPresentationView = usePresentationView;
    const MovementTuning currentMovementTuning = movementTuning(console);
    const float currentPlayerSizeScaleXY =
      console.getFloat("g_playersize_xy");
    const float currentPlayerSizeScaleZ =
      console.getFloat("g_playersize_z");
    const float currentLightningKnockback =
      console.getFloat("g_lg_knockback");
    const float currentLightningFireHz =
      console.getFloat("g_lg_fire_hz");
    const float currentRocketKnockback =
      console.getFloat("g_rl_knockback");
    const WeaponDamageTuning currentWeaponDamage =
      weaponDamageTuning(console);
    const float currentVampirism =
      console.getFloat("g_vampirism");
    const std::uint8_t currentSelfDamagePercent =
      selfDamagePercent(console);
    const std::int32_t currentHealthAmount =
      healthAmount(console);
    if (!sameRuntimeMovementTuning(
          currentMovementTuning,
          lastRequestedMovementTuning
        ) ||
        currentPlayerSizeScaleXY != lastRequestedPlayerSizeScaleXY ||
        currentPlayerSizeScaleZ != lastRequestedPlayerSizeScaleZ ||
        currentLightningKnockback != lastRequestedLightningKnockback ||
        currentLightningFireHz != lastRequestedLightningFireHz ||
        currentVampirism != lastRequestedVampirism ||
        currentRocketKnockback != lastRequestedRocketKnockback ||
        currentWeaponDamage.shotgunDamagePerPellet !=
          lastRequestedWeaponDamage.shotgunDamagePerPellet ||
        currentWeaponDamage.machineGunDamage !=
          lastRequestedWeaponDamage.machineGunDamage ||
        currentWeaponDamage.lightningGunDamage !=
          lastRequestedWeaponDamage.lightningGunDamage ||
        currentWeaponDamage.railgunDamage !=
          lastRequestedWeaponDamage.railgunDamage ||
        currentWeaponDamage.rocketLauncherDamage !=
          lastRequestedWeaponDamage.rocketLauncherDamage ||
        currentSelfDamagePercent != lastRequestedSelfDamagePercent ||
        currentHealthAmount != lastRequestedHealthAmount ||
        botDodgeEnabled != lastRequestedBotDodgeEnabled ||
        botDodgeMinIntervalMs != lastRequestedBotDodgeMinIntervalMs ||
        botDodgeMaxIntervalMs != lastRequestedBotDodgeMaxIntervalMs) {
      lastRequestedMovementTuning = currentMovementTuning;
      lastRequestedPlayerSizeScaleXY = currentPlayerSizeScaleXY;
      lastRequestedPlayerSizeScaleZ = currentPlayerSizeScaleZ;
      lastRequestedLightningKnockback = currentLightningKnockback;
      lastRequestedLightningFireHz = currentLightningFireHz;
      lastRequestedVampirism = currentVampirism;
      lastRequestedRocketKnockback = currentRocketKnockback;
      lastRequestedWeaponDamage = currentWeaponDamage;
      lastRequestedSelfDamagePercent = currentSelfDamagePercent;
      lastRequestedHealthAmount = currentHealthAmount;
      lastRequestedBotDodgeEnabled = botDodgeEnabled;
      lastRequestedBotDodgeMinIntervalMs = botDodgeMinIntervalMs;
      lastRequestedBotDodgeMaxIntervalMs = botDodgeMaxIntervalMs;
      movementTuningRequestPending = true;
    }

    const auto now = Clock::now();
    const auto elapsed = std::chrono::duration<float>(now - previousTime);
    previousTime = now;
    titleAccumulatorSeconds += elapsed.count();
    if (selectedWeapon != viewWeapon) {
      previousViewWeapon = viewWeapon;
      viewWeapon = selectedWeapon;
      weaponSwitchSeconds = 0.0F;
    }
    weaponSwitchSeconds = std::min(
      kWeaponSwitchDurationSeconds,
      weaponSwitchSeconds + elapsed.count()
    );

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

      int viewportWidth = 0;
      int viewportHeight = 0;
      SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);
      const AimMode currentAimMode =
        aimModeFromInt(console.getInt("cl_aim_mode"));
      const bool zoomHeld = zoomPressCount > 0;
      const float effectiveFieldOfView = zoomHeld
        ? console.getFloat("cl_zoom_fov")
        : console.getFloat("cl_fov");
      const float zoomSensitivity = zoomSensitivityMultiplier(
        console.getFloat("cl_fov"),
        console.getFloat("cl_zoom_fov"),
        console.getFloat("cl_zoom_sensitivity")
      );
      const float effectiveSensitivity = console.getFloat("sensitivity") *
        (zoomHeld ? zoomSensitivity : 1.0F);

      const UserCommand command =
        usePresentationView && presentationView.initialized
          ? buildCommandWithViewAngles(
              tickInput,
              commandSequence++,
              clientTick++,
              presentationView.yawRadians,
              presentationView.pitchRadians,
              false,
              selectedWeapon
            )
          : buildCommand(
              tickInput,
              predictedPlayer,
              commandSequence++,
              clientTick++,
              effectiveSensitivity,
              currentAimMode,
              viewportWidth,
              viewportHeight,
              effectiveFieldOfView,
              console.getFloat("cl_camera_zoom"),
              perspectiveRenderMode ? 1 : 0,
              selectedWeapon
            );
      std::string playerNameForCommand = std::move(pendingPlayerName);
      const std::string configuredPlayerName = console.getString("cl_player_name");
      if (
        playerNameForCommand.empty() &&
        !configuredPlayerName.empty() &&
        configuredPlayerName != lastSentPlayerName
      ) {
        playerNameForCommand = configuredPlayerName;
      }
      const std::string sentPlayerName = playerNameForCommand;
      session.sendCommand(
        command,
        resetRequested,
        readyRequested,
        movementTuningRequestPending,
        lastRequestedMovementTuning,
        lastRequestedPlayerSizeScaleXY,
        lastRequestedPlayerSizeScaleZ,
        lastRequestedLightningKnockback,
        lastRequestedRocketKnockback,
        lastRequestedVampirism,
        lastRequestedSelfDamagePercent,
        lastRequestedHealthAmount,
        lastRequestedWeaponDamage,
        lastRequestedLightningFireHz,
        lastRequestedBotDodgeEnabled,
        lastRequestedBotDodgeMinIntervalMs,
        lastRequestedBotDodgeMaxIntervalMs,
        std::move(chatState.pendingMessage),
        std::move(playerNameForCommand),
        std::move(pendingMapName),
        console.getInt("cl_interp_mode") != 0,
        requestGameModePending,
        requestedGameMode,
        requestTeamPending,
        requestedTeam
      );
      if (!sentPlayerName.empty()) {
        lastSentPlayerName = sentPlayerName;
      }
      chatState.pendingMessage.clear();
      pendingPlayerName.clear();
      pendingMapName.clear();
      resetRequested = false;
      readyRequested = false;
      requestGameModePending = false;
      requestTeamPending = false;
      movementTuningRequestPending = false;
      session.update();
      consumedMouseForTick = true;
    }
    if (consumedMouseForTick) {
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }

    const ClientGame* currentAudioGame = session.game();
    if (currentAudioGame != audioGame) {
      audioGame = currentAudioGame;
      audioStateInitialized = false;
      lastAudioCountdownSecond = 0;
      lastHitSoundServerTick = 0;
      lastPlayedWeaponFires = {};
      lastPlayedWeaponFireAudioTicks = {};
      hasLastPlayedWeaponFire = {};
      lastPlayedRocketExplosions = {};
      lastPlayedRocketExplosionAudioTicks = {};
      hasLastPlayedRocketExplosion = {};
      lastPlayedFragEvents = {};
      lastPlayedFragAudioTicks = {};
      hasLastPlayedFragEvent = {};
      lastPlayedFootstepAudioSequences = {};
      lastPlayedGrenadeBounceAudioSequences = {};
      lastLocalRailFireTick = 0;
      hasLocalRailFireTick = false;
      localRailReadySoundPlayed = true;
      lastAudioPlayerHealth = {};
      previousLocalHit = false;
      hasLocalPlayerAliveState = false;
      wasLocalPlayerAlive = false;
      hasEnemyHitTime = false;
      lingeringRailBeams = {};
      footstepAudioStates = {};
      damageNumberState.reset();
      lastDamageNumberServerTick = 0;
      lastDamageNumberFeedbackSequences = {};
      hasLastDamageNumberFeedbackSequence = {};
      damageNumberStateInitialized = false;
      audio.resetLightningGunFire();
    }
    if (
      audioAvailable &&
      currentAudioGame != nullptr &&
      currentAudioGame->hasSnapshot()
    ) {
      const ServerSnapshot& audioSnapshot = currentAudioGame->snapshot();
      if (
        !audioStateInitialized ||
        audioSnapshot.serverTick != lastAudioServerTick
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
        const bool localPlayerAlive =
          currentAudioGame->predictedPlayer().health > 0;
        if (
          hasLocalPlayerAliveState &&
          wasLocalPlayerAlive &&
          !localPlayerAlive
        ) {
          audio.resetLightningGunFire();
        }
        hasLocalPlayerAliveState = true;
        wasLocalPlayerAlive = localPlayerAlive;

        const DamageNumbersConfig damageConfig = damageNumbersConfig(console);
        if (audioSnapshot.serverTick != lastDamageNumberServerTick) {
          if (damageNumberStateInitialized) {
            std::uint32_t newestFeedbackSequence =
              lastDamageNumberFeedbackSequences[localPlayerIndex];
            const std::uint32_t previousFeedbackSequence =
              newestFeedbackSequence;
            const bool hadFeedbackSequence =
              hasLastDamageNumberFeedbackSequence[localPlayerIndex];
            bool consumedFeedback = false;
            for (
              const LocalHitFeedbackEvent& feedback :
              audioSnapshot.localHitFeedbackEvents[localPlayerIndex]
            ) {
              if (
                !feedback.active ||
                feedback.damageApplied <= 0 ||
                feedback.targetPlayerIndex >= kDuelPlayerCount ||
                (
                  hadFeedbackSequence &&
                  !isSequenceNewer(feedback.sequence, previousFeedbackSequence)
                )
              ) {
                continue;
              }
              LocalDamageEvent event{
                localDamageSourceForWeapon(feedback.weapon),
                feedback.sequence,
                static_cast<std::uint8_t>(localPlayerIndex),
                feedback.targetPlayerIndex,
                feedback.damageApplied,
                true,
                feedback.weapon,
              };
              event.hasTargetPosition = true;
              event.targetPosition =
                audioSnapshot.players[feedback.targetPlayerIndex].position;
              damageNumberState.addLocalDamageEvent(event, damageConfig);
              consumedFeedback = true;
              if (
                !hasLastDamageNumberFeedbackSequence[localPlayerIndex] ||
                isSequenceNewer(feedback.sequence, newestFeedbackSequence)
              ) {
                newestFeedbackSequence = feedback.sequence;
              }
            }
            lastDamageNumberFeedbackSequences[localPlayerIndex] =
              newestFeedbackSequence;
            hasLastDamageNumberFeedbackSequence[localPlayerIndex] =
              hadFeedbackSequence || consumedFeedback;
          } else {
            bool foundFeedbackSequence = false;
            std::uint32_t newestFeedbackSequence = 0;
            for (
              const LocalHitFeedbackEvent& feedback :
              audioSnapshot.localHitFeedbackEvents[localPlayerIndex]
            ) {
              if (!feedback.active) {
                continue;
              }
              if (
                !foundFeedbackSequence ||
                isSequenceNewer(feedback.sequence, newestFeedbackSequence)
              ) {
                newestFeedbackSequence = feedback.sequence;
              }
              foundFeedbackSequence = true;
            }
            if (foundFeedbackSequence) {
              lastDamageNumberFeedbackSequences[localPlayerIndex] =
                newestFeedbackSequence;
              hasLastDamageNumberFeedbackSequence[localPlayerIndex] = true;
            }
          }
          damageNumberStateInitialized = true;
          lastDamageNumberServerTick = audioSnapshot.serverTick;
        }

        const bool localHit =
          audioSnapshot.lightningGuns[localPlayerIndex].hit;
        const float volume = console.getFloat("s_volume");
        const bool soundEnabled = console.getBool("s_enable");
        const auto soundVolume = [&console, volume](std::string_view name) {
          return volume * console.getFloat(name);
        };
        const float hitVolume = soundVolume("s_hit_volume");
        const float painVolume = soundVolume("s_pain_volume");
        const float fragVolume = soundVolume("s_frag_volume");
        const float footstepVolume = soundVolume("s_footstep_volume");
        auto playPainGruntIfDamaged = [&](std::size_t playerIndex) {
          if (!soundEnabled || !audioStateInitialized) {
            return;
          }
          const PlayerState& player = audioSnapshot.players[playerIndex];
          if (
            !audioSnapshot.connectedPlayers[playerIndex] ||
            player.health >= lastAudioPlayerHealth[playerIndex] ||
            lastAudioPlayerHealth[playerIndex] <= 0
          ) {
            return;
          }
          const SpatialAudio painAudio = playerIndex == localPlayerIndex
            ? SpatialAudio{painVolume, 0.0F}
            : worldAudio(painVolume, player.position, currentAudioGame->predictedPlayer());
          audio.playPainGrunt(painAudio.volume, painAudio.pan);
        };
        playPainGruntIfDamaged(localPlayerIndex);
        for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
          if (playerIndex != localPlayerIndex) {
            playPainGruntIfDamaged(playerIndex);
          }
        }
        constexpr std::uint32_t kHitSoundIntervalTicks = 10;
        if (
          soundEnabled &&
          audioStateInitialized &&
          localHit &&
          (
            !previousLocalHit ||
            audioSnapshot.serverTick - lastHitSoundServerTick >=
              kHitSoundIntervalTicks
          )
        ) {
          audio.playHit(
            hitVolume,
            audioSnapshot.lightningGuns[localPlayerIndex].damageApplied
          );
          lastHitSoundServerTick = audioSnapshot.serverTick;
        }
        if (audioStateInitialized) {
          updateFootstepAudio(
            footstepAudioStates[localPlayerIndex],
            currentAudioGame->predictedPlayer(),
            currentAudioGame->predictedPlayer(),
            true,
            soundEnabled ? footstepVolume : 0.0F,
            audio
          );
          if (soundEnabled) {
            for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
              if (playerIndex == localPlayerIndex) {
                continue;
              }
              const FootstepAudioEvent& event =
                audioSnapshot.footstepAudioEvents[playerIndex];
              if (
                !event.active ||
                event.sequence == lastPlayedFootstepAudioSequences[playerIndex]
              ) {
                continue;
              }
              const SpatialAudio spatial =
                worldAudio(
                  footstepVolume,
                  event.position,
                  currentAudioGame->predictedPlayer()
                );
              audio.playFootstep(spatial.volume, event.sequence, spatial.pan);
              lastPlayedFootstepAudioSequences[playerIndex] = event.sequence;
            }
            for (
              std::size_t eventIndex = 0;
              eventIndex < audioSnapshot.grenadeBounceAudioEvents.size();
              ++eventIndex
            ) {
              const GrenadeBounceAudioEvent& event =
                audioSnapshot.grenadeBounceAudioEvents[eventIndex];
              if (
                !event.active ||
                event.sequence == lastPlayedGrenadeBounceAudioSequences[eventIndex]
              ) {
                continue;
              }
              const SpatialAudio spatial =
                worldAudio(
                  soundVolume("s_gl_bounce_volume"),
                  event.position,
                  currentAudioGame->predictedPlayer()
                );
              audio.playGrenadeBounce(spatial.volume * 0.5F, spatial.pan);
              lastPlayedGrenadeBounceAudioSequences[eventIndex] = event.sequence;
            }
          }
        }
        if (soundEnabled && audioStateInitialized) {
          const FragEvent& localFrag = audioSnapshot.fragEvents[localPlayerIndex];
          if (
            localFrag.active &&
            shouldPlaySnapshotAudioEvent(
              hasLastPlayedFragEvent[localPlayerIndex],
              sameFragEvent(localFrag, lastPlayedFragEvents[localPlayerIndex]),
              audioSnapshot.serverTick,
              lastPlayedFragAudioTicks[localPlayerIndex],
              kTransientAudioEventTicks
            )
          ) {
            audio.playFrag(fragVolume);
            lastPlayedFragEvents[localPlayerIndex] = localFrag;
            lastPlayedFragAudioTicks[localPlayerIndex] = audioSnapshot.serverTick;
            hasLastPlayedFragEvent[localPlayerIndex] = true;
          }

          for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {

            const WeaponFireResult& fire = audioSnapshot.weaponFires[playerIndex];
            const bool localWeaponEvent = playerIndex == localPlayerIndex;
            if (
              fire.fired &&
              shouldPlaySnapshotAudioEvent(
                hasLastPlayedWeaponFire[playerIndex],
                sameWeaponFireEvent(fire, lastPlayedWeaponFires[playerIndex]),
                audioSnapshot.serverTick,
                lastPlayedWeaponFireAudioTicks[playerIndex],
                kTransientAudioEventTicks
              )
            ) {
              const WeaponFireAudioEvent fireAudio =
                routeWeaponFireAudioEvent(fire, localWeaponEvent);
              float weaponFireVolume = volume;
              if (fireAudio.cue == WeaponFireAudioCue::Railgun) {
                weaponFireVolume = soundVolume("s_rg_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::RocketLauncher) {
                weaponFireVolume = soundVolume("s_rl_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::MachineGun) {
                weaponFireVolume = soundVolume("s_mg_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::Shotgun) {
                weaponFireVolume = soundVolume("s_sg_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::GrenadeLauncher) {
                weaponFireVolume = soundVolume("s_gl_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::PlasmaGun) {
                weaponFireVolume = soundVolume("s_pg_fire_volume");
              }
              const SpatialAudio weaponFireAudio = localWeaponEvent
                ? SpatialAudio{weaponFireVolume, 0.0F}
                : worldAudio(
                  weaponFireVolume,
                  fire.start,
                  currentAudioGame->predictedPlayer()
                );
              if (fireAudio.cue == WeaponFireAudioCue::Railgun) {
                audio.playRailFire(weaponFireAudio.volume, weaponFireAudio.pan);
                if (fireAudio.startsLocalRailCooldown) {
                  lastLocalRailFireTick = audioSnapshot.serverTick;
                  hasLocalRailFireTick = true;
                  localRailReadySoundPlayed = false;
                }
              } else if (fireAudio.cue == WeaponFireAudioCue::RocketLauncher) {
                audio.playRocketFire(weaponFireAudio.volume, weaponFireAudio.pan);
              } else if (fireAudio.cue == WeaponFireAudioCue::MachineGun) {
                audio.playMachineGunFire(weaponFireAudio.volume, weaponFireAudio.pan);
              } else if (fireAudio.cue == WeaponFireAudioCue::Shotgun) {
                audio.playShotgunFire(weaponFireAudio.volume, weaponFireAudio.pan);
              } else if (fireAudio.cue == WeaponFireAudioCue::GrenadeLauncher) {
                audio.playGrenadeLauncherFire(
                  weaponFireAudio.volume,
                  weaponFireAudio.pan
                );
              } else if (fireAudio.cue == WeaponFireAudioCue::PlasmaGun) {
                audio.playPlasmaGunFire(weaponFireAudio.volume, weaponFireAudio.pan);
              }
              if (fireAudio.localHitConfirmDamage > 0) {
                audio.playHit(hitVolume, fireAudio.localHitConfirmDamage);
                lastHitSoundServerTick = audioSnapshot.serverTick;
              }
              lastPlayedWeaponFires[playerIndex] = fire;
              lastPlayedWeaponFireAudioTicks[playerIndex] = audioSnapshot.serverTick;
              hasLastPlayedWeaponFire[playerIndex] = true;
            }

            const RocketExplosionResult& explosion =
              audioSnapshot.rocketExplosions[playerIndex];
            if (
              explosion.active &&
              shouldPlaySnapshotAudioEvent(
                hasLastPlayedRocketExplosion[playerIndex],
                sameRocketExplosionEvent(
                  explosion,
                  lastPlayedRocketExplosions[playerIndex]
                ),
                audioSnapshot.serverTick,
                lastPlayedRocketExplosionAudioTicks[playerIndex],
                kTransientAudioEventTicks
              )
            ) {
              const SpatialAudio explosionAudio = worldAudio(
                  soundVolume("s_rl_explosion_volume"),
                  explosion.position,
                  currentAudioGame->predictedPlayer()
              );
              audio.playRocketExplosion(explosionAudio.volume, explosionAudio.pan);
              if (
                localWeaponEvent &&
                explosion.opponentDamageApplied > 0
              ) {
                audio.playHit(hitVolume, explosion.opponentDamageApplied);
                lastHitSoundServerTick = audioSnapshot.serverTick;
              }
              lastPlayedRocketExplosions[playerIndex] = explosion;
              lastPlayedRocketExplosionAudioTicks[playerIndex] =
                audioSnapshot.serverTick;
              hasLastPlayedRocketExplosion[playerIndex] = true;
            }
          }

          if (
            hasLocalRailFireTick &&
            !localRailReadySoundPlayed &&
            selectedWeapon == Weapon::Railgun &&
            audioSnapshot.serverTick - lastLocalRailFireTick >=
              kClientRailgunCooldownTicks
          ) {
            audio.playRailReady(soundVolume("s_rg_ready_volume"));
            localRailReadySoundPlayed = true;
          }
        }
        if (
          soundEnabled &&
          audioStateInitialized &&
          audioSnapshot.matchPhase != lastAudioMatchPhase &&
          (
            audioSnapshot.matchPhase == MatchPhase::RoundEnd ||
            audioSnapshot.matchPhase == MatchPhase::MatchEnd
          )
        ) {
          const bool won = localPlayerWonResult(
            audioSnapshot,
            localPlayerIndex,
            audioSnapshot.matchPhase == MatchPhase::MatchEnd
          );
          audio.playRoundResult(
            won,
            soundVolume(won ? "s_round_win_volume" : "s_round_loss_volume")
          );
        }
        const std::uint32_t countdownSecond =
          audioSnapshot.matchPhase == MatchPhase::Countdown
          ? (audioSnapshot.phaseTicksRemaining + 124U) / 125U
          : 0U;
        if (
          soundEnabled &&
          audioStateInitialized &&
          countdownSecond > 0U &&
          countdownSecond != lastAudioCountdownSecond
        ) {
          audio.playCountdown(countdownSecond, soundVolume("s_countdown_volume"));
        }
        lastAudioCountdownSecond = countdownSecond;
        for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
          lastAudioPlayerHealth[playerIndex] = audioSnapshot.players[playerIndex].health;
        }
        previousLocalHit = localHit;
        lastAudioServerTick = audioSnapshot.serverTick;
        lastAudioMatchPhase = audioSnapshot.matchPhase;
        audioStateInitialized = true;
      }
    }
    if (audioAvailable) {
      float lightningGunVolume = 0.0F;
      float lightningGunPan = 0.0F;
      if (
        console.getBool("s_enable") &&
        currentAudioGame != nullptr &&
        currentAudioGame->hasSnapshot() &&
        currentAudioGame->predictedPlayer().health > 0
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
        const ServerSnapshot& snapshot = currentAudioGame->snapshot();
        const float masterVolume =
          console.getFloat("s_volume") * console.getFloat("s_lg_fire_volume");
        if (snapshot.lightningGuns[localPlayerIndex].active) {
          lightningGunVolume = masterVolume;
          lightningGunPan = 0.0F;
        }
        for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
          if (
            playerIndex == localPlayerIndex ||
            !snapshot.lightningGuns[playerIndex].active ||
            snapshot.players[playerIndex].health <= 0
          ) {
            continue;
          }
          const SpatialAudio spatial = worldAudio(
              masterVolume,
              snapshot.players[playerIndex].position,
              currentAudioGame->predictedPlayer()
          );
          if (spatial.volume > lightningGunVolume) {
            lightningGunVolume = spatial.volume;
            lightningGunPan = spatial.pan;
          }
        }
      }
      audio.setLightningGunFire(
        lightningGunVolume > 0.0F,
        lightningGunVolume,
        lightningGunPan
      );
      audio.update();
    }
    if (ClientGame* interpolationGame = session.game();
        interpolationGame != nullptr && interpolationGame->hasSnapshot()) {
      interpolationGame->advanceInterpolation(
        elapsed.count(),
        console.getFloat("cl_interp")
      );
    }

    ++renderedFrameCount;
    if (titleAccumulatorSeconds >= 0.1F) {
      displayedFramesPerSecond =
        static_cast<float>(renderedFrameCount) / titleAccumulatorSeconds;
      renderedFrameCount = 0;
      char title[512];
      char fpsText[256] = {};
      if (console.getBool("cl_showfps")) {
        const float frameMilliseconds = displayedFramesPerSecond > 0.0F
          ? 1000.0F / displayedFramesPerSecond
          : 0.0F;
        const RendererFrameDiagnostics& renderDiagnostics =
          renderer.lastFrameDiagnostics();
        char rendererTimingText[96] = {};
        if (console.getBool("cl_show_frame_stats")) {
          std::snprintf(
            rendererTimingText,
            sizeof(rendererTimingText),
            " gpu %.2f/%.2f/%.2f/%.2f",
            renderDiagnostics.swapchainAcquireMilliseconds,
            renderDiagnostics.renderBuildUploadMilliseconds,
            renderDiagnostics.submitMilliseconds,
            renderDiagnostics.totalRenderMilliseconds
          );
        }
        const std::string_view presentMode =
          renderDiagnostics.selectedPresentModeName;
        std::snprintf(
          fpsText,
          sizeof(fpsText),
          " | %.0f FPS %.2f ms %s delay %d lrp %d frame %.2f/%.2f/%.2f/%.2f/%.2f mode %.*s%s",
          displayedFramesPerSecond,
          frameMilliseconds,
          std::string(renderer.backendName()).c_str(),
          console.getBool("cl_legacy_frame_delay") ? 1 : 0,
          console.getBool("cl_local_render_prediction") ? 1 : 0,
          displayedFrameTimes.averageMilliseconds,
          displayedFrameTimes.p50Milliseconds,
          displayedFrameTimes.p95Milliseconds,
          displayedFrameTimes.p99Milliseconds,
          displayedFrameTimes.maxMilliseconds,
          static_cast<int>(presentMode.size()),
          presentMode.data(),
          rendererTimingText
        );
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
    const bool bufferedInterpolation = console.getInt("cl_interp_mode") != 0;
    const bool localRenderPredictionEnabled =
      console.getBool("cl_local_render_prediction");
    const float localRenderPredictionSeconds =
      localRenderPredictionEnabled
        ? clamp(accumulatorSeconds, 0.0F, kFixedTickSeconds)
        : 0.0F;
    PlayerState renderPlayer;
    LightningGunResult renderLocalLightningGun;
    std::array<RemotePlayerView, kDuelPlayerCount> renderRemotePlayers = {};
    std::array<WeaponFireResult, kDuelPlayerCount> renderWeaponFires = {};
    std::array<RocketExplosionResult, kDuelPlayerCount> renderRocketExplosions = {};
    std::array<RocketProjectileSnapshot, kMaxRocketProjectiles> renderRockets = {};
    std::size_t renderLocalPlayerIndex = 0;
    if (const ClientGame* renderClient = session.game();
        renderClient != nullptr && renderClient->hasSnapshot()) {
      const std::size_t localPlayerIndex = session.playerIndex();
      renderLocalPlayerIndex = localPlayerIndex;
      renderPlayer = renderClient->predictedPlayer();
      if (
        localRenderPredictionSeconds > 0.0F &&
        renderPlayer.health > 0
      ) {
        int viewportWidth = 0;
        int viewportHeight = 0;
        SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);
        const bool zoomHeld = zoomPressCount > 0;
        const float effectiveFieldOfView = zoomHeld
          ? console.getFloat("cl_zoom_fov")
          : console.getFloat("cl_fov");
        const float zoomSensitivity = zoomSensitivityMultiplier(
          console.getFloat("cl_fov"),
          console.getFloat("cl_zoom_fov"),
          console.getFloat("cl_zoom_sensitivity")
        );
        const float effectiveSensitivity = console.getFloat("sensitivity") *
          (zoomHeld ? zoomSensitivity : 1.0F);
        const AimMode currentAimMode =
          aimModeFromInt(console.getInt("cl_aim_mode"));
        const UserCommand visualCommand =
          usePresentationView && presentationView.initialized
            ? buildCommandWithViewAngles(
                input,
                commandSequence,
                clientTick,
                presentationView.yawRadians,
                presentationView.pitchRadians,
                false,
                selectedWeapon
              )
            : buildCommand(
                input,
                renderPlayer,
                commandSequence,
                clientTick,
                effectiveSensitivity,
                currentAimMode,
                viewportWidth,
                viewportHeight,
                effectiveFieldOfView,
                console.getFloat("cl_camera_zoom"),
                perspectiveRenderMode ? 1 : 0,
                selectedWeapon
              );

        PlayerState visualPlayer = renderPlayer;
        simulateMovement(
          visualPlayer,
          visualCommand,
          renderClient->arena(),
          renderClient->movementTuning(),
          localRenderPredictionSeconds
        );
        renderPlayer = visualPlayer;
      }
      const ServerSnapshot& renderSnapshot = renderClient->snapshot();
      for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
        if (playerIndex == localPlayerIndex) {
          continue;
        }
        if (!renderSnapshot.participatingPlayers[playerIndex]) {
          continue;
        }
        if (
          renderSnapshot.gameMode == GameMode::ClanArena &&
          renderSnapshot.players[playerIndex].health <= 0
        ) {
          continue;
        }
        const bool teammate = playerPresentedAsTeammate(
          renderSnapshot,
          localPlayerIndex,
          playerIndex
        );
        renderRemotePlayers[playerIndex] = RemotePlayerView{
          bufferedInterpolation
            ? renderClient->interpolatedPlayer(playerIndex)
            : renderClient->interpolatedPlayer(playerIndex, interpolationAlpha),
          renderSnapshot.lightningGuns[playerIndex],
          renderSnapshot.selectedWeapons[playerIndex],
          0.0F,
          1.0F,
          true,
          teammate,
          renderSnapshot.playerNames[playerIndex],
        };
        const int currentRemoteHealth =
          renderSnapshot.players[playerIndex].health;
        if (
          hasLastRemoteHealth[playerIndex] &&
          currentRemoteHealth < lastRemoteHealth[playerIndex]
        ) {
          lastRemoteDamageTime[playerIndex] = now;
          hasLastRemoteDamageTime[playerIndex] = true;
        }
        lastRemoteHealth[playerIndex] = currentRemoteHealth;
        hasLastRemoteHealth[playerIndex] = true;
      }
      renderLocalLightningGun =
        renderSnapshot.lightningGuns[localPlayerIndex];
      renderWeaponFires = renderSnapshot.weaponFires;
      renderRocketExplosions = renderSnapshot.rocketExplosions;
      renderRockets = renderSnapshot.rockets;
      const LocalHitFeedbackBatch hitFeedback =
        consumeLocalHitFeedbackEvents(
          renderSnapshot.localHitFeedbackEvents[localPlayerIndex],
          localHitFeedbackDedupe
        );
      if (hitFeedback.active) {
        lastEnemyHitTime = now;
        hasEnemyHitTime = true;
        for (std::size_t targetIndex = 0; targetIndex < kDuelPlayerCount; ++targetIndex) {
          if (hitFeedback.hitTargets[targetIndex]) {
            lastEnemyHitTimeByTarget[targetIndex] = now;
            hasEnemyHitTimeByTarget[targetIndex] = true;
          }
        }
        if (hitFeedback.lightningGunHit) {
          lastBeamHitTime = now;
          hasBeamHitTime = true;
        }
      }
    }
    if (usePresentationView && presentationView.initialized) {
      renderPlayer.viewYawRadians = presentationView.yawRadians;
      renderPlayer.viewPitchRadians = presentationView.pitchRadians;
    }
    RenderSettings currentRenderSettings = renderSettings(console);
    currentRenderSettings.hasRemotePlayer = std::any_of(
      renderRemotePlayers.begin(),
      renderRemotePlayers.end(),
      [](const RemotePlayerView& remote) { return remote.visible; }
    );
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      WeaponFireResult& currentFire = renderWeaponFires[playerIndex];
      LingeringRailBeam& lingeringBeam = lingeringRailBeams[playerIndex];
      if (currentFire.fired && currentFire.weapon == Weapon::Railgun) {
        const WeaponFireResult sourceFire = currentFire;
        const bool localPerspectiveRail =
          currentRenderSettings.renderMode == 1 &&
          playerIndex == renderLocalPlayerIndex;
        const bool newRailEvent =
          !lingeringBeam.active ||
          !sameWeaponFireEvent(sourceFire, lingeringBeam.sourceFire);
        if (newRailEvent) {
          if (localPerspectiveRail) {
            currentFire.start = viewmodelMuzzlePosition(renderPlayer);
          }
          lingeringBeam.sourceFire = sourceFire;
          lingeringBeam.fire = currentFire;
          lingeringBeam.active = true;
          lingeringBeam.expiresAt =
            now + std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<float>(kRailgunBeamLingerSeconds)
            );
        } else {
          currentFire = lingeringBeam.fire;
        }
      } else if (
        !currentFire.fired &&
        lingeringBeam.active &&
        now < lingeringBeam.expiresAt
      ) {
        currentFire = lingeringBeam.fire;
      } else if (lingeringBeam.active && now >= lingeringBeam.expiresAt) {
        lingeringBeam.active = false;
      }
    }
    if (zoomPressCount > 0) {
      currentRenderSettings.fieldOfView = console.getFloat("cl_zoom_fov");
    }
    constexpr float kBeamPulseRadiansPerSecond = 31.4159265359F;
    const double presentationSeconds =
      std::chrono::duration<double>(now.time_since_epoch()).count();
    currentRenderSettings.beamPulse = std::sin(
      static_cast<float>(std::fmod(presentationSeconds, 1.0)) *
        kBeamPulseRadiansPerSecond
    );
    const float elapsedSinceHit = hasEnemyHitTime
      ? std::chrono::duration<float>(now - lastEnemyHitTime).count()
      : 0.0F;
    const auto hitFeedbackAmount =
      [&](float duration, bool fade) {
        if (!hasEnemyHitTime || duration <= 0.0F || elapsedSinceHit >= duration) {
          return 0.0F;
        }
        return fade ? 1.0F - (elapsedSinceHit / duration) : 1.0F;
      };
    const auto beamHitFeedbackAmount =
      [&](float duration, bool fade) {
        if (!hasBeamHitTime || duration <= 0.0F) {
          return 0.0F;
        }
        const float elapsedSinceBeamHit =
          std::chrono::duration<float>(now - lastBeamHitTime).count();
        if (elapsedSinceBeamHit >= duration) {
          return 0.0F;
        }
        return fade ? 1.0F - (elapsedSinceBeamHit / duration) : 1.0F;
      };
    currentRenderSettings.enemyHitAmount = 0.0F;
    if (console.getBool("r_enemy_hit_enable")) {
      for (
        std::size_t playerIndex = 0;
        playerIndex < renderRemotePlayers.size();
        ++playerIndex
      ) {
        RemotePlayerView& hitRemote = renderRemotePlayers[playerIndex];
        if (
          hitRemote.teammate ||
          !hasEnemyHitTimeByTarget[playerIndex]
        ) {
          continue;
        }
        const float elapsedSinceTargetHit =
          std::chrono::duration<float>(
            now - lastEnemyHitTimeByTarget[playerIndex]
          ).count();
        const float duration = console.getFloat("r_enemy_hit_duration");
        if (duration <= 0.0F || elapsedSinceTargetHit >= duration) {
          hitRemote.enemyHitAmount = 0.0F;
          continue;
        }
        hitRemote.enemyHitAmount =
          console.getBool("r_enemy_hit_fade")
            ? 1.0F - (elapsedSinceTargetHit / duration)
            : 1.0F;
      }
    }
    if (console.getBool("r_beam_hit_enable")) {
      currentRenderSettings.beamHitAmount = beamHitFeedbackAmount(
        console.getFloat("r_beam_hit_duration"),
        console.getBool("r_beam_hit_fade")
      );
    }
    if (console.getBool("crosshair_hit_enable")) {
      currentRenderSettings.crosshairHitAmount = hitFeedbackAmount(
        console.getFloat("crosshair_hit_duration"),
        console.getBool("crosshair_hit_fade")
      );
    }
    if (currentRenderSettings.hitMarkerEnabled) {
      currentRenderSettings.hitMarkerAmount = hitFeedbackAmount(
        console.getFloat("r_hitmarker_duration"),
        true
      );
    }
    damageNumberState.update(
      outerFrameElapsed.count(),
      damageNumbersConfig(console)
    );
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      RemotePlayerView& remote = renderRemotePlayers[playerIndex];
      if (!remote.visible) {
        continue;
      }
      const bool damageOnly = remote.teammate
        ? currentRenderSettings.teammateHealthBarDamageOnly
        : currentRenderSettings.enemyHealthBarDamageOnly;
      if (!damageOnly) {
        remote.enemyHealthAlpha = 1.0F;
        continue;
      }
      const float duration = remote.teammate
        ? currentRenderSettings.teammateHealthBarVisibleDuration
        : currentRenderSettings.enemyHealthBarVisibleDuration;
      if (!hasLastRemoteDamageTime[playerIndex] || duration <= 0.0F) {
        remote.enemyHealthAlpha = 0.0F;
        continue;
      }
      const float elapsed =
        std::chrono::duration<float>(now - lastRemoteDamageTime[playerIndex]).count();
      if (elapsed >= duration) {
        remote.enemyHealthAlpha = 0.0F;
        continue;
      }
      const bool fade = remote.teammate
        ? currentRenderSettings.teammateHealthBarFade
        : currentRenderSettings.enemyHealthBarFade;
      remote.enemyHealthAlpha = fade ? 1.0F - (elapsed / duration) : 1.0F;
    }
    currentRenderSettings.playerSizePixels =
      14.0F * (renderPlayer.bounds.radius / 0.35F);
    const AimMode renderAimMode = currentRenderSettings.renderMode == 1
      ? AimMode::Relative3D
      : aimModeFromInt(console.getInt("cl_aim_mode"));
    if (currentRenderSettings.renderMode == 1) {
      currentRenderSettings.rotateView = false;
      currentRenderSettings.crosshairUseScreenPosition = false;
    }
    if (
      renderAimMode == AimMode::Relative3D &&
      currentRenderSettings.renderMode == 0
    ) {
      currentRenderSettings.crosshairEnabled = false;
    } else {
      // Absolute screen-space aiming needs a stable world-aligned camera.
      currentRenderSettings.rotateView = false;
    }
    if (
      renderAimMode == AimMode::Absolute2D &&
      input.hasMousePosition &&
      !consoleState.open
    ) {
      currentRenderSettings.crosshairUseScreenPosition = true;
      currentRenderSettings.crosshairScreenX = input.mouseX;
      currentRenderSettings.crosshairScreenY = input.mouseY;
    }

    HudRenderState hud = buildHud(session, console.getBool("cl_show_alive_counts"));
    hud.selectedWeapon = selectedWeapon;
    hud.previousWeapon = previousViewWeapon;
    hud.damageNumbers = damageNumberState.presentation();
    hud.weaponSwitchProgress = kWeaponSwitchDurationSeconds > 0.0F
      ? weaponSwitchSeconds / kWeaponSwitchDurationSeconds
      : 1.0F;
    if (
      console.getBool("cl_showspeed") &&
      session.game() != nullptr &&
      session.game()->hasSnapshot()
    ) {
      constexpr float kQuakeUnitsPerProjectUnit = 40.0F;
      const float horizontalSpeed = std::hypot(
        renderPlayer.velocity.x,
        renderPlayer.velocity.y
      );
      hud.bottomCenterLines.insert(
        hud.bottomCenterLines.begin(),
        "SPEED " + std::to_string(static_cast<int>(std::lround(
          horizontalSpeed * kQuakeUnitsPerProjectUnit
        ))) + " UPS"
      );
    }
    if (
      currentRenderSettings.showLagCompensation &&
      session.game() != nullptr &&
      session.game()->hasSnapshot()
    ) {
      const std::size_t localPlayerIndex = session.playerIndex();
      const ServerSnapshot& lagSnapshot = session.game()->snapshot();
      const LightningGunResult& beam =
        lagSnapshot.lightningGuns[localPlayerIndex];
      if (beam.hasRewindDebug) {
        char rewindText[192];
        std::snprintf(
          rewindText,
          sizeof(rewindText),
          "REWIND %u/%u%s TARGET TICK %u",
          beam.requestedRewindTicks,
          beam.appliedRewindTicks,
          beam.rewindClamped ? " CLAMP" : "",
          beam.rewindTargetTick
        );
        hud.topLeftLines.emplace_back(rewindText);
        std::snprintf(
          rewindText,
          sizeof(rewindText),
          "CURRENT %.2f %.2f %.2f | REWOUND %.2f %.2f %.2f",
          beam.currentTargetPosition.x,
          beam.currentTargetPosition.y,
          beam.currentTargetPosition.z,
          beam.rewoundTargetPosition.x,
          beam.rewoundTargetPosition.y,
          beam.rewoundTargetPosition.z
        );
        hud.topLeftLines.emplace_back(rewindText);
      } else {
        const Weapon selectedWeapon =
          lagSnapshot.selectedWeapons[localPlayerIndex];
        hud.topLeftLines.emplace_back(
          selectedWeapon == Weapon::LightningGun
            ? "LAG COMPENSATION: NOT USED"
            : "LAG COMPENSATION: NOT USED BY THIS WEAPON"
        );
      }
    }
    if (
      scoreboardPressCount > 0 &&
      session.game() != nullptr &&
      session.game()->hasSnapshot()
    ) {
      populateScoreboard(
        hud,
        session.game()->snapshot(),
        session.playerIndex()
      );
    }
    if (chatState.inputOpen || Clock::now() < chatState.visibleUntil) {
      for (const ClientChatState::Message& message : chatState.history) {
        hud.chatLines.push_back(HudRenderState::ChatLine{
          message.playerIndex,
          message.text,
          message.speakerName,
        });
      }
    }
    hud.chatInputOpen = chatState.inputOpen;
    hud.chatInput = chatState.input;
    hud.chatCursorIndex = chatState.cursorIndex;
    hud.chatHasSelection = hasSelection(chatState.selection);
    hud.chatSelectionAnchor = chatState.selection.anchor;
    hud.chatSelectionFocus = chatState.selection.focus;
    const Arena& renderArena =
      session.game() != nullptr && session.game()->hasSnapshot()
        ? session.game()->arena()
        : fallbackArena;
    renderer.render(
      renderArena,
      renderPlayer,
      renderRemotePlayers,
      renderLocalLightningGun,
      renderWeaponFires,
      renderRocketExplosions,
      renderRockets,
      currentRenderSettings,
      hud,
      consoleRenderState(consoleState)
    );
    session.update();
    if (console.getBool("cl_legacy_frame_delay")) {
      SDL_Delay(1);
    }
  }
  saveClientConfig(console, bindings, configPath);
  audio.shutdown();
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
