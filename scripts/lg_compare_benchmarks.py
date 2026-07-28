#!/usr/bin/env python3
"""Compare LG Duel benchmark results or run two revisions in clean worktrees."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any, Callable, Iterator, Sequence

import lg_benchmark
import lg_launch
import lg_performance_policy
import lg_verification


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_POLICY = REPO_ROOT / "config" / "performance-policy.json"
MAX_REPETITIONS = 20
MAX_SCENARIOS = 16
CONFIGURE_TIMEOUT = 300.0
BUILD_TIMEOUT = 1200.0
RUN_TIMEOUT = 600.0
GIT_TIMEOUT = 60.0
HEADLESS_WORKLOADS = ("movement-collision", "trace-projectile")
GPU_SUITES = ("trusted_gpu", "trusted_gpu_competitive")
SUITES = ("pr_headless", *GPU_SUITES)
PROFILES = ("pr_headless", *GPU_SUITES)
SUCCESS_STATUSES = {"PASS", "WARN", "INCONCLUSIVE", "UNAVAILABLE"}
REF_PATTERN = re.compile(r"^[A-Za-z0-9_./~^@+-]{1,240}$")


class CompareError(RuntimeError):
    """A safe, clear comparison failure."""


def _atomic_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        temporary.write_text(text, encoding="utf-8")
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def _atomic_json(path: Path, value: Any) -> None:
    _atomic_text(
        path,
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False)
        + "\n",
    )


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _is_within(path: Path, root: Path) -> bool:
    resolved = path.resolve()
    parent = root.resolve()
    return resolved == parent or parent in resolved.parents


def _portable_arg(value: str, output: Path) -> str:
    try:
        path = Path(value)
        if not path.is_absolute():
            return value
        resolved = path.resolve()
    except (OSError, ValueError):
        return value
    if _is_within(resolved, output):
        return resolved.relative_to(output.resolve()).as_posix() or "."
    if _is_within(resolved, REPO_ROOT):
        return f"<repository>/{resolved.relative_to(REPO_ROOT.resolve()).as_posix()}"
    return f"<absolute>/{resolved.name}"


def _artifact_inventory(output: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    if not output.is_dir():
        return records
    for path in sorted(output.rglob("*"), key=lambda item: item.as_posix()):
        relative = path.relative_to(output)
        if not path.is_file() or relative == Path("manifest.json"):
            continue
        if relative.parts[:2] in {("temp", "worktrees"), ("temp", "builds")}:
            continue
        records.append(
            {
                "path": relative.as_posix(),
                "bytes": path.stat().st_size,
                "sha256": _sha256(path),
            }
        )
    return records


def _write_manifest(output: Path, manifest: dict[str, Any]) -> None:
    manifest["artifacts"] = _artifact_inventory(output)
    _atomic_json(output / "manifest.json", manifest)


def _create_output(path: Path) -> Path:
    output = path.expanduser().absolute()
    if output.exists():
        raise CompareError(f"output path already exists: {path}")
    if output.parent.exists() and not output.parent.is_dir():
        raise CompareError(f"output parent is not a directory: {output.parent}")
    output.mkdir(parents=True, exist_ok=False)
    return output


def _safe_ref(value: str, label: str) -> str:
    if (
        not REF_PATTERN.fullmatch(value)
        or value.startswith("-")
        or ".." in value
        or "@{" in value
    ):
        raise CompareError(f"{label} is not a safe git revision: {value!r}")
    return value


def _call(
    runner: Callable[..., subprocess.CompletedProcess[str]],
    command: Sequence[str],
    *,
    cwd: Path,
    timeout: float,
) -> subprocess.CompletedProcess[str]:
    try:
        return runner(
            list(command),
            cwd=cwd,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise CompareError(f"command timed out after {timeout:g} seconds: {command[0]}") from error
    except OSError as error:
        raise CompareError(f"could not run {command[0]}: {error}") from error


def _resolve_ref(
    value: str,
    label: str,
    runner: Callable[..., subprocess.CompletedProcess[str]],
) -> str:
    ref = _safe_ref(value, label)
    command = ["git", "rev-parse", "--verify", f"{ref}^{{commit}}"]
    completed = _call(runner, command, cwd=REPO_ROOT, timeout=GIT_TIMEOUT)
    commit = completed.stdout.strip()
    if completed.returncode != 0 or not re.fullmatch(r"[0-9a-fA-F]{40,64}", commit):
        detail = completed.stderr.strip() or "revision was not found"
        raise CompareError(f"cannot resolve {label} {value!r}: {detail}")
    return commit.lower()


def _reject_dirty_tree(
    runner: Callable[..., subprocess.CompletedProcess[str]],
) -> None:
    completed = _call(
        runner,
        ["git", "status", "--porcelain", "--untracked-files=normal"],
        cwd=REPO_ROOT,
        timeout=GIT_TIMEOUT,
    )
    if completed.returncode != 0:
        raise CompareError(
            f"cannot inspect the active worktree: {completed.stderr.strip() or completed.returncode}"
        )
    if completed.stdout.strip():
        raise CompareError("revision mode requires a clean active worktree")


def _run_command(
    runner: Callable[..., subprocess.CompletedProcess[str]],
    command: Sequence[str],
    *,
    cwd: Path,
    timeout: float,
    output: Path,
    manifest: dict[str, Any],
    name: str,
    required: bool = True,
) -> subprocess.CompletedProcess[str]:
    completed = _call(runner, command, cwd=cwd, timeout=timeout)
    log_root = output / "logs"
    _atomic_text(log_root / f"{name}.stdout.log", completed.stdout or "")
    _atomic_text(log_root / f"{name}.stderr.log", completed.stderr or "")
    record = {
        "name": name,
        "argv": [_portable_arg(str(item), output) for item in command],
        "cwd": _portable_arg(str(cwd), output),
        "return_code": completed.returncode,
        "stdout": f"logs/{name}.stdout.log",
        "stderr": f"logs/{name}.stderr.log",
    }
    manifest["commands"].append(record)
    manifest["stage"] = name
    _write_manifest(output, manifest)
    if required and completed.returncode != 0:
        raise CompareError(
            f"{name} exited {completed.returncode}; see {record['stderr']}"
        )
    return completed


def _worktree_paths(output: Path, side: str, commit: str) -> tuple[Path, Path]:
    token = f"{side}-{commit[:12]}-{uuid.uuid4().hex[:8]}"
    return (
        output / "temp" / "worktrees" / token,
        output / "temp" / "builds" / token,
    )


def _registered_worktrees(
    runner: Callable[..., subprocess.CompletedProcess[str]],
) -> set[Path]:
    completed = _call(
        runner,
        ["git", "worktree", "list", "--porcelain"],
        cwd=REPO_ROOT,
        timeout=GIT_TIMEOUT,
    )
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "").strip()
        raise CompareError(
            "cannot inspect registered worktrees"
            + (f": {detail}" if detail else "")
        )
    result: set[Path] = set()
    for line in completed.stdout.splitlines():
        if line.startswith("worktree "):
            result.add(Path(line[len("worktree ") :]).resolve())
    return result


def _cleanup_worktrees(
    runner: Callable[..., subprocess.CompletedProcess[str]],
    output: Path,
    owned_root: Path,
    intended: list[Path],
    manifest: dict[str, Any],
) -> None:
    try:
        registered = _registered_worktrees(runner)
    except Exception as error:
        for path in intended:
            manifest["cleanup"].append(
                {
                    "path": path.relative_to(output).as_posix(),
                    "status": "inspection-failed",
                    "error": str(error),
                    "recovery_command": (
                        "git worktree remove --force "
                        f"\"$OUTPUT/{path.relative_to(output).as_posix()}\""
                    ),
                }
            )
        _write_manifest(output, manifest)
        return
    for path in intended:
        record: dict[str, Any] = {
            "path": path.relative_to(output).as_posix(),
            "status": "not-registered",
            "recovery_command": None,
        }
        manifest["cleanup"].append(record)
        if not _is_within(path, owned_root) or path.resolve() not in registered:
            continue
        command = ["git", "worktree", "remove", "--force", str(path)]
        try:
            completed = _run_command(
                runner,
                command,
                cwd=REPO_ROOT,
                timeout=GIT_TIMEOUT,
                output=output,
                manifest=manifest,
                name=f"cleanup-{len(manifest['cleanup'])}",
                required=False,
            )
        except Exception as error:
            record["status"] = "failed"
            record["error"] = str(error)
            record["recovery_command"] = (
                f"git worktree remove --force \"$OUTPUT/{record['path']}\""
            )
            continue
        if completed.returncode == 0:
            record["status"] = "removed"
        else:
            record["status"] = "failed"
            record["recovery_command"] = (
                f"git worktree remove --force \"$OUTPUT/{record['path']}\""
            )
    try:
        owned_root.rmdir()
    except OSError:
        pass
    _write_manifest(output, manifest)


def _cleanup_builds(
    output: Path,
    owned_root: Path,
    intended: list[Path],
    manifest: dict[str, Any],
) -> None:
    expected_root = output / "temp" / "builds"
    if owned_root.resolve() != expected_root.resolve():
        raise CompareError("owned build root is outside the comparison temp tree")
    for path in intended:
        relative = path.relative_to(output).as_posix()
        record: dict[str, Any] = {
            "path": relative,
            "status": "absent",
            "recovery_command": None,
        }
        manifest["cleanup"].append(record)
        if path.resolve() == owned_root.resolve() or not _is_within(path, owned_root):
            record["status"] = "unsafe"
            record["error"] = "build path is outside its owned root"
            continue
        if not path.exists():
            continue
        try:
            shutil.rmtree(path)
            record["status"] = "removed"
        except OSError as error:
            record["status"] = "failed"
            record["error"] = str(error)
            record["recovery_command"] = (
                f'Remove-Item -LiteralPath "$OUTPUT/{relative}" -Recurse -Force'
            )
    for parent in (owned_root, owned_root.parent):
        try:
            parent.rmdir()
        except OSError:
            pass
    _write_manifest(output, manifest)


def _add_worktree(
    side: str,
    commit: str,
    path: Path,
    runner: Callable[..., subprocess.CompletedProcess[str]],
    output: Path,
    manifest: dict[str, Any],
) -> None:
    if path.exists():
        raise CompareError(f"refusing pre-existing worktree path: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    _run_command(
        runner,
        ["git", "worktree", "add", "--detach", str(path), commit],
        cwd=REPO_ROOT,
        timeout=GIT_TIMEOUT,
        output=output,
        manifest=manifest,
        name=f"{side}-worktree-add",
    )


def _build_targets(suite: str) -> list[str]:
    targets = ["lg_duel_protocol_tests"]
    if suite == "pr_headless":
        targets.append("lg_duel_sim_benchmark")
    if suite in GPU_SUITES:
        targets.extend(("lg_duel_client", "lg_duel_server"))
    return sorted(set(targets))


def _configure_and_build(
    side: str,
    source: Path,
    build: Path,
    suite: str,
    runner: Callable[..., subprocess.CompletedProcess[str]],
    output: Path,
    manifest: dict[str, Any],
) -> None:
    if build.exists():
        raise CompareError(f"refusing pre-existing build path: {build}")
    build.parent.mkdir(parents=True, exist_ok=True)
    settings = [
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_TESTING=ON",
    ]
    if suite == "pr_headless":
        settings.append("-DLG_DUEL_REQUIRE_SDL3=OFF")
    else:
        settings.extend(
            (
                "-DLG_DUEL_REQUIRE_SDL3=ON",
                "-DLG_DUEL_FETCH_SDL3=ON",
                "-DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON",
            )
        )
    _run_command(
        runner,
        ["cmake", "-S", str(source), "-B", str(build), *settings],
        cwd=source,
        timeout=CONFIGURE_TIMEOUT,
        output=output,
        manifest=manifest,
        name=f"{side}-configure",
    )
    _run_command(
        runner,
        ["cmake", "--build", str(build), "--config", "Release", "--target", *_build_targets(suite)],
        cwd=source,
        timeout=BUILD_TIMEOUT,
        output=output,
        manifest=manifest,
        name=f"{side}-build",
    )


def _executable(build: Path, name: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    direct = build / f"{name}{suffix}"
    if direct.is_file() or name != "lg_duel_protocol_tests":
        return direct
    return build / "tests" / f"{name}{suffix}"


def _read_datagram_ceiling(source: Path) -> int:
    header = source / "src" / "net" / "NetCodec.hpp"
    try:
        text = header.read_text(encoding="utf-8")
    except OSError as error:
        raise CompareError(f"cannot read datagram ceiling from {header}: {error}") from error
    match = re.search(
        r"\bkMaxUdpApplicationDatagramBytes\s*=\s*(\d+)", text
    )
    if not match:
        raise CompareError(f"datagram ceiling is missing from {header}")
    return int(match.group(1))


def _run_protocol_test(
    side: str,
    source: Path,
    build: Path,
    runner: Callable[..., subprocess.CompletedProcess[str]],
    output: Path,
    manifest: dict[str, Any],
) -> dict[str, Any]:
    executable = _executable(build, "lg_duel_protocol_tests")
    if not executable.is_file():
        raise CompareError(f"protocol test executable is missing: {executable}")
    completed = _run_command(
        runner,
        [str(executable)],
        cwd=source,
        timeout=RUN_TIMEOUT,
        output=output,
        manifest=manifest,
        name=f"{side}-protocol-test",
    )
    facts = lg_verification.parse_packet_facts(completed.stdout or "")
    observed = max((fact["bytes"] for fact in facts), default=None)
    ceiling = _read_datagram_ceiling(source)
    if observed is None:
        raise CompareError(f"{side} protocol test emitted no packet byte facts")
    return {
        "max_app_datagram_bytes": observed,
        "max_app_datagram_bytes_source": "maximum byte fact emitted by protocol tests",
        "configured_datagram_ceiling_bytes": ceiling,
        "packet_facts": facts,
        "snapshot_encode_failures": 0,
        "protocol_test_return_code": completed.returncode,
        "protocol_test_log": f"logs/{side}-protocol-test.stdout.log",
    }


@contextlib.contextmanager
def _benchmark_scope(source: Path, build: Path, results: Path) -> Iterator[None]:
    benchmark_values = {
        "REPO_ROOT": source,
        "SCENARIO_ROOT": source / "config" / "benchmarks",
        "RESULT_ROOT": results,
        "BASELINE_ROOT": results / "baselines",
        "BUILD_MODES": {
            "release": {"directory": build, "preset": "comparison-release"},
            "debug": {"directory": build, "preset": "comparison-release"},
        },
    }
    state = results / "_launcher"
    launch_values = {
        "REPO_ROOT": source,
        "BUILD_DIR": build,
        "STATE_DIR": state,
        "STATE_PATH": state / "processes.json",
        "LOCAL_VULKAN_CONFIGS": (state / "vulkan.json", results / "vulkan.json"),
        "BENCHMARK_ROOT": results,
    }
    old_benchmark = {key: getattr(lg_benchmark, key) for key in benchmark_values}
    old_launch = {key: getattr(lg_launch, key) for key in launch_values}
    try:
        for key, value in benchmark_values.items():
            setattr(lg_benchmark, key, value)
        for key, value in launch_values.items():
            setattr(lg_launch, key, value)
        yield
    finally:
        for key, value in old_benchmark.items():
            setattr(lg_benchmark, key, value)
        for key, value in old_launch.items():
            setattr(lg_launch, key, value)


def _rewrite_aggregate(result: dict[str, Any], evidence: dict[str, Any]) -> None:
    result.update(evidence)
    path = Path(result["result_directory"]) / "aggregate.json"
    _atomic_json(path, result)


def _run_gpu(
    side: str,
    source: Path,
    build: Path,
    results: Path,
    repetitions: int,
    scenarios: Sequence[str],
    protocol: dict[str, Any],
    manifest: dict[str, Any],
    output: Path,
) -> None:
    if os.name != "nt":
        # The shared launcher still uses the shipped Windows names. Owned Linux
        # builds get local hard links so that the same verified launch path works.
        for name in ("lg_duel_client", "lg_duel_server"):
            native = build / name
            launcher_name = build / f"{name}.exe"
            if not native.is_file():
                raise CompareError(f"GPU benchmark executable is missing: {native}")
            if launcher_name.exists():
                raise CompareError(
                    f"refusing pre-existing Linux launcher link: {launcher_name}"
                )
            try:
                os.link(native, launcher_name)
            except OSError:
                shutil.copy2(native, launcher_name)
    with _benchmark_scope(source, build, results):
        for scenario in scenarios:
            manifest["stage"] = f"{side}-run-{scenario}"
            _write_manifest(output, manifest)
            cleanup_failures = 0
            result: dict[str, Any] | None = None
            run_error: BaseException | None = None
            try:
                result = lg_benchmark.run_benchmark(
                    scenario,
                    repetitions=repetitions,
                    timeout=RUN_TIMEOUT,
                    start_client=True,
                    build_mode="release",
                )
            except BaseException as error:
                run_error = error
            finally:
                try:
                    cleanup = lg_launch.stop_owned()
                    if (
                        cleanup.get("left_owned_running") is True
                        or cleanup.get("left_unowned_running") is True
                    ):
                        cleanup_failures = 1
                except Exception:
                    cleanup_failures = 1
            if run_error is not None:
                manifest["commands"].append(
                    {
                        "name": f"{side}-benchmark-{scenario}",
                        "argv": ["lg_benchmark.run_benchmark", scenario],
                        "cwd": _portable_arg(str(source), output),
                        "return_code": None,
                        "error": f"{type(run_error).__name__}: {run_error}",
                    }
                )
                _write_manifest(output, manifest)
                raise run_error
            if result is None:
                raise CompareError(f"{side} benchmark returned no result")
            manifest["commands"].append(
                {
                    "name": f"{side}-benchmark-{scenario}",
                    "argv": ["lg_benchmark.run_benchmark", scenario],
                    "cwd": _portable_arg(str(source), output),
                    "return_code": 0,
                }
            )
            evidence = {**protocol, "cleanup_failures": cleanup_failures}
            _rewrite_aggregate(result, evidence)
            manifest["runs"].append(
                {
                    "side": side,
                    "scenario": scenario,
                    "requested": repetitions,
                    "valid": sum(
                        1 for run in result.get("runs", [])
                        if run.get("valid", True) is True
                    ),
                }
            )
            _write_manifest(output, manifest)
            if cleanup_failures:
                raise CompareError(f"{side} benchmark launcher cleanup failed")


def _load_bounded_results(
    path: Path, expected: Sequence[str]
) -> dict[str, Any]:
    if len(expected) > MAX_SCENARIOS:
        raise CompareError(f"policy requests more than {MAX_SCENARIOS} scenarios")
    return lg_performance_policy.load_result_directory(path, expected)


def _copy_tree(source: Path, destination: Path) -> None:
    if destination.exists():
        raise CompareError(f"refusing pre-existing result copy path: {destination}")
    for path in source.rglob("*"):
        if path.is_symlink():
            raise CompareError(f"result directory contains a symbolic link: {path}")
    shutil.copytree(source, destination)


def _result_commit(result_set: dict[str, Any]) -> str | None:
    commits = {
        str(value.get("git", {}).get("commit"))
        for value in result_set["scenarios"].values()
        if value.get("git", {}).get("commit") not in {None, "", "unknown"}
    }
    return next(iter(commits)) if len(commits) == 1 else None


def _run_counts(result_set: dict[str, Any]) -> dict[str, dict[str, int]]:
    counts: dict[str, dict[str, int]] = {}
    for name, result in sorted(result_set["scenarios"].items()):
        runs = result.get("runs", [])
        requested = int(
            result.get("settings", {}).get(
                "repetitions", result.get("aggregate", {}).get("run_count", len(runs))
            )
        )
        valid = (
            sum(1 for run in runs if run.get("valid", True) is True)
            if runs
            else int(result.get("aggregate", {}).get("run_count", 0))
            if result.get("aggregate", {}).get("valid") is True
            else 0
        )
        counts[name] = {"requested": requested, "valid": valid}
    return counts


def _write_comparison(
    output: Path,
    baseline: dict[str, Any],
    candidate: dict[str, Any],
    policy: dict[str, Any],
    *,
    baseline_commit: str | None,
    candidate_commit: str | None,
    repetitions: int | None,
) -> str:
    comparison = lg_performance_policy.compare_result_sets(
        baseline, candidate, policy
    )
    comparison["artifacts"] = {
        "baseline": "raw/baseline",
        "candidate": "raw/candidate",
    }
    baseline_counts = _run_counts(baseline)
    candidate_counts = _run_counts(candidate)
    stored_repetitions: Any = repetitions
    if repetitions is None:
        stored_repetitions = {
            "baseline": {
                name: value["requested"]
                for name, value in baseline_counts.items()
            },
            "candidate": {
                name: value["requested"]
                for name, value in candidate_counts.items()
            },
        }
    comparison["comparison_metadata"] = {
        "baseline_commit": baseline_commit,
        "candidate_commit": candidate_commit,
        "repetitions": stored_repetitions,
        "baseline_runs": baseline_counts,
        "candidate_runs": candidate_counts,
    }
    _atomic_json(output / "comparison.json", comparison)
    report = lg_performance_policy.render_markdown(comparison)
    metadata = comparison["comparison_metadata"]
    report += (
        "\n### Comparison metadata\n\n"
        f"- Baseline commit: `{metadata['baseline_commit'] or 'unknown'}`\n"
        f"- Candidate commit: `{metadata['candidate_commit'] or 'unknown'}`\n"
        f"- Repetitions: `{json.dumps(metadata['repetitions'], sort_keys=True)}`\n"
    )
    _atomic_text(output / "report.md", report)
    return str(comparison["status"]).upper()


def _new_manifest(mode: str, profile: str, policy_path: Path) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "tool": "lg_compare_benchmarks",
        "tool_version": 1,
        "mode": mode,
        "profile": profile,
        "policy": {
            "path": (
                policy_path.resolve().relative_to(REPO_ROOT.resolve()).as_posix()
                if _is_within(policy_path, REPO_ROOT)
                else policy_path.name
            ),
            "sha256": _sha256(policy_path),
        },
        "platform": {
            "os_name": os.name,
            "python": f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}",
        },
        "build": {
            "generator": "Ninja",
            "configuration": "Release",
            "cmake_options": ["BUILD_TESTING=ON"],
            "timeouts_seconds": {
                "configure": CONFIGURE_TIMEOUT,
                "build": BUILD_TIMEOUT,
                "run": RUN_TIMEOUT,
            },
        },
        "stage": "started",
        "status": "RUNNING",
        "commands": [],
        "runs": [],
        "cleanup": [],
        "error": None,
        "artifacts": [],
    }


def _validate_mode(args: argparse.Namespace) -> str:
    revision_values = (args.baseline, args.candidate)
    stored_values = (args.baseline_results, args.candidate_results)
    revision_any = any(value is not None for value in revision_values)
    stored_any = any(value is not None for value in stored_values)
    if revision_any and stored_any:
        raise CompareError("revision and stored-result modes are exclusive")
    if revision_any:
        if not all(value is not None for value in revision_values):
            raise CompareError("revision mode requires --baseline and --candidate")
        if args.suite is None or args.repetitions is None:
            raise CompareError("revision mode requires --suite and --repetitions")
        return "revision"
    if stored_any:
        if not all(value is not None for value in stored_values):
            raise CompareError(
                "stored mode requires --baseline-results and --candidate-results"
            )
        if args.suite is not None or args.repetitions is not None:
            raise CompareError("stored mode does not accept --suite or --repetitions")
        return "stored"
    raise CompareError("choose revision mode or stored-result mode")


def execute(
    args: argparse.Namespace,
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> int:
    mode = _validate_mode(args)
    policy_path = Path(args.policy).expanduser().absolute()
    policy = lg_performance_policy.load_policy(policy_path, args.profile)
    expected = policy["expected_scenarios"]
    if len(expected) > MAX_SCENARIOS:
        raise CompareError(f"policy requests more than {MAX_SCENARIOS} scenarios")
    if mode == "revision":
        repetitions = args.repetitions
        if not 1 <= repetitions <= MAX_REPETITIONS:
            raise CompareError(
                f"repetitions must be from 1 through {MAX_REPETITIONS}"
            )
        if repetitions < policy["required_repetitions"]:
            raise CompareError(
                f"repetitions must be at least policy required_repetitions "
                f"({policy['required_repetitions']})"
            )
        if args.suite == "pr_headless" and args.profile != "pr_headless":
            raise CompareError("pr_headless suite requires the pr_headless profile")
        if args.suite in GPU_SUITES and args.profile != args.suite:
            raise CompareError(f"{args.suite} suite requires the {args.suite} profile")
        baseline_commit = _resolve_ref(args.baseline, "baseline", runner)
        candidate_commit = _resolve_ref(args.candidate, "candidate", runner)
        _reject_dirty_tree(runner)
    else:
        baseline_commit = candidate_commit = None
        repetitions = None
        # Validation comes before output creation, so bad input never leaves an owned dir.
        _load_bounded_results(Path(args.baseline_results), expected)
        _load_bounded_results(Path(args.candidate_results), expected)
        requested_output = Path(args.output).expanduser().absolute()
        for label, source in (
            ("baseline", Path(args.baseline_results).resolve()),
            ("candidate", Path(args.candidate_results).resolve()),
        ):
            if _is_within(requested_output, source) or _is_within(
                source, requested_output
            ):
                raise CompareError(
                    f"output and {label} results must not contain each other"
                )

    output = _create_output(Path(args.output))
    manifest = _new_manifest(mode, args.profile, policy_path)
    manifest["policy"]["version"] = policy["version"]
    if mode == "revision":
        manifest["build"]["cmake_options"] = (
            ["BUILD_TESTING=ON", "LG_DUEL_REQUIRE_SDL3=OFF"]
            if args.suite == "pr_headless"
            else [
                "BUILD_TESTING=ON",
                "LG_DUEL_REQUIRE_SDL3=ON",
                "LG_DUEL_FETCH_SDL3=ON",
                "CMAKE_DISABLE_FIND_PACKAGE_SDL3=ON",
            ]
        )
        manifest.update(
            {
                "suite": args.suite,
                "repetitions": repetitions,
                "execution_order": (
                    "Each benchmark call groups its repetitions. Headless workload "
                    "ownership alternates baseline/candidate between workloads."
                ),
                "revisions": {
                    "baseline": {
                        "requested": args.baseline,
                        "commit": baseline_commit,
                    },
                    "candidate": {
                        "requested": args.candidate,
                        "commit": candidate_commit,
                    },
                },
            }
        )
    _write_manifest(output, manifest)

    intended: list[Path] = []
    intended_builds: list[Path] = []
    owned_worktrees = output / "temp" / "worktrees"
    owned_builds = output / "temp" / "builds"
    try:
        if mode == "stored":
            baseline_source = Path(args.baseline_results).resolve()
            candidate_source = Path(args.candidate_results).resolve()
            _copy_tree(baseline_source, output / "raw" / "baseline")
            manifest["stage"] = "baseline-results-copied"
            _write_manifest(output, manifest)
            _copy_tree(candidate_source, output / "raw" / "candidate")
            manifest["stage"] = "candidate-results-copied"
            _write_manifest(output, manifest)
            baseline_set = _load_bounded_results(
                output / "raw" / "baseline", expected
            )
            candidate_set = _load_bounded_results(
                output / "raw" / "candidate", expected
            )
            baseline_commit = _result_commit(baseline_set)
            candidate_commit = _result_commit(candidate_set)
            manifest["revisions"] = {
                "baseline": {"commit": baseline_commit},
                "candidate": {"commit": candidate_commit},
            }
            for side, result_set in (
                ("baseline", baseline_set),
                ("candidate", candidate_set),
            ):
                for scenario, counts in _run_counts(result_set).items():
                    manifest["runs"].append(
                        {"side": side, "scenario": scenario, **counts}
                    )
            manifest["stage"] = "stored-results-loaded"
            _write_manifest(output, manifest)
        else:
            sides: dict[str, tuple[Path, Path, str]] = {}
            for side, commit in (
                ("baseline", baseline_commit),
                ("candidate", candidate_commit),
            ):
                source, build = _worktree_paths(output, side, commit)
                intended.append(source)
                intended_builds.append(build)
                _add_worktree(side, commit, source, runner, output, manifest)
                sides[side] = (source, build, commit)
            for side in ("baseline", "candidate"):
                source, build, _ = sides[side]
                _configure_and_build(
                    side, source, build, args.suite, runner, output, manifest
                )
            protocols = {
                side: _run_protocol_test(
                    side,
                    sides[side][0],
                    sides[side][1],
                    runner,
                    output,
                    manifest,
                )
                for side in ("baseline", "candidate")
            }
            raw = {
                side: output / "raw" / side
                for side in ("baseline", "candidate")
            }
            for path in raw.values():
                path.mkdir(parents=True, exist_ok=False)

            if args.suite == "pr_headless":
                shared_map_directory = sides["candidate"][0] / "maps"
                if not shared_map_directory.is_dir():
                    raise CompareError(
                        "candidate map directory is missing: "
                        f"{shared_map_directory}"
                    )
                manifest["shared_inputs"] = {
                    "headless_maps": {
                        "source": "candidate",
                        "commit": candidate_commit,
                        "path": "maps",
                    }
                }
                _write_manifest(output, manifest)
                # Each native call owns all repetitions. Swap the first side for
                # the second workload to keep grouped runs from sharing one order.
                orders = (
                    ("baseline", "candidate"),
                    ("candidate", "baseline"),
                )
                for workload, order in zip(HEADLESS_WORKLOADS, orders):
                    for side in order:
                        source, build, _ = sides[side]
                        with _benchmark_scope(source, build, raw[side]):
                            manifest["stage"] = f"{side}-run-{workload}"
                            _write_manifest(output, manifest)
                            operation_name = f"{side}-benchmark-{workload}"
                            try:
                                result = lg_benchmark.run_simulation_benchmark(
                                    workload,
                                    repetitions=repetitions,
                                    map_directory=shared_map_directory,
                                    warmup_batches=5,
                                    measured_batches=40,
                                    operations_per_batch=256,
                                    timeout=RUN_TIMEOUT,
                                    build_mode="release",
                                )
                            except BaseException as error:
                                manifest["commands"].append(
                                    {
                                        "name": operation_name,
                                        "argv": [
                                            "lg_benchmark.run_simulation_benchmark",
                                            workload,
                                            "--repetitions",
                                            str(repetitions),
                                            "--warmup-batches",
                                            "5",
                                            "--measured-batches",
                                            "40",
                                            "--operations-per-batch",
                                            "256",
                                        ],
                                        "cwd": _portable_arg(str(source), output),
                                        "return_code": None,
                                        "error": f"{type(error).__name__}: {error}",
                                    }
                                )
                                _write_manifest(output, manifest)
                                raise
                            manifest["commands"].append(
                                {
                                    "name": operation_name,
                                    "argv": [
                                        "lg_benchmark.run_simulation_benchmark",
                                        workload,
                                        "--repetitions",
                                        str(repetitions),
                                        "--warmup-batches",
                                        "5",
                                        "--measured-batches",
                                        "40",
                                        "--operations-per-batch",
                                        "256",
                                    ],
                                    "cwd": _portable_arg(str(source), output),
                                    "return_code": 0,
                                }
                            )
                            _rewrite_aggregate(result, protocols[side])
                            manifest["runs"].append(
                                {
                                    "side": side,
                                    "scenario": f"sim-{workload}",
                                    "requested": repetitions,
                                    "valid": int(
                                        result.get("aggregate", {}).get(
                                            "run_count", 0
                                        )
                                    )
                                    if result.get("aggregate", {}).get("valid")
                                    is True
                                    else 0,
                                }
                            )
                            _write_manifest(output, manifest)
            if args.suite in GPU_SUITES:
                gpu_scenarios = expected
                for side in ("baseline", "candidate"):
                    source, build, _ = sides[side]
                    _run_gpu(
                        side,
                        source,
                        build,
                        raw[side],
                        repetitions,
                        gpu_scenarios,
                        protocols[side],
                        manifest,
                        output,
                    )
            baseline_all = lg_performance_policy.load_result_directory(
                raw["baseline"]
            )
            candidate_all = lg_performance_policy.load_result_directory(
                raw["candidate"]
            )
            missing_baseline = sorted(
                set(expected) - set(baseline_all["scenarios"])
            )
            missing_candidate = sorted(
                set(expected) - set(candidate_all["scenarios"])
            )
            if missing_baseline or missing_candidate:
                raise CompareError(
                    "suite did not produce the policy scenarios; "
                    f"baseline missing {missing_baseline}, candidate missing {missing_candidate}"
                )
            baseline_set = {
                **baseline_all,
                "scenarios": {
                    name: baseline_all["scenarios"][name] for name in expected
                },
            }
            candidate_set = {
                **candidate_all,
                "scenarios": {
                    name: candidate_all["scenarios"][name] for name in expected
                },
            }

        status = _write_comparison(
            output,
            baseline_set,
            candidate_set,
            policy,
            baseline_commit=baseline_commit,
            candidate_commit=candidate_commit,
            repetitions=repetitions,
        )
        manifest["stage"] = "comparison-written"
        manifest["status"] = status
        _write_manifest(output, manifest)
    except BaseException as error:
        manifest["status"] = "ERROR"
        manifest["error"] = f"{type(error).__name__}: {error}"
        _write_manifest(output, manifest)
        raise
    finally:
        if mode == "revision":
            _cleanup_worktrees(
                runner, output, owned_worktrees, intended, manifest
            )
            _cleanup_builds(output, owned_builds, intended_builds, manifest)
            if manifest["status"] != "ERROR":
                manifest["stage"] = "complete"
            _write_manifest(output, manifest)
    cleanup_failed = any(
        item["status"] in {"failed", "inspection-failed", "unsafe"}
        for item in manifest["cleanup"]
    )
    if cleanup_failed:
        manifest["stage"] = "cleanup-failed"
        manifest["status"] = "ERROR"
        manifest["error"] = "one or more owned temp resources could not be removed"
        _write_manifest(output, manifest)
        raise CompareError(manifest["error"])
    return 0 if manifest["status"] in SUCCESS_STATUSES else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline")
    parser.add_argument("--candidate")
    parser.add_argument("--baseline-results")
    parser.add_argument("--candidate-results")
    parser.add_argument("--suite", choices=SUITES)
    parser.add_argument("--repetitions", type=int)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--policy", default=str(DEFAULT_POLICY))
    parser.add_argument("--output", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        return execute(build_parser().parse_args(argv))
    except (CompareError, lg_benchmark.BenchmarkError,
            lg_performance_policy.PerformancePolicyError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("error: interrupted", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
