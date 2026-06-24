#include "app/GameApp.hpp"

#include "app/AudioAssets.hpp"
#include "app/ConsoleInput.hpp"
#include "app/HudPresentation.hpp"
#include "app/Scoreboard.hpp"
#include "client/ClientSession.hpp"
#include "client/HitConfirmAudio.hpp"
#include "console/ConsoleSystem.hpp"
#include "input/InputBindings.hpp"
#include "input/MouseAim.hpp"
#include "render/ConsoleLayout.hpp"
#include "render/Renderer.hpp"
#include "shared/Constants.hpp"
#include "shared/FixedTick.hpp"
#include "shared/Math.hpp"
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
  bool inputOpen = false;
  std::string input;
  std::string pendingMessage;
  std::deque<std::string> history;
  std::uint32_t lastSequence = 0;
  std::chrono::steady_clock::time_point visibleUntil = {};
};

struct LingeringRailBeam {
  WeaponFireResult fire;
  WeaponFireResult sourceFire;
  bool active = false;
  std::chrono::steady_clock::time_point expiresAt = {};
};

struct FootstepAudioState {
  Vec3 previousPosition = {};
  float distanceSinceStep = 0.0F;
  std::uint32_t stepIndex = 0;
  bool wasOnGround = false;
  bool initialized = false;
};

class ClientAudio {
public:
  bool initialize(const std::filesystem::path& assetBasePath) {
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kSampleRate};
    stream_ = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
      &spec,
      nullptr,
      nullptr
    );
    if (stream_ == nullptr || !SDL_ResumeAudioStreamDevice(stream_)) {
      return false;
    }
    loadCueAssets(assetBasePath);
    return true;
  }

  void playHit(float volume, int damageApplied) {
    const float damageScale =
      std::clamp(static_cast<float>(damageApplied) / 100.0F, 0.0F, 1.0F);
    const float baseFrequency = 920.0F - (damageScale * 190.0F);
    queueHitPing(baseFrequency, volume);
  }

  void playRailFire(float volume) {
    queueRailDischarge(volume);
  }

  void playRailReady(float volume) {
    queueTone(760.0F, 0.035F, volume * 0.55F);
    queueTone(1040.0F, 0.045F, volume * 0.45F);
  }

  void playRocketFire(float volume) {
    queueRocketFire(volume);
  }

  void playMachineGunFire(float volume) {
    if (queueClip(machineGunFireClip_, volume)) {
      return;
    }
    queueMachineGunFire(volume);
  }

  void playShotgunFire(float volume) {
    if (queueClip(shotgunFireClip_, volume)) {
      return;
    }
    queueShotgunFire(volume);
  }

  void playGrenadeLauncherFire(float volume) {
    if (queueClip(grenadeLauncherFireClip_, volume)) {
      return;
    }
    queueGrenadeLauncherFire(volume);
  }

  void playPlasmaGunFire(float volume) {
    if (queueClip(plasmaGunFireClip_, volume)) {
      return;
    }
    queuePlasmaGunFire(volume);
  }

  void playRocketExplosion(float volume) {
    queueRocketPop(volume);
  }

  void playFootstep(float volume, std::uint32_t stepIndex) {
    if (queueClip(footstepClip_, volume * footstepGain(stepIndex))) {
      return;
    }
    queueFootstep(volume, stepIndex);
  }

  void setLightningGunFire(bool active, float volume) {
    lightningGunFireActive_ = active && volume > 0.0F;
    lightningGunFireTargetVolume_ = lightningGunFireActive_
      ? std::clamp(volume * 0.52F, 0.0F, 1.0F)
      : 0.0F;
  }

  void resetLightningGunFire() {
    lightningGunFireActive_ = false;
    lightningGunFireTargetVolume_ = 0.0F;
    lightningGunFireGain_ = 0.0F;
    lightningGunSampleIndex_ = 0;
  }

  void playRoundResult(bool won, float volume) {
    if (won) {
      queueTone(520.0F, 0.08F, volume);
      queueTone(780.0F, 0.12F, volume);
    } else {
      queueTone(420.0F, 0.09F, volume);
      queueTone(260.0F, 0.14F, volume);
    }
  }

  void playCountdown(std::uint32_t seconds, float volume) {
    const float urgency = 5.0F - static_cast<float>(std::min(seconds, 5U));
    queueTone(440.0F + (urgency * 55.0F), 0.11F, volume * 0.8F);
    queueTone(220.0F + (urgency * 27.5F), 0.07F, volume * 0.45F);
  }

  void update() {
    pumpAudio();
  }

  void shutdown() {
    setLightningGunFire(false, 0.0F);
    if (stream_ != nullptr) {
      SDL_DestroyAudioStream(stream_);
      stream_ = nullptr;
    }
    voices_.clear();
  }

private:
  struct SoundVoice {
    std::vector<float> samples;
    std::size_t playhead = 0;
  };

  struct LoadedClip {
    std::vector<float> samples;
  };

  void loadCueAssets(const std::filesystem::path& assetBasePath) {
    lightningGunLoop_ = loadCueClip(assetBasePath, AudioCue::LightningGunFireLoop);
    machineGunFireClip_ = loadCueClip(assetBasePath, AudioCue::MachineGunFire);
    shotgunFireClip_ = loadCueClip(assetBasePath, AudioCue::ShotgunFire);
    grenadeLauncherFireClip_ =
      loadCueClip(assetBasePath, AudioCue::GrenadeLauncherFire);
    plasmaGunFireClip_ = loadCueClip(assetBasePath, AudioCue::PlasmaGunFire);
    footstepClip_ = loadCueClip(assetBasePath, AudioCue::Footstep);
  }

  [[nodiscard]] static LoadedClip loadCueClip(
    const std::filesystem::path& assetBasePath,
    AudioCue cue
  ) {
    if (std::optional<AudioClip> clip = loadAudioCue(assetBasePath, cue)) {
      return LoadedClip{resampleToMixerRate(*clip)};
    }
    return {};
  }

  [[nodiscard]] static std::vector<float> resampleToMixerRate(const AudioClip& clip) {
    if (clip.samples.empty() || clip.sampleRate <= 0) {
      return {};
    }
    if (clip.sampleRate == kSampleRate) {
      return clip.samples;
    }

    const double sourceStep =
      static_cast<double>(clip.sampleRate) / static_cast<double>(kSampleRate);
    const auto outputCount = static_cast<std::size_t>(
      std::max(1.0, static_cast<double>(clip.samples.size()) / sourceStep)
    );
    std::vector<float> output(outputCount);
    for (std::size_t index = 0; index < output.size(); ++index) {
      const double sourcePosition = static_cast<double>(index) * sourceStep;
      const auto sourceIndex = static_cast<std::size_t>(sourcePosition);
      const std::size_t nextIndex =
        std::min(sourceIndex + 1U, clip.samples.size() - 1U);
      const float blend =
        static_cast<float>(sourcePosition - static_cast<double>(sourceIndex));
      output[index] =
        (clip.samples[sourceIndex] * (1.0F - blend)) +
        (clip.samples[nextIndex] * blend);
    }
    return output;
  }

  [[nodiscard]] bool queueClip(const LoadedClip& clip, float volume) {
    if (stream_ == nullptr || volume <= 0.0F || clip.samples.empty()) {
      return false;
    }
    std::vector<float> samples = clip.samples;
    for (float& sample : samples) {
      sample = std::clamp(sample * volume, -0.98F, 0.98F);
    }
    addVoice(std::move(samples));
    return true;
  }

  [[nodiscard]] static float footstepGain(std::uint32_t stepIndex) {
    return stepIndex % 2U == 0U ? 0.92F : 0.78F;
  }

  [[nodiscard]] static float audioEnvelope(
    float time,
    float duration,
    float attack,
    float release,
    float curve
  ) {
    const float clampedAttack = std::min(attack, duration * 0.35F);
    const float clampedRelease = std::min(release, duration * 0.7F);
    if (time < clampedAttack) {
      return time / std::max(clampedAttack, 0.0001F);
    }
    if (time > duration - clampedRelease) {
      const float releaseProgress =
        (duration - time) / std::max(clampedRelease, 0.0001F);
      return std::pow(std::max(0.0F, releaseProgress), curve);
    }
    return 1.0F;
  }

  [[nodiscard]] static float sine(float frequency, float time, float phase = 0.0F) {
    constexpr float kTwoPi = 6.28318530718F;
    return std::sin((kTwoPi * frequency * time) + phase);
  }

  [[nodiscard]] static float triangle(float frequency, float time) {
    const float phase = frequency * time - std::floor(frequency * time);
    return (4.0F * std::abs(phase - 0.5F)) - 1.0F;
  }

  [[nodiscard]] static float saw(float frequency, float time) {
    const float phase = frequency * time - std::floor(frequency * time);
    return (2.0F * phase) - 1.0F;
  }

  [[nodiscard]] static float noise(int sampleIndex) {
    std::uint32_t value = static_cast<std::uint32_t>(sampleIndex) * 747796405U +
      2891336453U;
    value = ((value >> ((value >> 28U) + 4U)) ^ value) * 277803737U;
    value = (value >> 22U) ^ value;
    return (static_cast<float>(value & 0xFFFFU) / 32767.5F) - 1.0F;
  }

  void queueRailDischarge(float volume) {
    queueSynth(0.185F, volume, [](float time, int sampleIndex) {
      const float progress = std::min(time / 0.18F, 1.0F);
      const float fastProgress = std::min(time / 0.10F, 1.0F);
      const float envelope = audioEnvelope(time, 0.185F, 0.002F, 0.070F, 1.35F);
      return envelope * (
        0.40F * sine(138.0F - (42.0F * progress), time) +
        0.24F * sine(276.0F - (90.0F * std::min(time / 0.12F, 1.0F)), time) +
        0.14F * saw(640.0F - (360.0F * fastProgress), time) +
        0.06F * noise(sampleIndex)
      );
    });
  }

  void queueHitPing(float baseFrequency, float volume) {
    queueSynth(0.090F, volume, [baseFrequency](float time, int) {
      const float progress = std::min(time / 0.03F, 1.0F);
      const float envelope = audioEnvelope(time, 0.090F, 0.001F, 0.070F, 1.7F);
      return envelope * (
        0.40F * sine(baseFrequency + (60.0F * progress), time) +
        0.22F * sine((baseFrequency * 1.5F) + (90.0F * progress), time)
      );
    });
  }

  void queueRocketFire(float volume) {
    queueSynth(0.185F, volume, [](float time, int sampleIndex) {
      const float progress = std::min(time / 0.14F, 1.0F);
      const float envelope = audioEnvelope(time, 0.185F, 0.003F, 0.060F, 1.35F);
      return envelope * (
        0.34F * sine(135.0F + (175.0F * progress), time) +
        0.18F * triangle(270.0F + (255.0F * std::min(time / 0.13F, 1.0F)), time) +
        0.11F * noise(sampleIndex)
      );
    });
  }

  void queueRocketPop(float volume) {
    queueSynth(0.210F, volume, [](float time, int sampleIndex) {
      const float envelope = audioEnvelope(time, 0.210F, 0.001F, 0.095F, 1.35F);
      const float bounceFrequency = time < 0.045F ? 520.0F : 374.0F;
      return envelope * (
        0.38F * sine(120.0F - (44.0F * std::min(time / 0.14F, 1.0F)), time) +
        0.24F * triangle(240.0F - (92.0F * std::min(time / 0.10F, 1.0F)), time) +
        0.10F * sine(bounceFrequency, time) +
        0.10F * noise(sampleIndex)
      );
    });
  }

  void queueMachineGunFire(float volume) {
    queueSynth(0.055F, volume * 0.62F, [](float time, int sampleIndex) {
      const float progress = std::min(time / 0.055F, 1.0F);
      const float envelope = audioEnvelope(time, 0.055F, 0.001F, 0.035F, 1.65F);
      return envelope * (
        0.30F * sine(230.0F - (80.0F * progress), time) +
        0.18F * triangle(520.0F - (260.0F * progress), time) +
        0.16F * noise(sampleIndex)
      );
    });
  }

  void queueShotgunFire(float volume) {
    queueSynth(0.155F, volume * 0.82F, [](float time, int sampleIndex) {
      const float progress = std::min(time / 0.155F, 1.0F);
      const float envelope = audioEnvelope(time, 0.155F, 0.001F, 0.085F, 1.45F);
      return envelope * (
        0.36F * sine(118.0F - (38.0F * progress), time) +
        0.20F * triangle(210.0F - (72.0F * progress), time) +
        0.22F * noise(sampleIndex) * (1.0F - (progress * 0.35F))
      );
    });
  }

  void queueGrenadeLauncherFire(float volume) {
    queueSynth(0.170F, volume * 0.78F, [](float time, int sampleIndex) {
      const float progress = std::min(time / 0.145F, 1.0F);
      const float envelope = audioEnvelope(time, 0.170F, 0.002F, 0.075F, 1.4F);
      return envelope * (
        0.32F * sine(105.0F + (90.0F * progress), time) +
        0.18F * triangle(190.0F + (110.0F * progress), time) +
        0.15F * noise(sampleIndex)
      );
    });
  }

  void queuePlasmaGunFire(float volume) {
    queueSynth(0.070F, volume * 0.52F, [](float time, int sampleIndex) {
      const float progress = std::min(time / 0.070F, 1.0F);
      const float envelope = audioEnvelope(time, 0.070F, 0.001F, 0.040F, 1.55F);
      return envelope * (
        0.24F * sine(760.0F + (180.0F * progress), time) +
        0.18F * sine(1140.0F + (260.0F * progress), time) +
        0.08F * triangle(380.0F, time) +
        0.06F * noise(sampleIndex)
      );
    });
  }

  void queueFootstep(float volume, std::uint32_t stepIndex) {
    queueSynth(0.105F, volume, [stepIndex](float time, int sampleIndex) {
      const float progress = std::min(time / 0.105F, 1.0F);
      const float envelope = audioEnvelope(time, 0.105F, 0.001F, 0.055F, 1.85F);
      const float lowThump =
        sine(92.0F - (18.0F * progress) + static_cast<float>(stepIndex % 2U) * 7.0F, time);
      const float slap =
        triangle(210.0F + static_cast<float>(stepIndex % 3U) * 18.0F, time);
      const float grit = noise(sampleIndex + static_cast<int>(stepIndex * 97U));
      return envelope * (
        0.30F * lowThump +
        0.16F * slap +
        0.18F * grit * (1.0F - progress)
      );
    });
  }

  [[nodiscard]] float lightningGunSample() {
    if (!lightningGunLoop_.samples.empty()) {
      const float sample =
        lightningGunLoop_.samples[
          static_cast<std::size_t>(
            lightningGunSampleIndex_ % lightningGunLoop_.samples.size()
          )
        ];
      lightningGunSampleIndex_ =
        (lightningGunSampleIndex_ + 1U) % lightningGunLoop_.samples.size();
      return sample;
    }

    constexpr std::uint64_t kLightningGunPeriodSamples =
      static_cast<std::uint64_t>(kSampleRate / 48);
    const float time =
      static_cast<float>(lightningGunSampleIndex_) / static_cast<float>(kSampleRate);
    lightningGunSampleIndex_ =
      (lightningGunSampleIndex_ + 1U) % kLightningGunPeriodSamples;

    return
      0.46F * sine(96.0F, time) +
      0.24F * sine(192.0F, time) +
      0.16F * std::tanh(sine(48.0F, time) * 1.9F) +
      0.07F * triangle(288.0F, time);
  }

  void mixLightningGunLoop(std::vector<float>& buffer) {
    constexpr float kGainStepPerSample = 1.0F / 900.0F;
    for (float& sample : buffer) {
      if (lightningGunFireGain_ < lightningGunFireTargetVolume_) {
        lightningGunFireGain_ =
          std::min(lightningGunFireTargetVolume_, lightningGunFireGain_ + kGainStepPerSample);
      } else if (lightningGunFireGain_ > lightningGunFireTargetVolume_) {
        lightningGunFireGain_ =
          std::max(lightningGunFireTargetVolume_, lightningGunFireGain_ - kGainStepPerSample);
      }
      if (lightningGunFireGain_ > 0.0005F) {
        sample += lightningGunSample() * lightningGunFireGain_;
      }
    }
  }

  [[nodiscard]] bool hasActiveLoop() const {
    return lightningGunFireActive_ || lightningGunFireGain_ > 0.0005F;
  }

  template <typename Generator>
  void queueSynth(float durationSeconds, float volume, Generator generator) {
    if (stream_ == nullptr || volume <= 0.0F) {
      return;
    }

    const int sampleCount =
      std::max(1, static_cast<int>(durationSeconds * kSampleRate));
    std::vector<float> samples(static_cast<std::size_t>(sampleCount));
    for (int index = 0; index < sampleCount; ++index) {
      const float time =
        static_cast<float>(index) / static_cast<float>(kSampleRate);
      samples[static_cast<std::size_t>(index)] =
        std::clamp(generator(time, index) * volume, -0.98F, 0.98F);
    }
    const int fadeSamples = std::min(160, sampleCount / 8);
    for (int index = 0; index < fadeSamples; ++index) {
      const float gain = static_cast<float>(index) /
        static_cast<float>(std::max(1, fadeSamples));
      samples[static_cast<std::size_t>(index)] *= gain;
      samples[static_cast<std::size_t>(sampleCount - 1 - index)] *= gain;
    }
    addVoice(std::move(samples));
  }

  void queueTone(float frequency, float durationSeconds, float volume) {
    if (stream_ == nullptr || volume <= 0.0F) {
      return;
    }

    const int sampleCount =
      std::max(1, static_cast<int>(durationSeconds * kSampleRate));
    std::vector<float> samples(static_cast<std::size_t>(sampleCount));
    constexpr float kTwoPi = 6.28318530718F;
    for (int index = 0; index < sampleCount; ++index) {
      const float progress =
        static_cast<float>(index) / static_cast<float>(sampleCount);
      const float envelope = 1.0F - progress;
      samples[static_cast<std::size_t>(index)] =
        std::sin(
          kTwoPi * frequency *
          (static_cast<float>(index) / static_cast<float>(kSampleRate))
        ) * envelope * volume;
    }
    addVoice(std::move(samples));
  }

  void addVoice(std::vector<float> samples) {
    if (samples.empty()) {
      return;
    }

    removeFinishedVoices();
    if (voices_.size() >= kMaxActiveVoices) {
      const auto quietestTail = std::min_element(
        voices_.begin(),
        voices_.end(),
        [](const SoundVoice& lhs, const SoundVoice& rhs) {
          return remainingSamples(lhs) < remainingSamples(rhs);
        }
      );
      voices_.erase(quietestTail);
    }
    voices_.push_back(SoundVoice{std::move(samples), 0});
  }

  void pumpAudio() {
    if (stream_ == nullptr) {
      return;
    }

    removeFinishedVoices();
    const int queuedBytes = SDL_GetAudioStreamQueued(stream_);
    int queuedSamples =
      std::max(0, queuedBytes) / static_cast<int>(sizeof(float));
    while ((!voices_.empty() || hasActiveLoop()) && queuedSamples < kTargetQueuedSamples) {
      const int sampleCount =
        std::min(kMixChunkSamples, kTargetQueuedSamples - queuedSamples);
      mixBuffer_.assign(static_cast<std::size_t>(sampleCount), 0.0F);

      for (SoundVoice& voice : voices_) {
        const std::size_t remaining = remainingSamples(voice);
        const std::size_t voiceSamples =
          std::min(remaining, static_cast<std::size_t>(sampleCount));
        for (std::size_t index = 0; index < voiceSamples; ++index) {
          mixBuffer_[index] += voice.samples[voice.playhead + index];
        }
      }
      if (hasActiveLoop()) {
        mixLightningGunLoop(mixBuffer_);
      }
      for (float& sample : mixBuffer_) {
        sample = std::clamp(sample, -0.98F, 0.98F);
      }
      for (SoundVoice& voice : voices_) {
        voice.playhead += static_cast<std::size_t>(sampleCount);
      }
      removeFinishedVoices();
      SDL_PutAudioStreamData(
        stream_,
        mixBuffer_.data(),
        static_cast<int>(mixBuffer_.size() * sizeof(float))
      );
      queuedSamples += sampleCount;
    }
  }

  void removeFinishedVoices() {
    voices_.erase(
      std::remove_if(
        voices_.begin(),
        voices_.end(),
        [](const SoundVoice& voice) {
          return voice.playhead >= voice.samples.size();
        }
      ),
      voices_.end()
    );
  }

  [[nodiscard]] static std::size_t remainingSamples(const SoundVoice& voice) {
    if (voice.playhead >= voice.samples.size()) {
      return 0;
    }
    return voice.samples.size() - voice.playhead;
  }

  static constexpr int kSampleRate = 48000;
  static constexpr int kMixChunkSamples = 256;
  static constexpr int kTargetQueuedSamples = 1024;
  static constexpr std::size_t kMaxActiveVoices = 32;
  SDL_AudioStream* stream_ = nullptr;
  std::vector<SoundVoice> voices_;
  std::vector<float> mixBuffer_;
  LoadedClip lightningGunLoop_;
  LoadedClip machineGunFireClip_;
  LoadedClip shotgunFireClip_;
  LoadedClip grenadeLauncherFireClip_;
  LoadedClip plasmaGunFireClip_;
  LoadedClip footstepClip_;
  bool lightningGunFireActive_ = false;
  float lightningGunFireTargetVolume_ = 0.0F;
  float lightningGunFireGain_ = 0.0F;
  std::uint64_t lightningGunSampleIndex_ = 0;
};

void updateFootstepAudio(
  FootstepAudioState& state,
  const PlayerState& player,
  Vec3 listenerPosition,
  bool localPlayer,
  float volume,
  ClientAudio& audio
) {
  constexpr float kMinimumStepSpeed = 1.15F;
  constexpr float kLandingStepSpeed = 2.0F;
  constexpr float kBaseStrideDistance = 1.45F;
  constexpr float kMinimumStrideDistance = 0.95F;

  if (!state.initialized) {
    state.previousPosition = player.position;
    state.wasOnGround = player.onGround;
    state.initialized = true;
    return;
  }

  const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);
  const Vec3 delta = player.position - state.previousPosition;
  const float horizontalDistance = std::hypot(delta.x, delta.y);
  const bool movingOnGround =
    player.health > 0 && player.onGround && horizontalSpeed >= kMinimumStepSpeed;

  auto playStep = [&]() {
    float spatialVolume = volume;
    if (!localPlayer) {
      const Vec3 listenerDelta = player.position - listenerPosition;
      const float distance = std::hypot(listenerDelta.x, listenerDelta.y);
      spatialVolume *= std::clamp(1.0F - (distance / 28.0F), 0.25F, 0.70F);
    }
    audio.playFootstep(spatialVolume, state.stepIndex++);
  };

  if (
    movingOnGround &&
    !state.wasOnGround &&
    horizontalSpeed >= kLandingStepSpeed
  ) {
    playStep();
    state.distanceSinceStep = 0.0F;
  } else if (movingOnGround) {
    state.distanceSinceStep += horizontalDistance;
    const float strideDistance = std::max(
      kMinimumStrideDistance,
      kBaseStrideDistance - (horizontalSpeed * 0.045F)
    );
    if (state.distanceSinceStep >= strideDistance) {
      playStep();
      state.distanceSinceStep = std::fmod(state.distanceSinceStep, strideDistance);
    }
  } else if (!player.onGround || horizontalSpeed < 0.25F) {
    state.distanceSinceStep = 0.0F;
  }

  state.previousPosition = player.position;
  state.wasOnGround = player.onGround;
}

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
  console.registerCvar({"cl_aim_mode", "Aim mode: 0 relative 3D, 1 absolute 2D.", 0, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"cl_render_mode", "Renderer: 0 top-down, 1 first-person 3D.", 0, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"cl_fov", "Top-down camera view extent.", 90.0F, archivedClient, 45.0F, 140.0F});
  console.registerCvar({"cl_zoom_fov", "Field of view while +zoom is held.", 45.0F, archivedClient, 20.0F, 140.0F});
  console.registerCvar({"cl_zoom_sensitivity", "Mouse sensitivity multiplier while +zoom is held; zero auto-matches FOV.", 0.0F, archivedClient, 0.0F, 10.0F});
  console.registerCvar({"cl_camera_zoom", "Camera zoom multiplier; values above one zoom in.", 1.0F, archivedClient, 0.25F, 4.0F});
  console.registerCvar({"cl_rotate_view", "Rotate relative-aim view so facing direction points up.", false, archivedClient, {}, {}});
  console.registerCvar({"cl_health_size", "Bottom-center health text scale.", 2.0F, archivedClient, 0.5F, 6.0F});
  console.registerCvar({"cl_showfps", "Show FPS, frame time, and renderer backend in the window title.", false, archivedClient, {}, {}});
  console.registerCvar({"cl_showspeed", "Show current horizontal speed in Quake units per second.", true, archivedClient, {}, {}});
  console.registerCvar({"cl_show_net", "Show network diagnostics in the window title.", true, archivedClient, {}, {}});
  console.registerCvar({"cl_show_lagcomp", "Show current and rewound LG target bounds.", false, archivedClient, {}, {}});
  console.registerCvar({"cl_show_alive_counts", "Show Clan Arena alive counts on the HUD.", false, archivedClient, {}, {}});
  console.registerCvar({"cl_interp_mode", "Remote interpolation mode: 0 legacy latest-pair, 1 buffered delay.", 1, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"cl_interp", "Remote player snapshot interpolation delay in seconds.", kDefaultSnapshotInterpolationDelaySeconds, archivedClient, 0.0F, 0.25F});
  console.registerCvar({"cl_player_name", "Local player name sent to the server.", std::string{}, archivedClient, {}, {}});
  console.registerCvar({"s_enable", "Enable client sound effects.", true, archivedClient, {}, {}});
  console.registerCvar({"s_volume", "Client sound effect volume.", 0.35F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"s_footstep_volume", "Footstep sound volume multiplier.", 0.45F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"g_accel", "Authoritative ground acceleration; affects time to reach g_maxspeed.", 10.0F, CvarFlag::Client, 0.0F, 1000.0F, "10"});
  console.registerCvar({"g_airaccel", "Authoritative air acceleration.", 1.0F, CvarFlag::Client, 0.0F, 1000.0F, "1"});
  console.registerCvar({"g_aircontrol", "Enable QuakeWorld-style air control while holding forward.", false, CvarFlag::Client, {}, {}});
  console.registerCvar({"g_friction", "Authoritative grounded coasting friction; release movement to evaluate it.", 6.0F, CvarFlag::Client, 0.0F, 100.0F, "6"});
  console.registerCvar({"g_stopspeed", "Minimum speed used when calculating grounded friction.", 2.5F, CvarFlag::Client, 0.0F, 100.0F, "2.5 (pm_stopspeed 100)"});
  console.registerCvar({"g_maxspeed", "Authoritative sustained ground and air speed cap.", 8.0F, CvarFlag::Client, 0.1F, 100.0F, "8 (g_speed 320)"});
  console.registerCvar({"g_knockback", "Authoritative LG knockback magnitude per second.", 1000.0F, CvarFlag::Client, 0.0F, 1000.0F, "1000"});
  console.registerCvar({"g_rl_knockback", "Authoritative rocket knockback on the Q3 g_knockback scale.", 1000.0F, CvarFlag::Client, 0.0F, 1000.0F, "1000"});
  console.registerCvar({"g_sg_damage", "Authoritative shotgun damage per pellet.", 5, CvarFlag::Client, 1.0F, 500.0F});
  console.registerCvar({"g_mg_damage", "Authoritative machine gun damage per shot.", 5, CvarFlag::Client, 1.0F, 500.0F});
  console.registerCvar({"g_lg_damage", "Authoritative lightning gun damage per second.", 80, CvarFlag::Client, 1.0F, 500.0F});
  console.registerCvar({"g_rg_damage", "Authoritative railgun damage per shot.", 80, CvarFlag::Client, 1.0F, 500.0F});
  console.registerCvar({"g_rl_damage", "Authoritative rocket launcher direct and max splash damage.", 100, CvarFlag::Client, 1.0F, 500.0F});
  console.registerCvar({"g_vampirism", "Heal by this multiple of authoritative damage dealt.", 0.0F, CvarFlag::Client, 0.0F, 2.0F});
  console.registerCvar({"g_selfdamage", "Percent of self splash damage you take.", 100.0F, CvarFlag::Client, 0.0F, 100.0F});
  console.registerCvar({"g_healthamount", "Authoritative player health amount on spawn and round start.", 100, CvarFlag::Client, 1.0F, 100000.0F});
  console.registerCvar({"g_flight", "Enable unrestricted flight symmetrically for both players.", false, CvarFlag::Client, {}, {}});
  console.registerCvar({"g_flightaccel", "Authoritative flight thrust acceleration.", 32.0F, CvarFlag::Client, 0.0F, 1000.0F});
  console.registerCvar({"g_flightmaxspeed", "Authoritative maximum flight speed.", 12.0F, CvarFlag::Client, 0.1F, 100.0F});
  console.registerCvar({"g_flightdamping", "Authoritative flight velocity damping.", 2.0F, CvarFlag::Client, 0.0F, 100.0F});
  console.registerCvar({"crosshair_enable", "Draw the crosshair.", true, archivedClient, {}, {}});
  console.registerCvar({"crosshair_style", "Crosshair style: 0 cross, 1 cross and dot, 2 dot.", 0, archivedClient, 0.0F, 2.0F});
  console.registerCvar({"crosshair_size", "Crosshair arm length in pixels.", 8.0F, archivedClient, 1.0F, 40.0F});
  console.registerCvar({"crosshair_thickness", "Crosshair thickness in pixels.", 2.0F, archivedClient, 1.0F, 10.0F});
  console.registerCvar({"crosshair_gap", "Crosshair center gap in pixels.", 3.0F, archivedClient, 0.0F, 30.0F});
  console.registerCvar({"crosshair_alpha", "Crosshair opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"crosshair_r", "Crosshair red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"crosshair_g", "Crosshair green channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"crosshair_b", "Crosshair blue channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"crosshair_hit_enable", "Enable crosshair hit-color feedback.", true, archivedClient, {}, {}});
  console.registerCvar({"crosshair_hit_r", "Crosshair hit-feedback red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"crosshair_hit_g", "Crosshair hit-feedback green channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"crosshair_hit_b", "Crosshair hit-feedback blue channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"crosshair_hit_duration", "Crosshair hit-color duration in seconds.", 0.12F, archivedClient, 0.0F, 2.0F});
  console.registerCvar({"crosshair_hit_fade", "Gradually blend crosshair hit color back to base.", true, archivedClient, {}, {}});
  console.registerCvar({"r_vsync", "Enable renderer vertical sync.", true, archivedClient, {}, {}});
  console.registerCvar({"g_playersize_xy", "Authoritative player X/Y radius scale.", 1.0F, CvarFlag::Client, 0.5F, 3.0F});
  console.registerCvar({"g_playersize_z", "Authoritative player height scale.", 1.0F, CvarFlag::Client, 0.5F, 3.0F});
  console.registerCvar({"r_beam_width", "Lightning beam width in pixels.", 2.0F, archivedClient, 1.0F, 12.0F});
  console.registerCvar({"r_beam_alpha", "Lightning beam opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_beam_r", "Lightning beam red channel.", 74, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_beam_g", "Lightning beam green channel.", 166, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_beam_b", "Lightning beam blue channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_beam_hit_enable", "Enable local beam hit-color feedback.", true, archivedClient, {}, {}});
  console.registerCvar({"r_beam_hit_r", "Local beam hit-feedback red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_beam_hit_g", "Local beam hit-feedback green channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_beam_hit_b", "Local beam hit-feedback blue channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_beam_hit_duration", "Local beam hit-color duration in seconds.", 0.12F, archivedClient, 0.0F, 2.0F});
  console.registerCvar({"r_beam_hit_fade", "Gradually blend beam hit color back to base.", true, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_beam_width", "Opponent lightning beam width in pixels.", 2.0F, archivedClient, 1.0F, 12.0F});
  console.registerCvar({"r_enemy_beam_alpha", "Opponent lightning beam opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_enemy_beam_r", "Opponent lightning beam red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_beam_g", "Opponent lightning beam green channel.", 110, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_beam_b", "Opponent lightning beam blue channel.", 80, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_hitmarker_enable", "Draw a center-screen hitmarker.", true, archivedClient, {}, {}});
  console.registerCvar({"r_hitmarker_duration", "Hitmarker duration in seconds.", 0.12F, archivedClient, 0.0F, 2.0F});
  console.registerCvar({"r_hitmarker_size", "Hitmarker arm length in pixels.", 10.0F, archivedClient, 2.0F, 40.0F});
  console.registerCvar({"r_hitmarker_thickness", "Hitmarker thickness in pixels.", 2.0F, archivedClient, 1.0F, 10.0F});
  console.registerCvar({"r_hitmarker_r", "Hitmarker red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_hitmarker_g", "Hitmarker green channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_hitmarker_b", "Hitmarker blue channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_r", "Enemy model red channel.", 224, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_g", "Enemy model green channel.", 82, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_b", "Enemy model blue channel.", 92, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_alpha", "Enemy model opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_enemy_outline_enable", "Draw an expanded enemy model outline.", true, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_outline_width", "Enemy model outline expansion in world units.", 0.045F, archivedClient, 0.0F, 0.5F});
  console.registerCvar({"r_enemy_outline_alpha", "Enemy model outline opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_enemy_outline_r", "Enemy model outline red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_outline_g", "Enemy model outline green channel.", 220, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_outline_b", "Enemy model outline blue channel.", 84, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_lean", "Enable Q3-style velocity lean on the enemy model.", true, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_lean_scale", "Enemy model velocity lean multiplier; 1 approximates Q3 cg_runroll.", 1.0F, archivedClient, 0.0F, 3.0F, "cg_runroll 0.005"});
  console.registerCvar({"r_enemy_hit_enable", "Enable enemy hit-color feedback.", true, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_hit_r", "Enemy hit-feedback red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_hit_g", "Enemy hit-feedback green channel.", 190, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_hit_b", "Enemy hit-feedback blue channel.", 198, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_hit_duration", "Enemy hit-color duration in seconds.", 0.12F, archivedClient, 0.0F, 2.0F});
  console.registerCvar({"r_enemy_hit_fade", "Gradually blend hit color back to the base color.", true, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_health_enable", "Draw floating enemy health bars.", true, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_health_damage_only", "Only show enemy health bars after recent damage.", false, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_health_fade", "Fade enemy health bars during their damage-only duration.", true, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_health_duration", "Seconds to show enemy health after damage.", 5.0F, archivedClient, 0.0F, 30.0F});
  console.registerCvar({"r_enemy_health_max_distance", "Hide enemy health bars beyond this 3D distance; zero disables the limit.", 0.0F, archivedClient, 0.0F, 1000.0F});
  console.registerCvar({"r_enemy_health_width", "Floating enemy health bar width in pixels.", 72.0F, archivedClient, 12.0F, 360.0F});
  console.registerCvar({"r_enemy_health_height", "Floating enemy health bar height in pixels.", 7.0F, archivedClient, 2.0F, 60.0F});
  console.registerCvar({"r_enemy_health_offset_z", "Floating enemy health bar vertical world offset above the model.", 0.35F, archivedClient, -2.0F, 6.0F});
  console.registerCvar({"r_enemy_health_offset_x", "Floating enemy health bar horizontal screen offset.", 0.0F, archivedClient, -400.0F, 400.0F});
  console.registerCvar({"r_enemy_health_offset_y", "Floating enemy health bar vertical screen offset.", -18.0F, archivedClient, -400.0F, 400.0F});
  console.registerCvar({"r_enemy_health_alpha", "Floating enemy health bar opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_enemy_health_r", "Floating enemy health bar red channel.", 224, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_health_g", "Floating enemy health bar green channel.", 82, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_health_b", "Floating enemy health bar blue channel.", 92, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_name_enable", "Draw floating enemy name tags.", true, archivedClient, {}, {}});
  console.registerCvar({"r_enemy_name_alpha", "Floating enemy name tag opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_enemy_name_font_size", "Floating enemy name tag font scale.", 1.5F, archivedClient, 0.5F, 6.0F});
  console.registerCvar({"r_enemy_name_offset_z", "Floating enemy name tag vertical world offset.", 0.75F, archivedClient, -2.0F, 6.0F});
  console.registerCvar({"r_enemy_name_offset_x", "Floating enemy name tag horizontal screen offset.", 0.0F, archivedClient, -400.0F, 400.0F});
  console.registerCvar({"r_enemy_name_offset_y", "Floating enemy name tag vertical screen offset.", -34.0F, archivedClient, -400.0F, 400.0F});
  console.registerCvar({"r_enemy_name_max_distance", "Hide enemy name tags beyond this 3D distance; zero disables the limit.", 0.0F, archivedClient, 0.0F, 1000.0F});
  console.registerCvar({"r_enemy_name_r", "Floating enemy name tag red channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_name_g", "Floating enemy name tag green channel.", 235, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_enemy_name_b", "Floating enemy name tag blue channel.", 235, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_beam_width", "Teammate lightning beam width in pixels.", 2.0F, archivedClient, 1.0F, 12.0F});
  console.registerCvar({"r_teammate_beam_alpha", "Teammate lightning beam opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_teammate_beam_r", "Teammate lightning beam red channel.", 80, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_beam_g", "Teammate lightning beam green channel.", 220, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_beam_b", "Teammate lightning beam blue channel.", 150, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_r", "Teammate model red channel.", 82, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_g", "Teammate model green channel.", 190, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_b", "Teammate model blue channel.", 224, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_alpha", "Teammate model opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_teammate_outline_enable", "Draw an expanded teammate model outline.", true, archivedClient, {}, {}});
  console.registerCvar({"r_teammate_outline_width", "Teammate model outline expansion in world units.", 0.045F, archivedClient, 0.0F, 0.5F});
  console.registerCvar({"r_teammate_outline_alpha", "Teammate model outline opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_teammate_outline_r", "Teammate model outline red channel.", 128, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_outline_g", "Teammate model outline green channel.", 240, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_outline_b", "Teammate model outline blue channel.", 255, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_lean", "Enable Q3-style velocity lean on teammate models.", true, archivedClient, {}, {}});
  console.registerCvar({"r_teammate_lean_scale", "Teammate model velocity lean multiplier.", 1.0F, archivedClient, 0.0F, 3.0F});

  console.registerCvar({"r_teammate_health_enable", "Draw floating teammate health bars.", true, archivedClient, {}, {}});
  console.registerCvar({"r_teammate_health_damage_only", "Only show teammate health bars after recent damage.", false, archivedClient, {}, {}});
  console.registerCvar({"r_teammate_health_fade", "Fade teammate health bars during their damage-only duration.", true, archivedClient, {}, {}});
  console.registerCvar({"r_teammate_health_duration", "Seconds to show teammate health after damage.", 5.0F, archivedClient, 0.0F, 30.0F});
  console.registerCvar({"r_teammate_health_max_distance", "Hide teammate health bars beyond this 3D distance; zero disables the limit.", 0.0F, archivedClient, 0.0F, 1000.0F});
  console.registerCvar({"r_teammate_health_width", "Floating teammate health bar width in pixels.", 72.0F, archivedClient, 12.0F, 360.0F});
  console.registerCvar({"r_teammate_health_height", "Floating teammate health bar height in pixels.", 7.0F, archivedClient, 2.0F, 60.0F});
  console.registerCvar({"r_teammate_health_offset_z", "Floating teammate health bar vertical world offset.", 0.35F, archivedClient, -2.0F, 6.0F});
  console.registerCvar({"r_teammate_health_offset_x", "Floating teammate health bar horizontal screen offset.", 0.0F, archivedClient, -400.0F, 400.0F});
  console.registerCvar({"r_teammate_health_offset_y", "Floating teammate health bar vertical screen offset.", -18.0F, archivedClient, -400.0F, 400.0F});
  console.registerCvar({"r_teammate_health_alpha", "Floating teammate health bar opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_teammate_health_r", "Floating teammate health bar red channel.", 82, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_health_g", "Floating teammate health bar green channel.", 190, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_health_b", "Floating teammate health bar blue channel.", 224, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_name_enable", "Draw floating teammate name tags.", true, archivedClient, {}, {}});
  console.registerCvar({"r_teammate_name_alpha", "Floating teammate name tag opacity.", 1.0F, archivedClient, 0.0F, 1.0F});
  console.registerCvar({"r_teammate_name_font_size", "Floating teammate name tag font scale.", 1.5F, archivedClient, 0.5F, 6.0F});
  console.registerCvar({"r_teammate_name_offset_z", "Floating teammate name tag vertical world offset.", 0.75F, archivedClient, -2.0F, 6.0F});
  console.registerCvar({"r_teammate_name_offset_x", "Floating teammate name tag horizontal screen offset.", 0.0F, archivedClient, -400.0F, 400.0F});
  console.registerCvar({"r_teammate_name_offset_y", "Floating teammate name tag vertical screen offset.", -34.0F, archivedClient, -400.0F, 400.0F});
  console.registerCvar({"r_teammate_name_max_distance", "Hide teammate name tags beyond this 3D distance; zero disables the limit.", 0.0F, archivedClient, 0.0F, 1000.0F});
  console.registerCvar({"r_teammate_name_r", "Floating teammate name tag red channel.", 210, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_name_g", "Floating teammate name tag green channel.", 245, archivedClient, 0.0F, 255.0F});
  console.registerCvar({"r_teammate_name_b", "Floating teammate name tag blue channel.", 255, archivedClient, 0.0F, 255.0F});
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
  settings.enemyRed = static_cast<std::uint8_t>(console.getInt("r_enemy_r"));
  settings.enemyGreen = static_cast<std::uint8_t>(console.getInt("r_enemy_g"));
  settings.enemyBlue = static_cast<std::uint8_t>(console.getInt("r_enemy_b"));
  settings.enemyAlpha = console.getFloat("r_enemy_alpha");
  settings.enemyOutlineEnabled = console.getBool("r_enemy_outline_enable");
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
  float accumulatorSeconds = 0.0F;
  float titleAccumulatorSeconds = 0.0F;
  constexpr float kWeaponSwitchDurationSeconds = 0.16F;
  float droppedSimulationSeconds = 0.0F;
  std::uint32_t overloadFrameCount = 0;
  std::uint32_t renderedFrameCount = 0;
  float displayedFramesPerSecond = 0.0F;
  MovementTuning lastRequestedMovementTuning = movementTuning(console);
  float lastRequestedPlayerSizeScaleXY =
    console.getFloat("g_playersize_xy");
  float lastRequestedPlayerSizeScaleZ =
    console.getFloat("g_playersize_z");
  float lastRequestedLightningKnockback =
    console.getFloat("g_knockback");
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
  std::array<WeaponFireResult, kDuelPlayerCount> lastPlayedWeaponFires = {};
  std::array<bool, kDuelPlayerCount> hasLastPlayedWeaponFire = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> lastPlayedRocketExplosions = {};
  std::array<bool, kDuelPlayerCount> hasLastPlayedRocketExplosion = {};
  std::uint32_t lastLocalRailFireTick = 0;
  bool hasLocalRailFireTick = false;
  bool localRailReadySoundPlayed = true;
  MatchPhase lastAudioMatchPhase = MatchPhase::WaitingForPlayers;
  std::uint32_t lastAudioCountdownSecond = 0;
  bool previousLocalHit = false;
  bool audioStateInitialized = false;
  bool hasLocalPlayerAliveState = false;
  bool wasLocalPlayerAlive = false;
  bool hasEnemyHitTime = false;
  Clock::time_point lastEnemyHitTime = {};
  std::uint8_t lastEnemyHitTarget = 255;
  std::array<int, kDuelPlayerCount> lastRemoteHealth = {};
  std::array<bool, kDuelPlayerCount> hasLastRemoteHealth = {};
  std::array<Clock::time_point, kDuelPlayerCount> lastRemoteDamageTime = {};
  std::array<bool, kDuelPlayerCount> hasLastRemoteDamageTime = {};
  std::array<LingeringRailBeam, kDuelPlayerCount> lingeringRailBeams = {};
  std::array<FootstepAudioState, kDuelPlayerCount> footstepAudioStates = {};

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
        if (chatState.inputOpen) {
          if (!pressed) {
            (void)bindings.handleKey(key, false);
            break;
          }
          if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            chatState.input.clear();
            setChatOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
            if (!chatState.input.empty()) {
              chatState.input.pop_back();
            }
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (!chatState.input.empty()) {
              chatState.pendingMessage = chatState.input;
            }
            chatState.input.clear();
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
          if (
            chatState.input.size() + std::string_view(event.text.text).size() <=
            kMaxChatMessageBytes
          ) {
            chatState.input += event.text.text;
          }
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
        } else if (!chatState.inputOpen) {
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
        chatState.history.push_back(
          "PLAYER " + std::to_string(snapshot.chatPlayerIndex + 1U) +
          ": " + snapshot.chatMessage
        );
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
    const MovementTuning currentMovementTuning = movementTuning(console);
    const float currentPlayerSizeScaleXY =
      console.getFloat("g_playersize_xy");
    const float currentPlayerSizeScaleZ =
      console.getFloat("g_playersize_z");
    const float currentLightningKnockback =
      console.getFloat("g_knockback");
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
        buildCommand(
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
      hasLastPlayedWeaponFire = {};
      lastPlayedRocketExplosions = {};
      hasLastPlayedRocketExplosion = {};
      lastLocalRailFireTick = 0;
      hasLocalRailFireTick = false;
      localRailReadySoundPlayed = true;
      previousLocalHit = false;
      hasLocalPlayerAliveState = false;
      wasLocalPlayerAlive = false;
      hasEnemyHitTime = false;
      lingeringRailBeams = {};
      footstepAudioStates = {};
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

        const bool localHit =
          audioSnapshot.lightningGuns[localPlayerIndex].hit;
        const float volume = console.getFloat("s_volume");
        const bool soundEnabled = console.getBool("s_enable");
        const float footstepVolume = volume * console.getFloat("s_footstep_volume");
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
            volume,
            audioSnapshot.lightningGuns[localPlayerIndex].damageApplied
          );
          lastHitSoundServerTick = audioSnapshot.serverTick;
        }
        if (audioStateInitialized) {
          for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
            const bool localPlayer = playerIndex == localPlayerIndex;
            const PlayerState footstepPlayer = localPlayer
              ? currentAudioGame->predictedPlayer()
              : audioSnapshot.players[playerIndex];
            updateFootstepAudio(
              footstepAudioStates[playerIndex],
              footstepPlayer,
              currentAudioGame->predictedPlayer().position,
              localPlayer,
              soundEnabled ? footstepVolume : 0.0F,
              audio
            );
          }
        }
        if (soundEnabled && audioStateInitialized) {
          for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {

            const WeaponFireResult& fire = audioSnapshot.weaponFires[playerIndex];
            const bool localWeaponEvent = playerIndex == localPlayerIndex;
            if (
              fire.fired &&
              (
                !hasLastPlayedWeaponFire[playerIndex] ||
                !sameWeaponFireEvent(fire, lastPlayedWeaponFires[playerIndex])
              )
            ) {
              const WeaponFireAudioEvent fireAudio =
                routeWeaponFireAudioEvent(fire, localWeaponEvent);
              if (fireAudio.cue == WeaponFireAudioCue::Railgun) {
                audio.playRailFire(volume);
                if (fireAudio.startsLocalRailCooldown) {
                  lastLocalRailFireTick = audioSnapshot.serverTick;
                  hasLocalRailFireTick = true;
                  localRailReadySoundPlayed = false;
                }
              } else if (fireAudio.cue == WeaponFireAudioCue::RocketLauncher) {
                audio.playRocketFire(volume);
              } else if (fireAudio.cue == WeaponFireAudioCue::MachineGun) {
                audio.playMachineGunFire(volume);
              } else if (fireAudio.cue == WeaponFireAudioCue::Shotgun) {
                audio.playShotgunFire(volume);
              } else if (fireAudio.cue == WeaponFireAudioCue::GrenadeLauncher) {
                audio.playGrenadeLauncherFire(volume);
              } else if (fireAudio.cue == WeaponFireAudioCue::PlasmaGun) {
                audio.playPlasmaGunFire(volume);
              }
              if (fireAudio.localHitConfirmDamage > 0) {
                audio.playHit(volume, fireAudio.localHitConfirmDamage);
                lastHitSoundServerTick = audioSnapshot.serverTick;
              }
              lastPlayedWeaponFires[playerIndex] = fire;
              hasLastPlayedWeaponFire[playerIndex] = true;
            }

            const RocketExplosionResult& explosion =
              audioSnapshot.rocketExplosions[playerIndex];
            if (
              explosion.active &&
              (
                !hasLastPlayedRocketExplosion[playerIndex] ||
                !sameRocketExplosionEvent(
                  explosion,
                  lastPlayedRocketExplosions[playerIndex]
                )
              )
            ) {
              audio.playRocketExplosion(volume);
              if (
                localWeaponEvent &&
                explosion.opponentDamageApplied > 0
              ) {
                audio.playHit(volume, explosion.opponentDamageApplied);
                lastHitSoundServerTick = audioSnapshot.serverTick;
              }
              lastPlayedRocketExplosions[playerIndex] = explosion;
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
            audio.playRailReady(volume);
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
          audio.playRoundResult(
            localPlayerWonResult(
              audioSnapshot,
              localPlayerIndex,
              audioSnapshot.matchPhase == MatchPhase::MatchEnd
            ),
            volume
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
          audio.playCountdown(countdownSecond, volume);
        }
        lastAudioCountdownSecond = countdownSecond;
        previousLocalHit = localHit;
        lastAudioServerTick = audioSnapshot.serverTick;
        lastAudioMatchPhase = audioSnapshot.matchPhase;
        audioStateInitialized = true;
      }
    }
    if (audioAvailable) {
      bool localLightningGunFiring = false;
      if (
        console.getBool("s_enable") &&
        currentAudioGame != nullptr &&
        currentAudioGame->hasSnapshot() &&
        currentAudioGame->predictedPlayer().health > 0
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
        localLightningGunFiring =
          currentAudioGame->snapshot().lightningGuns[localPlayerIndex].active;
      }
      audio.setLightningGunFire(
        localLightningGunFiring,
        localLightningGunFiring ? console.getFloat("s_volume") : 0.0F
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
      char title[256];
      char fpsText[96] = {};
      if (console.getBool("cl_showfps")) {
        const float frameMilliseconds = displayedFramesPerSecond > 0.0F
          ? 1000.0F / displayedFramesPerSecond
          : 0.0F;
        std::snprintf(
          fpsText,
          sizeof(fpsText),
          " | %.0f FPS %.2f ms %s",
          displayedFramesPerSecond,
          frameMilliseconds,
          std::string(renderer.backendName()).c_str()
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
        const bool teammate =
          renderSnapshot.gameMode == GameMode::ClanArena &&
          isPlayableTeam(renderSnapshot.teams[localPlayerIndex]) &&
          renderSnapshot.teams[playerIndex] ==
            renderSnapshot.teams[localPlayerIndex];
        renderRemotePlayers[playerIndex] = RemotePlayerView{
          bufferedInterpolation
            ? renderClient->interpolatedPlayer(playerIndex)
            : renderClient->interpolatedPlayer(playerIndex, interpolationAlpha),
          renderSnapshot.lightningGuns[playerIndex],
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
    if (renderLocalLightningGun.hit) {
      lastEnemyHitTime = now;
      lastEnemyHitTarget = renderLocalLightningGun.targetPlayerIndex;
      hasEnemyHitTime = true;
    }
    const float elapsedSinceHit = hasEnemyHitTime
      ? std::chrono::duration<float>(now - lastEnemyHitTime).count()
      : 0.0F;
    const auto hitFeedbackAmount =
      [&](float duration, bool fade) {
        if (renderLocalLightningGun.hit) {
          return 1.0F;
        }
        if (!hasEnemyHitTime || duration <= 0.0F || elapsedSinceHit >= duration) {
          return 0.0F;
        }
        return fade ? 1.0F - (elapsedSinceHit / duration) : 1.0F;
      };
    currentRenderSettings.enemyHitAmount = 0.0F;
    if (hasEnemyHitTime && lastEnemyHitTarget < renderRemotePlayers.size()) {
      RemotePlayerView& hitRemote = renderRemotePlayers[lastEnemyHitTarget];
      if (!hitRemote.teammate && console.getBool("r_enemy_hit_enable")) {
        hitRemote.enemyHitAmount = hitFeedbackAmount(
          console.getFloat("r_enemy_hit_duration"),
          console.getBool("r_enemy_hit_fade")
        );
      }
    }
    if (console.getBool("r_beam_hit_enable")) {
      currentRenderSettings.beamHitAmount = hitFeedbackAmount(
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
      const LightningGunResult& beam =
        session.game()->snapshot().lightningGuns[localPlayerIndex];
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
      hud.chatLines.assign(chatState.history.begin(), chatState.history.end());
    }
    hud.chatInputOpen = chatState.inputOpen;
    hud.chatInput = chatState.input;
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
    SDL_Delay(1);
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
