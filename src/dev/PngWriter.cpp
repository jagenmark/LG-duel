#include "dev/PngWriter.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <vector>

namespace lg::dev {
namespace {

void appendBigEndian(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

[[nodiscard]] std::uint32_t adler32(std::span<const std::uint8_t> bytes) {
  constexpr std::uint32_t modulus = 65521U;
  std::uint32_t a = 1U;
  std::uint32_t b = 0U;
  for (const std::uint8_t byte : bytes) {
    a = (a + byte) % modulus;
    b = (b + a) % modulus;
  }
  return (b << 16U) | a;
}

void appendChunk(
  std::vector<std::uint8_t>& png,
  const std::array<std::uint8_t, 4>& type,
  std::span<const std::uint8_t> data
) {
  appendBigEndian(png, static_cast<std::uint32_t>(data.size()));
  const std::size_t crcStart = png.size();
  png.insert(png.end(), type.begin(), type.end());
  png.insert(png.end(), data.begin(), data.end());
  appendBigEndian(png, crc32(std::span<const std::uint8_t>(png).subspan(crcStart)));
}

} // namespace

bool writeRgbaPng(
  const std::string& path,
  std::uint32_t width,
  std::uint32_t height,
  std::span<const std::uint8_t> rgbaPixels,
  std::string& error
) {
  if (width == 0U || height == 0U) {
    error = "capture dimensions must be non-zero";
    return false;
  }
  const std::uint64_t pixelBytes = static_cast<std::uint64_t>(width) * height * 4U;
  if (pixelBytes != rgbaPixels.size() || pixelBytes > std::numeric_limits<std::uint32_t>::max()) {
    error = "capture pixel buffer size does not match its dimensions";
    return false;
  }

  const std::uint64_t scanlineBytes64 = static_cast<std::uint64_t>(width) * 4U + 1U;
  const std::uint64_t filteredBytes64 = scanlineBytes64 * height;
  if (filteredBytes64 > std::numeric_limits<std::uint32_t>::max()) {
    error = "capture is too large for the PNG writer";
    return false;
  }
  const std::size_t scanlineBytes = static_cast<std::size_t>(scanlineBytes64);
  std::vector<std::uint8_t> filtered(static_cast<std::size_t>(filteredBytes64));
  for (std::uint32_t row = 0; row < height; ++row) {
    const std::size_t destination = static_cast<std::size_t>(row) * scanlineBytes;
    filtered[destination] = 0U;
    const std::size_t source = static_cast<std::size_t>(row) * width * 4U;
    std::copy_n(
      rgbaPixels.begin() + static_cast<std::ptrdiff_t>(source),
      static_cast<std::size_t>(width) * 4U,
      filtered.begin() + static_cast<std::ptrdiff_t>(destination + 1U)
    );
  }

  std::vector<std::uint8_t> compressed;
  compressed.reserve(filtered.size() + (filtered.size() / 65535U + 1U) * 5U + 6U);
  compressed.push_back(0x78U);
  compressed.push_back(0x01U);
  std::size_t offset = 0;
  while (offset < filtered.size()) {
    const std::size_t count = std::min<std::size_t>(65535U, filtered.size() - offset);
    const bool finalBlock = offset + count == filtered.size();
    compressed.push_back(finalBlock ? 1U : 0U);
    const std::uint16_t length = static_cast<std::uint16_t>(count);
    const std::uint16_t inverse = static_cast<std::uint16_t>(~length);
    compressed.push_back(static_cast<std::uint8_t>(length));
    compressed.push_back(static_cast<std::uint8_t>(length >> 8U));
    compressed.push_back(static_cast<std::uint8_t>(inverse));
    compressed.push_back(static_cast<std::uint8_t>(inverse >> 8U));
    compressed.insert(
      compressed.end(),
      filtered.begin() + static_cast<std::ptrdiff_t>(offset),
      filtered.begin() + static_cast<std::ptrdiff_t>(offset + count)
    );
    offset += count;
  }
  appendBigEndian(compressed, adler32(filtered));

  std::vector<std::uint8_t> png = {137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U};
  std::vector<std::uint8_t> header;
  header.reserve(13);
  appendBigEndian(header, width);
  appendBigEndian(header, height);
  header.insert(header.end(), {8U, 6U, 0U, 0U, 0U});
  appendChunk(png, {'I', 'H', 'D', 'R'}, header);
  appendChunk(png, {'I', 'D', 'A', 'T'}, compressed);
  appendChunk(png, {'I', 'E', 'N', 'D'}, {});

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not open capture output '" + path + "'";
    return false;
  }
  output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  if (!output) {
    error = "failed while writing capture output '" + path + "'";
    return false;
  }
  return true;
}

} // namespace lg::dev
