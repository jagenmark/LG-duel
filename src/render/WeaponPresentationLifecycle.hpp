#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lg {

// Tracks the explicit authority keys that define one first-person presentation
// timeline. Callers reset their render-only controllers when observe() returns
// true. reset() is also the replay seek and hard client reset entry point.
class WeaponPresentationLifecycle {
public:
  [[nodiscard]] bool observe(
    std::uint32_t mapRevision,
    std::optional<std::size_t> subject,
    std::uint8_t cameraMode
  ) {
    const bool changed = !initialized_ || mapRevision_ != mapRevision ||
      subject_ != subject || cameraMode_ != cameraMode;
    initialized_ = true;
    mapRevision_ = mapRevision;
    subject_ = subject;
    cameraMode_ = cameraMode;
    return changed;
  }

  void reset() { *this = {}; }

private:
  bool initialized_ = false;
  std::uint32_t mapRevision_ = 0;
  std::optional<std::size_t> subject_;
  std::uint8_t cameraMode_ = 0;
};

// Slot state stays separate from the switch controller so disconnect, slot
// reuse, and alive edges cannot leak an old weapon or hand pose into a new
// body. The body token comes from data already present in snapshots.
class RemoteWeaponPresentationLifecycle {
public:
  [[nodiscard]] bool observe(
    bool participating,
    std::string_view bodyToken,
    bool alive
  ) {
    if (!participating) {
      const bool hadBody = initialized_;
      reset();
      return hadBody;
    }
    const bool changed = !initialized_ || bodyToken_ != bodyToken || alive_ != alive;
    initialized_ = true;
    bodyToken_.assign(bodyToken);
    alive_ = alive;
    return changed;
  }

  void reset() { *this = {}; }

private:
  bool initialized_ = false;
  std::string bodyToken_;
  bool alive_ = false;
};

} // namespace lg
