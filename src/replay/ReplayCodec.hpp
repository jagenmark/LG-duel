#pragma once

#include "replay/ReplayTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lg::replay {

// Every field uses fixed-width little-endian encoding.  Decoding never mutates
// the destination until it has checked the full file and all chunk checksums.
[[nodiscard]] bool encodeDemo(const ReplayDemo& demo, std::vector<std::uint8_t>& bytes,
                              std::string* error = nullptr);
[[nodiscard]] bool decodeDemo(const std::vector<std::uint8_t>& bytes, ReplayDemo& demo,
                              std::string* error = nullptr);
[[nodiscard]] std::uint64_t canonicalStateHash(const ReplayCheckpoint& checkpoint);

} // namespace lg::replay
