#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lg::replay {

inline constexpr std::size_t kMaxReplayStemBytes = 64;

struct ReplayFileInfo {
    std::string name;
    std::filesystem::path path;
    std::uintmax_t sizeBytes = 0;
};

class ReplayStorage {
public:
    explicit ReplayStorage(std::filesystem::path directory = {});

    [[nodiscard]] static std::filesystem::path defaultDirectory();
    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

    [[nodiscard]] bool ensureDirectory(std::string* error = nullptr) const;

    // Returns a file stem without the .lgdemo suffix.
    [[nodiscard]] static std::optional<std::string> sanitizeStem(std::string_view value,
                                                                  std::string* error = nullptr);

    // Resolve a user name below the demo directory. This never creates a file.
    [[nodiscard]] bool resolveDemoPath(std::string_view value,
                                       std::filesystem::path& path,
                                       std::string* error = nullptr) const;

    [[nodiscard]] bool deleteDemo(std::string_view value, std::string* error = nullptr) const;
    [[nodiscard]] std::vector<ReplayFileInfo> list(std::string* error = nullptr) const;

    [[nodiscard]] std::string automaticStem(std::string_view mapName,
                                             std::string_view modeName) const;

private:
    std::filesystem::path directory_;
    mutable std::chrono::time_point<std::chrono::system_clock,
                                    std::chrono::milliseconds>
      lastAutomaticStemTime_ = {};
};

} // namespace lg::replay
