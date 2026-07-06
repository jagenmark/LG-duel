#pragma once

#include "app/AudioAssets.hpp"
#include "sim/PlayerState.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <random>
#include <vector>

struct SDL_AudioStream;

namespace lg {

struct FootstepAudioState {
  Vec3 previousPosition = {};
  float distanceSinceStep = 0.0F;
  std::uint32_t stepIndex = 0;
  bool wasOnGround = false;
  bool initialized = false;
};

struct SpatialAudio {
  float volume = 0.0F;
  float pan = 0.0F;
};

class ClientAudio {
public:
  bool initialize(const std::filesystem::path& assetBasePath);

  void playHit(float volume, int damageApplied, bool headshot = false);
  void playFrag(float volume);
  void playPainGrunt(float volume, float pan = 0.0F);
  void playRailFire(float volume, float pan = 0.0F);
  void playRailReady(float volume);
  void playRocketFire(float volume, float pan = 0.0F);
  void playMachineGunFire(float volume, float pan = 0.0F);
  void playShotgunFire(float volume, float pan = 0.0F);
  void playGrenadeLauncherFire(float volume, float pan = 0.0F);
  void playGrenadeBounce(float volume, float pan = 0.0F);
  void playPlasmaGunFire(float volume, float pan = 0.0F);
  void playRocketExplosion(float volume, float pan = 0.0F);
  void playFootstep(float volume, std::uint32_t stepIndex, float pan = 0.0F);
  void playJump(float volume, float pan = 0.0F);
  void playLand(float volume, float pan = 0.0F);
  void setLightningGunFire(bool active, float volume, float pan = 0.0F);
  void resetLightningGunFire();
  void playRoundResult(bool won, float volume);
  void playCountdown(std::uint32_t seconds, float volume);
  void update();
  void shutdown();

private:
  struct SoundVoice {
    std::vector<float> samples;
    std::size_t playhead = 0;
  };

  struct LoadedClip {
    std::vector<float> samples;
  };

  void loadCueAssets(const std::filesystem::path& assetBasePath);
  [[nodiscard]] static LoadedClip loadCueClip(
    const std::filesystem::path& assetBasePath,
    AudioCue cue
  );
  [[nodiscard]] static LoadedClip loadClipFile(
    const std::filesystem::path& path
  );
  [[nodiscard]] static std::vector<LoadedClip> loadFootstepClips(
    const std::filesystem::path& assetBasePath
  );
  [[nodiscard]] static std::vector<float> resampleToMixerRate(
    const AudioClip& clip
  );
  bool queueClip(const LoadedClip& clip, float volume, float pan);
  [[nodiscard]] float lightningGunSample();
  void mixLightningGunLoop(std::vector<float>& buffer);
  [[nodiscard]] bool hasActiveLoop() const;
  void addVoice(std::vector<float> samples);
  void pumpAudio();
  void removeFinishedVoices();

  [[nodiscard]] static float footstepGain(std::uint32_t stepIndex);
  [[nodiscard]] static std::size_t remainingFrames(const SoundVoice& voice);

  static constexpr int kSampleRate = 48000;
  static constexpr int kOutputChannels = 2;
  static constexpr int kMixChunkSamples = 256;
  static constexpr int kTargetQueuedSamples = 1024;
  static constexpr std::size_t kMaxActiveVoices = 32;
  SDL_AudioStream* stream_ = nullptr;
  std::vector<SoundVoice> voices_;
  std::vector<float> mixBuffer_;
  LoadedClip lightningGunLoop_;
  LoadedClip hitConfirmLightClip_;
  LoadedClip hitConfirmMediumClip_;
  LoadedClip hitConfirmHeavyClip_;
  LoadedClip headshotConfirmClip_;
  LoadedClip fragClip_;
  LoadedClip painGruntClip_;
  LoadedClip railgunFireClip_;
  LoadedClip railgunReadyClip_;
  LoadedClip rocketLauncherFireClip_;
  LoadedClip rocketExplosionClip_;
  LoadedClip machineGunFireClip_;
  LoadedClip shotgunFireClip_;
  LoadedClip grenadeLauncherFireClip_;
  LoadedClip grenadeBounceClip_;
  LoadedClip plasmaGunFireClip_;
  std::vector<LoadedClip> footstepClips_;
  LoadedClip jumpClip_;
  LoadedClip landClip_;
  LoadedClip roundWinClip_;
  LoadedClip roundLossClip_;
  LoadedClip countdownFiveClip_;
  LoadedClip countdownFourClip_;
  LoadedClip countdownThreeClip_;
  LoadedClip countdownTwoClip_;
  LoadedClip countdownOneClip_;
  bool lightningGunFireActive_ = false;
  float lightningGunFireTargetVolume_ = 0.0F;
  float lightningGunFireTargetPan_ = 0.0F;
  float lightningGunFireGain_ = 0.0F;
  float lightningGunFirePan_ = 0.0F;
  std::uint64_t lightningGunSampleIndex_ = 0;
  std::size_t painGruntFramesRemaining_ = 0;
  std::mt19937 footstepRng_{std::random_device{}()};
  std::size_t lastFootstepClipIndex_ = std::numeric_limits<std::size_t>::max();
};

[[nodiscard]] std::size_t selectFootstepVariantIndex(
  std::size_t variantCount,
  std::size_t previousVariantIndex,
  std::uint32_t randomValue
);

[[nodiscard]] SpatialAudio worldAudio(
  float baseVolume,
  Vec3 sourcePosition,
  const PlayerState& listener
);

void updateFootstepAudio(
  FootstepAudioState& state,
  const PlayerState& player,
  const PlayerState& listener,
  bool localPlayer,
  float volume,
  ClientAudio& audio
);

} // namespace lg
