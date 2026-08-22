from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file_path = Path(path)
    text = file_path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}")
    file_path.write_text(text.replace(old, new, 1), encoding="utf-8")


def replace_region(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    file_path = Path(path)
    text = file_path.read_text(encoding="utf-8")
    start = text.find(start_marker)
    if start < 0:
        raise SystemExit(f"{path}: start marker not found: {start_marker!r}")
    end = text.find(end_marker, start)
    if end < 0:
        raise SystemExit(f"{path}: end marker not found: {end_marker!r}")
    file_path.write_text(text[:start] + replacement + text[end:], encoding="utf-8")


payload_path = Path(".github/pr265/followup_payload.yml")
source = payload_path.read_text(encoding="utf-8")
lines = source.splitlines()
start = next(i for i, line in enumerate(lines) if line.strip() == "python - <<'PY'")
end = next(i for i in range(start + 1, len(lines)) if lines[i].strip() == "PY")

script_lines: list[str] = []
in_triple = False
prefix = " " * 10
for line in lines[start + 1 : end]:
    if in_triple:
        if "'''" in line:
            script_lines.append(line[len(prefix) :] if line.startswith(prefix) else line)
            in_triple = False
        else:
            script_lines.append(line)
        continue
    stripped = line[len(prefix) :] if line.startswith(prefix) else line
    script_lines.append(stripped)
    if stripped.count("'''") % 2 == 1:
        in_triple = True

payload_script = "\n".join(script_lines) + "\n"

old_replace_guard = """    if count != 1:
        raise SystemExit(f'{path}: expected one replacement, found {count}')
    write(path, text.replace(old, new, 1))
"""
new_replace_guard = """    if path == 'src/replay/ReplayIoService.cpp':
        return
    if count == 0 and text.count(new) == 1:
        return
    if count != 1:
        raise SystemExit(
            f'{path}: expected one replacement, found {count}: {old[:120]!r}'
        )
    write(path, text.replace(old, new, 1))
"""
if payload_script.count(old_replace_guard) != 1:
    raise SystemExit("payload replace_once guard changed unexpectedly")
payload_script = payload_script.replace(old_replace_guard, new_replace_guard, 1)

# Apply ReplayIoService.cpp by structural boundaries. The retained payload's
# exact marker predates formatting already present on this PR.
replace_region(
    "src/replay/ReplayIoService.cpp",
    "ReplayIoService::ReplayIoService(Config config)\n",
    "bool ReplayIoService::enqueueSave(",
    """ReplayIoService::ReplayIoService(Config config)
    : config_(config) {
    if (config_.maxPendingJobs == 0) {
        config_.maxPendingJobs = 1;
    }
    if (config_.startWorker) {
        start();
    }
}

ReplayIoService::~ReplayIoService() {
    shutdown();
}

void ReplayIoService::start() {
    std::lock_guard lock(mutex_);
    if (workerStarted_ || stopping_ || stopped_) {
        return;
    }
    worker_ = std::thread(&ReplayIoService::workerLoop, this);
    workerStarted_ = true;
}

""",
)
replace_region(
    "src/replay/ReplayIoService.cpp",
    "bool ReplayIoService::enqueueDecode(",
    "bool ReplayIoService::enqueueEncode(",
    """bool ReplayIoService::enqueueDecode(
                                    std::vector<std::uint8_t>& bytes,
                                    JobId& id,
                                    std::string* error,
                                    std::size_t maximumResidentBytes,
                                    std::size_t maximumTicks) {
    if (bytes.size() > kMaxReplayBytes) {
        setError(error, "replay bytes exceed the size limit");
        return false;
    }
    if (maximumResidentBytes == 0U ||
        maximumResidentBytes > kMaxReplayDecodedResidentBytes ||
        maximumTicks == 0U || maximumTicks > kMaxReplayTicks) {
        setError(error, "replay decode limits are invalid");
        return false;
    }
    std::lock_guard lock(mutex_);
    if (stopping_ || stopped_) {
        setError(error, "replay I/O service is stopped");
        return false;
    }
    if (jobs_.size() + runningJobs_ >= config_.maxPendingJobs) {
        setError(error, "replay I/O queue is full");
        return false;
    }
    id = nextId_++;
    jobs_.push_back(Job{
        id,
        JobKind::Decode,
        DecodeJob{std::move(bytes), maximumResidentBytes, maximumTicks}
    });
    condition_.notify_one();
    return true;
}

""",
)
replace_region(
    "src/replay/ReplayIoService.cpp",
    "void ReplayIoService::shutdown()",
    "void ReplayIoService::workerLoop()",
    """void ReplayIoService::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return;
        }
        if (!workerStarted_ && !jobs_.empty()) {
            worker_ = std::thread(&ReplayIoService::workerLoop, this);
            workerStarted_ = true;
        }
        stopping_ = true;
    }
    condition_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard lock(mutex_);
    stopped_ = true;
}

""",
)
replace_once(
    "src/replay/ReplayIoService.cpp",
    """        } else if constexpr (std::is_same_v<Payload, DecodeJob>) {
            ReplayDemo demo;
            result.ok = decodeDemo(payload.bytes, demo, &result.error);""",
    """        } else if constexpr (std::is_same_v<Payload, DecodeJob>) {
            ReplayDemo demo;
            result.ok = decodeDemo(
                payload.bytes,
                demo,
                &result.error,
                payload.maximumResidentBytes,
                payload.maximumTicks
            );""",
)

exec(compile(payload_script, "pr265-followup-apply.py", "exec"), {"__name__": "__main__"})

# Repair the transfer-loop closure produced by the retained payload.
replace_once(
    "src/app/GameApp.cpp",
    '''      }
  };

    if (const std::optional<replay::ReplayTransferMessage> timeout =''',
    '''      }
    }
    if (const std::optional<replay::ReplayTransferMessage> timeout =''',
)

# A manual skip must release every stage of the pending candidate.
replace_once(
    "src/app/GameApp.cpp",
    '''    [&killcamReceiver, &session, &replayRuntime, &remoteKillcamActive,
     &pendingKillcamDecode, &pendingKillcamContext,
     &replayPresentationResetRequested](''',
    '''    [&killcamReceiver, &session, &replayRuntime, &remoteKillcamActive,
     &pendingKillcamDecode, &pendingKillcamDecodeIdentity,
     &pendingKillcamPlayback, &pendingKillcamContext,
     &replayPresentationResetRequested](''',
)
replace_once(
    "src/app/GameApp.cpp",
    '''      pendingKillcamDecode.reset();
      pendingKillcamContext.reset();
      if (remoteKillcamActive && replayRuntime != nullptr &&''',
    '''      pendingKillcamDecode.reset();
      pendingKillcamDecodeIdentity.reset();
      pendingKillcamPlayback.reset();
      pendingKillcamContext.reset();
      if (remoteKillcamActive && replayRuntime != nullptr &&''',
)

# Keep the two-death admission capacity a named production contract and make
# the regression use that exact value.
replace_once(
    "src/replay/KillcamServerCoordinator.hpp",
    '''inline constexpr std::size_t kDefaultKillcamPacketsPerTick = 2U;
''',
    '''inline constexpr std::size_t kDefaultKillcamPacketsPerTick = 2U;
inline constexpr std::size_t kKillcamEncodeQueueCapacity = 2U;
''',
)
replace_once(
    "src/replay/KillcamServerCoordinator.cpp",
    '''      io_(ReplayIoService::Config{2U}),''',
    '''      io_(ReplayIoService::Config{kKillcamEncodeQueueCapacity}),''',
)
replace_once(
    "tests/replay/ReplayStorageIoTests.cpp",
    '''#include "replay/ReplayIoService.hpp"
''',
    '''#include "replay/ReplayIoService.hpp"
#include "replay/KillcamServerCoordinator.hpp"
''',
)
replace_once(
    "tests/replay/ReplayStorageIoTests.cpp",
    '''      lg::replay::ReplayIoService::Config{2U, false}
''',
    '''      lg::replay::ReplayIoService::Config{
        lg::replay::kKillcamEncodeQueueCapacity, false
      }
''',
)

# The queue-pressure regression must exercise the retry, not merely observe
# that the rejected lvalue survived admission failure.
replace_once(
    "tests/replay/ReplayStorageIoTests.cpp",
    '''    failures += expect(firstResult.has_value() && firstResult->ok,
      "the retained queued save should run after the worker starts");
    queued.shutdown();''',
    '''    failures += expect(firstResult.has_value() && firstResult->ok,
      "the first queued save should run after the worker starts");
    lg::replay::ReplayIoService::JobId retryJob = 0U;
    failures += expect(
      queued.enqueueSave(asyncPath, retained, retryJob, &error),
      "the completed demo rejected under queue pressure should be retryable"
    );
    const auto retryResult = waitFor(queued, [retryJob](const auto& result) {
      return result.id == retryJob;
    });
    failures += expect(
      retryResult.has_value() && retryResult->ok,
      "the retried completed demo should eventually save"
    );
    queued.shutdown();''',
)

replace_once(
    "tests/replay/KillcamClientReceiverTests.cpp",
    '''#include <string_view>
#include <vector>
''',
    '''#include <string_view>
#include <utility>
#include <vector>
''',
)
replace_once(
    "src/replay/ReplayPresentationSession.cpp",
    '''#include <limits>
''',
    '''#include <limits>
#include <iterator>
''',
)
