from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file_path = Path(path)
    text = file_path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}")
    file_path.write_text(text.replace(old, new, 1), encoding="utf-8")


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
