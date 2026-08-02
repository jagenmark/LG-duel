#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lg {

enum class AudioCue {
  LightningGunFireLoop,
  HitConfirmLight,
  HitConfirmMedium,
  HitConfirmHeavy,
  HeadshotConfirm,
  PainGrunt,
  Frag,
  RailgunFire,
  RevolverFire,
  RailgunReady,
  RocketLauncherFire,
  RocketExplosion,
  MachineGunFire,
  ShotgunFire,
  GrenadeLauncherFire,
  GrenadeBounce,
  PlasmaGunFire,
  Footstep,
  Jump,
  Land,
  RoundWin,
  RoundLoss,
  CountdownFive,
  CountdownFour,
  CountdownThree,
  CountdownTwo,
  CountdownOne,
};

struct AudioClip {
  std::vector<float> samples;
  int sampleRate = 0;
  std::filesystem::path sourcePath;
};

[[nodiscard]] const char* audioCueFileName(AudioCue cue);
[[nodiscard]] std::filesystem::path audioCuePath(
  const std::filesystem::path& basePath,
  AudioCue cue
);
[[nodiscard]] std::vector<std::filesystem::path> footstepCuePaths(
  const std::filesystem::path& basePath
);
[[nodiscard]] std::optional<AudioClip> loadAudioCue(
  const std::filesystem::path& basePath,
  AudioCue cue
);
[[nodiscard]] std::optional<AudioClip> loadAudioFile(
  const std::filesystem::path& path
);
[[nodiscard]] std::optional<AudioClip> loadWavFile(
  const std::filesystem::path& path
);
[[nodiscard]] std::optional<AudioClip> loadOggFile(
  const std::filesystem::path& path
);

} // namespace lg
