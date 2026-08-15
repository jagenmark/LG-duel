#!/usr/bin/env python3
"""Offline benchmark orchestration and comparison for LG Duel."""

from __future__ import annotations

import argparse
import contextlib
import csv
import datetime as dt
import hashlib
import json
import math
import os
import platform
import re
import shutil
import socket
import statistics
import subprocess
import sys
import threading
import webbrowser
from pathlib import Path
from typing import Any, Callable, Iterable, Iterator

from lg_control import ControlError, send_request
import lg_frame_timeline_report
import lg_launch
from lg_launch import LaunchError, ensure_client


REPO_ROOT = Path(__file__).resolve().parents[1]
SCENARIO_ROOT = REPO_ROOT / "config" / "benchmarks"
RESULT_ROOT = REPO_ROOT / "build" / "benchmarks"
BASELINE_ROOT = RESULT_ROOT / "baselines"
BENCHMARK_STATE_ROOT = REPO_ROOT / "build" / "benchmark-control"
RESULT_SCHEMA_VERSION = 1
DEFAULT_SERVER_PORT = 28960
DEFAULT_CONTROL_PORT = 28961
# Kept as the old public name for callers which used it as a control port.
DEFAULT_PORT = DEFAULT_CONTROL_PORT
DEFAULT_TIMEOUT = 180.0
DEFAULT_STABLE_CV_PERCENT = 3.0
FRAME_TIMELINE_REPORT_DIR = "frame-timeline-report"
SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,79}$")
NATIVE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")
BUILD_MODES = {
    "release": {"directory": REPO_ROOT / "build" / "perf", "preset": "perf"},
    "debug": {"directory": REPO_ROOT / "build" / "default", "preset": "default"},
}
SDL_CONFIGURATION_OPTIONS = {
    "LG_DUEL_FETCH_SDL3",
    "LG_DUEL_REQUIRE_SDL3",
    "LG_DUEL_SDL3_GIT_TAG",
    "LG_DUEL_SDL3_SOURCE_DIR",
    "LG_DUEL_USE_PATCHED_SDL3",
}
_BENCHMARK_SCOPE_LOCK = threading.RLock()

GRAPHICS_CONTRACT_CVARS = {
    "r_world_frustum_cull": "world_frustum_cull",
    "r_world_gpu_indirect": "world_gpu_indirect",
    "r_antialiasing": "anti_aliasing",
    "r_sun_shadows": "sun_shadow_quality",
    "r_contact_shadows": "contact_shadows",
    "r_material_quality": "material_quality",
    "r_ambient_grounding": "ambient_grounding",
    "r_player_rim": "rim_quality",
    "r_atmosphere_grade": "atmosphere_grade",
    "r_bloom": "bloom",
    "r_render_scale": "render_scale",
}


class BenchmarkError(RuntimeError):
    """An actionable benchmark input, execution, or artifact error."""


def _valid_port(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 1 <= value <= 65535:
        raise BenchmarkError(f"{label} must be an integer from 1 through 65535")
    return value


def resolve_benchmark_ports(
    *,
    server_port: int = DEFAULT_SERVER_PORT,
    control_port: int | None = None,
    port: int | None = None,
) -> tuple[int, int]:
    """Resolve the old control-port alias and reject unsafe port pairs."""
    server_port = _valid_port(server_port, "server port")
    if control_port is not None and port is not None and control_port != port:
        raise BenchmarkError(
            "control_port and the legacy port alias must match when both are set"
        )
    selected_control = (
        control_port
        if control_port is not None
        else port if port is not None else DEFAULT_CONTROL_PORT
    )
    selected_control = _valid_port(selected_control, "control port")
    if server_port == selected_control:
        raise BenchmarkError("server port and control port must differ")
    return server_port, selected_control


def benchmark_state_directory(server_port: int, control_port: int) -> Path:
    server_port, control_port = resolve_benchmark_ports(
        server_port=server_port, control_port=control_port
    )
    return BENCHMARK_STATE_ROOT / f"{server_port}-{control_port}"


@contextlib.contextmanager
def claim_benchmark_session(state_dir: Path) -> Iterator[None]:
    """Claim one benchmark port-pair state root across local processes."""
    state_dir.mkdir(parents=True, exist_ok=True)
    lock_path = state_dir / "benchmark-session.lock"
    lock_file = lock_path.open("a+b")
    locked = False
    try:
        if lock_file.seek(0, os.SEEK_END) == 0:
            lock_file.write(b"\0")
            lock_file.flush()
        locked = lg_launch._try_file_lock(lock_file)
        if not locked:
            raise BenchmarkError(
                f"benchmark session is already claimed at {lock_path}; "
                "no process was changed"
            )
        yield
    finally:
        if locked:
            try:
                lg_launch._unlock_file(lock_file)
            except OSError:
                pass
        lock_file.close()


@contextlib.contextmanager
def benchmark_launcher_scope(
    server_port: int, control_port: int
) -> Iterator[Path]:
    """Point launcher state at one derived benchmark port-pair directory."""
    state_dir = benchmark_state_directory(server_port, control_port)
    keys = ("STATE_DIR", "STATE_PATH", "LOCAL_VULKAN_CONFIGS", "BENCHMARK_ROOT")
    with _BENCHMARK_SCOPE_LOCK:
        previous = {key: getattr(lg_launch, key) for key in keys}
        local_configs = [state_dir / "vulkan.json"]
        local_configs.extend(Path(path) for path in previous["LOCAL_VULKAN_CONFIGS"])
        deduped_configs = tuple(dict.fromkeys(local_configs))
        try:
            lg_launch.STATE_DIR = state_dir
            lg_launch.STATE_PATH = state_dir / "processes.json"
            lg_launch.LOCAL_VULKAN_CONFIGS = deduped_configs
            lg_launch.BENCHMARK_ROOT = RESULT_ROOT
            yield state_dir
        finally:
            for key, value in previous.items():
                setattr(lg_launch, key, value)


def _assert_benchmark_state_available(state_dir: Path) -> None:
    state_path = state_dir / "processes.json"
    if not state_path.exists():
        return
    try:
        document = json.loads(state_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BenchmarkError(
            f"benchmark state is corrupt at {state_path}; no process was changed"
        ) from error
    if not isinstance(document, dict):
        raise BenchmarkError(
            f"benchmark state is corrupt at {state_path}; no process was changed"
        )
    raise BenchmarkError(
        f"benchmark state is already in use at {state_path}; "
        "finish or recover that port pair before starting another run"
    )


def _bind_probe(port: int, socket_type: int, label: str) -> socket.socket:
    probe = socket.socket(socket.AF_INET, socket_type)
    try:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(("127.0.0.1", port))
        return probe
    except OSError as error:
        probe.close()
        raise BenchmarkError(
            f"benchmark {label} port 127.0.0.1:{port} is busy or unavailable; "
            "no connection was sent to the existing endpoint"
        ) from error


def _assert_benchmark_ports_available(
    server_port: int, control_port: int
) -> None:
    probes: list[socket.socket] = []
    try:
        probes.append(_bind_probe(server_port, socket.SOCK_DGRAM, "server"))
        probes.append(_bind_probe(control_port, socket.SOCK_STREAM, "control"))
    finally:
        for probe in probes:
            probe.close()


def benchmark_build(build_mode: str) -> tuple[Path, str]:
    try:
        selected = BUILD_MODES[build_mode]
    except KeyError as error:
        raise BenchmarkError("build mode must be 'release' or 'debug'") from error
    return Path(selected["directory"]), str(selected["preset"])


def _object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise BenchmarkError(f"{label} must be an object")
    return value


def _closed(obj: dict[str, Any], allowed: set[str], label: str) -> None:
    unknown = sorted(set(obj) - allowed)
    if unknown:
        raise BenchmarkError(f"{label} contains unknown field(s): {', '.join(unknown)}")


def _number(value: Any, label: str, *, minimum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise BenchmarkError(f"{label} must be a finite number")
    result = float(value)
    if minimum is not None and result < minimum:
        raise BenchmarkError(f"{label} must be at least {minimum:g}")
    return result


def _positive_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise BenchmarkError(f"{label} must be a positive integer")
    return value


def validate_safe_name(value: str, label: str = "name") -> str:
    if not isinstance(value, str) or not SAFE_NAME.fullmatch(value) or value in {".", ".."}:
        raise BenchmarkError(
            f"{label} must be filename-safe (letters, digits, '.', '_' or '-', max 80 characters)"
        )
    return value


def validate_native_name(value: str, label: str) -> str:
    if not isinstance(value, str) or not NATIVE_NAME.fullmatch(value):
        raise BenchmarkError(f"{label} may only use letters, digits, '_' and '-' (max 64 characters)")
    return value


def _duration(value: Any, label: str) -> dict[str, Any]:
    obj = _object(value, label)
    _closed(obj, {"seconds", "frames"}, label)
    if len(obj) != 1:
        raise BenchmarkError(f"{label} must specify exactly one of seconds or frames")
    if "seconds" in obj:
        seconds = _number(obj["seconds"], f"{label}.seconds", minimum=0.001)
        return {"seconds": seconds}
    return {"frames": _positive_int(obj["frames"], f"{label}.frames")}


def _vector3(value: Any, label: str) -> list[float]:
    if not isinstance(value, list) or len(value) != 3:
        raise BenchmarkError(f"{label} must be a three-number array")
    return [_number(item, f"{label}[{index}]") for index, item in enumerate(value)]


def _camera_pose(value: Any, label: str, *, keyframe: bool = False) -> dict[str, Any]:
    obj = _object(value, label)
    allowed = {"position", "pos", "yaw", "pitch", "fov"}
    if keyframe:
        allowed |= {"time_seconds", "progress"}
    _closed(obj, allowed, label)
    if ("position" in obj) == ("pos" in obj):
        raise BenchmarkError(f"{label} must specify exactly one of position or pos")
    result: dict[str, Any] = {
        "position": _vector3(obj.get("position", obj.get("pos")), f"{label}.position"),
        "yaw": _number(obj.get("yaw"), f"{label}.yaw"),
        "pitch": _number(obj.get("pitch"), f"{label}.pitch"),
    }
    if not -89.9 <= result["pitch"] <= 89.9:
        raise BenchmarkError(f"{label}.pitch must be between -89.9 and 89.9")
    if "fov" in obj:
        result["fov"] = _number(obj["fov"], f"{label}.fov", minimum=1.0)
    if keyframe:
        has_time = "time_seconds" in obj
        has_progress = "progress" in obj
        if has_time == has_progress:
            raise BenchmarkError(f"{label} must specify exactly one of time_seconds or progress")
        if has_time:
            result["time_seconds"] = _number(obj["time_seconds"], f"{label}.time_seconds", minimum=0.0)
        else:
            progress = _number(obj["progress"], f"{label}.progress", minimum=0.0)
            if progress > 1.0:
                raise BenchmarkError(f"{label}.progress must not exceed 1")
            result["progress"] = progress
    return result


def validate_scenario(document: Any, *, source: Path | None = None) -> dict[str, Any]:
    """Strictly validate and normalize a version-1 scenario descriptor."""
    obj = _object(document, "scenario")
    allowed = {
        "schema_version", "benchmark_version", "expected_benchmark_version", "name", "labels", "map",
        "backend_requirement", "resolution", "fullscreen", "vsync", "frame_cap", "fov",
        "warmup_seconds", "warmup_frames", "measured_seconds", "measured_frames", "camera_start",
        "camera_path", "player_state", "actors", "effects", "cvars", "screenshots",
        "residual_nondeterminism", "graphics_profile", "render_scale",
    }
    _closed(obj, allowed, "scenario")
    required = {
        "schema_version", "benchmark_version", "expected_benchmark_version", "name", "map",
        "backend_requirement", "resolution", "fullscreen", "vsync", "frame_cap", "fov", "camera_start",
        "player_state", "actors", "effects", "cvars", "screenshots",
    }
    missing = sorted(required - set(obj))
    if missing:
        raise BenchmarkError(f"scenario is missing required field(s): {', '.join(missing)}")
    if obj["schema_version"] != 1:
        raise BenchmarkError("scenario.schema_version must be 1")
    if obj["benchmark_version"] != 1 or obj["expected_benchmark_version"] != 1:
        raise BenchmarkError("scenario benchmark_version and expected_benchmark_version must be 1")
    name = validate_native_name(obj["name"], "scenario.name")
    if source is not None and source.stem != name:
        raise BenchmarkError(f"scenario.name '{name}' must match filename '{source.stem}.json'")
    map_name = obj["map"]
    if not isinstance(map_name, str) or not re.fullmatch(r"[A-Za-z0-9_-]+(?:\.map)?", map_name):
        raise BenchmarkError("scenario.map must be a safe map name")

    validate_safe_name(obj["backend_requirement"], "scenario.backend_requirement")
    resolution = obj["resolution"]
    if not isinstance(resolution, list) or len(resolution) != 2:
        raise BenchmarkError("scenario.resolution must be [width, height]")
    width = _positive_int(resolution[0], "scenario.resolution[0]")
    height = _positive_int(resolution[1], "scenario.resolution[1]")
    if width < 320 or height < 200 or width > 16384 or height > 16384:
        raise BenchmarkError("scenario.resolution must be between 320x200 and 16384x16384")
    if not isinstance(obj["fullscreen"], bool) or not isinstance(obj["vsync"], bool):
        raise BenchmarkError("scenario.fullscreen and scenario.vsync must be boolean")
    if isinstance(obj["frame_cap"], bool) or not isinstance(obj["frame_cap"], int) or obj["frame_cap"] < 0:
        raise BenchmarkError("scenario.frame_cap must be a non-negative integer")
    fov = _number(obj["fov"], "scenario.fov", minimum=30.0)
    if fov > 140.0:
        raise BenchmarkError("scenario.fov must not exceed 140")
    profile = obj.get("graphics_profile", "Default")
    if profile not in {"Low", "Default", "Competitive", "High"}:
        raise BenchmarkError("scenario.graphics_profile must be Low, Default, Competitive, or High")
    render_scale = _number(obj.get("render_scale", 1.0), "scenario.render_scale", minimum=0.5)
    if render_scale > 1.5:
        raise BenchmarkError("scenario.render_scale must not exceed 1.5")
    for prefix in ("warmup", "measured"):
        keys = [key for key in (f"{prefix}_seconds", f"{prefix}_frames") if key in obj]
        if len(keys) != 1:
            raise BenchmarkError(f"scenario must specify exactly one of {prefix}_seconds or {prefix}_frames")
        if keys[0].endswith("_frames"):
            _positive_int(obj[keys[0]], f"scenario.{keys[0]}")
        else:
            _number(obj[keys[0]], f"scenario.{keys[0]}", minimum=0.001)

    _camera_pose(obj["camera_start"], "scenario.camera_start")
    camera_path = obj.get("camera_path", [])
    if not isinstance(camera_path, list):
        raise BenchmarkError("scenario.camera_path must be an array")
    if camera_path:
        poses = [_camera_pose(value, f"scenario.camera_path[{i}]", keyframe=True) for i, value in enumerate(camera_path)]
        time_kind = "time_seconds" if "time_seconds" in poses[0] else "progress"
        if any(time_kind not in pose for pose in poses):
            raise BenchmarkError("scenario.camera_path keyframes must use one timing kind")
        times = [pose[time_kind] for pose in poses]
        if times != sorted(times) or len(times) != len(set(times)):
            raise BenchmarkError("scenario.camera_path keyframe timing must be strictly increasing")

    state = _object(obj["player_state"], "scenario.player_state")
    _closed(state, {"alive", "spectator", "weapon", "attack", "hide_hud", "hide_overlays"}, "scenario.player_state")
    if not any(key in state for key in ("alive", "spectator")):
        raise BenchmarkError("scenario.player_state must define alive or spectator state")
    if any(key in state and not isinstance(state[key], bool) for key in ("alive", "spectator", "attack", "hide_hud", "hide_overlays")):
        raise BenchmarkError("scenario.player_state boolean fields must be boolean")
    if "weapon" in state and (not isinstance(state["weapon"], str) or not state["weapon"]):
        raise BenchmarkError("scenario.player_state.weapon must be a non-empty string")

    actors = _object(obj["actors"], "scenario.actors")
    _closed(actors, {"bots", "weapon", "attack_mode", "stare", "standstill", "dodge", "dodge_min_ms", "dodge_max_ms", "expected_count", "commands"}, "scenario.actors")
    for key in ("bots", "dodge_min_ms", "dodge_max_ms", "expected_count"):
        if key in actors and (isinstance(actors[key], bool) or not isinstance(actors[key], int) or actors[key] < 0):
            raise BenchmarkError(f"scenario.actors.{key} must be a non-negative integer")
    if "commands" in actors and (not isinstance(actors["commands"], list) or not all(isinstance(x, str) for x in actors["commands"])):
        raise BenchmarkError("scenario.actors.commands must be a string array")
    if "weapon" in actors and (not isinstance(actors["weapon"], str) or not actors["weapon"]):
        raise BenchmarkError("scenario.actors.weapon must be a non-empty string")
    for key in ("stare", "standstill", "dodge"):
        if key in actors and not isinstance(actors[key], bool):
            raise BenchmarkError(f"scenario.actors.{key} must be boolean")

    if not isinstance(obj["effects"], dict):
        raise BenchmarkError("scenario.effects must be an object")
    cvars = obj["cvars"]
    if not isinstance(cvars, dict) or not all(isinstance(k, str) and re.fullmatch(r"(?:cl|r|s|vid)_[A-Za-z0-9_]+", k) for k in cvars):
        raise BenchmarkError("scenario presentation cvars must be an object of presentation-only cvar names")
    if not all(isinstance(value, (str, int, float, bool)) for value in cvars.values()):
        raise BenchmarkError("scenario presentation cvar values must be scalar")

    screenshots = obj["screenshots"]
    if not isinstance(screenshots, list):
        raise BenchmarkError("scenario screenshots must be an array")
    normalized_screenshots: list[dict[str, Any]] = []
    for index, value in enumerate(screenshots):
        shot = _object(value, f"scenario.screenshots[{index}]")
        _closed(shot, {"name", "time_seconds", "progress", "frame"}, f"scenario.screenshots[{index}]")
        if "name" not in shot:
            raise BenchmarkError(f"scenario.screenshots[{index}].name is required")
        timing = [key for key in ("time_seconds", "progress", "frame") if key in shot]
        if len(timing) != 1:
            raise BenchmarkError(f"scenario.screenshots[{index}] must specify exactly one checkpoint")
        normalized = {"name": validate_native_name(shot["name"], "screenshot name")}
        key = timing[0]
        normalized[key] = _positive_int(shot[key], f"screenshot.{key}") if key == "frame" else _number(shot[key], f"screenshot.{key}", minimum=0.0)
        if key == "progress" and normalized[key] > 1:
            raise BenchmarkError("screenshot.progress must not exceed 1")
        normalized_screenshots.append(normalized)

    if "labels" in obj and (not isinstance(obj["labels"], list) or not all(isinstance(x, str) and x for x in obj["labels"])):
        raise BenchmarkError("scenario.labels must be a non-empty string array")
    if "residual_nondeterminism" in obj and (not isinstance(obj["residual_nondeterminism"], list) or not all(isinstance(x, str) for x in obj["residual_nondeterminism"])):
        raise BenchmarkError("scenario.residual_nondeterminism must be a string array")

    # Normalize benchmark-wide defaults before hashing, sending, and embedding
    # the descriptor so every artifact records the exact applied contract.
    normalized = json.loads(json.dumps(obj, ensure_ascii=False, allow_nan=False))
    normalized["cvars"].setdefault("s_volume", 0)
    return normalized


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8")


def scenario_hash(scenario: dict[str, Any]) -> str:
    return hashlib.sha256(canonical_json(scenario)).hexdigest()


def load_scenario(name_or_path: str | Path) -> tuple[dict[str, Any], Path, str]:
    text = str(name_or_path)
    candidate = Path(text)
    if candidate.is_absolute() or candidate.parent != Path("."):
        raise BenchmarkError("scenario must be a safe scenario name, not a path")
    name = candidate.stem if candidate.suffix == ".json" else text
    validate_safe_name(name, "scenario")
    path = (SCENARIO_ROOT / f"{name}.json").resolve()
    if path.parent != SCENARIO_ROOT.resolve():
        raise BenchmarkError("scenario path escapes config/benchmarks")
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise BenchmarkError(f"benchmark scenario not found: {path}") from error
    except json.JSONDecodeError as error:
        raise BenchmarkError(f"invalid benchmark scenario JSON at {path}:{error.lineno}: {error.msg}") from error
    scenario = validate_scenario(raw, source=path)
    return scenario, path, scenario_hash(scenario)


def list_scenarios() -> dict[str, Any]:
    scenarios = []
    if SCENARIO_ROOT.exists():
        for path in sorted(SCENARIO_ROOT.glob("*.json")):
            try:
                scenario = validate_scenario(json.loads(path.read_text(encoding="utf-8")), source=path)
                scenarios.append({"name": scenario["name"], "path": str(path), "hash": scenario_hash(scenario), "valid": True})
            except (BenchmarkError, json.JSONDecodeError) as error:
                scenarios.append({"name": path.stem, "path": str(path), "valid": False, "error": str(error)})
    return {"scenarios": scenarios, "count": len(scenarios)}


def percentile(values: Iterable[float], percent: float) -> float:
    ordered = sorted(float(value) for value in values)
    if not ordered:
        raise BenchmarkError("cannot calculate a percentile of no values")
    if not 0 <= percent <= 100:
        raise BenchmarkError("percentile must be between 0 and 100")
    # Match the native collector: nearest rank is an observed frame, never an
    # interpolated value. The one-based rank is ceil(p * N).
    rank = max(1, math.ceil(percent / 100.0 * len(ordered)))
    return ordered[rank - 1]


def tukey_outliers(values: list[float]) -> dict[str, Any]:
    if not values:
        return {"q1": None, "q3": None, "iqr": None, "lower_fence": None, "upper_fence": None, "indices": []}
    q1, q3 = percentile(values, 25), percentile(values, 75)
    iqr = q3 - q1
    lower, upper = q1 - 1.5 * iqr, q3 + 1.5 * iqr
    return {"q1": q1, "q3": q3, "iqr": iqr, "lower_fence": lower, "upper_fence": upper,
            "indices": [index for index, value in enumerate(values) if value < lower or value > upper]}


def summarize_values(values: list[float]) -> dict[str, Any]:
    if not values:
        return {"count": 0}
    mean = statistics.fmean(values)
    stddev = statistics.stdev(values) if len(values) > 1 else 0.0
    return {
        "count": len(values), "min": min(values), "median": statistics.median(values),
        "mean": mean, "p95": percentile(values, 95), "p99": percentile(values, 99), "max": max(values),
        "stddev": stddev, "cv_percent": (stddev / mean * 100.0) if mean else 0.0,
    }


METRIC_ALIASES = {
    "cpu_ms": ("cpu_ms", "cpu_frame_ms", "cpu_time_ms"),
    "render_ms": ("render_ms", "render_frame_ms", "render_time_ms", "gpu_ms"),
    "frame_ms": ("frame_ms", "frame_time_ms", "total_ms"),
}


def _numeric_metric(source: dict[str, Any], names: Iterable[str]) -> float | None:
    for name in names:
        value = source.get(name)
        if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
            return float(value)
    return None


def _run_metric(run: dict[str, Any], metric: str, statistic: str = "median") -> float | None:
    summary = run.get("summary", run)
    if not isinstance(summary, dict):
        return None
    aliases = METRIC_ALIASES.get(metric, (metric,))
    for alias in aliases:
        value = summary.get(alias)
        if isinstance(value, dict):
            found = _numeric_metric(value, (statistic, "value"))
            if found is not None:
                return found
        elif statistic == "median" and isinstance(value, (int, float)) and not isinstance(value, bool):
            return float(value)
    direct_names = tuple(f"{name}_{statistic}" for name in aliases)
    return _numeric_metric(summary, direct_names)


def aggregate_runs(runs: list[dict[str, Any]], *, stable_cv_percent: float = DEFAULT_STABLE_CV_PERCENT) -> dict[str, Any]:
    if not runs:
        raise BenchmarkError("cannot aggregate zero benchmark runs")
    metric_names = set(METRIC_ALIASES)
    for run in runs:
        summary = run.get("summary", {})
        if isinstance(summary, dict):
            metric_names.update(key for key, value in summary.items() if isinstance(value, (int, float, dict)))
    metrics: dict[str, Any] = {}
    for metric in sorted(metric_names):
        values = [value for run in runs if (value := _run_metric(run, metric)) is not None]
        if values:
            stats = summarize_values(values)
            # Frame-tail statistics are first calculated inside each native run.
            # Use their cross-run medians so a faster median cannot hide worse p95/p99.
            for statistic in ("p95", "p99", "max"):
                tail_values = [value for run in runs if (value := _run_metric(run, metric, statistic)) is not None]
                if tail_values:
                    stats[statistic] = statistics.median(tail_values)
                    stats[f"run_{statistic}_values"] = tail_values
            stats["run_values"] = values
            stats["outliers"] = tukey_outliers(values)
            metrics[metric] = stats
    headline = "frame_ms" if "frame_ms" in metrics else "cpu_ms" if "cpu_ms" in metrics else next(iter(metrics), None)
    cv = metrics.get(headline, {}).get("cv_percent") if headline else None
    valid = all(run.get("valid", True) is True for run in runs)
    return {
        "schema_version": RESULT_SCHEMA_VERSION, "run_count": len(runs), "valid": valid,
        "headline_metric": headline, "stable_cv_threshold_percent": stable_cv_percent,
        "stable": bool(valid and cv is not None and cv <= stable_cv_percent), "metrics": metrics,
        "outlier_runs": sorted({index + 1 for stats in metrics.values() for index in stats["outliers"]["indices"]}),
    }


def _git_metadata() -> dict[str, Any]:
    def git(*args: str) -> str | None:
        try:
            return subprocess.run(["git", *args], cwd=REPO_ROOT, text=True, capture_output=True, check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return None
    commit = git("rev-parse", "HEAD")
    status = git("status", "--porcelain")
    return {"commit": commit or "unknown", "dirty": bool(status) if status is not None else None}


def _parse_vulkan_summary(output: str) -> dict[str, Any]:
    fields: dict[str, str] = {}
    for line in output.splitlines():
        match = re.match(r"\s*(apiVersion|driverVersion|deviceType|deviceName|driverName|driverInfo)\s*=\s*(.+?)\s*$", line)
        if match and match.group(1) not in fields:
            fields[match.group(1)] = match.group(2)
    return {
        "gpu_name": fields.get("deviceName"),
        "gpu_type": fields.get("deviceType"),
        "graphics_driver_name": fields.get("driverName"),
        "graphics_driver_version": fields.get("driverInfo") or fields.get("driverVersion"),
        "vulkan_api_version": fields.get("apiVersion"),
    }


def _vulkan_metadata(renderer: Any) -> dict[str, Any]:
    renderer_text = str(renderer or "").lower()
    if "vulkan" not in renderer_text:
        return {"vulkan_metadata_status": "not-applicable"}

    driver_variables = {
        name: os.environ.get(name)
        for name in ("VK_DRIVER_FILES", "VK_ICD_FILENAMES", "VK_ADD_DRIVER_FILES")
        if os.environ.get(name)
    }
    metadata: dict[str, Any] = {
        "vulkan_metadata_status": "unavailable",
        "vulkan_driver_environment": driver_variables,
    }
    effective = driver_variables.get("VK_DRIVER_FILES") or driver_variables.get("VK_ICD_FILENAMES")
    if effective:
        manifests = [Path(item) for item in effective.split(os.pathsep) if item]
        metadata["vulkan_icd_manifests"] = [str(path) for path in manifests]
        manifest_records = []
        for path in manifests:
            record: dict[str, Any] = {"path": str(path), "exists": path.is_file()}
            if path.is_file():
                try:
                    record["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
                    parsed = json.loads(path.read_text(encoding="utf-8-sig"))
                    library = parsed.get("ICD", {}).get("library_path") if isinstance(parsed, dict) else None
                    if isinstance(library, str):
                        record["library_path"] = str((path.parent / library).resolve())
                except (OSError, json.JSONDecodeError) as error:
                    record["read_error"] = str(error)
            manifest_records.append(record)
        metadata["vulkan_icd_manifest_records"] = manifest_records

    try:
        completed = subprocess.run(
            ["vulkaninfo", "--summary"], text=True, capture_output=True,
            timeout=8.0, check=False, env=os.environ.copy(),
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        metadata["vulkan_metadata_error"] = str(error)
        return metadata
    if completed.returncode != 0:
        metadata["vulkan_metadata_error"] = (
            completed.stderr.strip() or completed.stdout.strip() or f"vulkaninfo exited {completed.returncode}"
        )
        return metadata
    parsed = _parse_vulkan_summary(completed.stdout)
    if not parsed.get("gpu_name"):
        metadata["vulkan_metadata_error"] = "vulkaninfo returned no physical device"
        return metadata
    metadata.update(parsed)
    metadata["vulkan_metadata_status"] = "available"
    return metadata


def environment_metadata(status: dict[str, Any] | None = None) -> dict[str, Any]:
    status = status or {}
    renderer = status.get("renderer")
    protocol = status.get("game_protocol_version", status.get("protocol_version"))
    metadata: dict[str, Any] = {
        "os": platform.platform(), "cpu": platform.processor() or platform.machine(),
        "architecture": platform.machine(),
        "logical_cores": os.cpu_count(), "python": platform.python_version(),
        "renderer": renderer, "protocol_version": protocol,
        "control_protocol_version": status.get("control_protocol"),
    }
    for key in (
        "gpu_name", "gpu_type", "graphics_driver_name", "graphics_driver_version",
        "vulkan_api_version", "vulkan_metadata_status", "vulkan_driver_environment",
        "vulkan_icd_manifests", "vulkan_icd_manifest_records", "software_renderer",
        "gpu_verification_state", "gpu_verified", "vulkan_selection_source",
    ):
        if key in status:
            metadata[key] = status[key]
    if "vulkan_metadata_status" not in metadata:
        metadata.update(_vulkan_metadata(renderer))
    return metadata


def build_environment_metadata(build_dir: Path) -> dict[str, Any]:
    """Read comparable build facts without changing the configured build."""
    cache_path = build_dir / "CMakeCache.txt"
    values: dict[str, str] = {}
    try:
        for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line or line.startswith(("#", "//")) or "=" not in line:
                continue
            key_and_type, value = line.split("=", 1)
            key = key_and_type.split(":", 1)[0]
            values[key] = value
    except OSError:
        return {"build_metadata_status": "unavailable"}

    compiler_path = values.get("CMAKE_CXX_COMPILER")
    metadata: dict[str, Any] = {
        "build_metadata_status": "available",
        "build_type": values.get("CMAKE_BUILD_TYPE"),
        "cmake_generator": values.get("CMAKE_GENERATOR"),
        "compiler": Path(compiler_path).name if compiler_path else None,
        "compiler_path": compiler_path,
        "compile_time_options": {
            key: values[key]
            for key in sorted(values)
            if (
                key == "BUILD_TESTING"
                or key.startswith("LG_DUEL_")
            )
            and key not in SDL_CONFIGURATION_OPTIONS
        },
        "sdl_configuration": {
            key: values[key]
            for key in sorted(SDL_CONFIGURATION_OPTIONS)
            if key in values
        },
    }
    if compiler_path:
        try:
            completed = subprocess.run(
                [compiler_path, "--version"],
                text=True,
                capture_output=True,
                timeout=10.0,
                check=False,
            )
            first_line = (completed.stdout or completed.stderr).splitlines()
            metadata["compiler_version"] = first_line[0].strip() if first_line else None
        except (OSError, subprocess.TimeoutExpired):
            metadata["compiler_version"] = None
    return metadata


def source_protocol_version(repo_root: Path = REPO_ROOT) -> int | None:
    """Read the wire version used by headless tools that have no live status."""
    header = repo_root / "src" / "net" / "NetCodec.hpp"
    try:
        text = header.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"\bkProtocolVersion\s*=\s*(\d+)", text)
    return int(match.group(1)) if match else None


def source_fixed_tick_rate(repo_root: Path = REPO_ROOT) -> float | None:
    header = repo_root / "src" / "shared" / "Constants.hpp"
    try:
        text = header.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"\bkFixedTickRate\s*=\s*(\d+(?:\.\d+)?)F?", text)
    return float(match.group(1)) if match else None


def _attach_run_conditions(normalized: dict[str, Any], conditions: dict[str, Any]) -> None:
    """Persist orchestration metadata in the native per-run result artifact."""
    result_path = normalized.get("result_path")
    if not isinstance(result_path, str):
        return
    path = Path(result_path)
    try:
        native = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(native, dict):
            return
        native["run_conditions"] = conditions
        _write_json(path, native)
        normalized["native"] = {key: value for key, value in native.items() if key != "samples"}
    except (OSError, json.JSONDecodeError):
        return


def build_run_request(scenario: dict[str, Any], digest: str, run_id: str, run_group: str) -> dict[str, Any]:
    """Build the single adapter boundary shared with the native control operation."""
    validate_native_name(run_id, "run_id")
    validate_native_name(run_group, "run_group")
    return {"scenario": scenario, "scenario_hash": digest, "run_id": run_id, "run_group": run_group}


def _start_client(
    server_port: int,
    control_port: int,
    timeout: float,
    build_mode: str = "release",
) -> dict[str, Any]:
    build_dir, preset = benchmark_build(build_mode)
    try:
        return ensure_client(
            renderer="gpu",
            benchmark=True,
            server_port=server_port,
            control_port=control_port,
            timeout=min(timeout, 30.0), build_dir=build_dir,
        )
    except LaunchError as error:
        raise BenchmarkError(
            f"benchmark client failed GPU startup verification for the '{build_mode}' build: {error}; "
            f"run cmake --preset {preset} and cmake --build --preset {preset}"
        ) from error


def _safe_artifact_path(path_text: str, run_dir: Path) -> Path:
    path = Path(path_text)
    if path.is_absolute():
        resolved = path.resolve()
    elif path.parts and path.parts[0].lower() == "build":
        resolved = (REPO_ROOT / path).resolve()
    else:
        resolved = (run_dir / path).resolve()
    allowed = RESULT_ROOT.resolve()
    if resolved != allowed and allowed not in resolved.parents:
        raise BenchmarkError(f"native benchmark returned artifact outside build/benchmarks: {resolved}")
    return resolved


def _native_summary(summary: dict[str, Any]) -> dict[str, Any]:
    mapping = {
        "count": "count", "mean_ms": "mean", "median_ms": "median", "p95_ms": "p95",
        "p99_ms": "p99", "max_ms": "max", "min_ms": "min", "stddev_ms": "stddev",
    }
    frame = {target: summary[source] for source, target in mapping.items() if isinstance(summary.get(source), (int, float))}
    if "mean" in frame:
        frame["cv_percent"] = frame.get("stddev", 0.0) / frame["mean"] * 100.0 if frame["mean"] else 0.0
    return {"frame_ms": frame} if frame else dict(summary)


def graphics_contract_from_native(native: dict[str, Any], profile: str) -> dict[str, Any]:
    """Read the effective contract from the native client before it restores cvars."""
    effective = native.get("effective_cvars")
    if not isinstance(effective, dict):
        raise BenchmarkError("native benchmark result is missing effective_cvars")
    contract: dict[str, Any] = {"profile": profile, "effective_cvars": {}}
    for cvar, field in GRAPHICS_CONTRACT_CVARS.items():
        value = effective.get(cvar)
        if not isinstance(value, str) or not value:
            raise BenchmarkError(
                f"native benchmark result is missing effective graphics cvar {cvar}"
            )
        contract[field] = value
        contract["effective_cvars"][cvar] = value
    return contract


def _telemetry_metrics(path: Path, *, prefix: str = "") -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
    except OSError as error:
        raise BenchmarkError(f"could not read native telemetry {path}: {error}") from error
    values: dict[str, list[float]] = {}
    for row in rows:
        for key, raw in row.items():
            if key in {"frame", "tick", "render_frame", "elapsed_seconds"} or raw is None:
                continue
            try:
                value = float(raw)
            except ValueError:
                continue
            if math.isfinite(value):
                values.setdefault(key, []).append(value)
    return {
        f"{prefix}{key}": summarize_values(samples)
        for key, samples in values.items()
        if samples
    }


def _normalize_native_result(response: dict[str, Any], run_dir: Path, run_id: str) -> dict[str, Any]:
    source = response
    if isinstance(response.get("result_path"), str):
        native_path = _safe_artifact_path(response["result_path"], run_dir)
        try:
            loaded = json.loads(native_path.read_text(encoding="utf-8"))
            if isinstance(loaded, dict):
                source = {**loaded, **response}
        except (OSError, json.JSONDecodeError) as error:
            # A control response may already carry a complete summary.  Keep
            # that current result usable if an optional artifact cannot be
            # read, but still fail when the artifact is the only summary.
            if not isinstance(response.get("summary"), dict):
                raise BenchmarkError(f"could not read native benchmark result {native_path}: {error}") from error
    raw_summary = source.get("summary", {})
    if not isinstance(raw_summary, dict):
        raise BenchmarkError("native benchmark response summary must be an object")
    summary = _native_summary(raw_summary)
    telemetry_text = source.get("telemetry_path", response.get("telemetry_path"))
    if isinstance(telemetry_text, str):
        telemetry_path = _safe_artifact_path(telemetry_text, run_dir)
        summary.update(_telemetry_metrics(telemetry_path))
    simulation_ticks_text = source.get(
        "simulation_ticks_path", response.get("simulation_ticks_path")
    )
    if isinstance(simulation_ticks_text, str):
        simulation_ticks_path = _safe_artifact_path(simulation_ticks_text, run_dir)
        summary.update(
            _telemetry_metrics(simulation_ticks_path, prefix="simulation_tick_")
        )
    thresholds = source.get("thresholds_ms", {})
    if isinstance(thresholds, dict):
        for threshold, item in thresholds.items():
            if isinstance(item, dict):
                for field in ("count_over", "percent_over"):
                    if isinstance(item.get(field), (int, float)):
                        summary[f"threshold_{threshold}_{field}"] = float(item[field])
    validity = source.get("validity", source.get("valid", True))
    if isinstance(validity, dict):
        explicit = validity.get("valid")
        checks = [value for key, value in validity.items() if key != "valid" and isinstance(value, bool)]
        is_valid = (explicit is not False) and all(checks)
    else:
        is_valid = validity is not False and validity != "invalid"
    result: dict[str, Any] = {"run_id": run_id, "valid": is_valid, "validity": validity, "summary": summary}
    for key in ("result_path", "result_directory"):
        if isinstance(response.get(key), str):
            result[key] = str(_safe_artifact_path(response[key], run_dir))
    screenshots = source.get("screenshots", [])
    if isinstance(screenshots, list):
        result["screenshots"] = [
            ({**shot, "path": str(_safe_artifact_path(shot["path"], run_dir))}
             if isinstance(shot, dict) and isinstance(shot.get("path"), str)
             else str(_safe_artifact_path(shot, run_dir)) if isinstance(shot, str) else shot)
            for shot in screenshots
        ]
    result["native"] = {key: value for key, value in source.items() if key not in {"samples"}}
    # New renderers may publish per-pass diagnostics.  Older native artifacts
    # omit this key, which remains valid and comparable under existing policy.
    diagnostics = source.get("render_pass_diagnostics")
    if isinstance(diagnostics, dict):
        result["render_pass_diagnostics"] = diagnostics
    return result


def _timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


def render_report(result: dict[str, Any]) -> str:
    aggregate = result.get("aggregate", result)
    lines = [f"# LG Duel benchmark: {result.get('scenario', {}).get('name', result.get('scenario_name', 'unknown'))}", ""]
    git = result.get("git", {})
    environment = result.get("environment", {})
    settings = result.get("settings", {})
    lines += [f"- Result: `{result.get('result_directory', '')}`", f"- Runs: {aggregate.get('run_count', 0)}",
              f"- Valid: {aggregate.get('valid', False)}", f"- Stable: {aggregate.get('stable', False)}",
              f"- Commit: `{git.get('commit', 'unknown')}` (dirty: {git.get('dirty', 'unknown')})",
              f"- Host: {environment.get('os', 'unknown')} / {environment.get('cpu', 'unknown')} / {environment.get('logical_cores', 'unknown')} logical cores",
              f"- Build mode: {environment.get('build_mode', 'unknown')}",
              f"- Executable SHA-256: `{environment.get('executable_sha256', 'unknown')}`",
              f"- Renderer: {environment.get('renderer', 'unknown')} / requested {settings.get('backend', 'unknown')} / resolution {settings.get('resolution', 'unknown')}",
              f"- Protocol/benchmark version: {environment.get('protocol_version', 'unknown')}",
              f"- Started/completed: {result.get('started_at', 'unknown')} / {result.get('completed_at', 'unknown')}",
              "", "## Metrics", ""]
    lines.append("| Metric | Median | p95 | p99 | Max | CV |")
    lines.append("|---|---:|---:|---:|---:|---:|")
    for name, stats in aggregate.get("metrics", {}).items():
        lines.append(f"| {name} | {stats.get('median', 0):.4g} | {stats.get('p95', 0):.4g} | {stats.get('p99', 0):.4g} | {stats.get('max', 0):.4g} | {stats.get('cv_percent', 0):.2f}% |")
    if aggregate.get("outlier_runs"):
        lines += ["", f"Tukey outlier run(s), retained in all statistics: {aggregate['outlier_runs']}."]
    timeline_reports = result.get("frame_timeline_reports", [])
    available_reports = [
        entry for entry in timeline_reports
        if isinstance(entry, dict) and entry.get("status") == "available"
    ]
    if available_reports:
        lines += ["", "## Frame timeline reports", ""]
        for entry in available_reports:
            lines.append(f"- {entry['run_id']}: `{entry['html_path']}`")
    return "\n".join(lines) + "\n"


def _frame_timeline_report_path(result_dir: Path, run_id: str) -> Path:
    validate_native_name(run_id, "run id")
    return result_dir / FRAME_TIMELINE_REPORT_DIR / run_id


def _write_frame_timeline_report(result_dir: Path, run_id: str) -> dict[str, Any]:
    """Create a report from one completed native run without changing its raw data."""
    run_dir = result_dir / run_id
    output = _frame_timeline_report_path(result_dir, run_id)
    try:
        artifact = lg_frame_timeline_report.write_report(run_dir, output)
    except (lg_frame_timeline_report.ReportError, OSError) as error:
        return {
            "run_id": run_id,
            "status": "unavailable",
            "reason": str(error),
        }
    summary = artifact["analysis"]["summary"]
    return {
        "run_id": run_id,
        "status": "available",
        "html_path": str(output / "frame-timeline.html"),
        "svg_path": str(output / "frame-timeline.svg"),
        "analysis_path": str(output / "timeline-analysis.json"),
        "gpu_execution_timing_available": bool(
            artifact["source"].get("gpu_execution_timing_available")
        ),
        "gpu_sample_count": int(summary["gpu_sample_count"]),
    }


def _write_frame_timeline_reports(result_dir: Path, runs: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        _write_frame_timeline_report(result_dir, str(run["run_id"]))
        for run in runs
        if isinstance(run.get("run_id"), str)
    ]


def _run_benchmark_with_session(
    scenario_name: str,
    *,
    repetitions: int,
    server_port: int,
    control_port: int,
    timeout: float,
    request_sender: Callable[..., dict[str, Any]],
    start_client: bool,
    build_mode: str,
    graphics_profile: str | None,
    status: dict[str, Any],
    state_dir: Path,
) -> dict[str, Any]:
    repetitions = _positive_int(repetitions, "repetitions")
    build_dir, _ = benchmark_build(build_mode)
    scenario, scenario_path, digest = load_scenario(scenario_name)
    if graphics_profile is not None:
        if graphics_profile not in {"Low", "Default", "Competitive", "High"}:
            raise BenchmarkError(
                "graphics profile must be Low, Default, Competitive, or High"
            )
        scenario = dict(scenario)
        scenario["graphics_profile"] = graphics_profile
        digest = scenario_hash(scenario)
    requested_backend = scenario["backend_requirement"].lower()
    actual_renderer = str(status.get("renderer", ""))
    if requested_backend == "gpu" and status and (
        actual_renderer != "SDL_GPU/vulkan" or status.get("gpu_verified") is not True
    ):
        raise BenchmarkError(
            "scenario requires a verified SDL_GPU/vulkan session, but the active "
            f"renderer is {status.get('renderer', 'unknown')} with verification "
            f"state {status.get('gpu_verification_state', 'unknown')}"
        )
    git = _git_metadata()
    commit_short = str(git["commit"])[:12]
    run_group = f"{_timestamp()}-{commit_short}"
    result_dir = RESULT_ROOT / scenario["name"] / run_group
    suffix = 1
    original = result_dir
    while result_dir.exists():
        run_group = f"{original.name}-{suffix}"
        result_dir = original.parent / run_group
        suffix += 1
    result_dir.mkdir(parents=True)
    started_at = dt.datetime.now(dt.timezone.utc).isoformat()
    environment = environment_metadata(status)
    environment.update(build_environment_metadata(build_dir))
    environment["server_tick_rate"] = source_fixed_tick_rate(REPO_ROOT)
    environment["build_mode"] = build_mode
    environment["build_directory"] = str(build_dir)
    environment["benchmark_server_port"] = server_port
    environment["benchmark_control_port"] = control_port
    environment["benchmark_state_directory"] = str(state_dir)
    client_executable = build_dir / (
        "lg_duel_client.exe" if os.name == "nt" else "lg_duel_client"
    )
    if client_executable.is_file():
        environment["executable"] = str(client_executable)
        environment["executable_sha256"] = hashlib.sha256(client_executable.read_bytes()).hexdigest()
    run_conditions = {
        "git": git,
        "environment": environment,
        "scenario_hash": digest,
        "scenario_path": str(scenario_path),
        "launch_mode": "external" if not start_client else "owned",
    }
    runs: list[dict[str, Any]] = []
    graphics_contracts: list[dict[str, Any]] = []
    for index in range(1, repetitions + 1):
        run_id = f"run-{index}"
        run_dir = result_dir / run_id
        run_dir.mkdir()
        payload = build_run_request(scenario, digest, run_id, run_group)
        try:
            response = request_sender(
                "run_benchmark", port=control_port, timeout=timeout, **payload
            )
        except ControlError as error:
            raise BenchmarkError(f"{run_id} failed: {error}; partial artifacts remain at {result_dir}") from error
        normalized = _normalize_native_result(response, run_dir, run_id)
        try:
            contract = graphics_contract_from_native(
                normalized["native"], scenario.get("graphics_profile", "Default")
            )
        except BenchmarkError as error:
            raise BenchmarkError(f"{run_id} did not attest its applied graphics contract: {error}") from error
        normalized["graphics_contract"] = contract
        graphics_contracts.append(contract)
        _attach_run_conditions(normalized, run_conditions)
        _write_json(run_dir / "orchestration.json", normalized)
        runs.append(normalized)
    aggregate = aggregate_runs(runs)
    benchmark_version = runs[0].get("native", {}).get("benchmark_version") if runs else None
    environment["benchmark_version"] = benchmark_version
    observed_native = runs[0].get("native", {}) if runs else {}
    environment["observed_resolution"] = observed_native.get("actual_resolution")
    environment["selected_present_mode"] = observed_native.get("selected_present_mode")
    environment["gpu_timing_instrumentation_version"] = observed_native.get(
        "gpu_timing_instrumentation_version"
    )
    map_content_hash = observed_native.get("map_content_hash")
    graphics_contract = graphics_contracts[0]
    if any(contract != graphics_contract for contract in graphics_contracts[1:]):
        raise BenchmarkError("benchmark repetitions reported different applied graphics contracts")
    result = {
        "schema_version": RESULT_SCHEMA_VERSION, "scenario": scenario, "scenario_path": str(scenario_path),
        "scenario_hash": digest, "result_directory": str(result_dir), "started_at": started_at,
        "completed_at": dt.datetime.now(dt.timezone.utc).isoformat(), "git": git,
        "environment": environment, "settings": {
            "backend": scenario["backend_requirement"], "resolution": scenario["resolution"],
            "window_mode": "fullscreen" if scenario["fullscreen"] else "windowed",
            "vsync": scenario["vsync"], "frame_cap": scenario["frame_cap"], "fov": scenario["fov"],
            "presentation_cvars": scenario["cvars"],
            "graphics_profile": scenario.get("graphics_profile", "Default"),
            "render_scale": scenario.get("render_scale", 1.0),
            "graphics_contract": graphics_contract,
        }, "map_content_hash": map_content_hash, "runs": runs, "aggregate": aggregate,
    }
    result["frame_timeline_reports"] = _write_frame_timeline_reports(
        result_dir,
        runs,
    )
    _write_json(result_dir / "aggregate.json", result)
    (result_dir / "report.md").write_text(render_report(result), encoding="utf-8")
    return result


def _cleanup_benchmark_session() -> tuple[dict[str, Any] | None, str | None]:
    try:
        cleanup = lg_launch.stop_owned()
    except Exception as error:
        return None, f"{type(error).__name__}: {error}"
    if (
        cleanup.get("left_owned_running") is True
        or cleanup.get("left_unowned_running") is True
        or cleanup.get("state_preserved") is True
    ):
        return cleanup, f"launcher cleanup was incomplete: {cleanup}"
    return cleanup, None


def _mark_cleanup_failure(
    result: dict[str, Any],
    cleanup: dict[str, Any] | None,
    cleanup_error: str,
) -> None:
    result["launcher_cleanup"] = cleanup
    result["launcher_cleanup_error"] = cleanup_error
    aggregate = result.get("aggregate")
    if isinstance(aggregate, dict):
        aggregate["valid"] = False
        aggregate["stable"] = False
    result_directory = result.get("result_directory")
    if not isinstance(result_directory, str):
        return
    result_path = Path(result_directory) / "aggregate.json"
    _write_json(result_path, result)
    (Path(result_directory) / "report.md").write_text(
        render_report(result), encoding="utf-8"
    )


def run_benchmark(
    scenario_name: str,
    *,
    repetitions: int = 3,
    server_port: int = DEFAULT_SERVER_PORT,
    control_port: int | None = None,
    port: int | None = None,
    timeout: float = DEFAULT_TIMEOUT,
    request_sender: Callable[..., dict[str, Any]] = send_request,
    start_client: bool = True,
    build_mode: str = "release",
    graphics_profile: str | None = None,
) -> dict[str, Any]:
    """Run a benchmark without sharing normal visual-control ownership state."""
    server_port, control_port = resolve_benchmark_ports(
        server_port=server_port, control_port=control_port, port=port
    )
    state_dir = benchmark_state_directory(server_port, control_port)
    if not start_client:
        return _run_benchmark_with_session(
            scenario_name,
            repetitions=repetitions,
            server_port=server_port,
            control_port=control_port,
            timeout=timeout,
            request_sender=request_sender,
            start_client=False,
            build_mode=build_mode,
            graphics_profile=graphics_profile,
            status={},
            state_dir=state_dir,
        )

    with benchmark_launcher_scope(server_port, control_port) as state_dir:
        with claim_benchmark_session(state_dir):
            _assert_benchmark_state_available(state_dir)
            _assert_benchmark_ports_available(server_port, control_port)
            try:
                status = _start_client(
                    server_port, control_port, timeout, build_mode
                )
                result = _run_benchmark_with_session(
                    scenario_name,
                    repetitions=repetitions,
                    server_port=server_port,
                    control_port=control_port,
                    timeout=timeout,
                    request_sender=request_sender,
                    start_client=True,
                    build_mode=build_mode,
                    graphics_profile=graphics_profile,
                    status=status,
                    state_dir=state_dir,
                )
            except BaseException as error:
                _, cleanup_error = _cleanup_benchmark_session()
                if cleanup_error:
                    error.add_note(f"benchmark cleanup also failed: {cleanup_error}")
                raise

            cleanup, cleanup_error = _cleanup_benchmark_session()
            if cleanup_error:
                _mark_cleanup_failure(result, cleanup, cleanup_error)
                raise BenchmarkError(cleanup_error)
            result["launcher_cleanup"] = cleanup
            _write_json(Path(result["result_directory"]) / "aggregate.json", result)
            return result


def run_simulation_benchmark(
    workload: str,
    *,
    repetitions: int = 5,
    map_name: str = "overkill_import",
    map_directory: str | Path | None = None,
    warmup_batches: int = 5,
    measured_batches: int = 40,
    operations_per_batch: int = 256,
    timeout: float = DEFAULT_TIMEOUT,
    force_linear: bool = False,
    build_mode: str = "release",
) -> dict[str, Any]:
    if workload not in {"movement-collision", "trace-projectile"}:
        raise BenchmarkError("simulation workload must be movement-collision or trace-projectile")
    validate_safe_name(map_name, "map")
    repetitions = _positive_int(repetitions, "repetitions")
    warmup_batches = _positive_int(warmup_batches, "warmup batches")
    measured_batches = _positive_int(measured_batches, "measured batches")
    operations_per_batch = _positive_int(operations_per_batch, "operations per batch")
    resolved_map_directory = (
        Path(map_directory).expanduser().resolve()
        if map_directory is not None
        else (REPO_ROOT / "maps").resolve()
    )
    if not resolved_map_directory.is_dir():
        raise BenchmarkError(
            f"simulation benchmark map directory not found: {resolved_map_directory}"
        )
    build_dir, preset = benchmark_build(build_mode)
    executable_name = "lg_duel_sim_benchmark.exe" if os.name == "nt" else "lg_duel_sim_benchmark"
    executable = build_dir / executable_name
    if not executable.is_file():
        raise BenchmarkError(
            f"simulation benchmark executable not found: {executable}; "
            f"run cmake --preset {preset} and cmake --build --preset {preset}"
        )
    git = _git_metadata()
    commit_short = str(git["commit"])[:12]
    group = f"{_timestamp()}-{commit_short}"
    result_dir = RESULT_ROOT / f"sim-{workload}" / group
    suffix = 1
    while result_dir.exists():
        result_dir = RESULT_ROOT / f"sim-{workload}" / f"{group}-{suffix}"
        suffix += 1
    result_dir.mkdir(parents=True)
    command = [
        str(executable), "--workload", workload, "--map", map_name,
        "--map-directory", str(resolved_map_directory), "--output", str(result_dir),
        "--repetitions", str(repetitions), "--warmup-batches", str(warmup_batches),
        "--measured-batches", str(measured_batches), "--operations-per-batch", str(operations_per_batch),
    ]
    if force_linear:
        command.append("--force-linear")
    started_at = dt.datetime.now(dt.timezone.utc).isoformat()
    try:
        completed = subprocess.run(command, cwd=REPO_ROOT, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        raise BenchmarkError(f"simulation benchmark timed out; partial artifacts remain at {result_dir}") from error
    (result_dir / "stdout.log").write_text(completed.stdout, encoding="utf-8")
    (result_dir / "stderr.log").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise BenchmarkError(
            f"simulation benchmark exited {completed.returncode}: "
            f"{completed.stderr.strip() or 'inspect logs at ' + str(result_dir)}"
        )
    native_path = result_dir / "result.json"
    try:
        native = json.loads(native_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BenchmarkError(f"invalid simulation benchmark result: {error}") from error
    parameters = {
        "workload": workload, "map": map_name, "repetitions": repetitions,
        "warmup_batches": warmup_batches, "measured_batches": measured_batches,
        "operations_per_batch": operations_per_batch,
    }
    digest = hashlib.sha256(json.dumps(parameters, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    executable_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
    environment = environment_metadata({"renderer": "headless/shared-simulation"})
    environment.update(build_environment_metadata(build_dir))
    environment["benchmark_version"] = native.get("benchmark_version", 1)
    environment["protocol_version"] = source_protocol_version(REPO_ROOT)
    environment["server_tick_rate"] = source_fixed_tick_rate(REPO_ROOT)
    environment["build_mode"] = build_mode
    environment["build_directory"] = str(build_dir)
    environment["executable"] = str(executable)
    environment["executable_sha256"] = executable_hash
    sample_columns = {
        "movement_us_per_operation": "movement_collision_us_per_operation",
        "hitscan_us_per_trace": "hitscan_us_per_trace",
        "projectile_us_per_trace": "projectile_segment_us_per_trace",
    }
    grouped: dict[int, dict[str, list[float]]] = {}
    try:
        with (result_dir / "samples.csv").open("r", encoding="utf-8", newline="") as stream:
            for row in csv.DictReader(stream):
                repetition = int(row["repetition"])
                metrics_for_run = grouped.setdefault(repetition, {})
                for column, metric in sample_columns.items():
                    value = float(row[column])
                    if value > 0.0 and math.isfinite(value):
                        metrics_for_run.setdefault(metric, []).append(value)
    except (OSError, ValueError, KeyError) as error:
        raise BenchmarkError(f"invalid simulation samples CSV: {error}") from error
    runs = []
    for repetition in sorted(grouped):
        runs.append({
            "valid": native.get("valid") is True,
            "summary": {metric: summarize_values(values) for metric, values in grouped[repetition].items()},
        })
    aggregate = aggregate_runs(runs)
    result = {
        "schema_version": RESULT_SCHEMA_VERSION,
        "scenario": {
            "name": f"sim-{workload}", "schema_version": 1,
            "benchmark_version": native.get("benchmark_version", 1),
            "expected_benchmark_version": 1, "map": map_name,
        },
        "scenario_hash": digest, "scenario_path": str(executable), "result_directory": str(result_dir),
        "started_at": started_at, "completed_at": dt.datetime.now(dt.timezone.utc).isoformat(), "git": git,
        "environment": environment,
        "map_content_hash": native.get("map_content_hash"),
        "deterministic_checksum": native.get("checksum"),
        "settings": {"backend": "headless-shared-simulation",
                     "collision_query_mode": "forced-linear" if force_linear else "indexed-when-available",
                     **parameters},
        "aggregate": aggregate, "native_result": native,
    }
    _write_json(result_dir / "aggregate.json", result)
    (result_dir / "report.md").write_text(render_report(result), encoding="utf-8")
    return result


def _resolve_result(reference: str | Path, *, baseline: bool = False) -> Path:
    text = str(reference)
    candidate = Path(text)
    root = BASELINE_ROOT if baseline else RESULT_ROOT
    if baseline:
        validate_safe_name(text, "baseline")
        candidate = root / text / "aggregate.json"
    elif not candidate.is_absolute():
        if candidate.parent == Path(".") and SAFE_NAME.fullmatch(text):
            candidate = RESULT_ROOT / text
        else:
            candidate = REPO_ROOT / candidate
    if candidate.is_dir():
        candidate = candidate / "aggregate.json"
    resolved = candidate.resolve()
    allowed = root.resolve()
    if resolved != allowed and allowed not in resolved.parents:
        raise BenchmarkError(f"artifact path escapes {root}")
    if not resolved.is_file():
        raise BenchmarkError(f"benchmark artifact not found: {resolved}")
    return resolved


def load_result(reference: str | Path, *, baseline: bool = False, detailed: bool = False) -> dict[str, Any]:
    path = _resolve_result(reference, baseline=baseline)
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise BenchmarkError(f"invalid benchmark artifact {path}: {error}") from error
    if not isinstance(result, dict) or result.get("schema_version") != RESULT_SCHEMA_VERSION:
        raise BenchmarkError(f"unsupported or missing result schema in {path}")
    result["artifact_path"] = str(path)
    if detailed:
        return result
    return {key: value for key, value in result.items() if key not in {"runs"}}


def create_baseline(
    scenario_name: str,
    name: str,
    *,
    repetitions: int = 3,
    server_port: int = DEFAULT_SERVER_PORT,
    control_port: int | None = None,
    port: int | None = None,
    timeout: float = DEFAULT_TIMEOUT,
    result: dict[str, Any] | None = None,
    build_mode: str = "release",
) -> dict[str, Any]:
    validate_safe_name(name, "baseline name")
    target = BASELINE_ROOT / name
    if target.exists():
        raise BenchmarkError(f"baseline '{name}' already exists")
    result = result or run_benchmark(
        scenario_name,
        repetitions=repetitions,
        server_port=server_port,
        control_port=control_port,
        port=port,
        timeout=timeout,
        build_mode=build_mode,
    )
    target.mkdir(parents=True)
    baseline = dict(result)
    baseline["baseline_name"] = name
    baseline["source_result_directory"] = result.get("result_directory")
    baseline["result_directory"] = str(target)
    _write_json(target / "aggregate.json", baseline)
    (target / "report.md").write_text(render_report(baseline), encoding="utf-8")
    return baseline


MATERIAL_KEYS = ("backend", "resolution", "window_mode", "vsync", "frame_cap", "fov", "presentation_cvars")
WORLD_MODE_SCENARIOS = frozenset({
    "overkill-static-flythrough",
    "overkill-static-flythrough-bvh-off",
    "overkill-static-flythrough-gpu-indirect",
})
WORLD_MODE_BASELINE = "overkill-static-flythrough"
WORLD_MODE_SELECTOR_VALUES = {
    "overkill-static-flythrough": {
        "r_world_frustum_cull": "1",
        "r_world_gpu_indirect": "0",
    },
    "overkill-static-flythrough-bvh-off": {
        "r_world_frustum_cull": "0",
        "r_world_gpu_indirect": "0",
    },
    "overkill-static-flythrough-gpu-indirect": {
        "r_world_frustum_cull": "0",
        "r_world_gpu_indirect": "1",
    },
}
WORLD_MODE_SELECTOR_CVARS = frozenset({
    "r_world_frustum_cull",
    "r_world_gpu_indirect",
})
WORLD_MODE_CONTRACT_KEYS = frozenset({
    "world_frustum_cull",
    "world_gpu_indirect",
})
COMPARISON_ENVIRONMENT_KEYS = (
    "os", "cpu", "architecture", "logical_cores", "renderer",
    "protocol_version", "control_protocol_version", "gpu_name", "gpu_type",
    "graphics_driver_name", "graphics_driver_version", "vulkan_api_version",
    "vulkan_metadata_status", "software_renderer", "gpu_verification_state",
    "gpu_verified", "build_metadata_status", "build_type", "cmake_generator",
    "compiler", "compiler_path", "compiler_version", "compile_time_options",
    "sdl_configuration", "observed_resolution", "selected_present_mode",
    "benchmark_version", "gpu_timing_instrumentation_version", "build_mode",
    "executable_sha256",
)
WORLD_MODE_TIMING_METRIC_DIRECTIONS = {
    "frame_ms": "lower",
    "total_render_cpu_ms": "lower",
    "gpu_primary_command_buffer_ms": "lower",
    "main_scene_gpu_ms": "lower",
    "world_visibility_ms": "lower",
    "world_visibility_query_ms": "lower",
    "world_command_encoding_ms": "lower",
}
WORLD_MODE_GPU_ONLY_METRICS = (
    "world_gpu_indirect_cpu_ms",
    "world_indirect_cull_gpu_ms",
)


def _missing_world_mode_metadata(result: dict[str, Any]) -> list[str]:
    missing: list[str] = []
    if not isinstance(result.get("scenario_hash"), str) or not result["scenario_hash"]:
        missing.append("scenario hash")
    map_hash = result.get("map_content_hash")
    if isinstance(map_hash, bool) or not isinstance(map_hash, int) or map_hash <= 0:
        missing.append("map content hash")
    deterministic_checksum = result.get("deterministic_checksum")
    if deterministic_checksum is not None and not isinstance(
        deterministic_checksum, (int, str)
    ):
        missing.append("deterministic checksum")
    environment = result.get("environment")
    if not isinstance(environment, dict):
        missing.append("environment")
    else:
        for key in COMPARISON_ENVIRONMENT_KEYS:
            if key not in environment or environment[key] is None:
                missing.append(f"environment {key}")
    git = result.get("git")
    if not isinstance(git, dict):
        missing.append("git metadata")
    else:
        for key in ("commit", "dirty"):
            if key not in git or git[key] is None:
                missing.append(f"git {key}")
    return missing


def _world_mode_selector_values(result: dict[str, Any]) -> dict[str, str]:
    scenario = result.get("scenario", {})
    cvars = scenario.get("cvars", {}) if isinstance(scenario, dict) else {}
    if not isinstance(cvars, dict):
        return {}
    values: dict[str, str] = {}
    for key in WORLD_MODE_SELECTOR_CVARS:
        value = cvars.get(key)
        if isinstance(value, bool):
            values[key] = "1" if value else "0"
        elif value is not None:
            values[key] = str(value)
    return values


def _selector_values_from_mapping(
    value: Any, keys: Iterable[str]
) -> dict[str, str]:
    if not isinstance(value, dict):
        return {}
    result: dict[str, str] = {}
    for key in keys:
        item = value.get(key)
        if isinstance(item, bool):
            result[key] = "1" if item else "0"
        elif item is not None:
            result[key] = str(item)
    return result


def _world_mode_applied_selector_mismatches(
    result: dict[str, Any], expected: dict[str, str]
) -> list[str]:
    settings = result.get("settings", {})
    contract = settings.get("graphics_contract", {}) if isinstance(settings, dict) else {}
    checks = (
        (
            "scenario cvars",
            _world_mode_selector_values(result),
            expected,
        ),
        (
            "presentation cvars",
            _selector_values_from_mapping(
                settings.get("presentation_cvars") if isinstance(settings, dict) else None,
                WORLD_MODE_SELECTOR_CVARS,
            ),
            expected,
        ),
        (
            "graphics contract selectors",
            _selector_values_from_mapping(contract, WORLD_MODE_CONTRACT_KEYS),
            {
                "world_frustum_cull": expected["r_world_frustum_cull"],
                "world_gpu_indirect": expected["r_world_gpu_indirect"],
            },
        ),
        (
            "effective graphics cvars",
            _selector_values_from_mapping(
                contract.get("effective_cvars") if isinstance(contract, dict) else None,
                WORLD_MODE_SELECTOR_CVARS,
            ),
            expected,
        ),
    )
    return [label for label, actual, wanted in checks if actual != wanted]


def _world_mode_descriptor_mismatches(
    result: dict[str, Any], descriptor: dict[str, Any], descriptor_hash: str
) -> list[str]:
    mismatches: list[str] = []
    if result.get("scenario") != descriptor:
        mismatches.append("scenario descriptor")
    if result.get("scenario_hash") != descriptor_hash:
        mismatches.append("scenario hash")
    settings = result.get("settings")
    if not isinstance(settings, dict):
        return mismatches + ["settings"]
    expected_settings = {
        "backend": descriptor["backend_requirement"],
        "resolution": descriptor["resolution"],
        "window_mode": "fullscreen" if descriptor["fullscreen"] else "windowed",
        "vsync": descriptor["vsync"],
        "frame_cap": descriptor["frame_cap"],
        "fov": descriptor["fov"],
        "presentation_cvars": descriptor["cvars"],
        "graphics_profile": descriptor.get("graphics_profile", "Default"),
        "render_scale": descriptor.get("render_scale", 1.0),
    }
    for key, expected in expected_settings.items():
        if key not in settings or settings[key] != expected:
            mismatches.append(f"settings {key}")
    contract = settings.get("graphics_contract")
    if not isinstance(contract, dict):
        mismatches.append("graphics contract")
    else:
        required_contract_keys = {"profile", "effective_cvars"} | set(
            GRAPHICS_CONTRACT_CVARS.values()
        )
        missing = sorted(required_contract_keys - set(contract))
        if missing:
            mismatches.append(
                "graphics contract fields: " + ", ".join(missing)
            )
        effective = contract.get("effective_cvars")
        if not isinstance(effective, dict):
            mismatches.append("effective graphics cvars")
        else:
            missing_effective = sorted(
                set(GRAPHICS_CONTRACT_CVARS) - set(effective)
            )
            if missing_effective:
                mismatches.append(
                    "effective graphics cvars: " + ", ".join(missing_effective)
                )
    return mismatches


def _is_world_mode_result(result: dict[str, Any]) -> bool:
    scenario = result.get("scenario", {})
    return isinstance(scenario, dict) and scenario.get("name") in WORLD_MODE_SCENARIOS


def _without_world_mode_cvars(value: Any) -> Any:
    if not isinstance(value, dict):
        return value
    return {
        key: item for key, item in value.items()
        if key not in WORLD_MODE_SELECTOR_CVARS
    }


def _normalized_world_mode_scenario(value: Any) -> Any:
    if not isinstance(value, dict):
        return value
    normalized = dict(value)
    for key in ("name", "labels", "residual_nondeterminism"):
        normalized.pop(key, None)
    normalized["cvars"] = _without_world_mode_cvars(normalized.get("cvars", {}))
    return normalized


def _normalized_world_mode_settings(value: Any) -> Any:
    if not isinstance(value, dict):
        return value
    normalized = dict(value)
    normalized["presentation_cvars"] = _without_world_mode_cvars(
        normalized.get("presentation_cvars", {})
    )
    contract = normalized.get("graphics_contract")
    if isinstance(contract, dict):
        contract = dict(contract)
        effective = contract.get("effective_cvars")
        contract["effective_cvars"] = _without_world_mode_cvars(effective)
        for key in WORLD_MODE_CONTRACT_KEYS:
            contract.pop(key, None)
        normalized["graphics_contract"] = contract
    return normalized


def _world_mode_comparison_checksum(result: dict[str, Any]) -> str:
    payload = {
        "scenario": _normalized_world_mode_scenario(result.get("scenario", {})),
        "map_content_hash": result.get("map_content_hash"),
    }
    return hashlib.sha256(
        json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()


def _comparison_mismatch(
    baseline: dict[str, Any],
    result: dict[str, Any],
    *,
    allow_world_mode_selectors: bool = False,
) -> list[str]:
    reasons = []
    world_mode = (
        allow_world_mode_selectors and
        _is_world_mode_result(baseline) and
        _is_world_mode_result(result)
    )
    if baseline.get("schema_version") != result.get("schema_version"):
        reasons.append("result schema version")
    if not world_mode and baseline.get("scenario_hash") != result.get("scenario_hash"):
        reasons.append("scenario hash")
    if baseline.get("map_content_hash") != result.get("map_content_hash"):
        reasons.append("map content hash")
    if world_mode:
        if _world_mode_comparison_checksum(baseline) != _world_mode_comparison_checksum(result):
            reasons.append("deterministic checksum")
    elif baseline.get("deterministic_checksum") != result.get("deterministic_checksum"):
        reasons.append("deterministic checksum")
    base_scenario, new_scenario = baseline.get("scenario", {}), result.get("scenario", {})
    if world_mode:
        if _normalized_world_mode_scenario(base_scenario) != _normalized_world_mode_scenario(new_scenario):
            reasons.append("scenario settings")
        if _normalized_world_mode_settings(baseline.get("settings", {})) != _normalized_world_mode_settings(result.get("settings", {})):
            reasons.append("settings")
    else:
        for key in ("name", "schema_version", "expected_benchmark_version"):
            if base_scenario.get(key) != new_scenario.get(key):
                reasons.append(f"scenario {key}")
        for key in MATERIAL_KEYS:
            if baseline.get("settings", {}).get(key) != result.get("settings", {}).get(key):
                reasons.append(key)
    base_env, new_env = baseline.get("environment", {}), result.get("environment", {})
    for key in COMPARISON_ENVIRONMENT_KEYS:
        if base_env.get(key) != new_env.get(key):
            reasons.append(key)
    base_git, new_git = baseline.get("git", {}), result.get("git", {})
    for key in ("commit", "dirty"):
        if base_git.get(key) != new_git.get(key):
            reasons.append(f"git {key}")
    return sorted(set(reasons))


def classify_metric(base: dict[str, Any], current: dict[str, Any], *, threshold_percent: float = 3.0,
                    tail_threshold_percent: float = 3.0) -> tuple[str, dict[str, Any]]:
    diffs: dict[str, Any] = {}
    tail_regression = False
    meaningful: list[float] = []
    for statistic in ("median", "p95", "p99", "max"):
        old, new = base.get(statistic), current.get(statistic)
        if not isinstance(old, (int, float)) or not isinstance(new, (int, float)):
            continue
        absolute = float(new) - float(old)
        percent = absolute / float(old) * 100.0 if old else None
        diffs[statistic] = {"baseline": old, "result": new, "absolute": absolute, "percent": percent}
        if percent is not None:
            meaningful.append(percent)
            if statistic in {"p95", "p99", "max"} and percent > tail_threshold_percent:
                tail_regression = True
    noise = max(float(base.get("cv_percent", 0.0)), float(current.get("cv_percent", 0.0)), threshold_percent)
    median_delta = diffs.get("median", {}).get("percent")
    if tail_regression:
        classification = "regression"
    elif median_delta is not None and median_delta > noise:
        classification = "regression"
    elif median_delta is not None and median_delta < -noise and not any(delta > tail_threshold_percent for delta in meaningful):
        classification = "improvement"
    else:
        classification = "inconclusive"
    return classification, {"statistics": diffs, "noise_threshold_percent": noise, "classification": classification}


def compare_results(baseline_ref: str | Path, result_ref: str | Path, *, threshold_percent: float = 3.0,
                    tail_threshold_percent: float = 3.0) -> dict[str, Any]:
    baseline = load_result(baseline_ref, baseline=True, detailed=True)
    result = load_result(result_ref, detailed=True)
    mismatches = _comparison_mismatch(baseline, result)
    if mismatches:
        return {"classification": "invalid", "valid": False, "mismatches": mismatches,
                "baseline": str(baseline_ref), "result": str(result_ref)}
    if not baseline.get("aggregate", {}).get("valid") or not result.get("aggregate", {}).get("valid"):
        return {"classification": "invalid", "valid": False, "mismatches": ["invalid benchmark run"]}
    metric_results: dict[str, Any] = {}
    classifications = []
    base_metrics, new_metrics = baseline["aggregate"].get("metrics", {}), result["aggregate"].get("metrics", {})
    for metric in sorted(set(base_metrics) & set(new_metrics)):
        classification, details = classify_metric(base_metrics[metric], new_metrics[metric], threshold_percent=threshold_percent,
                                                  tail_threshold_percent=tail_threshold_percent)
        metric_results[metric] = details
        classifications.append(classification)
    if not classifications:
        overall = "invalid"
    elif "regression" in classifications:
        overall = "regression"
    elif classifications and all(value == "improvement" for value in classifications):
        overall = "improvement"
    else:
        overall = "inconclusive"
    comparison = {"classification": overall, "valid": overall != "invalid", "metrics": metric_results,
                  "baseline": baseline["artifact_path"], "result": result["artifact_path"],
                  "threshold_percent": threshold_percent, "tail_threshold_percent": tail_threshold_percent}
    comparison_path = Path(result["artifact_path"]).parent / "comparison.json"
    comparison["comparison_path"] = str(comparison_path)
    _write_json(comparison_path, comparison)
    return comparison


def compare_world_mode_results(
    result_refs: Iterable[str | Path],
    *,
    threshold_percent: float = 3.0,
    tail_threshold_percent: float = 3.0,
) -> dict[str, Any]:
    references = list(result_refs)
    if len(references) != len(WORLD_MODE_SCENARIOS):
        raise BenchmarkError(
            "compare-modes needs exactly three --result references"
        )
    results = [load_result(reference, detailed=True) for reference in references]
    names = [result.get("scenario", {}).get("name") for result in results]
    if set(names) != WORLD_MODE_SCENARIOS or len(set(names)) != len(names):
        return {
            "classification": "invalid",
            "valid": False,
            "mismatches": [
                "results must contain the three overkill static-world modes"
            ],
            "results": [str(reference) for reference in references],
        }

    by_name = dict(zip(names, results, strict=True))
    baseline = by_name[WORLD_MODE_BASELINE]
    mode_comparisons: dict[str, Any] = {}
    classifications: list[str] = []
    artifact_mismatches: dict[str, list[str]] = {}
    for name, result in by_name.items():
        descriptor, _, descriptor_hash = load_scenario(name)
        mismatches = _missing_world_mode_metadata(result)
        mismatches.extend(
            _world_mode_descriptor_mismatches(result, descriptor, descriptor_hash)
        )
        mismatches.extend(
            _world_mode_applied_selector_mismatches(
                result, WORLD_MODE_SELECTOR_VALUES[name]
            )
        )
        artifact_mismatches[name] = sorted(set(mismatches))
    if any(artifact_mismatches.values()):
        return {
            "classification": "invalid",
            "valid": False,
            "mismatches": [
                f"{name}: missing {', '.join(mismatches)}"
                for name, mismatches in artifact_mismatches.items()
                if mismatches
            ],
            "results": [result["artifact_path"] for result in results],
        }
    for name in sorted(WORLD_MODE_SCENARIOS - {WORLD_MODE_BASELINE}):
        result = by_name[name]
        mismatches = _comparison_mismatch(
            baseline,
            result,
            allow_world_mode_selectors=True,
        )
        if mismatches:
            mode_comparisons[name] = {
                "classification": "invalid",
                "valid": False,
                "mismatches": mismatches,
                "baseline": baseline["artifact_path"],
                "result": result["artifact_path"],
            }
            classifications.append("invalid")
            continue
        if not baseline.get("aggregate", {}).get("valid") or not result.get("aggregate", {}).get("valid"):
            mode_comparisons[name] = {
                "classification": "invalid",
                "valid": False,
                "mismatches": ["invalid benchmark run"],
                "baseline": baseline["artifact_path"],
                "result": result["artifact_path"],
            }
            classifications.append("invalid")
            continue

        metric_results: dict[str, Any] = {}
        metric_classifications: list[str] = []
        base_metrics = baseline["aggregate"].get("metrics", {})
        new_metrics = result["aggregate"].get("metrics", {})
        for metric, direction in WORLD_MODE_TIMING_METRIC_DIRECTIONS.items():
            if direction != "lower":
                raise BenchmarkError(
                    f"unsupported world mode metric direction: {direction}"
                )
            if metric not in base_metrics or metric not in new_metrics:
                continue
            classification, details = classify_metric(
                base_metrics[metric],
                new_metrics[metric],
                threshold_percent=threshold_percent,
                tail_threshold_percent=tail_threshold_percent,
            )
            metric_results[metric] = details
            metric_classifications.append(classification)
        gpu_only_metrics = {
            metric: result["aggregate"]["metrics"][metric]
            for metric in WORLD_MODE_GPU_ONLY_METRICS
            if metric in result["aggregate"].get("metrics", {})
        }
        if not metric_classifications:
            overall = "invalid"
        elif "regression" in metric_classifications:
            overall = "regression"
        elif all(value == "improvement" for value in metric_classifications):
            overall = "improvement"
        else:
            overall = "inconclusive"
        mode_comparisons[name] = {
            "classification": overall,
            "valid": overall != "invalid",
            "metrics": metric_results,
            "baseline": baseline["artifact_path"],
            "result": result["artifact_path"],
            "threshold_percent": threshold_percent,
            "tail_threshold_percent": tail_threshold_percent,
            "gpu_only_metrics": gpu_only_metrics,
        }
        classifications.append(overall)

    if "invalid" in classifications:
        overall = "invalid"
    elif "regression" in classifications:
        overall = "regression"
    elif classifications and all(value == "improvement" for value in classifications):
        overall = "improvement"
    else:
        overall = "inconclusive"
    comparison = {
        "classification": overall,
        "valid": overall != "invalid",
        "baseline": baseline["artifact_path"],
        "comparison_checksum": _world_mode_comparison_checksum(baseline),
        "mode_comparisons": mode_comparisons,
        "threshold_percent": threshold_percent,
        "tail_threshold_percent": tail_threshold_percent,
    }
    comparison_path = Path(by_name["overkill-static-flythrough-gpu-indirect"]["artifact_path"]).parent / "mode-comparison.json"
    comparison["comparison_path"] = str(comparison_path)
    _write_json(comparison_path, comparison)
    return comparison


EXIT_CODES = {"pass": 0, "improvement": 0, "regression": 2, "invalid": 3, "error": 3, "inconclusive": 4}


def human_output(value: dict[str, Any]) -> str:
    if "classification" in value:
        lines = [f"Benchmark comparison: {value['classification']}"]
        if value.get("mismatches"):
            lines.append("Not comparable: " + ", ".join(value["mismatches"]))
        for name, details in value.get("mode_comparisons", {}).items():
            line = f"  {name}: {details['classification']}"
            median = details.get("metrics", {}).get("frame_ms", {}).get("statistics", {}).get("median", {})
            delta = median.get("percent")
            if delta is not None:
                line += f" ({delta:+.2f}% median frame_ms)"
            if details.get("mismatches"):
                line += " [" + ", ".join(details["mismatches"]) + "]"
            lines.append(line)
            for metric, stats in details.get("gpu_only_metrics", {}).items():
                lines.append(
                    f"    {metric}: median {stats.get('median', 0):.4g} ms"
                )
        for metric, details in value.get("metrics", {}).items():
            median = details.get("statistics", {}).get("median", {})
            delta = median.get("percent")
            lines.append(f"  {metric}: {details['classification']}" + (f" ({delta:+.2f}% median)" if delta is not None else ""))
        return "\n".join(lines)
    if "scenarios" in value:
        return "\n".join(f"{entry['name']}: {'valid' if entry['valid'] else entry.get('error', 'invalid')}" for entry in value["scenarios"]) or "No benchmark scenarios found."
    if "aggregate" in value:
        return render_report(value).strip()
    return json.dumps(value, indent=2, ensure_ascii=False)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--server-port", type=int, default=DEFAULT_SERVER_PORT,
        help=f"dedicated benchmark game-server UDP port (default: {DEFAULT_SERVER_PORT})",
    )
    parser.add_argument(
        "--control-port", type=int,
        help=f"dedicated benchmark control TCP port (default: {DEFAULT_CONTROL_PORT})",
    )
    parser.add_argument(
        "--port", type=int,
        help="legacy alias for --control-port; values must match if both are set",
    )
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--json", action="store_true")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list")
    run = commands.add_parser("run")
    run.add_argument("--scenario", required=True)
    run.add_argument("--repetitions", type=int, default=3)
    run.add_argument(
        "--graphics-profile",
        choices=("Low", "Default", "Competitive", "High"),
    )
    run.add_argument("--build-mode", choices=tuple(BUILD_MODES), default="release")
    sim_run = commands.add_parser("sim-run")
    sim_run.add_argument("--workload", required=True, choices=("movement-collision", "trace-projectile"))
    sim_run.add_argument("--map", default="overkill_import")
    sim_run.add_argument("--repetitions", type=int, default=5)
    sim_run.add_argument("--warmup-batches", type=int, default=5)
    sim_run.add_argument("--measured-batches", type=int, default=40)
    sim_run.add_argument("--operations-per-batch", type=int, default=256)
    sim_run.add_argument("--force-linear", action="store_true")
    sim_run.add_argument("--build-mode", choices=tuple(BUILD_MODES), default="release")
    create = commands.add_parser("baseline-create")
    create.add_argument("--scenario", required=True)
    create.add_argument("--name", required=True)
    create.add_argument("--repetitions", type=int, default=3)
    create.add_argument("--build-mode", choices=tuple(BUILD_MODES), default="release")
    compare = commands.add_parser("compare")
    compare.add_argument("--baseline", required=True)
    compare.add_argument("--result", required=True)
    compare.add_argument("--threshold-percent", type=float, default=3.0)
    compare.add_argument("--tail-threshold-percent", type=float, default=3.0)
    compare_modes = commands.add_parser("compare-modes")
    compare_modes.add_argument(
        "--result", action="append", required=True,
        help="repeat three times with the CPU-BVH, CPU-no-cull, and GPU-indirect results",
    )
    compare_modes.add_argument("--threshold-percent", type=float, default=3.0)
    compare_modes.add_argument("--tail-threshold-percent", type=float, default=3.0)
    report = commands.add_parser("report")
    report.add_argument("--result", required=True)
    report.add_argument("--detailed", action="store_true")
    return parser


def execute(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    if args.command == "list":
        return list_scenarios(), 0
    if args.command == "run":
        result = run_benchmark(
            args.scenario,
            repetitions=args.repetitions,
            server_port=args.server_port,
            control_port=args.control_port,
            port=args.port,
            timeout=args.timeout,
            build_mode=args.build_mode,
            graphics_profile=args.graphics_profile,
        )
        return result, 0 if result["aggregate"]["valid"] else 3
    if args.command == "sim-run":
        result = run_simulation_benchmark(
            args.workload, repetitions=args.repetitions, map_name=args.map,
            warmup_batches=args.warmup_batches, measured_batches=args.measured_batches,
            operations_per_batch=args.operations_per_batch, timeout=args.timeout,
            force_linear=args.force_linear, build_mode=args.build_mode,
        )
        return result, 0 if result["aggregate"]["valid"] else 3
    if args.command == "baseline-create":
        return create_baseline(
            args.scenario,
            args.name,
            repetitions=args.repetitions,
            server_port=args.server_port,
            control_port=args.control_port,
            port=args.port,
            timeout=args.timeout,
            build_mode=args.build_mode,
        ), 0
    if args.command == "compare":
        result = compare_results(args.baseline, args.result, threshold_percent=args.threshold_percent,
                                 tail_threshold_percent=args.tail_threshold_percent)
        return result, EXIT_CODES[result["classification"]]
    if args.command == "compare-modes":
        result = compare_world_mode_results(
            args.result,
            threshold_percent=args.threshold_percent,
            tail_threshold_percent=args.tail_threshold_percent,
        )
        return result, EXIT_CODES[result["classification"]]
    if args.command == "report":
        return load_result(args.result, detailed=args.detailed), 0
    raise BenchmarkError(f"unsupported command: {args.command}")


def main(argv: list[str] | None = None) -> int:
    raw = list(sys.argv[1:] if argv is None else argv)
    json_requested = "--json" in raw
    raw = [item for item in raw if item != "--json"]
    if json_requested:
        raw.insert(0, "--json")
    parser = build_parser()
    args = parser.parse_args(raw)
    try:
        result, code = execute(args)
    except (BenchmarkError, ControlError) as error:
        if args.json:
            print(json.dumps({"ok": False, "error": str(error)}, separators=(",", ":")))
        else:
            print(f"LG benchmark error: {error}", file=sys.stderr)
        return 3
    print(json.dumps(result, separators=(",", ":"), ensure_ascii=False) if args.json else human_output(result))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
