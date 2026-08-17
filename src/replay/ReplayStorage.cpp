#include "replay/ReplayStorage.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace lg::replay {

namespace {

constexpr std::string_view kExtension = ".lgdemo";

void setError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool hasReservedStem(std::string_view value) {
    std::string upper;
    upper.reserve(value.size());
    for (const char c : value) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") {
        return true;
    }
    if (upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) &&
        upper[3] >= '1' && upper[3] <= '9') {
        return true;
    }
    return false;
}

std::string safeToken(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            result.push_back(c);
        } else {
            result.push_back('_');
        }
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    if (result.empty()) {
        result = "map";
    }
    if (result.size() > 20) {
        result.resize(20);
    }
    return result;
}

} // namespace

ReplayStorage::ReplayStorage(std::filesystem::path directory)
    : directory_(directory.empty() ? defaultDirectory() : std::move(directory)) {}

std::filesystem::path ReplayStorage::defaultDirectory() {
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "LG Duel" / "LG Duel" / "demos";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" / "Application Support" /
            "LG Duel" / "LG Duel" / "demos";
    }
#else
    if (const char* dataHome = std::getenv("XDG_DATA_HOME"); dataHome != nullptr && *dataHome != '\0') {
        return std::filesystem::path(dataHome) / "LG Duel" / "LG Duel" / "demos";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "share" /
            "LG Duel" / "LG Duel" / "demos";
    }
#endif
    return std::filesystem::current_path() / "LG Duel" / "LG Duel" / "demos";
}

bool ReplayStorage::ensureDirectory(std::string* error) const {
    std::error_code ec;
    if (std::filesystem::exists(directory_, ec)) {
        if (ec || !std::filesystem::is_directory(directory_, ec)) {
            setError(error, "demo path is not a directory: " + directory_.string());
            return false;
        }
        return true;
    }
    if (ec || !std::filesystem::create_directories(directory_, ec) || ec) {
        setError(error, "could not create demo directory: " + directory_.string());
        return false;
    }
    return true;
}

std::optional<std::string> ReplayStorage::sanitizeStem(std::string_view value,
                                                        std::string* error) {
    if (value.empty()) {
        setError(error, "demo name is empty");
        return std::nullopt;
    }

    std::string stem(value);
    if (stem.size() >= kExtension.size() &&
        stem.compare(stem.size() - kExtension.size(), kExtension.size(), kExtension) == 0) {
        stem.resize(stem.size() - kExtension.size());
    }
    if (stem.empty()) {
        setError(error, "demo name is empty");
        return std::nullopt;
    }
    if (stem.size() > kMaxReplayStemBytes) {
        setError(error, "demo name is too long");
        return std::nullopt;
    }
    if (stem == "." || stem == ".." || stem.find("..") != std::string::npos) {
        setError(error, "demo name contains a parent path");
        return std::nullopt;
    }
    if (stem.find('/') != std::string::npos || stem.find('\\') != std::string::npos ||
        stem.find(':') != std::string::npos) {
        setError(error, "demo name must be one file name");
        return std::nullopt;
    }
    if (hasReservedStem(stem)) {
        setError(error, "demo name is reserved");
        return std::nullopt;
    }
    for (const unsigned char c : stem) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            setError(error, "demo name has an invalid character");
            return std::nullopt;
        }
    }
    return stem;
}

bool ReplayStorage::resolveDemoPath(std::string_view value,
                                    std::filesystem::path& path,
                                    std::string* error) const {
    const auto stem = sanitizeStem(value, error);
    if (!stem.has_value()) {
        return false;
    }
    path = directory_ / (*stem + std::string(kExtension));
    return true;
}

bool ReplayStorage::deleteDemo(std::string_view value, std::string* error) const {
    std::filesystem::path path;
    if (!resolveDemoPath(value, path, error)) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        setError(error, "demo does not exist: " + path.filename().string());
        return false;
    }
    if (ec || !std::filesystem::is_regular_file(path, ec)) {
        setError(error, "demo is not a regular file: " + path.filename().string());
        return false;
    }
    if (!std::filesystem::remove(path, ec) || ec) {
        setError(error, "could not delete demo: " + path.filename().string());
        return false;
    }
    return true;
}

std::vector<ReplayFileInfo> ReplayStorage::list(std::string* error) const {
    std::vector<ReplayFileInfo> result;
    if (!ensureDirectory(error)) {
        return result;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, ec)) {
        if (ec) {
            setError(error, "could not list demo directory: " + directory_.string());
            return {};
        }
        if (!entry.is_regular_file(ec) || ec || entry.path().extension() != kExtension) {
            continue;
        }
        const std::string rawStem = entry.path().stem().string();
        if (rawStem.find('.') != std::string::npos) {
            continue;
        }
        std::string stemError;
        const auto stem = sanitizeStem(rawStem, &stemError);
        if (!stem.has_value()) {
            continue;
        }
        const auto size = entry.file_size(ec);
        if (ec) {
            continue;
        }
        result.push_back(ReplayFileInfo{*stem, entry.path(), size});
    }
    std::sort(result.begin(), result.end(), [](const ReplayFileInfo& left, const ReplayFileInfo& right) {
        return left.name < right.name;
    });
    return result;
}

std::string ReplayStorage::automaticStem(std::string_view mapName, std::string_view modeName) const {
    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now()
    );
    if (now <= lastAutomaticStemTime_) {
        now = lastAutomaticStemTime_ + std::chrono::milliseconds(1);
    }
    lastAutomaticStemTime_ = now;

    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count() % 1000;
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif

    std::ostringstream timestamp;
    timestamp << std::put_time(&utc, "%Y%m%dT%H%M%S")
              << std::setfill('0') << std::setw(3) << milliseconds << 'Z';
    std::string value = timestamp.str() + "-" + safeToken(mapName) + "-" + safeToken(modeName);
    if (value.size() > kMaxReplayStemBytes) {
        value.resize(kMaxReplayStemBytes);
    }
    return value;
}

} // namespace lg::replay
