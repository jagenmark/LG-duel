# Project agent rules

## Parallel task isolation

- Read-only audits, reviews, diagnoses, and plans may use the shared checkout.
- Any task that will edit files must use its own worktree based on a clean, explicit commit.
- If a read-only task turns into an implementation task, stop and move it to a worktree before editing.
- Name one integration task. That task alone owns the main/shared checkout, staging, the combined branch, pushes, and the combined pull request.
- Parallel runtime work must use separate sessions, ports, processes, build folders, and output paths.
- Give shared default clients and state-changing MCP resources one named owner.
- If a task needs uncommitted work from another task, wait for a checkpoint commit and base its worktree on that commit.

## Background checks

- When you would run tests now and can keep doing independent work, request one opt-in background check. Follow `docs/CODEX-BACKGROUND-CHECKS.md` for scopes, results, and completion rules.

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
