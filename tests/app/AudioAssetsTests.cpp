#include "app/AudioAssets.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void expect(bool condition, const char* context) {
  if (condition) {
    return;
  }
  std::cerr << context << '\n';
  std::exit(1);
}

void writeU16(std::ofstream& file, std::uint16_t value) {
  const std::array<char, 2> bytes{
    static_cast<char>(value & 0xFFU),
    static_cast<char>((value >> 8U) & 0xFFU),
  };
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ofstream& file, std::uint32_t value) {
  const std::array<char, 4> bytes{
    static_cast<char>(value & 0xFFU),
    static_cast<char>((value >> 8U) & 0xFFU),
    static_cast<char>((value >> 16U) & 0xFFU),
    static_cast<char>((value >> 24U) & 0xFFU),
  };
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeStereoPcm16Wav(const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  constexpr std::uint16_t channels = 2;
  constexpr std::uint32_t sampleRate = 48000;
  constexpr std::uint16_t bitsPerSample = 16;
  constexpr std::uint32_t dataBytes = 8;
  file.write("RIFF", 4);
  writeU32(file, 36U + dataBytes);
  file.write("WAVE", 4);
  file.write("fmt ", 4);
  writeU32(file, 16);
  writeU16(file, 1);
  writeU16(file, channels);
  writeU32(file, sampleRate);
  writeU32(file, sampleRate * channels * (bitsPerSample / 8U));
  writeU16(file, channels * (bitsPerSample / 8U));
  writeU16(file, bitsPerSample);
  file.write("data", 4);
  writeU32(file, dataBytes);
  writeU16(file, 32767);
  writeU16(file, 32767);
  writeU16(file, 0);
  writeU16(file, 0);
}

} // namespace

int main() {
  expect(
    std::string{lg::audioCueFileName(lg::AudioCue::LightningGunFireLoop)} ==
      "lg_fire_selected_low_drone.wav",
    "lightning gun cue should map to the selected checked-in WAV"
  );
  expect(
    std::string{lg::audioCueFileName(lg::AudioCue::Footstep)} ==
      "footstep_preview_01_concrete_snap.wav",
    "footstep cue should map to the selected checked-in WAV"
  );
  expect(
    std::string{lg::audioCueFileName(lg::AudioCue::MachineGunFire)} ==
      "mg_fire_selected_snap.wav",
    "machine gun cue should map to the selected checked-in WAV"
  );
  expect(
    std::string{lg::audioCueFileName(lg::AudioCue::ShotgunFire)} ==
      "sg_fire_selected_blast.wav",
    "shotgun cue should map to the selected checked-in WAV"
  );
  expect(
    std::string{lg::audioCueFileName(lg::AudioCue::GrenadeLauncherFire)} ==
      "gl_fire_selected_thump.wav",
    "grenade launcher cue should map to the selected checked-in WAV"
  );
  expect(
    std::string{lg::audioCueFileName(lg::AudioCue::PlasmaGunFire)} ==
      "pg_fire_selected_pulse.wav",
    "plasma gun cue should map to the selected checked-in WAV"
  );

  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "lg_duel_audio_assets_test";
  std::filesystem::remove_all(root);
  expect(
    !lg::loadAudioCue(root, lg::AudioCue::LightningGunFireLoop).has_value(),
    "missing cue WAV should fall back without loading a clip"
  );

  const std::filesystem::path cuePath =
    lg::audioCuePath(root, lg::AudioCue::LightningGunFireLoop);
  writeStereoPcm16Wav(cuePath);
  const auto clip = lg::loadAudioCue(root, lg::AudioCue::LightningGunFireLoop);
  expect(clip.has_value(), "valid PCM16 cue WAV should load");
  expect(clip->sampleRate == 48000, "loaded cue should keep the WAV sample rate");
  expect(clip->samples.size() == 2, "stereo WAV should downmix to mono frames");
  expect(clip->samples[0] > 0.99F, "first downmixed sample should preserve signal");
  expect(clip->samples[1] == 0.0F, "second downmixed sample should preserve silence");

  std::filesystem::remove_all(root);
  return 0;
}
