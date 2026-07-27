# Visual and graphics batch task ledger

This ledger accounts for every task in the visual-work list used for draft PR
228. A task is either included directly or covered by a later included commit.

| Task | Source evidence | Decision | Included commit(s) |
| --- | --- | --- | --- |
| Build visual evidence publishing | Gallery worktree commits `fae1db6`, `6b84d3a` and reviewed captures | Included | `441d009`, `6375d63`, `e3c25d6`, `e21ef10` |
| Improve developer-control workflow | Worktree `adc6`, source `deff16d` | Included | `9b82dcf` |
| Add machine-gun visual validation | Worktree `78ad`; later scenario timing and direct test command are in the batch | Covered by later integration | `cd1f33c`, `a835e0d` |
| Add repeatable integration workflow | Branch `codex/project-integration-workflow`, sources `1d8c7d6`, `05571d3` | Included | `bda8e80`, `e8e0579` |
| Fix SDL worktree bootstrap | Worktree `db3c`, source `c11a7d9` | Included | `c19768e` |
| Add chat overlay toggle | Worktree `6e1f`; scope corrected to the Console cat | Included | `0214347` |
| Diagnose sniper scope FPS drop | Worktree `2917`, source `edce900` | Included | `552c423` |
| Add graphics presets and benchmarks | Worktree `0f36`, source `5f28e47` | Included | `7faf4a0` |
| Create visual overhaul backlog | Worktree `6566`, source `8d31e88` | Included | `ea86fc7` |
| Improve global visual quality | Worktree `b160`, source `8ef149f`; shader interface combined with combat lighting | Included | `4eedde7` |
| Integrate effects with barrel | Completed machine-gun source `db134fd` | Included | `cd1f33c` |

The global-pass integration kept combat-light world position at shader location
2 and added haze distance at location 3. It did not replace the combat-effects
path.
