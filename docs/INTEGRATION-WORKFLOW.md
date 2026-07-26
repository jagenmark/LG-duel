# Task Integration

Use `scripts/integrate-tasks.ps1` to gather finished task commits into one
branch and worktree. The script does not touch the worktree from which you run
it. It does not merge uncommitted work.

Copy `config/integration-manifest.example.json`, then set:

- `base`: the commit or branch on which the batch starts.
- `integrationBranch`: a branch used only for this batch.
- `worktreePath`: the integration worktree, relative to the repository root or
  as a full path.
- `reportPath`: a local `reports/integration/*.generated.md` path.
- `groups`: ordered sets such as `bootstrap`, `renderer/effects`, and
  `UI/settings`.

Each item has a `type` of `commit` or `branch`. A commit item takes one commit.
A branch item takes each commit in `from..ref`, in oldest-first topological
order. `from` defaults to the manifest `base`. The helper rejects a range that
has a merge commit. Use exact commit IDs when the order must not change.

Each validation entry has a program name and an argument list. The sample uses
the project's noninteractive default configure, build, and test presets. The
script runs validation after its group. It notes a missing program as skipped
and stops on a failed command.

Preview the resolved order without changing Git:

```powershell
.\scripts\integrate-tasks.ps1 -ManifestPath .\config\my-integration.json -DryRun
```

Run the batch:

```powershell
.\scripts\integrate-tasks.ps1 -ManifestPath .\config\my-integration.json
```

The first run creates the branch and worktree at `base`. A later run uses the
same clean worktree and skips source commits already recorded by
`git cherry-pick -x`. The chosen base must remain an ancestor of an existing
integration branch. Start a new branch name for a new batch or base.

On a conflict, the script stops at once and leaves Git's conflict state intact.
It never resolves files. In the integration worktree, either:

```powershell
git status
# Edit files, then:
git add <resolved-files>
git cherry-pick --continue
# Run the helper again to continue the manifest.
```

or cancel the current pick:

```powershell
git cherry-pick --abort
```

The generated report lists the base, resolved picks, group checks, result, and
conflict recovery steps. Reports named `*.generated.md` under
`reports/integration/` stay local through `.gitignore`.

Limits:

- The integration worktree must be clean before a run.
- The worktree must sit outside every other worktree from this repository.
- One run at a time may use an integration worktree.
- Merge commits are rejected; list their needed non-merge commits instead.
- Branch ranges depend on their refs at run time. Use commit items for a fixed
  plan.
- The helper turns off Git's saved conflict fixes for each pick. It does not
  fetch, push, reset, delete worktrees, launch the game, or change source code.
