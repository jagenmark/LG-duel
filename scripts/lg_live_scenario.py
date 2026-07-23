#!/usr/bin/env python3
"""Run one C++-validated LG Duel scenario through a real client and server."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import time
import uuid
from pathlib import Path
from typing import Any
from xml.sax.saxutils import escape

import lg_control
import lg_launch


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "build" / "scenario-results"
DEFAULT_BUILD_DIR = REPO_ROOT / "build" / "default"
MAX_SCHEDULE_LATE_TICKS = 32


class LiveScenarioError(RuntimeError):
    """A live scenario did not reach a valid completed result."""


class LiveScenarioStageError(LiveScenarioError):
    def __init__(self, stage: str, detail: str) -> None:
        self.stage = stage
        super().__init__(f"{stage}: {detail}")


def _json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _read_json(path: Path, *, stage: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LiveScenarioStageError(stage, f"could not read {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise LiveScenarioStageError(stage, f"{path.name} must contain a JSON object")
    return value

def _wait_json(path: Path, *, stage: str, deadline: float) -> dict[str, Any]:
    last_error: LiveScenarioStageError | None = None
    while time.monotonic() < deadline:
        if path.is_file():
            try:
                return _read_json(path, stage=stage)
            except LiveScenarioStageError as error:
                last_error = error
        time.sleep(0.02)
    if last_error is not None:
        raise last_error
    raise LiveScenarioStageError(stage, f"timed out waiting for {path.name}")


def _copy(source: Path | str | None, target: Path) -> None:
    if not source:
        return
    try:
        path = Path(source)
        if path.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    except OSError:
        pass


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def _safe_live_fields(scenario: Any) -> dict[str, Any]:
    """Only guard runner fields. The C++ parser owns schema validation."""
    if not isinstance(scenario, dict):
        raise LiveScenarioStageError("runtime_safety", "canonical scenario must be an object")
    execution, world = scenario.get("execution"), scenario.get("world")
    if not isinstance(execution, dict) or execution.get("mode") != "client_server":
        raise LiveScenarioStageError("runtime_safety", "execution.mode must be client_server")
    if not isinstance(world, dict) or not isinstance(world.get("map"), str) or not world["map"]:
        raise LiveScenarioStageError("runtime_safety", "world.map must be a non-empty string")
    if not isinstance(execution.get("max_ticks"), int) or execution["max_ticks"] < 1:
        raise LiveScenarioStageError("runtime_safety", "execution.max_ticks must be a positive integer")
    if not isinstance(scenario.get("timeline", []), list):
        raise LiveScenarioStageError("runtime_safety", "timeline must be an array")
    return scenario


def validate_live_scenario(path: Path, build_dir: Path, timeout: float) -> dict[str, Any]:
    executable = build_dir / "lg_duel_scenarios.exe"
    if not executable.is_file():
        raise LiveScenarioStageError("validate", f"C++ scenario runner is missing: {executable}")
    try:
        completed = subprocess.run(
            [str(executable), "--scenario", str(path), "--validate-only"],
            cwd=build_dir, capture_output=True, text=True, timeout=timeout, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise LiveScenarioStageError("validate", f"C++ validation could not run: {error}") from error
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or f"exit {completed.returncode}").strip()
        raise LiveScenarioStageError("validate", f"C++ validation rejected scenario: {detail}")
    try:
        return _safe_live_fields(json.loads(completed.stdout))
    except json.JSONDecodeError as error:
        raise LiveScenarioStageError("validate", f"C++ validation did not return canonical JSON: {error}") from error


def _request(operation: str, session: dict[str, Any], timeout: float, **parameters: Any) -> dict[str, Any]:
    return lg_control.send_request(
        operation, host="127.0.0.1", port=int(session["control_port"]), timeout=timeout, **parameters,
    )


def _checkpoint(run_dir: Path, tick: int, deadline: float) -> dict[str, Any]:
    """Read only completed, immutable server checkpoint files."""
    while time.monotonic() < deadline:
        choices: list[tuple[int, Path]] = []
        for path in run_dir.glob("checkpoint-*.json"):
            try:
                value = int(path.stem.removeprefix("checkpoint-"))
            except ValueError:
                continue
            if value >= tick:
                choices.append((value, path))
        if choices:
            _, path = min(choices)
            try:
                return _read_json(path, stage="server_checkpoint")
            except LiveScenarioStageError:
                pass
        time.sleep(0.02)
    raise LiveScenarioStageError("server_checkpoint", f"timed out waiting for server tick {tick}")

def _latest_checkpoint(run_dir: Path) -> dict[str, Any]:
    deadline = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        choices: list[tuple[int, Path]] = []
        for path in run_dir.glob("checkpoint-*.json"):
            try:
                choices.append(
                    (int(path.stem.removeprefix("checkpoint-")), path)
                )
            except ValueError:
                continue
        if choices:
            try:
                return _read_json(
                    max(choices)[1],
                    stage="server_checkpoint",
                )
            except LiveScenarioStageError:
                pass
        time.sleep(0.02)
    raise LiveScenarioStageError(
        "server_checkpoint",
        "server has no readable checkpoint",
    )

def _checkpoint_for_event(
    run_dir: Path,
    expected: dict[str, Any],
    deadline: float,
) -> dict[str, Any]:
    inspected: set[Path] = set()
    while time.monotonic() < deadline:
        choices: list[tuple[int, Path]] = []
        for path in run_dir.glob("checkpoint-*.json"):
            try:
                choices.append(
                    (int(path.stem.removeprefix("checkpoint-")), path)
                )
            except ValueError:
                continue
        for _, path in sorted(choices):
            if path in inspected:
                continue
            try:
                checkpoint = _read_json(path, stage="capture_checkpoint")
            except LiveScenarioStageError:
                continue
            inspected.add(path)
            if any(
                _event_matches(event, expected)
                for event in _events(checkpoint)
            ):
                return checkpoint
        time.sleep(0.02)
    raise LiveScenarioStageError(
        "capture_checkpoint",
        f"timed out waiting for event {expected}",
    )


def _event_matches(event: dict[str, Any], expected: dict[str, Any]) -> bool:
    return all(event.get(key) == value for key, value in expected.items())

def _client_authority_distance(state: dict[str, Any]) -> float | None:
    predicted = state.get("predicted_local_player")
    authoritative = state.get("authoritative_local_player")
    left = predicted.get("position") if isinstance(predicted, dict) else None
    right = authoritative.get("position") if isinstance(authoritative, dict) else None
    if not (
        isinstance(left, list)
        and isinstance(right, list)
        and len(left) == len(right) == 3
    ):
        return None
    return sum(
        (float(a) - float(b)) ** 2
        for a, b in zip(left, right)
    ) ** 0.5


def _events(checkpoint: dict[str, Any]) -> list[dict[str, Any]]:
    value = checkpoint.get("events_since_setup", checkpoint.get("events", checkpoint.get("authoritative_events", [])))
    return [event for event in value if isinstance(event, dict)] if isinstance(value, list) else []


def _sequence(response: dict[str, Any]) -> int | None:
    for key in ("release_sequence", "command_sequence", "sequence", "first_command_sequence"):
        value = response.get(key)
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return None


def _input_parts(entry: dict[str, Any]) -> list[dict[str, Any]]:
    source = dict(entry.get("input", {}))
    duration = int(entry["duration_ticks"])
    edges = [str(value) for value in entry.get("one_tick_edges", [])]
    for edge in edges:
        source[edge] = True
    return [{"ticks": duration, "one_tick_edges": edges, **source}]


def _capture(capture: dict[str, Any], session: dict[str, Any], timeout: float, shots: list[dict[str, Any]]) -> None:
    frames = int(capture.get("wait_rendered_frames", 0))
    if frames:
        _request("wait_frames", session, timeout, frames=frames)
    client_state = _request("get_client_state", session, timeout)
    result = _request("capture_screenshot", session, timeout, name=str(capture["name"]), hide_hud=True, hide_overlays=True)
    source = Path(str(result.get("path", "")))
    if not source.is_file():
        raise LiveScenarioStageError(
            "capture",
            f"renderer did not write screenshot {capture['name']!r}",
        )
    width, height = _png_dimensions(source)
    if result.get("width") != width or result.get("height") != height:
        raise LiveScenarioStageError(
            "capture",
            "renderer capture dimensions do not match the PNG header",
        )
    result["verified_width"] = width
    result["verified_height"] = height
    shots.append(
        {
            "name": capture["name"],
            "result": result,
            "render_state": {
                "client_tick": client_state.get("client_tick"),
                "latest_server_tick": client_state.get("latest_server_tick"),
                "latest_snapshot_tick": client_state.get(
                    "latest_snapshot_tick"
                ),
                "presentation_tick": client_state.get("presentation_tick"),
            },
        }
    )


def _png_dimensions(path: Path) -> tuple[int, int]:
    try:
        header = path.read_bytes()[:24]
    except OSError as error:
        raise LiveScenarioStageError(
            "capture",
            f"could not read screenshot {path}: {error}",
        ) from error
    if (
        len(header) != 24
        or header[:8] != b"\x89PNG\r\n\x1a\n"
        or header[12:16] != b"IHDR"
    ):
        raise LiveScenarioStageError(
            "capture",
            f"screenshot is not a PNG with an IHDR header: {path}",
        )
    return (
        int.from_bytes(header[16:20], "big"),
        int.from_bytes(header[20:24], "big"),
    )


def _live_assertions(scenario: dict[str, Any], client: dict[str, Any], session: dict[str, Any],
                     acknowledgements: list[dict[str, Any]], screenshots: list[dict[str, Any]],
                     server_result: dict[str, Any], client_samples: list[dict[str, Any]]) -> list[dict[str, Any]]:
    output: list[dict[str, Any]] = []
    authoritative_types = {
        "player_position", "player_velocity", "player_health", "player_alive",
        "player_weapon", "projectile_exists", "projectile_removed", "event",
        "state_hash",
    }
    for index, assertion in enumerate(scenario.get("assertions", [])):
        if not isinstance(assertion, dict):
            continue
        kind = assertion.get("type")
        if kind in authoritative_types:
            continue
        classification = assertion.get("classification")
        record: dict[str, Any] = {"index": index, "type": kind, "classification": classification}
        if kind == "command_acknowledged":
            timeline = assertion.get("timeline_index")
            found = [row for row in acknowledgements if row.get("timeline_index") == timeline]
            maximum = assertion.get("max_ticks")
            passed = bool(found) and all(
                isinstance(row.get("ack_delay_ticks"), int) and
                row["ack_delay_ticks"] <= maximum
                for row in found
            )
            record.update({"status": "passed" if passed else "failed", "actual": found})
        elif kind == "input_edge_count":
            edge = assertion.get("edge")
            command = server_result.get("latest_consumed_command", {})
            consumed = command.get("consumed_action_edge_counts", {}) if isinstance(command, dict) else {}
            count = consumed.get(edge) if isinstance(consumed, dict) else None
            record.update({"status": "passed" if count == assertion.get("count") else "failed", "actual": count})
        elif kind == "client_pending_commands_max":
            actual = client.get("maximum_pending_command_count")
            record.update({"status": "passed" if isinstance(actual, (int, float)) and actual <= assertion.get("max") else "failed", "actual": actual})
        elif kind == "client_correction_magnitude_max":
            actual = client.get("maximum_correction_distance")
            record.update({"status": "passed" if isinstance(actual, (int, float)) and actual <= assertion.get("max") else "failed", "actual": actual})
        elif kind == "client_correction_count":
            actual, minimum, maximum = client.get("correction_count"), assertion.get("min"), assertion.get("max")
            passed = isinstance(actual, (int, float)) and actual >= minimum and (maximum is None or actual <= maximum)
            record.update({"status": "passed" if passed else "failed", "actual": actual})
        elif kind == "client_converged":
            start_tick = max(
                (
                    int(entry["at_tick"]) + int(entry["duration_ticks"])
                    for entry in scenario.get("timeline", [])
                ),
                default=0,
            )
            deadline = start_tick + int(assertion.get("within_ticks", 0))
            distances = []
            for sample in client_samples:
                tick = sample.get("server_relative_tick")
                sample_state = sample.get("state", {})
                if not isinstance(tick, int) or tick < start_tick or tick > deadline:
                    continue
                distance = _client_authority_distance(sample_state)
                if distance is not None:
                    distances.append(
                        {
                            "tick": tick,
                            "distance": distance,
                        }
                    )
            passed = any(
                row["distance"] <= assertion.get("tolerance")
                for row in distances
            )
            record.update({"status": "passed" if passed else "failed", "actual": distances})
        elif kind == "client_connected":
            actual, expected = bool(client.get("connected")), assertion.get("expected")
            record.update({"status": "passed" if actual == expected else "failed", "actual": actual})
        elif kind == "renderer_backend":
            actual, expected = session.get("status", {}).get("renderer"), assertion.get("backend")
            record.update({"status": "passed" if actual == expected else "failed", "actual": actual})
        elif kind == "screenshot_checkpoint":
            found = next((row for row in screenshots if row.get("name") == assertion.get("capture")), None)
            image = dict(found.get("result", {})) if isinstance(found, dict) else {}
            if isinstance(found, dict) and isinstance(found.get("trigger"), dict):
                image["trigger"] = found["trigger"]
            if isinstance(found, dict) and isinstance(found.get("render_state"), dict):
                image["render_state"] = found["render_state"]
            passed = bool(found) and image.get("verified_width") == assertion.get("width") and image.get("verified_height") == assertion.get("height")
            record.update({"status": "passed" if passed else "failed", "actual": image})
        else:
            record.update({"status": "skipped", "reason": "not a Phase-2 live assertion"})
        output.append(record)
    return output


def _junit(name: str, assertions: list[dict[str, Any]], error: str | None) -> str:
    failed = [row for row in assertions if row.get("status") == "failed"]
    skipped = [row for row in assertions if row.get("status") in {"skipped", "copied_from_server"}]
    cases = []
    for row in assertions or [{"type": "run", "status": "failed" if error else "passed"}]:
        body = ""
        if row.get("status") == "failed": body = f'<failure message="{escape(str(row), {"\"": "&quot;"})}"/>'
        elif row.get("status") in {"skipped", "copied_from_server"}: body = "<skipped/>"
        cases.append(f'<testcase name="{escape(str(row.get("type", "run")))}">{body}</testcase>')
    return f'<testsuite name="{escape(name, {"\"": "&quot;"})}" tests="{len(cases)}" failures="{len(failed)}" skipped="{len(skipped)}">' + "".join(cases) + "</testsuite>\n"


def run_live_scenario(path: str | Path, output_root: str | Path = DEFAULT_OUTPUT_ROOT, *, renderer: str = "gpu",
                      allow_fallback: bool = False, timeout: float = 60.0,
                      build_dir: str | Path = DEFAULT_BUILD_DIR) -> dict[str, Any]:
    """Validate, launch, drive, and collect one real client/server scenario."""
    started = time.monotonic()
    scenario_path, build = Path(path).resolve(), Path(build_dir).resolve()
    run_token = uuid.uuid4().hex
    run_dir = Path(output_root).resolve() / f"live-{int(time.time() * 1000)}-{run_token[:8]}"
    scenario_dir = run_dir / "scenarios" / scenario_path.stem
    scenario_dir.mkdir(parents=True, exist_ok=True)
    session: dict[str, Any] | None = None
    scenario: dict[str, Any] = {}
    stage, error = "validate", None
    screenshots: list[dict[str, Any]] = []
    acknowledgements: list[dict[str, Any]] = []
    client_samples: list[dict[str, Any]] = []
    client: dict[str, Any] = {}
    checkpoint: dict[str, Any] = {}
    cleanup: dict[str, Any] = {"stopped": [], "failures": []}
    try:
        # Keep the supplied source even when C++ validation rejects it.
        _copy(scenario_path, scenario_dir / "scenario.json")
        scenario = validate_live_scenario(scenario_path, build, timeout)
        _json(scenario_dir / "scenario.json", scenario)
        canonical_scenario_path = scenario_dir / "scenario.json"
        server_port, control_port = _free_port(), _free_port()
        while control_port == server_port:
            control_port = _free_port()
        stage = "launch"
        session = lg_launch.launch_scenario_session(
            canonical_scenario_path, run_dir, run_token, server_port, control_port, renderer,
            allow_fallback, timeout, build,
        )
        ready_path = run_dir / "ready.json"
        deadline = time.monotonic() + timeout
        ready = _wait_json(ready_path, stage="ready", deadline=deadline)
        if ready.get("token") != run_token or ready.get("scenario") != scenario.get("name"):
            raise LiveScenarioStageError("ready", "server ready record does not attest this scenario session")
        expected_map = scenario["world"]["map"].removesuffix(".map")
        if str(ready.get("map", "")).removesuffix(".map") != expected_map:
            raise LiveScenarioStageError("ready", f"server map is {ready.get('map')!r}, expected {expected_map!r}")
        if not isinstance(ready.get("map_revision"), int):
            raise LiveScenarioStageError("ready", "server ready record has no map revision")
        initial_client = _request("get_client_state", session, timeout)
        client_tick_base = int(initial_client.get("client_tick", 0))
        stage = "network"
        network = scenario.get("network")
        if isinstance(network, dict):
            _request("set_network_simulation", session, timeout, **network)
        start_path = run_dir / "start.request.json"
        temporary_start = run_dir / f".{start_path.name}.{run_token}.tmp"
        _json(
            temporary_start,
            {"schema_version": 1, "token": run_token},
        )
        os.replace(temporary_start, start_path)
        initial_checkpoint = _checkpoint(
            run_dir,
            0,
            time.monotonic() + timeout,
        )
        state = initial_checkpoint.get("state")
        if not isinstance(state, dict) or not isinstance(state.get("map_revision"), int):
            raise LiveScenarioStageError("ready", "initial server checkpoint has no map revision")
        client_samples.append(
            {"server_relative_tick": 0, "state": initial_client}
        )
        captured: set[str] = set()
        tick_captures = sorted(
            (
                capture
                for capture in scenario.get("captures", [])
                if isinstance(capture, dict) and "at_server_tick" in capture
            ),
            key=lambda capture: int(capture["at_server_tick"]),
        )

        def capture_through(relative_tick: int) -> None:
            nonlocal stage
            for capture in tick_captures:
                if (
                    capture["name"] in captured
                    or int(capture["at_server_tick"]) > relative_tick
                ):
                    continue
                requested_tick = int(capture["at_server_tick"])
                capture_checkpoint = _checkpoint(
                    run_dir,
                    requested_tick,
                    time.monotonic() + timeout,
                )
                absolute_tick = capture_checkpoint.get("absolute_server_tick")
                if not isinstance(absolute_tick, int):
                    raise LiveScenarioStageError(
                        "capture_checkpoint",
                        "capture checkpoint has no absolute_server_tick",
                    )
                # A tick capture must reach the client before a rendered frame
                # can attest that state.
                _request(
                    "wait_snapshot_tick",
                    session,
                    timeout,
                    min_tick=absolute_tick,
                )
                stage = "capture"
                _capture(capture, session, timeout, screenshots)
                screenshots[-1]["trigger"] = {
                    "requested_server_relative_tick": requested_tick,
                    "trigger_checkpoint_relative_tick": capture_checkpoint.get(
                        "relative_tick"
                    ),
                    "trigger_checkpoint_absolute_tick": absolute_tick,
                }
                captured.add(capture["name"])

        scheduled_entries = sorted(
            enumerate(scenario["timeline"]),
            key=lambda item: int(item[1]["at_tick"]),
        )
        for index, entry in scheduled_entries:
            target = int(entry["at_tick"])
            end_tick = target + int(entry["duration_ticks"])
            capture_through(target)
            for part in _input_parts(entry):
                stage = "schedule"
                checkpoint = _checkpoint(
                    run_dir,
                    target,
                    time.monotonic() + timeout,
                )
                absolute_server_tick = checkpoint.get("absolute_server_tick")
                if not isinstance(absolute_server_tick, int):
                    raise LiveScenarioStageError(
                        "server_checkpoint",
                        "checkpoint has no absolute_server_tick",
                    )
                _request(
                    "wait_client_tick",
                    session,
                    timeout,
                    min_tick=client_tick_base + target,
                )
                _request(
                    "wait_snapshot_tick",
                    session,
                    timeout,
                    min_tick=absolute_server_tick,
                )
                before_input = _request("get_client_state", session, timeout)
                # Read the server clock after all other control calls. This is
                # the last step before send_input, so the bound measures the
                # real dispatch point rather than an earlier wait.
                dispatch_checkpoint = _latest_checkpoint(run_dir)
                dispatch_tick = dispatch_checkpoint.get("relative_tick")
                if not isinstance(dispatch_tick, int):
                    raise LiveScenarioStageError(
                        "server_checkpoint",
                        "dispatch checkpoint has no relative_tick",
                    )
                schedule_late_ticks = max(0, dispatch_tick - target)
                if schedule_late_ticks > MAX_SCHEDULE_LATE_TICKS:
                    raise LiveScenarioStageError(
                        "schedule",
                        f"timeline[{index}] started {schedule_late_ticks} ticks late; "
                        f"limit is {MAX_SCHEDULE_LATE_TICKS}",
                    )
                response = _request("send_input", session, timeout, **part)
                sequence = _sequence(response)
                record = {"timeline_index": index, "input": part, "sequence": sequence,
                          "scheduled_server_tick": target,
                          "dispatch_server_tick": dispatch_tick,
                          "schedule_late_ticks": schedule_late_ticks,
                          "response": response}
                capture_through(end_tick)
                if sequence is not None:
                    record["ack"] = _request("wait_command_ack", session, timeout, sequence=sequence)
                after_input = _request("get_client_state", session, timeout)
                before_tick = before_input.get("client_tick")
                after_tick = after_input.get("client_tick")
                if isinstance(before_tick, int) and isinstance(after_tick, int):
                    record["ack_delay_ticks"] = max(
                        0,
                        after_tick - before_tick - int(part["ticks"]),
                    )
                latest = _latest_checkpoint(run_dir)
                record["ack_server_relative_tick"] = latest.get(
                    "relative_tick"
                )
                client_samples.append(
                    {
                        "server_relative_tick": latest.get("relative_tick"),
                        "state": after_input,
                    }
                )
                acknowledgements.append(record)
            checkpoint = _checkpoint(run_dir, end_tick, time.monotonic() + timeout)
            relative_tick = checkpoint.get("relative_tick")
            for record in acknowledgements:
                if record["timeline_index"] == index:
                    record["server_relative_tick"] = relative_tick
            for capture in scenario.get("captures", []):
                wanted = capture.get("after_event") if isinstance(capture, dict) else None
                matching_events = [
                    event
                    for event in _events(checkpoint)
                    if wanted and _event_matches(event, wanted)
                ]
                if wanted and capture["name"] not in captured and matching_events:
                    stage = "capture"
                    _capture(capture, session, timeout, screenshots)
                    screenshots[-1]["trigger"] = {
                        "after_event": wanted,
                        "trigger_checkpoint_relative_tick": checkpoint.get(
                            "relative_tick"
                        ),
                        "trigger_checkpoint_absolute_tick": checkpoint.get(
                            "absolute_server_tick"
                        ),
                        "events": matching_events[:16],
                    }
                    captured.add(capture["name"])
        capture_through(int(scenario["execution"]["max_ticks"]))
        convergence_start_tick = max(
            (
                int(entry["at_tick"]) + int(entry["duration_ticks"])
                for entry in scenario.get("timeline", [])
            ),
            default=0,
        )
        for assertion in scenario.get("assertions", []):
            if not isinstance(assertion, dict) or assertion.get("type") != "client_converged":
                continue
            convergence_deadline_tick = (
                convergence_start_tick + int(assertion["within_ticks"])
            )
            while True:
                sample_state = _request("get_client_state", session, timeout)
                sample_checkpoint = _latest_checkpoint(run_dir)
                sample_tick = sample_checkpoint.get("relative_tick")
                client_samples.append(
                    {
                        "server_relative_tick": sample_tick,
                        "state": sample_state,
                    }
                )
                distance = _client_authority_distance(sample_state)
                if (
                    distance is not None
                    and distance <= float(assertion["tolerance"])
                ):
                    break
                if (
                    not isinstance(sample_tick, int)
                    or sample_tick >= convergence_deadline_tick
                ):
                    break
                client_tick = sample_state.get("client_tick")
                if not isinstance(client_tick, int):
                    break
                _request(
                    "wait_client_tick",
                    session,
                    timeout,
                    min_tick=client_tick + 1,
                )
        stage = "finish"
        for capture in scenario.get("captures", []):
            if not isinstance(capture, dict) or capture["name"] in captured:
                continue
            if "after_event" in capture:
                checkpoint = _checkpoint_for_event(
                    run_dir,
                    capture["after_event"],
                    time.monotonic() + timeout,
                )
                stage = "capture"
                _capture(capture, session, timeout, screenshots)
                matching_events = [
                    event
                    for event in _events(checkpoint)
                    if _event_matches(event, capture["after_event"])
                ]
                screenshots[-1]["trigger"] = {
                    "after_event": capture["after_event"],
                    "trigger_checkpoint_relative_tick": checkpoint.get(
                        "relative_tick"
                    ),
                    "trigger_checkpoint_absolute_tick": checkpoint.get(
                        "absolute_server_tick"
                    ),
                    "events": matching_events[:16],
                }
                captured.add(capture["name"])
        stage = "finish"
        minimum_sequence = max((row["sequence"] for row in acknowledgements if row.get("sequence") is not None), default=0)
        finish_path = run_dir / "finish.request.json"
        temporary_finish = run_dir / f".{finish_path.name}.{run_token}.tmp"
        _json(temporary_finish, {"schema_version": 1, "token": run_token, "minimum_command_sequence": minimum_sequence})
        os.replace(temporary_finish, finish_path)
        deadline = time.monotonic() + timeout
        result_path = run_dir / "result.json"
        while not result_path.is_file() and time.monotonic() < deadline:
            time.sleep(0.02)
        if not result_path.is_file():
            raise LiveScenarioStageError("finish", "server did not write result.json before the deadline")
        result = _wait_json(
            result_path,
            stage="finish",
            deadline=time.monotonic() + timeout,
        )
        client = _request("get_client_state", session, timeout)
        client_samples.append(
            {
                "server_relative_tick": result.get("relative_tick"),
                "state": client,
            }
        )
        if finish_path.is_file():
            _json(
                scenario_dir / "finish-request.json",
                _wait_json(
                    finish_path,
                    stage="finish",
                    deadline=time.monotonic() + timeout,
                ),
            )
        if result_path.is_file(): _json(scenario_dir / "result.json", result)
        else: raise LiveScenarioStageError("finish", "server did not write result.json")
    except Exception as caught:  # Preserve partial evidence before reporting the failure.
        error = str(caught)
    finally:
        if session is not None:
            try:
                cleanup = lg_launch.cleanup_scenario_session(session)
            except Exception as caught:
                cleanup = {"stopped": [], "failures": [str(caught)]}
        result = _read_json(scenario_dir / "result.json", stage="result") if (scenario_dir / "result.json").is_file() else {}
        server_assertions = result.get("assertions", []) if isinstance(result.get("assertions", []), list) else []
        phase_two = _live_assertions(
            scenario,
            client,
            session or {},
            acknowledgements,
            screenshots,
            result,
            client_samples,
        ) if scenario else []
        assertions = {"server": server_assertions, "live": phase_two}
        _json(scenario_dir / "assertions.json", assertions)
        _json(scenario_dir / "authoritative-events.json", result.get("events_since_setup", result.get("events", _events(checkpoint))))
        _json(scenario_dir / "client-events.json", [row.get("ack", {}) for row in acknowledgements])
        _json(scenario_dir / "client-samples.json", client_samples)
        _json(scenario_dir / "server-state.json", checkpoint)
        _json(scenario_dir / "client-state.json", client)
        _json(scenario_dir / "network-decisions.json", client.get("network_simulation", {}))
        _json(scenario_dir / "reconciliation.json", {key: client.get(key) for key in ("pending_command_count", "correction_count", "last_correction_distance", "interpolation")})
        checkpoint_hashes = {}
        for item in run_dir.glob("checkpoint-*.json"):
            try:
                recorded = _read_json(item, stage="hashes")
                checkpoint_hashes[item.stem] = recorded.get("state_hash")
            except LiveScenarioError:
                continue
        _json(scenario_dir / "hashes.json", {"result_state_hash": result.get("state_hash"), "checkpoints": checkpoint_hashes, "reported": result.get("hashes", {})})
        _json(scenario_dir / "process-log.json", {"session": session or {}, "cleanup": cleanup, "stage": stage})
        for key, name in (("server_stdout", "server.stdout.log"), ("server_stderr", "server.stderr.log"), ("client_stdout", "client.stdout.log"), ("client_stderr", "client.stderr.log")):
            _copy((session or {}).get("logs", {}).get(key) or run_dir / name, scenario_dir / name)
        for shot in screenshots:
            _copy(shot.get("result", {}).get("path"), scenario_dir / "screenshots" / f"{shot['name']}.png")
        failed = [row for row in phase_two if row.get("status") == "failed"]
        failed_server = [
            row for row in server_assertions
            if isinstance(row, dict) and row.get("passed") is False
        ]
        server_result_failed = bool(result) and result.get("passed") is False
        junit_assertions = list(phase_two) + [
            {
                "type": row.get("type", "authoritative"),
                "status": "passed" if row.get("passed") else "failed",
                "message": row.get("message", ""),
            }
            for row in server_assertions
            if isinstance(row, dict)
        ]
        if server_result_failed and not failed_server:
            junit_assertions.append(
                {
                    "type": "authoritative_server_result",
                    "status": "failed",
                    "message": str(
                        result.get("completion_reason", "server reported failure")
                    ),
                }
            )
        status = (
            "failed"
            if (
                error
                or failed
                or failed_server
                or server_result_failed
                or cleanup.get("failures")
            )
            else "passed"
        )
        summary = {"scenario": scenario.get("name", scenario_path.stem), "status": status, "stage": stage,
                   "error": error, "runtime_seconds": time.monotonic() - started, "artifact_path": str(run_dir),
                   "live_failed": len(failed),
                   "authoritative_failed": len(failed_server) + int(server_result_failed and not failed_server),
                   "cleanup": cleanup}
        _json(run_dir / "manifest.json", {"schema_version": 1, "run_token": run_token, "scenario_path": str(scenario_path), "session": session or {}})
        _json(run_dir / "environment.json", {"renderer": renderer, "allow_fallback": allow_fallback, "build_dir": str(build), "session": session or {}})
        _json(run_dir / "summary.json", summary)
        (run_dir / "junit.xml").write_text(
            _junit(summary["scenario"], junit_assertions, error),
            encoding="utf-8",
        )
    if summary["status"] != "passed":
        raise LiveScenarioError(f"{summary['stage']}: {summary['error'] or 'live assertion or cleanup failed'}; artifacts: {run_dir}")
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run one LG Duel live client/server scenario")
    parser.add_argument("scenario")
    parser.add_argument("--output-root", default=str(DEFAULT_OUTPUT_ROOT))
    parser.add_argument("--renderer", choices=("gpu", "fallback"), default="gpu")
    parser.add_argument("--allow-fallback", action="store_true")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--build-dir", default=str(DEFAULT_BUILD_DIR))
    arguments = parser.parse_args(argv)
    if arguments.renderer == "fallback" and not arguments.allow_fallback:
        parser.error("--renderer fallback requires --allow-fallback")
    try:
        result = run_live_scenario(
            arguments.scenario, arguments.output_root, renderer=arguments.renderer,
            allow_fallback=arguments.allow_fallback, timeout=arguments.timeout,
            build_dir=arguments.build_dir,
        )
    except LiveScenarioError as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
