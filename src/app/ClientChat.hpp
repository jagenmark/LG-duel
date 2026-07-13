#pragma once

#include "net/NetProtocol.hpp"

#include <string>

namespace lg {

[[nodiscard]] inline std::string chatPlayerDisplayName(
  const ServerSnapshot& snapshot,
  std::size_t playerIndex
) {
  if (
    playerIndex < snapshot.playerNames.size() &&
    !snapshot.playerNames[playerIndex].empty()
  ) {
    return snapshot.playerNames[playerIndex];
  }
  return "PLAYER " + std::to_string(playerIndex + 1U);
}

[[nodiscard]] inline std::string chatLine(const ChatMessage& message) {
  const std::string speaker = !message.speakerName.empty()
    ? message.speakerName
    : "PLAYER " + std::to_string(message.playerIndex + 1U);
  return speaker + ": " + message.message;
}

} // namespace lg
