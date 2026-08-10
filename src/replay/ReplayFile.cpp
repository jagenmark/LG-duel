#include "replay/ReplayFile.hpp"

#include "replay/ReplayCodec.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lg::replay {
namespace {

bool fail(std::string *error, const char *message) {
  if (error != nullptr)
    *error = message;
  return false;
}

bool validDemoPath(const std::filesystem::path &path) {
  return !path.empty() && path.extension() == ".lgdemo";
}

std::filesystem::path temporaryPath(const std::filesystem::path &path,
                                    std::uint64_t nonce) {
  std::filesystem::path temporary = path;
  temporary += ".partial." + std::to_string(nonce);
  return temporary;
}

void removeQuietly(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

std::uint64_t randomNonce(std::random_device &random) {
  const std::uint64_t high = static_cast<std::uint64_t>(random());
  const std::uint64_t low = static_cast<std::uint64_t>(random());
  return (high << 32U) ^ low ^ static_cast<std::uint64_t>(
    std::chrono::steady_clock::now().time_since_epoch().count()
  );
}

bool writeExclusive(const std::filesystem::path &path,
                    const std::vector<std::uint8_t> &bytes,
                    bool &collision) {
  collision = false;
#if defined(_WIN32)
  const HANDLE file = CreateFileW(
    path.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_NEW,
    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr
  );
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD result = GetLastError();
    collision = result == ERROR_FILE_EXISTS || result == ERROR_ALREADY_EXISTS;
    return false;
  }
  std::size_t offset = 0U;
  bool written = true;
  while (offset < bytes.size()) {
    const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
      bytes.size() - offset, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())
    ));
    DWORD count = 0U;
    if (!WriteFile(file, bytes.data() + offset, request, &count, nullptr) || count != request) {
      written = false;
      break;
    }
    offset += count;
  }
  written = written && FlushFileBuffers(file) != 0;
  CloseHandle(file);
  return written;
#else
  const int file = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
  if (file < 0) {
    collision = errno == EEXIST;
    return false;
  }
  std::size_t offset = 0U;
  bool written = true;
  while (offset < bytes.size()) {
    const ssize_t count = write(file, bytes.data() + offset, bytes.size() - offset);
    if (count <= 0) {
      written = false;
      break;
    }
    offset += static_cast<std::size_t>(count);
  }
  written = written && fsync(file) == 0;
  close(file);
  return written;
#endif
}

} // namespace

bool saveDemoFile(const std::filesystem::path &path, const ReplayDemo &demo,
                  std::string *error) {
  if (!validDemoPath(path))
    return fail(error, "replay file path must end in .lgdemo");
  std::vector<std::uint8_t> bytes;
  if (!encodeDemo(demo, bytes, error))
    return false;

  const std::filesystem::path parent =
      path.has_parent_path() ? path.parent_path() : ".";
  std::error_code filesystemError;
  if (!std::filesystem::is_directory(parent, filesystemError) ||
      filesystemError) {
    return fail(error, "replay output directory is unavailable");
  }
  if (std::filesystem::exists(path, filesystemError) || filesystemError) {
    return fail(error, "refusing to replace an existing replay file");
  }
  std::filesystem::path temporary;
  bool created = false;
  try {
    std::random_device random;
    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
      temporary = temporaryPath(path, randomNonce(random));
      bool collision = false;
      if (writeExclusive(temporary, bytes, collision)) {
        created = true;
        break;
      }
      if (!collision) {
        removeQuietly(temporary);
        return fail(error, "could not create replay temporary file");
      }
    }
  } catch (...) {
    if (!temporary.empty()) removeQuietly(temporary);
    return fail(error, "could not create replay temporary file");
  }
  if (!created) return fail(error, "could not reserve a unique replay temporary file");

  // Linking creates the final name only if it does not already exist. Unlike
  // rename, it cannot replace a racing user file on platforms that replace a
  // rename target. The link and its source share a directory and volume.
  std::filesystem::create_hard_link(temporary, path, filesystemError);
  if (filesystemError) {
    removeQuietly(temporary);
    return fail(error, "could not publish replay file");
  }
  removeQuietly(temporary);
  if (error != nullptr)
    error->clear();
  return true;
}

bool loadDemoFile(const std::filesystem::path &path, ReplayDemo &demo,
                  std::string *error) {
  if (!validDemoPath(path))
    return fail(error, "replay file path must end in .lgdemo");
  std::error_code filesystemError;
  if (!std::filesystem::is_regular_file(path, filesystemError) ||
      filesystemError) {
    return fail(error, "replay file does not exist");
  }
  const std::uintmax_t size = std::filesystem::file_size(path, filesystemError);
  if (filesystemError || size == 0U || size > kMaxReplayBytes) {
    return fail(error, "replay file size is invalid");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::ifstream stream(path, std::ios::binary | std::ios::in);
  if (!stream.is_open())
    return fail(error, "could not open replay file");
  stream.read(reinterpret_cast<char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return fail(error, "replay file changed or could not be read");
  }
  return decodeDemo(bytes, demo, error);
}

} // namespace lg::replay
