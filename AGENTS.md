# Project agent rules

## Local game launches

- When the user asks to open or launch a game client, use the SDL GPU/Vulkan
  backend by default. Set `LG_DUEL_RENDER_BACKEND=gpu`. Use another backend
  only when the user asks for it.
- For a playable local test, start a server from the same worktree on a free,
  explicit port, then launch the client against that port.

## Visual evidence

- Publish each image that a task intends to show the user to the private LG Duel gallery as part of the task. Do not add a second approval step for ordinary image delivery.
- When a task claims that an image proves visual correctness or task completion, treat that evidence claim as unfinished until an independent reviewer gives the capture a `pass` verdict and the image appears in the gallery.
- Do not use a local path as the user-facing result. Give the user the hosted gallery page or hosted image link, with an embedded preview when the client supports it.
- Name captures `YYYYMMDDTHHMMSSZ-<short-task>-<view>-NN.<ext>`. Use UTC, lower-case ASCII words, and a two-digit sequence. Never overwrite an earlier capture.
- Keep the original image and its metadata together. The metadata must name the task and capture, the capture author, the UTC capture time, a clear title and note, and the sensitivity check. Add review data when the task will claim visual proof or completion.
- For evidence claims, the reviewer must not be the capture author. `pass` supports the claim; `fail`, `needs_changes`, missing review, and self-review do not.
- Run `scripts/publish_visual_evidence.py --dry-run <metadata.json>` first, then run it without `--dry-run` to stage the image. Deploy the gallery privately in the same task. Do not stage sensitive, secret, personal, or unrelated files.
- Use only the project-owned Sites gallery. Do not use public image hosts.
- If gallery publication fails, say that the image is not yet delivered. Keep the local original and metadata, retry or report the exact block, and never claim delivery from a local path alone.

See `docs/VISUAL-EVIDENCE.md` for the record format, review flow, setup, and commands.

## Parallel task isolation

- Read-only audits, reviews, diagnoses, and plans may use the shared checkout.
- Any task that will edit files must use its own worktree based on a clean, explicit commit.
- If a read-only task turns into an implementation task, stop and move it to a worktree before editing.
- Name one integration task. That task alone owns the main/shared checkout, staging, the combined branch, pushes, and the combined pull request.
- Parallel runtime work must use separate sessions, ports, processes, build folders, and output paths.
- Give shared default clients and state-changing MCP resources one named owner.
- If a task needs uncommitted work from another task, wait for a checkpoint commit and base its worktree on that commit.

## Automatic Task Integration

Task workers should end a reviewed, committed worktree task with this line:

```text
Integration candidate: <feature> | <group> | <commit> | depends-on: <commit-or-none> | reviewed: yes
```

Use one short lower-case feature key with hyphens. Use group names that show the
needed order, such as `bootstrap`, `renderer-effects`, and `ui-settings`.

When two or more complete worktree tasks report candidates for the same
feature, the coordinator must start the task integration workflow without
waiting for a separate user request:

1. Check that each candidate has a commit, passed review, and a clean task
   worktree.
2. Put the commits in dependency order. A named dependency must come first.
3. Copy `config/integration-manifest.example.json`, set its `feature`, groups,
   exact commit IDs, base, integration branch, and worktree path, then run the
   dry run.
4. Run `scripts/integrate-tasks.ps1` with a dedicated `integration/` branch and
   worktree.
5. Report the integration branch, report path, picks, and check results.

Stop when a real conflict occurs. Do not resolve it without user input. Do not
merge the integration branch into `main` and do not push any branch unless the
user approves that action.
