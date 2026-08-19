# Codex background checks

Use a background check only at a point where you would run the same check in the foreground. It lets you do independent work while the check runs; it is not an after-edit trigger.

Review and trust the project hook once through `/hooks` when Codex first finds it. Codex skips new or changed hooks until they are trusted.

Request a check from the repository root:

```bash
python3 scripts/request_codex_check.py changed-files
```

Available scopes:

- `changed-files`: choose the smallest safe plan from the current Git changes.
- `python`: compile changed Python files and run their matching `test_<name>.py` files.
- `shaders`: run the shader unit tests and check compiled shader files.
- `build`: configure if needed, then build the default preset.
- `ctest`: build, then run the default CTest preset.
- `full`: run Python tests, shader checks, build, and CTest.

The request command confirms that the hook accepted the request. The hook later sends one short `PASS`, `FAIL`, `SKIP`, or `BUSY` result to Codex. A request with the same scope and unchanged inputs returns `SKIP`. Only one check may run per worktree at a time.

Treat a reported `PASS` as evidence for the commands named in that result. Run any check the task still needs before claiming completion. If later edits touch the checked inputs, request the needed check again.
