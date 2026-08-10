#include "replay/ReplayFile.hpp"

#include "replay/ReplayCodec.hpp"

#include <fstream>
#include <system_error>
#include <vector>

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

std::filesystem::path temporaryPath(const std::filesystem::path &path) {
  std::filesystem::path temporary = path;
  temporary += ".partial";
  return temporary;
}

void removeQuietly(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
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
  const std::filesystem::path temporary = temporaryPath(path);
  std::error_code filesystemError;
  if (!std::filesystem::is_directory(parent, filesystemError) ||
      filesystemError) {
    return fail(error, "replay output directory is unavailable");
  }
  if (std::filesystem::exists(path, filesystemError) || filesystemError) {
    return fail(error, "refusing to replace an existing replay file");
  }
  if (std::filesystem::exists(temporary, filesystemError) || filesystemError) {
    return fail(error, "replay temporary file already exists");
  }

  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::out);
    if (!stream.is_open())
      return fail(error, "could not create replay temporary file");
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream.good()) {
      stream.close();
      removeQuietly(temporary);
      return fail(error, "could not write replay temporary file");
    }
  }

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
