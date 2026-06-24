#include "app/AudioAssets.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>

namespace lg {
namespace {

struct WavFormat {
  std::uint16_t formatTag = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitsPerSample = 0;
};

[[nodiscard]] std::uint16_t readU16(const std::array<unsigned char, 4>& bytes) {
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(bytes[0]) |
    (static_cast<std::uint16_t>(bytes[1]) << 8U)
  );
}

[[nodiscard]] std::uint32_t readU32(const std::array<unsigned char, 4>& bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
    (static_cast<std::uint32_t>(bytes[1]) << 8U) |
    (static_cast<std::uint32_t>(bytes[2]) << 16U) |
    (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] bool readExact(std::ifstream& file, char* data, std::streamsize size) {
  file.read(data, size);
  return file.gcount() == size;
}

[[nodiscard]] std::optional<std::uint32_t> readChunkHeader(
  std::ifstream& file,
  std::array<char, 4>& id
) {
  std::array<unsigned char, 4> sizeBytes = {};
  if (!readExact(file, id.data(), static_cast<std::streamsize>(id.size())) ||
      !readExact(file, reinterpret_cast<char*>(sizeBytes.data()), 4)) {
    return std::nullopt;
  }
  return readU32(sizeBytes);
}

void skipChunkPadding(std::ifstream& file, std::uint32_t chunkSize) {
  if ((chunkSize & 1U) != 0U) {
    file.seekg(1, std::ios::cur);
  }
}

[[nodiscard]] bool parseFormatChunk(
  const std::vector<unsigned char>& bytes,
  WavFormat& format
) {
  if (bytes.size() < 16) {
    return false;
  }
  std::array<unsigned char, 4> scratch = {};
  scratch = {bytes[0], bytes[1], 0, 0};
  format.formatTag = readU16(scratch);
  scratch = {bytes[2], bytes[3], 0, 0};
  format.channels = readU16(scratch);
  scratch = {bytes[4], bytes[5], bytes[6], bytes[7]};
  format.sampleRate = readU32(scratch);
  scratch = {bytes[14], bytes[15], 0, 0};
  format.bitsPerSample = readU16(scratch);
  return format.channels > 0 && format.sampleRate > 0;
}

[[nodiscard]] float pcmSample(const unsigned char* bytes, std::uint16_t bitsPerSample) {
  if (bitsPerSample == 8) {
    return (static_cast<float>(*bytes) - 128.0F) / 128.0F;
  }
  if (bitsPerSample == 16) {
    const auto value = static_cast<std::int16_t>(
      static_cast<std::uint16_t>(bytes[0]) |
      (static_cast<std::uint16_t>(bytes[1]) << 8U)
    );
    return static_cast<float>(value) / 32768.0F;
  }
  return 0.0F;
}

[[nodiscard]] float floatSample(const unsigned char* bytes) {
  static_assert(sizeof(float) == 4);
  float value = 0.0F;
  auto* output = reinterpret_cast<unsigned char*>(&value);
  output[0] = bytes[0];
  output[1] = bytes[1];
  output[2] = bytes[2];
  output[3] = bytes[3];
  return std::clamp(value, -1.0F, 1.0F);
}

[[nodiscard]] std::optional<std::vector<float>> decodeSamples(
  const WavFormat& format,
  const std::vector<unsigned char>& bytes
) {
  constexpr std::uint16_t kPcmFormat = 1;
  constexpr std::uint16_t kIeeeFloatFormat = 3;
  const bool supportedPcm =
    format.formatTag == kPcmFormat &&
    (format.bitsPerSample == 8 || format.bitsPerSample == 16);
  const bool supportedFloat =
    format.formatTag == kIeeeFloatFormat && format.bitsPerSample == 32;
  if (!supportedPcm && !supportedFloat) {
    return std::nullopt;
  }

  const std::size_t bytesPerSample = static_cast<std::size_t>(format.bitsPerSample / 8U);
  const std::size_t frameSize = bytesPerSample * format.channels;
  if (frameSize == 0 || bytes.size() < frameSize) {
    return std::nullopt;
  }
  const std::size_t frameCount = bytes.size() / frameSize;
  std::vector<float> samples(frameCount);
  for (std::size_t frame = 0; frame < frameCount; ++frame) {
    float mixed = 0.0F;
    const unsigned char* frameBytes = bytes.data() + (frame * frameSize);
    for (std::uint16_t channel = 0; channel < format.channels; ++channel) {
      const unsigned char* sampleBytes = frameBytes + (channel * bytesPerSample);
      mixed += supportedFloat
        ? floatSample(sampleBytes)
        : pcmSample(sampleBytes, format.bitsPerSample);
    }
    samples[frame] =
      std::clamp(mixed / static_cast<float>(format.channels), -1.0F, 1.0F);
  }
  return samples;
}

} // namespace

const char* audioCueFileName(AudioCue cue) {
  switch (cue) {
  case AudioCue::LightningGunFireLoop:
    return "lg_fire_selected_low_drone.wav";
  case AudioCue::MachineGunFire:
    return "mg_fire_selected_snap.wav";
  case AudioCue::ShotgunFire:
    return "sg_fire_selected_blast.wav";
  case AudioCue::GrenadeLauncherFire:
    return "gl_fire_selected_thump.wav";
  case AudioCue::PlasmaGunFire:
    return "pg_fire_selected_pulse.wav";
  case AudioCue::Footstep:
    return "footstep_preview_01_concrete_snap.wav";
  }
  return "";
}

std::filesystem::path audioCuePath(
  const std::filesystem::path& basePath,
  AudioCue cue
) {
  return basePath / "assets" / "audio" / audioCueFileName(cue);
}

std::optional<AudioClip> loadAudioCue(
  const std::filesystem::path& basePath,
  AudioCue cue
) {
  return loadWavFile(audioCuePath(basePath, cue));
}

std::optional<AudioClip> loadWavFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }

  std::array<char, 4> id = {};
  std::array<unsigned char, 4> sizeBytes = {};
  std::array<char, 4> wave = {};
  if (!readExact(file, id.data(), 4) ||
      !readExact(file, reinterpret_cast<char*>(sizeBytes.data()), 4) ||
      !readExact(file, wave.data(), 4) ||
      id != std::array<char, 4>{'R', 'I', 'F', 'F'} ||
      wave != std::array<char, 4>{'W', 'A', 'V', 'E'}) {
    return std::nullopt;
  }

  WavFormat format;
  bool hasFormat = false;
  std::vector<unsigned char> sampleBytes;
  while (file) {
    auto chunkSize = readChunkHeader(file, id);
    if (!chunkSize) {
      break;
    }
    if (*chunkSize > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    std::vector<unsigned char> chunk(*chunkSize);
    if (!chunk.empty() &&
        !readExact(file, reinterpret_cast<char*>(chunk.data()), *chunkSize)) {
      return std::nullopt;
    }
    skipChunkPadding(file, *chunkSize);
    if (id == std::array<char, 4>{'f', 'm', 't', ' '}) {
      hasFormat = parseFormatChunk(chunk, format);
    } else if (id == std::array<char, 4>{'d', 'a', 't', 'a'}) {
      sampleBytes = std::move(chunk);
    }
  }

  if (!hasFormat || sampleBytes.empty()) {
    return std::nullopt;
  }
  std::optional<std::vector<float>> samples = decodeSamples(format, sampleBytes);
  if (!samples || samples->empty()) {
    return std::nullopt;
  }
  return AudioClip{
    std::move(*samples),
    static_cast<int>(format.sampleRate),
    path,
  };
}

} // namespace lg
