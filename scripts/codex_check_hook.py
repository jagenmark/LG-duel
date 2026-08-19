#!/usr/bin/env python3
"""Run opt-in LG Duel checks for the Codex PostToolUse hook."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time
from typing import Iterable

import fcntl

SCOPES = {"changed-files", "python", "shaders", "build", "ctest", "full"}
REQUEST_SCRIPT = "scripts/request_codex_check.py"


def run(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=repo,
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def requested_scope(payload: dict) -> str | None:
    command = payload.get("tool_input", {}).get("command")
    if not isinstance(command, str):
        return None
    try:
        words = shlex.split(command)
    except ValueError:
        return None
    for index, word in enumerate(words[:-1]):
        if word.endswith(REQUEST_SCRIPT) and words[index + 1] in SCOPES:
            return words[index + 1]
    return None


def changed_paths(repo: Path) -> list[Path]:
    output = run(repo, "git", "status", "--porcelain=v1", "-z").stdout
    paths: list[Path] = []
    entries = output.split("\0")
    index = 0
    while index < len(entries):
        entry = entries[index]
        if not entry:
            index += 1
            continue
        status = entry[:2]
        path = entry[3:]
        if status[0] in "RC" and index + 1 < len(entries):
            index += 1
            path = entries[index]
        paths.append(Path(path))
        index += 1
    return sorted(set(paths), key=lambda item: item.as_posix())


def fingerprint(repo: Path, scope: str, paths: Iterable[Path]) -> str:
    digest = hashlib.sha256(scope.encode())
    digest.update(run(repo, "git", "rev-parse", "HEAD").stdout.strip().encode())
    for path in paths:
        digest.update(path.as_posix().encode())
        full_path = repo / path
        if full_path.is_file():
            digest.update(full_path.read_bytes())
        else:
            digest.update(b"<missing>")
    return digest.hexdigest()[:16]


def python_commands(paths: list[Path]) -> list[list[str]]:
    python_files = [path for path in paths if path.suffix == ".py" and path.is_relative_to(Path("scripts"))]
    if not python_files:
        return []
    commands: list[list[str]] = [[sys.executable, "-m", "py_compile", *map(str, python_files)]]
    tests: set[Path] = set()
    for path in python_files:
        if path.name.startswith("test_"):
            tests.add(path)
        else:
            candidate = path.with_name(f"test_{path.name}")
            if candidate.exists():
                tests.add(candidate)
    commands.extend([[sys.executable, str(test)] for test in sorted(tests)])
    return commands


def command_plan(scope: str, paths: list[Path]) -> list[list[str]]:
    changed = {path.as_posix() for path in paths}
    has_cpp = any(path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"} for path in paths)
    has_cmake = any(path.name == "CMakeLists.txt" or path.suffix == ".cmake" for path in paths)
    has_shaders = any(path.as_posix().startswith("assets/shaders/") for path in paths)
    py = python_commands(paths)
    shader = [
        [sys.executable, "scripts/test_compile_shaders.py"],
        [sys.executable, "tools/compile_shaders.py", "--check"],
    ]
    build = [["cmake", "--preset", "default"], ["cmake", "--build", "--preset", "default", "--parallel", "2"]]
    ctest = [*build, ["ctest", "--preset", "default", "--parallel", "2", "--output-on-failure"]]

    if scope == "python":
        return py
    if scope == "shaders":
        return shader
    if scope == "build":
        return build
    if scope == "ctest":
        return ctest
    if scope == "full":
        return [
            [sys.executable, "-m", "unittest", "discover", "-s", "scripts", "-p", "test_*.py"],
            *shader,
            *ctest,
        ]
    if scope == "changed-files":
        if has_cpp or has_cmake:
            return ctest
        plan = [*py]
        if has_shaders or "tools/compile_shaders.py" in changed:
            plan.extend(shader)
        return plan
    raise ValueError(f"unknown scope: {scope}")


def state_dir(repo: Path) -> Path:
    git_path = run(repo, "git", "rev-parse", "--git-path", "codex-check").stdout.strip()
    path = Path(git_path)
    return path if path.is_absolute() else repo / path


def emit(status: str, message: str) -> None:
    context = f"[codex-check] {status}: {message}"
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": context[:2400],
        }
    }))


def concise_failure(command: list[str], output: str) -> str:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    tail = " | ".join(lines[-8:])
    return f"{' '.join(command)} :: {tail}"[:2100]


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, TypeError):
        return 0
    scope = requested_scope(payload)
    if scope is None:
        return 0

    repo = Path(payload.get("cwd") or os.getcwd()).resolve()
    try:
        repo = Path(run(repo, "git", "rev-parse", "--show-toplevel").stdout.strip())
        paths = changed_paths(repo)
        key = fingerprint(repo, scope, paths)
        storage = state_dir(repo)
        storage.mkdir(parents=True, exist_ok=True)
        lock_path = storage / "running.lock"
        state = storage / f"{scope}.json"
        lock = lock_path.open("w")
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            lock.close()
            emit("BUSY", "another background check is already running in this worktree")
            return 0
        try:
            if state.exists() and json.loads(state.read_text()).get("fingerprint") == key:
                emit("SKIP", f"{scope} already passed for state {key}")
                return 0
            commands = command_plan(scope, paths)
            if not commands:
                emit("SKIP", f"{scope} found no matching checks for state {key}")
                return 0
            started = time.monotonic()
            for command in commands:
                result = run(repo, *command, check=False)
                if result.returncode:
                    emit("FAIL", concise_failure(command, result.stdout))
                    return 0
            elapsed = time.monotonic() - started
            state.write_text(json.dumps({"fingerprint": key, "scope": scope, "commands": commands}) + "\n")
            names = "; ".join(" ".join(command) for command in commands)
            emit("PASS", f"{scope} state {key} in {elapsed:.1f}s — {names}")
        finally:
            lock.close()
    except Exception as error:  # Keep hook failures short and visible to the model.
        emit("FAIL", f"hook error: {type(error).__name__}: {error}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
