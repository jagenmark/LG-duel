# CI verification

The pull-request workflow runs with read-only repository access. It does not use
repository secrets. Runs for an older commit on the same pull request stop when
a new commit arrives. Each job has a time limit and uploads its JSON, JUnit,
logs, and other evidence even when a check fails.

The main Linux and Windows CTest steps run two independent test programs at a
time. Linux uses Debug and Windows uses Release, including the full test suite.
The local `msvc` preset still defaults to Debug; CI overrides the build and test
configuration explicitly. Linux Debug and sanitizer checks retain unoptimized
and sanitizer coverage. Tests that open UDP sockets use system-assigned ports,
and tests that write files use separate paths, so this keeps the same checks
while cutting idle CPU time.

## Required checks

In **Settings > Branches > Branch protection rules**, add or edit the rule for
`main`. Turn on these settings:

1. **Require a pull request before merging**.
2. **Require approvals** and set the needed approval count.
3. **Dismiss stale pull request approvals when new commits are pushed**.
4. **Require approval of the most recent reviewable push**.
5. **Require status checks to pass before merging**.
6. **Require branches to be up to date before merging**.
7. Add these exact required checks:
   - `linux-build-and-tests`
   - `windows-build-and-tests`
   - `deterministic-scenarios`
   - `protocol-and-packet-budgets`
   - `linux-sanitizers`
8. **Require conversation resolution before merging**.

`deterministic-scenarios` and `protocol-and-packet-budgets` are lightweight
required-check gates over results produced from the existing Linux build. This
preserves their branch-protection identities without repeating checkout,
configuration, and compilation. `linux-sanitizers` is an independent required
check so moving the focused sanitizer build off the main Linux critical path
does not silently weaken protection.

9. **Do not allow bypassing the above settings**.
10. Turn off force pushes and branch deletion.

## Optional PR checks

`performance-smoke` runs when a pull request has the `performance-smoke` label,
or when a maintainer starts the separate **Performance smoke** workflow by hand.
Label events do not restart PR verification. Unrelated label additions skip the
performance job, and its job-level concurrency cannot cancel required checks.
It is not a required status check. Keep it out of the required-check list so a hosted-runner
timing result cannot block a merge. When it runs, tool errors and hard packet or
queue-limit failures still fail the job. A `NOT_COMPARABLE` result remains useful
evidence and does not fail the job.

`live-client-server-smoke` checks the real client, UDP server, input path, and
snapshot path with the fallback renderer. Its three scenarios reuse the Windows
Release build after CTest, even if CTest fails. Detailed live evidence is included
in the Windows artifact; the separate live check gates the recorded outcome and
fails if smoke failed or could not run. This avoids a second Windows checkout,
configure, and compilation. Keep it visible at first. Make it a
required check once its hosted-runner record is stable.

The workflow writes evidence under `verification/`. CTest writes JUnit files to
`verification/<platform>/ctest.xml`. Scenario and benchmark tools add JSON,
JUnit, Markdown, logs, hashes, and captures below the same root. GitHub keeps PR
artifacts for 14 days.

The shared layout is:

```text
verification/
  manifest.json
  summary.json
  linux/ or windows/
    ctest.xml
  deterministic-scenarios/
  live-client-server/
  benchmarks/
    raw/
      baseline/
      candidate/
    comparison.json
    report.md
    logs/
  protocol/
    packet-budget.json
    protocol-test.log
```

Jobs may produce only their own part of this tree. `manifest.json` records that
partial state, including missing and unavailable categories, instead of claiming
that a failed or unsupported check ran. Artifact paths in the manifest stay
relative to `verification/`. Owned temp worktrees and build trees are removed
and excluded from both the manifest and uploaded artifacts.

## Hosted-runner limits

The PR checks use GitHub-hosted CPUs. Their load, clock rate, and host type can
change between runs. The short performance check can enforce hard packet and
queue limits and broad CPU limits. It must report noisy changes as
`INCONCLUSIVE`; it must not treat a small timing change as a failure.

Hosted runners do not provide a stable, trusted Vulkan GPU setup. PR checks
must report GPU timings, captures, and GPU-only metrics as `UNAVAILABLE`. A
fallback renderer run does not count as GPU proof. Submit and present timing do
not measure display scan-out or full input-to-photon delay.

The independent `linux-sanitizers` job runs a small AddressSanitizer and
UndefinedBehaviorSanitizer set without SDL or a GPU. It executes in parallel,
writes structured step outcomes even when setup or compilation fails, and does
not replace the full Linux CTest run.

## Full benchmark

Run **Actions > Full benchmark > Run workflow** for a full comparison. Supply
an explicit baseline, candidate, suite, and repetition count. The job only runs
after `confirm_trusted_code` is checked, and only on a runner with all of these
labels:

- `self-hosted`
- `windows`
- `trusted`
- `lg-duel-gpu`

Create a GitHub environment named `full-benchmark`. Add required reviewers,
limit which branches may deploy to it, and do not place secrets in it unless a
later task proves a need. A trusted reviewer must inspect both refs before
allowing the job. Never approve a baseline or candidate that fetches and runs
code from an untrusted fork.

The full job has read-only repository access, a three-hour limit, and no
automatic pull-request trigger. The `full` choice runs both the headless CPU
profile and the trusted GPU profile, and keeps a report for each. The other
choices run only their named profile. It collects repeated CPU and GPU runs,
screenshots, telemetry, JSON, and Markdown reports. It keeps artifacts for 30
days. The runner owner must pin the GPU, driver, power mode, display settings,
and background load. A result is `NOT_COMPARABLE` when key host or run settings
differ.

Do not make `full-benchmark` a required pull-request check. It is a manual,
trusted hardware check for changes that need full GPU or low-noise data.
