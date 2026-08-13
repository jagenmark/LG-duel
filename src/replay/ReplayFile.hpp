#pragma once

#include "replay/ReplayTypes.hpp"

#include <filesystem>
#include <string>

namespace lg::replay {

// File operations perform encoding, allocation, and blocking disk I/O. Call
// them from a save/load job, never from ServerGame::tick. Saving only creates
// a new .lgdemo file; it never replaces an existing user recording.
[[nodiscard]] bool saveDemoFile(const std::filesystem::path &path,
                                const ReplayDemo &demo,
                                std::string *error = nullptr);
[[nodiscard]] bool loadDemoFile(const std::filesystem::path &path,
                                ReplayDemo &demo, std::string *error = nullptr);

} // namespace lg::replay
