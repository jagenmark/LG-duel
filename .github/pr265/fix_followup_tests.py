from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file_path = Path(path)
    text = file_path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}")
    file_path.write_text(text.replace(old, new, 1), encoding="utf-8")


# Slot zero is the local-playback default and may fall back to the first
# occupied recorded player. A caller-selected empty/nonexistent slot is still
# an invalid request and must not silently change subjects.
replace_once(
    "src/replay/ReplayPresentationSession.cpp",
    '''  if (initialFollowSlot >= followable_.size()) {
    state_ = {};
    state_.stopReason = ReplayPresentationStopReason::InvalidDemo;
    return fail(error, "replay presentation follow slot is outside the player table");
  }
  std::uint8_t followSlot = initialFollowSlot;
  if (!followable_[followSlot]) {
    const auto fallback = std::find(followable_.begin(), followable_.end(), true);
    if (fallback == followable_.end()) {
      state_ = {};
      state_.stopReason = ReplayPresentationStopReason::InvalidDemo;
      return fail(error, "replay presentation has no recorded player to follow");
    }
    followSlot = static_cast<std::uint8_t>(
      std::distance(followable_.begin(), fallback)
    );
  }
''',
    '''  if (initialFollowSlot >= followable_.size() ||
      (!followable_[initialFollowSlot] && initialFollowSlot != 0U)) {
    state_ = {};
    state_.stopReason = ReplayPresentationStopReason::InvalidDemo;
    return fail(error, "replay presentation follow slot is not recorded");
  }
  std::uint8_t followSlot = initialFollowSlot;
  if (!followable_[followSlot]) {
    const auto fallback = std::find(followable_.begin(), followable_.end(), true);
    if (fallback == followable_.end()) {
      state_ = {};
      state_.stopReason = ReplayPresentationStopReason::InvalidDemo;
      return fail(error, "replay presentation has no recorded player to follow");
    }
    followSlot = static_cast<std::uint8_t>(
      std::distance(followable_.begin(), fallback)
    );
  }
''',
)

# The queue-pressure fixture must use fresh destinations. The existing
# async.lgdemo file is intentionally retained by earlier storage tests, and a
# successful first save must not make the retry collide with that same file.
replace_once(
    "tests/replay/ReplayStorageIoTests.cpp",
    '''    lg::replay::ReplayDemo first = sampleDemo(10U);
    lg::replay::ReplayDemo retained = sampleDemo(11U);
''',
    '''    const std::filesystem::path firstQueuedPath =
      storage.directory() / "queue_pressure_first.lgdemo";
    const std::filesystem::path retryQueuedPath =
      storage.directory() / "queue_pressure_retry.lgdemo";
    lg::replay::ReplayDemo first = sampleDemo(10U);
    lg::replay::ReplayDemo retained = sampleDemo(11U);
''',
)
replace_once(
    "tests/replay/ReplayStorageIoTests.cpp",
    '''      queued.enqueueSave(asyncPath, first, firstJob, &error),
''',
    '''      queued.enqueueSave(firstQueuedPath, first, firstJob, &error),
''',
)
replace_once(
    "tests/replay/ReplayStorageIoTests.cpp",
    '''      !queued.enqueueSave(asyncPath, retained, rejectedJob, &error) &&
''',
    '''      !queued.enqueueSave(retryQueuedPath, retained, rejectedJob, &error) &&
''',
)
replace_once(
    "tests/replay/ReplayStorageIoTests.cpp",
    '''      queued.enqueueSave(asyncPath, retained, retryJob, &error),
''',
    '''      queued.enqueueSave(retryQueuedPath, retained, retryJob, &error),
''',
)
